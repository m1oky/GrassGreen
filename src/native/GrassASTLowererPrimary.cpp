#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    ir::ValueId ASTLowerer::parse_primary(std::string& error) {
        // Parenthesized expression: ( expr )
        if (match(TOK_LPAREN)) {
            bool prev_io = in_stream_io_;
            in_stream_io_ = false;
            ir::ValueId inner = parse_expression(error);
            in_stream_io_ = prev_io;
            consume(TOK_RPAREN, "Expected ')' closing expression!", error);
            return inner;
        }

        // Array literal: [] or [elem1, elem2, ...]
        if (match(TOK_LBRACKET)) {
            std::vector<ir::ValueId> elems;
            std::vector<std::string> elem_structs;
            ir::IRType* elem_t = nullptr;
            std::string elem_struct;
            if (!match(TOK_RBRACKET)) {
                do {
                    last_struct_name_.clear();
                    ir::ValueId ev = parse_expression(error);
                    if (!ev) return 0;
                    elems.push_back(ev);
                    elem_structs.push_back(last_struct_name_);
                    ir::IRType* et = builder_.get_function()->has_value(ev)
                        ? builder_.get_function()->get_value(ev).type : nullptr;
                    if (et) {
                        if (!elem_t) {
                            elem_t = et;
                            elem_struct = last_struct_name_;
                        } else if (!elem_t->equals(et)) {
                            if (elem_t->is_numeric() && et->is_numeric() &&
                                (elem_t->is_floating_point() || et->is_floating_point())) {
                                elem_t = ctx_.get_f64_type();
                                elem_struct.clear();
                            } else {
                                elem_t = ctx_.get_i64_type();
                                elem_struct.clear();
                            }
                        }
                    }
                } while (match(TOK_COMMA));
                if (!consume(TOK_RBRACKET, "Expected ']' closing array literal!", error)) return 0;
            }
            ir::ValueId arr = builder_.create_call("grass_array_create", ctx_.get_ptr_type(), {});
            for (size_t ei = 0; ei < elems.size(); ++ei) {
                ir::ValueId pushed = encode_array_slot(elems[ei], elem_t);
                builder_.create_call("grass_array_push", ctx_.get_void_type(), { arr, pushed });
            }
            if (elem_t) {
                expr_array_elem_types_[arr] = elem_t;
                if (!elem_struct.empty()) {
                    expr_array_elem_structs_[arr] = elem_struct;
                }
            }
            last_struct_name_.clear();
            return arr;
        }

        // Number literal
        if (peek().kind == TOK_NUMBER) {
            last_struct_name_.clear();
            const auto& tok = advance();
            bool is_float = (tok.text.find('.') != std::string::npos ||
                             tok.text.find('e') != std::string::npos ||
                             tok.text.find('E') != std::string::npos);
            if (!is_float) {
                try {
                    int64_t ival = std::stoll(tok.text);
                    return builder_.create_const_i64(ival);
                } catch (...) {}
            }
            double val = tok.num_val;
            if (!is_float && std::floor(val) == val) {
                return builder_.create_const_i64(static_cast<int64_t>(val));
            }
            return builder_.create_const_f64(val);
        }

        // String literal
        if (peek().kind == TOK_STRING) {
            last_struct_name_.clear();
            std::string s = advance().text;
            std::string sym = add_rodata_string(s);
            return builder_.create_call(sym, ctx_.get_ptr_type(ctx_.get_i8_type()), {});
        }

        // Boolean literals: true / false
        if (peek().kind == TOK_IDENT && peek().text == "true") {
            last_struct_name_.clear();
            advance();
            return builder_.create_const_i1(true);
        }
        if (peek().kind == TOK_IDENT && peek().text == "false") {
            last_struct_name_.clear();
            advance();
            return builder_.create_const_i1(false);
        }

        // Nullptr literal
        if (peek().kind == TOK_NULLPTR || (peek().kind == TOK_IDENT && (peek().text == "nullptr" || peek().text == "null"))) {
            last_struct_name_.clear();
            advance();
            return builder_.create_const_int(ctx_.get_ptr_type(ctx_.get_void_type()), 0);
        }

        // Sizeof operator
        if (match(TOK_SIZEOF)) {
            last_struct_name_.clear();
            if (!match(TOK_LPAREN)) {
                error = "Expected '(' after sizeof";
                return 0;
            }
            bool is_type = is_type_or_func_keyword(peek().text) || (oop_mgr_ && oop_mgr_->has_class(peek().text));
            if (is_type) {
                std::string tname = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
                }
                ir::IRType* t = parse_type_name(tname);
                while (match(TOK_STAR)) {
                    t = ctx_.get_ptr_type(t);
                }
                consume(TOK_RPAREN, "Expected ')' after sizeof(Type)", error);
                uint64_t sz = t ? t->size() : 8;
                if (sz == 0 && t && t->is_void()) sz = 1;
                return builder_.create_const_i64(static_cast<int64_t>(sz));
            } else {
                ir::ValueId expr_val = parse_expression(error);
                consume(TOK_RPAREN, "Expected ')' after sizeof(expr)", error);
                uint64_t sz = 8;
                if (expr_val && builder_.get_function()->has_value(expr_val)) {
                    ir::IRType* et = builder_.get_function()->get_value(expr_val).type;
                    if (et) sz = et->size();
                }
                return builder_.create_const_i64(static_cast<int64_t>(sz));
            }
        }

        // Heap allocation: new ClassName(args...) or new ClassName
        if (match_ident("new")) {
            if (peek().kind != TOK_IDENT) {
                error = "Expected class or type name after 'new'!";
                return 0;
            }
            std::string cname = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) cname += "::" + advance().text;
            }
            std::vector<ir::ValueId> args;
            if (match(TOK_LPAREN)) {
                if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                    while (true) {
                        ir::ValueId arg = parse_expression(error);
                        if (!arg) return 0;
                        args.push_back(arg);
                        if (!match(TOK_COMMA)) break;
                    }
                }
                consume(TOK_RPAREN, "Expected ')' closing 'new' arguments!", error);
            }
            if (oop_mgr_ && oop_mgr_->has_class(cname)) {
                const auto* cd = oop_mgr_->get_class(cname);
                ir::ValueId raw_obj = mem_mgr_
                    ? mem_mgr_->emit_alloc_const(builder_, cd->total_size, cname + "_new_raw")
                    : builder_.create_call("grass_alloc", ctx_.get_ptr_type(), { builder_.create_const_i64(static_cast<int64_t>(cd->total_size)) });
                ir::IRType* ptr_t = ctx_.get_ptr_type(cd->ir_struct_type);
                ir::ValueId obj_ptr = builder_.create_bitcast(raw_obj, ptr_t, cname + "_new");
                emit_zero_struct(obj_ptr, cd->ir_struct_type);
                if (cd->has_vtable) {
                    oop_mgr_->emit_vtable_init(builder_, obj_ptr, cname);
                }
                emit_ctor_call(obj_ptr, cname, args);
                last_struct_name_ = cname;
                last_expr_is_borrowed_ptr_ = true;
                return obj_ptr;
            }
            ir::IRType* pt = parse_type_name(cname);
            uint64_t sz = get_ir_type_size_bytes(pt);
            if (sz < 8) sz = 8;
            ir::ValueId raw_ptr = mem_mgr_
                ? mem_mgr_->emit_alloc_const(builder_, sz, "new_raw")
                : builder_.create_call("grass_alloc", ctx_.get_ptr_type(), { builder_.create_const_i64(static_cast<int64_t>(sz)) });
            last_struct_name_.clear();
            last_expr_is_borrowed_ptr_ = true;
            return builder_.create_bitcast(raw_ptr, ctx_.get_ptr_type(pt), "new_ptr");
        }

        // Identifier or Function Call
        if (peek().kind == TOK_IDENT) {
            std::string ident = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) {
                    ident += "::" + advance().text;
                }
            }

            size_t enum_sep = ident.rfind("::");
            if (enum_sep != std::string::npos) {
                std::string ename = ident.substr(0, enum_sep);
                std::string entry = ident.substr(enum_sep + 2);
                auto eit = enum_defs_.find(ename);
                if (eit != enum_defs_.end()) {
                    auto vit = eit->second.find(entry);
                    if (vit != eit->second.end()) {
                        last_struct_name_.clear();
                        return builder_.create_const_i64(vit->second);
                    }
                }
            }

            if (peek().kind == TOK_DOT && peek(1).kind == TOK_IDENT && enum_defs_.count(ident)) {
                auto vit = enum_defs_[ident].find(peek(1).text);
                if (vit != enum_defs_[ident].end()) {
                    advance(); // '.'
                    advance(); // entry
                    last_struct_name_.clear();
                    return builder_.create_const_i64(vit->second);
                }
            }

            // Member access ahead? (e.g. hero.name or ptr->name)
            if (peek().kind == TOK_DOT || peek().kind == TOK_ARROW) {
                const VarBinding* var = lookup_var(ident);
                if (var) {
                    last_struct_name_ = var->struct_type_name;
                    ir::ValueId inst_ptr = var->alloca_id;
                    if (var->type && var->type->is_pointer()) {
                        inst_ptr = builder_.create_load(var->alloca_id, ident + "_ptr");
                    }
                    return inst_ptr;
                }
                if (!current_class_context_.empty() && oop_mgr_) {
                    const VarBinding* this_var = lookup_var("this");
                    const auto* cd = oop_mgr_->get_class(current_class_context_);
                    if (this_var && cd) {
                        auto fit = cd->field_indices.find(ident);
                        if (fit != cd->field_indices.end()) {
                            ir::ValueId this_ptr = builder_.create_load(this_var->alloca_id, "this_ptr");
                            const auto& fld = cd->fields[fit->second];
                            if (fld.type && fld.type->is_struct()) {
                                last_struct_name_ = fld.type->struct_name;
                                return oop_mgr_->emit_field_gep(builder_, this_ptr, current_class_context_, ident);
                            } else if (fld.type && fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                last_struct_name_ = fld.type->element_type->struct_name;
                                return oop_mgr_->emit_load_field(builder_, this_ptr, current_class_context_, ident);
                            }
                            last_struct_name_.clear();
                            return oop_mgr_->emit_load_field(builder_, this_ptr, current_class_context_, ident);
                        }
                    }
                }
                error = "Undeclared identifier: " + ident;
                return 0;
            }

            // Function call: ident(args)
            if (match(TOK_LPAREN)) {
                std::vector<ir::ValueId> args;
                if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                    while (true) {
                        ir::ValueId arg = parse_expression(error);
                        if (!arg) return 0;
                        args.push_back(arg);
                        if (!match(TOK_COMMA)) break;
                    }
                }
                consume(TOK_RPAREN, "Expected ')' closing function arguments!", error);

                std::string target_call_name = resolve_callee(ident);
                ir::IRType* call_ret_type = ctx_.get_i64_type();
                if ((ident == "to_str" || ident == "to_string") && !args.empty()) {
                    ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                    if (at0 && at0->is_floating_point()) {
                        ident = "grass_float_to_str";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    } else if (at0 && at0->is_pointer()) {
                        last_struct_name_.clear();
                        return args[0];
                    } else {
                        ident = "int_to_str";
                        target_call_name = ident;
                        call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                    }
                } else if (ident == "upper" && !args.empty()) {
                    ident = "grass_to_upper";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                } else if (ident == "lower" && !args.empty()) {
                    ident = "grass_to_lower";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                } else if (ident == "trim" && !args.empty()) {
                    ident = "grass_trim";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                } else if (ident == "len" || ident == "size") {
                    ident = "grass_array_len";
                    target_call_name = ident;
                } else if (ident == "alloc") {
                    ident = "grass_alloc";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_ptr_type();
                } else if (ident == "free") {
                    ident = "grass_free";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_void_type();
                } else if (ident == "realloc") {
                    ident = "grass_realloc";
                    target_call_name = ident;
                    call_ret_type = ctx_.get_ptr_type();
                }

                if (oop_mgr_ && oop_mgr_->has_class(ident)) {
                    const auto* cd = oop_mgr_->get_class(ident);
                    ir::ValueId obj_ptr = builder_.create_alloca(cd->ir_struct_type, 1, static_cast<uint32_t>(cd->alignment), ident + "_tmp");
                    emit_zero_struct(obj_ptr, cd->ir_struct_type);
                    if (cd->has_vtable) {
                        oop_mgr_->emit_vtable_init(builder_, obj_ptr, ident);
                    }
                    emit_ctor_call(obj_ptr, ident, args);
                    last_struct_name_ = ident;
                    return obj_ptr;
                }

                // Implicit 'this' method call inside class method: foo(args) -> this->foo(args)
                if (!module_.get_function(target_call_name) && !current_class_context_.empty() && oop_mgr_) {
                    const auto* cd = oop_mgr_->get_class(current_class_context_);
                    const auto* md = cd ? cd->find_method(ident) : nullptr;
                    const VarBinding* this_var = lookup_var("this");
                    if (md && this_var) {
                        ir::ValueId this_ptr = builder_.create_load(this_var->alloca_id, "this_ptr");
                        ir::ValueId res = oop_mgr_->emit_call_method(builder_, this_ptr, current_class_context_, ident, args);
                        if (md->return_type && md->return_type->is_struct()) {
                            last_struct_name_ = md->return_type->struct_name;
                        } else if (md->return_type && md->return_type->is_pointer() && md->return_type->element_type && md->return_type->element_type->is_struct()) {
                            last_struct_name_ = md->return_type->element_type->struct_name;
                        } else {
                            last_struct_name_.clear();
                        }
                        return res;
                    }
                }

                // Overload / arity normalization for 3D Math, Gfx3D, and Physics
                if (ident == "m4_translate" && args.size() >= 3) {
                    ident = "m4_translate_xyz";
                    target_call_name = ident;
                } else if (ident == "m4_rotate" && args.size() >= 4) {
                    ident = "m4_rotate_xyz";
                    target_call_name = ident;
                } else if (ident == "m4_scale" && args.size() >= 3) {
                    ident = "m4_scale_xyz";
                    target_call_name = ident;
                } else if (ident == "g3d_clear" && args.size() >= 3) {
                    ident = "g3d_clear_rgb";
                    target_call_name = ident;
                } else if (ident == "g3d_key_down" && args.size() == 1) {
                    ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                    if (at0 && at0->is_pointer()) {
                        ident = "g3d_key_down_str";
                        target_call_name = ident;
                    }
                } else if (ident == "g3d_mouse_btn" && args.empty()) {
                    args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 0));
                } else if (ident == "g3d_camera" && args.size() >= 7) {
                    ident = "g3d_camera_xyz";
                    target_call_name = ident;
                } else if (ident == "g3d_sphere") {
                    if (args.size() == 1) args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 16));
                    else if (args.size() > 2) args.resize(2);
                } else if (ident == "g3d_plane" && args.size() >= 3) {
                    ident = "g3d_plane_segs";
                    target_call_name = ident;
                } else if (ident == "g3d_set_color" && args.size() >= 4) {
                    ident = "g3d_set_color_rgb";
                    target_call_name = ident;
                } else if (ident == "g3d_transform" && args.size() == 2) {
                    ident = "g3d_transform_mat";
                    target_call_name = ident;
                } else if (ident == "g3d_dir_light" && args.size() >= 4) {
                    ident = "g3d_dir_light_rgb";
                    target_call_name = ident;
                    if (args.size() == 4) args.push_back(builder_.create_const_f64(1.0));
                } else if (ident == "g3d_point_light" && args.size() == 4) {
                    args.push_back(builder_.create_const_f64(10.0));
                } else if (ident == "g3d_fog" && args.size() == 3) {
                    args.push_back(builder_.create_const_f64(0.05));
                } else if (ident == "g3d_bind_framebuffer" && args.size() == 3) {
                    args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 0));
                } else if (ident == "phys_world") {
                    if (args.empty()) {
                        args.push_back(builder_.create_const_f64(0.0));
                        args.push_back(builder_.create_const_f64(-9.81));
                        args.push_back(builder_.create_const_f64(0.0));
                    } else if (args.size() == 1) {
                        ir::ValueId gy = args[0];
                        args = {
                            builder_.create_const_f64(0.0),
                            gy,
                            builder_.create_const_f64(0.0)
                        };
                    }
                } else if (ident == "phys_step" && args.size() == 2) {
                    args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 8));
                } else if (ident == "phys_box" && args.size() == 7) {
                    args.push_back(builder_.create_const_f64(1.0));
                } else if (ident == "phys_sphere" && args.size() == 5) {
                    args.push_back(builder_.create_const_f64(1.0));
                } else if (ident == "phys_cloth" && args.size() == 4) {
                    args.push_back(builder_.create_const_int(ctx_.get_i64_type(), 8));
                } else if (ident == "phys_link" && args.size() == 4) {
                    args.push_back(builder_.create_const_f64(0.0));
                } else if ((ident == "phys_get_pos" || ident == "phys_get_rot") && args.size() == 1) {
                    args.insert(args.begin(), builder_.create_const_int(ctx_.get_i64_type(), 1));
                } else if ((ident == "phys_set_pos" || ident == "phys_set_vel" || ident == "phys_impulse") && args.size() == 4) {
                    args.insert(args.begin(), builder_.create_const_int(ctx_.get_i64_type(), 1));
                } else if (ident == "g2d_clear" && args.size() >= 3) {
                    ident = "g2d_clear_rgb";
                    target_call_name = ident;
                } else if (ident == "g2d_key_down" && args.size() == 1) {
                    ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                    if (at0 && at0->is_pointer()) {
                        ident = "g2d_key_down_str";
                        target_call_name = ident;
                    }
                } else if (ident == "g2d_mouse_btn" && args.empty()) {
                    args.push_back(builder_.create_const_i64(0));
                } else if (ident == "g2d_cam_set") {
                    while (args.size() < 2) args.push_back(builder_.create_const_f64(0.0));
                    if (args.size() == 2) args.push_back(builder_.create_const_f64(1.0));
                    if (args.size() == 3) args.push_back(builder_.create_const_f64(0.0));
                } else if (ident == "g2d_emit_particles") {
                    if (args.size() == 3) args.push_back(builder_.create_const_i64(0xFFFF00));
                    if (args.size() >= 5) {
                        ident = "g2d_emit_particles_ex";
                        target_call_name = ident;
                        if (args.size() == 5) args.push_back(builder_.create_const_f64(1.0));
                    }
                } else if (ident == "g2d_update_particles" && args.empty()) {
                    args.push_back(builder_.create_const_f64(1.0 / 60.0));
                } else if (ident == "g2d_draw_sprite") {
                    if (args.size() == 5) args.push_back(builder_.create_const_f64(0.0));
                    if (args.size() == 6) args.push_back(builder_.create_const_f64(1.0));
                } else if (ident == "g2d_bind_framebuffer" && args.size() == 3) {
                    args.push_back(builder_.create_const_i64(0));
                } else if (ident == "g2d_line" && args.size() == 5) {
                    args.push_back(builder_.create_const_i64(1));
                } else if (ident == "gfx_line" && args.size() == 5) {
                    args.push_back(builder_.create_const_i64(1));
                } else if (ident == "gfx_frame" && args.empty()) {
                    args.push_back(builder_.create_const_i64(60));
                } else if ((ident == "synth_play" || ident == "synth") && args.size() == 3) {
                    args.push_back(builder_.create_const_f64(80.0));
                } else if (ident == "tone" && args.size() == 2) {
                    args.push_back(builder_.create_const_f64(80.0));
                } else if (ident == "beep") {
                    if (args.empty()) args.push_back(builder_.create_const_f64(750.0));
                    if (args.size() == 1) args.push_back(builder_.create_const_i64(300));
                } else if (ident == "sound_play" && args.size() == 1) {
                    args.push_back(builder_.create_const_i64(0));
                } else if (ident == "bind_synth" && args.size() == 4) {
                    args.push_back(builder_.create_const_f64(80.0));
                } else if (ident == "attach_sound" && args.size() == 4) {
                    args.push_back(builder_.create_const_f64(80.0));
                } else if (ident == "tg_poll" && args.empty()) {
                    args.push_back(builder_.create_const_i64(2));
                } else if (ident == "tg_send_photo" && args.size() == 2) {
                    args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                } else if (ident == "sleep") {
                    ident = "grass_sleep_sec";
                    target_call_name = ident;
                    if (args.empty()) args.push_back(builder_.create_const_i64(0));
                } else if (ident == "ping" && args.size() == 1) {
                    args.push_back(builder_.create_const_i64(80));
                } else if (ident == "connect" && args.size() == 2) {
                    ident = "net_connect_host";
                    target_call_name = ident;
                } else if (ident == "accept" && args.empty()) {
                    ident = "net_accept_srv";
                    target_call_name = ident;
                } else if (ident == "net_close" && args.empty()) {
                    ident = "net_close_srv";
                    target_call_name = ident;
                } else if (ident == "web_compile" && args.size() == 1) {
                    args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                } else if (ident == "color" && !args.empty()) {
                    ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                    if (at0 && at0->is_pointer()) {
                        ident = "grass_bm_color_str";
                        target_call_name = ident;
                        if (args.size() == 1) {
                            args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                        }
                    } else {
                        ident = "grass_bm_color_int";
                        target_call_name = ident;
                        if (args.size() == 1) {
                            args.push_back(builder_.create_const_i64(0));
                        }
                    }
                } else if (ident == "draw_box") {
                    if (args.size() == 4) args.push_back(builder_.create_call(add_rodata_string(""), ctx_.get_ptr_type(ctx_.get_i8_type()), {}));
                    if (args.size() == 5) args.push_back(builder_.create_const_i64(10));
                    if (args.size() == 6) args.push_back(builder_.create_const_i64(0));
                } else if (ident == "progress_bar") {
                    if (args.size() == 4) args.push_back(builder_.create_const_i64(10));
                    if (args.size() == 5) args.push_back(builder_.create_const_i64(0));
                } else if (ident == "mouse_down") {
                    if (args.empty()) {
                        args.push_back(builder_.create_const_i64(1));
                    } else {
                        ir::IRType* at0 = builder_.get_function()->has_value(args[0]) ? builder_.get_function()->get_value(args[0]).type : nullptr;
                        if (at0 && at0->is_pointer()) {
                            ident = "mouse_down_str";
                            target_call_name = ident;
                        }
                    }
                } else if (ident == "mouse_cursor" && args.empty()) {
                    args.push_back(builder_.create_const_i64(1));
                } else if (ident == "not_clear" && args.empty()) {
                    args.push_back(builder_.create_const_i64(1));
                } else if ((ident == "thread_join" || ident == "mutex_lock") && args.size() == 1) {
                    args.push_back(builder_.create_const_i64(0));
                } else if ((ident == "thread" || ident == "async") && !args.empty()) {
                    ir::ValueId fn_arg = args[0];
                    ir::ValueId param_arg = (args.size() >= 2) ? coerce_type(args[1], ctx_.get_ptr_type()) : builder_.create_const_int(ctx_.get_ptr_type(), 0);
                    args = { coerce_type(fn_arg, ctx_.get_ptr_type()), param_arg };
                    ident = "grass_native_thread_spawn";
                    target_call_name = ident;
                }

                const native::NativeFunctionDesc* std_fn = native::GrassNativeStdlib::instance().find_function(ident);
                if (std_fn && std_fn->return_type) {
                    call_ret_type = std_fn->return_type;
                } else if (ident == "start_time") call_ret_type = ctx_.get_void_type();
                else if (ident == "stop_time" || ident == "discor" || ident == "lerp" ||
                         ident == "sin" || ident == "cos" || ident == "memory_usit") {
                    call_ret_type = ctx_.get_f64_type();
                } else if (ident == "conclus_time" || ident == "get_date" || ident == "get_time_str" ||
                           ident == "memory_free" || ident == "sha256" || ident == "fs_list" ||
                           ident == "fs_read") {
                    call_ret_type = ctx_.get_ptr_type(ctx_.get_i8_type());
                } else if (ident == "len") {
                    ident = "grass_array_len";
                    call_ret_type = ctx_.get_i64_type();
                }

                ir::IRFunction* target_fn = module_.get_function(target_call_name);
                if (!target_fn && target_call_name != ident) {
                    target_fn = module_.get_function(ident);
                }
                if (target_fn) {
                    call_ret_type = target_fn->return_type;
                    for (size_t i = 0; i < args.size() && i < target_fn->arguments.size(); ++i) {
                        args[i] = coerce_type(args[i], target_fn->arguments[i].type);
                    }
                } else if (std_fn) {
                    for (size_t i = 0; i < args.size() && i < std_fn->param_types.size(); ++i) {
                        args[i] = coerce_type(args[i], std_fn->param_types[i]);
                    }
                }

                if (oop_mgr_ && (ident == "v3_create" || ident == "v3_add" || ident == "v3_sub" ||
                                 ident == "v3_mul" || ident == "v3_cross" || ident == "v3_normalize" ||
                                 ident == "v3_lerp" || ident == "phys_get_pos" || ident == "phys_get_rot")) {
                    const auto* vcd = oop_mgr_->get_class("Vec3");
                    if (vcd && vcd->ir_struct_type) {
                        call_ret_type = ctx_.get_ptr_type(vcd->ir_struct_type);
                    }
                }

                if (call_ret_type && call_ret_type->is_struct()) {
                    last_struct_name_ = call_ret_type->struct_name;
                } else if (call_ret_type && call_ret_type->is_pointer() && call_ret_type->element_type && call_ret_type->element_type->is_struct()) {
                    last_struct_name_ = call_ret_type->element_type->struct_name;
                } else {
                    last_struct_name_.clear();
                }
                return builder_.create_call(target_fn ? target_fn->name : target_call_name, call_ret_type, args);
            }

            // Variable access
            if (ident == "proc_mem") {
                last_struct_name_.clear();
                return builder_.create_call("hw_ram_used", ctx_.get_i64_type(), {});
            }
            const VarBinding* var = lookup_var(ident);
            if (!var) {
                if (!current_class_context_.empty() && oop_mgr_) {
                    const VarBinding* this_var = lookup_var("this");
                    const auto* cd = oop_mgr_->get_class(current_class_context_);
                    if (this_var && cd) {
                        auto fit = cd->field_indices.find(ident);
                        if (fit != cd->field_indices.end()) {
                            ir::ValueId this_ptr = builder_.create_load(this_var->alloca_id, "this_ptr");
                            const auto& fld = cd->fields[fit->second];
                            if (fld.type && fld.type->is_struct()) {
                                last_struct_name_ = fld.type->struct_name;
                                return oop_mgr_->emit_field_gep(builder_, this_ptr, current_class_context_, ident);
                            } else if (fld.type && fld.type->is_pointer() && fld.type->element_type && fld.type->element_type->is_struct()) {
                                last_struct_name_ = fld.type->element_type->struct_name;
                                return oop_mgr_->emit_load_field(builder_, this_ptr, current_class_context_, ident);
                            }
                            last_struct_name_.clear();
                            return oop_mgr_->emit_load_field(builder_, this_ptr, current_class_context_, ident);
                        }
                    }
                }
                auto ueit = unscoped_enum_vals_.find(ident);
                if (ueit != unscoped_enum_vals_.end()) {
                    last_struct_name_.clear();
                    return builder_.create_const_i64(ueit->second);
                }
                if (ident == "BLACK")         return builder_.create_const_i64(0);
                if (ident == "BLUE")          return builder_.create_const_i64(1);
                if (ident == "GREEN")         return builder_.create_const_i64(2);
                if (ident == "CYAN")          return builder_.create_const_i64(3);
                if (ident == "RED")           return builder_.create_const_i64(4);
                if (ident == "MAGENTA")       return builder_.create_const_i64(5);
                if (ident == "BROWN")         return builder_.create_const_i64(6);
                if (ident == "LIGHT_GRAY")    return builder_.create_const_i64(7);
                if (ident == "DARK_GRAY")     return builder_.create_const_i64(8);
                if (ident == "LIGHT_BLUE")    return builder_.create_const_i64(9);
                if (ident == "LIGHT_GREEN")   return builder_.create_const_i64(10);
                if (ident == "LIGHT_CYAN")    return builder_.create_const_i64(11);
                if (ident == "LIGHT_RED")     return builder_.create_const_i64(12);
                if (ident == "LIGHT_MAGENTA") return builder_.create_const_i64(13);
                if (ident == "YELLOW")        return builder_.create_const_i64(14);
                if (ident == "WHITE")         return builder_.create_const_i64(15);
                if (ir::IRFunction* fn_ref = module_.get_function(ident)) {
                    (void)fn_ref;
                    last_struct_name_.clear();
                    return builder_.create_const_int(ctx_.get_ptr_type(ctx_.get_void_type()), 0);
                }
                error = "Undeclared identifier: " + ident;
                return 0;
            }
            ir::ValueId vaddr = var_addr(var);
            if (!var->struct_type_name.empty()) {
                last_struct_name_ = var->struct_type_name;
                if (var->type && var->type->is_pointer()) {
                    return builder_.create_load(vaddr, ident + "_ptr");
                }
                return vaddr;
            }
            last_struct_name_.clear();
            if (match(TOK_INC)) {
                ir::ValueId cur = builder_.create_load(vaddr, ident);
                ir::ValueId one = builder_.create_const_int(var->type, 1);
                ir::ValueId next = builder_.create_add(cur, one);
                builder_.create_store(next, vaddr);
                return cur; // Postfix returns old value
            }
            if (match(TOK_DEC)) {
                ir::ValueId cur = builder_.create_load(vaddr, ident);
                ir::ValueId one = builder_.create_const_int(var->type, 1);
                ir::ValueId next = builder_.create_sub(cur, one);
                builder_.create_store(next, vaddr);
                return cur; // Postfix returns old value
            }
            ir::ValueId loaded = builder_.create_load(vaddr, ident);
            if (var->is_array && var->array_elem_type) {
                expr_array_elem_types_[loaded] = var->array_elem_type;
                if (!var->array_elem_struct.empty()) {
                    expr_array_elem_structs_[loaded] = var->array_elem_struct;
                }
            }
            return loaded;
        }

        error = "Line " + std::to_string(peek().line) + ": Unexpected token in expression: " + peek().text;
        return 0;
    }

} // namespace grass::codegen
