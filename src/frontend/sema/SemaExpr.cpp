#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

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
        case NodeKind::CastExpr:
            return checkCastExpr(static_cast<const CastExprAST*>(expr));
        case NodeKind::SizeofExpr:
            return checkSizeofExpr(static_cast<const SizeofExprAST*>(expr));
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
        case LiteralKind::Nullptr: return SemaType::Nullptr();
    }
    return SemaType::Unknown();
}

SemaType GrassSema::checkVariableExpr(const VariableExprAST* var) {
    if (var->name == "endl" || var->name == "std::endl") return SemaType::String();
    if (var->name == "BLACK" || var->name == "BLUE" || var->name == "GREEN" ||
        var->name == "CYAN" || var->name == "RED" || var->name == "MAGENTA" ||
        var->name == "BROWN" || var->name == "LIGHT_GRAY" || var->name == "DARK_GRAY" ||
        var->name == "LIGHT_BLUE" || var->name == "LIGHT_GREEN" || var->name == "LIGHT_CYAN" ||
        var->name == "LIGHT_RED" || var->name == "LIGHT_MAGENTA" || var->name == "YELLOW" ||
        var->name == "WHITE") {
        if (!current_scope_->lookupVariable(var->name)) {
            return SemaType::Int();
        }
    }
    if (var->name == "proc_mem") return SemaType::Int();
    VariableSymbol* sym = current_scope_->lookupVariable(var->name);
    if (!sym) {
        // Check if it's a field of 'this' when inside a method
        if (current_struct_) {
            auto fit = current_struct_->fields.find(var->name);
            if (fit != current_struct_->fields.end()) return fit->second;
        }
        // Check if passing a function reference (e.g. thread(fn, ...), attach_sound(fn, ...))
        if (functions_.count(var->name)) {
            return SemaType::Ptr(SemaType::Void());
        }
        reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
        return SemaType::Unknown();
    }
    return sym->type;
}

SemaType GrassSema::checkBinaryExpr(const BinaryExprAST* bin) {
    if (!bin) return SemaType::Unknown();
    SemaType lhs = checkExpr(bin->lhs.get());
    SemaType rhs = checkExpr(bin->rhs.get());

    switch (bin->op) {
        case TOK_PLUS:
            if (lhs.is_pointer() && rhs.is_integral()) return lhs;
            if (rhs.is_pointer() && lhs.is_integral()) return rhs;
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_string() || rhs.is_string()) return SemaType::String();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '+': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands must be numeric or strings for concatenation");
            return SemaType::Unknown();

        case TOK_MINUS:
            if (lhs.is_pointer() && rhs.is_integral()) return lhs;
            if (lhs.is_pointer() && rhs.is_pointer()) {
                if (strict_mode_ && !lhs.equals(rhs) && lhs.name != "void" && rhs.name != "void") {
                    reportError(bin->loc, "Cannot subtract pointers of different types: '" + lhs.toString() + "' and '" + rhs.toString() + "'.");
                }
                return SemaType::Int();
            }
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '-': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands of arithmetic operations must be numeric");
            return SemaType::Unknown();

        case TOK_STAR:
        case TOK_SLASH:
        case TOK_MOD:
            if (strict_mode_ && (lhs.is_pointer() || rhs.is_pointer())) {
                reportError(bin->loc, "Cannot perform multiplication/division on pointer types: '" + lhs.toString() + "' and '" + rhs.toString() + "'.");
                return SemaType::Unknown();
            }
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (bin->op == TOK_MOD) return SemaType::Int();
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
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
            if (lhs.is_pointer() && rhs.is_pointer()) {
                return SemaType::Bool();
            }
            if ((!lhs.is_numeric() && !lhs.is_any() && !lhs.is_unknown()) ||
                (!rhs.is_numeric() && !rhs.is_any() && !rhs.is_unknown())) {
                reportError(bin->loc,
                    "Comparison operator '" + bin->op_str + "' requires numeric or pointer operands, but got '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Compare numeric or pointer expressions");
            }
            return SemaType::Bool();

        case TOK_EQ:
        case TOK_NEQ:
            if (lhs.is_pointer() && rhs.is_pointer()) {
                return SemaType::Bool();
            }
            if ((lhs.is_pointer() && rhs.is_integral()) || (rhs.is_pointer() && lhs.is_integral())) {
                return SemaType::Bool();
            }
            if (strict_mode_ && (lhs.is_pointer() != rhs.is_pointer())) {
                reportError(bin->loc,
                    "Cannot compare pointer and non-pointer types '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Cast or compare matching types");
                return SemaType::Bool();
            }
            if (!lhs.is_assignable_to(rhs, strict_mode_) && !rhs.is_assignable_to(lhs, strict_mode_)) {
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
    if (!un) return SemaType::Unknown();
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
    bool flex_thread_arity = ((call->callee == "thread_join" || call->callee == "mutex_lock") &&
                              (call->args.size() >= 1 && call->args.size() <= 2)) ||
                             (call->callee == "net_close" && call->args.size() <= 1);
    if (!func->is_variadic && !flex_thread_arity && call->args.size() != func->params.size()) {
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
        if (arg_type.is_vec() && !param_type.is_pointer()) {
            reportError(call->args[i]->loc,
                "Cannot pass '" + arg_type.toString() + "' by value to function '" + call->callee + "' (implicit shallow copy is forbidden to prevent double-free).",
                "Pass a pointer using '&' and declare parameter as '" + arg_type.toString() + "*'");
        } else if (!arg_type.is_assignable_to(param_type, strict_mode_)) {
            reportError(call->args[i]->loc,
                "Argument " + std::to_string(i + 1) + " of function '" + call->callee + "' expected type '" +
                param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                "Ensure argument type matches expected parameter type");
        }
    }

    return func->return_type;
}

SemaType GrassSema::checkMemberAccessExpr(const MemberAccessExprAST* mem) {
    if (!mem || !mem->object) return SemaType::Unknown();
    SemaType obj_type = checkExpr(mem->object.get());

    if (mem->is_arrow) {
        if (!obj_type.is_pointer()) {
            reportError(mem->loc,
                "Operator '->' requires a pointer type, got '" + obj_type.toString() + "'.",
                "Use '.' for value types or take address with '&'");
            return SemaType::Unknown();
        }
        if (obj_type.element_type) {
            obj_type = *obj_type.element_type;
        } else {
            obj_type = SemaType::Any();
        }
    } else {
        if (obj_type.is_pointer()) {
            if (strict_mode_) {
                reportError(mem->loc,
                    "Cannot use '.' on pointer type '" + obj_type.toString() + "'.",
                    "Use '->' for pointer member access");
                return SemaType::Unknown();
            } else if (obj_type.element_type) {
                obj_type = *obj_type.element_type;
            }
        }
    }

    // Built-in array methods: push, pop, len, size, clear
    if (obj_type.kind == SemaTypeKind::Array) {
        if (mem->is_method_call) {
            if (mem->member == "push") {
                for (const auto& arg : mem->method_args) {
                    checkExpr(arg.get());
                }
                return SemaType::Void();
            } else if (mem->member == "pop") {
                return obj_type.element_type ? *obj_type.element_type : SemaType::Int();
            } else if (mem->member == "len" || mem->member == "size") {
                return SemaType::Int();
            } else if (mem->member == "clear") {
                return SemaType::Void();
            }
        }
        reportError(mem->loc, "Member access '" + mem->member + "' on array type is not supported.", "Use .push(), .pop(), or .len()");
        return SemaType::Unknown();
    }

    // Built-in vec<T> / vector<T> methods
    if (obj_type.kind == SemaTypeKind::Vec) {
        SemaType elem_t = obj_type.element_type ? *obj_type.element_type : SemaType::Any();
        if (!mem->is_method_call) {
            reportError(mem->loc,
                "Direct field access '" + mem->member + "' on '" + obj_type.toString() + "' is not supported.",
                "Use methods .size(), .capacity(), .empty(), .data(), .push(), .pop(), .clear(), .reserve()");
            return SemaType::Unknown();
        }
        const std::string& m = mem->member;
        if (m == "push" || m == "push_back") {
            if (mem->method_args.size() != 1) {
                reportError(mem->loc, "Method '" + m + "' on '" + obj_type.toString() + "' expects 1 argument.", "Pass element of type '" + elem_t.toString() + "'");
                return SemaType::Void();
            }
            SemaType arg_t = checkExpr(mem->method_args[0].get());
            if (arg_t.is_vec()) {
                reportError(mem->method_args[0]->loc, "Cannot shallow-copy push '" + arg_t.toString() + "' into vector.", "Store pointers or structs");
            } else if (!arg_t.is_assignable_to(elem_t, strict_mode_)) {
                reportError(mem->method_args[0]->loc,
                    "Argument 1 of '" + m + "' expected type '" + elem_t.toString() + "', but got '" + arg_t.toString() + "'.",
                    "Ensure pushed value matches element type");
            }
            return SemaType::Void();
        }
        if (m == "pop" || m == "pop_back" || m == "clear") {
            if (!mem->method_args.empty()) {
                reportError(mem->loc, "Method '" + m + "' expects 0 arguments.", "Remove arguments");
            }
            return SemaType::Void();
        }
        if (m == "size" || m == "capacity") {
            if (!mem->method_args.empty()) {
                reportError(mem->loc, "Method '" + m + "' expects 0 arguments.", "Remove arguments");
            }
            return SemaType::Int();
        }
        if (m == "empty") {
            if (!mem->method_args.empty()) {
                reportError(mem->loc, "Method 'empty' expects 0 arguments.", "Remove arguments");
            }
            return SemaType::Bool();
        }
        if (m == "reserve") {
            if (mem->method_args.size() != 1) {
                reportError(mem->loc, "Method 'reserve' expects 1 integer argument.", "Pass new capacity");
                return SemaType::Void();
            }
            SemaType arg_t = checkExpr(mem->method_args[0].get());
            if (!arg_t.is_integral() && !arg_t.is_any() && !arg_t.is_unknown()) {
                reportError(mem->method_args[0]->loc, "Argument to 'reserve' must be an integer, got '" + arg_t.toString() + "'.", "Pass integer capacity");
            }
            return SemaType::Void();
        }
        if (m == "data") {
            if (!mem->method_args.empty()) {
                reportError(mem->loc, "Method 'data' expects 0 arguments.", "Remove arguments");
            }
            return SemaType::Ptr(elem_t);
        }
        reportError(mem->loc, "'" + obj_type.toString() + "' has no method named '" + m + "'.",
            "Available methods: push, push_back, pop, pop_back, size, capacity, empty, clear, reserve, data");
        return SemaType::Unknown();
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

    // Helper lambda to search fields across inheritance hierarchy with cycle guard
    auto find_field = [this](const StructSymbol* s, const std::string& field_name,
                             std::unordered_set<std::string>& visited, auto& self) -> const SemaType* {
        if (!s || visited.count(s->name)) return nullptr;
        visited.insert(s->name);
        auto fit = s->fields.find(field_name);
        if (fit != s->fields.end()) return &fit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, field_name, visited, self);
        }
        return nullptr;
    };

    // Helper lambda to search methods across inheritance hierarchy with cycle guard
    auto find_method = [this](const StructSymbol* s, const std::string& method_name,
                              std::unordered_set<std::string>& visited, auto& self) -> const FunctionSymbol* {
        if (!s || visited.count(s->name)) return nullptr;
        visited.insert(s->name);
        auto mit = s->methods.find(method_name);
        if (mit != s->methods.end()) return &mit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, method_name, visited, self);
        }
        return nullptr;
    };

    std::unordered_set<std::string> visited_fields;
    std::unordered_set<std::string> visited_methods;

    if (mem->is_method_call) {
        const FunctionSymbol* method = find_method(st, mem->member, visited_methods, find_method);
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
            if (!arg_type.is_assignable_to(param_type, strict_mode_)) {
                reportError(mem->method_args[i]->loc,
                    "Argument " + std::to_string(i + 1) + " of method '" + mem->member + "' expected type '" +
                    param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                    "Ensure argument type matches expected parameter type");
            }
        }

        return method->return_type;
    } else {
        const SemaType* ftype = find_field(st, mem->member, visited_fields, find_field);
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
    if (!idx) return SemaType::Unknown();
    SemaType target_type = checkExpr(idx->target.get());
    SemaType idx_type = checkExpr(idx->index.get());

    if (!idx_type.is_integral() && !idx_type.is_any() && !idx_type.is_unknown()) {
        reportError(idx->loc, "Array index must be integer, got '" + idx_type.toString() + "'.", "Use integer value for indexing");
    }

    SemaType res = SemaType::Any();
    if (target_type.kind == SemaTypeKind::Array || target_type.is_vec()) {
        res = target_type.element_type ? *target_type.element_type : SemaType::Any();
    } else if (target_type.is_pointer() && target_type.element_type) {
        if (target_type.element_type->is_void()) {
            res = SemaType::Int();
        } else {
            res = *target_type.element_type;
        }
    } else if (target_type.is_string()) {
        res = SemaType::Int(); // char
    }
    idx->inferred_type = res.toString();
    return res;
}

SemaType GrassSema::checkArrayLiteralExpr(const ArrayLiteralExprAST* arr) {
    SemaType elem_type = SemaType::Unknown();
    for (const auto& elem : arr->elements) {
        SemaType et = checkExpr(elem.get());
        if (elem_type.is_unknown()) {
            elem_type = et;
        } else if (!elem_type.equals(et)) {
            if (elem_type.is_numeric() && et.is_numeric()) {
                elem_type = SemaType::Float();
            } else {
                elem_type = SemaType::Any();
            }
        }
    }
    if (elem_type.is_unknown()) {
        elem_type = SemaType::Any();
    }
    SemaType res = SemaType::Array(elem_type);
    arr->inferred_type = res.toString();
    return res;
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

SemaType GrassSema::checkCastExpr(const CastExprAST* cast) {
    SemaType src_type = checkExpr(cast->expr.get());
    SemaType dst_type = resolveType(cast->target_type, cast->target_pointer_depth, cast->loc);
    return dst_type;
}

SemaType GrassSema::checkSizeofExpr(const SizeofExprAST* sz) {
    if (!sz->type_name.empty()) {
        resolveType(sz->type_name, sz->pointer_depth, sz->loc);
    } else if (sz->expr) {
        checkExpr(sz->expr.get());
    }
    return SemaType::Int();
}

} // namespace grass::sema
