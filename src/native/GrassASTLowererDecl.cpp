#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    bool ASTLowerer::lower_declaration(std::string& error) {
        if (peek().kind == TOK_LIB) {
            std::string lib = advance().text;
            if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
                while (peek().kind != TOK_EOF) advance();
                if (!module_.get_function("main")) {
                    ir::IRFunction* mfn = module_.create_function("main", ctx_.get_i32_type());
                    builder_.set_function(mfn);
                    builder_.set_insert_point(mfn->create_block("entry"));
                    ir::ValueId src_str = builder_.create_call(add_rodata_string(source_path_), ctx_.get_ptr_type(ctx_.get_i8_type()), {});
                    builder_.create_call("web_start", ctx_.get_void_type(), { src_str });
                    builder_.create_ret(builder_.create_const_int(ctx_.get_i32_type(), 0));
                    mfn->rebuild_cfg();
                }
            }
            return true;
        }
        if (peek().kind == TOK_SEMI) {
            advance();
            return true;
        }
        if (peek().kind == TOK_IDENT && peek().text == "@strict") {
            advance();
            return true;
        }
        if (peek().kind == TOK_USING || (peek().kind == TOK_IDENT && peek().text == "using")) {
            advance();
            bool is_ns = false;
            if (peek().kind == TOK_NAMESPACE || (peek().kind == TOK_IDENT && peek().text == "namespace")) {
                advance();
                is_ns = true;
            }
            std::string path = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) path += "::" + advance().text;
            }
            match(TOK_SEMI);
            if (is_ns) {
                using_namespaces_.push_back(path);
            } else {
                size_t last_col = path.rfind("::");
                std::string sym = (last_col != std::string::npos) ? path.substr(last_col + 2) : path;
                using_symbols_[sym] = path;
            }
            return true;
        }
        if (peek().kind == TOK_NAMESPACE || (peek().kind == TOK_IDENT && peek().text == "namespace")) {
            advance();
            if (peek().kind != TOK_IDENT) {
                error = "Expected namespace name!";
                return false;
            }
            std::string ns_name = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) ns_name += "::" + advance().text;
            }
            if (!match(TOK_LBRACE)) {
                error = "Expected '{' after namespace name!";
                return false;
            }
            namespace_stack_.push_back(ns_name);
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!lower_declaration(error)) return false;
            }
            match(TOK_RBRACE);
            match(TOK_SEMI);
            namespace_stack_.pop_back();
            return true;
        }
        while (peek().kind == TOK_IDENT &&
               (peek().text == "public" || peek().text == "private" || peek().text == "protected" ||
                peek().text == "final" || peek().text == "abstract" || peek().text == "virtual" ||
                peek().text == "override" || peek().text == "static" || peek().text == "inline" ||
                peek().text == "export")) {
            advance();
        }
        if (peek().kind == TOK_IDENT && peek().text == "enum") {
            return parse_enum_definition(error);
        }
        if (peek().kind == TOK_IDENT && (peek().text == "struct" || peek().text == "class")) {
            if (!parse_struct_definition(error)) return false;
            return true;
        }
        bool is_fn = false;
        if (is_vec_type_start()) {
            int off = 2; int depth = 1;
            while (peek(off).kind != TOK_EOF && depth > 0) {
                if (peek(off).kind == TOK_LT) depth++;
                else if (peek(off).kind == TOK_GT) depth--;
                off++;
            }
            while (peek(off).kind == TOK_STAR) off++;
            if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) is_fn = true;
        } else if (is_type_or_func_keyword(peek().text) || is_known_user_type(peek().text)) {
            size_t off = 1;
            while (peek(off).kind == TOK_STAR) off++;
            if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) is_fn = true;
        }
        if (is_fn) {
            if (!parse_function(error)) return false;
            return true;
        }
        if (is_var_keyword(peek().text) || is_type_or_func_keyword(peek().text) || is_known_user_type(peek().text)) {
            std::string tname = advance().text;
            bool is_explicit_ptr = false;
            ir::IRType* dt = (oop_mgr_ && oop_mgr_->has_class(tname))
                ? (oop_mgr_->get_class(tname)->ir_struct_type ? oop_mgr_->get_class(tname)->ir_struct_type : ctx_.get_i64_type())
                : parse_type_name(tname);
            std::string sname = (oop_mgr_ && oop_mgr_->has_class(tname)) ? tname : "";
            while (match(TOK_STAR)) {
                dt = ctx_.get_ptr_type(dt);
                is_explicit_ptr = true;
            }
            while (peek().kind == TOK_IDENT) {
                std::string vname = advance().text;
                std::string gsym = "__grass_g_" + std::to_string(global_decls_.size()) + "_" + vname;
                module_.data_globals[gsym] = module_.data_globals_size;
                module_.data_globals_size += 8;

                GlobalDeclSpec spec;
                spec.global_sym = gsym;
                spec.var_name = vname;
                spec.type_name = tname;
                spec.is_explicit_ptr = is_explicit_ptr;
                spec.decl_type = dt;
                spec.struct_name = sname;

                VarBinding gvb;
                gvb.type = dt;
                gvb.struct_type_name = sname;
                gvb.is_global = true;
                gvb.global_sym = gsym;

                if (match(TOK_ASSIGN)) {
                    spec.has_init = true;
                    spec.init_start_pos = pos_;
                    int bdepth = 0, pdepth = 0;
                    while (peek().kind != TOK_EOF) {
                        if (bdepth == 0 && pdepth == 0 && (peek().kind == TOK_SEMI || peek().kind == TOK_COMMA)) break;
                        if (peek().kind == TOK_LBRACKET) bdepth++;
                        else if (peek().kind == TOK_RBRACKET && bdepth > 0) bdepth--;
                        else if (peek().kind == TOK_LPAREN) pdepth++;
                        else if (peek().kind == TOK_RPAREN && pdepth > 0) pdepth--;
                        advance();
                    }
                    spec.init_end_pos = pos_;
                    if ((tname == "take" || tname == "var" || tname == "set") && spec.init_start_pos < spec.init_end_pos) {
                        const auto& first = tokens_[spec.init_start_pos];
                        if (first.kind == TOK_LBRACKET) {
                            gvb.type = ctx_.get_ptr_type();
                            gvb.is_array = true;
                            gvb.array_elem_type = predict_array_elem_from_tokens(tokens_, spec.init_start_pos, spec.init_end_pos);
                        } else if (first.kind == TOK_STRING) {
                            gvb.type = ctx_.get_ptr_type(ctx_.get_i8_type());
                        } else if (first.kind == TOK_NUMBER) {
                            bool is_fp = (first.text.find('.') != std::string::npos ||
                                          first.text.find('e') != std::string::npos ||
                                          first.text.find('E') != std::string::npos);
                            gvb.type = is_fp ? ctx_.get_f64_type() : ctx_.get_i64_type();
                        } else if (first.kind == TOK_IDENT && (first.text == "true" || first.text == "false")) {
                            gvb.type = ctx_.get_i1_type();
                        }
                    }
                }
                global_vars_[vname] = gvb;
                global_decls_.push_back(std::move(spec));
                if (!match(TOK_COMMA)) break;
            }
            match(TOK_SEMI);
            return true;
        }
        advance();
        return true;
    }

    bool ASTLowerer::lower(std::string& error) {
        if (oop_mgr_ && !oop_mgr_->has_class("Vec3")) {
            oop_mgr_->begin_struct("Vec3");
            oop_mgr_->add_field("x", ctx_.get_f64_type());
            oop_mgr_->add_field("y", ctx_.get_f64_type());
            oop_mgr_->add_field("z", ctx_.get_f64_type());
            oop_mgr_->finalize_class();
        }
        while (peek().kind != TOK_EOF) {
            if (!lower_declaration(error)) return false;
        }
        return true;
    }

    bool ASTLowerer::is_type_or_func_keyword(const std::string& s) {
        return s == "int" || s == "take" || s == "void" || s == "fn" || s == "fun" ||
               s == "double" || s == "float" || s == "string" || s == "bool" ||
               s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
               s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
               s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
               s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
               s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
               s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
               s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
    }

    bool ASTLowerer::is_var_keyword(const std::string& s) {
        return s == "take" || s == "int" || s == "double" || s == "float" ||
               s == "string" || s == "bool" || s == "set" || s == "array" ||
               s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
               s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
               s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
               s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
               s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
               s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
               s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
    }

    bool ASTLowerer::is_known_user_type(const std::string& name) const {
        if (enum_defs_.count(name) > 0) return true;
        if (oop_mgr_ && oop_mgr_->has_class(name)) return true;
        return false;
    }

    ir::IRType* ASTLowerer::parse_type_name(const std::string& name) {
        if (name == "void") return ctx_.get_void_type();
        if (name == "bool") return ctx_.get_i1_type();
        if (name == "i8" || name == "u8" || name == "int8" || name == "uint8" || name == "char" || name == "byte") return ctx_.get_i8_type();
        if (name == "i16" || name == "u16" || name == "int16" || name == "uint16") return ctx_.get_i16_type();
        if (name == "i32" || name == "u32" || name == "int32" || name == "uint32" || name == "int") return ctx_.get_i32_type();
        if (name == "i64" || name == "u64" || name == "int64" || name == "uint64" || name == "take" || name == "size_t") return ctx_.get_i64_type();
        if (name == "float" || name == "double" || name == "f32" || name == "f64") return ctx_.get_f64_type();
        if (name == "string" || name == "String") return ctx_.get_ptr_type(ctx_.get_i8_type());
        if (name == "array") return ctx_.get_ptr_type(ctx_.get_void_type());
        if (name == "nullptr" || name == "null") return ctx_.get_ptr_type(ctx_.get_void_type());
        if (enum_defs_.count(name)) return ctx_.get_i64_type();
        if (oop_mgr_ && oop_mgr_->has_class(name)) {
            const auto* cd = oop_mgr_->get_class(name);
            return cd ? cd->ir_struct_type : ctx_.get_i64_type();
        }
        return ctx_.get_i64_type();
    }

    bool ASTLowerer::parse_enum_definition(std::string& error) {
        advance(); // 'enum'
        bool is_scoped = match_ident("class") || match_ident("struct");
        if (peek().kind != TOK_IDENT) {
            error = "Line " + std::to_string(peek().line) + ": Expected enum name!";
            return false;
        }
        std::string ename = advance().text;
        if (!consume(TOK_LBRACE, "Expected '{' in enum definition!", error)) return false;
        std::unordered_map<std::string, int64_t> entries;
        int64_t next_val = 0;
        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (match(TOK_SEMI) || match(TOK_COMMA)) continue;
            if (peek().kind != TOK_IDENT) {
                advance();
                continue;
            }
            std::string entry = advance().text;
            int64_t val = next_val;
            if (match(TOK_ASSIGN)) {
                bool neg = match(TOK_MINUS);
                if (peek().kind == TOK_NUMBER) {
                    try { val = std::stoll(advance().text, nullptr, 0); } catch (...) {}
                    if (neg) val = -val;
                }
            }
            entries[entry] = val;
            if (!is_scoped) unscoped_enum_vals_[entry] = val;
            next_val = val + 1;
            match(TOK_COMMA);
            match(TOK_SEMI);
        }
        consume(TOK_RBRACE, "Expected '}' closing enum definition!", error);
        match(TOK_SEMI);
        enum_defs_[ename] = std::move(entries);
        return true;
    }

    void ASTLowerer::emit_ctor_call(ir::ValueId obj_ptr, const std::string& class_name, const std::vector<ir::ValueId>& args) {
        if (!oop_mgr_ || obj_ptr == ir::kInvalidValueId) return;
        const auto* cd = oop_mgr_->get_class(class_name);
        if (!cd) return;
        std::string ctor_name = "ctor_" + std::to_string(args.size());
        if (cd->find_method(ctor_name)) {
            oop_mgr_->emit_call_method(builder_, obj_ptr, class_name, ctor_name, args);
        } else if (args.empty() && cd->find_method("ctor")) {
            oop_mgr_->emit_call_method(builder_, obj_ptr, class_name, "ctor", {});
        }
    }

    struct PendingCtorInit {
        std::string target_name;
        std::vector<std::pair<size_t, size_t>> arg_spans;
    };

    struct PendingMethod {
        std::string name;
        ir::IRType* ret_type = nullptr;
        std::vector<ir::IRType*> param_types;
        std::vector<std::string> param_names;
        std::vector<std::string> param_struct_names;
        std::vector<bool> param_is_vec;
        std::vector<ir::IRType*> param_vec_elem_types;
        std::vector<std::string> param_vec_elem_structs;
        size_t body_start_pos = 0;
        size_t body_end_pos = 0;
        bool is_dtor = false;
        bool is_ctor = false;
        std::vector<PendingCtorInit> ctor_inits;
    };

    bool ASTLowerer::parse_struct_definition(std::string& error) {
        bool is_class = (advance().text == "class"); // consume 'struct' or 'class'
        if (peek().kind != TOK_IDENT) {
            error = "Line " + std::to_string(peek().line) + ": Expected " + (is_class ? "class" : "struct") + " name!";
            return false;
        }
        std::string struct_name = advance().text;
        std::string base_class = "";
        if (match(TOK_COLON)) {
            if (peek().kind == TOK_IDENT &&
                (peek().text == "public" || peek().text == "private" || peek().text == "protected")) {
                advance();
            }
            if (peek().kind == TOK_IDENT) {
                base_class = advance().text;
            }
            while (match(TOK_COMMA)) {
                if (peek().kind == TOK_IDENT &&
                    (peek().text == "public" || peek().text == "private" || peek().text == "protected")) {
                    advance();
                }
                if (peek().kind == TOK_IDENT) advance();
            }
        } else if (match_ident("extends")) {
            if (peek().kind == TOK_IDENT) {
                base_class = advance().text;
            }
        }
        if (match_ident("implements")) {
            if (peek().kind == TOK_IDENT) advance();
            while (match(TOK_COMMA)) {
                if (peek().kind == TOK_IDENT) advance();
            }
        }
        if (!base_class.empty()) {
            class_base_names_[struct_name] = base_class;
        }
        if (!consume(TOK_LBRACE, "Expected '{' after name!", error)) return false;

        if (oop_mgr_) {
            if (is_class) {
                oop_mgr_->begin_class(struct_name, base_class);
            } else {
                oop_mgr_->begin_struct(struct_name);
            }
        }

        std::vector<ir::IRType*> field_types;
        std::vector<std::string> field_names;
        std::vector<PendingMethod> pending_methods;
        std::vector<FieldDefaultInit> field_defaults;
        bool has_ctor_0 = false;

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (match(TOK_SEMI)) continue;

            // Access section labels: public:, private:, protected:
            if (peek().kind == TOK_IDENT &&
                (peek().text == "public" || peek().text == "private" || peek().text == "protected") &&
                peek(1).kind == TOK_COLON) {
                advance();
                advance();
                continue;
            }

            // Inline member modifiers: public, private, protected, final, virtual, override, static, inline
            bool is_virtual_mod = false;
            bool is_override_mod = false;
            while (peek().kind == TOK_IDENT &&
                   (peek().text == "public" || peek().text == "private" || peek().text == "protected" ||
                    peek().text == "final" || peek().text == "virtual" || peek().text == "override" ||
                    peek().text == "static" || peek().text == "inline")) {
                std::string mod = advance().text;
                if (mod == "virtual") is_virtual_mod = true;
                if (mod == "override") { is_override_mod = true; is_virtual_mod = true; }
            }

            // Destructor: ~StructName() { ... }
            if (match(TOK_TILDE)) {
                if (peek().kind != TOK_IDENT) {
                    error = "Line " + std::to_string(peek().line) + ": Expected destructor name after '~'!";
                    return false;
                }
                std::string dname = advance().text;
                if (dname != struct_name) {
                    error = "Line " + std::to_string(peek().line) + ": Destructor name '~" + dname + "' does not match struct '" + struct_name + "'!";
                    return false;
                }
                if (!consume(TOK_LPAREN, "Expected '(' in destructor declaration!", error)) return false;
                if (!consume(TOK_RPAREN, "Destructor cannot take parameters!", error)) return false;
                if (!consume(TOK_LBRACE, "Expected '{' at start of destructor body!", error)) return false;

                size_t body_start = pos_;
                int brace_depth = 1;
                while (brace_depth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LBRACE) brace_depth++;
                    else if (peek().kind == TOK_RBRACE) {
                        brace_depth--;
                        if (brace_depth == 0) break;
                    }
                    advance();
                }
                size_t body_end = pos_;
                consume(TOK_RBRACE, "Expected '}' closing destructor body!", error);

                PendingMethod dm;
                dm.name = "dtor";
                dm.ret_type = ctx_.get_void_type();
                dm.body_start_pos = body_start;
                dm.body_end_pos = body_end;
                dm.is_dtor = true;
                pending_methods.push_back(std::move(dm));
                if (oop_mgr_) {
                    oop_mgr_->add_destructor();
                }
                continue;
            }

            // Constructor: ClassName(params...) [: member(val), ...] { ... }
            if (peek().kind == TOK_IDENT && peek().text == struct_name && peek(1).kind == TOK_LPAREN) {
                advance(); // ClassName
                advance(); // '('
                std::vector<ir::IRType*> param_types;
                std::vector<std::string> param_names;
                std::vector<std::string> param_struct_names;
                std::vector<bool> param_is_vec;
                std::vector<ir::IRType*> param_vec_elem_types;
                std::vector<std::string> param_vec_elem_structs;
                if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                    while (true) {
                        ir::IRType* ptype = ctx_.get_i64_type();
                        std::string p_struct_name = "";
                        bool p_is_vec = false;
                        ir::IRType* p_velem = nullptr;
                        std::string p_vestruct;
                        if (is_vec_type_start()) {
                            std::string vsn;
                            ptype = parse_vec_ir_type(p_velem, p_vestruct, vsn);
                            p_struct_name = vsn;
                            p_is_vec = true;
                            while (match(TOK_STAR)) { ptype = ctx_.get_ptr_type(ptype); }
                        } else if (peek().kind == TOK_IDENT &&
                                   (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR || is_known_user_type(peek().text))) {
                            std::string tname = advance().text;
                            if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                                p_struct_name = tname;
                                ptype = ctx_.get_ptr_type(parse_type_name(tname));
                                (void)match(TOK_STAR);
                            } else {
                                ptype = parse_type_name(tname);
                            }
                            while (match(TOK_STAR)) {
                                ptype = ctx_.get_ptr_type(ptype);
                            }
                        } else if (is_type_or_func_keyword(peek().text)) {
                            ptype = parse_type_name(advance().text);
                            while (match(TOK_STAR)) {
                                ptype = ctx_.get_ptr_type(ptype);
                            }
                        }
                        if (peek().kind != TOK_IDENT) {
                            error = "Line " + std::to_string(peek().line) + ": Expected constructor parameter name!";
                            return false;
                        }
                        std::string pname = advance().text;
                        param_types.push_back(ptype);
                        param_names.push_back(pname);
                        param_struct_names.push_back(p_struct_name);
                        param_is_vec.push_back(p_is_vec);
                        param_vec_elem_types.push_back(p_velem);
                        param_vec_elem_structs.push_back(p_vestruct);
                        if (!match(TOK_COMMA)) break;
                    }
                }
                if (!consume(TOK_RPAREN, "Expected ')' closing constructor parameters!", error)) return false;

                std::vector<PendingCtorInit> ctor_inits;
                if (match(TOK_COLON)) {
                    while (peek().kind == TOK_IDENT) {
                        PendingCtorInit ci;
                        ci.target_name = advance().text;
                        if (match(TOK_LPAREN)) {
                            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                                size_t astart = pos_;
                                int pdepth = 0, bdepth = 0;
                                while (peek().kind != TOK_EOF) {
                                    if (peek().kind == TOK_LPAREN || peek().kind == TOK_LBRACKET || peek().kind == TOK_LBRACE) pdepth++;
                                    else if (peek().kind == TOK_RPAREN || peek().kind == TOK_RBRACKET || peek().kind == TOK_RBRACE) {
                                        if (pdepth == 0) break;
                                        pdepth--;
                                    } else if (peek().kind == TOK_COMMA && pdepth == 0 && bdepth == 0) {
                                        break;
                                    }
                                    advance();
                                }
                                size_t aend = pos_;
                                if (aend > astart) ci.arg_spans.push_back({astart, aend});
                                if (!match(TOK_COMMA)) break;
                            }
                            consume(TOK_RPAREN, "Expected ')' in member initializer!", error);
                        }
                        ctor_inits.push_back(std::move(ci));
                        if (!match(TOK_COMMA)) break;
                    }
                }

                if (!consume(TOK_LBRACE, "Expected '{' at start of constructor body!", error)) return false;
                size_t body_start = pos_;
                int brace_depth = 1;
                while (brace_depth > 0 && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_LBRACE) brace_depth++;
                    else if (peek().kind == TOK_RBRACE) {
                        brace_depth--;
                        if (brace_depth == 0) break;
                    }
                    advance();
                }
                size_t body_end = pos_;
                consume(TOK_RBRACE, "Expected '}' closing constructor body!", error);

                std::string ctor_name = "ctor_" + std::to_string(param_types.size());
                if (param_types.empty()) has_ctor_0 = true;
                PendingMethod cm;
                cm.name = ctor_name;
                cm.ret_type = ctx_.get_void_type();
                cm.param_types = param_types;
                cm.param_names = param_names;
                cm.param_struct_names = param_struct_names;
                cm.param_is_vec = param_is_vec;
                cm.param_vec_elem_types = param_vec_elem_types;
                cm.param_vec_elem_structs = param_vec_elem_structs;
                cm.body_start_pos = body_start;
                cm.body_end_pos = body_end;
                cm.is_ctor = true;
                cm.ctor_inits = std::move(ctor_inits);
                pending_methods.push_back(std::move(cm));
                if (oop_mgr_) {
                    oop_mgr_->add_method(ctor_name, ctx_.get_void_type(), param_types, param_names, false, false);
                }
                continue;
            }

            if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_COLON || peek(1).kind == TOK_ASSIGN)) {
                std::string mname = advance().text;
                advance(); // ':' or '='
                ir::IRType* ftype = ctx_.get_i64_type();
                if (is_vec_type_start()) {
                    ir::IRType* ve = nullptr;
                    std::string ves, vsn;
                    ftype = parse_vec_ir_type(ve, ves, vsn);
                    if (!match(TOK_STAR) && oop_mgr_) {
                        oop_mgr_->add_destructor();
                    } else {
                        ftype = ctx_.get_ptr_type(ftype);
                    }
                } else if (peek().kind == TOK_IDENT) {
                    ftype = parse_type_name(advance().text);
                    while (match(TOK_STAR)) ftype = ctx_.get_ptr_type(ftype);
                }
                field_types.push_back(ftype);
                field_names.push_back(mname);
                if (oop_mgr_) oop_mgr_->add_field(mname, ftype);
                match(TOK_COMMA);
                match(TOK_SEMI);
                continue;
            }
            bool is_struct_field_or_ret = is_known_user_type(peek().text);
            bool is_vec_field_or_ret = is_vec_type_start();
            if (peek().kind == TOK_IDENT && (is_var_keyword(peek().text) || is_type_or_func_keyword(peek().text) || is_struct_field_or_ret || is_vec_field_or_ret)) {
                ir::IRType* ftype = nullptr;
                bool field_is_ptr = false;
                if (is_vec_field_or_ret) {
                    ir::IRType* ve = nullptr;
                    std::string ves, vsn;
                    ftype = parse_vec_ir_type(ve, ves, vsn);
                    if (!match(TOK_STAR) && oop_mgr_) {
                        oop_mgr_->add_destructor();
                    } else {
                        ftype = ctx_.get_ptr_type(ftype);
                        field_is_ptr = true;
                    }
                } else {
                    std::string ftype_str = advance().text;
                    ftype = (ftype_str == "fn") ? ctx_.get_void_type() : parse_type_name(ftype_str);
                }
                while (match(TOK_STAR)) {
                    ftype = ctx_.get_ptr_type(ftype);
                    field_is_ptr = true;
                }
                (void)field_is_ptr;
                if (peek().kind != TOK_IDENT) {
                    error = "Line " + std::to_string(peek().line) + ": Expected field or method name!";
                    return false;
                }
                std::string mname = advance().text;

                if (peek().kind == TOK_LPAREN) {
                    advance(); // '('
                    std::vector<ir::IRType*> param_types;
                    std::vector<std::string> param_names;
                    std::vector<std::string> param_struct_names;
                    std::vector<bool> param_is_vec;
                    std::vector<ir::IRType*> param_vec_elem_types;
                    std::vector<std::string> param_vec_elem_structs;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        while (true) {
                            ir::IRType* ptype = ctx_.get_i64_type();
                            std::string p_struct_name = "";
                            bool p_is_vec = false;
                            ir::IRType* p_velem = nullptr;
                            std::string p_vestruct;
                            if (is_vec_type_start()) {
                                std::string vsn;
                                ptype = parse_vec_ir_type(p_velem, p_vestruct, vsn);
                                p_struct_name = vsn;
                                p_is_vec = true;
                                while (match(TOK_STAR)) ptype = ctx_.get_ptr_type(ptype);
                            } else if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR || is_known_user_type(peek().text))) {
                                std::string tname = advance().text;
                                if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                                    p_struct_name = tname;
                                    ptype = ctx_.get_ptr_type(parse_type_name(tname));
                                    (void)match(TOK_STAR);
                                } else {
                                    ptype = parse_type_name(tname);
                                }
                                while (match(TOK_STAR)) {
                                    ptype = ctx_.get_ptr_type(ptype);
                                }
                            } else if (is_type_or_func_keyword(peek().text)) {
                                ptype = parse_type_name(advance().text);
                                while (match(TOK_STAR)) {
                                    ptype = ctx_.get_ptr_type(ptype);
                                }
                            }
                            if (peek().kind != TOK_IDENT) {
                                error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                                return false;
                            }
                            std::string pname = advance().text;
                            param_types.push_back(ptype);
                            param_names.push_back(pname);
                            param_struct_names.push_back(p_struct_name);
                            param_is_vec.push_back(p_is_vec);
                            param_vec_elem_types.push_back(p_velem);
                            param_vec_elem_structs.push_back(p_vestruct);
                            if (!match(TOK_COMMA)) break;
                        }
                    }
                    if (!consume(TOK_RPAREN, "Expected ')' closing method parameters!", error)) return false;
                    while (peek().kind == TOK_IDENT && (peek().text == "const" || peek().text == "override" || peek().text == "final")) {
                        if (peek().text == "override") { is_override_mod = true; is_virtual_mod = true; }
                        advance();
                    }
                    if (!consume(TOK_LBRACE, "Expected '{' at start of method body!", error)) return false;

                    size_t body_start = pos_;
                    int brace_depth = 1;
                    while (brace_depth > 0 && peek().kind != TOK_EOF) {
                        if (peek().kind == TOK_LBRACE) brace_depth++;
                        else if (peek().kind == TOK_RBRACE) {
                            brace_depth--;
                            if (brace_depth == 0) break;
                        }
                        advance();
                    }
                    size_t body_end = pos_;
                    consume(TOK_RBRACE, "Expected '}' closing method body!", error);

                    PendingMethod pm;
                    pm.name = mname;
                    pm.ret_type = ftype;
                    pm.param_types = param_types;
                    pm.param_names = param_names;
                    pm.param_struct_names = param_struct_names;
                    pm.param_is_vec = param_is_vec;
                    pm.param_vec_elem_types = param_vec_elem_types;
                    pm.param_vec_elem_structs = param_vec_elem_structs;
                    pm.body_start_pos = body_start;
                    pm.body_end_pos = body_end;
                    pending_methods.push_back(std::move(pm));
                    if (oop_mgr_) {
                        oop_mgr_->add_method(mname, ftype, param_types, param_names, is_virtual_mod, is_override_mod);
                    }
                } else {
                    if (match(TOK_ASSIGN)) {
                        size_t dstart = pos_;
                        int depth = 0;
                        while (peek().kind != TOK_EOF) {
                            if (peek().kind == TOK_LPAREN || peek().kind == TOK_LBRACKET || peek().kind == TOK_LBRACE) depth++;
                            else if (peek().kind == TOK_RPAREN || peek().kind == TOK_RBRACKET || peek().kind == TOK_RBRACE) {
                                if (depth == 0) break;
                                depth--;
                            } else if (peek().kind == TOK_SEMI && depth == 0) {
                                break;
                            }
                            advance();
                        }
                        size_t dend = pos_;
                        if (dend > dstart) field_defaults.push_back({ mname, dstart, dend });
                    }
                    match(TOK_SEMI);
                    if (!ftype->is_struct() && !ftype->is_pointer() &&
                        (mname == "name" || mname == "title" || mname == "msg" || mname == "text" || mname == "path" || mname == "str")) {
                        ftype = ctx_.get_ptr_type(ctx_.get_i8_type());
                    }
                    field_types.push_back(ftype);
                    field_names.push_back(mname);
                    if (oop_mgr_) {
                        oop_mgr_->add_field(mname, ftype);
                    }
                }
            } else {
                advance();
            }
        }
        consume(TOK_RBRACE, "Expected '}' closing struct definition!", error);
        match(TOK_SEMI);

        class_field_defaults_[struct_name] = field_defaults;
        bool has_base_ctor_0 = false;
        if (!base_class.empty() && oop_mgr_) {
            const auto* bcd = oop_mgr_->get_class(base_class);
            if (bcd && (bcd->find_method("ctor_0") || bcd->find_method("ctor"))) has_base_ctor_0 = true;
        }
        if (oop_mgr_ && !has_ctor_0 && (!field_defaults.empty() || has_base_ctor_0)) {
            PendingMethod sc;
            sc.name = "ctor_0";
            sc.ret_type = ctx_.get_void_type();
            sc.is_ctor = true;
            pending_methods.push_back(std::move(sc));
            oop_mgr_->add_method("ctor_0", ctx_.get_void_type(), {}, {}, false, false);
        }

        ctx_.get_struct_type(struct_name, field_types, field_names);
        if (oop_mgr_) {
            oop_mgr_->finalize_class();
        }

        std::vector<ir::IRFunction*> method_fns;
        method_fns.reserve(pending_methods.size());
        for (const auto& pm : pending_methods) {
            ir::IRFunction* fn = nullptr;
            if (oop_mgr_) {
                fn = oop_mgr_->create_method_function(struct_name, pm.name);
            }
            if (!fn) {
                std::string mangled = pm.is_dtor ? (struct_name + "__dtor") : (struct_name + "_" + pm.name);
                fn = module_.create_function(mangled, pm.ret_type);
                ir::IRType* this_t = ctx_.get_ptr_type(ctx_.get_struct_type(struct_name, field_types, field_names));
                fn->add_argument("this", this_t);
                for (size_t i = 0; i < pm.param_types.size(); ++i) {
                    fn->add_argument(pm.param_names[i], pm.param_types[i]);
                }
            }
            method_fns.push_back(fn);
        }

        bool generated_explicit_dtor = false;
        for (size_t mi = 0; mi < pending_methods.size(); ++mi) {
            const auto& pm = pending_methods[mi];
            if (pm.is_dtor) generated_explicit_dtor = true;
            ir::IRFunction* fn = method_fns[mi];

            ir::IRFunction* prev_fn = builder_.get_function();
            std::string prev_dtor_struct = current_dtor_struct_;
            std::string prev_class_ctx = current_class_context_;
            builder_.set_function(fn);
            if (pm.is_dtor) current_dtor_struct_ = struct_name;
            current_class_context_ = struct_name;
            scopes_.clear();
            expr_array_elem_types_.clear();
            expr_array_elem_structs_.clear();
            push_scope();

            ir::BasicBlock* entry_bb = builder_.create_block("entry");
            builder_.set_insert_point(entry_bb);

            ir::ValueId this_alloca = builder_.create_alloca(fn->arguments[0].type, 1, 0, "this");
            builder_.create_store(fn->arguments[0].id, this_alloca);
            declare_var("this", this_alloca, fn->arguments[0].type, struct_name, false);

            for (size_t i = 1; i < fn->arguments.size(); ++i) {
                ir::ValueId p_alloca = builder_.create_alloca(fn->arguments[i].type, 1, 0, fn->arguments[i].name);
                builder_.create_store(fn->arguments[i].id, p_alloca);
                size_t pi = i - 1;
                std::string sname = (pi < pm.param_struct_names.size()) ? pm.param_struct_names[pi] : "";
                bool pvec = (pi < pm.param_is_vec.size()) ? pm.param_is_vec[pi] : false;
                ir::IRType* pve = (pi < pm.param_vec_elem_types.size()) ? pm.param_vec_elem_types[pi] : nullptr;
                std::string pves = (pi < pm.param_vec_elem_structs.size()) ? pm.param_vec_elem_structs[pi] : "";
                declare_var(fn->arguments[i].name, p_alloca, fn->arguments[i].type, sname, false, pvec, pve, pves);
            }

            size_t saved_pos = pos_;
            if (pm.is_ctor && oop_mgr_) {
                ir::ValueId this_val = builder_.create_load(this_alloca, "this_ctor");
                bool explicit_base_ctor = false;
                for (const auto& ci : pm.ctor_inits) {
                    if (!base_class.empty() && ci.target_name == base_class) {
                        explicit_base_ctor = true;
                        std::vector<ir::ValueId> bargs;
                        for (const auto& span : ci.arg_spans) {
                            pos_ = span.first;
                            ir::ValueId av = parse_expression(error);
                            if (av == ir::kInvalidValueId) { pos_ = saved_pos; current_dtor_struct_ = prev_dtor_struct; current_class_context_ = prev_class_ctx; return false; }
                            bargs.push_back(av);
                        }
                        emit_ctor_call(this_val, base_class, bargs);
                        break;
                    }
                }
                if (!explicit_base_ctor && !base_class.empty()) {
                    emit_ctor_call(this_val, base_class, {});
                }
                for (const auto& fdef : field_defaults) {
                    pos_ = fdef.expr_start;
                    ir::ValueId dv = parse_expression(error);
                    if (dv == ir::kInvalidValueId) { pos_ = saved_pos; current_dtor_struct_ = prev_dtor_struct; current_class_context_ = prev_class_ctx; return false; }
                    const auto* cd = oop_mgr_->get_class(struct_name);
                    if (cd) {
                        auto fit = cd->field_indices.find(fdef.field_name);
                        if (fit != cd->field_indices.end()) {
                            dv = coerce_type(dv, cd->fields[fit->second].type);
                            oop_mgr_->emit_store_field(builder_, this_val, struct_name, fdef.field_name, dv);
                        }
                    }
                }
                for (const auto& ci : pm.ctor_inits) {
                    if (!base_class.empty() && ci.target_name == base_class) continue;
                    const auto* cd = oop_mgr_->get_class(struct_name);
                    if (!cd) continue;
                    auto fit = cd->field_indices.find(ci.target_name);
                    if (fit == cd->field_indices.end()) continue;
                    const auto& fld = cd->fields[fit->second];
                    std::vector<ir::ValueId> fargs;
                    for (const auto& span : ci.arg_spans) {
                        pos_ = span.first;
                        ir::ValueId av = parse_expression(error);
                        if (av == ir::kInvalidValueId) { pos_ = saved_pos; current_dtor_struct_ = prev_dtor_struct; current_class_context_ = prev_class_ctx; return false; }
                        fargs.push_back(av);
                    }
                    if (fld.type && fld.type->is_struct() && oop_mgr_->has_class(fld.type->struct_name) && !vec_struct_meta_.count(fld.type->struct_name)) {
                        ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, this_val, struct_name, ci.target_name);
                        emit_ctor_call(fptr, fld.type->struct_name, fargs);
                    } else if (!fargs.empty()) {
                        ir::ValueId fv = coerce_type(fargs[0], fld.type);
                        oop_mgr_->emit_store_field(builder_, this_val, struct_name, ci.target_name, fv);
                    }
                }
            }

            if (pm.body_end_pos > pm.body_start_pos) {
                pos_ = pm.body_start_pos;
                while (pos_ < pm.body_end_pos && peek().kind != TOK_EOF) {
                    if (!parse_statement(error)) {
                        pos_ = saved_pos;
                        current_dtor_struct_ = prev_dtor_struct;
                        current_class_context_ = prev_class_ctx;
                        return false;
                    }
                }
            }
            pos_ = saved_pos;

            ir::BasicBlock* cur_bb = builder_.get_insert_block();
            if (cur_bb && !cur_bb->has_terminator()) {
                pop_scope(true);
                if (pm.is_dtor) {
                    ir::ValueId this_val = builder_.create_load(this_alloca, "this_dtor");
                    emit_struct_field_dtors(this_val, struct_name);
                }
                cur_bb = builder_.get_insert_block();
                if (cur_bb && !cur_bb->has_terminator()) {
                    if (pm.ret_type->is_void()) {
                        builder_.create_ret_void();
                    } else {
                        ir::ValueId def_ret = builder_.create_const_int(pm.ret_type, 0);
                        builder_.create_ret(def_ret);
                    }
                }
            } else {
                pop_scope(false);
            }

            fn->rebuild_cfg();
            current_dtor_struct_ = prev_dtor_struct;
            current_class_context_ = prev_class_ctx;
            builder_.set_function(prev_fn);
        }

        if (!generated_explicit_dtor && oop_mgr_) {
            const auto* cd = oop_mgr_->get_class(struct_name);
            if (cd && cd->has_destructor) {
                ir::IRFunction* fn = oop_mgr_->create_method_function(struct_name, "dtor");
                if (fn) {
                    ir::IRFunction* prev_fn = builder_.get_function();
                    builder_.set_function(fn);
                    ir::BasicBlock* entry_bb = builder_.create_block("entry");
                    builder_.set_insert_point(entry_bb);
                    emit_struct_field_dtors(fn->arguments[0].id, struct_name);
                    ir::BasicBlock* cur_bb = builder_.get_insert_block();
                    if (cur_bb && !cur_bb->has_terminator()) {
                        builder_.create_ret_void();
                    }
                    fn->rebuild_cfg();
                    builder_.set_function(prev_fn);
                }
            }
        }

        return true;
    }

    bool ASTLowerer::parse_function(std::string& error) {
        ir::IRType* ret_type = nullptr;
        if (is_vec_type_start()) {
            ir::IRType* ve = nullptr;
            std::string ves, vsn;
            ret_type = parse_vec_ir_type(ve, ves, vsn);
        } else {
            std::string ret_type_name = advance().text;
            if (oop_mgr_ && oop_mgr_->has_class(ret_type_name)) {
                const auto* cd = oop_mgr_->get_class(ret_type_name);
                ret_type = (cd && cd->ir_struct_type) ? cd->ir_struct_type : ctx_.get_i64_type();
            } else {
                ret_type = parse_type_name(ret_type_name);
            }
        }
        while (match(TOK_STAR)) {
            ret_type = ctx_.get_ptr_type(ret_type);
        }

        std::string raw_fn_name = advance().text;
        std::string qualified_name = qualify_name(raw_fn_name);
        std::string mangled_name = grass::sema::GrassSema::mangleName(qualified_name);
        ir::IRFunction* fn = module_.create_function(mangled_name, ret_type);

        builder_.set_function(fn);
        scopes_.clear();
        expr_array_elem_types_.clear();
        expr_array_elem_structs_.clear();
        push_scope();

        if (!consume(TOK_LPAREN, "Expected '(' in function signature!", error)) return false;

        struct ParamInfo {
            std::string name;
            ir::IRType* type;
            std::string struct_name;
            bool is_vec = false;
            ir::IRType* vec_elem_t = nullptr;
            std::string vec_elem_struct;
        };
        std::vector<ParamInfo> params;

        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            while (true) {
                ir::IRType* ptype = ctx_.get_i64_type();
                std::string struct_name = "";
                bool p_is_vec = false;
                ir::IRType* p_velem = nullptr;
                std::string p_vestruct;
                if (is_vec_type_start()) {
                    std::string vsn;
                    ir::IRType* vst = parse_vec_ir_type(p_velem, p_vestruct, vsn);
                    ptype = vst;
                    struct_name = vsn;
                    p_is_vec = true;
                } else if (peek().kind == TOK_IDENT &&
                    (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR || is_known_user_type(peek().text))) {
                    std::string tname = advance().text;
                    if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                        struct_name = tname;
                        const auto* cd = oop_mgr_->get_class(tname);
                        ptype = (cd && cd->ir_struct_type) ? ctx_.get_ptr_type(cd->ir_struct_type) : ctx_.get_ptr_type();
                        (void)match(TOK_STAR); // Guard and Guard* both pass ptr(Guard) in native ABI
                    } else {
                        ptype = parse_type_name(tname);
                    }
                } else if (is_type_or_func_keyword(peek().text)) {
                    ptype = parse_type_name(advance().text);
                }
                while (match(TOK_STAR)) {
                    ptype = ctx_.get_ptr_type(ptype);
                }
                if (peek().kind != TOK_IDENT) {
                    error = "Line " + std::to_string(peek().line) + ": Expected parameter name!";
                    return false;
                }
                std::string pname = advance().text;
                params.push_back({ pname, ptype, struct_name, p_is_vec, p_velem, p_vestruct });
                fn->add_argument(pname, ptype);
                if (!match(TOK_COMMA)) break;
            }
        }
        if (!consume(TOK_RPAREN, "Expected ')' closing parameter list!", error)) return false;
        if (!consume(TOK_LBRACE, "Expected '{' at start of function body!", error)) return false;

        ir::BasicBlock* entry_bb = builder_.create_block("entry");
        builder_.set_insert_point(entry_bb);

        // Store arguments in stack allocas for SSA promotion
        for (size_t i = 0; i < params.size(); ++i) {
            ir::ValueId alloca_id = builder_.create_alloca(params[i].type, 1, 0, params[i].name);
            builder_.create_store(fn->arguments[i].id, alloca_id);
            declare_var(params[i].name, alloca_id, params[i].type, params[i].struct_name, false,
                        params[i].is_vec, params[i].vec_elem_t, params[i].vec_elem_struct);
        }

        if (mangled_name == "main" && !globals_initialized_ && !global_decls_.empty()) {
            globals_initialized_ = true;
            size_t saved_pos = pos_;
            for (const auto& gd : global_decls_) {
                VarBinding* gvb = lookup_var_mut(gd.var_name);
                if (!gvb) continue;
                if (gd.has_init && gd.init_start_pos < gd.init_end_pos) {
                    pos_ = gd.init_start_pos;
                    last_struct_name_.clear();
                    ir::ValueId init_val = parse_expression(error);
                    if (!init_val) {
                        pos_ = saved_pos;
                        return false;
                    }
                    if (gd.type_name == "take" || gd.type_name == "var" || gd.type_name == "set") {
                        ir::IRType* actual_t = builder_.get_function()->get_value(init_val).type;
                        if (actual_t && !actual_t->is_void()) gvb->type = actual_t;
                        auto arr_it = expr_array_elem_types_.find(init_val);
                        if (arr_it != expr_array_elem_types_.end()) {
                            gvb->is_array = true;
                            gvb->array_elem_type = arr_it->second;
                            if (expr_array_elem_structs_.count(init_val)) {
                                gvb->array_elem_struct = expr_array_elem_structs_[init_val];
                            }
                        }
                    } else {
                        init_val = coerce_type(init_val, gvb->type);
                    }
                    ir::ValueId gaddr = var_addr(gvb);
                    builder_.create_store(init_val, gaddr);
                }
            }
            pos_ = saved_pos;
        }

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (!parse_statement(error)) {
                if (error.empty()) error = "Failed to parse statement in function '" + mangled_name + "' near token '" + peek().text + "' (line " + std::to_string(peek().line) + ")";
                return false;
            }
        }
        if (!consume(TOK_RBRACE, "Expected '}' closing function body!", error)) return false;

        // Ensure block ends with terminator
        ir::BasicBlock* current_bb = builder_.get_insert_block();
        if (current_bb && !current_bb->has_terminator()) {
            pop_scope(true);
            current_bb = builder_.get_insert_block();
            if (current_bb && !current_bb->has_terminator()) {
                if (ret_type->is_void()) {
                    builder_.create_ret_void();
                } else {
                    ir::ValueId default_ret = builder_.create_const_int(ret_type, 0);
                    builder_.create_ret(default_ret);
                }
            }
        } else {
            pop_scope(false);
        }

        fn->rebuild_cfg();
        return true;
    }

} // namespace grass::codegen
