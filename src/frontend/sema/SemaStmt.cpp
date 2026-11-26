#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

void GrassSema::pass2_checkProgram(const ProgramAST& program) {
    enterScope(Scope::Kind::Global);

    // 1. Check top-level statements
    for (const auto& stmt : program.statements) {
        checkStmt(stmt.get());
    }

    // 2. Check function bodies
    for (const auto& decl : program.declarations) {
        checkDecl(decl.get());
    }

    exitScope();
}

void GrassSema::checkDecl(const DeclAST* decl) {
    if (!decl) return;
    switch (decl->node_kind) {
        case NodeKind::FunctionDecl:
            checkFunctionDecl(static_cast<const FunctionDeclAST*>(decl));
            break;
        case NodeKind::StructDecl:
            checkStructDecl(static_cast<const StructDeclAST*>(decl));
            break;
        case NodeKind::ClassDecl:
            checkClassDecl(static_cast<const ClassDeclAST*>(decl));
            break;
        case NodeKind::ModuleDecl:
            checkModuleDecl(static_cast<const ModuleDeclAST*>(decl));
            break;
        case NodeKind::NamespaceDecl:
            checkNamespaceDecl(static_cast<const NamespaceDeclAST*>(decl));
            break;
        case NodeKind::UsingDecl:
            checkUsingDecl(static_cast<const UsingDeclAST*>(decl));
            break;
        default:
            break;
    }
}

void GrassSema::checkNamespaceDecl(const NamespaceDeclAST* ns) {
    namespace_stack_.push_back(ns->name);
    for (const auto& stmt : ns->statements) {
        checkStmt(stmt.get());
    }
    for (const auto& decl : ns->declarations) {
        checkDecl(decl.get());
    }
    namespace_stack_.pop_back();
}

void GrassSema::checkUsingDecl(const UsingDeclAST* u) {
    if (u->is_namespace) {
        using_namespaces_.push_back(u->path);
    } else {
        size_t last_col = u->path.rfind("::");
        std::string sname = (last_col != std::string::npos) ? u->path.substr(last_col + 2) : u->path;
        using_symbols_[sname] = u->path;
    }
}

void GrassSema::checkModuleDecl(const ModuleDeclAST* mod) {
    for (const auto& st : mod->structs) checkStructDecl(st.get());
    for (const auto& cls : mod->classes) checkClassDecl(cls.get());
    for (const auto& fn : mod->functions) checkFunctionDecl(fn.get());
}

void GrassSema::checkFunctionDecl(const FunctionDeclAST* func) {
    std::string qname = qualifyName(func->name);
    auto it = functions_.find(qname);
    if (it == functions_.end()) it = functions_.find(mangleName(qname));
    if (it == functions_.end()) it = functions_.find(func->name);
    if (it == functions_.end()) return;
    const FunctionSymbol& sym = it->second;

    current_function_ = &sym;
    enterScope(Scope::Kind::Function);

    // Declare parameters in function scope
    for (const auto& param : sym.params) {
        if (param.second.is_vec()) {
            reportError(func->loc,
                "Parameter '" + param.first + "' of type '" + param.second.toString() +
                "' cannot be passed by value (implicit shallow copy is forbidden to prevent double-free).",
                "Pass by pointer '" + param.second.toString() + "*' instead");
        }
        VariableSymbol vsym;
        vsym.name = param.first;
        vsym.type = param.second;
        vsym.loc = func->loc;
        vsym.is_initialized = true;
        if (!current_scope_->declareVariable(vsym)) {
            reportError(func->loc, "Duplicate parameter name '" + param.first + "' in function '" + func->name + "'.", "Parameter names must be unique");
        }
    }

    if (func->body) {
        checkBlockStmt(func->body.get());

        // Check return on all control paths for non-void functions
        if (!sym.return_type.is_void() && !sym.return_type.is_any() && (func->name != "main" || strict_mode_)) {
            if (!returnsOnAllPaths(func->body.get())) {
                reportError(func->loc,
                    "Not all control paths return a value in function '" + func->name + "'.",
                    "Ensure every branch returns a value of type '" + sym.return_type.toString() + "'");
            }
        }
    }

    exitScope();
    current_function_ = nullptr;
}

void GrassSema::checkStructDecl(const StructDeclAST* st) {
    auto it = structs_.find(qualifyName(st->name));
    if (it == structs_.end()) it = structs_.find(st->name);
    if (it == structs_.end()) return;
    current_struct_ = &it->second;

    int dtor_count = 0;
    for (const auto& m : st->methods) {
        if (m->is_destructor) {
            dtor_count++;
            if (dtor_count > 1) {
                reportError(m->loc,
                    "Struct '" + st->name + "' cannot declare more than one destructor.",
                    "Keep at most one destructor declaration per struct");
            }
            std::string dname = (!m->name.empty() && m->name[0] == '~') ? m->name.substr(1) : m->name;
            if (dname != st->name) {
                reportError(m->loc,
                    "Destructor name '~" + dname + "' does not match struct name '" + st->name + "'.",
                    "Rename destructor to '~" + st->name + "'");
            }
            if (!m->params.empty()) {
                reportError(m->loc,
                    "Destructor '~" + st->name + "()' cannot take any parameters.",
                    "Remove parameters from '~" + st->name + "()'");
            }
        }
        auto mit = current_struct_->methods.find(m->name);
        if (mit == current_struct_->methods.end()) continue;
        const FunctionSymbol& msym = mit->second;

        current_function_ = &msym;
        enterScope(Scope::Kind::Function);

        // Add 'this' pointer in struct method scope
        VariableSymbol this_sym;
        this_sym.name = "this";
        this_sym.type = SemaType::CustomStruct(st->name);
        this_sym.loc = m->loc;
        this_sym.is_initialized = true;
        current_scope_->declareVariable(this_sym);

        for (const auto& p : msym.params) {
            VariableSymbol vsym;
            vsym.name = p.first;
            vsym.type = p.second;
            vsym.loc = m->loc;
            vsym.is_initialized = true;
            current_scope_->declareVariable(vsym);
        }

        if (m->body) {
            checkBlockStmt(m->body.get());
            if (!msym.return_type.is_void() && !msym.return_type.is_any()) {
                if (!returnsOnAllPaths(m->body.get())) {
                    reportError(m->loc,
                        "Not all control paths return a value in method '" + m->name + "'.",
                        "Ensure every branch returns a value of type '" + msym.return_type.toString() + "'");
                }
            }
        }

        exitScope();
        current_function_ = nullptr;
    }

    current_struct_ = nullptr;
}

void GrassSema::checkClassDecl(const ClassDeclAST* cls) {
    auto it = structs_.find(cls->name);
    if (it == structs_.end()) return;
    current_struct_ = &it->second;

    // Check base class existence and cycle detection
    if (!cls->base_class.empty()) {
        std::unordered_set<std::string> chain;
        chain.insert(cls->name);
        std::string cur = cls->base_class;
        while (!cur.empty()) {
            if (chain.count(cur)) {
                reportError(cls->loc, "Cyclic inheritance detected involving class '" + cur + "' and '" + cls->name + "'.", "Remove cyclic dependency in class hierarchy");
                break;
            }
            chain.insert(cur);
            const StructSymbol* base_sym = findStruct(cur);
            if (!base_sym) {
                reportError(cls->loc, "Base class '" + cur + "' not found for class '" + cls->name + "'.", "Declare base class before inheriting");
                break;
            }
            cur = base_sym->base_class;
        }
    }

    int dtor_count = 0;
    for (const auto& m : cls->methods) {
        if (m->is_destructor) {
            dtor_count++;
            if (dtor_count > 1) {
                reportError(m->loc,
                    "Class '" + cls->name + "' cannot declare more than one destructor.",
                    "Keep at most one destructor declaration per class");
            }
            std::string dname = (!m->name.empty() && m->name[0] == '~') ? m->name.substr(1) : m->name;
            if (dname != cls->name) {
                reportError(m->loc,
                    "Destructor name '~" + dname + "' does not match class name '" + cls->name + "'.",
                    "Rename destructor to '~" + cls->name + "'");
            }
            if (!m->params.empty()) {
                reportError(m->loc,
                    "Destructor '~" + cls->name + "()' cannot take any parameters.",
                    "Remove parameters from '~" + cls->name + "()'");
            }
        }
        auto mit = current_struct_->methods.find(m->name);
        if (mit == current_struct_->methods.end()) continue;
        const FunctionSymbol& msym = mit->second;

        current_function_ = &msym;
        enterScope(Scope::Kind::Function);

        VariableSymbol this_sym;
        this_sym.name = "this";
        this_sym.type = SemaType::CustomStruct(cls->name);
        this_sym.loc = m->loc;
        this_sym.is_initialized = true;
        current_scope_->declareVariable(this_sym);

        for (const auto& p : msym.params) {
            VariableSymbol vsym;
            vsym.name = p.first;
            vsym.type = p.second;
            vsym.loc = m->loc;
            vsym.is_initialized = true;
            current_scope_->declareVariable(vsym);
        }

        if (m->body) {
            checkBlockStmt(m->body.get());
            if (!msym.return_type.is_void() && !msym.return_type.is_any()) {
                if (!returnsOnAllPaths(m->body.get())) {
                    reportError(m->loc,
                        "Not all control paths return a value in method '" + m->name + "'.",
                        "Ensure every branch returns a value of type '" + msym.return_type.toString() + "'");
                }
            }
        }

        exitScope();
        current_function_ = nullptr;
    }

    current_struct_ = nullptr;
}

bool GrassSema::returnsOnAllPaths(const StmtAST* stmt) {
    if (!stmt) return false;
    switch (stmt->node_kind) {
        case NodeKind::ReturnStmt:
            return true;
        case NodeKind::BlockStmt: {
            auto block = static_cast<const BlockStmtAST*>(stmt);
            for (const auto& s : block->statements) {
                if (s->node_kind == NodeKind::ReturnStmt) return true;
                if (s->node_kind == NodeKind::BlockStmt || s->node_kind == NodeKind::IfStmt) {
                    if (returnsOnAllPaths(s.get())) return true;
                }
            }
            return false;
        }
        case NodeKind::IfStmt: {
            auto if_stmt = static_cast<const IfStmtAST*>(stmt);
            if (!if_stmt->else_branch) return false;
            return returnsOnAllPaths(if_stmt->then_branch.get()) &&
                   returnsOnAllPaths(if_stmt->else_branch.get());
        }
        default:
            return false;
    }
}

void GrassSema::checkStmt(const StmtAST* stmt) {
    if (!stmt) return;
    switch (stmt->node_kind) {
        case NodeKind::AssignStmt:
            checkAssignStmt(static_cast<const AssignStmtAST*>(stmt));
            break;
        case NodeKind::VarDeclStmt:
            checkVarDeclStmt(static_cast<const VarDeclStmtAST*>(stmt));
            break;
        case NodeKind::ExprStmt: {
            auto e = static_cast<const ExprStmtAST*>(stmt);
            checkExpr(e->expr.get());
            break;
        }
        case NodeKind::BlockStmt: {
            auto b = static_cast<const BlockStmtAST*>(stmt);
            enterScope(Scope::Kind::Block);
            checkBlockStmt(b);
            exitScope();
            break;
        }
        case NodeKind::IfStmt:
            checkIfStmt(static_cast<const IfStmtAST*>(stmt));
            break;
        case NodeKind::WhileStmt:
            checkWhileStmt(static_cast<const WhileStmtAST*>(stmt));
            break;
        case NodeKind::ForStmt:
            checkForStmt(static_cast<const ForStmtAST*>(stmt));
            break;
        case NodeKind::ReturnStmt:
            checkReturnStmt(static_cast<const ReturnStmtAST*>(stmt));
            break;
        case NodeKind::StreamIOStmt:
            checkStreamIOStmt(static_cast<const StreamIOStmtAST*>(stmt));
            break;
        default:
            break;
    }
}

void GrassSema::checkBlockStmt(const BlockStmtAST* block) {
    if (!block) return;
    for (const auto& s : block->statements) {
        checkStmt(s.get());
    }
}

void GrassSema::checkVarDeclStmt(const VarDeclStmtAST* vdecl) {
    if (current_scope_->isDeclaredInCurrentScope(vdecl->var_name)) {
        reportError(vdecl->loc,
            "Redeclaration of variable '" + vdecl->var_name + "' in the same scope.",
            "Variables cannot be redeclared in the same block");
        return;
    }

    SemaType decl_type = resolveType(vdecl->type_name, vdecl->pointer_depth, vdecl->loc);

    if (vdecl->type_name == "take" || (strict_mode_ && (vdecl->type_name == "auto" || vdecl->type_name == "var"))) {
        if (!vdecl->init_expr) {
            reportError(vdecl->loc,
                "Variable '" + vdecl->var_name + "' declared with 'take' must have an explicit initializer for type deduction.",
                "Provide initializer expression");
            return;
        }
        SemaType init_type = checkExpr(vdecl->init_expr.get());
        if (init_type.is_void()) {
            reportError(vdecl->loc,
                "Cannot deduce type for 'take' variable '" + vdecl->var_name + "' from 'void' expression.",
                "Initializer expression must return a non-void value");
            return;
        }
        if (init_type.is_vec()) {
            reportError(vdecl->loc,
                "Cannot shallow-copy '" + init_type.toString() + "' into 'take " + vdecl->var_name + "'.",
                "Use a pointer '&" + vdecl->var_name + "' instead of copying a RAII vec");
        }
        decl_type = init_type;
    } else if (vdecl->init_expr) {
        SemaType init_type = checkExpr(vdecl->init_expr.get());
        if (decl_type.is_vec() || init_type.is_vec()) {
            reportError(vdecl->loc,
                "Cannot shallow-copy initialize '" + decl_type.toString() + " " + vdecl->var_name + "'.",
                "Declare 'vec<T>' without initializer or borrow via '" + decl_type.toString() + "*'");
        } else if (decl_type.is_any()) {
            decl_type = init_type;
        } else if (!init_type.is_assignable_to(decl_type, strict_mode_)) {
            reportError(vdecl->loc,
                "Type mismatch in variable initialization: cannot initialize '" + decl_type.toString() +
                "' with value of type '" + init_type.toString() + "'.",
                "Ensure initializer expression matches variable type");
        }
    }

    VariableSymbol sym;
    sym.name = vdecl->var_name;
    sym.type = decl_type;
    sym.loc = vdecl->loc;
    sym.is_const = vdecl->is_const;
    sym.is_initialized = (vdecl->init_expr != nullptr);
    vdecl->inferred_type = decl_type.toString();
    current_scope_->declareVariable(sym);

    if (decl_type.is_vec()) {
        current_scope_->registerRAIIVariable(sym);
    } else if (decl_type.is_struct()) {
        const StructSymbol* st_sym = findStruct(decl_type.name);
        if (st_sym && st_sym->has_destructor) {
            current_scope_->registerRAIIVariable(sym);
        }
    }
}

void GrassSema::checkAssignStmt(const AssignStmtAST* assign) {
    if (!assign || !assign->target) return;

    if (assign->target->node_kind == NodeKind::LiteralExpr) {
        reportError(assign->loc, "Assignment target must be an lvalue.", "Cannot assign to literal value");
        return;
    }

    SemaType tgt_type;
    if (assign->target->node_kind == NodeKind::VariableExpr) {
        auto var = static_cast<const VariableExprAST*>(assign->target.get());
        VariableSymbol* sym = current_scope_->lookupVariable(var->name);
        if (!sym) {
            if (current_struct_) {
                auto fit = current_struct_->fields.find(var->name);
                if (fit != current_struct_->fields.end()) {
                    tgt_type = fit->second;
                } else {
                    reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
                    tgt_type = SemaType::Unknown();
                }
            } else {
                reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
                tgt_type = SemaType::Unknown();
            }
        } else {
            if (sym->is_const) {
                reportError(assign->loc, "Cannot assign to const variable '" + var->name + "'.", "Const variables cannot be reassigned");
            }
            tgt_type = sym->type;
        }
    } else {
        tgt_type = checkExpr(assign->target.get());
    }

    SemaType val_type = checkExpr(assign->value.get());
    if (tgt_type.is_vec() || val_type.is_vec()) {
        reportError(assign->loc,
            "Cannot shallow-copy assign value of type '" + val_type.toString() + "' to '" + tgt_type.toString() + "'.",
            "Use pointers 'vec<T>*' instead of copying RAII vec containers");
    } else if (!val_type.is_assignable_to(tgt_type, strict_mode_)) {
        reportError(assign->loc,
            "Cannot assign value of type '" + val_type.toString() + "' to variable of type '" + tgt_type.toString() + "'.",
            "Ensure types match in assignment");
    }
}

void GrassSema::checkIfStmt(const IfStmtAST* if_stmt) {
    if (!if_stmt) return;
    SemaType cond_type = checkExpr(if_stmt->condition.get());
    if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
        !cond_type.is_any() && !cond_type.is_unknown()) {
        reportError(if_stmt->loc,
            "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
            "Condition must evaluate to bool or integer");
    }
    checkStmt(if_stmt->then_branch.get());
    if (if_stmt->else_branch) {
        checkStmt(if_stmt->else_branch.get());
    }
}

void GrassSema::checkWhileStmt(const WhileStmtAST* while_stmt) {
    if (!while_stmt) return;
    SemaType cond_type = checkExpr(while_stmt->condition.get());
    if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
        !cond_type.is_any() && !cond_type.is_unknown()) {
        reportError(while_stmt->loc,
            "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
            "Condition must evaluate to bool or integer");
    }
    checkStmt(while_stmt->body.get());
}

void GrassSema::checkForStmt(const ForStmtAST* for_stmt) {
    if (!for_stmt) return;
    enterScope(Scope::Kind::Block);
    if (for_stmt->init) checkStmt(for_stmt->init.get());
    if (for_stmt->condition) {
        SemaType cond_type = checkExpr(for_stmt->condition.get());
        if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
            !cond_type.is_any() && !cond_type.is_unknown()) {
            reportError(for_stmt->loc,
                "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
                "Condition must evaluate to bool or integer");
        }
    }
    if (for_stmt->step) checkStmt(for_stmt->step.get());
    checkStmt(for_stmt->body.get());
    exitScope();
}

void GrassSema::checkReturnStmt(const ReturnStmtAST* ret_stmt) {
    if (!current_function_) {
        reportError(ret_stmt->loc, "Return statement outside of function.", "Return statements can only be used inside functions");
        return;
    }

    if (current_function_->return_type.is_void()) {
        if (ret_stmt->value) {
            reportError(ret_stmt->loc,
                "Void function '" + current_function_->name + "' cannot return a value.",
                "Remove return value or change function return type");
        }
    } else {
        if (!ret_stmt->value) {
            reportError(ret_stmt->loc,
                "Non-void function '" + current_function_->name + "' must return a value of type '" + current_function_->return_type.toString() + "'.",
                "Provide return value");
        } else {
            SemaType val_type = checkExpr(ret_stmt->value.get());
            if (!val_type.is_assignable_to(current_function_->return_type)) {
                reportError(ret_stmt->loc,
                    "Function '" + current_function_->name + "' expected return type '" + current_function_->return_type.toString() + "', but got '" + val_type.toString() + "'.",
                    "Return expression must match declared function return type");
            }
        }
    }
}

void GrassSema::checkStreamIOStmt(const StreamIOStmtAST* io_stmt) {
    for (const auto& op : io_stmt->operands) {
        checkExpr(op.get());
    }
}


} // namespace grass::sema
