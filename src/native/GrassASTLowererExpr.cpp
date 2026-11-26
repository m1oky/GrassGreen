#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    ir::ValueId ASTLowerer::parse_expression(std::string& error) {
        return parse_assignment(error);
    }

    ir::ValueId ASTLowerer::parse_assignment(std::string& error) {
        // Check for pointer dereference assignment: *ptr = expr, **ptr = expr, *(expr) = expr, *ptr += expr
        if (peek().kind == TOK_STAR) {
            size_t look = 0;
            while (peek(look).kind == TOK_STAR) look++;
            bool is_ident_deref = (peek(look).kind == TOK_IDENT &&
                (peek(look + 1).kind == TOK_ASSIGN || peek(look + 1).kind == TOK_PLUS_ASSIGN ||
                 peek(look + 1).kind == TOK_MINUS_ASSIGN || peek(look + 1).kind == TOK_MUL_ASSIGN ||
                 peek(look + 1).kind == TOK_DIV_ASSIGN));
            bool is_paren_deref = (peek(look).kind == TOK_LPAREN);
            size_t paren_close = pos_ + look;
            if (is_paren_deref) {
                paren_close++;
                int pdepth = 1;
                while (paren_close < tokens_.size() && pdepth > 0) {
                    if (tokens_[paren_close].kind == TOK_LPAREN) pdepth++;
                    else if (tokens_[paren_close].kind == TOK_RPAREN) pdepth--;
                    paren_close++;
                }
                is_paren_deref = (pdepth == 0 && paren_close < tokens_.size() &&
                    (tokens_[paren_close].kind == TOK_ASSIGN || tokens_[paren_close].kind == TOK_PLUS_ASSIGN ||
                     tokens_[paren_close].kind == TOK_MINUS_ASSIGN || tokens_[paren_close].kind == TOK_MUL_ASSIGN ||
                     tokens_[paren_close].kind == TOK_DIV_ASSIGN));
            }

            if (is_ident_deref || is_paren_deref) {
                advance(); // consume first '*'
                int deref_count = 1;
                while (match(TOK_STAR)) deref_count++;

                ir::ValueId target_addr = 0;
                if (is_ident_deref) {
                    std::string ptr_name = advance().text;
                    const VarBinding* ptr_var = lookup_var(ptr_name);
                    if (!ptr_var) {
                        error = "Undeclared pointer variable: " + ptr_name;
                        return 0;
                    }
                    target_addr = builder_.create_load(ptr_var->alloca_id, ptr_name + "_val");
                } else {
                    match(TOK_LPAREN);
                    target_addr = parse_expression(error);
                    if (!target_addr || !consume(TOK_RPAREN, "Expected ')' in dereference assignment", error)) return 0;
                }
                for (int d = 1; d < deref_count; ++d) {
                    target_addr = builder_.create_load(target_addr, "deref_addr");
                }

                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_assignment(error);
                if (!rhs) return 0;

                ir::IRType* ptr_t = builder_.get_function()->has_value(target_addr)
                    ? builder_.get_function()->get_value(target_addr).type : nullptr;
                ir::IRType* elem_t = (ptr_t && ptr_t->is_pointer() && ptr_t->element_type && !ptr_t->element_type->is_void())
                    ? ptr_t->element_type : nullptr;

                if (op == TOK_ASSIGN) {
                    if (elem_t) rhs = coerce_type(rhs, elem_t);
                    builder_.create_store(rhs, target_addr);
                    return rhs;
                } else {
                    ir::ValueId cur = builder_.create_load(target_addr, "deref_cur");
                    match_types(cur, rhs);
                    ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                    ir::ValueId computed = 0;
                    if (lt && lt->is_floating_point()) {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                        else computed = builder_.create_fdiv(cur, rhs);
                    } else {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                        else computed = builder_.create_sdiv(cur, rhs);
                    }
                    if (elem_t) computed = coerce_type(computed, elem_t);
                    builder_.create_store(computed, target_addr);
                    return computed;
                }
            }
        }

        // Check for member assignment: obj.field = expr or obj.a.b = expr (supports '.' and '->')
        if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_DOT || peek(1).kind == TOK_ARROW)) {
            size_t look = pos_ + 1;
            while (look + 1 < tokens_.size() &&
                   (tokens_[look].kind == TOK_DOT || tokens_[look].kind == TOK_ARROW) &&
                   tokens_[look + 1].kind == TOK_IDENT) {
                look += 2;
            }
            if (look > pos_ + 1 && look < tokens_.size() &&
                (tokens_[look].kind == TOK_ASSIGN || tokens_[look].kind == TOK_PLUS_ASSIGN ||
                 tokens_[look].kind == TOK_MINUS_ASSIGN || tokens_[look].kind == TOK_MUL_ASSIGN ||
                 tokens_[look].kind == TOK_DIV_ASSIGN)) {
                std::string obj_name = advance().text;
                const VarBinding* obj_var = lookup_var(obj_name);
                if (!obj_var) {
                    error = "Undeclared object: " + obj_name;
                    return 0;
                }
                ir::ValueId inst_ptr = obj_var->alloca_id;
                if (obj_var->type && obj_var->type->is_pointer()) {
                    inst_ptr = builder_.create_load(obj_var->alloca_id, obj_name + "_ptr");
                }
                std::string sname = obj_var->struct_type_name;

                std::vector<std::string> member_chain;
                while ((peek().kind == TOK_DOT || peek().kind == TOK_ARROW) && peek(1).kind == TOK_IDENT) {
                    advance();
                    member_chain.push_back(advance().text);
                }
                GrassTokenKind op = advance().kind;

                for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                    const std::string& mid_field = member_chain[ci];
                    if (!oop_mgr_ || sname.empty()) break;
                    const auto* cd = oop_mgr_->get_class(sname);
                    std::string next_s;
                    bool is_val_struct = false;
                    if (cd) {
                        auto fit = cd->field_indices.find(mid_field);
                        if (fit != cd->field_indices.end()) {
                            const auto& fld = cd->fields[fit->second];
                            if (fld.type && fld.type->is_struct()) {
                                is_val_struct = true;
                                next_s = fld.type->struct_name;
                            } else if (fld.type && fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                next_s = fld.type->element_type->struct_name;
                            }
                        }
                    }
                    if (is_val_struct) {
                        inst_ptr = oop_mgr_->emit_field_gep(builder_, inst_ptr, sname, mid_field, mid_field + "_gep");
                    } else {
                        inst_ptr = oop_mgr_->emit_load_field(builder_, inst_ptr, sname, mid_field, mid_field + "_ptr");
                    }
                    sname = next_s;
                }

                const std::string& field_name = member_chain.back();
                ir::ValueId rhs = parse_assignment(error);
                if (!rhs) return 0;

                ir::IRType* field_type = nullptr;
                if (oop_mgr_ && !sname.empty()) {
                    const auto* cd = oop_mgr_->get_class(sname);
                    const auto* fld = cd ? cd->find_field(field_name) : nullptr;
                    if (fld) field_type = fld->type;
                }

                if (op == TOK_ASSIGN) {
                    if (oop_mgr_) {
                        if (field_type && !field_type->is_struct()) {
                            rhs = coerce_type(rhs, field_type);
                        }
                        oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, rhs);
                    }
                    return rhs;
                } else {
                    ir::ValueId cur = 0;
                    if (oop_mgr_) {
                        cur = oop_mgr_->emit_load_field(builder_, inst_ptr, sname, field_name, field_name + "_val");
                    }
                    match_types(cur, rhs);
                    ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                    ir::ValueId computed = 0;
                    if (lt && lt->is_floating_point()) {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                        else if (op == TOK_DIV_ASSIGN) computed = builder_.create_fdiv(cur, rhs);
                    } else {
                        if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                        else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                        else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                        else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);
                    }

                    if (oop_mgr_) {
                        oop_mgr_->emit_store_field(builder_, inst_ptr, sname, field_name, computed);
                    }
                    return computed;
                }
            }
        }

        // Check for array / pointer element assignment: ptr[idx] = expr or arr[idx] = expr
        if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
            size_t look = pos_ + 2;
            int depth = 1;
            while (look < tokens_.size() && depth > 0) {
                if (tokens_[look].kind == TOK_LBRACKET) depth++;
                else if (tokens_[look].kind == TOK_RBRACKET) depth--;
                look++;
            }
            if (look < tokens_.size() && (tokens_[look].kind == TOK_ASSIGN ||
                tokens_[look].kind == TOK_PLUS_ASSIGN || tokens_[look].kind == TOK_MINUS_ASSIGN ||
                tokens_[look].kind == TOK_MUL_ASSIGN || tokens_[look].kind == TOK_DIV_ASSIGN)) {
                std::string arr_name = advance().text;
                advance(); // consume '['
                ir::ValueId idx = parse_expression(error);
                if (!idx || !consume(TOK_RBRACKET, "Expected ']'", error)) return 0;
                GrassTokenKind op = advance().kind;
                ir::ValueId rhs = parse_assignment(error);
                if (!rhs) return 0;

                const VarBinding* var = lookup_var(arr_name);
                if (!var) {
                    error = "Undeclared array: " + arr_name;
                    return 0;
                }
                ir::ValueId vaddr = var_addr(var);
                ir::ValueId arr_ptr = builder_.create_load(vaddr, arr_name + "_ptr");
                bool is_typed_ptr = (var->type && var->type->is_pointer() &&
                                     var->type->element_type && !var->type->element_type->is_void() &&
                                     !var->is_vec && !var->is_array);
                if (var->is_vec && var->vec_elem_type) {
                    ir::IRType* elem_t = var->vec_elem_type;
                    ir::ValueId vec_p = (var->type && var->type->is_pointer()) ? arr_ptr : var->alloca_id;
                    ir::ValueId data_ptr = emit_vec_data(vec_p, elem_t);
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::ValueId elem_ptr = builder_.create_gep(data_ptr, { idx64 }, elem_t, arr_name + "_vec_ep");
                    if (op == TOK_ASSIGN) {
                        ir::IRType* rhs_t = builder_.get_function()->has_value(rhs) ? builder_.get_function()->get_value(rhs).type : nullptr;
                        if (elem_t->is_struct() && rhs_t && rhs_t->is_pointer()) {
                            emit_copy_struct(elem_ptr, rhs, elem_t);
                        } else {
                            rhs = coerce_type(rhs, elem_t);
                            builder_.create_store(rhs, elem_ptr);
                        }
                        return rhs;
                    } else {
                        ir::ValueId cur = builder_.create_load(elem_ptr, arr_name + "_vec_el");
                        match_types(cur, rhs);
                        ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                        ir::ValueId computed = 0;
                        if (lt && lt->is_floating_point()) {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                            else computed = builder_.create_fdiv(cur, rhs);
                        } else {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else computed = builder_.create_sdiv(cur, rhs);
                        }
                        computed = coerce_type(computed, elem_t);
                        builder_.create_store(computed, elem_ptr);
                        return computed;
                    }
                } else if (is_typed_ptr) {
                    ir::IRType* elem_t = var->type->element_type;
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::ValueId elem_ptr = builder_.create_gep(arr_ptr, { idx64 }, elem_t, arr_name + "_elem_ptr");
                    if (op == TOK_ASSIGN) {
                        rhs = coerce_type(rhs, elem_t);
                        builder_.create_store(rhs, elem_ptr);
                        return rhs;
                    } else {
                        ir::ValueId cur = builder_.create_load(elem_ptr, arr_name + "_elem");
                        match_types(cur, rhs);
                        ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                        ir::ValueId computed = 0;
                        if (lt && lt->is_floating_point()) {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                            else computed = builder_.create_fdiv(cur, rhs);
                        } else {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else computed = builder_.create_sdiv(cur, rhs);
                        }
                        computed = coerce_type(computed, elem_t);
                        builder_.create_store(computed, elem_ptr);
                        return computed;
                    }
                } else {
                    ir::ValueId idx64 = coerce_type(idx, ctx_.get_i64_type());
                    ir::IRType* elem_t = var->is_array ? var->array_elem_type : nullptr;
                    if (op == TOK_ASSIGN) {
                        ir::ValueId enc = encode_array_slot(rhs, elem_t);
                        builder_.create_call("grass_array_set", ctx_.get_void_type(), { arr_ptr, idx64, enc });
                        return elem_t ? coerce_type(rhs, elem_t) : rhs;
                    } else {
                        ir::ValueId raw = builder_.create_call("grass_array_get", ctx_.get_i64_type(), { arr_ptr, idx64 });
                        ir::ValueId cur = decode_array_slot(raw, elem_t, var->array_elem_struct);
                        match_types(cur, rhs);
                        ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                        ir::ValueId computed = 0;
                        if (lt && lt->is_floating_point()) {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                            else computed = builder_.create_fdiv(cur, rhs);
                        } else {
                            if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                            else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                            else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                            else computed = builder_.create_sdiv(cur, rhs);
                        }
                        ir::ValueId enc = encode_array_slot(computed, elem_t);
                        builder_.create_call("grass_array_set", ctx_.get_void_type(), { arr_ptr, idx64, enc });
                        return computed;
                    }
                }
            }
        }

        // Check for assignment: identifier = expr
        if (peek().kind == TOK_IDENT &&
            (peek(1).kind == TOK_ASSIGN || peek(1).kind == TOK_PLUS_ASSIGN ||
             peek(1).kind == TOK_MINUS_ASSIGN || peek(1).kind == TOK_MUL_ASSIGN ||
             peek(1).kind == TOK_DIV_ASSIGN)) {
            std::string var_name = advance().text;
            GrassTokenKind op = advance().kind;

            VarBinding* var = lookup_var_mut(var_name);
            if (!var) {
                error = "Undeclared variable: " + var_name;
                return 0;
            }

            ir::ValueId rhs = parse_assignment(error);
            if (!rhs) return 0;

            ir::ValueId vaddr = var_addr(var);
            if (op == TOK_ASSIGN) {
                if (var->is_raii) {
                    emit_var_dtor(*var);
                }
                if (expr_array_elem_types_.count(rhs)) {
                    var->is_array = true;
                    var->array_elem_type = expr_array_elem_types_[rhs];
                    if (expr_array_elem_structs_.count(rhs)) {
                        var->array_elem_struct = expr_array_elem_structs_[rhs];
                    }
                }
                ir::IRType* rhs_t = builder_.get_function()->get_value(rhs).type;
                if (var->type && var->type->is_struct() && rhs_t && rhs_t->is_pointer()) {
                    emit_copy_struct(vaddr, rhs, var->type);
                } else {
                    rhs = coerce_type(rhs, var->type);
                    builder_.create_store(rhs, vaddr);
                }
                return rhs;
            } else {
                ir::ValueId cur = builder_.create_load(vaddr, var_name);
                match_types(cur, rhs);
                ir::IRType* lt = builder_.get_function()->get_value(cur).type;
                ir::ValueId computed = 0;
                if (lt && lt->is_floating_point()) {
                    if (op == TOK_PLUS_ASSIGN) computed = builder_.create_fadd(cur, rhs);
                    else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_fsub(cur, rhs);
                    else if (op == TOK_MUL_ASSIGN) computed = builder_.create_fmul(cur, rhs);
                    else if (op == TOK_DIV_ASSIGN) computed = builder_.create_fdiv(cur, rhs);
                } else {
                    if (op == TOK_PLUS_ASSIGN) computed = builder_.create_add(cur, rhs);
                    else if (op == TOK_MINUS_ASSIGN) computed = builder_.create_sub(cur, rhs);
                    else if (op == TOK_MUL_ASSIGN) computed = builder_.create_mul(cur, rhs);
                    else if (op == TOK_DIV_ASSIGN) computed = builder_.create_sdiv(cur, rhs);
                }

                computed = coerce_type(computed, var->type);
                builder_.create_store(computed, vaddr);
                return computed;
            }
        }

        return parse_logical_or(error);
    }

} // namespace grass::codegen
