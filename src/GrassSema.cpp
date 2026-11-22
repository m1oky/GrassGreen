#include "GrassSema.h"
#include <algorithm>

namespace grass::sema {

GrassSema::GrassSema() {
    registerBuiltins();
}

void GrassSema::registerBuiltins() {
    auto add_builtin = [this](const std::string& name, const SemaType& ret, const std::vector<SemaType>& params, bool is_variadic = false) {
        FunctionSymbol sym;
        sym.name = name;
        sym.return_type = ret;
        sym.loc = SourceLocation("<builtin>", 0, 0);
        sym.is_builtin = true;
        sym.has_body = true;
        sym.is_variadic = is_variadic;
        for (size_t i = 0; i < params.size(); ++i) {
            sym.params.emplace_back("arg" + std::to_string(i), params[i]);
        }
        functions_[name] = sym;
    };

    // Standard I/O and utility
    add_builtin("print", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("println", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("len", SemaType::Int(), {SemaType::Any()});
    add_builtin("discor", SemaType::Float(), {SemaType::Float()});

    // Math functions
    add_builtin("sqrt", SemaType::Float(), {SemaType::Float()});
    add_builtin("sin", SemaType::Float(), {SemaType::Float()});
    add_builtin("cos", SemaType::Float(), {SemaType::Float()});
    add_builtin("abs", SemaType::Float(), {SemaType::Float()});
    add_builtin("round", SemaType::Float(), {SemaType::Float()});
    add_builtin("floor", SemaType::Float(), {SemaType::Float()});
    add_builtin("ceil", SemaType::Float(), {SemaType::Float()});
    add_builtin("pow", SemaType::Float(), {SemaType::Float(), SemaType::Float()});
    add_builtin("min", SemaType::Float(), {SemaType::Float(), SemaType::Float()});
    add_builtin("max", SemaType::Float(), {SemaType::Float(), SemaType::Float()});
    add_builtin("rand", SemaType::Int(), {}, true);
    add_builtin("lerp", SemaType::Float(), {SemaType::Float(), SemaType::Float(), SemaType::Float()});

    // Time functions
    add_builtin("start_time", SemaType::Void(), {});
    add_builtin("stop_time", SemaType::Float(), {});
    add_builtin("conclus_time", SemaType::String(), {});
    add_builtin("clock_ms", SemaType::Int(), {});
    add_builtin("now_ms", SemaType::Int(), {});
    add_builtin("get_date", SemaType::String(), {});
    add_builtin("get_time_str", SemaType::String(), {});

    // CoreMath and Crypto
    add_builtin("sha256", SemaType::String(), {SemaType::Any()});
    add_builtin("pow_mod", SemaType::Int(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("crypto_rand", SemaType::Int(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("pi", SemaType::String(), {});
    add_builtin("gcd", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("pow_e", SemaType::String(), {SemaType::Float(), SemaType::Float()});
    add_builtin("qb_init", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("h", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("x", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("z", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("cnot", SemaType::String(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("q_measure", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("q_rand", SemaType::Int(), {}, true);

    // File system (fsys)
    add_builtin("fs_mkdir", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_is_dir", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_size", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_list", SemaType::String(), {SemaType::String()});

    // System memory
    add_builtin("memory_free", SemaType::String(), {});
    add_builtin("memory_usit", SemaType::Float(), {});
    add_builtin("memory_alloc", SemaType::Int(), {SemaType::Int()});
    add_builtin("memory_free_block", SemaType::Int(), {SemaType::Int()});

    // Memory management
    add_builtin("alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("free", SemaType::Void(), {SemaType::Ptr(SemaType::Void())});
}

void GrassSema::enterScope(Scope::Kind kind) {
    scope_pool_.push_back(std::make_unique<Scope>(kind, current_scope_));
    current_scope_ = scope_pool_.back().get();
}

void GrassSema::exitScope() {
    if (current_scope_) {
        current_scope_ = current_scope_->parent;
    }
}

void GrassSema::reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Error, loc, msg, hint);
}

void GrassSema::reportWarning(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Warning, loc, msg, hint);
}

bool GrassSema::hasErrors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

void GrassSema::printDiagnostics() const {
    for (const auto& d : diagnostics_) {
        std::cerr << d.format() << std::endl;
    }
}

const FunctionSymbol* GrassSema::findFunction(const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) return &it->second;
    return nullptr;
}

const StructSymbol* GrassSema::findStruct(const std::string& name) const {
    auto it = structs_.find(name);
    if (it != structs_.end()) return &it->second;
    return nullptr;
}

SemaType GrassSema::resolveType(const std::string& type_name, int ptr_depth, const SourceLocation& loc) {
    SemaType base;
    if (type_name == "int" || type_name == "i32" || type_name == "i64" ||
        type_name == "int32_t" || type_name == "int64_t" ||
        type_name == "uint32_t" || type_name == "uint64_t" ||
        type_name == "u32" || type_name == "u64" ||
        type_name == "u8" || type_name == "u16" || type_name == "i8" || type_name == "i16" ||
        type_name == "uint8_t" || type_name == "uint16_t" ||
        type_name == "int8_t" || type_name == "int16_t" ||
        type_name == "char" || type_name == "byte" || type_name == "size_t") {
        base = SemaType::Int();
    } else if (type_name == "float" || type_name == "double" || type_name == "f32" || type_name == "f64") {
        base = SemaType::Float();
    } else if (type_name == "bool") {
        base = SemaType::Bool();
    } else if (type_name == "string") {
        base = SemaType::String();
    } else if (type_name == "void") {
        base = SemaType::Void();
    } else if (type_name == "var" || type_name == "auto" || type_name == "take" || type_name.empty()) {
        base = SemaType::Any();
    } else {
        // Struct or class type
        if (structs_.count(type_name)) {
            base = SemaType::CustomStruct(type_name);
        } else {
            // Forward declaration or unknown type
            base = SemaType::CustomStruct(type_name);
        }
    }

    if (ptr_depth > 0) {
        SemaType ptr = base;
        for (int i = 0; i < ptr_depth; ++i) {
            ptr = SemaType::Ptr(ptr);
        }
        return ptr;
    }
    return base;
}

bool GrassSema::analyze(const ProgramAST& program) {
    diagnostics_.clear();
    scope_pool_.clear();
    scope_pool_.reserve(program.declarations.size() + 16);
    functions_.reserve(program.declarations.size() + 16);
    current_scope_ = nullptr;
    current_function_ = nullptr;
    current_struct_ = nullptr;

    pass1_registerDeclarations(program);
    pass2_checkProgram(program);

    return !hasErrors();
}

void GrassSema::pass1_registerDeclarations(const ProgramAST& program) {
    // 1. Register Structs and Classes
    for (const auto& decl : program.declarations) {
        if (decl->node_kind == NodeKind::StructDecl) {
            auto st = static_cast<const StructDeclAST*>(decl.get());
            if (structs_.count(st->name)) {
                reportError(st->loc, "Redeclaration of struct '" + st->name + "'.", "Struct names must be unique");
                continue;
            }
            StructSymbol sym;
            sym.name = st->name;
            sym.loc = st->loc;
            for (const auto& f : st->fields) {
                if (sym.fields.count(f.name)) {
                    reportError(f.loc, "Duplicate field '" + f.name + "' in struct '" + st->name + "'.", "Field names inside struct must be unique");
                }
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : st->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.struct_name = st->name;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                sym.methods[m->name] = msym;
            }
            structs_[st->name] = sym;
        } else if (decl->node_kind == NodeKind::ClassDecl) {
            auto cls = static_cast<const ClassDeclAST*>(decl.get());
            if (structs_.count(cls->name)) {
                reportError(cls->loc, "Redeclaration of class '" + cls->name + "'.", "Class names must be unique");
                continue;
            }
            StructSymbol sym;
            sym.name = cls->name;
            sym.loc = cls->loc;
            sym.base_class = cls->base_class;
            for (const auto& f : cls->fields) {
                if (sym.fields.count(f.name)) {
                    reportError(f.loc, "Duplicate field '" + f.name + "' in class '" + cls->name + "'.", "Field names inside class must be unique");
                }
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : cls->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.struct_name = cls->name;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                sym.methods[m->name] = msym;
            }
            structs_[cls->name] = sym;
        } else if (decl->node_kind == NodeKind::ModuleDecl) {
            auto mod = static_cast<const ModuleDeclAST*>(decl.get());
            if (modules_.count(mod->name)) {
                reportError(mod->loc, "Redeclaration of module '" + mod->name + "'.", "Module names must be unique");
                continue;
            }
            ModuleSymbol msym;
            msym.name = mod->name;
            msym.loc = mod->loc;
            modules_[mod->name] = msym;
        }
    }

    // 2. Register Global Functions
    for (const auto& decl : program.declarations) {
        if (decl->node_kind == NodeKind::FunctionDecl) {
            auto fn = static_cast<const FunctionDeclAST*>(decl.get());
            if (functions_.count(fn->name)) {
                reportError(fn->loc, "Redeclaration of function '" + fn->name + "'.", "Function names must be unique");
                continue;
            }
            FunctionSymbol sym;
            sym.name = fn->name;
            sym.return_type = resolveType(fn->return_type, fn->return_pointer_depth, fn->loc);
            sym.loc = fn->loc;
            sym.has_body = (fn->body != nullptr);
            for (const auto& p : fn->params) {
                sym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
            }
            functions_[fn->name] = sym;
        }
    }
}

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
        default:
            break;
    }
}

void GrassSema::checkFunctionDecl(const FunctionDeclAST* func) {
    auto it = functions_.find(func->name);
    if (it == functions_.end()) return;
    const FunctionSymbol& sym = it->second;

    current_function_ = &sym;
    enterScope(Scope::Kind::Function);

    // Declare parameters in function scope
    for (const auto& param : sym.params) {
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
        if (!sym.return_type.is_void() && !sym.return_type.is_any()) {
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
    auto it = structs_.find(st->name);
    if (it == structs_.end()) return;
    current_struct_ = &it->second;

    for (const auto& m : st->methods) {
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

    // Check base class existence
    if (!cls->base_class.empty() && !structs_.count(cls->base_class)) {
        reportError(cls->loc, "Base class '" + cls->base_class + "' not found for class '" + cls->name + "'.", "Declare base class before inheriting");
    }

    for (const auto& m : cls->methods) {
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

    if (vdecl->init_expr) {
        SemaType init_type = checkExpr(vdecl->init_expr.get());
        if (decl_type.is_any()) {
            decl_type = init_type;
        } else if (!init_type.is_assignable_to(decl_type)) {
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
    current_scope_->declareVariable(sym);
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
    if (!val_type.is_assignable_to(tgt_type)) {
        reportError(assign->loc,
            "Cannot assign value of type '" + val_type.toString() + "' to variable of type '" + tgt_type.toString() + "'.",
            "Ensure types match in assignment");
    }
}

void GrassSema::checkIfStmt(const IfStmtAST* if_stmt) {
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

SemaType GrassSema::checkExpr(const ExprAST* expr) {
    if (!expr) return SemaType::Unknown();

    switch (expr->node_kind) {
        case NodeKind::LiteralExpr:
            return checkLiteralExpr(static_cast<const LiteralExprAST*>(expr));
        case NodeKind::VariableExpr:
            return checkVariableExpr(static_cast<const VariableExprAST*>(expr));
        case NodeKind::BinaryExpr:
            return checkBinaryExpr(static_cast<const BinaryExprAST*>(expr));
        case NodeKind::UnaryExpr:
            return checkUnaryExpr(static_cast<const UnaryExprAST*>(expr));
        case NodeKind::PostfixExpr:
            return checkPostfixExpr(static_cast<const PostfixExprAST*>(expr));
        case NodeKind::CallExpr:
            return checkCallExpr(static_cast<const CallExprAST*>(expr));
        case NodeKind::MemberAccessExpr:
            return checkMemberAccessExpr(static_cast<const MemberAccessExprAST*>(expr));
        case NodeKind::IndexExpr:
            return checkIndexExpr(static_cast<const IndexExprAST*>(expr));
        case NodeKind::ArrayLiteralExpr:
            return checkArrayLiteralExpr(static_cast<const ArrayLiteralExprAST*>(expr));
        case NodeKind::AllocExpr:
            return checkAllocExpr(static_cast<const AllocExprAST*>(expr));
        case NodeKind::FreeExpr:
            return checkFreeExpr(static_cast<const FreeExprAST*>(expr));
        default:
            return SemaType::Unknown();
    }
}

SemaType GrassSema::checkLiteralExpr(const LiteralExprAST* lit) {
    switch (lit->lit_kind) {
        case LiteralKind::Int: return SemaType::Int();
        case LiteralKind::Float: return SemaType::Float();
        case LiteralKind::String: return SemaType::String();
        case LiteralKind::Bool: return SemaType::Bool();
        case LiteralKind::Nil: return SemaType(SemaTypeKind::Pointer, "void", 1);
    }
    return SemaType::Unknown();
}

SemaType GrassSema::checkVariableExpr(const VariableExprAST* var) {
    if (var->name == "endl") return SemaType::String();
    VariableSymbol* sym = current_scope_->lookupVariable(var->name);
    if (!sym) {
        // Check if it's a field of 'this' when inside a method
        if (current_struct_) {
            auto fit = current_struct_->fields.find(var->name);
            if (fit != current_struct_->fields.end()) return fit->second;
        }
        reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
        return SemaType::Unknown();
    }
    return sym->type;
}

SemaType GrassSema::checkBinaryExpr(const BinaryExprAST* bin) {
    SemaType lhs = checkExpr(bin->lhs.get());
    SemaType rhs = checkExpr(bin->rhs.get());

    switch (bin->op) {
        case TOK_PLUS:
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_string() || rhs.is_string()) return SemaType::String();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || lhs.is_unknown() || rhs.is_unknown()) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '+': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands must be numeric or strings for concatenation");
            return SemaType::Unknown();

        case TOK_MINUS:
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_MOD:
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (bin->op == TOK_MOD) return SemaType::Int();
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || lhs.is_unknown() || rhs.is_unknown()) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '" + bin->op_str + "': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands of arithmetic operations must be numeric");
            return SemaType::Unknown();

        case TOK_AMP:
        case TOK_PIPE:
        case TOK_CARET:
        case TOK_SHL:
        case TOK_SHR:
            if ((!lhs.is_integral() && !lhs.is_any() && !lhs.is_unknown()) ||
                (!rhs.is_integral() && !rhs.is_any() && !rhs.is_unknown())) {
                reportError(bin->loc,
                    "Bitwise operator '" + bin->op_str + "' requires integral operands, but got '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Bitwise operations are only valid on integer types");
                return SemaType::Unknown();
            }
            return SemaType::Int();

        case TOK_LT:
        case TOK_GT:
        case TOK_LTE:
        case TOK_GTE:
            if ((!lhs.is_numeric() && !lhs.is_any() && !lhs.is_unknown()) ||
                (!rhs.is_numeric() && !rhs.is_any() && !rhs.is_unknown())) {
                reportError(bin->loc,
                    "Comparison operator '" + bin->op_str + "' requires numeric operands, but got '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Compare numeric expressions");
            }
            return SemaType::Bool();

        case TOK_EQ:
        case TOK_NEQ:
            if (!lhs.is_assignable_to(rhs) && !rhs.is_assignable_to(lhs)) {
                reportError(bin->loc,
                    "Cannot compare incompatible types '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Operands of '==' and '!=' must have matching types");
            }
            return SemaType::Bool();

        case TOK_AND:
        case TOK_OR:
            return SemaType::Bool();

        default:
            return SemaType::Unknown();
    }
}

SemaType GrassSema::checkUnaryExpr(const UnaryExprAST* un) {
    SemaType operand_type = checkExpr(un->operand.get());

    switch (un->op) {
        case TOK_MINUS:
            if (!operand_type.is_numeric() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Unary '-' requires numeric operand, got '" + operand_type.toString() + "'.", "Apply unary '-' only to numbers");
                return SemaType::Unknown();
            }
            return operand_type;

        case TOK_NOT:
            return SemaType::Bool();

        case TOK_TILDE:
            if (!operand_type.is_integral() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Bitwise not '~' requires integral operand, got '" + operand_type.toString() + "'.", "Apply bitwise '~' only to integers");
                return SemaType::Unknown();
            }
            return SemaType::Int();

        case TOK_STAR: // Dereference *ptr
            if (!operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Cannot dereference non-pointer type '" + operand_type.toString() + "'.", "Operand of unary '*' must be a pointer");
                return SemaType::Unknown();
            }
            if (operand_type.is_pointer() && operand_type.element_type) {
                return *operand_type.element_type;
            }
            return SemaType::Int(); // Default dereferenced type for untyped pointer

        case TOK_AMP: // Address-of &var
            if (un->operand &&
                un->operand->node_kind != NodeKind::VariableExpr &&
                un->operand->node_kind != NodeKind::MemberAccessExpr &&
                un->operand->node_kind != NodeKind::IndexExpr) {
                reportError(un->loc, "Cannot take address of non-lvalue expression.", "Operand of unary '&' must be an lvalue");
                return SemaType::Unknown();
            }
            return SemaType::Ptr(operand_type);

        case TOK_INC:
        case TOK_DEC:
            if (!operand_type.is_numeric() && !operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Increment/decrement requires numeric or pointer operand.", "Apply '++'/'--' to numeric variable");
                return SemaType::Unknown();
            }
            return operand_type;

        default:
            return operand_type;
    }
}

SemaType GrassSema::checkPostfixExpr(const PostfixExprAST* post) {
    SemaType operand_type = checkExpr(post->operand.get());
    if (!operand_type.is_numeric() && !operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
        reportError(post->loc, "Postfix operator requires numeric or pointer operand.", "Apply '++'/'--' to numeric variable");
        return SemaType::Unknown();
    }
    return operand_type;
}

SemaType GrassSema::checkCallExpr(const CallExprAST* call) {
    // Check alloc and free builtins called via call expression
    if (call->callee == "alloc") {
        if (call->args.size() != 1) {
            reportError(call->loc, "Function 'alloc' expects 1 argument, but " + std::to_string(call->args.size()) + " were provided.", "Pass size in bytes to alloc()");
        } else {
            SemaType sz_type = checkExpr(call->args[0].get());
            if (!sz_type.is_integral() && !sz_type.is_any() && !sz_type.is_unknown()) {
                reportError(call->loc, "Argument to 'alloc()' must be an integer, got '" + sz_type.toString() + "'.", "Pass size in bytes as an integer");
            }
        }
        return SemaType::Ptr(SemaType::Void());
    }

    if (call->callee == "free") {
        if (call->args.size() != 1) {
            reportError(call->loc, "Function 'free' expects 1 argument, but " + std::to_string(call->args.size()) + " were provided.", "Pass pointer to free()");
        } else {
            SemaType tgt_type = checkExpr(call->args[0].get());
            if (!tgt_type.is_pointer() && !tgt_type.is_any() && !tgt_type.is_unknown()) {
                reportError(call->loc, "Argument to 'free()' must be a pointer, got '" + tgt_type.toString() + "'.", "Pass a pointer previously allocated with alloc()");
            }
        }
        return SemaType::Void();
    }

    const FunctionSymbol* func = findFunction(call->callee);
    if (!func) {
        // Check if it's a struct constructor: Point()
        if (structs_.count(call->callee)) {
            return SemaType::CustomStruct(call->callee);
        }
        reportError(call->loc, "Call to undeclared function '" + call->callee + "'.", "Check spelling or declare function before calling");
        return SemaType::Unknown();
    }

    // Argument count check
    if (!func->is_variadic && call->args.size() != func->params.size()) {
        reportError(call->loc,
            "Function '" + call->callee + "' expects " + std::to_string(func->params.size()) +
            " arguments, but " + std::to_string(call->args.size()) + " were provided.",
            "Ensure argument count matches function declaration");
    }

    // Argument type check
    size_t count = std::min(call->args.size(), func->params.size());
    for (size_t i = 0; i < count; ++i) {
        SemaType arg_type = checkExpr(call->args[i].get());
        const SemaType& param_type = func->params[i].second;
        if (!arg_type.is_assignable_to(param_type)) {
            reportError(call->args[i]->loc,
                "Argument " + std::to_string(i + 1) + " of function '" + call->callee + "' expected type '" +
                param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                "Ensure argument type matches expected parameter type");
        }
    }

    return func->return_type;
}

SemaType GrassSema::checkMemberAccessExpr(const MemberAccessExprAST* mem) {
    SemaType obj_type = checkExpr(mem->object.get());

    // If pointer to struct, dereference
    if (obj_type.is_pointer() && obj_type.element_type) {
        obj_type = *obj_type.element_type;
    }

    if (!obj_type.is_struct() && !obj_type.is_any() && !obj_type.is_unknown()) {
        reportError(mem->loc,
            "Member access '" + mem->member + "' on non-struct type '" + obj_type.toString() + "'.",
            "Only struct and class objects have members");
        return SemaType::Unknown();
    }

    if (obj_type.is_any() || obj_type.is_unknown()) return SemaType::Any();

    const StructSymbol* st = findStruct(obj_type.name);
    if (!st) {
        reportError(mem->loc, "Undefined struct '" + obj_type.name + "'.", "Declare struct before using it");
        return SemaType::Unknown();
    }

    // Helper lambda to search fields across inheritance hierarchy
    auto find_field = [this](const StructSymbol* s, const std::string& field_name, auto& self) -> const SemaType* {
        auto fit = s->fields.find(field_name);
        if (fit != s->fields.end()) return &fit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, field_name, self);
        }
        return nullptr;
    };

    // Helper lambda to search methods across inheritance hierarchy
    auto find_method = [this](const StructSymbol* s, const std::string& method_name, auto& self) -> const FunctionSymbol* {
        auto mit = s->methods.find(method_name);
        if (mit != s->methods.end()) return &mit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, method_name, self);
        }
        return nullptr;
    };

    if (mem->is_method_call) {
        const FunctionSymbol* method = find_method(st, mem->member, find_method);
        if (!method) {
            reportError(mem->loc,
                "Struct/Class '" + obj_type.name + "' has no method named '" + mem->member + "'.",
                "Check method name or declare method in struct");
            return SemaType::Unknown();
        }

        if (mem->method_args.size() != method->params.size()) {
            reportError(mem->loc,
                "Method '" + mem->member + "' expects " + std::to_string(method->params.size()) +
                " arguments, but " + std::to_string(mem->method_args.size()) + " were provided.",
                "Ensure argument count matches method declaration");
        }

        size_t count = std::min(mem->method_args.size(), method->params.size());
        for (size_t i = 0; i < count; ++i) {
            SemaType arg_type = checkExpr(mem->method_args[i].get());
            const SemaType& param_type = method->params[i].second;
            if (!arg_type.is_assignable_to(param_type)) {
                reportError(mem->method_args[i]->loc,
                    "Argument " + std::to_string(i + 1) + " of method '" + mem->member + "' expected type '" +
                    param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                    "Ensure argument type matches expected parameter type");
            }
        }

        return method->return_type;
    } else {
        const SemaType* ftype = find_field(st, mem->member, find_field);
        if (!ftype) {
            reportError(mem->loc,
                "Struct '" + obj_type.name + "' has no field named '" + mem->member + "'.",
                "Check struct declaration for available fields");
            return SemaType::Unknown();
        }
        return *ftype;
    }
}

SemaType GrassSema::checkIndexExpr(const IndexExprAST* idx) {
    SemaType target_type = checkExpr(idx->target.get());
    SemaType idx_type = checkExpr(idx->index.get());

    if (!idx_type.is_integral() && !idx_type.is_any() && !idx_type.is_unknown()) {
        reportError(idx->loc, "Array index must be integer, got '" + idx_type.toString() + "'.", "Use integer value for indexing");
    }

    if (target_type.is_pointer() && target_type.element_type) {
        return *target_type.element_type;
    }
    if (target_type.is_string()) {
        return SemaType::Int(); // char
    }
    return SemaType::Any();
}

SemaType GrassSema::checkArrayLiteralExpr(const ArrayLiteralExprAST* arr) {
    SemaType elem_type = SemaType::Any();
    for (const auto& elem : arr->elements) {
        checkExpr(elem.get());
    }
    return SemaType(SemaTypeKind::Array, elem_type.name);
}

SemaType GrassSema::checkAllocExpr(const AllocExprAST* alloc) {
    if (alloc->size_expr) {
        SemaType sz_type = checkExpr(alloc->size_expr.get());
        if (!sz_type.is_integral() && !sz_type.is_any() && !sz_type.is_unknown()) {
            reportError(alloc->loc, "Argument to 'alloc()' must be an integer, got '" + sz_type.toString() + "'.", "Pass size in bytes as an integer");
        }
    }
    return SemaType::Ptr(resolveType(alloc->type_name, 0, alloc->loc));
}

SemaType GrassSema::checkFreeExpr(const FreeExprAST* free_expr) {
    SemaType tgt_type = checkExpr(free_expr->target.get());
    if (!tgt_type.is_pointer() && !tgt_type.is_any() && !tgt_type.is_unknown()) {
        reportError(free_expr->loc, "Argument to 'free()' must be a pointer, got '" + tgt_type.toString() + "'.", "Pass a pointer previously allocated with alloc()");
    }
    return SemaType::Void();
}

} // namespace grass::sema
