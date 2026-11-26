#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    bool ASTLowerer::parse_statement(std::string& error) {
        last_struct_name_.clear();
        if (match(TOK_SEMI)) return true;
        if (match_ident("volatile")) return parse_statement(error);

        if (match_ident("clear")) {
            if (match(TOK_LPAREN)) match(TOK_RPAREN);
            match(TOK_SEMI);
            builder_.create_call("clear", ctx_.get_void_type(), {});
            return true;
        }
        if (match_ident("not_clear")) {
            if (peek().kind == TOK_LBRACE) {
                builder_.create_call("not_clear_on", ctx_.get_void_type(), {});
                bool ok = parse_statement(error);
                builder_.create_call("not_clear_off", ctx_.get_void_type(), {});
                return ok;
            }
            if (match(TOK_LPAREN)) {
                if (peek().kind != TOK_RPAREN) parse_expression(error);
                match(TOK_RPAREN);
            }
            match(TOK_SEMI);
            builder_.create_call("not_clear_on", ctx_.get_void_type(), {});
            return true;
        }
        if (peek().kind == TOK_IDENT &&
            (peek().text == "window" || peek().text == "tabs" || peek().text == "row" ||
             peek().text == "col" || peek().text == "panel" || peek().text == "group" ||
             peek().text == "card" || peek().text == "grid" || peek().text == "vbox" ||
             peek().text == "hbox" || peek().text == "header-bar" || peek().text == "tab" ||
             peek().text == "button" || peek().text == "label" || peek().text == "slider" ||
             peek().text == "checkbox" || peek().text == "progress" || peek().text == "enter" ||
             peek().text == "clock" || peek().text == "list" || peek().text == "scroll-box") &&
            (peek(1).kind == TOK_LBRACE || peek(1).kind == TOK_STRING ||
             peek(1).kind == TOK_LPAREN || peek(1).kind == TOK_ASSIGN)) {
            bool is_win = (peek().text == "window");
            if (peek(1).kind == TOK_LPAREN && !is_win &&
                peek().text != "tabs" && peek().text != "row" && peek().text != "col" &&
                peek().text != "panel" && peek().text != "group" && peek().text != "card" &&
                peek().text != "tab") {
                // Only treat widget(params) { ... } as UI-DSL if '{' follows ')'
                size_t scan = 2;
                int pd = 1;
                while (pd > 0 && peek((int)scan).kind != TOK_EOF) {
                    if (peek((int)scan).kind == TOK_LPAREN) pd++;
                    else if (peek((int)scan).kind == TOK_RPAREN) pd--;
                    scan++;
                }
                if (peek((int)scan).kind != TOK_LBRACE) goto not_ui_dsl_block;
            }
            advance(); // keyword
            while (peek().kind != TOK_LBRACE && peek().kind != TOK_SEMI && peek().kind != TOK_EOF) {
                if (match(TOK_LPAREN)) {
                    int pd = 1;
                    while (pd > 0 && peek().kind != TOK_EOF) {
                        if (peek().kind == TOK_LPAREN) pd++;
                        else if (peek().kind == TOK_RPAREN) pd--;
                        advance();
                    }
                } else {
                    advance();
                }
            }
            if (match(TOK_LBRACE)) {
                int depth = 1;
                while (depth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LBRACE) depth++;
                    else if (peek().kind == TOK_RBRACE) depth--;
                    advance();
                }
            }
            match(TOK_SEMI);
            if (is_win) builder_.create_call("gui_show", ctx_.get_void_type(), {});
            return true;
        }
        not_ui_dsl_block:
        if (peek().kind == TOK_IDENT && peek().text == "agent" && peek(1).kind == TOK_LBRACE) {
            advance(); advance(); // 'agent', '{'
            int depth = 1;
            while (depth > 0 && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LBRACE) depth++;
                else if (peek().kind == TOK_RBRACE) depth--;
                advance();
            }
            match(TOK_SEMI);
            return true;
        }
        if (peek().kind == TOK_IDENT && peek().text == "create_serv" && peek(1).kind == TOK_LPAREN) {
            advance(); advance(); // 'create_serv', '('
            int pdepth = 1;
            while (pdepth > 0 && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LPAREN) pdepth++;
                else if (peek().kind == TOK_RPAREN) pdepth--;
                advance();
            }
            if (peek().kind == TOK_LBRACE) {
                advance();
                int bdepth = 1;
                while (bdepth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LBRACE) bdepth++;
                    else if (peek().kind == TOK_RBRACE) bdepth--;
                    advance();
                }
            }
            match(TOK_SEMI);
            ir::ValueId ip_s = builder_.create_call(add_rodata_string("127.0.0.1"), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
            ir::ValueId empty_s = builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
            builder_.create_call("create_serv", ctx_.get_i64_type(), { ip_s, builder_.create_const_i64(8443), empty_s, empty_s });
            return true;
        }
        if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_SEMI) {
            advance();
            match(TOK_SEMI);
            return true;
        }
        if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_IDENT) {
            advance();
            std::string expr_err;
            parse_expression(expr_err);
            if (!expr_err.empty()) { error = expr_err; return false; }
            match(TOK_SEMI);
            return true;
        }

        // Block { ... }
        if (match(TOK_LBRACE)) {
            push_scope();
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parse_statement(error)) return false;
            }
            consume(TOK_RBRACE, "Expected '}' closing block!", error);
            pop_scope(true);
            return true;
        }

        // Stream Output: gout << expr << endl; or std::gout << expr << std::endl;
        bool is_stream_out = false;
        if (match_ident("gout") || match_ident("cout")) {
            is_stream_out = true;
        } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                   peek(1).kind == TOK_COLON_COLON &&
                   (peek(2).text == "gout" || peek(2).text == "cout")) {
            advance(); // std
            advance(); // ::
            advance(); // gout / cout
            is_stream_out = true;
        }

        if (is_stream_out) {
            bool prev_io = in_stream_io_;
            in_stream_io_ = true;
            while (match(TOK_SHL)) {
                bool is_endl = false;
                if (match_ident("endl")) {
                    is_endl = true;
                } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                           peek(1).kind == TOK_COLON_COLON && peek(2).text == "endl") {
                    advance(); // std
                    advance(); // ::
                    advance(); // endl
                    is_endl = true;
                }

                if (is_endl) {
                    builder_.create_call("grass_print_newline", ctx_.get_void_type(), {});
                } else if (peek().kind == TOK_STRING && peek().text == "\n" &&
                           (peek(1).kind == TOK_SHL || peek(1).kind == TOK_SEMI)) {
                    advance();
                    ir::ValueId nl_ch = builder_.create_const_int(ctx_.get_i64_type(), 10);
                    builder_.create_call("grass_print_char", ctx_.get_void_type(), { nl_ch });
                } else {
                    ir::ValueId val = parse_expression(error);
                    if (!error.empty()) {
                        in_stream_io_ = prev_io;
                        return false;
                    }
                    if (val != 0) {
                        ir::IRType* vtype = builder_.get_function()->get_value(val).type;
                        if (vtype->is_pointer()) {
                            if (vtype->element_type && vtype->element_type->is_i8()) {
                                // string / const char* / uint8*
                                ir::ValueId arg_val = coerce_type(val, ctx_.get_ptr_type());
                                builder_.create_call("grass_print_str", ctx_.get_void_type(), { arg_val });
                            } else {
                                // Pointer (T*) in hex format: 0x...
                                ir::ValueId arg_val = coerce_type(val, ctx_.get_i64_type());
                                builder_.create_call("grass_print_ptr", ctx_.get_void_type(), { arg_val });
                            }
                        } else if (vtype->is_floating_point()) {
                            ir::ValueId arg_val = coerce_type(val, ctx_.get_f64_type());
                            builder_.create_call("grass_print_double", ctx_.get_void_type(), { arg_val });
                        } else if (vtype->is_bool()) {
                            ir::ValueId arg_val = builder_.create_zext(val, ctx_.get_i64_type());
                            builder_.create_call("grass_print_bool", ctx_.get_void_type(), { arg_val });
                        } else {
                            // Integer (int, int64, int32, etc.)
                            ir::ValueId arg_val = coerce_type(val, ctx_.get_i64_type());
                            builder_.create_call("grass_print_int", ctx_.get_void_type(), { arg_val });
                        }
                    }
                }
            }
            in_stream_io_ = prev_io;
            match(TOK_SEMI);
            return true;
        }

        // Stream Input: gin >> var; or std::gin >> var;
        bool is_stream_in = false;
        if (match_ident("gin") || match_ident("cin")) {
            is_stream_in = true;
        } else if (peek().kind == TOK_IDENT && peek().text == "std" &&
                   peek(1).kind == TOK_COLON_COLON &&
                   (peek(2).text == "gin" || peek(2).text == "cin")) {
            advance(); // std
            advance(); // ::
            advance(); // gin / cin
            is_stream_in = true;
        }

        if (is_stream_in) {
            bool prev_io = in_stream_io_;
            in_stream_io_ = true;
            while (match(TOK_SHR)) {
                if (peek().kind == TOK_IDENT) {
                    std::string var_name = advance().text;
                    const VarBinding* b = lookup_var(var_name);
                    if (!b) {
                        error = "Line " + std::to_string(peek().line) + ": Undeclared variable '" + var_name + "' in stream input!";
                        in_stream_io_ = prev_io;
                        return false;
                    }
                    ir::ValueId read_val = builder_.create_call("grass_read_int", ctx_.get_i64_type(), {});
                    if (b->type->is_i32()) {
                        read_val = builder_.create_trunc(read_val, ctx_.get_i32_type());
                    }
                    builder_.create_store(read_val, var_addr(b));
                }
            }
            in_stream_io_ = prev_io;
            match(TOK_SEMI);
            return true;
        }

        // Local enum definition
        if (peek().kind == TOK_IDENT && peek().text == "enum") {
            return parse_enum_definition(error);
        }

        // OOP Heap Destructor: delete ptr; (keep !baremetal! delete("path") as function call)
        if (peek().kind == TOK_IDENT && peek().text == "delete" && peek(1).kind != TOK_LPAREN) {
            advance(); // 'delete'
            last_struct_name_.clear();
            ir::ValueId target_ptr = parse_expression(error);
            if (!target_ptr) return false;
            match(TOK_SEMI);
            if (oop_mgr_ && !last_struct_name_.empty() && oop_mgr_->has_class(last_struct_name_)) {
                oop_mgr_->emit_delete_object(builder_, target_ptr, last_struct_name_);
            } else {
                builder_.create_call("grass_free", ctx_.get_void_type(), { coerce_type(target_ptr, ctx_.get_ptr_type()) });
            }
            return true;
        }

        // Dynamic Vector Declaration: vec<T> v; / vector<T> v; / vec<T>* vp = &v;
        if (is_vec_type_start()) {
            ir::IRType* elem_t = nullptr;
            std::string elem_struct, vec_sname;
            ir::IRType* vst = parse_vec_ir_type(elem_t, elem_struct, vec_sname);
            bool is_explicit_ptr = false;
            ir::IRType* vtype = vst;
            while (match(TOK_STAR)) {
                vtype = ctx_.get_ptr_type(vtype);
                is_explicit_ptr = true;
            }
            while (peek().kind == TOK_IDENT) {
                std::string var_name = advance().text;
                ir::ValueId alloca_id = builder_.create_alloca(vtype, 1, 0, var_name);
                if (is_explicit_ptr) {
                    declare_var(var_name, alloca_id, vtype, vec_sname, false, true, elem_t, elem_struct);
                    if (match(TOK_ASSIGN)) {
                        ir::ValueId init_val = coerce_type(parse_expression(error), vtype);
                        if (!init_val) return false;
                        builder_.create_store(init_val, alloca_id);
                    } else {
                        builder_.create_store(builder_.create_const_int(vtype, 0), alloca_id);
                    }
                } else {
                    declare_var(var_name, alloca_id, vtype, vec_sname, true, true, elem_t, elem_struct);
                    emit_zero_struct(alloca_id, vst);
                    if (match(TOK_ASSIGN)) {
                        parse_expression(error);
                    }
                }
                if (!match(TOK_COMMA)) break;
            }
            match(TOK_SEMI);
            return true;
        }

        // Variable Declaration
        bool is_struct_decl = (oop_mgr_ && oop_mgr_->has_class(peek().text));
        bool is_enum_decl = (enum_defs_.count(peek().text) > 0);
        bool is_vdecl = false;
        if (is_var_keyword(peek().text) || is_type_or_func_keyword(peek().text) || is_struct_decl || is_enum_decl) {
            size_t off = 1;
            while (peek(off).kind == TOK_STAR) off++;
            if (peek(off).kind == TOK_IDENT) {
                is_vdecl = true;
            }
        }
        if (is_vdecl) {
            std::string type_name = advance().text;
            ir::IRType* vtype = nullptr;
            std::string struct_name = "";
            bool is_explicit_ptr = false;
            if (is_struct_decl) {
                struct_name = type_name;
                const auto* cd = oop_mgr_->get_class(type_name);
                vtype = cd ? cd->ir_struct_type : ctx_.get_i64_type();
            } else {
                vtype = parse_type_name(type_name);
            }
            while (match(TOK_STAR)) {
                vtype = ctx_.get_ptr_type(vtype);
                is_explicit_ptr = true;
            }

            while (peek().kind == TOK_IDENT || peek().kind == TOK_STAR) {
                ir::IRType* cur_vtype = vtype;
                bool cur_explicit_ptr = is_explicit_ptr;
                while (match(TOK_STAR)) {
                    cur_vtype = ctx_.get_ptr_type(cur_vtype);
                    cur_explicit_ptr = true;
                }
                if (peek().kind != TOK_IDENT) break;
                std::string var_name = advance().text;

                auto has_raii_dtor = [&](const std::string& sname, bool explicit_ptr) -> bool {
                    if (explicit_ptr || sname.empty() || !oop_mgr_) return false;
                    const auto* cd = oop_mgr_->get_class(sname);
                    return cd && cd->has_destructor;
                };

                if (type_name == "take") {
                    if (!match(TOK_ASSIGN)) {
                        error = "Variable '" + var_name + "' declared with 'take' requires an explicit initializer!";
                        return false;
                    }
                    last_struct_name_.clear();
                    last_expr_is_borrowed_ptr_ = false;
                    ir::ValueId init_val = parse_expression(error);
                    if (!init_val) {
                        if (error.empty()) error = "Failed to evaluate initializer for variable '" + var_name + "'";
                        return false;
                    }
                    ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                    if (!actual_t || actual_t->is_void()) {
                        error = "Initializer for 'take " + var_name + "' cannot have void type!";
                        return false;
                    }
                    cur_vtype = actual_t;
                    if (cur_vtype->is_struct()) {
                        struct_name = cur_vtype->struct_name;
                    } else if (cur_vtype->is_pointer() && !last_struct_name_.empty() && oop_mgr_ && oop_mgr_->has_class(last_struct_name_)) {
                        struct_name = last_struct_name_;
                    }
                    bool is_ptr_or_borrowed = cur_vtype->is_pointer() || last_expr_is_borrowed_ptr_;
                    bool raii = has_raii_dtor(struct_name, is_ptr_or_borrowed);
                    bool is_arr = expr_array_elem_types_.count(init_val) > 0;
                    ir::IRType* arr_et = is_arr ? expr_array_elem_types_[init_val] : nullptr;
                    std::string arr_es = expr_array_elem_structs_.count(init_val) ? expr_array_elem_structs_[init_val] : "";
                    ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                    declare_var(var_name, alloca_id, cur_vtype, struct_name, raii, false, nullptr, "", is_arr, arr_et, arr_es);
                    builder_.create_store(init_val, alloca_id);
                } else if (is_struct_decl && !cur_explicit_ptr && match(TOK_LPAREN)) {
                    std::vector<ir::ValueId> ctor_args;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        while (true) {
                            ir::ValueId arg = parse_expression(error);
                            if (!arg) return false;
                            ctor_args.push_back(arg);
                            if (!match(TOK_COMMA)) break;
                        }
                    }
                    if (!consume(TOK_RPAREN, "Expected ')' closing constructor arguments!", error)) return false;
                    bool raii = has_raii_dtor(struct_name, false);
                    ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                    declare_var(var_name, alloca_id, cur_vtype, struct_name, raii);
                    emit_zero_struct(alloca_id, cur_vtype);
                    const auto* cd = oop_mgr_ ? oop_mgr_->get_class(struct_name) : nullptr;
                    if (cd && cd->has_vtable) {
                        oop_mgr_->emit_vtable_init(builder_, alloca_id, struct_name);
                    }
                    emit_ctor_call(alloca_id, struct_name, ctor_args);
                } else if (match(TOK_ASSIGN)) {
                    last_struct_name_.clear();
                    last_expr_is_borrowed_ptr_ = false;
                    ir::ValueId init_val = parse_expression(error);
                    if (!init_val) {
                        if (error.empty()) error = "Failed to evaluate initializer for variable '" + var_name + "'";
                        return false;
                    }
                    if (type_name == "var" || type_name == "set") {
                        ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                        if (actual_t && !actual_t->is_void()) {
                            cur_vtype = actual_t;
                        }
                        if (cur_vtype && cur_vtype->is_struct()) {
                            struct_name = cur_vtype->struct_name;
                        } else if (cur_vtype && cur_vtype->is_pointer() && !last_struct_name_.empty() && oop_mgr_ && oop_mgr_->has_class(last_struct_name_)) {
                            struct_name = last_struct_name_;
                        }
                    }
                    bool is_ptr_or_borrowed = cur_explicit_ptr || (cur_vtype && cur_vtype->is_pointer()) || last_expr_is_borrowed_ptr_;
                    bool raii = has_raii_dtor(struct_name, is_ptr_or_borrowed);
                    bool is_arr = expr_array_elem_types_.count(init_val) > 0;
                    ir::IRType* arr_et = is_arr ? expr_array_elem_types_[init_val] : nullptr;
                    std::string arr_es = expr_array_elem_structs_.count(init_val) ? expr_array_elem_structs_[init_val] : "";
                    ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                    declare_var(var_name, alloca_id, cur_vtype, struct_name, raii, false, nullptr, "", is_arr, arr_et, arr_es);
                    ir::IRType* init_t = builder_.get_function()->get_value(init_val).type;
                    if (cur_vtype && cur_vtype->is_struct() && init_t && init_t->is_pointer()) {
                        emit_copy_struct(alloca_id, init_val, cur_vtype);
                    } else {
                        init_val = coerce_type(init_val, cur_vtype);
                        builder_.create_store(init_val, alloca_id);
                    }
                } else {
                    bool raii = has_raii_dtor(struct_name, cur_explicit_ptr);
                    ir::ValueId alloca_id = builder_.create_alloca(cur_vtype, 1, 0, var_name);
                    declare_var(var_name, alloca_id, cur_vtype, struct_name, raii);
                    if (cur_vtype && cur_vtype->is_struct()) {
                        emit_zero_struct(alloca_id, cur_vtype);
                        if (!struct_name.empty() && !cur_explicit_ptr && oop_mgr_) {
                            const auto* cd = oop_mgr_->get_class(struct_name);
                            if (cd && cd->has_vtable) {
                                oop_mgr_->emit_vtable_init(builder_, alloca_id, struct_name);
                            }
                            emit_ctor_call(alloca_id, struct_name, {});
                        }
                    } else {
                        ir::ValueId default_val = builder_.create_const_int(cur_vtype, 0);
                        builder_.create_store(default_val, alloca_id);
                    }
                }
                if (!match(TOK_COMMA)) break;
            }
            match(TOK_SEMI);
            return true;
        }

        // If Statement
        if (match_ident("if") || match_ident("check")) {
            match(TOK_LPAREN);
            ir::ValueId cond = parse_expression(error);
            if (!cond) return false;
            match(TOK_RPAREN);

            ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
            if (c_type && !c_type->is_bool()) {
                ir::ValueId zero = builder_.create_const_int(c_type, 0);
                cond = builder_.create_icmp_ne(cond, zero);
            }

            ir::BasicBlock* b_then = builder_.create_block("if_then");
            ir::BasicBlock* b_else = builder_.create_block("if_else");
            ir::BasicBlock* b_merge = builder_.create_block("if_merge");

            bool has_else = false;
            // Pre-check if else is coming
            // Emit branch
            builder_.create_branch(cond, b_then, b_else);

            // Then Block
            builder_.set_insert_point(b_then);
            push_scope();
            if (!parse_statement(error)) return false;
            if (!builder_.get_insert_block()->has_terminator()) {
                pop_scope(true);
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_merge);
                }
            } else {
                pop_scope(false);
            }

            // Else Block
            builder_.set_insert_point(b_else);
            if (match_ident("else")) {
                has_else = true;
                push_scope();
                if (!parse_statement(error)) return false;
                if (!builder_.get_insert_block()->has_terminator()) {
                    pop_scope(true);
                    if (!builder_.get_insert_block()->has_terminator()) {
                        builder_.create_jump(b_merge);
                    }
                } else {
                    pop_scope(false);
                }
            } else if (!builder_.get_insert_block()->has_terminator()) {
                builder_.create_jump(b_merge);
            }

            if (!has_else) {
                // Else block was just a jump to merge
                b_else->name = "if_skip";
            }

            builder_.set_insert_point(b_merge);
            return true;
        }

        // While Loop
        if (match_ident("while")) {
            match(TOK_LPAREN);
            ir::BasicBlock* b_cond = builder_.create_block("while_cond");
            ir::BasicBlock* b_body = builder_.create_block("while_body");
            ir::BasicBlock* b_exit = builder_.create_block("while_exit");

            builder_.create_jump(b_cond);

            // Condition block
            builder_.set_insert_point(b_cond);
            ir::ValueId cond = parse_expression(error);
            if (!cond) return false;
            match(TOK_RPAREN);

            ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
            if (c_type && !c_type->is_bool()) {
                ir::ValueId zero = builder_.create_const_int(c_type, 0);
                cond = builder_.create_icmp_ne(cond, zero);
            }
            builder_.create_branch(cond, b_body, b_exit);

            // Body block
            size_t loop_depth = scopes_.size();
            loop_stack_.push_back({ b_cond, b_exit, loop_depth, loop_depth });
            builder_.set_insert_point(b_body);
            push_scope();
            if (!parse_statement(error)) return false;
            if (!builder_.get_insert_block()->has_terminator()) {
                pop_scope(true);
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_cond);
                }
            } else {
                pop_scope(false);
            }
            loop_stack_.pop_back();

            builder_.set_insert_point(b_exit);
            return true;
        }

        // For Loop
        if (match_ident("for")) {
            if (!consume(TOK_LPAREN, "Expected '(' in for loop!", error)) return false;

            push_scope(); // for loop init scope
            if (peek().kind != TOK_SEMI) {
                if (!parse_statement(error)) return false;
            } else {
                advance(); // skip ';'
            }

            ir::BasicBlock* b_cond = builder_.create_block("for_cond");
            ir::BasicBlock* b_body = builder_.create_block("for_body");
            ir::BasicBlock* b_step = builder_.create_block("for_step");
            ir::BasicBlock* b_exit = builder_.create_block("for_exit");

            builder_.create_jump(b_cond);

            // Cond Block
            builder_.set_insert_point(b_cond);
            ir::ValueId cond = 0;
            if (peek().kind != TOK_SEMI) {
                cond = parse_expression(error);
                if (!error.empty()) return false;
                ir::IRType* c_type = builder_.get_function()->get_value(cond).type;
                if (c_type && !c_type->is_bool()) {
                    ir::ValueId zero = builder_.create_const_int(c_type, 0);
                    cond = builder_.create_icmp_ne(cond, zero);
                }
            } else {
                cond = builder_.create_const_i1(true);
            }
            match(TOK_SEMI);
            builder_.create_branch(cond, b_body, b_exit);

            // Step Tokens Range
            size_t step_start = pos_;
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                advance();
            }
            size_t step_end = pos_;
            if (!consume(TOK_RPAREN, "Expected ')' closing for loop header!", error)) return false;

            // Body Block
            size_t body_depth = scopes_.size();
            loop_stack_.push_back({ b_step, b_exit, body_depth, body_depth });
            builder_.set_insert_point(b_body);
            push_scope();
            if (!parse_statement(error)) return false;
            if (!builder_.get_insert_block()->has_terminator()) {
                pop_scope(true);
                if (!builder_.get_insert_block()->has_terminator()) {
                    builder_.create_jump(b_step);
                }
            } else {
                pop_scope(false);
            }
            loop_stack_.pop_back();

            // Step Block
            builder_.set_insert_point(b_step);
            if (step_start < step_end) {
                size_t saved_pos = pos_;
                pos_ = step_start;
                std::string step_err;
                parse_expression(step_err);
                if (!step_err.empty()) {
                    error = step_err;
                    return false;
                }
                pos_ = saved_pos;
            }
            builder_.create_jump(b_cond);

            builder_.set_insert_point(b_exit);
            pop_scope(true);
            return true;
        }

        // Break / Continue
        if (match_ident("break") || match_ident("stop")) {
            match(TOK_SEMI);
            if (loop_stack_.empty()) {
                error = "Break statement outside loop!";
                return false;
            }
            emit_scopes_cleanup_to(loop_stack_.back().break_scope_depth);
            builder_.create_jump(loop_stack_.back().b_exit);
            return true;
        }

        if (match_ident("continue") || match_ident("next")) {
            match(TOK_SEMI);
            if (loop_stack_.empty()) {
                error = "Continue statement outside loop!";
                return false;
            }
            emit_scopes_cleanup_to(loop_stack_.back().continue_scope_depth);
            builder_.create_jump(loop_stack_.back().b_step);
            return true;
        }

        // Return Statement
        if (match_ident("return")) {
            if (peek().kind != TOK_SEMI) {
                ir::ValueId ret_val = parse_expression(error);
                if (!error.empty()) return false;
                ir::IRType* exp_t = builder_.get_function()->return_type;
                if (exp_t && !exp_t->is_void()) {
                    ret_val = coerce_type(ret_val, exp_t);
                }
                emit_scopes_cleanup_to(0);
                if (mem_mgr_) mem_mgr_->emit_all_cleanups(builder_);
                if (!current_dtor_struct_.empty()) {
                    const VarBinding* tv = lookup_var("this");
                    if (tv) {
                        ir::ValueId tptr = builder_.create_load(tv->alloca_id, "this_ret_dtor");
                        emit_struct_field_dtors(tptr, current_dtor_struct_);
                    }
                }
                builder_.create_ret(ret_val);
            } else {
                emit_scopes_cleanup_to(0);
                if (mem_mgr_) mem_mgr_->emit_all_cleanups(builder_);
                if (!current_dtor_struct_.empty()) {
                    const VarBinding* tv = lookup_var("this");
                    if (tv) {
                        ir::ValueId tptr = builder_.create_load(tv->alloca_id, "this_ret_dtor");
                        emit_struct_field_dtors(tptr, current_dtor_struct_);
                    }
                }
                builder_.create_ret_void();
            }
            match(TOK_SEMI);
            return true;
        }

        // Expression Statement / Assignment / Function Call
        std::string expr_err;
        parse_expression(expr_err);
        if (!expr_err.empty()) {
            error = expr_err;
            return false;
        }
        match(TOK_SEMI);
        return true;
    }

} // namespace grass::codegen
