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

void BytecodeCompiler::parseExpression() {
    if (++recursion_depth > MAX_RECURSION_DEPTH) {
        reportError(peek().line, "Превышена максимальная глубина вложенности выражений (Stack Overflow Guard)!");
        --recursion_depth;
        return;
    }
    parseLogicalOr();
    --recursion_depth;
}

void BytecodeCompiler::parseLogicalOr() {
    parseLogicalAnd();
    while (match(TOK_OR) || matchIdent("or")) {
        parseLogicalAnd();
        emitByte(OP_OR);
    }
}

void BytecodeCompiler::parseLogicalAnd() {
    parseBitwiseOr();
    while (match(TOK_AND) || matchIdent("and")) {
        parseBitwiseOr();
        emitByte(OP_AND);
    }
}

void BytecodeCompiler::parseBitwiseOr() {
    parseBitwiseXor();
    while (match(TOK_PIPE)) {
        parseBitwiseXor();
        emitByte(OP_BIT_OR);
    }
}

void BytecodeCompiler::parseBitwiseXor() {
    parseBitwiseAnd();
    while (match(TOK_CARET)) {
        parseBitwiseAnd();
        emitByte(OP_BIT_XOR);
    }
}

void BytecodeCompiler::parseBitwiseAnd() {
    parseComparison();
    while (match(TOK_AMP)) {
        parseComparison();
        emitByte(OP_BIT_AND);
    }
}

void BytecodeCompiler::parseComparison() {
    parseShift();
    while (peek().kind == TOK_EQ || peek().kind == TOK_NEQ ||
           peek().kind == TOK_LT || peek().kind == TOK_GT ||
           peek().kind == TOK_LTE || peek().kind == TOK_GTE) {
        GrassTokenKind op = advance().kind;
        parseShift();
        if (op == TOK_EQ)  emitByte(OP_CMP_EQ);
        if (op == TOK_NEQ) emitByte(OP_CMP_NEQ);
        if (op == TOK_LT)  emitByte(OP_CMP_LT);
        if (op == TOK_GT)  emitByte(OP_CMP_GT);
        if (op == TOK_LTE) emitByte(OP_CMP_LTE);
        if (op == TOK_GTE) emitByte(OP_CMP_GTE);
    }
}

void BytecodeCompiler::parseShift() {
    parseTerm();
    if (!in_stream_io) {
        while (peek().kind == TOK_SHL || peek().kind == TOK_SHR) {
            GrassTokenKind op = advance().kind;
            parseTerm();
            emitByte(op == TOK_SHL ? OP_BIT_SHL : OP_BIT_SHR);
        }
    }
}

void BytecodeCompiler::parseTerm() {
    parseFactor();
    while (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS) {
        GrassTokenKind op = advance().kind;
        parseFactor();
        emitByte(op == TOK_PLUS ? OP_ADD : OP_SUB);
    }
}

void BytecodeCompiler::parseFactor() {
    parseUnary();
    while (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD) {
        GrassTokenKind op = advance().kind;
        parseUnary();
        if (op == TOK_STAR) emitByte(OP_MUL);
        else if (op == TOK_SLASH) emitByte(OP_DIV);
        else emitByte(OP_MOD);
    }
}

void BytecodeCompiler::parseUnary() {
    // C-style pointer/type cast: (Type*)expr or (Type)expr
    if (peek().kind == TOK_LPAREN && peek(1).kind == TOK_IDENT &&
        (isTypeOrFuncKeyword(peek(1).text) || g_struct_registry.count(peek(1).text))) {
        int off = 2;
        while (peek(off).kind == TOK_COLON_COLON && peek(off + 1).kind == TOK_IDENT) off += 2;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_RPAREN) {
            auto nk = peek(off + 1).kind;
            if (nk == TOK_IDENT || nk == TOK_NUMBER || nk == TOK_STRING ||
                nk == TOK_LPAREN || nk == TOK_STAR || nk == TOK_AMP ||
                nk == TOK_MINUS || nk == TOK_NULLPTR) {
                advance(); // '('
                advance(); // base type
                while (match(TOK_COLON_COLON)) { if (peek().kind == TOK_IDENT) advance(); }
                while (match(TOK_STAR)) {}
                match(TOK_RPAREN);
                parseUnary();
                return;
            }
        }
    }
    if (match(TOK_MINUS)) { parseUnary(); emitByte(OP_NEG); return; }
    if (match(TOK_NOT))   { parseUnary(); emitByte(OP_NOT); return; }
    if (match(TOK_TILDE)) { parseUnary(); emitByte(OP_BIT_NOT); return; }
    if (match(TOK_STAR))  { parseUnary(); emitByte(OP_DEREF); return; }
    if (match(TOK_AMP)) {
        if (peek().kind == TOK_IDENT) {
            int line = peek().line;
            string name = advance().text;
            size_t dot_pos = name.find('.');
            if (dot_pos != string::npos) {
                string obj_name = name.substr(0, dot_pos);
                string fld_name = name.substr(dot_pos + 1);
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_ADDR_FIELD); emitString(fld_name);
                return;
            }
            if (match(TOK_LBRACKET)) {
                parseExpression();
                consume(TOK_RBRACKET, "Ожидалась закрывающая ']' в &arr[i]!");
                emitByte(OP_ADDR_INDEX); emitString(name);
                return;
            }
            if (match(TOK_DOT) || match(TOK_ARROW)) {
                if (peek().kind != TOK_IDENT) {
                    reportError(line, "Ожидалось имя поля после '.' или '->' в '&'!");
                    emitByte(OP_CONST_NUM); emitDouble(0.0);
                    return;
                }
                emitByte(OP_LOAD_VAR); emitString(name);
                string fld = advance().text;
                while (match(TOK_DOT) || match(TOK_ARROW)) {
                    emitByte(OP_GET_FIELD); emitString(fld);
                    if (peek().kind != TOK_IDENT) {
                        reportError(line, "Ожидалось имя поля после '.' или '->'!");
                        return;
                    }
                    fld = advance().text;
                }
                emitByte(OP_ADDR_FIELD); emitString(fld);
                return;
            }
            emitByte(OP_ADDR_VAR); emitString(name);
            return;
        }
        reportError(peek().line, "Оператор '&' применим только к переменной, полю или элементу массива!");
        parseUnary();
        return;
    }
    if (match(TOK_PLUS))  { parseUnary(); return; }
    parsePrimary();
    while (match(TOK_AS)) {
        if (peek().kind == TOK_IDENT) {
            advance();
            while (match(TOK_COLON_COLON)) { if (peek().kind == TOK_IDENT) advance(); }
            while (match(TOK_STAR)) {}
        }
    }
}

void BytecodeCompiler::parsePrimary() {
    if (peek().kind == TOK_NUMBER) {
        double v = advance().num_val;
        emitByte(OP_CONST_NUM);
        emitDouble(v);
        return;
    }
    if (peek().kind == TOK_STRING) {
        string s = advance().text;
        emitByte(OP_CONST_STR);
        emitString(s);
        return;
    }
    if (match(TOK_LPAREN)) {
        bool prev_stream = in_stream_io;
        in_stream_io = false;
        parseExpression();
        in_stream_io = prev_stream;
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        return;
    }
    if (match(TOK_LBRACKET)) {
        if (match(TOK_RBRACKET)) {
            emitByte(OP_NEW_ARRAY);
            emitU32(0);
            return;
        }
        uint32_t count = 0;
        do {
            parseExpression();
            count++;
        } while (match(TOK_COMMA));
        match(TOK_RBRACKET);
        emitByte(OP_NEW_ARRAY);
        emitU32(count);
        return;
    }
    if (match(TOK_SIZEOF)) {
        consume(TOK_LPAREN, "Ожидалась '(' после sizeof!");
        if (peek().kind == TOK_IDENT && isTypeOrFuncKeyword(peek().text)) {
            advance();
            while (match(TOK_STAR)) {}
        } else {
            parseExpression();
        }
        consume(TOK_RPAREN, "Ожидалась ')' после sizeof!");
        emitByte(OP_CONST_NUM);
        emitDouble(8.0);
        return;
    }
    if (peek().kind == TOK_NULLPTR) {
        advance();
        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        return;
    }
    if (peek().kind == TOK_IDENT) {
        int tok_line = peek().line;
        string name = advance().text;
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) {
                name += "::" + advance().text;
            }
        }
        if (name == "true")  { emitByte(OP_CONST_NUM); emitDouble(1.0); return; }
        if (name == "false") { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }
        if (name == "endl")  { emitByte(OP_CONST_STR); emitString("\n"); return; }
        if (name == "nullptr" || name == "null") { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }

        // 16 цветов VGA для библиотеки !baremetal!
        if (name == "BLACK")         { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }
        if (name == "BLUE")          { emitByte(OP_CONST_NUM); emitDouble(1.0); return; }
        if (name == "GREEN")         { emitByte(OP_CONST_NUM); emitDouble(2.0); return; }
        if (name == "CYAN")          { emitByte(OP_CONST_NUM); emitDouble(3.0); return; }
        if (name == "RED")           { emitByte(OP_CONST_NUM); emitDouble(4.0); return; }
        if (name == "MAGENTA")       { emitByte(OP_CONST_NUM); emitDouble(5.0); return; }
        if (name == "BROWN")         { emitByte(OP_CONST_NUM); emitDouble(6.0); return; }
        if (name == "LIGHT_GRAY")    { emitByte(OP_CONST_NUM); emitDouble(7.0); return; }
        if (name == "DARK_GRAY")     { emitByte(OP_CONST_NUM); emitDouble(8.0); return; }
        if (name == "LIGHT_BLUE")    { emitByte(OP_CONST_NUM); emitDouble(9.0); return; }
        if (name == "LIGHT_GREEN")   { emitByte(OP_CONST_NUM); emitDouble(10.0); return; }
        if (name == "LIGHT_CYAN")    { emitByte(OP_CONST_NUM); emitDouble(11.0); return; }
        if (name == "LIGHT_RED")     { emitByte(OP_CONST_NUM); emitDouble(12.0); return; }
        if (name == "LIGHT_MAGENTA") { emitByte(OP_CONST_NUM); emitDouble(13.0); return; }
        if (name == "YELLOW")        { emitByte(OP_CONST_NUM); emitDouble(14.0); return; }
        if (name == "WHITE")         { emitByte(OP_CONST_NUM); emitDouble(15.0); return; }

        // Индексация массива / вектора: myArray[1] или v[i].field
        if (match(TOK_LBRACKET)) {
            parseExpression();
            match(TOK_RBRACKET);
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_GET_INDEX);
            string cur_struct;
            auto vit = var_vec_types_.find(name);
            if (vit != var_vec_types_.end() && g_struct_registry.count(vit->second)) {
                cur_struct = vit->second;
            }
            while (match(TOK_DOT) || match(TOK_ARROW)) {
                string member = advance().text;
                if (match(TOK_LPAREN)) {
                    uint8_t argc = 1;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        do {
                            parseExpression();
                            argc++;
                        } while (match(TOK_COMMA));
                    }
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у метода '" + member + "'!");
                    (void)argc;
                    string target_fn = !cur_struct.empty() ? (cur_struct + "_" + member) : (name + "_" + member);
                    if (user_functions.count(target_fn)) {
                        emitByte(OP_CALL_USER);
                        emitU32(user_functions[target_fn].entry_ip);
                    } else {
                        emitByte(OP_CALL_USER);
                        method_call_fixups.push_back({ code.size(), target_fn });
                        emitU32(0);
                    }
                    cur_struct.clear();
                } else {
                    emitByte(OP_GET_FIELD); emitString(member);
                    if (!cur_struct.empty() && g_struct_registry.count(cur_struct)) {
                        const auto& sdef = g_struct_registry[cur_struct];
                        auto fit = sdef.field_indices.find(member);
                        cur_struct = (fit != sdef.field_indices.end()) ? sdef.fields[fit->second].struct_name : "";
                    }
                }
            }
            return;
        }

        // Вызов метода или доступ к полю с точкой в токене: myArray.push(10), myArray.pop(), player.name
        size_t dot_pos = name.find('.');
        if (dot_pos != string::npos) {
            string obj_name = name.substr(0, dot_pos);
            string member = name.substr(dot_pos + 1);

            if (match(TOK_LPAREN)) {
                if (member == "push") {
                    parseExpression();
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    emitByte(OP_ARRAY_PUSH);
                    return;
                } else if (member == "pop") {
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    emitByte(OP_ARRAY_POP);
                    return;
                } else {
                    // Вызов метода структуры: obj.method(args...) -> Struct_method(obj, args...)
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    uint8_t argc = 1;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        do {
                            parseExpression();
                            argc++;
                        } while (match(TOK_COMMA));
                    }
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у метода '" + member + "'!");

                    string target_fn;
                    if (var_struct_types.count(obj_name)) {
                        target_fn = var_struct_types[obj_name] + "_" + member;
                    } else if (obj_name == "this" && !current_struct_context.empty()) {
                        target_fn = current_struct_context + "_" + member;
                    } else if (method_to_structs.count(member) && !method_to_structs[member].empty()) {
                        target_fn = method_to_structs[member][0] + "_" + member;
                    } else {
                        target_fn = obj_name + "_" + member;
                    }

                    if (user_functions.count(target_fn)) {
                        emitByte(OP_CALL_USER);
                        emitU32(user_functions[target_fn].entry_ip);
                    } else {
                        emitByte(OP_CALL_USER);
                        method_call_fixups.push_back({code.size(), target_fn});
                        emitU32(0);
                    }
                    return;
                }
            } else {
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_GET_FIELD); emitString(member);
                return;
            }
        }

        // Точка или стрелка как отдельный токен: obj.member / outer.inner.val / obj.method(args)
        if (match(TOK_DOT) || match(TOK_ARROW)) {
            string member = advance().text;
            bool is_vec_var = (var_vec_types_.count(name) > 0);
            bool struct_has_method = false;
            if (var_struct_types.count(name) && g_struct_registry.count(var_struct_types[name])) {
                const auto& mlist = g_struct_registry[var_struct_types[name]].methods;
                for (const auto& mn : mlist) {
                    if (mn == member) { struct_has_method = true; break; }
                }
            }
            if ((is_vec_var || !struct_has_method) && peek().kind == TOK_LPAREN) {
                if (member == "push" || member == "push_back") {
                    advance(); // (
                    emitByte(OP_LOAD_VAR); emitString(name);
                    parseExpression();
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_push"); emitByte(2);
                    return;
                } else if (member == "pop" || member == "pop_back") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    if (!is_vec_var && member == "pop") {
                        emitByte(OP_LOAD_VAR); emitString(name);
                        emitByte(OP_ARRAY_POP);
                        return;
                    }
                    if (is_vec_var) emitVMVecPopElemDtor(name, var_vec_types_[name]);
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_pop"); emitByte(1);
                    return;
                } else if (member == "size" || member == "len") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_size"); emitByte(1);
                    return;
                } else if (member == "capacity") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_capacity"); emitByte(1);
                    return;
                } else if (member == "empty") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_empty"); emitByte(1);
                    return;
                } else if (member == "clear") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    if (is_vec_var) emitVMVecElemDtorLoop(name, var_vec_types_[name]);
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_clear"); emitByte(1);
                    return;
                } else if (member == "reserve") {
                    advance(); // (
                    emitByte(OP_LOAD_VAR); emitString(name);
                    parseExpression();
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_reserve"); emitByte(2);
                    return;
                } else if (member == "data") {
                    advance(); // (
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_CALL_BUILTIN); emitString("__vec_data"); emitByte(1);
                    if (match(TOK_LBRACKET)) {
                        string tmp_ptr = "__vec_dp_" + to_string(vm_temp_counter_++);
                        emitByte(OP_DEF_VAR); emitString(tmp_ptr);
                        parseExpression();
                        consume(TOK_RBRACKET, "Ожидалась ']'!");
                        emitByte(OP_LOAD_VAR); emitString(tmp_ptr);
                        emitByte(OP_GET_INDEX);
                    }
                    return;
                }
            }

            emitByte(OP_LOAD_VAR); emitString(name);
            string cur_struct;
            if (var_struct_types.count(name)) {
                cur_struct = var_struct_types[name];
            } else if (name == "this" && !current_struct_context.empty()) {
                cur_struct = current_struct_context;
            }

            while (true) {
                if (match(TOK_LPAREN)) {
                    uint8_t argc = 1;
                    if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                        do {
                            parseExpression();
                            argc++;
                        } while (match(TOK_COMMA));
                    }
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' у метода '" + member + "'!");
                    (void)argc;

                    string target_fn;
                    if (!cur_struct.empty()) {
                        target_fn = cur_struct + "_" + member;
                    } else if (method_to_structs.count(member) && !method_to_structs[member].empty()) {
                        target_fn = method_to_structs[member][0] + "_" + member;
                    } else {
                        target_fn = name + "_" + member;
                    }

                    if (user_functions.count(target_fn)) {
                        emitByte(OP_CALL_USER);
                        emitU32(user_functions[target_fn].entry_ip);
                    } else {
                        emitByte(OP_CALL_USER);
                        method_call_fixups.push_back({code.size(), target_fn});
                        emitU32(0);
                    }
                    cur_struct.clear();
                } else {
                    emitByte(OP_GET_FIELD); emitString(member);
                    string next_struct;
                    if (!cur_struct.empty() && g_struct_registry.count(cur_struct)) {
                        const auto& sdef = g_struct_registry[cur_struct];
                        auto fit = sdef.field_indices.find(member);
                        if (fit != sdef.field_indices.end()) {
                            next_struct = sdef.fields[fit->second].struct_name;
                        }
                    }
                    cur_struct = next_struct;
                }
                if (match(TOK_DOT) || match(TOK_ARROW)) {
                    member = advance().text;
                } else {
                    break;
                }
            }
            return;
        }

        // Конструктор структуры: Player()
        if (g_struct_registry.count(name) && match(TOK_LPAREN)) {
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            emitByte(OP_NEW_STRUCT);
            emitString(name);
            return;
        }

        // Создание потока или асинхронной задачи: thread(fn, args...) или async(fn, args...)
        if ((name == "thread" || name == "async") && match(TOK_LPAREN)) {
            requireLib("thread", "!thread!", name + "()", tok_line);
            string fn_target;
            if (peek().kind == TOK_IDENT || peek().kind == TOK_STRING) {
                fn_target = advance().text;
            } else {
                reportError(tok_line, "Ожидалось имя функции в качестве первого аргумента " + name + "()!");
            }
            uint8_t argc = 0;
            while (match(TOK_COMMA)) {
                parseExpression();
                argc++;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");

            emitByte(name == "thread" ? OP_THREAD_SPAWN : OP_ASYNC_SPAWN);
            if (user_functions.count(fn_target)) {
                emitU32(user_functions[fn_target].entry_ip);
            } else {
                thread_spawn_fixups.push_back({code.size(), fn_target});
                emitU32(0);
            }
            emitByte(argc);
            return;
        }

        if (match(TOK_LPAREN)) {
            uint8_t argc = 0;
            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                do {
                    parseExpression();
                    argc++;
                } while (match(TOK_COMMA));
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");

            std::string resolved_fn = resolveUserFunc(name);
            if (!resolved_fn.empty()) {
                emitByte(OP_CALL_USER);
                emitU32(user_functions.at(resolved_fn).entry_ip);
                return;
            }
            if (argc == 0 && func_table.count(name)) {
                emitByte(OP_CALL_FUNC);
                emitU32(func_table[name]);
                return;
            }

            if (name == "ls" && !has_filesystem_struct) {
                reportError(tok_line, "Ошибка компиляции: вызов 'ls()' требует предварительного объявления файловой структуры через virtual_struct { ... } или physical_struct('путь')!");
            }
            checkBuiltinLibraryRequirement(name, tok_line);
            emitByte(OP_CALL_BUILTIN);
            emitString(name);
            emitByte(argc);
            return;
        }

        if (match(TOK_INC)) {
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(OP_ADD);
            emitByte(OP_STORE_VAR); emitString(name);
            return;
        }
        if (match(TOK_DEC)) {
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(OP_SUB);
            emitByte(OP_STORE_VAR); emitString(name);
            return;
        }

        emitByte(OP_LOAD_VAR);
        emitString(name);
        return;
    }

    if (peek().kind == TOK_EOF) {
        reportError(peek().line, "Непредвиденный конец файла: ожидалось выражение!");
    } else {
        reportError(peek().line, "Ожидалось выражение, но получен токен '" + peek().text + "'!");
        advance();
    }
    emitByte(OP_CONST_NUM);
    emitDouble(0.0);
}

