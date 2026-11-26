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

bool BytecodeCompiler::parseStatementControlAndVars() {
    size_t before = cur;
    // 3. ПОТОКОВЫЙ ВЫВОД GRASSGREEN / C++: gout / cout << ... << endl;
    bool is_std_stream = (peek().kind == TOK_IDENT && peek().text == "std" && peek(1).kind == TOK_COLON_COLON && (peek(2).text == "gout" || peek(2).text == "cout"));
    if (is_std_stream) { advance(); advance(); }
    if (matchIdent("gout") || matchIdent("cout")) {
        bool prev_stream = in_stream_io;
        in_stream_io = true;
        while (match(TOK_SHL)) {
            bool is_std_endl = (peek().kind == TOK_IDENT && peek().text == "std" && peek(1).kind == TOK_COLON_COLON && peek(2).text == "endl");
            if (is_std_endl) { advance(); advance(); }
            if (matchIdent("endl")) {
                emitByte(OP_LOG_EMPTY);
            } else if (matchIdent("not_clear")) {
                requireLib("baremetal", "!baremetal!", "not_clear", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            } else if (matchIdent("clearable")) {
                requireLib("baremetal", "!baremetal!", "clearable", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
            } else if (matchIdent("clear") || matchIdent("cls")) {
                requireLib("baremetal", "!baremetal!", "clear", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("clear"); emitByte(0); emitByte(OP_POP);
            } else {
                parseExpression();
                emitByte(OP_PRINT_RAW);
            }
        }
        in_stream_io = prev_stream;
        match(TOK_SEMI);
        return true;
    }

    // Потоковый ввод GRASSGREEN / C++: gin / cin >> var;
    if (matchIdent("gin") || matchIdent("cin")) {
        bool prev_stream = in_stream_io;
        in_stream_io = true;
        while (match(TOK_SHR)) {
            if (peek().kind == TOK_IDENT) {
                string vname = advance().text;
                emitByte(OP_CALL_BUILTIN);
                emitString("gin");
                emitByte(0);
                emitByte(OP_STORE_VAR);
                emitString(vname);
            }
        }
        in_stream_io = prev_stream;
        match(TOK_SEMI);
        return true;
    }

    // 4. Ключевое слово 'log' устарело и запрещено (используется только потоковый вывод gout)
    if (matchIdent("log")) {
        reportError(peek(-1).line, "Функция 'log' устарела и полностью удалена из GrassGreen! Используйте потоковый вывод: gout << ... << endl;");
        return true;
    }

    // ВЫВОД В КОНСОЛЬ (print / println)
    if (matchIdent("print") || matchIdent("println")) {
        match(TOK_ASSIGN);
        if (peek().kind == TOK_IDENT && peek().text == "text" &&
            (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_EOF)) {
            advance();
            match(TOK_SEMI);
            emitByte(OP_LOG_EMPTY);
            return true;
        }

        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_RPAREN) {
                advance();
                match(TOK_SEMI);
                emitByte(OP_LOG_EMPTY);
                return true;
            }
            parseExpression();
            while (match(TOK_COMMA)) {
                parseExpression();
                emitByte(OP_ADD);
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            parseExpression();
        }
        match(TOK_SEMI);
        emitByte(OP_LOG);
        return true;
    }

    // 5. ВВОД: gin / input / ask
    if (peek().kind == TOK_IDENT &&
        (peek().text == "gin" || peek().text == "gin.getline" ||
         peek().text == "input" || peek().text == "input.getline" || peek().text == "ask")) {
        size_t saved_cur = cur;
        string fn_name = advance().text;
        string builtin_target = (fn_name == "ask" || fn_name == "gin.getline" || fn_name == "input.getline") ? "gin.getline" : "gin";

        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_IDENT && peek(1).kind == TOK_RPAREN && !user_functions.count(peek().text)) {
                string var_name = advance().text;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(0);
                emitByte(OP_STORE_VAR);
                emitString(var_name);
                return true;
            }
            parseExpression();
            if (match(TOK_COMMA) && peek().kind == TOK_IDENT) {
                string var_name = advance().text;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(1);
                emitByte(OP_STORE_VAR);
                emitString(var_name);
                return true;
            }
            if (match(TOK_RPAREN)) {
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(1);
                emitByte(OP_POP);
                return true;
            }
        }
        cur = saved_cur;
    }

    // 5.1 АУДИО И ИНТЕРФЕЙС: attach_sound, bind_synth, bind_sound
    if (peek().kind == TOK_IDENT && peek().text == "attach_sound") {
        int as_line = advance().line;
        checkBuiltinLibraryRequirement("attach_sound", as_line);
        consume(TOK_LPAREN, "Ожидалась открывающая круглая скобка '(' после attach_sound!");
        if (peek().kind == TOK_STRING) {
            string s = advance().text;
            emitByte(OP_CONST_STR); emitString(s);
        } else if (peek().kind == TOK_IDENT) {
            string id = advance().text;
            emitByte(OP_CONST_STR); emitString(id);
        } else {
            parseExpression();
        }
        consume(TOK_COMMA, "Ожидалась запятая после первого аргумента attach_sound!");
        parseExpression(); // waveform
        consume(TOK_COMMA, "Ожидалась запятая после формы волны в attach_sound!");
        parseExpression(); // freq_hz
        consume(TOK_COMMA, "Ожидалась запятая после частоты в attach_sound!");
        parseExpression(); // duration_ms
        consume(TOK_COMMA, "Ожидалась запятая после длительности в attach_sound!");
        parseExpression(); // volume
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у attach_sound(...)!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("attach_sound"); emitByte(5);
        emitByte(OP_POP);
        return true;
    }

    if (peek().kind == TOK_IDENT && peek().text == "bind_synth") {
        int bs_line = advance().line;
        checkBuiltinLibraryRequirement("bind_synth", bs_line);
        consume(TOK_LPAREN, "Ожидалась открывающая круглая скобка '(' после bind_synth!");
        if (peek().kind == TOK_STRING) {
            string s = advance().text;
            emitByte(OP_CONST_STR); emitString(s);
        } else if (peek().kind == TOK_IDENT) {
            string id = advance().text;
            emitByte(OP_CONST_STR); emitString(id);
        } else {
            parseExpression();
        }
        consume(TOK_COMMA, "Ожидалась запятая после target_id в bind_synth!");
        parseExpression(); // waveform
        consume(TOK_COMMA, "Ожидалась запятая после формы волны в bind_synth!");
        parseExpression(); // freq_hz
        consume(TOK_COMMA, "Ожидалась запятая после частоты в bind_synth!");
        parseExpression(); // duration_ms
        consume(TOK_COMMA, "Ожидалась запятая после длительности в bind_synth!");
        parseExpression(); // volume
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у bind_synth(...)!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("bind_synth"); emitByte(5);
        emitByte(OP_POP);
        return true;
    }

    if (peek().kind == TOK_IDENT && peek().text == "bind_sound") {
        int bs_line = advance().line;
        checkBuiltinLibraryRequirement("bind_sound", bs_line);
        consume(TOK_LPAREN, "Ожидалась открывающая круглая скобка '(' после bind_sound!");
        if (peek().kind == TOK_STRING) {
            string s = advance().text;
            emitByte(OP_CONST_STR); emitString(s);
        } else if (peek().kind == TOK_IDENT) {
            string id = advance().text;
            emitByte(OP_CONST_STR); emitString(id);
        } else {
            parseExpression();
        }
        consume(TOK_COMMA, "Ожидалась запятая после target_id в bind_sound!");
        parseExpression(); // filepath
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у bind_sound(...)!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("bind_sound"); emitByte(2);
        emitByte(OP_POP);
        return true;
    }

    // Объявление динамического массива vec<T> / vector<T>: vec<int> v; или vec<int>* vp = &v;
    if (isVMVecTypeStart()) {
        std::string elem_t = parseVMVecInnerType();
        bool is_explicit_ptr = false;
        while (match(TOK_STAR)) is_explicit_ptr = true;
        if (peek().kind == TOK_IDENT) {
            std::string v_name = advance().text;
            var_vec_types_[v_name] = elem_t;
            if (match(TOK_ASSIGN)) {
                parseExpression();
            } else if (!is_explicit_ptr) {
                emitByte(OP_CALL_BUILTIN); emitString("__vec_create"); emitByte(0);
            } else {
                emitByte(OP_CONST_NUM); emitDouble(0.0);
            }
            emitByte(OP_DEF_VAR); emitString(v_name);
            if (!is_explicit_ptr) {
                registerVMRAIIVar(v_name, "__vec__:" + elem_t);
            }
            match(TOK_SEMI);
            return true;
        }
    }

    // Объявление перечисления (enum) или экземпляра структуры/класса: TokenType t; Player player; Token tok(args...);
    if (peek().kind == TOK_IDENT && enum_types_.count(peek().text)) {
        size_t off = 1;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_IDENT) {
            advance(); // enum type name
            while (match(TOK_STAR)) {}
            string v_name = advance().text;
            if (match(TOK_ASSIGN)) {
                parseExpression();
            } else {
                emitByte(OP_CONST_NUM); emitDouble(0.0);
            }
            emitByte(OP_DEF_VAR); emitString(v_name);
            match(TOK_SEMI);
            return true;
        }
    }
    if (peek().kind == TOK_IDENT && g_struct_registry.count(peek().text)) {
        size_t off = 1;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_IDENT) {
            string s_name = advance().text;
            bool is_explicit_ptr = false;
            while (match(TOK_STAR)) is_explicit_ptr = true;
            string v_name = advance().text;
            registerVMVarType(v_name, s_name);
            if (match(TOK_ASSIGN)) {
                parseExpression();
            } else if (!is_explicit_ptr && match(TOK_LPAREN)) {
                emitByte(OP_NEW_STRUCT);
                emitString(s_name);
                size_t argc = 0;
                if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                    do {
                        parseExpression();
                        argc++;
                    } while (match(TOK_COMMA));
                }
                consume(TOK_RPAREN, "Ожидалась закрывающая скобка ')' в конструкторе '" + s_name + "'!");
                string ctor_fn = s_name + "__ctor_" + to_string(argc);
                if (user_functions.count(ctor_fn)) {
                    emitByte(OP_CALL_USER);
                    emitU32(user_functions[ctor_fn].entry_ip);
                } else {
                    for (size_t k = 0; k < argc; ++k) emitByte(OP_POP);
                }
            } else {
                emitByte(OP_NEW_STRUCT);
                emitString(s_name);
                if (!is_explicit_ptr) {
                    string ctor0 = s_name + "__ctor_0";
                    if (user_functions.count(ctor0)) {
                        emitByte(OP_CALL_USER);
                        emitU32(user_functions[ctor0].entry_ip);
                    }
                }
            }
            emitByte(OP_DEF_VAR);
            emitString(v_name);
            if (!is_explicit_ptr) {
                registerVMRAIIVar(v_name, s_name);
            }
            match(TOK_SEMI);
            return true;
        }
    }

    // 6. ОБЪЯВЛЕНИЕ ПЕРЕМЕННЫХ: volatile, take, set, int, string, float, bool, array
    if (peek().kind == TOK_IDENT && peek().text == "volatile") {
        advance();
    }
    if (peek().kind == TOK_IDENT && (isVarKeyword(peek().text) || peek().text == "take")) {
        string decl_kw = advance().text;
        while (match(TOK_STAR)) {}
        if (match(TOK_ASSIGN) && peek().kind == TOK_IDENT &&
            (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_EOF)) {
            string var_name = advance().text;
            emitByte(OP_CONST_STR);
            emitString("");
            emitByte(OP_DEF_VAR);
            emitString(var_name);
            match(TOK_SEMI);
            return true;
        }

        while (peek().kind == TOK_IDENT || peek().kind == TOK_STAR) {
            while (match(TOK_STAR)) {}
            if (peek().kind != TOK_IDENT) break;
            string var_name = advance().text;
            string inferred_struct;
            bool borrowed_ptr = false;
            if (match(TOK_ASSIGN)) {
                int peek_off = 0;
                if (peek(0).kind == TOK_LPAREN && peek(1).kind == TOK_IDENT &&
                    (isTypeOrFuncKeyword(peek(1).text) || g_struct_registry.count(peek(1).text))) {
                    int off = 2;
                    while (peek(off).kind == TOK_COLON_COLON && peek(off + 1).kind == TOK_IDENT) off += 2;
                    while (peek(off).kind == TOK_STAR) { borrowed_ptr = true; off++; }
                    if (peek(off).kind == TOK_RPAREN) {
                        if (g_struct_registry.count(peek(1).text) && borrowed_ptr) {
                            inferred_struct = peek(1).text;
                        }
                        peek_off = off + 1;
                    }
                }
                if (peek(peek_off).kind == TOK_IDENT && peek(peek_off).text == "new" &&
                    peek(peek_off + 1).kind == TOK_IDENT && g_struct_registry.count(peek(peek_off + 1).text)) {
                    inferred_struct = peek(peek_off + 1).text;
                    borrowed_ptr = true;
                } else if (peek(peek_off).kind == TOK_AMP && peek(peek_off + 1).kind == TOK_IDENT &&
                    var_struct_types.count(peek(peek_off + 1).text)) {
                    inferred_struct = var_struct_types[peek(peek_off + 1).text];
                    borrowed_ptr = true;
                } else if (!borrowed_ptr && peek().kind == TOK_IDENT && g_struct_registry.count(peek().text) && peek(1).kind == TOK_LPAREN) {
                    inferred_struct = peek().text;
                } else if (!borrowed_ptr && peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN && var_struct_types.count("__fn_ret_ptr_" + peek().text)) {
                    inferred_struct = var_struct_types["__fn_ret_ptr_" + peek().text];
                    borrowed_ptr = true;
                } else if (!borrowed_ptr && peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN && var_struct_types.count("__fn_ret_" + peek().text)) {
                    inferred_struct = var_struct_types["__fn_ret_" + peek().text];
                } else if (!borrowed_ptr && peek().kind == TOK_IDENT && (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_COMMA) && var_struct_types.count(peek().text)) {
                    inferred_struct = var_struct_types[peek().text];
                }
                parseExpression();
            } else if (decl_kw == "array") {
                emitByte(OP_NEW_ARRAY);
                emitU32(0);
            } else {
                emitByte(OP_CONST_NUM);
                emitDouble(0.0);
            }
            emitByte(OP_DEF_VAR);
            emitString(var_name);
            if (!inferred_struct.empty()) {
                registerVMVarType(var_name, inferred_struct);
                if (!borrowed_ptr) {
                    registerVMRAIIVar(var_name, inferred_struct);
                }
            }
            if (!match(TOK_COMMA)) break;
        }
        match(TOK_SEMI);
        return true;
    }

    // 7. УСЛОВИЯ: if, check, chek
    if (matchIdent("if") || matchIdent("check") || matchIdent("chek")) {
        if (match(TOK_LPAREN)) {
            parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            parseExpression();
        }

        emitByte(OP_JMP_IF_FALSE);
        size_t false_jmp_patch = code.size();
        emitU32(0);

        parseBlock();

        if (matchIdent("else")) {
            emitByte(OP_JMP);
            size_t end_jmp_patch = code.size();
            emitU32(0);

            patchU32(false_jmp_patch, (uint32_t)code.size());
            if (peek().kind == TOK_IDENT &&
                (peek().text == "if" || peek().text == "check" || peek().text == "chek")) {
                parseStatement();
            } else {
                parseBlock();
            }
            patchU32(end_jmp_patch, (uint32_t)code.size());
        } else {
            patchU32(false_jmp_patch, (uint32_t)code.size());
        }
        return true;
    }

    // 8. ЦИКЛ FOR
    if (matchIdent("for")) {
        match(TOK_LPAREN);
        pushVMScope();
        parseStatement();

        size_t loop_start = code.size();
        if (peek().kind != TOK_SEMI) {
            parseExpression();
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(1.0);
        }
        match(TOK_SEMI);

        emitByte(OP_JMP_IF_FALSE);
        size_t exit_jmp_patch = code.size();
        emitU32(0);

        emitByte(OP_JMP);
        size_t body_jmp_patch = code.size();
        emitU32(0);

        size_t step_start = code.size();
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            parseStepStatement();
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        patchU32(body_jmp_patch, (uint32_t)code.size());
        break_Fixups.push_back({});
        continue_Fixups.push_back({});
        loop_scope_depths_.push_back(vm_scopes_.size());
        continue_scope_depths_.push_back(vm_scopes_.size());
        parseBlock();
        loop_scope_depths_.pop_back();
        continue_scope_depths_.pop_back();

        emitByte(OP_JMP);
        emitU32((uint32_t)step_start);

        size_t loop_end = code.size();
        patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();

        for (size_t patch_pos : continue_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)step_start);
        }
        continue_Fixups.pop_back();
        popVMScope(true);
        return true;
    }

    // 9. ЦИКЛ WHILE
    if (matchIdent("while")) {
        size_t loop_start = code.size();
        if (match(TOK_LPAREN)) {
            parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            parseExpression();
        }

        emitByte(OP_JMP_IF_FALSE);
        size_t exit_jmp_patch = code.size();
        emitU32(0);

        break_Fixups.push_back({});
        continue_Fixups.push_back({});
        loop_scope_depths_.push_back(vm_scopes_.size());
        continue_scope_depths_.push_back(vm_scopes_.size());
        parseBlock();
        loop_scope_depths_.pop_back();
        continue_scope_depths_.pop_back();

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        size_t loop_end = code.size();
        patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();

        for (size_t patch_pos : continue_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_start);
        }
        continue_Fixups.pop_back();
        return true;
    }

    // 10. ЦИКЛ AGAIN(N)
    if (matchIdent("again") || matchIdent("loop")) {
        int my_id = loop_counter++;
        string iter_var = "__iter_" + to_string(my_id);
        string limit_var = "__limit_" + to_string(my_id);

        bool has_count = false;
        if (match(TOK_LPAREN)) {
            has_count = true;
            parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            emitByte(OP_DEF_VAR);
            emitString(limit_var);

            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
            emitByte(OP_DEF_VAR);
            emitString(iter_var);
        }

        size_t loop_start = code.size();
        size_t exit_jmp_patch = 0;

        if (has_count) {
            emitByte(OP_LOAD_VAR); emitString(iter_var);
            emitByte(OP_LOAD_VAR); emitString(limit_var);
            emitByte(OP_CMP_LT);

            emitByte(OP_JMP_IF_FALSE);
            exit_jmp_patch = code.size();
            emitU32(0);

            emitByte(OP_LOAD_VAR); emitString(iter_var);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(OP_ADD);
            emitByte(OP_STORE_VAR); emitString(iter_var);
        }

        break_Fixups.push_back({});
        continue_Fixups.push_back({});
        loop_scope_depths_.push_back(vm_scopes_.size());
        continue_scope_depths_.push_back(vm_scopes_.size());
        parseBlock();
        loop_scope_depths_.pop_back();
        continue_scope_depths_.pop_back();

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        size_t loop_end = code.size();
        if (has_count) patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();

        for (size_t patch_pos : continue_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_start);
        }
        continue_Fixups.pop_back();
        return true;
    }

    if (matchIdent("break") || matchIdent("stop") || matchIdent("stoped")) {
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        if (!loop_scope_depths_.empty()) {
            emitVMScopesCleanupTo(loop_scope_depths_.back());
        }
        emitByte(OP_JMP);
        size_t patch_pos = code.size();
        emitU32(0);
        if (!break_Fixups.empty()) {
            break_Fixups.back().push_back(patch_pos);
        }
        return true;
    }

    if (matchIdent("continue") || matchIdent("next")) {
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        if (continue_Fixups.empty()) {
            reportError(peek(-1).line, "Оператор continue может использоваться только внутри циклов!");
            return true;
        }
        if (!continue_scope_depths_.empty()) {
            emitVMScopesCleanupTo(continue_scope_depths_.back());
        }
        emitByte(OP_JMP);
        size_t patch_pos = code.size();
        emitU32(0);
        continue_Fixups.back().push_back(patch_pos);
        return true;
    }

    if (matchIdent("wait") || matchIdent("sleep")) {
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return true;
    }

    if (matchIdent("exit")) {
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_HALT);
        return true;
    }

        // Присваивание по индексу массива: myArray[1] = 20; или v[i] += val;
        if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
            size_t look = 2;
            int depth = 1;
            while (peek((int)look).kind != TOK_EOF && depth > 0) {
                if (peek((int)look).kind == TOK_LBRACKET) depth++;
                else if (peek((int)look).kind == TOK_RBRACKET) depth--;
                look++;
            }
            GrassTokenKind ak = peek((int)look).kind;
            if (ak == TOK_ASSIGN || ak == TOK_PLUS_ASSIGN || ak == TOK_MINUS_ASSIGN ||
                ak == TOK_MUL_ASSIGN || ak == TOK_DIV_ASSIGN) {
                string arr_name = advance().text;
                match(TOK_LBRACKET);
                size_t idx_start = cur;
                parseExpression(); // index
                match(TOK_RBRACKET);
                GrassTokenKind op = advance().kind;
                if (op == TOK_ASSIGN) {
                    parseExpression(); // value
                    match(TOK_SEMI);
                    emitByte(OP_LOAD_VAR); emitString(arr_name);
                    emitByte(OP_SET_INDEX);
                } else {
                    std::string tmp_idx = "__vm_idx_" + to_string(vm_temp_counter_++);
                    (void)idx_start;
                    emitByte(OP_DEF_VAR); emitString(tmp_idx);
                    emitByte(OP_LOAD_VAR); emitString(tmp_idx);
                    emitByte(OP_LOAD_VAR); emitString(tmp_idx);
                    emitByte(OP_LOAD_VAR); emitString(arr_name);
                    emitByte(OP_GET_INDEX);
                    parseExpression();
                    if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
                    else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
                    else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
                    else emitByte(OP_DIV);
                    match(TOK_SEMI);
                    emitByte(OP_LOAD_VAR); emitString(arr_name);
                    emitByte(OP_SET_INDEX);
                }
                return true;
            }
        }

    // Разыменование указателя при присваивании: *ptr = val; **ptr = val; *(expr) = val;
    if (peek().kind == TOK_STAR) {
        int stars = 0;
        while (peek(stars).kind == TOK_STAR) stars++;
        if (peek(stars).kind == TOK_IDENT && peek(stars + 1).kind == TOK_ASSIGN) {
            for (int i = 0; i < stars; ++i) advance();
            string ptr_name = advance().text;
            advance(); // '='
            emitByte(OP_LOAD_VAR);
            emitString(ptr_name);
            for (int i = 1; i < stars; ++i) emitByte(OP_DEREF);
            parseExpression(); // value
            match(TOK_SEMI);
            emitByte(OP_STORE_DEREF);
            return true;
        }
        if (stars == 1 && peek(1).kind == TOK_LPAREN) {
            int depth = 0;
            int off = 1;
            bool found_assign = false;
            while (peek(off).kind != TOK_EOF) {
                if (peek(off).kind == TOK_LPAREN) depth++;
                else if (peek(off).kind == TOK_RPAREN) {
                    depth--;
                    if (depth == 0) {
                        found_assign = (peek(off + 1).kind == TOK_ASSIGN);
                        break;
                    }
                }
                off++;
            }
            if (found_assign) {
                advance(); // '*'
                advance(); // '('
                parseExpression();
                consume(TOK_RPAREN, "Ожидалась ')' в '*(expr) = val'!");
                advance(); // '='
                parseExpression();
                match(TOK_SEMI);
                emitByte(OP_STORE_DEREF);
                return true;
            }
        }
    }

    // Helper: check if identifier is an implicit field of 'this' in the current method
    auto is_implicit_this_field = [&](const string& id) -> bool {
        if (current_struct_context.empty() || id == "this") return false;
        if (!g_struct_registry.count(current_struct_context)) return false;
        if (!g_struct_registry[current_struct_context].field_indices.count(id)) return false;
        for (const auto& sc : vm_scopes_) {
            for (const auto& dv : sc.declared_var_types) {
                if (dv == id) return false;
            }
        }
        return true;
    };

    // 11. ПРИСВАИВАНИЯ И ИНКРЕМЕНТЫ
    if (peek().kind == TOK_IDENT) {
        string ident_text = peek().text;
        size_t dot = ident_text.find('.');
        if (dot != string::npos &&
            (peek(1).kind == TOK_ASSIGN || peek(1).kind == TOK_PLUS_ASSIGN ||
             peek(1).kind == TOK_MINUS_ASSIGN || peek(1).kind == TOK_MUL_ASSIGN ||
             peek(1).kind == TOK_DIV_ASSIGN)) {
            string obj_name = ident_text.substr(0, dot);
            string field_name = ident_text.substr(dot + 1);
            advance(); // consume token with dot
            GrassTokenKind op = advance().kind;
            if (op == TOK_ASSIGN) {
                parseExpression(); // value
                match(TOK_SEMI);
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_SET_FIELD); emitString(field_name);
            } else {
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_GET_FIELD); emitString(field_name);
                parseExpression();
                if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
                else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
                else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
                else emitByte(OP_DIV);
                match(TOK_SEMI);
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_SET_FIELD); emitString(field_name);
            }
            return true;
        }
        if (peek(1).kind == TOK_DOT || peek(1).kind == TOK_ARROW) {
            int off = 1;
            while ((peek(off).kind == TOK_DOT || peek(off).kind == TOK_ARROW) &&
                   peek(off + 1).kind == TOK_IDENT) {
                off += 2;
            }
            GrassTokenKind op = peek(off).kind;
            if (off > 1 &&
                (op == TOK_ASSIGN || op == TOK_PLUS_ASSIGN ||
                 op == TOK_MINUS_ASSIGN || op == TOK_MUL_ASSIGN ||
                 op == TOK_DIV_ASSIGN)) {
                string obj_name = advance().text;
                bool obj_is_this_fld = is_implicit_this_field(obj_name);
                vector<string> member_chain;
                while ((peek().kind == TOK_DOT || peek().kind == TOK_ARROW) && peek(1).kind == TOK_IDENT) {
                    advance();
                    member_chain.push_back(advance().text);
                }
                advance(); // consume assignment op
                const string& last_field = member_chain.back();
                auto load_root_obj = [&]() {
                    if (obj_is_this_fld) {
                        emitByte(OP_LOAD_VAR); emitString("this");
                        emitByte(OP_GET_FIELD); emitString(obj_name);
                    } else {
                        emitByte(OP_LOAD_VAR); emitString(obj_name);
                    }
                };
                if (op == TOK_ASSIGN) {
                    parseExpression(); // value
                    match(TOK_SEMI);
                    load_root_obj();
                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        emitByte(OP_GET_FIELD); emitString(member_chain[ci]);
                    }
                    emitByte(OP_SET_FIELD); emitString(last_field);
                } else {
                    load_root_obj();
                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        emitByte(OP_GET_FIELD); emitString(member_chain[ci]);
                    }
                    emitByte(OP_GET_FIELD); emitString(last_field);
                    parseExpression();
                    if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
                    else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
                    else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
                    else emitByte(OP_DIV);
                    match(TOK_SEMI);
                    load_root_obj();
                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        emitByte(OP_GET_FIELD); emitString(member_chain[ci]);
                    }
                    emitByte(OP_SET_FIELD); emitString(last_field);
                }
                return true;
            }
        }
        if (peek(1).kind == TOK_ASSIGN) {
            string var_name = advance().text;
            advance();
            if (is_implicit_this_field(var_name)) {
                parseExpression();
                match(TOK_SEMI);
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_SET_FIELD); emitString(var_name);
                return true;
            }
            string old_raii_struct;
            for (auto sit = vm_scopes_.rbegin(); sit != vm_scopes_.rend(); ++sit) {
                for (auto vit = sit->raii_vars.rbegin(); vit != sit->raii_vars.rend(); ++vit) {
                    if (vit->name == var_name) {
                        old_raii_struct = vit->struct_type;
                        break;
                    }
                }
                if (!old_raii_struct.empty()) break;
            }
            string new_struct;
            if (peek().kind == TOK_IDENT && g_struct_registry.count(peek().text) && peek(1).kind == TOK_LPAREN) {
                new_struct = peek().text;
            } else if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN && var_struct_types.count("__fn_ret_" + peek().text)) {
                new_struct = var_struct_types["__fn_ret_" + peek().text];
            } else if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_COMMA) && var_struct_types.count(peek().text)) {
                new_struct = var_struct_types[peek().text];
            }
            if (!new_struct.empty()) {
                var_struct_types[var_name] = new_struct;
            }
            parseExpression();
            match(TOK_SEMI);
            if (!old_raii_struct.empty()) {
                string tmp = "__vm_assign_" + to_string(vm_temp_counter_++);
                emitByte(OP_DEF_VAR); emitString(tmp);
                emitVMDtorCall(var_name, old_raii_struct);
                emitByte(OP_LOAD_VAR); emitString(tmp);
            }
            emitByte(OP_STORE_VAR);
            emitString(var_name);
            return true;
        }
        if (peek(1).kind == TOK_PLUS_ASSIGN || peek(1).kind == TOK_MINUS_ASSIGN ||
            peek(1).kind == TOK_MUL_ASSIGN  || peek(1).kind == TOK_DIV_ASSIGN) {
            string var_name = advance().text;
            GrassTokenKind op = advance().kind;
            if (is_implicit_this_field(var_name)) {
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_GET_FIELD); emitString(var_name);
                parseExpression();
                if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
                else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
                else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
                else emitByte(OP_DIV);
                match(TOK_SEMI);
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_SET_FIELD); emitString(var_name);
                return true;
            }
            emitByte(OP_LOAD_VAR);
            emitString(var_name);
            parseExpression();
            if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
            else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
            else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
            else emitByte(OP_DIV);
            match(TOK_SEMI);
            emitByte(OP_STORE_VAR);
            emitString(var_name);
            return true;
        }
        if (peek(1).kind == TOK_INC || peek(1).kind == TOK_DEC) {
            string var_name = advance().text;
            GrassTokenKind op = advance().kind;
            match(TOK_SEMI);
            if (is_implicit_this_field(var_name)) {
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_GET_FIELD); emitString(var_name);
                emitByte(OP_CONST_NUM); emitDouble(1.0);
                emitByte(op == TOK_INC ? OP_ADD : OP_SUB);
                emitByte(OP_LOAD_VAR); emitString("this");
                emitByte(OP_SET_FIELD); emitString(var_name);
                return true;
            }
            emitByte(OP_LOAD_VAR); emitString(var_name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(op == TOK_INC ? OP_ADD : OP_SUB);
            emitByte(OP_STORE_VAR); emitString(var_name);
            return true;
        }
    }

    before = cur;
    parseExpression();
    match(TOK_SEMI);
    emitByte(OP_POP);
    if (cur == before && peek().kind != TOK_EOF) {
        reportError(peek().line, "Синтаксическая ошибка: непредвиденная инструкция '" + peek().text + "'!");
        synchronize();
    }
    return true;
}
