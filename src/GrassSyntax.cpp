#include "GrassSyntax.h"
#include <iostream>
#include <cstring>
#include <cctype>

using namespace std;

// ============================================================================
// РЕАЛИЗАЦИЯ ЛЕКСЕРА
// ============================================================================
bool Lexer::isIdentStart(unsigned char c) {
    return isalpha(c) || c == '_' || c >= 128;
}

bool Lexer::isIdentCont(unsigned char c) {
    return isalnum(c) || c == '_' || c == '.' || c >= 128;
}

Lexer::Lexer(const string& source) : src(source) {}

vector<GrassToken> Lexer::tokenize() {
    vector<GrassToken> tokens;
    if (src.size() >= 3 &&
        (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF) {
        pos = 3;
    }

    while (pos < src.size()) {
        char c = src[pos];

        if (c == '\r') { pos++; continue; }
        if (c == '\n') { pos++; line++; at_line_start = true; continue; }
        if (c == ' ' || c == '\t') { pos++; continue; }

        // Многострочный комментарий ** ... ** или /* ... */
        if (c == '*' && pos + 1 < src.size() && src[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '*')) {
                if (src[pos] == '\n') line++;
                pos++;
            }
            if (pos + 1 < src.size()) pos += 2;
            continue;
        }
        if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/')) {
                if (src[pos] == '\n') line++;
                pos++;
            }
            if (pos + 1 < src.size()) pos += 2;
            continue;
        }

        // Однострочный комментарий * в начале строки или // (игнорируем, если перед ними двоеточие, как в http:// и https://)
        if ((c == '*' && at_line_start) || 
            (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/' && !(pos > 0 && src[pos - 1] == ':'))) {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }

        // Директивы
        if (c == '@' || (c == '#' && pos + 7 < src.size() && src.substr(pos, 8) == "#include")) {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }

        // Hex-цвета (#00ffcc)
        if (c == '#' && pos + 1 < src.size() && isalnum((unsigned char)src[pos + 1])) {
            size_t start = pos++;
            while (pos < src.size() && isalnum((unsigned char)src[pos])) pos++;
            tokens.push_back(GrassToken(TOK_STRING, src.substr(start, pos - start), 0.0, line));
            at_line_start = false;
            continue;
        }

        // Библиотека !Name! или строка !!текст!!
        if (c == '!') {
            if (pos + 1 < src.size() && src[pos + 1] == '!') {
                pos += 2;
                string s;
                while (pos + 1 < src.size() && !(src[pos] == '!' && src[pos + 1] == '!')) {
                    if (src[pos] == '\n') line++;
                    s += src[pos++];
                }
                if (pos + 1 < src.size()) pos += 2;
                tokens.push_back(GrassToken(TOK_STRING, s, 0.0, line));
                at_line_start = false;
                continue;
            }
            if (pos + 1 < src.size() && src[pos + 1] != '=') {
                size_t end_excl = src.find('!', pos + 1);
                size_t end_line = src.find('\n', pos);
                if (end_excl != string::npos && (end_line == string::npos || end_excl < end_line)) {
                    string lib = src.substr(pos, end_excl - pos + 1);
                    if (lib.find(' ') == string::npos && lib.find('(') == string::npos) {
                        tokens.push_back(GrassToken(TOK_LIB, lib, 0.0, line));
                        pos = end_excl + 1;
                        at_line_start = false;
                        continue;
                    }
                }
            }
        }

        at_line_start = false;

        // Строковые литералы "..." или '...'
        if (c == '"' || c == '\'') {
            char quote = c;
            pos++;
            string s;
            while (pos < src.size() && src[pos] != quote) {
                if (src[pos] == '\\' && pos + 1 < src.size()) {
                    pos++;
                    if (src[pos] == 'n') s += '\n';
                    else if (src[pos] == 't') s += '\t';
                    else if (src[pos] == 'r') s += '\r';
                    else s += src[pos];
                } else if (src[pos] == '/' && pos + 1 < src.size() && src[pos + 1] == 'n') {
                    s += '\n';
                    pos++;
                } else {
                    if (src[pos] == '\n') line++;
                    s += src[pos];
                }
                pos++;
            }
            if (pos < src.size()) pos++;
            tokens.push_back(GrassToken(TOK_STRING, s, 0.0, line));
            continue;
        }

        // Числа и IPv4 адреса (например 127.0.0.1)
        if (isdigit((unsigned char)c)) {
            size_t start = pos;
            int dot_count = 0;
            while (pos < src.size() && (isdigit((unsigned char)src[pos]) || src[pos] == '.')) {
                if (src[pos] == '.') dot_count++;
                pos++;
            }
            string num_str = src.substr(start, pos - start);
            if (dot_count > 1) {
                tokens.push_back(GrassToken(TOK_STRING, num_str, 0.0, line));
            } else {
                double val = 0.0;
                try { val = stod(num_str); } catch (...) {}
                tokens.push_back(GrassToken(TOK_NUMBER, num_str, val, line));
            }
            continue;
        }

        // Идентификаторы
        if (isIdentStart((unsigned char)c)) {
            size_t start = pos;
            while (pos < src.size()) {
                unsigned char cur_c = (unsigned char)src[pos];
                if (isIdentCont(cur_c)) {
                    pos++;
                } else if (cur_c == '-' && pos + 1 < src.size() &&
                           (isIdentStart((unsigned char)src[pos + 1]) || isdigit((unsigned char)src[pos + 1]))) {
                    pos++;
                } else {
                    break;
                }
            }
            string ident = src.substr(start, pos - start);
            tokens.push_back(GrassToken(TOK_IDENT, ident, 0.0, line));
            continue;
        }

        // Двухсимвольные операторы C++
        if (pos + 1 < src.size()) {
            string two = src.substr(pos, 2);
            if (two == "==") { tokens.push_back(GrassToken(TOK_EQ, two, 0.0, line)); pos += 2; continue; }
            if (two == "!=") { tokens.push_back(GrassToken(TOK_NEQ, two, 0.0, line)); pos += 2; continue; }
            if (two == "<=") { tokens.push_back(GrassToken(TOK_LTE, two, 0.0, line)); pos += 2; continue; }
            if (two == ">=") { tokens.push_back(GrassToken(TOK_GTE, two, 0.0, line)); pos += 2; continue; }
            if (two == "&&") { tokens.push_back(GrassToken(TOK_AND, two, 0.0, line)); pos += 2; continue; }
            if (two == "||") { tokens.push_back(GrassToken(TOK_OR, two, 0.0, line)); pos += 2; continue; }
            if (two == "++") { tokens.push_back(GrassToken(TOK_INC, two, 0.0, line)); pos += 2; continue; }
            if (two == "--") { tokens.push_back(GrassToken(TOK_DEC, two, 0.0, line)); pos += 2; continue; }
            if (two == "+=") { tokens.push_back(GrassToken(TOK_PLUS_ASSIGN, two, 0.0, line)); pos += 2; continue; }
            if (two == "-=") { tokens.push_back(GrassToken(TOK_MINUS_ASSIGN, two, 0.0, line)); pos += 2; continue; }
            if (two == "*=") { tokens.push_back(GrassToken(TOK_MUL_ASSIGN, two, 0.0, line)); pos += 2; continue; }
            if (two == "/=") { tokens.push_back(GrassToken(TOK_DIV_ASSIGN, two, 0.0, line)); pos += 2; continue; }
            if (two == "<<") { tokens.push_back(GrassToken(TOK_SHL, two, 0.0, line)); pos += 2; continue; }
            if (two == ">>") { tokens.push_back(GrassToken(TOK_SHR, two, 0.0, line)); pos += 2; continue; }
        }

        // Односимвольные операторы и скобки
        pos++;
        switch (c) {
            case '{': tokens.push_back(GrassToken(TOK_LBRACE, "{", 0.0, line)); at_line_start = true; break;
            case '}': tokens.push_back(GrassToken(TOK_RBRACE, "}", 0.0, line)); at_line_start = true; break;
            case '(': tokens.push_back(GrassToken(TOK_LPAREN, "(", 0.0, line)); break;
            case ')': tokens.push_back(GrassToken(TOK_RPAREN, ")", 0.0, line)); break;
            case '[': tokens.push_back(GrassToken(TOK_LBRACKET, "[", 0.0, line)); break;
            case ']': tokens.push_back(GrassToken(TOK_RBRACKET, "]", 0.0, line)); break;
            case ',': tokens.push_back(GrassToken(TOK_COMMA, ",", 0.0, line)); break;
            case ';': tokens.push_back(GrassToken(TOK_SEMI, ";", 0.0, line)); break;
            case ':':
            case '=': tokens.push_back(GrassToken(TOK_ASSIGN, "=", 0.0, line)); break;
            case '+': tokens.push_back(GrassToken(TOK_PLUS, "+", 0.0, line)); break;
            case '-': tokens.push_back(GrassToken(TOK_MINUS, "-", 0.0, line)); break;
            case '*': tokens.push_back(GrassToken(TOK_STAR, "*", 0.0, line)); break;
            case '/': tokens.push_back(GrassToken(TOK_SLASH, "/", 0.0, line)); break;
            case '%': tokens.push_back(GrassToken(TOK_MOD, "%", 0.0, line)); break;
            case '!': tokens.push_back(GrassToken(TOK_NOT, "!", 0.0, line)); break;
            case '<': tokens.push_back(GrassToken(TOK_LT, "<", 0.0, line)); break;
            case '>': tokens.push_back(GrassToken(TOK_GT, ">", 0.0, line)); break;
            default: break;
        }
    }
    tokens.push_back(GrassToken(TOK_EOF, "", 0.0, line));
    return tokens;
}

// ============================================================================
// РЕАЛИЗАЦИЯ КОМПИЛЯТОРА И КОНТРОЛЯ СИНТАКСИСА
// ============================================================================
BytecodeCompiler::BytecodeCompiler(const vector<GrassToken>& toks, const string& fname)
    : tokens(toks), source_filename(fname) {}

GrassToken& BytecodeCompiler::peek(int offset) {
    if (cur + offset >= tokens.size()) return tokens.back();
    return tokens[cur + offset];
}

GrassToken BytecodeCompiler::advance() {
    GrassToken t = peek();
    if (cur < tokens.size() - 1) cur++;
    return t;
}

bool BytecodeCompiler::match(GrassTokenKind kind) {
    if (peek().kind == kind) { advance(); return true; }
    return false;
}

bool BytecodeCompiler::matchIdent(const string& name) {
    if (peek().kind == TOK_IDENT && peek().text == name) { advance(); return true; }
    return false;
}

void BytecodeCompiler::reportError(int line, const string& msg) {
    cerr << "[GrassSyntax Error] (строка " << line << "): " << msg << endl;
    has_error = true;
}

bool BytecodeCompiler::requireLib(const string& lib_key, const string& display_name, const string& feature_desc, int line) {
    if (!included_libs.count(lib_key)) {
        reportError(line, "Использование '" + feature_desc + "' запрещено без подключения библиотеки " + display_name + " в начале файла!");
        return false;
    }
    return true;
}

void BytecodeCompiler::checkBuiltinLibraryRequirement(const string& fn, int line) {
    // 1. Проверка !FastMath!
    if (fn == "discor" || fn == "sqrt" || fn == "sin" || fn == "cos" || fn == "abs" ||
        fn == "round" || fn == "floor" || fn == "ceil" || fn == "pow" || fn == "clamp" ||
        fn == "min" || fn == "max" || fn == "rand" || fn == "lerp") {
        requireLib("fastmath", "!FastMath!", fn + "()", line);
    }
    // 2. Проверка !CoreMath!
    else if (fn == "qb_init" || fn == "h" || fn == "x" || fn == "cnot" ||
             fn == "q_rand" || fn == "pi" || fn == "gcd" || fn == "pow_e") {
        requireLib("coremath", "!CoreMath!", fn + "()", line);
    }
    // 3. Проверка !memory!
    else if (fn.find("memory_") == 0) {
        requireLib("memory", "!memory!", fn + "()", line);
    }
    // 4. Проверка !random!
    else if (fn == "random" || fn == "random_txt" || fn == "stop_random" || fn == "play_random") {
        requireLib("random", "!random!", fn + "()", line);
    }
    // 5. Проверка !database!
    else if (fn.find("db_") == 0 || fn == "table" || fn == "user" || fn == "last_log" ||
             fn == "net_tunnel_protocol" || fn == "net_baypass_state" || fn == "net_bypass_state" ||
             fn == "net_gateway_route" || fn == "remember_me" || fn == "auth_token_hash" ||
             fn == "env_target_runtime" || fn == "env_exec_flags" || fn == "net_bypass_config" ||
             fn == "net_ping_ms" || fn == "env_allocated_ram" || fn == "env_java_path" ||
             fn == "sys_fps_stabilizer" || fn == "sys_gpu_force_integrated") {
        requireLib("db", "!database!", fn + "()", line);
    }
    // 6. Проверка !interface!
    else if (fn.find("gui_") == 0) {
        requireLib("gui", "!interface!", fn + "()", line);
    }
    // 7. Проверка библиотеки !time!
    else if (fn == "start_time" || fn == "stop_time" || fn == "conclus_time" ||
             fn == "time" || fn == "clock_ms" || fn == "now_ms" ||
             fn == "get_date" || fn == "get_time_str") {
        requireLib("time", "!time!", fn + "()", line);
    }
    // 8. Проверка библиотеки !GrassNet!
    else if (fn == "connect" || fn == "send" || fn == "accept" || fn == "create_serv" ||
             fn == "net_set_agent" || fn == "API" || fn == "ping" || fn == "download" ||
             fn == "resolve_ip" || fn == "local_ip" || fn == "net_status" || fn == "net_close") {
        requireLib("net", "!GrassNet!", fn + "()", line);
    }
    // 9. Проверка библиотеки !json!
    else if (fn == "json_get" || fn == "json_set" || fn == "json_valid" ||
             fn == "json_minify" || fn == "json_clear" || fn == "json_add" ||
             fn == "json_add_num" || fn == "json_build") {
        requireLib("json", "!json!", fn + "()", line);
    }
    // 10. Проверка библиотеки !sys!
    else if (fn == "clip_set" || fn == "clip_get" || fn == "screen_w" ||
             fn == "screen_h" || fn == "pc_name" || fn == "user_name" ||
             fn == "cpu_cores" || fn == "ram_total_mb" || fn == "ram_free_mb" ||
             fn == "get_env" || fn == "uptime_sec") {
        requireLib("sys", "!sys!", fn + "()", line);
    }
    // 11. Проверка библиотеки !proc!
    else if (fn == "proc_find" || fn == "proc_kill" || fn == "proc_mem" ||
             fn == "proc_exists" || fn == "proc_list" ||
             fn == "kill" || fn == "mem" || fn == "list") {
        requireLib("proc", "!proc!", fn + "()", line);
    }
    // 12. Проверка библиотеки !fsys!
    else if (fn == "fs_mkdir" || fn == "fs_is_dir" || fn == "fs_size" ||
             fn == "fs_copy"  || fn == "fs_list") {
        requireLib("fsys", "!fsys!", fn + "()", line);
    }
    // 13. Проверка библиотеки !tg!
    else if (fn == "tg_init" || fn == "tg_poll" || fn == "tg_get_user" ||
             fn == "tg_get_chat_id" || fn == "tg_get_msg_id" || fn == "tg_get_callback" ||
             fn == "tg_send" || fn == "tg_reply" || fn == "tg_send_btn" ||
             fn == "tg_send_inline" || fn == "tg_send_webapp" || fn == "tg_send_photo" ||
             fn == "tg_send_dice" || fn == "tg_edit" || fn == "tg_del") {
        requireLib("tg", "!tg!", fn + "()", line);
    }
    // 14. Проверка библиотеки !2DGraphics!
    else if (fn == "gfx_init" || fn == "gfx_running" || fn == "gfx_clear" ||
             fn == "gfx_rect" || fn == "gfx_circle"  || fn == "gfx_line" ||
             fn == "gfx_text" || fn == "gfx_key"     || fn == "gfx_mouse_x" ||
             fn == "gfx_mouse_y" || fn == "gfx_mouse_down" || fn == "gfx_collide" ||
             fn == "gfx_frame" || fn == "gfx_close") {
        requireLib("2DGraphics", "!2DGraphics!", fn + "()", line);
    }
}

bool BytecodeCompiler::isTypeOrFuncKeyword(const string& s) {
    return s == "take" || s == "void" || s == "func" || s == "alt" || s == "fn" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto";
}

bool BytecodeCompiler::isVarKeyword(const string& s) {
    return s == "set" || s == "var" || s == "let" || s == "fany" || s == "invent" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto";
}

void BytecodeCompiler::emitByte(uint8_t b) { code.push_back(b); }

void BytecodeCompiler::emitU32(uint32_t v) {
    uint8_t bytes[4];
    memcpy(bytes, &v, 4);
    for (int i = 0; i < 4; ++i) code.push_back(bytes[i]);
}

void BytecodeCompiler::patchU32(size_t offset, uint32_t v) {
    if (offset + 4 <= code.size()) {
        memcpy(&code[offset], &v, 4);
    }
}

void BytecodeCompiler::emitDouble(double d) {
    uint8_t bytes[8];
    memcpy(bytes, &d, 8);
    for (int i = 0; i < 8; ++i) code.push_back(bytes[i]);
}

void BytecodeCompiler::emitString(const string& s) {
    emitU32((uint32_t)s.size());
    for (char c : s) code.push_back((uint8_t)c);
}

void BytecodeCompiler::emitUICmd(const string& cmd_line) {
    emitByte(OP_UI_CMD);
    emitString(cmd_line);
}

void BytecodeCompiler::parseExpression() { parseLogicalOr(); }

void BytecodeCompiler::parseLogicalOr() {
    parseLogicalAnd();
    while (match(TOK_OR) || matchIdent("or")) {
        parseLogicalAnd();
        emitByte(OP_OR);
    }
}

void BytecodeCompiler::parseLogicalAnd() {
    parseComparison();
    while (match(TOK_AND) || matchIdent("and")) {
        parseComparison();
        emitByte(OP_AND);
    }
}

void BytecodeCompiler::parseComparison() {
    parseTerm();
    while (peek().kind == TOK_EQ || peek().kind == TOK_NEQ ||
           peek().kind == TOK_LT || peek().kind == TOK_GT ||
           peek().kind == TOK_LTE || peek().kind == TOK_GTE) {
        GrassTokenKind op = advance().kind;
        parseTerm();
        if (op == TOK_EQ)  emitByte(OP_CMP_EQ);
        if (op == TOK_NEQ) emitByte(OP_CMP_NEQ);
        if (op == TOK_LT)  emitByte(OP_CMP_LT);
        if (op == TOK_GT)  emitByte(OP_CMP_GT);
        if (op == TOK_LTE) emitByte(OP_CMP_LTE);
        if (op == TOK_GTE) emitByte(OP_CMP_GTE);
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
    if (match(TOK_MINUS)) { parseUnary(); emitByte(OP_NEG); return; }
    if (match(TOK_NOT))   { parseUnary(); emitByte(OP_NOT); return; }
    if (match(TOK_PLUS))  { parseUnary(); return; }
    parsePrimary();
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
        parseExpression();
        match(TOK_RPAREN);
        return;
    }
    if (match(TOK_LBRACKET)) {
        string joined;
        while (peek().kind != TOK_RBRACKET && peek().kind != TOK_EOF) {
            joined += advance().text;
            if (match(TOK_COMMA)) joined += ", ";
        }
        match(TOK_RBRACKET);
        emitByte(OP_CONST_STR);
        emitString(joined);
        return;
    }
    if (peek().kind == TOK_IDENT) {
        int tok_line = peek().line;
        string name = advance().text;
        if (name == "true")  { emitByte(OP_CONST_NUM); emitDouble(1.0); return; }
        if (name == "false") { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }
        if (name == "endl")  { emitByte(OP_CONST_STR); emitString("\n"); return; }

        if (match(TOK_LPAREN)) {
            uint8_t argc = 0;
            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                do {
                    parseExpression();
                    argc++;
                } while (match(TOK_COMMA));
            }
            match(TOK_RPAREN);

            if (user_functions.count(name)) {
                emitByte(OP_CALL_USER);
                emitU32(user_functions[name].entry_ip);
                return;
            }
            if (argc == 0 && func_table.count(name)) {
                emitByte(OP_CALL_FUNC);
                emitU32(func_table[name]);
                return;
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

    if (peek().kind != TOK_EOF) advance();
    emitByte(OP_CONST_NUM);
    emitDouble(0.0);
}

string BytecodeCompiler::extractParenArgsAsString() {
    string res;
    if (match(TOK_LPAREN)) {
        bool first = true;
        while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
            if (peek().kind == TOK_COMMA) {
                advance();
                res += ", ";
                first = true;
            } else {
                if (!first) res += " ";
                res += advance().text;
                first = false;
            }
        }
        match(TOK_RPAREN);
    } else {
        match(TOK_ASSIGN);
        if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
            res = advance().text;
        }
    }
    match(TOK_SEMI);
    return res;
}

bool BytecodeCompiler::parseSingleGuiDirective() {
    if (match(TOK_SEMI)) return true;
    if (peek().kind != TOK_IDENT) return false;

    string cmd = peek().text;

    if (cmd.find("animate-") == 0) {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitUICmd(cmd);
        return true;
    }

    if (cmd == "show") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        return true;
    }

    // Элементы интерфейса: button "Текст" { ... }, label "Текст" { ... }, enter "Текст" { ... }
    if (cmd == "button" || cmd == "label" || cmd == "enter" || cmd == "icon" || cmd == "video") {
        int el_line = peek().line;
        advance();
        string inline_id = "";
        if (peek().kind == TOK_IDENT && peek(1).kind == TOK_ASSIGN) {
            inline_id = advance().text;
        }
        match(TOK_ASSIGN);

        string txt = "";
        if (match(TOK_LPAREN)) {
            bool first = true;
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                if (!first) txt += " ";
                txt += advance().text;
                first = false;
            }
            match(TOK_RPAREN);
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
            txt = advance().text;
        }

        if (cmd == "icon" || cmd == "video") {
            emitUICmd(cmd + "(" + txt + ")");
        } else {
            emitUICmd(cmd + " !!" + txt + "!!");
        }

        if (!inline_id.empty()) {
            emitUICmd("id(" + inline_id + ")");
        }

        if (!match(TOK_LBRACE)) {
            reportError(el_line, "Элемент '" + cmd + "' в едином синтаксисе GrassGreen должен открывать блок свойств '{ ... }'!");
            return true;
        }

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (!parseSingleGuiDirective()) advance();
        }
        match(TOK_RBRACE);
        match(TOK_SEMI);
        return true;
    }

    if (cmd == "middle" || cmd == "center" || cmd == "left" || cmd == "right" ||
        cmd == "shadow" || cmd == "glass" || cmd == "clear") {
        advance();
        string norm_cmd = (cmd == "center") ? "middle" : cmd;
        string arg = "";
        if (match(TOK_LPAREN)) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) arg += advance().text;
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        if (!arg.empty()) emitUICmd(norm_cmd + "(" + arg + ")");
        else emitUICmd(norm_cmd);
        return true;
    }

    if (cmd == "title" || cmd == "name" || cmd == "size" || cmd == "size-top" || cmd == "height" ||
        cmd == "color" || cmd == "bg" || cmd == "background" || cmd == "theme" || cmd == "fon" ||
        cmd == "iconP" || cmd == "id" || cmd == "name_el" || cmd == "window-ob" || cmd == "pos" ||
        cmd == "position" || cmd == "align" || cmd == "text-color" || cmd == "text_color" ||
        cmd == "button-color" || cmd == "button_color" || cmd == "font-size" || cmd == "font_size" ||
        cmd == "font-style" || cmd == "font_style" || cmd == "radius" || cmd == "pad" ||
        cmd == "click-ob" || cmd == "switch" || cmd == "run" || cmd == "action") {
        advance();
        string arg = extractParenArgsAsString();

        if (cmd == "height") cmd = "size-top";
        else if (cmd == "bg" || cmd == "background") cmd = "color";
        else if (cmd == "pos" || cmd == "position") cmd = "window-ob";
        else if (cmd == "text_color") cmd = "text-color";
        else if (cmd == "button_color") cmd = "button-color";
        else if (cmd == "font_size") cmd = "font-size";
        else if (cmd == "font_style") cmd = "font-style";
        else if (cmd == "action") cmd = "run";

        if (cmd == "align") {
            if (arg == "center" || arg == "middle") emitUICmd("middle");
            else if (arg == "left") emitUICmd("left");
            else if (arg == "right") emitUICmd("right");
        } else if (cmd == "title" || cmd == "name") {
            emitUICmd(cmd + "(!!" + arg + "!!)");
        } else {
            emitUICmd(cmd + "(" + arg + ")");
        }
        return true;
    }

    return false;
}

void BytecodeCompiler::parseWindowBlock() {
    int win_line = peek(-1).line;
    requireLib("gui", "!interface!", "window { ... }", win_line);

    if (match(TOK_LPAREN)) {
        while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) advance();
        match(TOK_RPAREN);
    }

    if (!match(TOK_LBRACE)) {
        reportError(win_line, "После ключевого слова 'window' обязателен блок в фигурных скобках '{ ... }'!");
        return;
    }

    emitByte(OP_UI_INIT);
    emitString("GrassGreen UI");
    emitU32(640);
    emitU32(480);
    emitString("#12131a");
    emitString("");

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (!parseSingleGuiDirective()) {
            advance();
        }
    }
    match(TOK_RBRACE);
    match(TOK_SEMI);
    emitByte(OP_UI_SHOW);
}

void BytecodeCompiler::parseBlock() {
    if (match(TOK_LBRACE)) {
        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        match(TOK_RBRACE);
    } else {
        reportError(peek().line, "Ожидалась открывающая фигурная скобка '{' для блока кода!");
        parseStatement();
    }
}

void BytecodeCompiler::parseStepStatement() {
    if (peek().kind == TOK_IDENT) {
        string var_name = peek().text;
        if (peek(1).kind == TOK_INC) {
            advance(); advance();
            emitByte(OP_LOAD_VAR); emitString(var_name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(OP_ADD);
            emitByte(OP_STORE_VAR); emitString(var_name);
            return;
        }
        if (peek(1).kind == TOK_DEC) {
            advance(); advance();
            emitByte(OP_LOAD_VAR); emitString(var_name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(OP_SUB);
            emitByte(OP_STORE_VAR); emitString(var_name);
            return;
        }
        if (peek(1).kind == TOK_PLUS_ASSIGN || peek(1).kind == TOK_MINUS_ASSIGN ||
            peek(1).kind == TOK_MUL_ASSIGN  || peek(1).kind == TOK_DIV_ASSIGN) {
            advance();
            GrassTokenKind op = advance().kind;
            emitByte(OP_LOAD_VAR); emitString(var_name);
            parseExpression();
            if (op == TOK_PLUS_ASSIGN) emitByte(OP_ADD);
            else if (op == TOK_MINUS_ASSIGN) emitByte(OP_SUB);
            else if (op == TOK_MUL_ASSIGN) emitByte(OP_MUL);
            else emitByte(OP_DIV);
            emitByte(OP_STORE_VAR); emitString(var_name);
            return;
        }
        if (peek(1).kind == TOK_ASSIGN) {
            advance(); advance();
            parseExpression();
            emitByte(OP_STORE_VAR); emitString(var_name);
            return;
        }
    }
    parseExpression();
    emitByte(OP_POP);
}

void BytecodeCompiler::parseStatement() {
    if (match(TOK_SEMI)) return;
    if (peek().kind == TOK_RBRACE || peek().kind == TOK_EOF) return;

    // 0. РЕГИСТРАЦИЯ ПОДКЛЮЧЕННЫХ БИБЛИОТЕК (!GrassGreen!, !interface!, !database! и т.д.)
    if (peek().kind == TOK_LIB) {
        string lib = advance().text;
        if (lib == "!GrassGreen!") {
            included_libs.insert("core");
        } else if (lib == "!sys!") {
            included_libs.insert("sys");
        } else if (lib == "!json!") {
            included_libs.insert("json");
        } else if (lib == "!tg!") {
            included_libs.insert("tg");
        } else if (lib == "!2DGraphics!") {
            included_libs.insert("2DGraphics");
        } else if (lib == "!proc!") {
            included_libs.insert("proc");
        } else if (lib == "!fsys!") {
            included_libs.insert("fsys");
        } else if (lib == "!GrassNet!" || lib == "!net!") {
            included_libs.insert("net");
        } else if (lib == "!interface!" || lib == "!gui!" || lib == "!GrassGUI!") {
            included_libs.insert("gui");
        } else if (lib == "!time!") {
            included_libs.insert("time");
        } else if (lib == "!FastMath!") {
            included_libs.insert("fastmath");
        } else if (lib == "!CoreMath!") {
            included_libs.insert("coremath");
        } else if (lib == "!memory!") {
            included_libs.insert("memory");
        } else if (lib == "!random!") {
            included_libs.insert("random");
        } else if (lib == "!database!" || lib == "!gdb!") {
            included_libs.insert("db");
            if (!included_libs.count("core")) is_special_file = true;
            emitByte(OP_LOAD_DB);
        } else if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
            included_libs.insert("web");
            is_special_file = true;
            emitByte(OP_START_WEB);
            emitString(source_filename);
            emitByte(OP_HALT);
        } else {
            reportError(peek(-1).line, "Неизвестная библиотека: " + lib);
        }
        return;
    }

    // 1. ОБЪЯВЛЕНИЕ ФУНКЦИЙ (int main() {}, take my_func() {}, void my_func() {})
    if (peek().kind == TOK_IDENT && isTypeOrFuncKeyword(peek().text) &&
        peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
        int fn_line = peek().line;
        requireLib("core", "!GrassGreen!", "объявление функции", fn_line);

        advance();
        string fn_name = advance().text;
        vector<string> params;

        match(TOK_LPAREN);
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            while (true) {
                if (isTypeOrFuncKeyword(peek().text) && peek(1).kind == TOK_IDENT) {
                    advance();
                }
                params.push_back(advance().text);
                if (!match(TOK_COMMA)) break;
            }
        }
        match(TOK_RPAREN);

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t entry_ip = (uint32_t)code.size();
        user_functions[fn_name] = { entry_ip, params };
        func_table[fn_name] = entry_ip;

        for (int i = (int)params.size() - 1; i >= 0; --i) {
            emitByte(OP_STORE_VAR);
            emitString(params[i]);
        }

        bool prev_inside = inside_function;
        inside_function = true;
        parseBlock();
        inside_function = prev_inside;

        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        emitByte(OP_RET);

        patchU32(skip_patch, (uint32_t)code.size());
        return;
    }

    // Запрет на выполнение кода вне int main() {} или пользовательских функций
    if (!inside_function && !is_special_file) {
        reportError(peek().line, "Инструкция '" + peek().text + "' находится вне функции! Весь исполняемый код должен быть внутри int main() { ... }.");
        advance();
        return;
    }

    if (peek().kind == TOK_LBRACE) {
        parseBlock();
        return;
    }

    if (peek().kind == TOK_SLASH && peek(1).kind == TOK_IDENT && peek(1).text == "n") {
        advance(); advance();
        match(TOK_SEMI);
        emitByte(OP_LOG_EMPTY);
        return;
    }

    if (peek().kind == TOK_IDENT && peek().text == "text" && peek(1).kind == TOK_SEMI) {
        advance(); advance();
        emitByte(OP_LOG_EMPTY);
        return;
    }

    if (matchIdent("end-table")) {
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN);
        emitString("table");
        emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("return")) {
        if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseExpression();
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
        }
        match(TOK_SEMI);
        emitByte(OP_RET);
        return;
    }

    // 2. БЛОК ОКНА ИНТЕРФЕЙСА: window { ... }
    if (matchIdent("window")) {
        parseWindowBlock();
        return;
    }

    // Блок настройки сетевого агента: agent { browser "..."; device "..."; }
    if (matchIdent("agent")) {
        int ag_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "agent { ... }", ag_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        string browser_val = "GrassGreenNet/3.1";
        string device_val = "Windows NT 10.0; Win64; x64";

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (matchIdent("browser")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) browser_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("device")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) device_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            match(TOK_RBRACE);
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(browser_val);
        emitByte(OP_CONST_STR); emitString(device_val);
        emitByte(OP_CALL_BUILTIN); emitString("net_set_agent"); emitByte(2);
        emitByte(OP_POP);
        return;
    }

    // Блок создания сервера: create_serv() { port: 443; IP: 127.0.0.1; password: 1234; certainIP: 127.0.0.1; }
    if (matchIdent("create_serv")) {
        int srv_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "create_serv()", srv_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        string ip_val = "127.0.0.1";
        double port_val = 443.0;
        string pass_val = "";
        string cert_ip_val = "127.0.0.1";

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (matchIdent("port")) {
                    match(TOK_ASSIGN);
                    if (peek().kind == TOK_NUMBER) port_val = advance().num_val;
                    else if (peek().kind != TOK_SEMI) { try { port_val = stod(advance().text); } catch (...) {} }
                    match(TOK_SEMI);
                } else if (matchIdent("IP") || matchIdent("ip")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) ip_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("password")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) pass_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("certainIP") || matchIdent("certain_ip")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) cert_ip_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            match(TOK_RBRACE);
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(ip_val);
        emitByte(OP_CONST_NUM); emitDouble(port_val);
        emitByte(OP_CONST_STR); emitString(pass_val);
        emitByte(OP_CONST_STR); emitString(cert_ip_val);
        emitByte(OP_CALL_BUILTIN); emitString("create_serv"); emitByte(4);
        emitByte(OP_POP);
        return;
    }

    // Команда синхронизации async
    if (matchIdent("async")) {
        int as_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "async", as_line);
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            else { emitByte(OP_CONST_NUM); emitDouble(10.0); }
            match(TOK_RPAREN);
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(10.0);
        }
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return;
    }

    // 3. ПОТОКОВЫЙ ВЫВОД C++: cout << ... << endl;
    if (matchIdent("cout")) {
        while (match(TOK_SHL)) {
            if (matchIdent("endl")) {
                emitByte(OP_LOG_EMPTY);
            } else {
                parseExpression();
                emitByte(OP_PRINT_RAW);
            }
        }
        match(TOK_SEMI);
        return;
    }

    // Потоковый ввод C++: cin >> var;
    if (matchIdent("cin")) {
        while (match(TOK_SHR)) {
            if (peek().kind == TOK_IDENT) {
                string vname = advance().text;
                emitByte(OP_CALL_BUILTIN);
                emitString("input");
                emitByte(0);
                emitByte(OP_STORE_VAR);
                emitString(vname);
            }
        }
        match(TOK_SEMI);
        return;
    }

    // 4. ВЫВОД В КОНСОЛЬ
    if (matchIdent("log") || matchIdent("print") || matchIdent("println")) {
        match(TOK_ASSIGN);
        if (peek().kind == TOK_IDENT && peek().text == "text" &&
            (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_EOF)) {
            advance();
            match(TOK_SEMI);
            emitByte(OP_LOG_EMPTY);
            return;
        }

        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_RPAREN) {
                advance();
                match(TOK_SEMI);
                emitByte(OP_LOG_EMPTY);
                return;
            }
            parseExpression();
            while (match(TOK_COMMA)) {
                parseExpression();
                emitByte(OP_ADD);
            }
            match(TOK_RPAREN);
        } else {
            parseExpression();
        }
        match(TOK_SEMI);
        emitByte(OP_LOG);
        return;
    }

    // 5. ВВОД
    if (peek().kind == TOK_IDENT &&
        (peek().text == "input" || peek().text == "input.getline" || peek().text == "ask")) {
        size_t saved_cur = cur;
        string fn_name = advance().text;
        string builtin_target = (fn_name == "ask") ? "input.getline" : fn_name;

        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_IDENT && peek(1).kind == TOK_RPAREN && !user_functions.count(peek().text)) {
                string var_name = advance().text;
                match(TOK_RPAREN);
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(0);
                emitByte(OP_STORE_VAR);
                emitString(var_name);
                return;
            }
            parseExpression();
            if (match(TOK_COMMA) && peek().kind == TOK_IDENT) {
                string var_name = advance().text;
                match(TOK_RPAREN);
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(1);
                emitByte(OP_STORE_VAR);
                emitString(var_name);
                return;
            }
            if (match(TOK_RPAREN)) {
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN);
                emitString(builtin_target);
                emitByte(1);
                emitByte(OP_POP);
                return;
            }
        }
        cur = saved_cur;
    }

    // 6. ОБЪЯВЛЕНИЕ ПЕРЕМЕННЫХ: take, set, int, string, float, bool
    if (peek().kind == TOK_IDENT && (isVarKeyword(peek().text) || peek().text == "take")) {
        advance();
        if (match(TOK_ASSIGN) && peek().kind == TOK_IDENT &&
            (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_EOF)) {
            string var_name = advance().text;
            emitByte(OP_CONST_STR);
            emitString("");
            emitByte(OP_STORE_VAR);
            emitString(var_name);
            match(TOK_SEMI);
            return;
        }

        while (peek().kind == TOK_IDENT) {
            string var_name = advance().text;
            if (match(TOK_ASSIGN)) {
                parseExpression();
            } else {
                emitByte(OP_CONST_NUM);
                emitDouble(0.0);
            }
            emitByte(OP_STORE_VAR);
            emitString(var_name);
            if (!match(TOK_COMMA)) break;
        }
        match(TOK_SEMI);
        return;
    }

    // 7. УСЛОВИЯ: if, check, chek
    if (matchIdent("if") || matchIdent("check") || matchIdent("chek")) {
        if (match(TOK_LPAREN)) {
            parseExpression();
            match(TOK_RPAREN);
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
        return;
    }

    // 8. ЦИКЛ FOR
    if (matchIdent("for")) {
        match(TOK_LPAREN);
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
        match(TOK_RPAREN);

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        patchU32(body_jmp_patch, (uint32_t)code.size());
        break_Fixups.push_back({});
        parseBlock();

        emitByte(OP_JMP);
        emitU32((uint32_t)step_start);

        size_t loop_end = code.size();
        patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();
        return;
    }

    // 9. ЦИКЛ WHILE
    if (matchIdent("while")) {
        size_t loop_start = code.size();
        if (match(TOK_LPAREN)) {
            parseExpression();
            match(TOK_RPAREN);
        } else {
            parseExpression();
        }

        emitByte(OP_JMP_IF_FALSE);
        size_t exit_jmp_patch = code.size();
        emitU32(0);

        break_Fixups.push_back({});
        parseBlock();

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        size_t loop_end = code.size();
        patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();
        return;
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
            match(TOK_RPAREN);
            emitByte(OP_STORE_VAR);
            emitString(limit_var);

            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
            emitByte(OP_STORE_VAR);
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
        parseBlock();

        emitByte(OP_JMP);
        emitU32((uint32_t)loop_start);

        size_t loop_end = code.size();
        if (has_count) patchU32(exit_jmp_patch, (uint32_t)loop_end);
        for (size_t patch_pos : break_Fixups.back()) {
            patchU32(patch_pos, (uint32_t)loop_end);
        }
        break_Fixups.pop_back();
        return;
    }

    if (matchIdent("break") || matchIdent("stop") || matchIdent("stoped")) {
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_JMP);
        size_t patch_pos = code.size();
        emitU32(0);
        if (!break_Fixups.empty()) {
            break_Fixups.back().push_back(patch_pos);
        }
        return;
    }

    if (matchIdent("wait") || matchIdent("sleep")) {
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return;
    }

    if (matchIdent("exit")) {
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        emitByte(OP_HALT);
        return;
    }

    // 11. ПРИСВАИВАНИЯ И ИНКРЕМЕНТЫ
    if (peek().kind == TOK_IDENT) {
        if (peek(1).kind == TOK_ASSIGN) {
            string var_name = advance().text;
            advance();
            parseExpression();
            match(TOK_SEMI);
            emitByte(OP_STORE_VAR);
            emitString(var_name);
            return;
        }
        if (peek(1).kind == TOK_PLUS_ASSIGN || peek(1).kind == TOK_MINUS_ASSIGN ||
            peek(1).kind == TOK_MUL_ASSIGN  || peek(1).kind == TOK_DIV_ASSIGN) {
            string var_name = advance().text;
            GrassTokenKind op = advance().kind;
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
            return;
        }
        if (peek(1).kind == TOK_INC || peek(1).kind == TOK_DEC) {
            string var_name = advance().text;
            GrassTokenKind op = advance().kind;
            match(TOK_SEMI);
            emitByte(OP_LOAD_VAR); emitString(var_name);
            emitByte(OP_CONST_NUM); emitDouble(1.0);
            emitByte(op == TOK_INC ? OP_ADD : OP_SUB);
            emitByte(OP_STORE_VAR); emitString(var_name);
            return;
        }
    }

    size_t before = cur;
    parseExpression();
    match(TOK_SEMI);
    emitByte(OP_POP);
    if (cur == before && peek().kind != TOK_EOF) {
        advance();
    }
}

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
            emitByte(OP_CALL_USER);
            emitU32(user_functions["main"].entry_ip);
            emitByte(OP_POP);
        }
    }

    emitByte(OP_HALT);
}