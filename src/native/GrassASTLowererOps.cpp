#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    ir::ValueId ASTLowerer::parse_logical_or(std::string& error) {
        ir::ValueId lhs = parse_logical_and(error);
        if (!lhs) return 0;

        while (match(TOK_OR)) {
            ir::ValueId rhs = parse_logical_and(error);
            if (!rhs) return 0;
            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            ir::IRType* rt = builder_.get_function()->get_value(rhs).type;
            ir::ValueId lhs_b = (lt && lt->is_bool()) ? lhs : builder_.create_icmp_ne(lhs, builder_.create_const_int(lt ? lt : ctx_.get_i64_type(), 0));
            ir::ValueId rhs_b = (rt && rt->is_bool()) ? rhs : builder_.create_icmp_ne(rhs, builder_.create_const_int(rt ? rt : ctx_.get_i64_type(), 0));
            ir::ValueId or_res = builder_.create_or(lhs_b, rhs_b);
            lhs = builder_.create_zext(or_res, ctx_.get_i64_type());
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_logical_and(std::string& error) {
        ir::ValueId lhs = parse_bitwise_or(error);
        if (!lhs) return 0;

        while (match(TOK_AND)) {
            ir::ValueId rhs = parse_bitwise_or(error);
            if (!rhs) return 0;
            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            ir::IRType* rt = builder_.get_function()->get_value(rhs).type;
            ir::ValueId lhs_b = (lt && lt->is_bool()) ? lhs : builder_.create_icmp_ne(lhs, builder_.create_const_int(lt ? lt : ctx_.get_i64_type(), 0));
            ir::ValueId rhs_b = (rt && rt->is_bool()) ? rhs : builder_.create_icmp_ne(rhs, builder_.create_const_int(rt ? rt : ctx_.get_i64_type(), 0));
            ir::ValueId and_res = builder_.create_and(lhs_b, rhs_b);
            lhs = builder_.create_zext(and_res, ctx_.get_i64_type());
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_bitwise_or(std::string& error) {
        ir::ValueId lhs = parse_bitwise_xor(error);
        if (!lhs) return 0;

        while (match(TOK_PIPE)) {
            ir::ValueId rhs = parse_bitwise_xor(error);
            if (!rhs) return 0;
            lhs = builder_.create_or(lhs, rhs);
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_bitwise_xor(std::string& error) {
        ir::ValueId lhs = parse_bitwise_and(error);
        if (!lhs) return 0;

        while (match(TOK_CARET)) {
            ir::ValueId rhs = parse_bitwise_and(error);
            if (!rhs) return 0;
            lhs = builder_.create_xor(lhs, rhs);
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_bitwise_and(std::string& error) {
        ir::ValueId lhs = parse_equality(error);
        if (!lhs) return 0;

        while (match(TOK_AMP)) {
            ir::ValueId rhs = parse_equality(error);
            if (!rhs) return 0;
            lhs = builder_.create_and(lhs, rhs);
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_equality(std::string& error) {
        ir::ValueId lhs = parse_relational(error);
        if (!lhs) return 0;

        while (peek().kind == TOK_EQ || peek().kind == TOK_NEQ) {
            GrassTokenKind op = advance().kind;
            ir::ValueId rhs = parse_relational(error);
            if (!rhs) return 0;
            ir::IRType* t1 = builder_.get_function()->has_value(lhs) ? builder_.get_function()->get_value(lhs).type : nullptr;
            ir::IRType* t2 = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
            if (t1 && t2 && ((t1->is_pointer() && t2->is_integer()) || (t1->is_integer() && t2->is_pointer()))) {
                ir::IRType* i64_t = ctx_.get_i64_type();
                lhs = coerce_type(lhs, i64_t);
                rhs = coerce_type(rhs, i64_t);
            } else {
                match_types(lhs, rhs);
            }
            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            if (lt && lt->is_floating_point()) {
                if (op == TOK_EQ) lhs = builder_.create_fcmp(ir::FCmpCond::OEQ, lhs, rhs);
                else lhs = builder_.create_fcmp(ir::FCmpCond::ONE, lhs, rhs);
            } else {
                if (op == TOK_EQ) lhs = builder_.create_icmp_eq(lhs, rhs);
                else lhs = builder_.create_icmp_ne(lhs, rhs);
            }
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_relational(std::string& error) {
        ir::ValueId lhs = parse_shift(error);
        if (!lhs) return 0;

        while (peek().kind == TOK_LT || peek().kind == TOK_LTE ||
               peek().kind == TOK_GT || peek().kind == TOK_GTE) {
            GrassTokenKind op = advance().kind;
            ir::ValueId rhs = parse_shift(error);
            if (!rhs) return 0;
            ir::IRType* t1 = builder_.get_function()->has_value(lhs) ? builder_.get_function()->get_value(lhs).type : nullptr;
            ir::IRType* t2 = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
            if (t1 && t2 && ((t1->is_pointer() && t2->is_integer()) || (t1->is_integer() && t2->is_pointer()))) {
                ir::IRType* i64_t = ctx_.get_i64_type();
                lhs = coerce_type(lhs, i64_t);
                rhs = coerce_type(rhs, i64_t);
            } else {
                match_types(lhs, rhs);
            }
            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            if (lt && lt->is_floating_point()) {
                if (op == TOK_LT) lhs = builder_.create_fcmp(ir::FCmpCond::OLT, lhs, rhs);
                else if (op == TOK_LTE) lhs = builder_.create_fcmp(ir::FCmpCond::OLE, lhs, rhs);
                else if (op == TOK_GT) lhs = builder_.create_fcmp(ir::FCmpCond::OGT, lhs, rhs);
                else if (op == TOK_GTE) lhs = builder_.create_fcmp(ir::FCmpCond::OGE, lhs, rhs);
            } else {
                if (op == TOK_LT) lhs = builder_.create_icmp_slt(lhs, rhs);
                else if (op == TOK_LTE) lhs = builder_.create_icmp_sle(lhs, rhs);
                else if (op == TOK_GT) lhs = builder_.create_icmp_sgt(lhs, rhs);
                else if (op == TOK_GTE) lhs = builder_.create_icmp_sge(lhs, rhs);
            }
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_shift(std::string& error) {
        ir::ValueId lhs = parse_additive(error);
        if (!error.empty()) return 0;

        if (!in_stream_io_) {
            while (peek().kind == TOK_SHL || peek().kind == TOK_SHR) {
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_additive(error);
                if (!error.empty()) return 0;
                match_types(lhs, rhs);
                if (op == TOK_SHL) lhs = builder_.create_shl(lhs, rhs);
                else lhs = builder_.create_ashr(lhs, rhs);
            }
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_additive(std::string& error) {
        ir::ValueId lhs = parse_multiplicative(error);
        if (!lhs) return 0;

        while (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS) {
            GrassTokenKind op = advance().kind;
            ir::ValueId rhs = parse_multiplicative(error);
            if (!rhs) return 0;

            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            ir::IRType* rt = builder_.get_function()->get_value(rhs).type;

            // Pointer arithmetic: ptr + n, ptr - n
            if (lt && lt->is_pointer() && rt && rt->is_integer()) {
                uint64_t elem_sz = 8;
                if (lt->element_type) {
                    elem_sz = lt->element_type->size();
                    if (elem_sz <= 0) elem_sz = 1;
                }
                ir::IRType* i64_t = ctx_.get_i64_type();
                ir::ValueId scaled_rhs = coerce_type(rhs, i64_t);
                if (elem_sz != 1) {
                    ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                    scaled_rhs = builder_.create_mul(scaled_rhs, sz_val);
                }
                ir::ValueId ptr_i64 = coerce_type(lhs, i64_t);
                ir::ValueId res_i64 = (op == TOK_PLUS)
                    ? builder_.create_add(ptr_i64, scaled_rhs)
                    : builder_.create_sub(ptr_i64, scaled_rhs);
                lhs = builder_.create_inttoptr(res_i64, lt);
                continue;
            }
            // n + ptr
            if (op == TOK_PLUS && rt && rt->is_pointer() && lt && lt->is_integer()) {
                uint64_t elem_sz = 8;
                if (rt->element_type) {
                    elem_sz = rt->element_type->size();
                    if (elem_sz <= 0) elem_sz = 1;
                }
                ir::IRType* i64_t = ctx_.get_i64_type();
                ir::ValueId scaled_lhs = coerce_type(lhs, i64_t);
                if (elem_sz != 1) {
                    ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                    scaled_lhs = builder_.create_mul(scaled_lhs, sz_val);
                }
                ir::ValueId ptr_i64 = coerce_type(rhs, i64_t);
                ir::ValueId res_i64 = builder_.create_add(scaled_lhs, ptr_i64);
                lhs = builder_.create_inttoptr(res_i64, rt);
                continue;
            }
            // ptr1 - ptr2: (ptr1 - ptr2) / sizeof(T)
            if (op == TOK_MINUS && lt && lt->is_pointer() && rt && rt->is_pointer()) {
                uint64_t elem_sz = 8;
                if (lt->element_type) {
                    elem_sz = lt->element_type->size();
                    if (elem_sz <= 0) elem_sz = 1;
                }
                ir::IRType* i64_t = ctx_.get_i64_type();
                ir::ValueId p1_i64 = coerce_type(lhs, i64_t);
                ir::ValueId p2_i64 = coerce_type(rhs, i64_t);
                ir::ValueId diff = builder_.create_sub(p1_i64, p2_i64);
                if (elem_sz != 1) {
                    ir::ValueId sz_val = builder_.create_const_int(i64_t, elem_sz);
                    lhs = builder_.create_sdiv(diff, sz_val);
                } else {
                    lhs = diff;
                }
                continue;
            }

            match_types(lhs, rhs);
            lt = builder_.get_function()->get_value(lhs).type;
            if (lt && lt->is_floating_point()) {
                if (op == TOK_PLUS) lhs = builder_.create_fadd(lhs, rhs);
                else lhs = builder_.create_fsub(lhs, rhs);
            } else {
                if (op == TOK_PLUS) lhs = builder_.create_add(lhs, rhs);
                else lhs = builder_.create_sub(lhs, rhs);
            }
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_multiplicative(std::string& error) {
        ir::ValueId lhs = parse_unary(error);
        if (!lhs) return 0;

        while (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD) {
            GrassTokenKind op = advance().kind;
            ir::ValueId rhs = parse_unary(error);
            if (!rhs) return 0;
            match_types(lhs, rhs);
            ir::IRType* lt = builder_.get_function()->get_value(lhs).type;
            if (lt && lt->is_floating_point()) {
                if (op == TOK_STAR) lhs = builder_.create_fmul(lhs, rhs);
                else if (op == TOK_SLASH) lhs = builder_.create_fdiv(lhs, rhs);
                else lhs = builder_.create_srem(lhs, rhs);
            } else {
                if (op == TOK_STAR) lhs = builder_.create_mul(lhs, rhs);
                else if (op == TOK_SLASH) lhs = builder_.create_sdiv(lhs, rhs);
                else if (op == TOK_MOD) lhs = builder_.create_srem(lhs, rhs);
            }
        }
        return lhs;
    }

    ir::ValueId ASTLowerer::parse_unary(std::string& error) {
        // Address-of operator &var, &arr[i], &obj.field, &ptr->field, &outer.inner.field
        if (match(TOK_AMP)) {
            if (peek().kind != TOK_IDENT) {
                error = "Line " + std::to_string(peek().line) + ": Cannot take address of non-lvalue expression!";
                return 0;
            }
            std::string id = advance().text;
            const VarBinding* var = lookup_var(id);
            if (!var) { error = "Undeclared variable: " + id; return 0; }
            last_expr_is_borrowed_ptr_ = true;

            if (match(TOK_LBRACKET)) {
                ir::ValueId idx = parse_expression(error);
                if (!idx || !consume(TOK_RBRACKET, "Expected ']'", error)) return 0;
                ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                if (var->is_vec && var->vec_elem_type) {
                    ir::ValueId vec_p = (var->type && var->type->is_pointer())
                        ? builder_.create_load(var->alloca_id, id + "_vptr")
                        : var->alloca_id;
                    ir::ValueId data_ptr = emit_vec_data(vec_p, var->vec_elem_type);
                    return builder_.create_gep(data_ptr, { idx64 }, var->vec_elem_type, id + "_vec_elem_addr");
                }
                ir::ValueId arr_ptr = builder_.create_load(var->alloca_id, id + "_ptr");
                bool is_typed_ptr = (var->type && var->type->is_pointer() &&
                                     var->type->element_type && !var->type->element_type->is_void());
                if (is_typed_ptr) {
                    return builder_.create_gep(arr_ptr, { idx64 }, var->type->element_type, id + "_elem_addr");
                }
                // Dynamic GrassDynArray header has data pointer at offset +8 (index 1 of i64*)
                ir::ValueId hdr_i64 = coerce_type(arr_ptr, ctx_.get_ptr_type(ctx_.get_i64_type()));
                ir::ValueId data_slot = builder_.create_gep(hdr_i64, { builder_.create_const_i64(1) }, ctx_.get_i64_type());
                ir::ValueId data_i64 = builder_.create_load(data_slot, id + "_data");
                ir::ValueId data_ptr = coerce_type(data_i64, ctx_.get_ptr_type(ctx_.get_i64_type()));
                return builder_.create_gep(data_ptr, { idx64 }, ctx_.get_i64_type(), id + "_elem_addr");
            }

            if (peek().kind == TOK_DOT || peek().kind == TOK_ARROW) {
                ir::ValueId inst_ptr = var->alloca_id;
                if (var->type && var->type->is_pointer()) {
                    inst_ptr = builder_.create_load(var->alloca_id, id + "_ptr");
                }
                std::string sname = var->struct_type_name;
                while (match(TOK_DOT) || match(TOK_ARROW)) {
                    if (peek().kind != TOK_IDENT) {
                        error = "Expected field name after '.' or '->' in address-of!";
                        return 0;
                    }
                    std::string field_name = advance().text;
                    if (!oop_mgr_ || sname.empty()) break;
                    const auto* cd = oop_mgr_->get_class(sname);
                    const auto* fld = cd ? cd->find_field(field_name) : nullptr;
                    ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, inst_ptr, sname, field_name, field_name + "_addr");
                    if (peek().kind != TOK_DOT && peek().kind != TOK_ARROW) {
                        last_struct_name_.clear();
                        if (fld && fld->type && fld->type->is_struct()) last_struct_name_ = fld->type->struct_name;
                        return fptr;
                    }
                    if (fld && fld->type && fld->type->is_struct()) {
                        inst_ptr = fptr;
                        sname = fld->type->struct_name;
                    } else if (fld && fld->type && fld->type->is_pointer() && fld->type->element_type && fld->type->element_type->is_struct()) {
                        inst_ptr = builder_.create_load(fptr, field_name + "_ptr");
                        sname = fld->type->element_type->struct_name;
                    } else {
                        return fptr;
                    }
                }
                return inst_ptr;
            }

            if (!var->struct_type_name.empty() && var->type && var->type->is_struct()) {
                last_struct_name_ = var->struct_type_name;
            } else {
                last_struct_name_.clear();
            }
            return var->alloca_id;
        }

        // Dereference operator *ptr
        if (match(TOK_STAR)) {
            ir::ValueId ptr_val = parse_unary(error);
            if (!ptr_val) return 0;
            ir::IRType* pt = builder_.get_function()->get_value(ptr_val).type;
            if (pt && pt->is_pointer() && pt->element_type && pt->element_type->is_struct()) {
                last_struct_name_ = pt->element_type->struct_name;
            } else if (pt && pt->is_pointer() && pt->element_type && pt->element_type->is_pointer() &&
                       pt->element_type->element_type && pt->element_type->element_type->is_struct()) {
                last_struct_name_ = pt->element_type->element_type->struct_name;
            }
            return builder_.create_load(ptr_val, "deref");
        }

        // C-style cast (Type*)expr or (Type)expr
        if (peek().kind == TOK_LPAREN) {
            size_t off = 1;
            if (peek(off).kind == TOK_IDENT && (is_type_or_func_keyword(peek(off).text) || (oop_mgr_ && oop_mgr_->has_class(peek(off).text)))) {
                std::string tname = peek(off).text;
                off++;
                while (peek(off).kind == TOK_COLON_COLON) {
                    off++;
                    if (peek(off).kind == TOK_IDENT) { tname += "::" + peek(off).text; off++; } else break;
                }
                int ptr_cnt = 0;
                while (peek(off).kind == TOK_STAR) { ptr_cnt++; off++; }
                if (peek(off).kind == TOK_RPAREN) {
                    auto next_k = peek(off + 1).kind;
                    if (next_k == TOK_IDENT || next_k == TOK_NUMBER || next_k == TOK_STRING ||
                        next_k == TOK_LPAREN || next_k == TOK_STAR || next_k == TOK_AMP ||
                        next_k == TOK_MINUS || next_k == TOK_NULLPTR) {
                        advance(); // consume '('
                        advance(); // consume base type identifier
                        while (match(TOK_COLON_COLON)) {
                            if (peek().kind == TOK_IDENT) advance();
                        }
                        ir::IRType* tgt_t = parse_type_name(tname);
                        while (match(TOK_STAR)) tgt_t = ctx_.get_ptr_type(tgt_t);
                        match(TOK_RPAREN);
                        ir::ValueId val = parse_unary(error);
                        if (!val) return 0;
                        if (tgt_t && tgt_t->is_pointer() && tgt_t->element_type && tgt_t->element_type->is_struct()) {
                            last_struct_name_ = tgt_t->element_type->struct_name;
                            last_expr_is_borrowed_ptr_ = true;
                        }
                        return coerce_type(val, tgt_t);
                    }
                }
            }
        }

        if (match(TOK_MINUS)) {
            ir::ValueId operand = parse_unary(error);
            if (!operand) return 0;
            ir::IRType* ot = builder_.get_function()->get_value(operand).type;
            if (ot && ot->is_floating_point()) {
                return builder_.create_fneg(operand);
            }
            return builder_.create_neg(operand);
        }
        if (match(TOK_NOT)) {
            ir::ValueId operand = parse_unary(error);
            if (!operand) return 0;
            ir::IRType* ot = builder_.get_function()->get_value(operand).type;
            if (ot && ot->is_bool()) {
                return builder_.create_xor(operand, builder_.create_const_i1(true));
            }
            ir::ValueId zero = builder_.create_const_int(ot ? ot : ctx_.get_i64_type(), 0);
            return builder_.create_icmp_eq(operand, zero);
        }
        if (match(TOK_TILDE)) {
            ir::ValueId operand = parse_unary(error);
            return builder_.create_not(operand);
        }
        if (match(TOK_INC)) {
            // Prefix ++x
            if (peek().kind == TOK_IDENT) {
                std::string var_name = advance().text;
                const VarBinding* var = lookup_var(var_name);
                if (!var) { error = "Undeclared variable: " + var_name; return 0; }
                ir::ValueId vaddr = var_addr(var);
                ir::ValueId cur = builder_.create_load(vaddr, var_name);
                ir::ValueId one = builder_.create_const_int(var->type, 1);
                ir::ValueId next = builder_.create_add(cur, one);
                builder_.create_store(next, vaddr);
                return next;
            }
        }
        if (match(TOK_DEC)) {
            // Prefix --x
            if (peek().kind == TOK_IDENT) {
                std::string var_name = advance().text;
                const VarBinding* var = lookup_var(var_name);
                if (!var) { error = "Undeclared variable: " + var_name; return 0; }
                ir::ValueId vaddr = var_addr(var);
                ir::ValueId cur = builder_.create_load(vaddr, var_name);
                ir::ValueId one = builder_.create_const_int(var->type, 1);
                ir::ValueId next = builder_.create_sub(cur, one);
                builder_.create_store(next, vaddr);
                return next;
            }
        }

        return parse_postfix(error);
    }

    ir::ValueId ASTLowerer::parse_postfix(std::string& error) {
        ir::ValueId base = parse_primary(error);
        if (!base) return 0;

        while (true) {
            if (match(TOK_DOT) || match(TOK_ARROW)) {
                if (peek().kind != TOK_IDENT) {
                    error = "Expected identifier after '.' or '->'!";
                    return 0;
                }
                std::string member_name = advance().text;
                std::string receiver_struct = last_struct_name_;
                if (match(TOK_LPAREN)) {
                    std::vector<ir::ValueId> args;
                    if (peek().kind != TOK_RPAREN) {
                        while (true) {
                            ir::ValueId arg = parse_expression(error);
                            if (!arg) return 0;
                            args.push_back(arg);
                            if (!match(TOK_COMMA)) break;
                        }
                    }
                    consume(TOK_RPAREN, "Expected ')' closing method call!", error);
                    if (!receiver_struct.empty() && vec_struct_meta_.count(receiver_struct)) {
                        const auto& vm = vec_struct_meta_[receiver_struct];
                        if ((member_name == "push" || member_name == "push_back") && !args.empty()) {
                            emit_vec_push(base, vm.elem_type, args[0]);
                            last_struct_name_.clear();
                            base = builder_.create_const_i64(0);
                            continue;
                        } else if (member_name == "pop" || member_name == "pop_back") {
                            emit_vec_pop(base, vm.elem_type, vm.elem_struct);
                            last_struct_name_.clear();
                            base = builder_.create_const_i64(0);
                            continue;
                        } else if (member_name == "size") {
                            last_struct_name_.clear();
                            base = emit_vec_size(base);
                            continue;
                        } else if (member_name == "capacity") {
                            last_struct_name_.clear();
                            base = emit_vec_capacity(base);
                            continue;
                        } else if (member_name == "empty") {
                            last_struct_name_.clear();
                            base = emit_vec_empty(base);
                            continue;
                        } else if (member_name == "clear") {
                            emit_vec_clear(base, vm.elem_type, vm.elem_struct);
                            last_struct_name_.clear();
                            base = builder_.create_const_i64(0);
                            continue;
                        } else if (member_name == "reserve" && !args.empty()) {
                            emit_vec_reserve(base, vm.elem_type, args[0]);
                            last_struct_name_.clear();
                            base = builder_.create_const_i64(0);
                            continue;
                        } else if (member_name == "data") {
                            base = emit_vec_data(base, vm.elem_type);
                            last_struct_name_ = vm.elem_struct;
                            continue;
                        }
                    }
                    if (member_name == "push") {
                        std::vector<ir::ValueId> push_args = { base };
                        push_args.insert(push_args.end(), args.begin(), args.end());
                        builder_.create_call("grass_array_push", ctx_.get_void_type(), push_args);
                        last_struct_name_.clear();
                        return base;
                    } else if (member_name == "len" || member_name == "size") {
                        last_struct_name_.clear();
                        return builder_.create_call("grass_array_len", ctx_.get_i64_type(), { base });
                    } else if (oop_mgr_ && !receiver_struct.empty()) {
                        const auto* cd = oop_mgr_->get_class(receiver_struct);
                        const auto* md = cd ? cd->find_method(member_name) : nullptr;
                        base = oop_mgr_->emit_call_method(builder_, base, receiver_struct, member_name, args);
                        if (md && md->return_type && md->return_type->is_struct()) {
                            last_struct_name_ = md->return_type->struct_name;
                        } else if (md && md->return_type && md->return_type->is_pointer() && md->return_type->element_type && md->return_type->element_type->is_struct()) {
                            last_struct_name_ = md->return_type->element_type->struct_name;
                        } else {
                            last_struct_name_.clear();
                        }
                        return base;
                    }
                } else {
                    if (oop_mgr_ && !receiver_struct.empty()) {
                        std::string prev_struct = receiver_struct;
                        const auto* cd = oop_mgr_->get_class(prev_struct);
                        std::string next_struct = "";
                        bool is_nested_val_struct = false;
                        if (cd) {
                            auto fit = cd->field_indices.find(member_name);
                            if (fit != cd->field_indices.end()) {
                                const auto& fld = cd->fields[fit->second];
                                if (fld.type) {
                                    if (fld.type->is_struct()) {
                                        is_nested_val_struct = true;
                                        next_struct = fld.type->struct_name;
                                    } else if (fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                        next_struct = fld.type->element_type->struct_name;
                                    }
                                }
                            }
                        }
                        if (is_nested_val_struct) {
                            base = oop_mgr_->emit_field_gep(builder_, base, prev_struct, member_name);
                        } else {
                            base = oop_mgr_->emit_load_field(builder_, base, prev_struct, member_name);
                        }
                        last_struct_name_ = next_struct;
                    }
                }
                continue;
            }
            // Cast: expr as Type* or expr as Type
            if (match(TOK_AS)) {
                if (peek().kind != TOK_IDENT) {
                    error = "Expected type name after 'as'!";
                    return 0;
                }
                std::string tname = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
                }
                ir::IRType* tgt_t = parse_type_name(tname);
                while (match(TOK_STAR)) {
                    tgt_t = ctx_.get_ptr_type(tgt_t);
                }
                base = coerce_type(base, tgt_t);
                continue;
            }
            // Array or pointer subscript: base[idx]
            if (match(TOK_LBRACKET)) {
                ir::ValueId idx = parse_expression(error);
                if (!idx) return 0;
                consume(TOK_RBRACKET, "Expected ']' closing array index!", error);
                ir::IRType* base_t = builder_.get_function()->has_value(base)
                    ? builder_.get_function()->get_value(base).type : nullptr;
                if (base_t && base_t->is_pointer() && base_t->element_type && base_t->element_type->is_struct() &&
                    vec_struct_meta_.count(base_t->element_type->struct_name)) {
                    const auto& vm = vec_struct_meta_[base_t->element_type->struct_name];
                    ir::IRType* elem_t = vm.elem_type;
                    ir::ValueId data_ptr = emit_vec_data(base, elem_t);
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::ValueId elem_ptr = builder_.create_gep(data_ptr, { idx64 }, elem_t, "vec_idx_gep");
                    if (elem_t->is_struct()) {
                        last_struct_name_ = vm.elem_struct;
                        base = elem_ptr;
                    } else if (elem_t->is_pointer() && elem_t->element_type && elem_t->element_type->is_struct()) {
                        last_struct_name_ = elem_t->element_type->struct_name;
                        base = builder_.create_load(elem_ptr, "vec_idx_val");
                    } else {
                        last_struct_name_.clear();
                        base = builder_.create_load(elem_ptr, "vec_idx_val");
                    }
                } else if (base_t && base_t->is_pointer() && base_t->element_type &&
                           !base_t->element_type->is_void() && !expr_array_elem_types_.count(base)) {
                    ir::IRType* elem_t = base_t->element_type;
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::ValueId elem_ptr = builder_.create_gep(base, { idx64 }, elem_t, "ptr_idx_gep");
                    if (elem_t->is_struct()) {
                        last_struct_name_ = elem_t->struct_name;
                        base = elem_ptr;
                    } else if (elem_t->is_pointer() && elem_t->element_type && elem_t->element_type->is_struct()) {
                        last_struct_name_ = elem_t->element_type->struct_name;
                        base = builder_.create_load(elem_ptr, "ptr_idx_val");
                    } else {
                        last_struct_name_.clear();
                        base = builder_.create_load(elem_ptr, "ptr_idx_val");
                    }
                } else {
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::ValueId raw = builder_.create_call("grass_array_get", ctx_.get_i64_type(), { base, idx64 });
                    auto arr_et_it = expr_array_elem_types_.find(base);
                    ir::IRType* arr_et = (arr_et_it != expr_array_elem_types_.end()) ? arr_et_it->second : nullptr;
                    std::string arr_es = expr_array_elem_structs_.count(base) ? expr_array_elem_structs_[base] : "";
                    base = decode_array_slot(raw, arr_et, arr_es);
                }
                continue;
            }
            break;
        }
        return base;
    }

} // namespace grass::codegen
