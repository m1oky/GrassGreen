#include "frontend/GrassSyntax.h"
#include "baremetal/GrassKeyboard.h"
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
    return isalnum(c) || c == '_' || c >= 128;
}

Lexer::Lexer(const string& source, const string& fname) : src(source), filename(fname) {}

vector<GrassToken> Lexer::tokenize() {
    vector<GrassToken> tokens;
    pos = 0;
    line = 1;
    line_start_pos = 0;
    at_line_start = true;

    if (src.size() >= 3 &&
        (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF) {
        pos = 3;
        line_start_pos = 3;
    }

    auto get_col = [&](size_t p) -> int {
        return (int)(p >= line_start_pos ? (p - line_start_pos + 1) : 1);
    };

    while (pos < src.size()) {
        char c = src[pos];

        if (c == '\r') { pos++; continue; }
        if (c == '\n') {
            pos++;
            line++;
            line_start_pos = pos;
            at_line_start = true;
            continue;
        }
        if (c == ' ' || c == '\t') { pos++; continue; }

        int tok_line = line;
        int tok_col = get_col(pos);

        // Многострочный комментарий /* ... */
        if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < src.size() && !(src[pos] == '*' && src[pos + 1] == '/')) {
                if (src[pos] == '\n') { line++; line_start_pos = pos + 1; }
                pos++;
            }
            if (pos + 1 < src.size()) pos += 2;
            continue;
        }

        // Однострочный комментарий // (игнорируем, если перед ними двоеточие, как в http:// и https://)
        if (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/' && !(pos > 0 && src[pos - 1] == ':')) {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }

        // Директивы #include
        if (c == '#' && pos + 7 < src.size() && src.substr(pos, 8) == "#include") {
            pos += 8;
            tokens.push_back(GrassToken(TOK_IDENT, "include", 0.0, tok_line, tok_col));
            continue;
        }
        if (c == '@') {
            size_t start = pos;
            while (pos < src.size() && src[pos] != '\n') pos++;
            string dir = src.substr(start, pos - start);
            while (!dir.empty() && (dir.back() == '\r' || dir.back() == ' ' || dir.back() == '\t')) dir.pop_back();
            if (dir == "@strict") {
                tokens.push_back(GrassToken(TOK_IDENT, "@strict", 0.0, tok_line, tok_col));
            }
            continue;
        }

        // Hex-цвета (#00ffcc)
        if (c == '#' && pos + 1 < src.size() && isalnum((unsigned char)src[pos + 1])) {
            size_t start = pos++;
            while (pos < src.size() && isalnum((unsigned char)src[pos])) pos++;
            tokens.push_back(GrassToken(TOK_STRING, src.substr(start, pos - start), 0.0, tok_line, tok_col));
            at_line_start = false;
            continue;
        }

        // Библиотека !Name! или строка !!текст!!
        if (c == '!') {
            if (pos + 1 < src.size() && src[pos + 1] == '!') {
                pos += 2;
                string s;
                while (pos + 1 < src.size() && !(src[pos] == '!' && src[pos + 1] == '!')) {
                    if (src[pos] == '\n') { line++; line_start_pos = pos + 1; }
                    s += src[pos++];
                }
                if (pos + 1 < src.size()) pos += 2;
                tokens.push_back(GrassToken(TOK_STRING, s, 0.0, tok_line, tok_col));
                at_line_start = false;
                continue;
            }
            if (pos + 1 < src.size() && src[pos + 1] != '=') {
                size_t end_excl = src.find('!', pos + 1);
                size_t end_line = src.find('\n', pos);
                if (end_excl != string::npos && (end_line == string::npos || end_excl < end_line)) {
                    string lib = src.substr(pos, end_excl - pos + 1);
                    if (lib.find(' ') == string::npos && lib.find('(') == string::npos) {
                        tokens.push_back(GrassToken(TOK_LIB, lib, 0.0, tok_line, tok_col));
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
                } else if (src[pos] == '/' && pos + 1 < src.size() && src[pos + 1] == 'n' && (pos + 2 >= src.size() || !isalpha((unsigned char)src[pos + 2]))) {
                    s += '\n';
                    pos++;
                } else {
                    if (src[pos] == '\n') { line++; line_start_pos = pos + 1; }
                    s += src[pos];
                }
                pos++;
            }
            if (pos < src.size()) pos++;
            tokens.push_back(GrassToken(TOK_STRING, s, 0.0, tok_line, tok_col));
            continue;
        }

        // 0x... (шестнадцатеричные числа)
        if (c == '0' && pos + 1 < src.size() && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
            size_t start = pos;
            pos += 2;
            while (pos < src.size() && isxdigit((unsigned char)src[pos])) pos++;
            string hex_str = src.substr(start, pos - start);
            double val = 0.0;
            try { val = (double)std::stoull(hex_str, nullptr, 16); } catch (...) {}
            tokens.push_back(GrassToken(TOK_NUMBER, hex_str, val, tok_line, tok_col));
            continue;
        }
        // 0b... (двоичные числа)
        if (c == '0' && pos + 1 < src.size() && (src[pos + 1] == 'b' || src[pos + 1] == 'B')) {
            size_t start = pos;
            pos += 2;
            while (pos < src.size() && (src[pos] == '0' || src[pos] == '1')) pos++;
            string bin_str = src.substr(start, pos - start);
            double val = 0.0;
            try { val = (double)std::stoull(bin_str.substr(2), nullptr, 2); } catch (...) {}
            tokens.push_back(GrassToken(TOK_NUMBER, bin_str, val, tok_line, tok_col));
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
                tokens.push_back(GrassToken(TOK_STRING, num_str, 0.0, tok_line, tok_col));
            } else {
                double val = 0.0;
                try { val = stod(num_str); } catch (...) {}
                tokens.push_back(GrassToken(TOK_NUMBER, num_str, val, tok_line, tok_col));
            }
            continue;
        }

        // Дефисные UI-директивы (docs/interface.md)
        static const char* const kHyphenKeywords[] = {
            "header-buttons-offset", "header-buttons-pos", "header-text-color",
            "header-title-align", "auto-size-display", "border-radius-app",
            "placeholder-color", "header-buttons", "header-height",
            "header-color", "window-border", "active-color", "hover-color",
            "button-color", "click-switch", "click-synth", "click-sound",
            "hover-synth", "hover-sound", "on-click",
            "on-change", "scroll-box", "add-item", "header-align", "font-weight",
            "clock-format", "time-format", "center-all", "font-style",
            "text-color", "font-size", "window-ob", "align-all",
            "size-top", "click-ob", "z-index"
        };
        bool matched_hk = false;
        for (const char* kw : kHyphenKeywords) {
            size_t klen = strlen(kw);
            if (pos + klen <= src.size() && src.compare(pos, klen, kw) == 0) {
                if (pos + klen == src.size() || (!isalnum((unsigned char)src[pos + klen]) && src[pos + klen] != '_' && src[pos + klen] != '-')) {
                    tokens.push_back(GrassToken(TOK_IDENT, kw, 0.0, tok_line, tok_col));
                    pos += klen;
                    matched_hk = true;
                    break;
                }
            }
        }
        if (matched_hk) continue;

        // Идентификаторы
        if (isIdentStart((unsigned char)c)) {
            size_t start = pos;
            while (pos < src.size() && isIdentCont((unsigned char)src[pos])) {
                pos++;
            }
            string ident = src.substr(start, pos - start);
            if (ident == "namespace") {
                tokens.push_back(GrassToken(TOK_NAMESPACE, ident, 0.0, tok_line, tok_col));
            } else if (ident == "using") {
                tokens.push_back(GrassToken(TOK_USING, ident, 0.0, tok_line, tok_col));
            } else if (ident == "as") {
                tokens.push_back(GrassToken(TOK_AS, ident, 0.0, tok_line, tok_col));
            } else if (ident == "sizeof") {
                tokens.push_back(GrassToken(TOK_SIZEOF, ident, 0.0, tok_line, tok_col));
            } else if (ident == "nullptr" || ident == "null") {
                tokens.push_back(GrassToken(TOK_NULLPTR, ident, 0.0, tok_line, tok_col));
            } else {
                tokens.push_back(GrassToken(TOK_IDENT, ident, 0.0, tok_line, tok_col));
            }
            continue;
        }

        // Двухсимвольные операторы C++
        if (pos + 1 < src.size()) {
            string two = src.substr(pos, 2);
            if (two == "::") { tokens.push_back(GrassToken(TOK_COLON_COLON, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "->") { tokens.push_back(GrassToken(TOK_ARROW, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "==") { tokens.push_back(GrassToken(TOK_EQ, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "!=") { tokens.push_back(GrassToken(TOK_NEQ, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "<=") { tokens.push_back(GrassToken(TOK_LTE, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == ">=") { tokens.push_back(GrassToken(TOK_GTE, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "&&") { tokens.push_back(GrassToken(TOK_AND, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "||") { tokens.push_back(GrassToken(TOK_OR, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "++") { tokens.push_back(GrassToken(TOK_INC, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "--") { tokens.push_back(GrassToken(TOK_DEC, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "+=") { tokens.push_back(GrassToken(TOK_PLUS_ASSIGN, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "-=") { tokens.push_back(GrassToken(TOK_MINUS_ASSIGN, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "*=") { tokens.push_back(GrassToken(TOK_MUL_ASSIGN, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "/=") { tokens.push_back(GrassToken(TOK_DIV_ASSIGN, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == "<<") { tokens.push_back(GrassToken(TOK_SHL, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
            if (two == ">>") { tokens.push_back(GrassToken(TOK_SHR, two, 0.0, tok_line, tok_col)); pos += 2; continue; }
        }

        // Односимвольные операторы и скобки
        pos++;
        switch (c) {
            case '{': tokens.push_back(GrassToken(TOK_LBRACE, "{", 0.0, tok_line, tok_col)); at_line_start = true; break;
            case '}': tokens.push_back(GrassToken(TOK_RBRACE, "}", 0.0, tok_line, tok_col)); at_line_start = true; break;
            case '(': tokens.push_back(GrassToken(TOK_LPAREN, "(", 0.0, tok_line, tok_col)); break;
            case ')': tokens.push_back(GrassToken(TOK_RPAREN, ")", 0.0, tok_line, tok_col)); break;
            case '[': tokens.push_back(GrassToken(TOK_LBRACKET, "[", 0.0, tok_line, tok_col)); break;
            case ']': tokens.push_back(GrassToken(TOK_RBRACKET, "]", 0.0, tok_line, tok_col)); break;
            case ',': tokens.push_back(GrassToken(TOK_COMMA, ",", 0.0, tok_line, tok_col)); break;
            case ';': tokens.push_back(GrassToken(TOK_SEMI, ";", 0.0, tok_line, tok_col)); break;
            case ':': tokens.push_back(GrassToken(TOK_COLON, ":", 0.0, tok_line, tok_col)); break;
            case '=': tokens.push_back(GrassToken(TOK_ASSIGN, "=", 0.0, tok_line, tok_col)); break;
            case '+': tokens.push_back(GrassToken(TOK_PLUS, "+", 0.0, tok_line, tok_col)); break;
            case '-': tokens.push_back(GrassToken(TOK_MINUS, "-", 0.0, tok_line, tok_col)); break;
            case '*': tokens.push_back(GrassToken(TOK_STAR, "*", 0.0, tok_line, tok_col)); break;
            case '/': tokens.push_back(GrassToken(TOK_SLASH, "/", 0.0, tok_line, tok_col)); break;
            case '%': tokens.push_back(GrassToken(TOK_MOD, "%", 0.0, tok_line, tok_col)); break;
            case '!': tokens.push_back(GrassToken(TOK_NOT, "!", 0.0, tok_line, tok_col)); break;
            case '<': tokens.push_back(GrassToken(TOK_LT, "<", 0.0, tok_line, tok_col)); break;
            case '>': tokens.push_back(GrassToken(TOK_GT, ">", 0.0, tok_line, tok_col)); break;
            case '.': tokens.push_back(GrassToken(TOK_DOT, ".", 0.0, tok_line, tok_col)); break;
            case '&': tokens.push_back(GrassToken(TOK_AMP, "&", 0.0, tok_line, tok_col)); break;
            case '|': tokens.push_back(GrassToken(TOK_PIPE, "|", 0.0, tok_line, tok_col)); break;
            case '^': tokens.push_back(GrassToken(TOK_CARET, "^", 0.0, tok_line, tok_col)); break;
            case '~': tokens.push_back(GrassToken(TOK_TILDE, "~", 0.0, tok_line, tok_col)); break;
            default: break;
        }
    }
    tokens.push_back(GrassToken(TOK_EOF, "", 0.0, line, get_col(pos)));
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

bool BytecodeCompiler::consume(GrassTokenKind kind, const string& error_msg) {
    if (peek().kind == kind) {
        advance();
        return true;
    }
    reportError(peek().line, error_msg, peek().col);
    return false;
}

void BytecodeCompiler::reportError(int line, const string& msg, int col, const string& hint) {
    diagnostics.push_back(SyntaxDiagnostic(DiagnosticSeverity::Error,
        SourceLocation(source_filename, line, col), msg, hint));
    cerr << "[GrassSyntax Error] (" << source_filename;
    if (!source_filename.empty()) cerr << ":";
    cerr << line << ":" << col << "): " << msg;
    if (!hint.empty()) cerr << " (подсказка: " << hint << ")";
    cerr << endl;
    has_error = true;
}

void BytecodeCompiler::reportError(const SourceLocation& loc, const string& msg, const string& hint) {
    reportError(loc.line, msg, loc.col, hint);
}

void BytecodeCompiler::synchronize() {
    if (peek().kind != TOK_EOF) advance();
    while (peek().kind != TOK_EOF) {
        if (peek(-1).kind == TOK_SEMI) return;
        switch (peek().kind) {
            case TOK_RBRACE:
            case TOK_LBRACE:
            case TOK_SEMI:
                return;
            case TOK_IDENT: {
                const string& txt = peek().text;
                if (isTypeOrFuncKeyword(txt) || isVarKeyword(txt) ||
                    txt == "struct" || txt == "if" || txt == "check" ||
                    txt == "while" || txt == "for" || txt == "again" ||
                    txt == "loop" || txt == "return" || txt == "break" ||
                    txt == "continue" || txt == "gout" || txt == "cout" ||
                    txt == "gin" || txt == "cin" || txt == "clear" ||
                    txt == "window" || txt == "surface" || txt == "tab") {
                    return;
                }
                break;
            }
            case TOK_LIB:
                return;
            default:
                break;
        }
        advance();
    }
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
    else if (fn == "qb_init" || fn == "h" || fn == "x" || fn == "z" || fn == "cnot" ||
             fn == "q_rand" || fn == "q_measure" || fn == "pi" || fn == "gcd" || fn == "pow_e" ||
             fn == "sha256" || fn == "pow_mod" || fn == "crypto_rand") {
        requireLib("coremath", "!CoreMath!", fn + "()", line);
    }
    // 2.1. Проверка !math3d!
    else if (fn.rfind("v3_", 0) == 0 || fn.rfind("m4_", 0) == 0) {
        requireLib("math3d", "!math3d!", fn + "()", line);
    }
    // 2.2. Проверка !gfx3d!
    else if (fn.rfind("g3d_", 0) == 0) {
        requireLib("gfx3d", "!gfx3d!", fn + "()", line);
    }
    // 2.3. Проверка !physics!
    else if (fn.rfind("phys_", 0) == 0) {
        requireLib("physics", "!physics!", fn + "()", line);
    }
    // 3. Проверка !memory!
    else if ((fn.find("memory_") == 0 && fn != "memory_barrier") || fn == "pack_u16" || fn == "pack_u32" || fn == "pack_u64") {
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
    else if (fn.find("gui_") == 0 || fn == "surface" || fn == "auto_size_display" ||
             fn == "border_radius_app" || fn == "border-radius-app" ||
             fn == "switch_tab" || fn == "switch" ||
             fn == "fullscreen" || fn == "fullscrean" ||
             fn == "set_text" || fn == "get_text" || fn == "set_val" || fn == "get_val" ||
             fn == "set_color" || fn == "set_bg" || fn == "set_visible" ||
             fn == "hide" || fn == "show" || fn == "is_checked" ||
             fn == "add_item" || fn == "clear_list" || fn == "get_selected" ||
             fn == "on_timer") {
        requireLib("gui", "!interface!", fn + "()", line);
    }
    else if (fn == "bind_synth" || fn == "bind_sound") {
        if (!included_libs.count("gui") && !included_libs.count("audio")) {
            reportError(line, "Ошибка: вызов '" + fn + "()' требует подключения библиотеки !interface! или !audio!.");
        }
    }
    else if (fn == "attach_sound") {
        if (!included_libs.count("audio") && !included_libs.count("gui")) {
            reportError(line, "Ошибка: вызов 'attach_sound()' требует подключения библиотеки !audio!.");
        }
    }
    // 7. Проверка библиотеки !time!
    else if (fn == "start_time" || fn == "stop_time" || fn == "conclus_time" ||
             fn == "time" || fn == "clock_ms" || fn == "now_ms" || fn == "sleep_ms" ||
             fn == "time_now_ms" || fn == "time_now_us" || fn == "time_frequency" ||
             fn == "time_counter" || fn == "time_sleep" || fn == "time_delay_ms" ||
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
    else if (fn == "ram_total_mb" || fn == "ram_used_mb" || fn == "ram_free_mb") {
        if (!included_libs.count("sys") && !included_libs.count("hardware")) {
            requireLib("hardware", "!hardware! или !sys!", fn + "()", line);
        }
    }
    else if (fn == "clip_set" || fn == "clip_get" || fn == "screen_w" ||
             fn == "screen_h" || fn == "pc_name" || fn == "user_name" ||
             fn == "cpu_cores" ||
             fn == "get_env" || fn == "uptime_sec" || fn == "cli_args" ||
             fn == "system" || fn == "exec" || fn == "shell") {
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
             fn == "fs_copy"  || fn == "fs_list" || fn == "fs_read" || fn == "fs_write" ||
             fn == "fs_read_bytes" || fn == "fs_write_bytes" ||
             fn == "fs_exists" || fn == "fs_remove") {
        requireLib("fsys", "!fsys!", fn + "()", line);
    }
    // 12.1. Проверка библиотеки !ffi!
    else if (fn == "ffi_open" || fn == "ffi_sym" || fn == "ffi_call" || fn == "ffi_close") {
        requireLib("ffi", "!ffi!", fn + "()", line);
    }
    // 12.2. Проверка библиотеки !langgen!
    else if (fn == "lang_new" || fn == "lang_comment" || fn == "lang_rule" || fn == "lang_parse" ||
             fn == "lang_count" || fn == "lang_cmd" || fn == "lang_arg" || fn == "lang_eval" ||
             fn == "lang_set_var" || fn == "lang_get_var") {
        requireLib("langgen", "!langgen!", fn + "()", line);
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
    else if (fn.rfind("g2d_", 0) == 0 ||
             fn == "gfx_init" || fn == "gfx_running" || fn == "gfx_clear" ||
             fn == "gfx_rect" || fn == "gfx_circle"  || fn == "gfx_line" ||
             fn == "gfx_text" || fn == "gfx_key"     || fn == "gfx_mouse_x" ||
             fn == "gfx_mouse_y" || fn == "gfx_mouse_down" || fn == "gfx_collide" ||
             fn == "gfx_frame" || fn == "gfx_close") {
        requireLib("2DGraphics", "!2DGraphics!", fn + "()", line);
    }
    // 15. Проверка библиотеки !thread!
    else if (fn == "thread_join" || fn == "thread_detach" || fn == "thread_id" ||
             fn == "thread_sleep" || fn == "mutex_create" || fn == "mutex_lock" ||
             fn == "mutex_unlock" || fn == "mutex_destroy") {
        requireLib("thread", "!thread!", fn + "()", line);
    }
    // 16. Проверка библиотеки !mmio! / !io!
    else if (fn == "mmio_read8" || fn == "mmio_read16" || fn == "mmio_read32" || fn == "mmio_read64" ||
             fn == "mmio_write8" || fn == "mmio_write16" || fn == "mmio_write32" || fn == "mmio_write64" ||
             fn == "io_in8" || fn == "io_in16" || fn == "io_in32" ||
             fn == "inb" || fn == "inw" || fn == "inl" ||
             fn == "io_out8" || fn == "io_out16" || fn == "io_out32" ||
             fn == "outb" || fn == "outw" || fn == "outl" ||
             fn == "memory_barrier" || fn == "read_barrier" || fn == "write_barrier") {
        requireLib("mmio", "!mmio!", fn + "()", line);
    }
    // 17. Проверка библиотеки !audio!
    else if (fn == "tone" || fn == "beep" || fn == "synth_play" || fn == "synth" ||
             fn == "sound_play" || fn == "sound_stop" || fn == "sound_pause" ||
             fn == "sound_resume" || fn == "sound_volume" || fn == "sound_is_playing") {
        requireLib("audio", "!audio!", fn + "()", line);
    }
    // 18. Проверка библиотеки !asm!
    else if (fn == "cli" || fn == "sti" || fn == "hlt" ||
             fn == "in" || fn == "out" ||
             fn == "in8" || fn == "in16" || fn == "in32" ||
             fn == "out8" || fn == "out16" || fn == "out32" ||
             fn == "read_cr0" || fn == "write_cr0" ||
             fn == "read_cr2" || fn == "write_cr2" ||
             fn == "read_cr3" || fn == "write_cr3" ||
             fn == "read_cr4" || fn == "write_cr4" ||
             fn == "read_rflags" || fn == "write_rflags" ||
             fn == "invlpg" || fn == "rdtsc" || fn == "pause" || fn == "nop" ||
             fn == "cpuid_eax" || fn == "cpuid_ebx" || fn == "cpuid_ecx" || fn == "cpuid_edx" ||
             fn == "asm_run" || fn == "asm_bytes" || fn == "asm_hex" || fn == "asm_machine_hex") {
        requireLib("asm", "!asm!", fn + "()", line);
    }
    // 19. Проверка библиотеки !keyboard!
    else if (fn == "kbd_has_key" || fn == "kbd_available" ||
             fn == "kbd_read_scancode" || fn == "kbd_get_scancode" ||
             fn == "kbd_scancode_to_char" || fn == "kbd_scancode_to_ascii" ||
             fn == "kbd_getc" || fn == "kbd_poll" || fn == "kbd_read_line" ||
             fn == "kbd_is_shift" || fn == "kbd_is_caps" ||
             fn == "kbd_is_ctrl" || fn == "kbd_is_alt" ||
             fn == "kbd_is_key_pressed" || fn == "kbd_scancode_name" ||
             fn == "kbd_leds" || fn == "kbd_reset" || fn == "kbd_self_test" ||
             fn == "kbd_wait_ack" || fn == "kbd_set_layout" || fn == "kbd_get_layout") {
        requireLib("keyboard", "!keyboard!", fn + "()", line);
    }
    // 20. Проверка библиотеки !hardware!
    else if (fn.rfind("hw_", 0) == 0 ||
             fn == "cpu_vendor" || fn == "cpu_brand" || fn == "cpu_model" ||
             fn == "cpu_cores" || fn == "cpu_family" || fn == "cpu_freq" ||
             fn == "ram_total" || fn == "ram_used" || fn == "ram_free" || fn == "proc_mem" ||
             fn == "board_vendor" || fn == "board_model" ||
             fn == "bios_vendor" || fn == "bios_date" ||
             fn == "pci_count" || fn == "rdtsc") {
        requireLib("hardware", "!hardware!", fn + "()", line);
    }
    // 21. Проверка библиотеки !baremetal!
    else if (fn == "clear" || fn == "cls" || fn == "not_clear" || fn == "not_clear_on" || fn == "not_clear_off" ||
             fn == "clearable" || fn == "color" || fn == "read_key" ||
             fn == "draw_box" || fn == "menu" || fn == "progress_bar" ||
             fn == "reboot" || fn == "poweroff" ||
             fn == "ls" || fn == "pwd" || fn == "cd" || fn == "copy" || fn == "move" ||
             fn == "delete" || fn == "cr_folder" || fn == "cr_file" ||
             fn == "not_show" || fn == "specSign_show" ||
             fn == "virtual_struct" || fn == "physical_struct" ||
             fn == "bm_exec" || fn == "cat" || fn == "read_file" ||
             fn == "gui" || fn == "vd" || fn == "vd_desktop" ||
             fn == "mouse_init" || fn == "mouse_x" || fn == "mouse_y" ||
             fn == "mouse_btn" || fn == "mouse_down" || fn == "mouse_poll" || fn == "mouse_cursor") {
        requireLib("baremetal", "!baremetal!", fn + "()", line);
    }
}

bool BytecodeCompiler::isTypeOrFuncKeyword(const string& s) {
    return s == "fun" || s == "take" || s == "void" || s == "func" || s == "alt" || s == "fn" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
}

bool BytecodeCompiler::isVarKeyword(const string& s) {
    return s == "set" || s == "var" || s == "let" || s == "take" || s == "fany" || s == "invent" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile" || s == "void" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
}

void BytecodeCompiler::emitByte(uint8_t b) { code.push_back(b); }

void BytecodeCompiler::pushVMScope() {
    vm_scopes_.emplace_back();
}

void BytecodeCompiler::registerVMVarType(const std::string& var_name, const std::string& struct_name) {
    if (vm_scopes_.empty()) pushVMScope();
    auto& frame = vm_scopes_.back();
    bool already_tracked = false;
    for (const auto& d : frame.declared_var_types) {
        if (d == var_name) { already_tracked = true; break; }
    }
    if (!already_tracked) {
        for (const auto& s : frame.shadowed_var_types) {
            if (s.first == var_name) { already_tracked = true; break; }
        }
    }
    if (!already_tracked) {
        auto it = var_struct_types.find(var_name);
        if (it != var_struct_types.end()) {
            frame.shadowed_var_types.push_back({ var_name, it->second });
        } else {
            frame.declared_var_types.push_back(var_name);
        }
    }
    if (!struct_name.empty()) {
        var_struct_types[var_name] = struct_name;
    } else {
        var_struct_types.erase(var_name);
    }
}

void BytecodeCompiler::registerVMRAIIVar(const std::string& var_name, const std::string& struct_name) {
    if (struct_name.empty()) return;
    auto it = g_struct_registry.find(struct_name);
    if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    if (vm_scopes_.empty()) pushVMScope();
    vm_scopes_.back().raii_vars.push_back({ var_name, struct_name });
}

void BytecodeCompiler::emitVMDtorCall(const std::string& var_name, const std::string& struct_name) {
    if (struct_name.empty()) return;
    auto it = g_struct_registry.find(struct_name);
    if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    emitByte(OP_LOAD_VAR); emitString(var_name);
    std::string target_fn = struct_name + "__dtor";
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

void BytecodeCompiler::emitVMScopesCleanupTo(size_t target_depth) {
    for (size_t idx = vm_scopes_.size(); idx > target_depth; --idx) {
        const auto& vars = vm_scopes_[idx - 1].raii_vars;
        for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
            emitVMDtorCall(it->name, it->struct_type);
        }
    }
}

void BytecodeCompiler::emitVMFieldDtors(const std::string& struct_name) {
    if (struct_name.empty()) return;
    emitByte(OP_LOAD_VAR); emitString("this");
    std::string target_fn = struct_name + "__dtor_fields";
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

void BytecodeCompiler::popVMScope(bool emit_cleanups) {
    if (vm_scopes_.empty()) return;
    if (emit_cleanups) {
        const auto& vars = vm_scopes_.back().raii_vars;
        for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
            emitVMDtorCall(it->name, it->struct_type);
        }
    }
    for (const auto& d : vm_scopes_.back().declared_var_types) {
        var_struct_types.erase(d);
    }
    for (const auto& s : vm_scopes_.back().shadowed_var_types) {
        var_struct_types[s.first] = s.second;
    }
    vm_scopes_.pop_back();
}

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

std::string BytecodeCompiler::resolveUserFunc(const std::string& name) const {
    if (user_functions.count(name)) return name;
    std::string mangled = name;
    for (size_t p = 0; (p = mangled.find("::", p)) != std::string::npos; ) {
        mangled.replace(p, 2, "__");
    }
    if (user_functions.count(mangled)) return mangled;
    if (using_symbols_.count(name)) {
        std::string sym = using_symbols_.at(name);
        if (user_functions.count(sym)) return sym;
        std::string sym_m = sym;
        for (size_t p = 0; (p = sym_m.find("::", p)) != std::string::npos; ) {
            sym_m.replace(p, 2, "__");
        }
        if (user_functions.count(sym_m)) return sym_m;
    }
    for (const auto& ns : using_namespaces_) {
        std::string q = ns + "::" + name;
        if (user_functions.count(q)) return q;
        std::string qm = q;
        for (size_t p = 0; (p = qm.find("::", p)) != std::string::npos; ) {
            qm.replace(p, 2, "__");
        }
        if (user_functions.count(qm)) return qm;
    }
    return "";
}

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

        // Индексация массива: myArray[1]
        if (match(TOK_LBRACKET)) {
            parseExpression();
            match(TOK_RBRACKET);
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_GET_INDEX);
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
            if ((member == "push" || member == "pop") && match(TOK_LPAREN)) {
                if (member == "push") {
                    parseExpression();
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_ARRAY_PUSH);
                    return;
                } else {
                    consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_ARRAY_POP);
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
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
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

    // Реактивный таймер: every(1000) { ... } / on_timer(ms) { ... }
    if (cmd == "every" || cmd == "on_timer" || cmd == "timer") {
        advance();
        string ms = "1000";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_NUMBER) {
                ms = to_string((int)advance().num_val);
            } else if (peek().kind == TOK_IDENT) {
                ms = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' после интервала таймера every(...)!");
        } else if (peek().kind == TOK_NUMBER) {
            ms = to_string((int)advance().num_val);
        }

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для таймера every(" + ms + ")!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' таймера every!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("every(" + ms + ", " + to_string(callback_ip) + ")");
        return true;
    }

    // Обработчик события клика: on_click { ... } / on-click { ... }
    if (cmd == "on_click" || cmd == "on-click") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для обработчика on_click!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' обработчика on_click!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("on_click_callback(" + to_string(callback_ip) + ")");
        return true;
    }

    // Обработчик изменения ввода/ползунка: on_change { ... } / on-change { ... }
    if (cmd == "on_change" || cmd == "on-change") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для обработчика on_change!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' обработчика on_change!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("on_change_callback(" + to_string(callback_ip) + ")");
        return true;
    }

    // Элементы интерфейса: button, label, enter, icon, video, svg, clock, progress, slider, checkbox, list
    if (cmd == "button" || cmd == "label" || cmd == "enter" || cmd == "icon" || cmd == "video" || cmd == "svg" || cmd == "clock" ||
        cmd == "progress" || cmd == "slider" || cmd == "checkbox" || cmd == "list" || cmd == "scroll-box") {
        int el_line = peek().line;
        advance();
        string inline_id = "";
        if (peek().kind == TOK_IDENT && peek(1).kind == TOK_ASSIGN) {
            inline_id = advance().text;
        }
        match(TOK_ASSIGN);

        string txt = "";
        if (cmd == "clock" && peek().kind == TOK_LBRACE) {
            txt = "HH:mm:ss";
        } else if (cmd == "icon" || cmd == "video" || cmd == "svg") {
            if (match(TOK_LPAREN)) {
                if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) txt = advance().text;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                txt = advance().text;
            }
        } else if (match(TOK_LPAREN)) {
            vector<GrassToken> expr_tokens;
            int paren_depth = 1;
            while (paren_depth > 0 && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LPAREN) paren_depth++;
                else if (peek().kind == TOK_RPAREN) {
                    paren_depth--;
                    if (paren_depth == 0) {
                        advance();
                        break;
                    }
                }
                expr_tokens.push_back(advance());
            }
            if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_STRING) {
                string s = expr_tokens[0].text;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                txt = s;
            } else if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_IDENT) {
                txt = "{" + expr_tokens[0].text + "}";
            } else {
                for (size_t ti = 0; ti < expr_tokens.size(); ++ti) {
                    const auto& tk = expr_tokens[ti];
                    if (tk.kind == TOK_STRING) {
                        string s = tk.text;
                        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                        txt += s;
                    } else if (tk.kind == TOK_IDENT) {
                        if (tk.text == "to_str" || tk.text == "to_string" || tk.text == "to_num") {
                            continue;
                        }
                        if (ti + 2 < expr_tokens.size() && expr_tokens[ti+1].kind == TOK_LPAREN && expr_tokens[ti+2].kind == TOK_RPAREN) {
                            txt += "{" + tk.text + "}";
                            ti += 2;
                        } else {
                            txt += "{" + tk.text + "}";
                        }
                    } else if (tk.kind == TOK_NUMBER) {
                        txt += tk.text;
                    }
                }
            }
        } else {
            vector<GrassToken> expr_tokens;
            while (peek().kind != TOK_LBRACE && peek().kind != TOK_SEMI && peek().kind != TOK_EOF) {
                expr_tokens.push_back(advance());
            }
            if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_STRING) {
                string s = expr_tokens[0].text;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                txt = s;
            } else if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_IDENT) {
                txt = "{" + expr_tokens[0].text + "}";
            } else {
                for (size_t ti = 0; ti < expr_tokens.size(); ++ti) {
                    const auto& tk = expr_tokens[ti];
                    if (tk.kind == TOK_STRING) {
                        string s = tk.text;
                        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                        txt += s;
                    } else if (tk.kind == TOK_IDENT) {
                        if (tk.text == "to_str" || tk.text == "to_string" || tk.text == "to_num") {
                            continue;
                        }
                        if (ti + 2 < expr_tokens.size() && expr_tokens[ti+1].kind == TOK_LPAREN && expr_tokens[ti+2].kind == TOK_RPAREN) {
                            txt += "{" + tk.text + "}";
                            ti += 2;
                        } else {
                            txt += "{" + tk.text + "}";
                        }
                    } else if (tk.kind == TOK_NUMBER) {
                        txt += tk.text;
                    }
                }
            }
        }

        if (cmd == "icon" || cmd == "video" || cmd == "svg") {
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
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
        match(TOK_SEMI);
        return true;
    }

    // ── Система вкладок (Tabs System): tab = "main" { ... }, tab "profile" { ... }, tab("name") ──
    if (cmd == "tab") {
        advance();
        if (match(TOK_ASSIGN)) {
            // съедаем '=' если написано tab = ...
        }
        string tab_name = "main";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                tab_name = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
            tab_name = advance().text;
        }

        emitUICmd("tab(" + tab_name + ")");

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parseSingleGuiDirective()) advance();
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
            match(TOK_SEMI);
            emitUICmd("tab(__global__)");
            return true;
        }
        match(TOK_SEMI);
        return true;
    }

    if (cmd == "middle" || cmd == "center" || cmd == "left" || cmd == "right" ||
        cmd == "shadow" || cmd == "glass" || cmd == "clear" || cmd == "topmost" || cmd == "clock") {
        advance();
        string norm_cmd = (cmd == "center") ? "middle" : cmd;
        string arg = "";
        if (match(TOK_LPAREN)) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) arg += advance().text;
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        if (!arg.empty()) emitUICmd(norm_cmd + "(" + arg + ")");
        else emitUICmd(norm_cmd);
        return true;
    }

    if (cmd == "surface" || cmd == "auto_size_display" || cmd == "auto-size-display" ||
        cmd == "border-radius-app" || cmd == "border_radius_app" ||
        cmd == "topmost" || cmd == "z-index" || cmd == "z_index" || cmd == "layer" ||
        cmd == "clock" || cmd == "clock-format" || cmd == "clock_format" || cmd == "time-format" || cmd == "time_format" ||
        cmd == "header" || cmd == "header-color" || cmd == "header_color" ||
        cmd == "header-text-color" || cmd == "header_text_color" ||
        cmd == "header-height" || cmd == "header_height" ||
        cmd == "header-buttons" || cmd == "header_buttons" ||
        cmd == "header-buttons-pos" || cmd == "header_buttons_pos" ||
        cmd == "header-buttons-offset" || cmd == "header_buttons_offset" ||
        cmd == "header-title-align" || cmd == "header_title_align" ||
        cmd == "header-align" || cmd == "header_align" ||
        cmd == "window-border" || cmd == "window_border" ||
        cmd == "fullscreen" || cmd == "fullscrean" ||
        cmd == "layout" || cmd == "align-all" || cmd == "align_all" ||
        cmd == "center-all" || cmd == "center_all" ||
        cmd == "engine" || cmd == "vd_plus" || cmd == "vdplus" ||
        cmd == "particles" || cmd == "animate" ||
        cmd == "gradient" || cmd == "glow" || cmd == "border" || cmd == "placeholder" ||
        cmd == "placeholder-color" || cmd == "placeholder_color" ||
        cmd == "hover-color" || cmd == "hover_color" || cmd == "active-color" || cmd == "active_color" ||
        cmd == "title" || cmd == "name" || cmd == "size" || cmd == "size-top" || cmd == "height" ||
        cmd == "color" || cmd == "bg" || cmd == "background" || cmd == "theme" || cmd == "fon" ||
        cmd == "iconP" || cmd == "id" || cmd == "name_el" || cmd == "window-ob" || cmd == "pos" ||
        cmd == "position" || cmd == "align" || cmd == "text-color" || cmd == "text_color" ||
        cmd == "button-color" || cmd == "button_color" || cmd == "font-size" || cmd == "font_size" ||
        cmd == "font-style" || cmd == "font_style" || cmd == "font-weight" || cmd == "font_weight" ||
        cmd == "radius" || cmd == "pad" ||
        cmd == "click-sound" || cmd == "click_sound" || cmd == "click-synth" || cmd == "click_synth" ||
        cmd == "hover-synth" || cmd == "hover_synth" || cmd == "hover-sound" || cmd == "hover_sound" ||
        cmd == "tooltip" || cmd == "cursor" || cmd == "disabled" ||
        cmd == "val" || cmd == "value" || cmd == "percent" || cmd == "item" || cmd == "add_item" ||
        cmd == "click-ob" || cmd == "click-switch" || cmd == "click_switch" ||
        cmd == "switch" || cmd == "switch_tab" || cmd == "run" || cmd == "action") {
        advance();
        string arg = extractParenArgsAsString();

        if (cmd == "auto-size-display") cmd = "auto_size_display";
        else if (cmd == "border_radius_app") cmd = "border-radius-app";
        else if (cmd == "click_sound") cmd = "click-sound";
        else if (cmd == "click_synth") cmd = "click-synth";
        else if (cmd == "hover_synth") cmd = "hover-synth";
        else if (cmd == "hover_sound") cmd = "hover-sound";
        else if (cmd == "value" || cmd == "percent") cmd = "val";
        else if (cmd == "add_item") cmd = "item";
        else if (cmd == "fullscrean") cmd = "fullscreen";
        else if (cmd == "z_index" || cmd == "layer") cmd = "z-index";
        else if (cmd == "clock_format" || cmd == "time_format" || cmd == "time-format") cmd = "clock-format";
        else if (cmd == "align_all" || cmd == "align-all" || cmd == "center-all" || cmd == "center_all") cmd = "layout";
        else if (cmd == "click_switch") cmd = "click-switch";
        else if (cmd == "switch_tab") cmd = "switch";
        else if (cmd == "header_buttons_pos") cmd = "header-buttons-pos";
        else if (cmd == "header_buttons_offset") cmd = "header-buttons-offset";
        else if (cmd == "header_title_align" || cmd == "header_align" || cmd == "header-align") cmd = "header-title-align";
        else if (cmd == "height") cmd = "size-top";
        else if (cmd == "bg" || cmd == "background") cmd = "color";
        else if (cmd == "pos" || cmd == "position") cmd = "window-ob";
        else if (cmd == "text_color") cmd = "text-color";
        else if (cmd == "button_color") cmd = "button-color";
        else if (cmd == "hover_color") cmd = "hover-color";
        else if (cmd == "active_color") cmd = "active-color";
        else if (cmd == "header_color") cmd = "header-color";
        else if (cmd == "header_text_color") cmd = "header-text-color";
        else if (cmd == "header_height") cmd = "header-height";
        else if (cmd == "header_buttons") cmd = "header-buttons";
        else if (cmd == "window_border") cmd = "window-border";
        else if (cmd == "placeholder_color") cmd = "placeholder-color";
        else if (cmd == "font_size") cmd = "font-size";
        else if (cmd == "font_style") cmd = "font-style";
        else if (cmd == "font_weight") cmd = "font-weight";
        else if (cmd == "action") cmd = "run";

        if (cmd == "align") {
            if (arg == "center" || arg == "middle") emitUICmd("middle");
            else if (arg == "left") emitUICmd("left");
            else if (arg == "right") emitUICmd("right");
        } else if (cmd == "title" || cmd == "name") {
            emitUICmd(cmd + "(!!" + arg + "!!)");
        } else if (cmd == "header") {
            emitUICmd("header(" + arg + ")");
            if (match(TOK_LBRACE)) {
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_IDENT) {
                        string hsub = peek().text;
                        if (hsub == "title" || hsub == "name") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-title(!!" + harg + "!!)");
                            continue;
                        } else if (hsub == "color" || hsub == "bg" || hsub == "background") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-color(" + harg + ")");
                            continue;
                        } else if (hsub == "text-color" || hsub == "text_color") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-text-color(" + harg + ")");
                            continue;
                        } else if (hsub == "height") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-height(" + harg + ")");
                            continue;
                        }
                    }
                    if (!parseSingleGuiDirective()) advance();
                }
                consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' блока 'header'!");
                match(TOK_SEMI);
            }
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
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
    }

    if (!match(TOK_LBRACE)) {
        reportError(win_line, "После ключевого слова 'window' обязателен блок в фигурных скобках '{ ... }'!");
        return;
    }

    emitByte(OP_UI_INIT);
    emitString("GrassGreen UI");
    emitU32(640);
    emitU32(480);
    emitString("#09090b");
    emitString("");

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (!parseSingleGuiDirective()) {
            parseStatement();
        }
    }
    consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' блока 'window'!");
    match(TOK_SEMI);
    emitByte(OP_UI_SHOW);
}

void BytecodeCompiler::parseBlock() {
    if (match(TOK_LBRACE)) {
        pushVMScope();
        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            size_t before = cur;
            parseStatement();
            if (cur == before && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
                reportError(peek().line, "Синтаксическая ошибка: непредвиденная инструкция '" + peek().text + "'!");
                synchronize();
            }
        }
        popVMScope(true);
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
    } else {
        reportError(peek().line, "Ожидалась открывающая фигурная скобка '{' для блока кода!");
        pushVMScope();
        parseStatement();
        popVMScope(true);
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
    if (peek().kind == TOK_LBRACE) {
        parseBlock();
        return;
    }

    // 0. РЕГИСТРАЦИЯ ПОДКЛЮЧЕННЫХ БИБЛИОТЕК (!GrassGreen!, !interface!, !database! и т.д.)
    if (peek().kind == TOK_LIB) {
        string lib = advance().text;
        if (lib == "!GrassGreen!") {
            included_libs.insert("core");
            included_libs.insert("time");
            included_libs.insert("fastmath");
            included_libs.insert("coremath");
            included_libs.insert("fsys");
            included_libs.insert("memory");
            included_libs.insert("string");
            included_libs.insert("sys");
            included_libs.insert("net");
            included_libs.insert("thread");
            included_libs.insert("algo");
            included_libs.insert("io");
            included_libs.insert("mmio");
        } else if (lib == "!sys!" || lib == "!os!") {
            included_libs.insert("sys");
        } else if (lib == "!json!") {
            included_libs.insert("json");
        } else if (lib == "!tg!") {
            included_libs.insert("tg");
        } else if (lib == "!2DGraphics!" || lib == "!2dgraphics!") {
            included_libs.insert("2DGraphics");
        } else if (lib == "!math3d!" || lib == "!Math3D!") {
            included_libs.insert("math3d");
        } else if (lib == "!gfx3d!" || lib == "!Gfx3D!" || lib == "!3d!" || lib == "!3D!") {
            included_libs.insert("gfx3d");
        } else if (lib == "!physics!" || lib == "!Physics!") {
            included_libs.insert("physics");
        } else if (lib == "!proc!") {
            included_libs.insert("proc");
        } else if (lib == "!fsys!" || lib == "!fs!") {
            included_libs.insert("fsys");
        } else if (lib == "!GrassNet!" || lib == "!net!") {
            included_libs.insert("net");
        } else if (lib == "!interface!" || lib == "!gui!" || lib == "!GrassGUI!") {
            included_libs.insert("gui");
        } else if (lib == "!time!") {
            included_libs.insert("time");
        } else if (lib == "!FastMath!" || lib == "!math!") {
            included_libs.insert("fastmath");
        } else if (lib == "!CoreMath!") {
            included_libs.insert("coremath");
        } else if (lib == "!memory!" || lib == "!mem!") {
            included_libs.insert("memory");
        } else if (lib == "!string!" || lib == "!str!") {
            included_libs.insert("string");
        } else if (lib == "!algo!") {
            included_libs.insert("algo");
        } else if (lib == "!random!") {
            included_libs.insert("random");
        } else if (lib == "!thread!") {
            included_libs.insert("thread");
        } else if (lib == "!hardware!" || lib == "!hw!" || lib == "!sysinfo!" || lib == "!device!") {
            included_libs.insert("hardware");
        } else if (lib == "!baremetal!" || lib == "!bm!" || lib == "!kernel!" || lib == "!BareMetal!") {
            included_libs.insert("baremetal");
        } else if (lib == "!vd+!" || lib == "!VD+!" || lib == "!vd!" || lib == "!VD!" || lib == "!gui!") {
            included_libs.insert("vd");
            included_libs.insert("baremetal");
        } else if (lib == "!mmio!" || lib == "!io!") {
            included_libs.insert("mmio");
            included_libs.insert("io");
        } else if (lib == "!database!" || lib == "!gdb!") {
            included_libs.insert("db");
            if (!included_libs.count("core")) is_special_file = true;
            emitByte(OP_LOAD_DB);
        } else if (lib == "!audio!" || lib == "!sound!" || lib == "!GrassAudio!") {
            included_libs.insert("audio");
            emitByte(OP_CALL_BUILTIN); emitString("audio_init"); emitByte(0); emitByte(OP_POP);
        } else if (lib == "!asm!" || lib == "!GrassASM!" || lib == "!assembler!") {
            included_libs.insert("asm");
        } else if (lib == "!keyboard!" || lib == "!kbd!" || lib == "!ps2!" || lib == "!PS2Keyboard!") {
            included_libs.insert("keyboard");
        } else if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
            included_libs.insert("web");
            is_special_file = true;
            emitByte(OP_START_WEB);
            emitString(source_filename);
            emitByte(OP_HALT);
        } else if (lib == "!ffi!" || lib == "!FFI!") {
            included_libs.insert("ffi");
        } else if (lib == "!langgen!" || lib == "!LANGGEN!" || lib == "!lang!") {
            included_libs.insert("langgen");
        } else {
            reportError(peek(-1).line, "Неизвестная библиотека: " + lib);
        }
        return;
    }

    // 0.1 ДИРЕКТИВА / ИНСТРУКЦИЯ: use utf - <id>; (или use utf - "ru"; use utf-8;)
    if (peek().kind == TOK_IDENT && peek().text == "use") {
        if (peek(1).kind == TOK_IDENT && (peek(1).text == "utf" || peek(1).text == "UTF" || peek(1).text.rfind("utf-", 0) == 0 || peek(1).text.rfind("UTF-", 0) == 0)) {
            advance(); // consume 'use'
            string utf_tok = advance().text;
            string inline_suffix = "";
            if (utf_tok.size() > 4 && (utf_tok[3] == '-' || utf_tok[3] == '_')) {
                inline_suffix = utf_tok.substr(4);
            }
            if (!inline_suffix.empty()) {
                match(TOK_SEMI);
                emitByte(OP_CONST_STR);
                emitString(inline_suffix);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout_by_name(inline_suffix);
                return;
            }
            if (peek().kind == TOK_MINUS) {
                advance(); // consume '-'
            }
            if (peek().kind == TOK_NUMBER) {
                double num = advance().num_val;
                match(TOK_SEMI);
                emitByte(OP_CONST_NUM);
                emitDouble(num);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout((int)num);
                return;
            } else if (peek().kind == TOK_IDENT || peek().kind == TOK_STRING) {
                string lang = advance().text;
                match(TOK_SEMI);
                emitByte(OP_CONST_STR);
                emitString(lang);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout_by_name(lang);
                return;
            } else {
                match(TOK_SEMI);
                emitByte(OP_CONST_NUM);
                emitDouble(8.0);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout(8);
                return;
            }
        }
    }

    // 0.2 ОЧИСТКА ЭКРАНА И ФИКСИРОВАННЫЙ ТЕКСТ: clear; clear(); not_clear { ... }; not_clear; clearable;
    if (peek().kind == TOK_IDENT && (peek().text == "clear" || peek().text == "cls")) {
        int clr_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "clear", clr_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN);
        emitString("clear");
        emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (peek().kind == TOK_IDENT && peek().text == "not_clear") {
        int nc_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "not_clear", nc_line);
        if (peek().kind == TOK_LBRACE) {
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            parseBlock();
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
            return;
        }
        if (match(TOK_LPAREN)) {
            if (match(TOK_RPAREN)) {
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            } else {
                parseExpression();
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                emitByte(OP_CALL_BUILTIN); emitString("not_clear"); emitByte(1); emitByte(OP_POP);
            }
        } else {
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
        }
        match(TOK_SEMI);
        return;
    }

    if (peek().kind == TOK_IDENT && (peek().text == "clearable" || peek().text == "not_clear_off")) {
        int co_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "clearable", co_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
        return;
    }

    // 0.3 КОМАНДЫ BAREMETAL И VFS (virtual_struct, physical_struct, ls, pwd, cd, reboot, poweroff, etc.)
    if (peek().kind == TOK_IDENT && peek().text == "virtual_struct") {
        parseVirtualStruct();
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "physical_struct") {
        int ps_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "physical_struct", ps_line);
        has_filesystem_struct = true;
        match(TOK_LPAREN);
        string path = "";
        if (peek().kind == TOK_STRING) path = advance().text;
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString(path);
        emitByte(OP_CALL_BUILTIN); emitString("physical_struct"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "bm_exec") {
        int bme_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "bm_exec", bme_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("bm_exec"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && (peek().text == "cat" || peek().text == "read_file")) {
        string cname = peek().text;
        int cat_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", cname, cat_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cat"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && (peek().text == "gui" || peek().text == "vd" || peek().text == "vd_desktop")) {
        string gname = peek().text;
        int gui_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", gname, gui_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("gui"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "reboot") {
        int rb_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "reboot", rb_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("reboot"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "poweroff") {
        int po_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "poweroff", po_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("poweroff"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "pwd") {
        int pwd_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "pwd", pwd_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("pwd"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "ls") {
        int ls_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "ls()", ls_line);
        if (!has_filesystem_struct) {
            reportError(ls_line, "Ошибка компиляции: вызов 'ls()' требует предварительного объявления файловой структуры через virtual_struct { ... } или physical_struct('путь')!");
            return;
        }
        uint8_t argc = 0;
        if (match(TOK_LPAREN)) {
            if (!match(TOK_RPAREN)) {
                parseExpression();
                argc = 1;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            }
        }
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("ls"); emitByte(argc); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "cd") {
        int cd_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "cd", cd_line);
        if (match(TOK_LPAREN)) {
            parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_DOT) {
            string p = advance().text;
            while (peek().kind == TOK_DOT || peek().kind == TOK_SLASH || peek().kind == TOK_IDENT) {
                p += advance().text;
            }
            emitByte(OP_CONST_STR); emitString(p);
        } else {
            emitByte(OP_CONST_STR); emitString("/");
        }
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cd"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "cr_folder") {
        int cf_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "cr_folder", cf_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cr_folder"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "cr_file") {
        int cf_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "cr_file", cf_line);
        match(TOK_LPAREN);
        parseExpression();
        uint8_t argc = 1;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 2;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cr_file"); emitByte(argc); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "delete") {
        int del_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "delete", del_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("delete"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "copy") {
        int cp_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "copy", cp_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("copy"); emitByte(2); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "move") {
        int mv_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "move", mv_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("move"); emitByte(2); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "not_show") {
        int ns_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "not_show", ns_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("not_show"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "specSign_show") {
        int ss_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "specSign_show", ss_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("specSign_show"); emitByte(1); emitByte(OP_POP);
        return;
    }

    // 1. ОБЪЯВЛЕНИЕ ФУНКЦИЙ (int main() {}, take my_func() {}, void my_func() {}, int64* get_ptr() {})
    bool is_func_decl = false;
    if (peek().kind == TOK_IDENT && (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text))) {
        size_t off = 1;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) {
            is_func_decl = true;
        }
    }
    if (is_func_decl) {
        int fn_line = peek().line;
        requireLib("core", "!GrassGreen!", "объявление функции", fn_line);

        string ret_type_tok = advance().text;
        bool ret_is_ptr = false;
        while (match(TOK_STAR)) ret_is_ptr = true;
        string fn_name = advance().text;
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) fn_name += "::" + advance().text;
        }
        if (!current_ns_.empty() && fn_name.find("::") == string::npos) {
            fn_name = current_ns_ + "::" + fn_name;
        }
        if (g_struct_registry.count(ret_type_tok)) {
            if (ret_is_ptr) var_struct_types["__fn_ret_ptr_" + fn_name] = ret_type_tok;
            else var_struct_types["__fn_ret_" + fn_name] = ret_type_tok;
        }
        vector<string> params;
        vector<pair<string, string>> param_structs;

        match(TOK_LPAREN);
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            while (true) {
                string ptype;
                if (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text)) {
                    ptype = advance().text;
                    while (match(TOK_STAR)) {}
                }
                string pname = advance().text;
                params.push_back(pname);
                if (!ptype.empty() && g_struct_registry.count(ptype)) {
                    param_structs.push_back({pname, ptype});
                }
                if (!match(TOK_COMMA)) break;
            }
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t entry_ip = (uint32_t)code.size();
        user_functions[fn_name] = { entry_ip, params };
        func_table[fn_name] = entry_ip;
        string mangled_fn = fn_name;
        for (size_t p = 0; (p = mangled_fn.find("::", p)) != string::npos; ) {
            mangled_fn.replace(p, 2, "__");
        }
        user_functions[mangled_fn] = { entry_ip, params };
        func_table[mangled_fn] = entry_ip;

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

        {
            FunctionContextGuard func_guard(inside_function, true);
            auto saved_scopes = std::move(vm_scopes_);
            auto saved_loop_depths = std::move(loop_scope_depths_);
            auto saved_cont_depths = std::move(continue_scope_depths_);
            vm_scopes_.clear();
            loop_scope_depths_.clear();
            continue_scope_depths_.clear();
            pushVMScope();
            for (const auto& ps : param_structs) {
                registerVMVarType(ps.first, ps.second);
            }
            parseBlock();
            popVMScope(false);
            vm_scopes_ = std::move(saved_scopes);
            loop_scope_depths_ = std::move(saved_loop_depths);
            continue_scope_depths_ = std::move(saved_cont_depths);
        }

        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        emitByte(OP_RET);

        patchU32(skip_patch, (uint32_t)code.size());
        return;
    }

    // Директива @strict
    if (peek().kind == TOK_IDENT && peek().text == "@strict") {
        advance();
        return;
    }

    // Пространства имен namespace Name { ... }
    if (match(TOK_NAMESPACE) || matchIdent("namespace")) {
        if (peek().kind == TOK_IDENT) advance();
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) advance();
        }
        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                size_t before_ns = cur;
                parseStatement();
                if (cur == before_ns && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
                    advance();
                }
            }
            match(TOK_RBRACE);
        }
        match(TOK_SEMI);
        return;
    }

    // Директива using
    if (match(TOK_USING) || matchIdent("using")) {
        if (match(TOK_NAMESPACE) || matchIdent("namespace")) {}
        if (peek().kind == TOK_IDENT) advance();
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) advance();
        }
        match(TOK_SEMI);
        return;
    }

    // Объявление структуры: struct Name { ... };
    if (matchIdent("struct") || matchIdent("class")) {
        parseStructDefinition();
        return;
    }

    // Разрешаем объявление глобальных переменных (take, int, string, void и т.д.) на верхнем уровне
    if (!inside_function && !is_special_file) {
        bool is_var_decl = (peek().kind == TOK_IDENT &&
            (isVarKeyword(peek().text) || peek().text == "take" || peek().text == "volatile" || g_struct_registry.count(peek().text)));
        if (!is_var_decl) {
            reportError(peek().line, "Инструкция '" + peek().text + "' находится вне функции! Весь исполняемый код (кроме объявлений переменных через take/тип) должен быть внутри функций.");
            synchronize();
            return;
        }
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
        bool need_cleanup = !current_dtor_struct_.empty();
        if (!need_cleanup) {
            for (const auto& sc : vm_scopes_) {
                if (!sc.raii_vars.empty()) { need_cleanup = true; break; }
            }
        }
        if (need_cleanup) {
            std::string tmp_ret = "__vm_ret_" + to_string(vm_temp_counter_++);
            emitByte(OP_DEF_VAR); emitString(tmp_ret);
            emitVMScopesCleanupTo(0);
            if (!current_dtor_struct_.empty()) {
                emitVMFieldDtors(current_dtor_struct_);
            }
            emitByte(OP_LOAD_VAR); emitString(tmp_ret);
        }
        emitByte(OP_RET);
        return;
    }

    // Встроенный ассемблер ядра: asm { ... } или asm("...")
    if (matchIdent("asm") || matchIdent("__asm__")) {
        int asm_line = peek(-1).line;
        requireLib("asm", "!asm!", "asm", asm_line);
        string asm_body;
        if (match(TOK_LBRACE)) {
            int depth = 1;
            int last_line = peek().line;
            while (peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LBRACE) depth++;
                else if (peek().kind == TOK_RBRACE) {
                    depth--;
                    if (depth == 0) { advance(); break; }
                }
                if (peek().line > last_line) {
                    asm_body += "\n";
                    last_line = peek().line;
                } else if (!asm_body.empty() && asm_body.back() != '\n') {
                    asm_body += " ";
                }
                asm_body += advance().text;
            }
            match(TOK_SEMI);
        } else if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_STRING) {
                asm_body = advance().text;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                match(TOK_SEMI);
            } else {
                parseExpression();
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN); emitString("asm_run"); emitByte(1);
                emitByte(OP_POP);
                return;
            }
        } else {
            reportError(peek().line, "Ожидалось '{' или '(' после ключевого слова asm");
            return;
        }
        emitByte(OP_CONST_STR); emitString(asm_body);
        emitByte(OP_CALL_BUILTIN); emitString("asm_run"); emitByte(1);
        emitByte(OP_POP);
        return;
    }

    // 2. БЛОК ОКНА ИНТЕРФЕЙСА: window { ... }
    if (matchIdent("window")) {
        parseWindowBlock();
        return;
    }

    // Директива движка отображения: surface("auto" | "windowed" | "freestanding" | "borderless")
    if (matchIdent("surface")) {
        int sf_line = peek(-1).line;
        requireLib("gui", "!interface!", "surface()", sf_line);
        string mode = "auto";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                mode = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString(mode);
        emitByte(OP_CALL_BUILTIN); emitString("surface"); emitByte(1);
        emitByte(OP_POP);
        return;
    }

    // Директива авто-масштабирования High-DPI: auto_size_display();
    if (matchIdent("auto_size_display")) {
        int as_line = peek(-1).line;
        requireLib("gui", "!interface!", "auto_size_display()", as_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("auto_size_display"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    // Директива скругления углов окна приложения: border_radius_app(16) / border-radius-app(16)
    if (matchIdent("border_radius_app") || matchIdent("border-radius-app")) {
        int br_line = peek(-1).line;
        requireLib("gui", "!interface!", "border_radius_app()", br_line);
        string rad = "12";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_NUMBER || peek().kind == TOK_IDENT) {
                rad = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString("border-radius-app(" + rad + ")");
        emitByte(OP_CALL_BUILTIN); emitString("gui_cmd"); emitByte(1);
        emitByte(OP_POP);
        return;
    }

    // ── БИБЛИОТЕКА !audio! ──
    if (matchIdent("tone")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "tone()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        uint8_t argc = 2;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 3;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("tone"); emitByte(argc);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("beep")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "beep()", aud_line);
        match(TOK_LPAREN);
        uint8_t argc = 0;
        if (peek().kind != TOK_RPAREN) {
            parseExpression();
            argc = 1;
            if (match(TOK_COMMA)) {
                parseExpression();
                argc = 2;
            }
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("beep"); emitByte(argc);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("synth_play") || matchIdent("synth")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "synth_play()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        uint8_t argc = 3;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 4;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("synth_play"); emitByte(argc);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_play")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_play()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        uint8_t argc = 1;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 2;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_play"); emitByte(argc);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_stop")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_stop()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_stop"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_pause")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_pause()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_pause"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_resume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_resume()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_resume"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_volume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_volume()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_volume"); emitByte(1);
        emitByte(OP_POP);
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
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) browser_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("device")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) device_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
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
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind == TOK_NUMBER) port_val = advance().num_val;
                    else if (peek().kind != TOK_SEMI) { try { port_val = stod(advance().text); } catch (...) {} }
                    match(TOK_SEMI);
                } else if (matchIdent("IP") || matchIdent("ip")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) ip_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("password")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) pass_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("certainIP") || matchIdent("certain_ip")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) cert_ip_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
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

    // Команда асинхронного запуска: async worker(a, b);
    if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
        int as_line = peek().line;
        requireLib("thread", "!thread!", "async", as_line);
        advance(); // consume "async"
        string fn_target = advance().text; // consume fn name
        match(TOK_LPAREN);
        uint8_t argc = 0;
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            do {
                parseExpression();
                argc++;
            } while (match(TOK_COMMA));
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);

        emitByte(OP_ASYNC_SPAWN);
        if (user_functions.count(fn_target)) {
            emitU32(user_functions[fn_target].entry_ip);
        } else {
            thread_spawn_fixups.push_back({code.size(), fn_target});
            emitU32(0);
        }
        emitByte(argc);
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
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(10.0);
        }
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return;
    }

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
        return;
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
        return;
    }

    // 4. Ключевое слово 'log' устарело и запрещено (используется только потоковый вывод gout)
    if (matchIdent("log")) {
        reportError(peek(-1).line, "Функция 'log' устарела и полностью удалена из GrassGreen! Используйте потоковый вывод: gout << ... << endl;");
        return;
    }

    // ВЫВОД В КОНСОЛЬ (print / println)
    if (matchIdent("print") || matchIdent("println")) {
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
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            parseExpression();
        }
        match(TOK_SEMI);
        emitByte(OP_LOG);
        return;
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
                return;
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
        return;
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
        return;
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
        return;
    }

    // Объявление экземпляра структуры: Player player; или Player* player = ...;
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
            } else {
                emitByte(OP_NEW_STRUCT);
                emitString(s_name);
            }
            emitByte(OP_DEF_VAR);
            emitString(v_name);
            if (!is_explicit_ptr) {
                registerVMRAIIVar(v_name, s_name);
            }
            match(TOK_SEMI);
            return;
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
            return;
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
                if (peek(peek_off).kind == TOK_AMP && peek(peek_off + 1).kind == TOK_IDENT &&
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
        return;
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
        return;
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
        return;
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
        return;
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
        return;
    }

    if (matchIdent("continue") || matchIdent("next")) {
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        if (continue_Fixups.empty()) {
            reportError(peek(-1).line, "Оператор continue может использоваться только внутри циклов!");
            return;
        }
        if (!continue_scope_depths_.empty()) {
            emitVMScopesCleanupTo(continue_scope_depths_.back());
        }
        emitByte(OP_JMP);
        size_t patch_pos = code.size();
        emitU32(0);
        continue_Fixups.back().push_back(patch_pos);
        return;
    }

    if (matchIdent("wait") || matchIdent("sleep")) {
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return;
    }

    if (matchIdent("exit")) {
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_HALT);
        return;
    }

    // Присваивание по индексу массива: myArray[1] = 20;
    if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
        string arr_name = advance().text;
        match(TOK_LBRACKET);
        parseExpression(); // index
        match(TOK_RBRACKET);
        if (match(TOK_ASSIGN)) {
            parseExpression(); // value
            match(TOK_SEMI);
            emitByte(OP_LOAD_VAR); emitString(arr_name);
            emitByte(OP_SET_INDEX);
            return;
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
            return;
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
                return;
            }
        }
    }

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
            return;
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
                vector<string> member_chain;
                while ((peek().kind == TOK_DOT || peek().kind == TOK_ARROW) && peek(1).kind == TOK_IDENT) {
                    advance();
                    member_chain.push_back(advance().text);
                }
                advance(); // consume assignment op
                const string& last_field = member_chain.back();
                if (op == TOK_ASSIGN) {
                    parseExpression(); // value
                    match(TOK_SEMI);
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        emitByte(OP_GET_FIELD); emitString(member_chain[ci]);
                    }
                    emitByte(OP_SET_FIELD); emitString(last_field);
                } else {
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
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
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    for (size_t ci = 0; ci + 1 < member_chain.size(); ++ci) {
                        emitByte(OP_GET_FIELD); emitString(member_chain[ci]);
                    }
                    emitByte(OP_SET_FIELD); emitString(last_field);
                }
                return;
            }
        }
        if (peek(1).kind == TOK_ASSIGN) {
            string var_name = advance().text;
            advance();
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
        reportError(peek().line, "Синтаксическая ошибка: непредвиденная инструкция '" + peek().text + "'!");
        synchronize();
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
    if (match(TOK_COLON)) {
        if (peek().kind == TOK_IDENT) advance();
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

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (match(TOK_SEMI)) continue;

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

        bool is_method = false;
        if (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text)) {
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
            if ((isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text)) &&
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

            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    if ((isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text)) &&
                        (peek(1).kind == TOK_IDENT || peek(1).kind == TOK_STAR)) {
                        advance();
                        while (match(TOK_STAR)) {}
                    }
                    params.push_back(advance().text);
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
            current_struct_context = sname;
            {
                FunctionContextGuard func_guard(inside_function, true);
                parseBlock();
            }
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
        // Поддержка двух вариантов объявления полей:
        // 1. C-style: string name; или int hp;
        // 2. RoadMap-style: hp: u32, или x: u64
        string f_type, f_name;
        bool is_field_ptr = false;
        if (peek(1).kind == TOK_ASSIGN || peek(1).kind == TOK_COLON) {
            f_name = advance().text;
            advance(); // consume ':' / '='
            f_type = advance().text;
            while (match(TOK_STAR)) is_field_ptr = true;
            if (match(TOK_COMMA) || match(TOK_SEMI)) {}
        } else {
            f_type = advance().text;
            while (match(TOK_STAR)) is_field_ptr = true;
            f_name = advance().text;
            if (match(TOK_SEMI) || match(TOK_COMMA)) {}
        }

        GrassFieldType ft = GrassFieldType::VALUE;
        size_t f_size = 8;
        size_t f_align = 8;
        string f_struct_name;

        if (is_field_ptr) {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
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

        // C-ABI Data Alignment: выравнивание смещения поля
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

bool GrassParser::match(GrassTokenKind kind) {
    if (peek().kind == kind) {
        advance();
        return true;
    }
    return false;
}

bool GrassParser::matchIdent(const std::string& name) {
    if (peek().kind == TOK_IDENT && peek().text == name) {
        advance();
        return true;
    }
    return false;
}

bool GrassParser::check(GrassTokenKind kind) const {
    return peek().kind == kind;
}

bool GrassParser::checkIdent(const std::string& name) const {
    return peek().kind == TOK_IDENT && peek().text == name;
}

bool GrassParser::isAtEnd() const {
    return peek().kind == TOK_EOF;
}

void GrassParser::reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics.push_back(SyntaxDiagnostic(DiagnosticSeverity::Error, loc, msg, hint));
    error_count++;
}

void GrassParser::synchronize() {
    if (isAtEnd()) return;
    advance();

    while (!isAtEnd()) {
        if (peek(-1).kind == TOK_SEMI) return;

        switch (peek().kind) {
            case TOK_SEMI:
            case TOK_RBRACE:
            case TOK_LIB:
                return;
            case TOK_IDENT: {
                const std::string& txt = peek().text;
                if (isTypeName(txt) || isVarKeyword(txt) ||
                    txt == "struct" || txt == "class" || txt == "module" || txt == "fn" ||
                    txt == "if" || txt == "check" || txt == "while" || txt == "loop" ||
                    txt == "for" || txt == "again" || txt == "return" ||
                    txt == "break" || txt == "continue" ||
                    txt == "gout" || txt == "cout" || txt == "gin" || txt == "cin") {
                    return;
                }
                break;
            }
            default:
                break;
        }
        advance();
    }
}

bool GrassParser::isTypeName(const std::string& s) const {
    if (custom_types.count(s)) return true;
    return s == "int" || s == "float" || s == "double" || s == "string" ||
           s == "bool" || s == "void" || s == "char" || s == "take" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "byte" || s == "size_t";
}

bool GrassParser::isVarKeyword(const std::string& s) {
    return s == "var" || s == "auto" || s == "const" || s == "take";
}

// Expressions
std::unique_ptr<ExprAST> GrassParser::parseExpression() {
    if (parser_depth_ >= MAX_PARSER_DEPTH) {
        reportError(peek().loc(filename), "Expression nesting depth exceeds safety limit", "Simplify nested expression");
        return nullptr;
    }
    parser_depth_++;
    auto res = parseLogicalOr();
    parser_depth_--;
    return res;
}

std::unique_ptr<ExprAST> GrassParser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (expr && match(TOK_OR)) {
        auto loc = peek(-1).loc(filename);
        auto rhs = parseLogicalAnd();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(loc, TOK_OR, "||", std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseLogicalAnd() {
    auto expr = parseBitwiseOr();
    while (expr && match(TOK_AND)) {
        auto loc = peek(-1).loc(filename);
        auto rhs = parseBitwiseOr();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(loc, TOK_AND, "&&", std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();
    while (expr && match(TOK_PIPE)) {
        auto loc = peek(-1).loc(filename);
        auto rhs = parseBitwiseXor();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(loc, TOK_PIPE, "|", std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();
    while (expr && match(TOK_CARET)) {
        auto loc = peek(-1).loc(filename);
        auto rhs = parseBitwiseAnd();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(loc, TOK_CARET, "^", std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseBitwiseAnd() {
    auto expr = parseEquality();
    while (expr && match(TOK_AMP)) {
        auto loc = peek(-1).loc(filename);
        auto rhs = parseEquality();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(loc, TOK_AMP, "&", std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseEquality() {
    auto expr = parseRelational();
    while (expr && (peek().kind == TOK_EQ || peek().kind == TOK_NEQ)) {
        auto op_tok = advance();
        auto rhs = parseRelational();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseRelational() {
    auto expr = parseShift();
    while (expr && (peek().kind == TOK_LT || peek().kind == TOK_GT ||
                    peek().kind == TOK_LTE || peek().kind == TOK_GTE)) {
        auto op_tok = advance();
        auto rhs = parseShift();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseShift() {
    auto expr = parseAdditive();
    while (expr && (peek().kind == TOK_SHL || peek().kind == TOK_SHR)) {
        auto op_tok = advance();
        auto rhs = parseAdditive();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (expr && (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS)) {
        auto op_tok = advance();
        auto rhs = parseMultiplicative();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseMultiplicative() {
    auto expr = parseUnary();
    while (expr && (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD)) {
        auto op_tok = advance();
        auto rhs = parseUnary();
        if (!rhs) return nullptr;
        expr = std::make_unique<BinaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(expr), std::move(rhs));
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parseUnary() {
    if (parser_depth_ >= MAX_PARSER_DEPTH) {
        reportError(peek().loc(filename), "Unary nesting depth exceeds safety limit", "Simplify expression");
        return nullptr;
    }
    // Check for C-style cast: (Type)expr or (Type*)expr
    if (check(TOK_LPAREN)) {
        size_t off = 1;
        if (peek(off).kind == TOK_IDENT && isTypeName(peek(off).text)) {
            std::string tname = peek(off).text;
            off++;
            while (peek(off).kind == TOK_COLON_COLON) {
                off++;
                if (peek(off).kind == TOK_IDENT) {
                    tname += "::" + peek(off).text;
                    off++;
                } else break;
            }
            int ptr_depth = 0;
            while (peek(off).kind == TOK_STAR) {
                ptr_depth++;
                off++;
            }
            if (peek(off).kind == TOK_RPAREN) {
                auto next_tok = peek(off + 1);
                if (next_tok.kind == TOK_IDENT || next_tok.kind == TOK_NUMBER ||
                    next_tok.kind == TOK_STRING || next_tok.kind == TOK_LPAREN ||
                    next_tok.kind == TOK_STAR || next_tok.kind == TOK_AMP ||
                    next_tok.kind == TOK_MINUS || next_tok.kind == TOK_NULLPTR) {
                    auto loc = advance().loc(filename); // consume '('
                    advance(); // consume type
                    while (match(TOK_COLON_COLON)) advance();
                    while (match(TOK_STAR)) {}
                    match(TOK_RPAREN);
                    parser_depth_++;
                    auto operand = parseUnary();
                    parser_depth_--;
                    if (!operand) return nullptr;
                    return std::make_unique<CastExprAST>(loc, tname, ptr_depth, std::move(operand), true);
                }
            }
        }
    }

    if (peek().kind == TOK_MINUS || peek().kind == TOK_NOT ||
        peek().kind == TOK_TILDE || peek().kind == TOK_STAR ||
        peek().kind == TOK_AMP || peek().kind == TOK_INC || peek().kind == TOK_DEC) {
        auto op_tok = advance();
        parser_depth_++;
        auto operand = parseUnary();
        parser_depth_--;
        if (!operand) return nullptr;
        return std::make_unique<UnaryExprAST>(op_tok.loc(filename), op_tok.kind, op_tok.text, std::move(operand), true);
    }
    return parsePostfix();
}

std::unique_ptr<ExprAST> GrassParser::parsePostfix() {
    auto expr = parsePrimary();
    if (!expr) return nullptr;

    while (true) {
        if (match(TOK_INC)) {
            expr = std::make_unique<PostfixExprAST>(peek(-1).loc(filename), TOK_INC, "++", std::move(expr));
        } else if (match(TOK_DEC)) {
            expr = std::make_unique<PostfixExprAST>(peek(-1).loc(filename), TOK_DEC, "--", std::move(expr));
        } else if (match(TOK_LBRACKET)) {
            auto loc = peek(-1).loc(filename);
            auto idx = parseExpression();
            if (!match(TOK_RBRACKET)) {
                reportError(peek().loc(filename), "Expected ']' after index expression", "Close array subscript with ']'");
            }
            expr = std::make_unique<IndexExprAST>(loc, std::move(expr), std::move(idx));
        } else if (match(TOK_ARROW)) {
            auto loc = peek(-1).loc(filename);
            if (peek().kind == TOK_IDENT) {
                std::string member = advance().text;
                if (match(TOK_LPAREN)) {
                    std::vector<std::unique_ptr<ExprAST>> args;
                    if (!check(TOK_RPAREN) && !isAtEnd()) {
                        do {
                            args.push_back(parseExpression());
                        } while (match(TOK_COMMA));
                    }
                    if (!match(TOK_RPAREN)) {
                        reportError(peek().loc(filename), "Expected ')' after method arguments", "Close argument list with ')'");
                    }
                    auto mem_node = std::make_unique<MemberAccessExprAST>(loc, std::move(expr), member, true);
                    mem_node->is_method_call = true;
                    mem_node->method_args = std::move(args);
                    expr = std::move(mem_node);
                } else {
                    expr = std::make_unique<MemberAccessExprAST>(loc, std::move(expr), member, true);
                }
            } else {
                reportError(peek().loc(filename), "Expected identifier after '->'", "Provide member name");
            }
        } else if (match(TOK_AS)) {
            auto loc = peek(-1).loc(filename);
            std::string tname = "";
            if (peek().kind == TOK_IDENT) {
                tname = advance().text;
                while (match(TOK_COLON_COLON)) {
                    if (peek().kind == TOK_IDENT) {
                        tname += "::" + advance().text;
                    }
                }
            } else {
                reportError(peek().loc(filename), "Expected type name after 'as'", "Specify target type");
            }
            int ptr_depth = 0;
            while (match(TOK_STAR)) {
                ptr_depth++;
            }
            expr = std::make_unique<CastExprAST>(loc, tname, ptr_depth, std::move(expr), false);
        } else if (match(TOK_DOT)) {
            auto loc = peek(-1).loc(filename);
            if (peek().kind == TOK_IDENT) {
                std::string member = advance().text;
                if (match(TOK_LPAREN)) {
                    std::vector<std::unique_ptr<ExprAST>> args;
                    if (!check(TOK_RPAREN) && !isAtEnd()) {
                        do {
                            args.push_back(parseExpression());
                        } while (match(TOK_COMMA));
                    }
                    if (!match(TOK_RPAREN)) {
                        reportError(peek().loc(filename), "Expected ')' after method arguments", "Close argument list with ')'");
                    }
                    auto mem_node = std::make_unique<MemberAccessExprAST>(loc, std::move(expr), member, false);
                    mem_node->is_method_call = true;
                    mem_node->method_args = std::move(args);
                    expr = std::move(mem_node);
                } else {
                    expr = std::make_unique<MemberAccessExprAST>(loc, std::move(expr), member, false);
                }
            } else {
                reportError(peek().loc(filename), "Expected identifier after '.'", "Provide member name");
            }
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<ExprAST> GrassParser::parsePrimary() {
    if (peek().kind == TOK_NUMBER) {
        auto tok = advance();
        if (tok.text.find('.') == std::string::npos &&
            tok.text.find('e') == std::string::npos &&
            tok.text.find('E') == std::string::npos) {
            return std::make_unique<LiteralExprAST>(tok.loc(filename), (int64_t)tok.num_val);
        } else {
            return std::make_unique<LiteralExprAST>(tok.loc(filename), tok.num_val);
        }
    }
    if (peek().kind == TOK_STRING) {
        auto tok = advance();
        return std::make_unique<LiteralExprAST>(tok.loc(filename), tok.text);
    }
    if (checkIdent("true")) {
        auto tok = advance();
        return std::make_unique<LiteralExprAST>(tok.loc(filename), true);
    }
    if (checkIdent("false")) {
        auto tok = advance();
        return std::make_unique<LiteralExprAST>(tok.loc(filename), false);
    }
    if (checkIdent("nil")) {
        auto tok = advance();
        return std::make_unique<LiteralExprAST>(tok.loc(filename));
    }
    if (peek().kind == TOK_NULLPTR) {
        auto tok = advance();
        return std::make_unique<LiteralExprAST>(tok.loc(filename), nullptr);
    }
    if (match(TOK_SIZEOF)) {
        auto loc = peek(-1).loc(filename);
        if (!match(TOK_LPAREN)) {
            reportError(peek().loc(filename), "Expected '(' after sizeof", "Enclose sizeof operand in '(' and ')'");
        }
        if (peek().kind == TOK_IDENT && isTypeName(peek().text)) {
            std::string tname = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) {
                    tname += "::" + advance().text;
                }
            }
            int ptr_depth = 0;
            while (match(TOK_STAR)) {
                ptr_depth++;
            }
            if (!match(TOK_RPAREN)) {
                reportError(peek().loc(filename), "Expected ')' after type in sizeof", "Close with ')'");
            }
            return std::make_unique<SizeofExprAST>(loc, tname, ptr_depth);
        } else {
            auto expr = parseExpression();
            if (!match(TOK_RPAREN)) {
                reportError(peek().loc(filename), "Expected ')' after expression in sizeof", "Close with ')'");
            }
            return std::make_unique<SizeofExprAST>(loc, "", 0, std::move(expr));
        }
    }
    if (checkIdent("alloc")) {
        auto loc = advance().loc(filename);
        std::unique_ptr<ExprAST> sz = nullptr;
        if (match(TOK_LPAREN)) {
            sz = parseExpression();
            if (!match(TOK_RPAREN)) {
                reportError(peek().loc(filename), "Expected ')' after alloc argument", "Close alloc call with ')'");
            }
        }
        return std::make_unique<AllocExprAST>(loc, "void", std::move(sz));
    }
    if (checkIdent("free")) {
        auto loc = advance().loc(filename);
        std::unique_ptr<ExprAST> tgt = nullptr;
        if (match(TOK_LPAREN)) {
            tgt = parseExpression();
            if (!match(TOK_RPAREN)) {
                reportError(peek().loc(filename), "Expected ')' after free argument", "Close free call with ')'");
            }
        }
        return std::make_unique<FreeExprAST>(loc, std::move(tgt));
    }
    if (peek().kind == TOK_IDENT) {
        auto tok = advance();
        std::string full_name = tok.text;
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) {
                full_name += "::" + advance().text;
            } else {
                reportError(peek().loc(filename), "Expected identifier after '::'", "Provide identifier");
                break;
            }
        }
        if (match(TOK_LPAREN)) {
            std::vector<std::unique_ptr<ExprAST>> args;
            if (!check(TOK_RPAREN) && !isAtEnd()) {
                do {
                    args.push_back(parseExpression());
                } while (match(TOK_COMMA));
            }
            if (!match(TOK_RPAREN)) {
                reportError(peek().loc(filename), "Expected ')' after function arguments in call to '" + full_name + "'", "Close call with ')'");
            }
            return std::make_unique<CallExprAST>(tok.loc(filename), full_name, std::move(args));
        }
        return std::make_unique<VariableExprAST>(tok.loc(filename), full_name);
    }
    if (match(TOK_LBRACKET)) {
        auto loc = peek(-1).loc(filename);
        std::vector<std::unique_ptr<ExprAST>> elements;
        if (!check(TOK_RBRACKET) && !isAtEnd()) {
            do {
                elements.push_back(parseExpression());
            } while (match(TOK_COMMA));
        }
        if (!match(TOK_RBRACKET)) {
            reportError(peek().loc(filename), "Expected ']' at end of array literal", "Close array with ']'");
        }
        return std::make_unique<ArrayLiteralExprAST>(loc, std::move(elements));
    }
    if (match(TOK_LPAREN)) {
        auto expr = parseExpression();
        if (!match(TOK_RPAREN)) {
            reportError(peek().loc(filename), "Expected ')' after expression", "Close parentheses with ')'");
        }
        return expr;
    }

    reportError(peek().loc(filename), "Unexpected token '" + peek().text + "' in expression", "Expected expression");
    advance();
    return nullptr;
}

// Statements & Declarations
std::unique_ptr<StmtAST> GrassParser::parseStatement() {
    if (match(TOK_SEMI)) return nullptr;
    if (check(TOK_LBRACE)) return parseBlock();

    if (checkIdent("if") || checkIdent("check")) return parseIf();
    if (checkIdent("while") || checkIdent("loop")) return parseWhile();
    if (checkIdent("for") || checkIdent("again")) return parseFor();
    if (checkIdent("return")) return parseReturn();

    if (checkIdent("break")) {
        auto loc = advance().loc(filename);
        if (!match(TOK_SEMI)) {
            reportError(peek().loc(filename), "Expected ';' after break", "Insert ';'");
            synchronize();
        }
        return std::make_unique<BreakStmtAST>(loc);
    }
    if (checkIdent("continue")) {
        auto loc = advance().loc(filename);
        if (!match(TOK_SEMI)) {
            reportError(peek().loc(filename), "Expected ';' after continue", "Insert ';'");
            synchronize();
        }
        return std::make_unique<ContinueStmtAST>(loc);
    }
    if (checkIdent("gout") || checkIdent("cout")) return parseStreamIO(true);
    if (checkIdent("gin") || checkIdent("cin")) return parseStreamIO(false);
    if (peek().kind == TOK_IDENT && peek().text == "std" && peek(1).kind == TOK_COLON_COLON) {
        if (peek(2).text == "gout" || peek(2).text == "cout") {
            advance(); advance();
            return parseStreamIO(true);
        }
        if (peek(2).text == "gin" || peek(2).text == "cin") {
            advance(); advance();
            return parseStreamIO(false);
        }
    }

    bool is_type = isTypeName(peek().text);
    if (!is_type && peek().kind == TOK_IDENT) {
        if (peek(1).kind == TOK_IDENT) {
            is_type = true;
        } else if (peek(1).kind == TOK_STAR) {
            size_t off = 2;
            while (peek(off).kind == TOK_STAR) off++;
            if (peek(off).kind == TOK_IDENT) is_type = true;
        }
    }

    if (is_type) {
        std::string tname = advance().text;
        int ptr_depth = 0;
        while (match(TOK_STAR)) ptr_depth++;
        if (peek().kind == TOK_IDENT) {
            return parseVarDecl(tname, ptr_depth, false);
        }
        reportError(peek().loc(filename), "Expected identifier after type '" + tname + "'", "Provide variable name");
        synchronize();
        return nullptr;
    }

    if (isVarKeyword(peek().text)) {
        std::string kw = advance().text;
        bool is_const = (kw == "const");
        std::string tname = "var";
        int ptr_depth = 0;
        if (isTypeName(peek().text)) {
            tname = advance().text;
            while (match(TOK_STAR)) ptr_depth++;
        }
        return parseVarDecl(tname, ptr_depth, is_const);
    }

    // Assignment or expression statement
    SourceLocation loc = peek().loc(filename);
    auto expr = parseExpression();
    if (!expr) {
        synchronize();
        return nullptr;
    }

    if (peek().kind == TOK_ASSIGN ||
        peek().kind == TOK_PLUS_ASSIGN ||
        peek().kind == TOK_MINUS_ASSIGN ||
        peek().kind == TOK_MUL_ASSIGN ||
        peek().kind == TOK_DIV_ASSIGN) {
        auto op_tok = advance();
        auto val = parseExpression();
        if (!match(TOK_SEMI)) {
            reportError(peek().loc(filename), "Expected ';' after assignment, found '" + peek().text + "'", "Insert ';' at end of assignment");
            synchronize();
        }
        return std::make_unique<AssignStmtAST>(loc, std::move(expr), op_tok.kind, op_tok.text, std::move(val));
    }

    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after expression statement, found '" + peek().text + "'", "Insert ';' at end of statement");
        synchronize();
    }
    return std::make_unique<ExprStmtAST>(loc, std::move(expr));
}

std::unique_ptr<BlockStmtAST> GrassParser::parseBlock() {
    auto loc = advance().loc(filename); // consume {
    std::vector<std::unique_ptr<StmtAST>> stmts;

    while (!check(TOK_RBRACE) && !isAtEnd()) {
        try {
            auto s = parseStatement();
            if (s) stmts.push_back(std::move(s));
        } catch (...) {
            synchronize();
        }
    }

    if (!match(TOK_RBRACE)) {
        reportError(peek().loc(filename), "Expected '}' to close block", "Insert '}' at the end of block");
    }
    return std::make_unique<BlockStmtAST>(loc, std::move(stmts));
}

std::unique_ptr<StmtAST> GrassParser::parseVarDecl(const std::string& type_name, int ptr_depth, bool is_const) {
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected variable name in declaration", "Provide variable identifier");
        synchronize();
        return nullptr;
    }
    auto tok = advance();
    std::string vname = tok.text;
    std::unique_ptr<ExprAST> init = nullptr;

    if (match(TOK_ASSIGN)) {
        init = parseExpression();
    }

    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after variable declaration of '" + vname + "', found '" + peek().text + "'", "Insert ';' to end declaration");
        synchronize();
    }

    return std::make_unique<VarDeclStmtAST>(tok.loc(filename), type_name, vname, ptr_depth, is_const, std::move(init));
}

std::unique_ptr<StmtAST> GrassParser::parseIf() {
    auto loc = advance().loc(filename);
    if (!match(TOK_LPAREN)) {
        reportError(peek().loc(filename), "Expected '(' after 'if'", "Enclose condition in '(' and ')'");
    }
    auto cond = parseExpression();
    if (!match(TOK_RPAREN)) {
        reportError(peek().loc(filename), "Expected ')' after condition", "Close condition with ')'");
    }
    auto then_b = parseStatement();
    std::unique_ptr<StmtAST> else_b = nullptr;
    if (checkIdent("else")) {
        advance();
        else_b = parseStatement();
    }
    return std::make_unique<IfStmtAST>(loc, std::move(cond), std::move(then_b), std::move(else_b));
}

std::unique_ptr<StmtAST> GrassParser::parseWhile() {
    auto loc = advance().loc(filename);
    if (!match(TOK_LPAREN)) {
        reportError(peek().loc(filename), "Expected '(' after 'while'", "Enclose condition in '(' and ')'");
    }
    auto cond = parseExpression();
    if (!match(TOK_RPAREN)) {
        reportError(peek().loc(filename), "Expected ')' after condition", "Close condition with ')'");
    }
    auto body = parseStatement();
    return std::make_unique<WhileStmtAST>(loc, std::move(cond), std::move(body));
}

std::unique_ptr<StmtAST> GrassParser::parseFor() {
    auto loc = advance().loc(filename);
    if (!match(TOK_LPAREN)) {
        reportError(peek().loc(filename), "Expected '(' after 'for'", "Enclose loop control in '(' and ')'");
    }
    std::unique_ptr<StmtAST> init = nullptr;
    if (!check(TOK_SEMI)) {
        if (isTypeName(peek().text)) {
            std::string tname = advance().text;
            int ptr = 0; while (match(TOK_STAR)) ptr++;
            init = parseVarDecl(tname, ptr, false);
        } else {
            auto e = parseExpression();
            if (e) {
                init = std::make_unique<ExprStmtAST>(loc, std::move(e));
                match(TOK_SEMI);
            }
        }
    } else {
        advance();
    }

    std::unique_ptr<ExprAST> cond = nullptr;
    if (!check(TOK_SEMI)) {
        cond = parseExpression();
    }
    match(TOK_SEMI);

    std::unique_ptr<StmtAST> step = nullptr;
    if (!check(TOK_RPAREN)) {
        auto e = parseExpression();
        if (e) step = std::make_unique<ExprStmtAST>(loc, std::move(e));
    }
    if (!match(TOK_RPAREN)) {
        reportError(peek().loc(filename), "Expected ')' after for-loop clauses", "Close for-loop clauses with ')'");
    }
    auto body = parseStatement();
    return std::make_unique<ForStmtAST>(loc, std::move(init), std::move(cond), std::move(step), std::move(body));
}

std::unique_ptr<StmtAST> GrassParser::parseReturn() {
    auto loc = advance().loc(filename);
    std::unique_ptr<ExprAST> val = nullptr;
    if (!check(TOK_SEMI) && !check(TOK_RBRACE) && !isAtEnd()) {
        val = parseExpression();
    }
    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after return statement, found '" + peek().text + "'", "Insert ';' after return value");
        synchronize();
    }
    return std::make_unique<ReturnStmtAST>(loc, std::move(val));
}

std::unique_ptr<StmtAST> GrassParser::parseStreamIO(bool is_output) {
    auto loc = advance().loc(filename);
    std::vector<std::unique_ptr<ExprAST>> operands;
    GrassTokenKind op = is_output ? TOK_SHL : TOK_SHR;

    while (match(op)) {
        operands.push_back(parseAdditive());
    }
    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after stream IO statement", "Insert ';' at end of stream expression");
        synchronize();
    }
    return std::make_unique<StreamIOStmtAST>(loc, is_output, std::move(operands));
}

std::unique_ptr<FunctionDeclAST> GrassParser::parseFunction(const std::string& ret_type, int ret_ptr, const std::string& name) {
    SourceLocation loc = peek(-1).loc(filename);
    if (!match(TOK_LPAREN)) {
        reportError(peek().loc(filename), "Expected '(' after function name '" + name + "'", "Enclose parameters in '(' and ')'");
    }

    std::vector<ParamDecl> params;
    if (!check(TOK_RPAREN) && !isAtEnd()) {
        do {
            std::string ptype = "var";
            int pptr = 0;
            if (isTypeName(peek().text) || isVarKeyword(peek().text)) {
                ptype = advance().text;
                while (match(TOK_STAR)) pptr++;
            }
            if (peek().kind == TOK_IDENT) {
                auto ptok = advance();
                params.emplace_back(ptype, ptok.text, pptr, ptok.loc(filename));
            } else {
                reportError(peek().loc(filename), "Expected parameter name", "Provide identifier for parameter");
            }
        } while (match(TOK_COMMA));
    }

    if (!match(TOK_RPAREN)) {
        reportError(peek().loc(filename), "Expected ')' after parameter list in function '" + name + "'", "Close parameter list with ')'");
    }

    std::unique_ptr<BlockStmtAST> body = nullptr;
    if (check(TOK_LBRACE)) {
        body = parseBlock();
    } else if (match(TOK_SEMI)) {
        // Prototype
    } else {
        reportError(peek().loc(filename), "Expected '{' for function body or ';' for declaration", "Provide function body block");
        synchronize();
    }

    return std::make_unique<FunctionDeclAST>(loc, ret_type, ret_ptr, name, std::move(params), std::move(body));
}

std::unique_ptr<StructDeclAST> GrassParser::parseStruct() {
    auto loc = advance().loc(filename);
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected struct name after 'struct'", "Provide struct identifier");
        synchronize();
        return nullptr;
    }
    std::string sname = advance().text;
    custom_types.insert(sname);
    if (!match(TOK_LBRACE)) {
        reportError(peek().loc(filename), "Expected '{' after struct name", "Open struct body with '{'");
        synchronize();
        return nullptr;
    }

    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;

    while (!check(TOK_RBRACE) && !isAtEnd()) {
        if (match(TOK_SEMI)) continue;
        if (match(TOK_TILDE)) {
            SourceLocation dloc = peek(-1).loc(filename);
            if (peek().kind != TOK_IDENT) {
                reportError(peek().loc(filename), "Expected struct name after '~'", "Declare destructor as ~" + sname + "()");
                synchronize();
                continue;
            }
            std::string dname = advance().text;
            auto dtor = parseFunction("void", 0, "~" + dname);
            dtor->loc = dloc;
            dtor->is_method = true;
            dtor->is_destructor = true;
            methods.push_back(std::move(dtor));
            continue;
        }
        if (checkIdent("fn")) {
            advance();
            if (peek().kind != TOK_IDENT) {
                reportError(peek().loc(filename), "Expected method name after 'fn' in struct", "Provide method identifier");
                synchronize();
                continue;
            }
            std::string fname = advance().text;
            auto fn = parseFunction("auto", 0, fname);
            fn->is_method = true;
            methods.push_back(std::move(fn));
            continue;
        }
        if (isTypeName(peek().text)) {
            std::string tname = advance().text;
            int ptr = 0; while (match(TOK_STAR)) ptr++;
            if (peek().kind == TOK_IDENT) {
                std::string fname = advance().text;
                if (check(TOK_LPAREN)) {
                    auto fn = parseFunction(tname, ptr, fname);
                    fn->is_method = true;
                    methods.push_back(std::move(fn));
                } else {
                    fields.emplace_back(tname, fname, ptr, peek(-1).loc(filename));
                    match(TOK_SEMI);
                }
            } else {
                reportError(peek().loc(filename), "Expected field or method name in struct", "Provide valid identifier");
                synchronize();
            }
        } else {
            reportError(peek().loc(filename), "Unexpected token '" + peek().text + "' in struct body", "Declare field or method");
            synchronize();
        }
    }

    if (!match(TOK_RBRACE)) {
        reportError(peek().loc(filename), "Expected '}' at end of struct definition", "Close struct definition with '}'");
    }
    match(TOK_SEMI);

    return std::make_unique<StructDeclAST>(loc, sname, std::move(fields), std::move(methods));
}

std::unique_ptr<ClassDeclAST> GrassParser::parseClass() {
    auto loc = advance().loc(filename);
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected class name after 'class'", "Provide class name");
        synchronize();
        return nullptr;
    }
    std::string cname = advance().text;
    custom_types.insert(cname);
    std::string base_class;
    if (match(TOK_COLON)) {
        if (peek().kind == TOK_IDENT) {
            base_class = advance().text;
        } else {
            reportError(peek().loc(filename), "Expected base class name after ':'", "Provide inherited class name");
        }
    }

    if (!match(TOK_LBRACE)) {
        reportError(peek().loc(filename), "Expected '{' after class name", "Open class body with '{'");
        synchronize();
        return nullptr;
    }

    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;

    while (!check(TOK_RBRACE) && !isAtEnd()) {
        if (match(TOK_SEMI)) continue;
        if (match(TOK_TILDE)) {
            SourceLocation dloc = peek(-1).loc(filename);
            if (peek().kind != TOK_IDENT) {
                reportError(peek().loc(filename), "Expected class name after '~'", "Declare destructor as ~" + cname + "()");
                synchronize();
                continue;
            }
            std::string dname = advance().text;
            auto dtor = parseFunction("void", 0, "~" + dname);
            dtor->loc = dloc;
            dtor->is_method = true;
            dtor->is_destructor = true;
            methods.push_back(std::move(dtor));
            continue;
        }
        if (checkIdent("fn")) {
            advance();
            if (peek().kind != TOK_IDENT) {
                reportError(peek().loc(filename), "Expected method name after 'fn' in struct", "Provide method identifier");
                synchronize();
                continue;
            }
            std::string fname = advance().text;
            auto fn = parseFunction("auto", 0, fname);
            fn->is_method = true;
            methods.push_back(std::move(fn));
            continue;
        }
        if (isTypeName(peek().text)) {
            std::string tname = advance().text;
            int ptr = 0; while (match(TOK_STAR)) ptr++;
            if (peek().kind == TOK_IDENT) {
                std::string fname = advance().text;
                if (check(TOK_LPAREN)) {
                    auto fn = parseFunction(tname, ptr, fname);
                    fn->is_method = true;
                    methods.push_back(std::move(fn));
                } else {
                    fields.emplace_back(tname, fname, ptr, peek(-1).loc(filename));
                    match(TOK_SEMI);
                }
            } else {
                reportError(peek().loc(filename), "Expected field or method name in class", "Provide valid identifier");
                synchronize();
            }
        } else {
            reportError(peek().loc(filename), "Unexpected token '" + peek().text + "' in class body", "Declare field or method");
            synchronize();
        }
    }

    if (!match(TOK_RBRACE)) {
        reportError(peek().loc(filename), "Expected '}' at end of class definition", "Close class definition with '}'");
    }
    match(TOK_SEMI);

    return std::make_unique<ClassDeclAST>(loc, cname, base_class, std::move(fields), std::move(methods));
}

std::unique_ptr<ModuleDeclAST> GrassParser::parseModule() {
    auto loc = advance().loc(filename);
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected module name after 'module'", "Provide module name");
        synchronize();
        return nullptr;
    }
    std::string mname = advance().text;
    if (!match(TOK_LBRACE)) {
        reportError(peek().loc(filename), "Expected '{' after module name", "Open module body with '{'");
        synchronize();
        return nullptr;
    }

    auto mod = std::make_unique<ModuleDeclAST>(loc, mname);

    while (!check(TOK_RBRACE) && !isAtEnd()) {
        if (match(TOK_SEMI)) continue;
        if (checkIdent("struct")) {
            auto st = parseStruct();
            if (st) mod->structs.push_back(std::move(st));
        } else if (checkIdent("class")) {
            auto cls = parseClass();
            if (cls) mod->classes.push_back(std::move(cls));
        } else if (isTypeName(peek().text)) {
            std::string tname = advance().text;
            int ptr = 0; while (match(TOK_STAR)) ptr++;
            if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN) {
                std::string fname = advance().text;
                mod->functions.push_back(parseFunction(tname, ptr, fname));
            } else {
                reportError(peek().loc(filename), "Expected function declaration in module", "Only functions, structs, and classes are allowed inside modules");
                synchronize();
            }
        } else {
            reportError(peek().loc(filename), "Unexpected token in module", "Only declarations allowed inside module");
            synchronize();
        }
    }

    if (!match(TOK_RBRACE)) {
        reportError(peek().loc(filename), "Expected '}' to close module", "Close module definition with '}'");
    }
    return mod;
}

std::unique_ptr<NamespaceDeclAST> GrassParser::parseNamespace() {
    auto loc = advance().loc(filename); // consume 'namespace'
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected namespace name after 'namespace'", "Provide namespace identifier");
        synchronize();
        return nullptr;
    }
    std::string ns_name = advance().text;
    while (match(TOK_COLON_COLON)) {
        if (peek().kind == TOK_IDENT) {
            ns_name += "::" + advance().text;
        } else {
            reportError(peek().loc(filename), "Expected identifier after '::'", "Provide identifier");
            break;
        }
    }
    if (!match(TOK_LBRACE)) {
        reportError(peek().loc(filename), "Expected '{' after namespace name", "Open namespace body with '{'");
        synchronize();
        return nullptr;
    }

    std::vector<std::unique_ptr<DeclAST>> decls;
    std::vector<std::unique_ptr<StmtAST>> stmts;

    while (!check(TOK_RBRACE) && !isAtEnd()) {
        if (match(TOK_SEMI)) continue;
        if (peek().kind == TOK_NAMESPACE || checkIdent("namespace")) {
            auto inner_ns = parseNamespace();
            if (inner_ns) decls.push_back(std::move(inner_ns));
        } else if (peek().kind == TOK_USING || checkIdent("using")) {
            auto u = parseUsing();
            if (u) decls.push_back(std::move(u));
        } else if (checkIdent("struct")) {
            auto st = parseStruct();
            if (st) decls.push_back(std::move(st));
        } else if (checkIdent("class")) {
            auto cls = parseClass();
            if (cls) decls.push_back(std::move(cls));
        } else if (checkIdent("module")) {
            auto mod = parseModule();
            if (mod) decls.push_back(std::move(mod));
        } else if (checkIdent("fn")) {
            advance();
            std::string fname = advance().text;
            auto fn = parseFunction("auto", 0, fname);
            if (fn) decls.push_back(std::move(fn));
        } else {
            bool is_type = isTypeName(peek().text);
            if (!is_type && peek().kind == TOK_IDENT) {
                if (peek(1).kind == TOK_IDENT) {
                    is_type = true;
                } else if (peek(1).kind == TOK_STAR) {
                    size_t off = 2;
                    while (peek(off).kind == TOK_STAR) off++;
                    if (peek(off).kind == TOK_IDENT) is_type = true;
                }
            }
            if (is_type) {
                std::string type_name = advance().text;
                int ptr_depth = 0;
                while (match(TOK_STAR)) ptr_depth++;

                if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN) {
                    std::string fname = advance().text;
                    auto fn = parseFunction(type_name, ptr_depth, fname);
                    if (fn) decls.push_back(std::move(fn));
                } else if (peek().kind == TOK_IDENT) {
                    auto stmt = parseVarDecl(type_name, ptr_depth, false);
                    if (stmt) stmts.push_back(std::move(stmt));
                } else {
                    reportError(peek().loc(filename), "Expected identifier after type '" + type_name + "'", "Provide variable or function name");
                    synchronize();
                }
            } else {
                auto stmt = parseStatement();
                if (stmt) stmts.push_back(std::move(stmt));
            }
        }
    }

    if (!match(TOK_RBRACE)) {
        reportError(peek().loc(filename), "Expected '}' at end of namespace", "Close namespace with '}'");
    }
    match(TOK_SEMI);

    return std::make_unique<NamespaceDeclAST>(loc, ns_name, std::move(decls), std::move(stmts));
}

std::unique_ptr<UsingDeclAST> GrassParser::parseUsing() {
    auto loc = advance().loc(filename); // consume 'using'
    bool is_ns = false;
    if (peek().kind == TOK_NAMESPACE || checkIdent("namespace")) {
        advance();
        is_ns = true;
    }
    if (peek().kind != TOK_IDENT) {
        reportError(peek().loc(filename), "Expected identifier after 'using'", "Specify namespace or symbol");
        synchronize();
        return nullptr;
    }
    std::string path = advance().text;
    while (match(TOK_COLON_COLON)) {
        if (peek().kind == TOK_IDENT) {
            path += "::" + advance().text;
        } else {
            reportError(peek().loc(filename), "Expected identifier after '::'", "Provide identifier");
            break;
        }
    }
    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after using declaration", "Terminate with ';'");
    }
    return std::make_unique<UsingDeclAST>(loc, path, is_ns);
}

std::unique_ptr<ProgramAST> GrassParser::parseProgram() {
    auto program = std::make_unique<ProgramAST>(SourceLocation(filename, 1, 1));

    while (!isAtEnd()) {
        if (peek().kind == TOK_SEMI) {
            advance();
            continue;
        }
        if (peek().kind == TOK_RBRACE) {
            reportError(peek().loc(filename), "Unexpected closing brace '}'", "Check matching braces");
            advance();
            continue;
        }
        if (peek().kind == TOK_LIB) {
            program->included_libs.push_back(advance().text);
            continue;
        }
        if (peek().kind == TOK_IDENT && peek().text == "@strict") {
            program->is_strict = true;
            advance();
            continue;
        }

        try {
            if (peek().kind == TOK_NAMESPACE || checkIdent("namespace")) {
                auto ns = parseNamespace();
                if (ns) program->declarations.push_back(std::move(ns));
            } else if (peek().kind == TOK_USING || checkIdent("using")) {
                auto u = parseUsing();
                if (u) program->declarations.push_back(std::move(u));
            } else if (checkIdent("struct")) {
                auto st = parseStruct();
                if (st) program->declarations.push_back(std::move(st));
            } else if (checkIdent("class")) {
                auto cls = parseClass();
                if (cls) program->declarations.push_back(std::move(cls));
            } else if (checkIdent("module")) {
                auto mod = parseModule();
                if (mod) program->declarations.push_back(std::move(mod));
            } else if (checkIdent("fn")) {
                advance();
                std::string fname = advance().text;
                auto fn = parseFunction("auto", 0, fname);
                if (fn) program->declarations.push_back(std::move(fn));
            } else {
                bool is_type = isTypeName(peek().text);
                if (!is_type && peek().kind == TOK_IDENT) {
                    if (peek(1).kind == TOK_IDENT) {
                        is_type = true;
                    } else if (peek(1).kind == TOK_STAR) {
                        size_t off = 2;
                        while (peek(off).kind == TOK_STAR) off++;
                        if (peek(off).kind == TOK_IDENT) is_type = true;
                    }
                }
                if (is_type) {
                    std::string type_name = advance().text;
                    int ptr_depth = 0;
                    while (match(TOK_STAR)) ptr_depth++;

                    if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LPAREN) {
                        std::string fname = advance().text;
                        auto fn = parseFunction(type_name, ptr_depth, fname);
                        if (fn) program->declarations.push_back(std::move(fn));
                    } else if (peek().kind == TOK_IDENT) {
                        auto stmt = parseVarDecl(type_name, ptr_depth, false);
                        if (stmt) program->statements.push_back(std::move(stmt));
                    } else {
                        reportError(peek().loc(filename), "Expected identifier after type '" + type_name + "'", "Provide variable or function name");
                        synchronize();
                    }
                } else {
                    auto stmt = parseStatement();
                    if (stmt) program->statements.push_back(std::move(stmt));
                }
            }
        } catch (...) {
            synchronize();
        }
    }

    program->diagnostics = diagnostics;
    return program;
}
