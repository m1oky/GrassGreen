#include "frontend/GrassSyntax.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <set>

using namespace std;

extern std::unordered_map<std::string, GrassStructDef> g_struct_registry;

void BytecodeCompiler::compileProgram() {
    while (peek().kind != TOK_EOF) {
        if (is_special_file) break; // ВЕБ-ФАЙЛЫ НЕ ПАРСЯТСЯ КАК БАЙТ-КОД C++
        if (peek().kind == TOK_RBRACE) {
            advance();
            continue;
        }
        parseStatement();
        if (is_special_file) break;
    }

    if (!is_special_file) {
        if (!included_libs.count("core")) {
            reportError(1, "Отсутствует главный заголовок языка !GrassGreen! в начале файла!");
        }
        if (!user_functions.count("main")) {
            reportError(1, "Отсутствует точка входа 'int main() { ... }'!");
        } else {
            // Регистрация имен и адресов пользовательских функций в VM (для attach_sound и рефлексии)
            for (const auto& kv : user_functions) {
                emitByte(OP_CONST_STR); emitString(kv.first);
                emitByte(OP_CONST_NUM); emitDouble((double)kv.second.entry_ip);
                emitByte(OP_CALL_BUILTIN); emitString("__reg_func"); emitByte(2);
                emitByte(OP_POP);
            }

            emitByte(OP_CALL_USER);
            emitU32(user_functions["main"].entry_ip);
            emitByte(OP_POP);
        }

        for (const auto& fix : thread_spawn_fixups) {
            if (user_functions.count(fix.second)) {
                patchU32(fix.first, user_functions[fix.second].entry_ip);
            } else {
                reportError(1, "Неизвестная функция для thread / async: '" + fix.second + "'!");
            }
        }

        for (const auto& fix : method_call_fixups) {
            if (user_functions.count(fix.second)) {
                patchU32(fix.first, user_functions[fix.second].entry_ip);
            } else {
                bool found = false;
                for (const auto& kv : user_functions) {
                    if (kv.first.size() >= fix.second.size() &&
                        kv.first.rfind("_" + fix.second) != string::npos) {
                        patchU32(fix.first, kv.second.entry_ip);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    reportError(1, "Неизвестный метод структуры: '" + fix.second + "'!");
                }
            }
        }
    }

    emitByte(OP_HALT);
}

void BytecodeCompiler::parseStructDefinition() {
    if (peek().kind != TOK_IDENT) {
        reportError(peek().line, "Ожидалось имя структуры или класса!");
        return;
    }
    string sname = advance().text;
    if (matchIdent("final")) {}
    string base_class;
    if (match(TOK_COLON)) {
        if (matchIdent("public") || matchIdent("private") || matchIdent("protected")) {}
        if (peek().kind == TOK_IDENT) base_class = advance().text;
    } else if (matchIdent("extends")) {
        if (peek().kind == TOK_IDENT) base_class = advance().text;
    }
    if (matchIdent("implements")) {
        while (peek().kind == TOK_IDENT) {
            advance();
            if (!match(TOK_COMMA)) break;
        }
    }
    if (!match(TOK_LBRACE)) {
        reportError(peek().line, "Ожидалось '{' после имени типа!");
        return;
    }

    GrassStructDef def;
    def.name = sname;
    size_t cur_offset = 0;
    size_t max_align = 1;
    bool has_explicit_dtor = false;

    if (!base_class.empty() && g_struct_registry.count(base_class)) {
        const auto& base = g_struct_registry[base_class];
        for (const auto& bf : base.fields) {
            size_t a = bf.alignment > 0 ? bf.alignment : 8;
            cur_offset = (cur_offset + a - 1) & ~(a - 1);
            GrassFieldDef inherited = bf;
            inherited.offset = cur_offset;
            def.field_indices[inherited.name] = def.fields.size();
            def.fields.push_back(inherited);
            cur_offset += bf.size;
            if (a > max_align) max_align = a;
            if (inherited.struct_name.rfind("__vec__:", 0) == 0) {
                struct_field_vecs_[sname][inherited.name] = inherited.struct_name.substr(8);
            }
        }
        if (base.has_destructor) def.has_destructor = true;
        for (const auto& bm : base.methods) {
            if (bm == "__dtor") continue;
            string base_fn = base_class + "_" + bm;
            string derived_fn = sname + "_" + bm;
            if (user_functions.count(base_fn)) {
                user_functions[derived_fn] = user_functions[base_fn];
                func_table[derived_fn] = user_functions[base_fn].entry_ip;
                struct_methods[sname].push_back(bm);
                method_to_structs[bm].push_back(sname);
                def.methods.push_back(bm);
            }
        }
    }

    // Register placeholder early so self/member references resolve inside methods
    g_struct_registry[sname] = def;

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (match(TOK_SEMI)) continue;

        if ((peek().kind == TOK_IDENT && (peek().text == "public" || peek().text == "private" || peek().text == "protected")) &&
            peek(1).kind == TOK_COLON) {
            advance(); advance();
            continue;
        }
        while (peek().kind == TOK_IDENT &&
               (peek().text == "public" || peek().text == "private" || peek().text == "protected" ||
                peek().text == "final" || peek().text == "virtual" || peek().text == "override" ||
                peek().text == "static" || peek().text == "inline")) {
            advance();
        }

        if (match(TOK_TILDE)) {
            string dname;
            if (peek().kind == TOK_IDENT) {
                dname = advance().text;
            }
            (void)dname;
            match(TOK_LPAREN);
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) advance();
            consume(TOK_RPAREN, "Expected ')' after destructor parameters!");

            string fn_name = sname + "__dtor";
            def.has_destructor = true;
            has_explicit_dtor = true;
            dtor_structs_[sname] = true;

            emitByte(OP_JUMP);
            size_t skip_patch = code.size();
            emitU32(0);

            vector<string> params = { "this" };
            uint32_t entry_ip = (uint32_t)code.size();
            user_functions[fn_name] = { entry_ip, params };
            func_table[fn_name] = entry_ip;
            struct_methods[sname].push_back("__dtor");
            method_to_structs["__dtor"].push_back(sname);
            def.methods.push_back("__dtor");

            emitByte(OP_PARAM_PROLOGUE);
            emitByte(OP_DEF_VAR); emitString("this");
            emitByte(OP_PARAM_EPILOGUE);

            string prev_struct = current_struct_context;
            string prev_dtor = current_dtor_struct_;
            auto saved_scopes = std::move(vm_scopes_);
            auto saved_loop_depths = std::move(loop_scope_depths_);
            auto saved_cont_depths = std::move(continue_scope_depths_);
            vm_scopes_.clear();
            loop_scope_depths_.clear();
            continue_scope_depths_.clear();
            current_struct_context = sname;
            current_dtor_struct_ = sname;
            {
                FunctionContextGuard func_guard(inside_function, true);
                parseBlock();
            }
            emitVMFieldDtors(sname);
            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
            emitByte(OP_RET);

            current_dtor_struct_ = prev_dtor;
            current_struct_context = prev_struct;
            vm_scopes_ = std::move(saved_scopes);
            loop_scope_depths_ = std::move(saved_loop_depths);
            continue_scope_depths_ = std::move(saved_cont_depths);

            patchU32(skip_patch, (uint32_t)code.size());
            match(TOK_SEMI);
            continue;
        }

        // Constructor: ClassName(params...) [: field(val), ...] { ... }
        if (peek().kind == TOK_IDENT && peek().text == sname && peek(1).kind == TOK_LPAREN) {
            advance(); // sname
            match(TOK_LPAREN);
            vector<string> params;
            params.push_back("this");
            vector<pair<string, string>> param_structs;
            vector<pair<string, string>> param_vecs;
            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    string ptype, pvec;
                    if (isVMVecTypeStart()) {
                        pvec = parseVMVecInnerType();
                        while (match(TOK_STAR)) {}
                    } else if ((isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text)) &&
                               (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR)) {
                        ptype = advance().text;
                        while (match(TOK_STAR)) {}
                    }
                    string pname = advance().text;
                    params.push_back(pname);
                    if (!pvec.empty()) param_vecs.push_back({pname, pvec});
                    else if (!ptype.empty() && g_struct_registry.count(ptype)) param_structs.push_back({pname, ptype});
                    if (!match(TOK_COMMA)) break;
                }
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая ')' у конструктора '" + sname + "'!");

            size_t user_argc = params.size() - 1;
            ctor_arities_[sname].insert(user_argc);
            string ctor_fn = sname + "__ctor_" + to_string(user_argc);

            emitByte(OP_JUMP);
            size_t skip_patch = code.size();
            emitU32(0);

            uint32_t entry_ip = (uint32_t)code.size();
            user_functions[ctor_fn] = { entry_ip, params };
            func_table[ctor_fn] = entry_ip;

            emitByte(OP_PARAM_PROLOGUE);
            for (int i = (int)params.size() - 1; i >= 0; --i) {
                emitByte(OP_DEF_VAR);
                emitString(params[i]);
            }
            emitByte(OP_PARAM_EPILOGUE);

            string prev_struct = current_struct_context;
            auto saved_scopes = std::move(vm_scopes_);
            auto saved_loop_depths = std::move(loop_scope_depths_);
            auto saved_cont_depths = std::move(continue_scope_depths_);
            vm_scopes_.clear();
            loop_scope_depths_.clear();
            continue_scope_depths_.clear();
            pushVMScope();
            for (const auto& p : params) {
                if (p != "this") vm_scopes_.back().declared_var_types.push_back(p);
            }
            for (const auto& ps : param_structs) registerVMVarType(ps.first, ps.second);
            for (const auto& pv : param_vecs) var_vec_types_[pv.first] = pv.second;
            current_struct_context = sname;
            {
                FunctionContextGuard func_guard(inside_function, true);
                if (match(TOK_COLON)) {
                    while (peek().kind == TOK_IDENT) {
                        string mfield = advance().text;
                        match(TOK_LPAREN);
                        if (mfield == base_class && !base_class.empty()) {
                            emitByte(OP_LOAD_VAR); emitString("this");
                            size_t b_argc = 0;
                            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                                do { parseExpression(); b_argc++; } while (match(TOK_COMMA));
                            }
                            consume(TOK_RPAREN, "Ожидалась ')' в инициализаторе базы!");
                            string b_ctor = base_class + "__ctor_" + to_string(b_argc);
                            if (user_functions.count(b_ctor)) {
                                emitByte(OP_CALL_USER); emitU32(user_functions[b_ctor].entry_ip); emitByte(OP_POP);
                            } else {
                                for (size_t k = 0; k <= b_argc; ++k) emitByte(OP_POP);
                            }
                        } else {
                            if (peek().kind != TOK_RPAREN) parseExpression();
                            else { emitByte(OP_CONST_NUM); emitDouble(0.0); }
                            consume(TOK_RPAREN, "Ожидалась ')' в списке инициализации!");
                            emitByte(OP_LOAD_VAR); emitString("this");
                            emitByte(OP_SET_FIELD); emitString(mfield);
                        }
                        if (!match(TOK_COMMA)) break;
                    }
                }
                parseBlock();
            }
            popVMScope(true);
            current_struct_context = prev_struct;
            vm_scopes_ = std::move(saved_scopes);
            loop_scope_depths_ = std::move(saved_loop_depths);
            continue_scope_depths_ = std::move(saved_cont_depths);

            emitByte(OP_LOAD_VAR); emitString("this");
            emitByte(OP_RET);
            patchU32(skip_patch, (uint32_t)code.size());
            match(TOK_SEMI);
            continue;
        }

        bool is_method = false;
        if (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text)) {
            int off = 1;
            while (peek(off).kind == TOK_STAR) off++;
            if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) {
                is_method = true;
            }
        }
        if (!is_method && peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN) {
            is_method = true;
        }

        if (is_method) {
            string mname;
            if ((isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text)) &&
                (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR)) {
                advance(); // consume return type / keyword
                while (match(TOK_STAR)) {}
                mname = advance().text;
            } else {
                mname = advance().text;
            }

            match(TOK_LPAREN);
            vector<string> params;
            params.push_back("this");
            vector<pair<string, string>> param_structs;
            vector<pair<string, string>> param_vecs;

            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    string ptype, pvec;
                    if (isVMVecTypeStart()) {
                        pvec = parseVMVecInnerType();
                        while (match(TOK_STAR)) {}
                    } else if ((isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text)) &&
                               (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR)) {
                        ptype = advance().text;
                        while (match(TOK_STAR)) {}
                    }
                    string pname = advance().text;
                    params.push_back(pname);
                    if (!pvec.empty()) param_vecs.push_back({pname, pvec});
                    else if (!ptype.empty() && g_struct_registry.count(ptype)) param_structs.push_back({pname, ptype});
                    if (!match(TOK_COMMA)) break;
                }
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у метода '" + mname + "'!");

            string fn_name = sname + "_" + mname;

            emitByte(OP_JUMP);
            size_t skip_patch = code.size();
            emitU32(0);

            uint32_t entry_ip = (uint32_t)code.size();
            user_functions[fn_name] = { entry_ip, params };
            func_table[fn_name] = entry_ip;
            struct_methods[sname].push_back(mname);
            method_to_structs[mname].push_back(sname);
            def.methods.push_back(mname);

            if (!params.empty()) {
                emitByte(OP_PARAM_PROLOGUE);
            }
            for (int i = (int)params.size() - 1; i >= 0; --i) {
                emitByte(OP_DEF_VAR);
                emitString(params[i]);
            }
            if (!params.empty()) {
                emitByte(OP_PARAM_EPILOGUE);
            }

            string prev_struct = current_struct_context;
            auto saved_scopes = std::move(vm_scopes_);
            auto saved_loop_depths = std::move(loop_scope_depths_);
            auto saved_cont_depths = std::move(continue_scope_depths_);
            vm_scopes_.clear();
            loop_scope_depths_.clear();
            continue_scope_depths_.clear();
            pushVMScope();
            for (const auto& p : params) {
                if (p != "this") vm_scopes_.back().declared_var_types.push_back(p);
            }
            for (const auto& ps : param_structs) registerVMVarType(ps.first, ps.second);
            for (const auto& pv : param_vecs) var_vec_types_[pv.first] = pv.second;
            current_struct_context = sname;
            {
                FunctionContextGuard func_guard(inside_function, true);
                parseBlock();
            }
            popVMScope(true);
            current_struct_context = prev_struct;
            vm_scopes_ = std::move(saved_scopes);
            loop_scope_depths_ = std::move(saved_loop_depths);
            continue_scope_depths_ = std::move(saved_cont_depths);

            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
            emitByte(OP_RET);

            patchU32(skip_patch, (uint32_t)code.size());
            match(TOK_SEMI);
            continue;
        }
        // Поля: C-style или RoadMap-style
        string f_type, f_name, f_vec_elem;
        bool is_field_ptr = false;
        if (peek(1).kind == TOK_ASSIGN || peek(1).kind == TOK_COLON) {
            f_name = advance().text;
            advance(); // ':' / '='
            if (isVMVecTypeStart()) {
                f_vec_elem = parseVMVecInnerType();
                f_type = "vec<" + f_vec_elem + ">";
            } else {
                f_type = advance().text;
            }
            while (match(TOK_STAR)) is_field_ptr = true;
            if (match(TOK_COMMA) || match(TOK_SEMI)) {}
        } else {
            if (isVMVecTypeStart()) {
                f_vec_elem = parseVMVecInnerType();
                f_type = "vec<" + f_vec_elem + ">";
            } else {
                f_type = advance().text;
            }
            while (match(TOK_STAR)) is_field_ptr = true;
            f_name = advance().text;
            if (match(TOK_ASSIGN)) {
                while (peek().kind != TOK_SEMI && peek().kind != TOK_COMMA && peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    advance();
                }
            }
            if (match(TOK_SEMI) || match(TOK_COMMA)) {}
        }

        GrassFieldType ft = GrassFieldType::VALUE;
        size_t f_size = 8;
        size_t f_align = 8;
        string f_struct_name;

        if (is_field_ptr) {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
        } else if (!f_vec_elem.empty()) {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
            f_struct_name = "__vec__:" + f_vec_elem;
            struct_field_vecs_[sname][f_name] = f_vec_elem;
            def.has_destructor = true;
        } else if (f_type == "u8" || f_type == "uint8" || f_type == "char" || f_type == "byte") {
            ft = GrassFieldType::U8; f_size = 1; f_align = 1;
        } else if (f_type == "bool") {
            ft = GrassFieldType::BOOL; f_size = 1; f_align = 1;
        } else if (f_type == "u16" || f_type == "uint16" || f_type == "short") {
            ft = GrassFieldType::U16; f_size = 2; f_align = 2;
        } else if (f_type == "u32" || f_type == "uint32") {
            ft = GrassFieldType::U32; f_size = 4; f_align = 4;
        } else if (f_type == "int" || f_type == "i32") {
            ft = GrassFieldType::INT; f_size = 4; f_align = 4;
        } else if (enum_types_.count(f_type)) {
            ft = GrassFieldType::U64; f_size = 8; f_align = 8;
        } else if (f_type == "float" || f_type == "f32") {
            ft = GrassFieldType::FLOAT; f_size = 4; f_align = 4;
        } else if (f_type == "u64" || f_type == "uint64") {
            ft = GrassFieldType::U64; f_size = 8; f_align = 8;
        } else if (f_type == "double" || f_type == "f64" || f_type == "num") {
            ft = GrassFieldType::DOUBLE; f_size = 8; f_align = 8;
        } else if (f_type == "string" || f_type == "str") {
            ft = GrassFieldType::STRING; f_size = 8; f_align = 8;
        } else if (g_struct_registry.count(f_type)) {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
            f_struct_name = f_type;
            if (g_struct_registry[f_type].has_destructor) {
                def.has_destructor = true;
            }
        } else {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
        }

        cur_offset = (cur_offset + f_align - 1) & ~(f_align - 1);

        GrassFieldDef fd;
        fd.name = f_name;
        fd.type = ft;
        fd.offset = cur_offset;
        fd.size = f_size;
        fd.alignment = f_align;
        fd.struct_name = f_struct_name;

        def.field_indices[f_name] = def.fields.size();
        def.fields.push_back(fd);
        g_struct_registry[sname] = def;

        cur_offset += f_size;
        if (f_align > max_align) max_align = f_align;
    }

    consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' структуры!");
    match(TOK_SEMI); // опциональный ';'

    def.alignment = max_align;
    def.total_size = (cur_offset + max_align - 1) & ~(max_align - 1);
    if (def.total_size == 0) def.total_size = 1;

    if (def.has_destructor) {
        dtor_structs_[sname] = true;
    }
    g_struct_registry[sname] = def;

    auto emit_field_dtor_loop = [&]() {
        for (auto it = def.fields.rbegin(); it != def.fields.rend(); ++it) {
            if (it->struct_name.empty()) continue;
            if (it->struct_name.rfind("__vec__:", 0) == 0) {
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_GET_FIELD); emitString(it->name);
                emitByte(OP_CALL_BUILTIN); emitString("__vec_destroy"); emitByte(1);
                emitByte(OP_POP);
                continue;
            }
            auto fit = g_struct_registry.find(it->struct_name);
            if (fit == g_struct_registry.end() || !fit->second.has_destructor) continue;
            emitByte(OP_LOAD_VAR); emitString("this");
            emitByte(OP_GET_FIELD); emitString(it->name);
            string target_fn = it->struct_name + "__dtor";
            if (user_functions.count(target_fn)) {
                emitByte(OP_CALL_USER);
                emitU32(user_functions[target_fn].entry_ip);
            } else {
                emitByte(OP_CALL_USER);
                method_call_fixups.push_back({ code.size(), target_fn });
                emitU32(0);
            }
            emitByte(OP_POP);
        }
    };

    if (has_explicit_dtor) {
        string fields_fn = sname + "__dtor_fields";
        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);
        uint32_t entry_ip = (uint32_t)code.size();
        vector<string> params = { "this" };
        user_functions[fields_fn] = { entry_ip, params };
        func_table[fields_fn] = entry_ip;
        emitByte(OP_PARAM_PROLOGUE);
        emitByte(OP_DEF_VAR); emitString("this");
        emitByte(OP_PARAM_EPILOGUE);
        emit_field_dtor_loop();
        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());
    } else if (def.has_destructor) {
        string dtor_fn = sname + "__dtor";
        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);
        uint32_t entry_ip = (uint32_t)code.size();
        vector<string> params = { "this" };
        user_functions[dtor_fn] = { entry_ip, params };
        func_table[dtor_fn] = entry_ip;
        struct_methods[sname].push_back("__dtor");
        method_to_structs["__dtor"].push_back(sname);
        g_struct_registry[sname].methods.push_back("__dtor");
        emitByte(OP_PARAM_PROLOGUE);
        emitByte(OP_DEF_VAR); emitString("this");
        emitByte(OP_PARAM_EPILOGUE);
        emit_field_dtor_loop();
        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());
    }
}

void BytecodeCompiler::parseVFSBody() {
    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (matchIdent("folder") || matchIdent("dir")) {
            string name;
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                name = advance().text;
            }
            emitByte(OP_CONST_STR); emitString(name);
            emitByte(OP_CALL_BUILTIN); emitString("cr_folder"); emitByte(1); emitByte(OP_POP);

            if (peek().kind == TOK_LBRACE) {
                advance(); // {
                emitByte(OP_CONST_STR); emitString(name);
                emitByte(OP_CALL_BUILTIN); emitString("cd"); emitByte(1); emitByte(OP_POP);

                parseVFSBody();
                consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' папки virtual_struct!");

                emitByte(OP_CONST_STR); emitString("..");
                emitByte(OP_CALL_BUILTIN); emitString("cd"); emitByte(1); emitByte(OP_POP);
            }
            match(TOK_SEMI);
        } else if (matchIdent("file")) {
            string name;
            string fcontent = "";
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                name = advance().text;
            }
            if (match(TOK_COMMA)) {
                if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                    fcontent = advance().text;
                }
            }
            emitByte(OP_CONST_STR); emitString(name);
            emitByte(OP_CONST_STR); emitString(fcontent);
            emitByte(OP_CALL_BUILTIN); emitString("cr_file"); emitByte(2); emitByte(OP_POP);
            match(TOK_SEMI);
        } else if (match(TOK_SEMI)) {
            continue;
        } else {
            FunctionContextGuard vfs_guard(inside_function, true);
            parseStatement();
        }
    }
}

void BytecodeCompiler::parseVirtualStruct() {
    has_filesystem_struct = true;
    int vs_line = peek().line;
    advance(); // virtual_struct
    requireLib("baremetal", "!baremetal!", "virtual_struct", vs_line);

    emitByte(OP_CALL_BUILTIN);
    emitString("virtual_struct");
    emitByte(0);
    emitByte(OP_POP);

    if (match(TOK_LPAREN)) {
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        return;
    }

    if (match(TOK_LBRACE)) {
        parseVFSBody();
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' блока virtual_struct!");
    }
}

// ============================================================================
// IMPLEMENTATION OF GrassParser WITH ERROR RECOVERY
// ============================================================================

GrassParser::GrassParser(const std::vector<GrassToken>& toks, const std::string& fname)
    : tokens(toks), filename(fname) {}

const GrassToken& GrassParser::peek(int offset) const {
    size_t idx = cur + offset;
    if (idx >= tokens.size()) return tokens.back();
    return tokens[idx];
}

GrassToken GrassParser::advance() {
    GrassToken t = peek();
    if (cur < tokens.size() - 1) cur++;
    return t;
}

