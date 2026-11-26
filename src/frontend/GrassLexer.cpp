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
            "header-color", "window-border", "border-radius", "active-color",
            "hover-color", "button-color", "click-switch", "click-synth",
            "click-sound", "hover-synth", "hover-sound", "header-align",
            "clock-format", "scroll-box", "font-weight", "time-format",
            "header-bar", "center-all", "font-style", "text-color",
            "fill-color", "font-size", "window-ob", "align-all",
            "on-change", "bar-color", "tab-color", "bg-color",
            "add-item", "click-ob", "on-click", "on-timer",
            "size-top", "z-index"
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
    tokens.push_back(GrassToken(TOK_EOF, "", 0.0, line, get_col(pos), filename));
    for (auto& t : tokens) {
        if (t.file.empty()) t.file = filename;
    }
    return tokens;
}

