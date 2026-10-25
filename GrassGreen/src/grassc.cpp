#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <windows.h>

#include "FastMath.h"
#include "CoreMath.h"
#include "GrassLib.h"
#include "GrassWeb.h"
#include "GrassMemory.h"
#include "GrassRandom.h"

using namespace std;
namespace fs = std::filesystem;

// ============================================================================
// 1. НАБОР ДВОИЧНЫХ ИНСТРУКЦИЙ (OPCODES) ВИРТУАЛЬНОГО ПРОЦЕССОРА GRASSGREEN
// ============================================================================
enum OpCode : uint8_t {
    OP_HALT          = 0x00,
    OP_CONST_NUM     = 0x01,
    OP_CONST_STR     = 0x02,
    OP_LOAD_VAR      = 0x03,
    OP_STORE_VAR     = 0x04,
    OP_POP           = 0x05,

    OP_ADD           = 0x10,
    OP_SUB           = 0x11,
    OP_MUL           = 0x12,
    OP_DIV           = 0x13,
    OP_NEG           = 0x14,
    OP_MOD           = 0x15,

    OP_CMP_EQ        = 0x20,
    OP_CMP_NEQ       = 0x21,
    OP_CMP_LT        = 0x22,
    OP_CMP_GT        = 0x23,
    OP_CMP_LTE       = 0x24,
    OP_CMP_GTE       = 0x25,
    OP_AND           = 0x26,
    OP_OR            = 0x27,
    OP_NOT           = 0x28,

    OP_JMP           = 0x30,
    OP_JMP_IF_FALSE  = 0x31,
    OP_CALL_FUNC     = 0x32,
    OP_RET           = 0x33,

    OP_LOG           = 0x40,
    OP_LOG_EMPTY     = 0x41,
    OP_WAIT          = 0x42,
    OP_ASK           = 0x43,
    OP_PRINT_RAW     = 0x44,

    OP_CALL_BUILTIN  = 0x50,
    OP_LOAD_DB       = 0x60,
    OP_START_WEB     = 0x61,

    OP_UI_INIT       = 0x70,
    OP_UI_ADD_ELEM   = 0x71,
    OP_UI_SHOW       = 0x72,

    OP_JUMP          = 0xF0,
    OP_CALL_USER     = 0xF1
};

// ============================================================================
// 2. УНИВЕРСАЛЬНЫЙ ТИП ДАННЫХ ВМ (GRASS VALUE)
// ============================================================================
struct GrassValue {
    bool is_string = false;
    double num = 0.0;
    string str = "";

    static GrassValue Num(double v) { GrassValue g; g.is_string = false; g.num = v; return g; }
    static GrassValue Str(const string& s) { GrassValue g; g.is_string = true; g.str = s; return g; }

    string toString() const {
        if (is_string) return str;
        if (std::floor(num) == num && std::fabs(num) < 1e15) {
            return to_string((long long)num);
        }
        stringstream ss;
        ss << num;
        return ss.str();
    }

    double toNumber() const {
        if (!is_string) return num;
        try { return stod(str); } catch (...) { return 0.0; }
    }

    bool isTruthy() const {
        if (is_string) return !str.empty() && str != "0" && str != "false";
        return num != 0.0;
    }
};

// ============================================================================
// 3. ЛЕКСЕР (ПОДДЕРЖКА C++ ОПЕРАТОРОВ, UTF-8, #HEX И БИБЛИОТЕК)
// ============================================================================
enum GrassTokenKind {
    TOK_EOF,
    TOK_IDENT,
    TOK_NUMBER,
    TOK_STRING,
    TOK_LIB,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACKET,
    TOK_RBRACKET,
    TOK_COMMA,
    TOK_SEMI,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_MUL_ASSIGN,
    TOK_DIV_ASSIGN,
    TOK_INC,
    TOK_DEC,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_MOD,
    TOK_EQ,
    TOK_NEQ,
    TOK_LT,
    TOK_GT,
    TOK_LTE,
    TOK_GTE,
    TOK_AND,
    TOK_OR,
    TOK_NOT,
    TOK_SHL,
    TOK_SHR
};

struct GrassToken {
    GrassTokenKind kind;
    string text;
    double num_val;
    int line;

    GrassToken(GrassTokenKind k = TOK_EOF, const string& t = "", double n = 0.0, int l = 1)
        : kind(k), text(t), num_val(n), line(l) {}
};

class Lexer {
    string src;
    size_t pos = 0;
    int line = 1;
    bool at_line_start = true;

    static bool isIdentStart(unsigned char c) {
        return isalpha(c) || c == '_' || c >= 128;
    }

    static bool isIdentCont(unsigned char c) {
        return isalnum(c) || c == '_' || c == '.' || c >= 128;
    }

public:
    explicit Lexer(const string& source) : src(source) {}

    vector<GrassToken> tokenize() {
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

            // Однострочный комментарий * в начале строки или //
            if ((c == '*' && at_line_start) || (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/')) {
                while (pos < src.size() && src[pos] != '\n') pos++;
                continue;
            }

            // Директива @text_off и #include
            if (c == '@' || (c == '#' && pos + 7 < src.size() && src.substr(pos, 8) == "#include")) {
                while (pos < src.size() && src[pos] != '\n') pos++;
                continue;
            }

            // Hex-цвета без кавычек (#00ffcc)
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

            // Числа
            if (isdigit((unsigned char)c)) {
                size_t start = pos;
                bool has_dot = false;
                while (pos < src.size() && (isdigit((unsigned char)src[pos]) || (src[pos] == '.' && !has_dot))) {
                    if (src[pos] == '.') has_dot = true;
                    pos++;
                }
                string num_str = src.substr(start, pos - start);
                double val = 0.0;
                try { val = stod(num_str); } catch (...) {}
                tokens.push_back(GrassToken(TOK_NUMBER, num_str, val, line));
                continue;
            }

            // Идентификаторы и ключевые слова (включая input.getline)
            if (isIdentStart((unsigned char)c)) {
                size_t start = pos;
                while (pos < src.size()) {
                    unsigned char cur_c = (unsigned char)src[pos];
                    if (isIdentCont(cur_c)) {
                        pos++;
                    } else if (cur_c == '-' && pos + 1 < src.size() && isIdentStart((unsigned char)src[pos + 1])) {
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
};

// ============================================================================
// 4. КОМПИЛЯТОР В ДВОИЧНЫЙ БАЙТ-КОД (C++ SYNTAX + GRASSGREEN ENGINE)
// ============================================================================
class BytecodeCompiler {
    vector<GrassToken> tokens;
    size_t cur = 0;
    string source_filename;
    int loop_counter = 0;
    vector<vector<size_t>> break_Fixups;
    unordered_map<string, uint32_t> func_table;

    struct UserFunction {
        uint32_t entry_ip;
        vector<string> param_names;
    };
    unordered_map<string, UserFunction> user_functions;

    GrassToken& peek(int offset = 0) {
        if (cur + offset >= tokens.size()) return tokens.back();
        return tokens[cur + offset];
    }

    GrassToken advance() {
        GrassToken t = peek();
        if (cur < tokens.size() - 1) cur++;
        return t;
    }

    bool match(GrassTokenKind kind) {
        if (peek().kind == kind) { advance(); return true; }
        return false;
    }

    bool matchIdent(const string& name) {
        if (peek().kind == TOK_IDENT && peek().text == name) { advance(); return true; }
        return false;
    }

    static bool isTypeOrFuncKeyword(const string& s) {
        return s == "take" || s == "void" || s == "func" || s == "alt" || s == "fn" ||
               s == "int" || s == "float" || s == "double" || s == "num" ||
               s == "str" || s == "string" || s == "bool" || s == "auto";
    }

    static bool isVarKeyword(const string& s) {
        return s == "set" || s == "var" || s == "let" || s == "fany" || s == "invent" ||
               s == "int" || s == "float" || s == "double" || s == "num" ||
               s == "str" || s == "string" || s == "bool" || s == "auto";
    }

public:
    vector<uint8_t> code;

    BytecodeCompiler(const vector<GrassToken>& toks, const string& fname)
        : tokens(toks), source_filename(fname) {}

    void emitByte(uint8_t b) { code.push_back(b); }

    void emitU32(uint32_t v) {
        uint8_t bytes[4];
        memcpy(bytes, &v, 4);
        for (int i = 0; i < 4; ++i) code.push_back(bytes[i]);
    }

    void patchU32(size_t offset, uint32_t v) {
        if (offset + 4 <= code.size()) {
            memcpy(&code[offset], &v, 4);
        }
    }

    void emitDouble(double d) {
        uint8_t bytes[8];
        memcpy(bytes, &d, 8);
        for (int i = 0; i < 8; ++i) code.push_back(bytes[i]);
    }

    void emitString(const string& s) {
        emitU32((uint32_t)s.size());
        for (char c : s) code.push_back((uint8_t)c);
    }

    void parseExpression() { parseLogicalOr(); }

    void parseLogicalOr() {
        parseLogicalAnd();
        while (match(TOK_OR) || matchIdent("or")) {
            parseLogicalAnd();
            emitByte(OP_OR);
        }
    }

    void parseLogicalAnd() {
        parseComparison();
        while (match(TOK_AND) || matchIdent("and")) {
            parseComparison();
            emitByte(OP_AND);
        }
    }

    void parseComparison() {
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

    void parseTerm() {
        parseFactor();
        while (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS) {
            GrassTokenKind op = advance().kind;
            parseFactor();
            emitByte(op == TOK_PLUS ? OP_ADD : OP_SUB);
        }
    }

    void parseFactor() {
        parseUnary();
        while (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD) {
            GrassTokenKind op = advance().kind;
            parseUnary();
            if (op == TOK_STAR) emitByte(OP_MUL);
            else if (op == TOK_SLASH) emitByte(OP_DIV);
            else emitByte(OP_MOD);
        }
    }

    void parseUnary() {
        if (match(TOK_MINUS)) {
            parseUnary();
            emitByte(OP_NEG);
            return;
        }
        if (match(TOK_NOT)) {
            parseUnary();
            emitByte(OP_NOT);
            return;
        }
        if (match(TOK_PLUS)) {
            parseUnary();
            return;
        }
        parsePrimary();
    }

    void parsePrimary() {
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

                emitByte(OP_CALL_BUILTIN);
                emitString(name);
                emitByte(argc);
                return;
            }

            // Постфиксный инкремент/декремент в выражении (i++, i--)
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

    string extractParenStringOrIdent() {
        bool has_paren = match(TOK_LPAREN);
        match(TOK_ASSIGN);
        string res;
        if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
            res = advance().text;
        }
        if (has_paren) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
                advance();
            }
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        return res;
    }

    int extractParenInt() {
        bool has_paren = match(TOK_LPAREN);
        match(TOK_ASSIGN);
        bool neg = match(TOK_MINUS);
        int val = 0;
        if (peek().kind == TOK_NUMBER) val = (int)advance().num_val;
        if (neg) val = -val;
        if (has_paren) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
                advance();
            }
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        return val;
    }

    void parseWindowBlock() {
        if (peek().kind == TOK_IDENT || peek().kind == TOK_STRING) advance();
        if (match(TOK_LPAREN)) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) advance();
            match(TOK_RPAREN);
        }
        match(TOK_LBRACE);

        string win_title = "GrassGreen Engine v3.0";
        int win_w = 640, win_h = 480;
        string win_bg = "#0f0f14", win_fon = "";

        struct ElemDef {
            string type, id, text, color = "#00ffcc", fstyle = "Segoe UI", run = "";
            int x = 0, y = 0, w = 200, h = 45, align = 0, fsize = 18;
            bool glass = false;
        };
        vector<ElemDef> elems;

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (match(TOK_SEMI)) continue;

            if (matchIdent("title") || matchIdent("name")) {
                win_title = extractParenStringOrIdent();
            } else if (matchIdent("size")) {
                match(TOK_LPAREN);
                if (peek().kind == TOK_NUMBER) win_w = (int)advance().num_val;
                if (match(TOK_COMMA) && peek().kind == TOK_NUMBER) {
                    win_h = (int)advance().num_val;
                }
                match(TOK_RPAREN);
                match(TOK_SEMI);
            } else if (matchIdent("size-top") || matchIdent("height")) {
                win_h = extractParenInt();
            } else if (matchIdent("color") || matchIdent("bg") || matchIdent("background")) {
                win_bg = extractParenStringOrIdent();
            } else if (matchIdent("fon")) {
                win_fon = extractParenStringOrIdent();
            } else if (matchIdent("show")) {
                if (match(TOK_LPAREN)) match(TOK_RPAREN);
                match(TOK_SEMI);
            } else if (peek().kind == TOK_IDENT &&
                      (peek().text == "label" || peek().text == "button" || peek().text == "enter")) {
                ElemDef el;
                el.type = advance().text;
                el.w = (el.type == "label") ? 500 : 220;

                if (peek().kind == TOK_IDENT && peek(1).kind == TOK_ASSIGN) {
                    el.id = advance().text;
                }
                match(TOK_ASSIGN);

                if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
                    el.text = advance().text;
                }
                if (match(TOK_LPAREN)) {
                    if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
                        el.text = advance().text;
                    }
                    match(TOK_RPAREN);
                }

                if (match(TOK_LBRACE)) {
                    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                        if (match(TOK_SEMI)) continue;

                        if (matchIdent("id") || matchIdent("name_el")) {
                            el.id = extractParenStringOrIdent();
                        } else if (matchIdent("text")) {
                            el.text = extractParenStringOrIdent();
                        } else if (matchIdent("window-ob") || matchIdent("pos") || matchIdent("position")) {
                            match(TOK_LPAREN);
                            if (peek().kind == TOK_NUMBER) el.x = (int)advance().num_val;
                            match(TOK_COMMA);
                            if (peek().kind == TOK_NUMBER) el.y = (int)advance().num_val;
                            match(TOK_RPAREN);
                            match(TOK_SEMI);
                        } else if (matchIdent("size")) {
                            match(TOK_LPAREN);
                            if (peek().kind == TOK_NUMBER) el.w = (int)advance().num_val;
                            if (match(TOK_COMMA) && peek().kind == TOK_NUMBER) el.h = (int)advance().num_val;
                            match(TOK_RPAREN);
                            match(TOK_SEMI);
                        } else if (matchIdent("size-top") || matchIdent("height")) {
                            el.h = extractParenInt();
                        } else if (matchIdent("middle") || matchIdent("center")) {
                            if (match(TOK_LPAREN)) match(TOK_RPAREN);
                            match(TOK_SEMI);
                            el.align = 1;
                        } else if (matchIdent("left")) {
                            if (match(TOK_LPAREN)) match(TOK_RPAREN);
                            match(TOK_SEMI);
                            el.align = 2;
                        } else if (matchIdent("right")) {
                            if (match(TOK_LPAREN)) match(TOK_RPAREN);
                            match(TOK_SEMI);
                            el.align = 3;
                        } else if (matchIdent("align")) {
                            string a = extractParenStringOrIdent();
                            if (a == "center" || a == "middle") el.align = 1;
                            else if (a == "left") el.align = 2;
                            else if (a == "right") el.align = 3;
                        } else if (matchIdent("text-color") || matchIdent("text_color") ||
                                   matchIdent("button-color") || matchIdent("color")) {
                            el.color = extractParenStringOrIdent();
                        } else if (matchIdent("font-size") || matchIdent("font_size")) {
                            el.fsize = extractParenInt();
                        } else if (matchIdent("font-style") || matchIdent("font_style")) {
                            el.fstyle = extractParenStringOrIdent();
                        } else if (matchIdent("glass")) {
                            if (match(TOK_LPAREN)) match(TOK_RPAREN);
                            match(TOK_SEMI);
                            el.glass = true;
                        } else if (matchIdent("run") || matchIdent("action")) {
                            el.run = extractParenStringOrIdent();
                        } else {
                            advance();
                        }
                    }
                    match(TOK_RBRACE);
                }
                match(TOK_SEMI);
                elems.push_back(el);
            } else {
                advance();
            }
        }
        match(TOK_RBRACE);

        emitByte(OP_UI_INIT);
        emitString(win_title);
        emitU32((uint32_t)win_w);
        emitU32((uint32_t)win_h);
        emitString(win_bg);
        emitString(win_fon);

        for (const auto& el : elems) {
            emitByte(OP_UI_ADD_ELEM);
            emitString(el.type);
            emitString(el.id);
            emitString(el.text);
            emitU32((uint32_t)el.x);
            emitU32((uint32_t)el.y);
            emitU32((uint32_t)el.w);
            emitU32((uint32_t)el.h);
            emitU32((uint32_t)el.align);
            emitString(el.color);
            emitU32((uint32_t)el.fsize);
            emitString(el.fstyle);
            emitByte(el.glass ? 1 : 0);
            emitString(el.run);
        }

        emitByte(OP_UI_SHOW);
    }

    void parseBlock() {
        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                parseStatement();
            }
            match(TOK_RBRACE);
        } else {
            parseStatement();
        }
    }

    void parseStepStatement() {
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

    void parseStatement() {
        if (match(TOK_SEMI)) return;
        if (peek().kind == TOK_RBRACE || peek().kind == TOK_EOF) return;

        // Вложенный блок { ... }
        if (peek().kind == TOK_LBRACE) {
            parseBlock();
            return;
        }

        // Команда /n; для пустой строки
        if (peek().kind == TOK_SLASH && peek(1).kind == TOK_IDENT && peek(1).text == "n") {
            advance(); advance();
            match(TOK_SEMI);
            emitByte(OP_LOG_EMPTY);
            return;
        }

        // Команда text; для пустой строки
        if (peek().kind == TOK_IDENT && peek().text == "text" && peek(1).kind == TOK_SEMI) {
            advance(); advance();
            emitByte(OP_LOG_EMPTY);
            return;
        }

        // Подключение библиотек !database!, !GrassWeb!, !memory!, !random!
        if (peek().kind == TOK_LIB) {
            string lib = advance().text;
            if (lib == "!database!" || lib == "!gdb!") {
                emitByte(OP_LOAD_DB);
            } else if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
                emitByte(OP_START_WEB);
                emitString(source_filename);
                emitByte(OP_HALT);
            }
            return;
        }

        if (matchIdent("use") || matchIdent("import") || matchIdent("include") || matchIdent("using")) {
            while (peek().kind != TOK_SEMI && peek().kind != TOK_EOF) advance();
            match(TOK_SEMI);
            return;
        }

        // 1. ОБЪЯВЛЕНИЕ ФУНКЦИЙ (включая int main() {}, take my_func() {}, void my_func() {})
        if (peek().kind == TOK_IDENT && isTypeOrFuncKeyword(peek().text) &&
            peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
            advance(); // Пропускаем take / void / int / func / alt
            string fn_name = advance().text;
            vector<string> params;

            match(TOK_LPAREN);
            if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                while (true) {
                    if (isTypeOrFuncKeyword(peek().text) && peek(1).kind == TOK_IDENT) {
                        advance(); // Пропускаем необязательный тип параметра (int a)
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

            parseBlock();

            emitByte(OP_CONST_NUM);
            emitDouble(0.0);
            emitByte(OP_RET);

            patchU32(skip_patch, (uint32_t)code.size());
            return;
        }

        // Оператор return
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

        // 2. Блок графического интерфейса
        if (matchIdent("window")) {
            parseWindowBlock();
            return;
        }

        // 3. Потоковый вывод C++: cout << "Привет" << endl;
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

        // 4. Вывод в консоль: log = ..., log(...), print(...), println(...)
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

        // 5. Ввод: input("Вопрос: ", var), input.getline("Вопрос: ", var), ask("Вопрос: ", var)
        if (peek().kind == TOK_IDENT &&
            (peek().text == "input" || peek().text == "input.getline" || peek().text == "ask")) {
            size_t saved_cur = cur;
            string fn_name = advance().text;
            string builtin_target = (fn_name == "ask") ? "input.getline" : fn_name;

            if (match(TOK_LPAREN)) {
                // Вариант 1: input(var);
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
                // Вариант 2: input("Текст вопроса: ", var);
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
                // Вариант 3: просто input("Нажми Enter...");
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

        // 6. Объявление переменных C++ и GrassGreen: int, string, float, bool, set, var, let
        if (peek().kind == TOK_IDENT && isVarKeyword(peek().text)) {
            advance();
            // Поддержка синтаксиса set = name;
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

        // 7. Условия: if, check, chek
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

        // 8. Цикл C++: for (int i = 0; i < 10; i++) { ... }
        if (matchIdent("for")) {
            match(TOK_LPAREN);
            parseStatement(); // init

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

        // 9. Цикл while(условие) { ... }
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

        // 10. Цикл again(N) { ... }
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

        // Выход из цикла: break, stop, stoped
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

        // Задержка wait(ms) / sleep(ms)
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

        // 11. Присваивания и инкременты: x = ..., x += ..., x++, x--
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

    void compileProgram() {
        while (peek().kind != TOK_EOF) {
            if (peek().kind == TOK_RBRACE) {
                advance();
                continue;
            }
            parseStatement();
        }
        // Автоматический запуск int main() {} или take main() {}, если функция объявлена
        if (user_functions.count("main")) {
            emitByte(OP_CALL_USER);
            emitU32(user_functions["main"].entry_ip);
            emitByte(OP_POP);
        }
        emitByte(OP_HALT);
    }
};

// ============================================================================
// 5. СТЕКОВАЯ ВИРТУАЛЬНАЯ МАШИНА GRASSGREEN (C++ STACK VM)
// ============================================================================
class GrassVM {
    const vector<uint8_t>& code;
    size_t ip = 0;
    vector<GrassValue> stack;
    vector<size_t> call_stack;
    unordered_map<string, GrassValue> vars;

    uint8_t readByte() {
        if (ip >= code.size()) return OP_HALT;
        return code[ip++];
    }

    uint32_t readU32() {
        if (ip + 4 > code.size()) { ip = code.size(); return 0; }
        uint32_t v;
        memcpy(&v, &code[ip], 4);
        ip += 4;
        return v;
    }

    double readDouble() {
        if (ip + 8 > code.size()) { ip = code.size(); return 0.0; }
        double d;
        memcpy(&d, &code[ip], 8);
        ip += 8;
        return d;
    }

    string readString() {
        uint32_t len = readU32();
        if (ip + len > code.size()) { ip = code.size(); return ""; }
        string s((const char*)&code[ip], len);
        ip += len;
        return s;
    }

    GrassValue pop() {
        if (stack.empty()) return GrassValue::Num(0.0);
        GrassValue v = stack.back();
        stack.pop_back();
        return v;
    }

    void push(const GrassValue& v) { stack.push_back(v); }

public:
    explicit GrassVM(const vector<uint8_t>& bytecode) : code(bytecode) {}

    void execute() {
        while (ip < code.size()) {
            if (GrassMemory::is_limit_exceeded()) {
                cerr << "\n[GrassGreen Error]: Превышен установленный лимит памяти (memory_limit)!\n";
                return;
            }
            uint8_t op = readByte();
            switch (op) {
                case OP_HALT: return;

                case OP_CONST_NUM: push(GrassValue::Num(readDouble())); break;
                case OP_CONST_STR: push(GrassValue::Str(readString())); break;

                case OP_JUMP:
                case OP_JMP: {
                    uint32_t target = readU32();
                    ip = target;
                    break;
                }

                case OP_JMP_IF_FALSE: {
                    uint32_t target = readU32();
                    GrassValue cond = pop();
                    if (!cond.isTruthy()) ip = target;
                    break;
                }

                case OP_CALL_USER:
                case OP_CALL_FUNC: {
                    uint32_t target_ip = readU32();
                    call_stack.push_back(ip);
                    ip = target_ip;
                    break;
                }

                case OP_RET: {
                    if (!call_stack.empty()) {
                        ip = call_stack.back();
                        call_stack.pop_back();
                    } else {
                        return;
                    }
                    break;
                }

                case OP_LOAD_VAR: {
                    string name = readString();
                    if (vars.count(name)) push(vars[name]);
                    else push(GrassValue::Str(""));
                    break;
                }
                case OP_STORE_VAR: {
                    string name = readString();
                    vars[name] = pop();
                    break;
                }
                case OP_POP: pop(); break;

                case OP_ADD: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string || b.is_string) push(GrassValue::Str(a.toString() + b.toString()));
                    else push(GrassValue::Num(a.num + b.num));
                    break;
                }
                case OP_SUB: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() - b.toNumber())); break; }
                case OP_MUL: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() * b.toNumber())); break; }
                case OP_DIV: {
                    GrassValue b = pop(), a = pop();
                    double div = b.toNumber();
                    push(GrassValue::Num(div != 0.0 ? a.toNumber() / div : 0.0));
                    break;
                }
                case OP_MOD: {
                    GrassValue b = pop(), a = pop();
                    long long bv = (long long)b.toNumber();
                    push(GrassValue::Num(bv != 0 ? (double)((long long)a.toNumber() % bv) : 0.0));
                    break;
                }
                case OP_NEG: { GrassValue a = pop(); push(GrassValue::Num(-a.toNumber())); break; }
                case OP_NOT: { GrassValue a = pop(); push(GrassValue::Num(a.isTruthy() ? 0.0 : 1.0)); break; }
                case OP_AND: { GrassValue b = pop(), a = pop(); push(GrassValue::Num((a.isTruthy() && b.isTruthy()) ? 1.0 : 0.0)); break; }
                case OP_OR:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num((a.isTruthy() || b.isTruthy()) ? 1.0 : 0.0)); break; }

                case OP_CMP_EQ: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string && b.is_string) push(GrassValue::Num(a.str == b.str ? 1.0 : 0.0));
                    else push(GrassValue::Num(a.toNumber() == b.toNumber() ? 1.0 : 0.0));
                    break;
                }
                case OP_CMP_NEQ: {
                    GrassValue b = pop(), a = pop();
                    if (a.is_string && b.is_string) push(GrassValue::Num(a.str != b.str ? 1.0 : 0.0));
                    else push(GrassValue::Num(a.toNumber() != b.toNumber() ? 1.0 : 0.0));
                    break;
                }
                case OP_CMP_LT:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() <  b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_GT:  { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() >  b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_LTE: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() <= b.toNumber() ? 1.0 : 0.0)); break; }
                case OP_CMP_GTE: { GrassValue b = pop(), a = pop(); push(GrassValue::Num(a.toNumber() >= b.toNumber() ? 1.0 : 0.0)); break; }

                case OP_LOG: {
                    GrassValue v = pop();
                    cout << v.toString() << endl;
                    break;
                }
                case OP_PRINT_RAW: {
                    GrassValue v = pop();
                    cout << v.toString();
                    break;
                }
                case OP_LOG_EMPTY: cout << endl; break;

                case OP_WAIT: {
                    int ms = (int)pop().toNumber();
                    if (ms > 0) Sleep(ms);
                    break;
                }
                case OP_ASK: {
                    string var_name = readString();
                    string input;
                    getline(cin, input);
                    vars[var_name] = GrassValue::Str(input);
                    break;
                }

                case OP_LOAD_DB: {
                    if (GrassStorageEngine_Load("storage.gdb")) {
                        for (const auto& kv : variables_cache) {
                            vars[kv.first] = GrassValue::Str(wstring_to_utf8(kv.second));
                        }
                    }
                    break;
                }

                case OP_START_WEB: {
                    string fname = readString();
                    GrassWeb_LoadPage(fname);
                    break;
                }

                case OP_CALL_BUILTIN: {
                    string fn = readString();
                    uint8_t argc = readByte();
                    vector<GrassValue> args(argc);
                    for (int i = argc - 1; i >= 0; --i) args[i] = pop();

                    // ===== ВВОД С КЛАВИАТУРЫ: input() и input.getline() =====
                    if (fn == "input") {
                        if (argc >= 1) { cout << args[0].toString(); cout.flush(); }
                        string s;
                        if (cin >> s) {
                            string rest;
                            getline(cin, rest);
                            push(GrassValue::Str(s));
                        } else {
                            cin.clear();
                            push(GrassValue::Str(""));
                        }
                    }
                    else if (fn == "input.getline" || fn == "getline" || fn == "ask") {
                        if (argc >= 1) { cout << args[0].toString(); cout.flush(); }
                        string s;
                        getline(cin, s);
                        push(GrassValue::Str(s));
                    }

                    // ===== СТРОКОВЫЕ ФУНКЦИИ C++ =====
                    else if ((fn == "len" || fn == "length" || fn == "size") && argc >= 1) {
                        push(GrassValue::Num((double)args[0].toString().size()));
                    }
                    else if (fn == "upper" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::toupper);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "lower" && argc >= 1) {
                        string s = args[0].toString();
                        transform(s.begin(), s.end(), s.begin(), ::tolower);
                        push(GrassValue::Str(s));
                    }
                    else if (fn == "substr" && argc >= 2) {
                        string s = args[0].toString();
                        size_t start = (size_t)max(0.0, args[1].toNumber());
                        if (start >= s.size()) push(GrassValue::Str(""));
                        else if (argc >= 3) push(GrassValue::Str(s.substr(start, (size_t)args[2].toNumber())));
                        else push(GrassValue::Str(s.substr(start)));
                    }
                    else if (fn == "replace" && argc >= 3) {
                        string s = args[0].toString();
                        string from = args[1].toString();
                        string to = args[2].toString();
                        if (!from.empty()) {
                            size_t start_pos = 0;
                            while ((start_pos = s.find(from, start_pos)) != string::npos) {
                                s.replace(start_pos, from.length(), to);
                                start_pos += to.length();
                            }
                        }
                        push(GrassValue::Str(s));
                    }
                    else if ((fn == "contains" || fn == "find") && argc >= 2) {
                        string s = args[0].toString();
                        string sub = args[1].toString();
                        push(GrassValue::Num(s.find(sub) != string::npos ? 1.0 : 0.0));
                    }
                    else if (fn == "trim" && argc >= 1) {
                        string s = args[0].toString();
                        size_t first = s.find_first_not_of(" \t\r\n");
                        if (first == string::npos) push(GrassValue::Str(""));
                        else push(GrassValue::Str(s.substr(first, s.find_last_not_of(" \t\r\n") - first + 1)));
                    }
                    else if ((fn == "to_str" || fn == "to_string") && argc >= 1) {
                        push(GrassValue::Str(args[0].toString()));
                    }
                    else if ((fn == "to_num" || fn == "stoi" || fn == "stod") && argc >= 1) {
                        push(GrassValue::Num(args[0].toNumber()));
                    }
                    else if (fn == "typeof" && argc >= 1) {
                        push(GrassValue::Str(args[0].is_string ? "string" : "number"));
                    }

                    // ===== КОНСОЛЬ, СИСТЕМА И ВРЕМЯ =====
                    else if (fn == "clear" || fn == "cls") {
                        system("cls");
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "color" && argc >= 1) {
                        string c = args[0].toString();
                        WORD attr = 7;
                        if (c == "green" || c == "2") attr = 10;
                        else if (c == "cyan" || c == "aqua" || c == "3") attr = 11;
                        else if (c == "red" || c == "4") attr = 12;
                        else if (c == "purple" || c == "magenta" || c == "5") attr = 13;
                        else if (c == "yellow" || c == "6") attr = 14;
                        else if (c == "white" || c == "7") attr = 15;
                        else if (c == "blue" || c == "1") attr = 9;
                        else if (c == "gray" || c == "8") attr = 8;
                        else attr = (WORD)args[0].toNumber();
                        SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "time") {
                        push(GrassValue::Num((double)std::time(nullptr)));
                    }
                    else if (fn == "clock_ms") {
                        auto now = chrono::steady_clock::now().time_since_epoch();
                        push(GrassValue::Num((double)chrono::duration_cast<chrono::milliseconds>(now).count()));
                    }
                    else if (fn == "beep") {
                        int freq = (argc >= 1) ? (int)args[0].toNumber() : 750;
                        int dur  = (argc >= 2) ? (int)args[1].toNumber() : 300;
                        Beep(freq, dur);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "msgbox" && argc >= 1) {
                        string title = (argc >= 2) ? args[0].toString() : "GrassGreen";
                        string msg   = (argc >= 2) ? args[1].toString() : args[0].toString();
                        MessageBoxA(NULL, msg.c_str(), title.c_str(), MB_OK | MB_ICONINFORMATION);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "system" && argc >= 1) {
                        int r = system(args[0].toString().c_str());
                        push(GrassValue::Num((double)r));
                    }

                    // ===== БИБЛИОТЕКА !random! =====
                    else if (fn == "random") {
                        double min_v = 0.0, max_v = 100.0;
                        if (argc == 1) {
                            max_v = args[0].toNumber();
                        } else if (argc >= 2) {
                            min_v = args[0].toNumber();
                            max_v = args[1].toNumber();
                        }
                        push(GrassValue::Num(GrassRandom::get_random(min_v, max_v)));
                    }
                    else if (fn == "random_txt") {
                        string lang = (argc >= 1) ? args[0].toString() : "en";
                        int len = (argc >= 2) ? (int)args[1].toNumber() : 1;
                        push(GrassValue::Str(GrassRandom::get_random_txt(lang, len)));
                    }
                    else if (fn == "stop_random") {
                        GrassRandom::stop_random();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "play_random") {
                        GrassRandom::play_random();
                        push(GrassValue::Num(1.0));
                    }

                    // ===== БИБЛИОТЕКА !memory! =====
                    else if (fn == "memory_usit") {
                        push(GrassValue::Num(GrassMemory::usit()));
                    }
                    else if (fn == "memory_separ") {
                        GrassMemory::separ();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_auto") {
                        GrassMemory::set_auto(true);
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_clean") {
                        GrassMemory::clean();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_free") {
                        push(GrassValue::Str(GrassMemory::get_system_free()));
                    }
                    else if (fn == "memory_alloc" && argc >= 1) {
                        int block_id = GrassMemory::alloc_block((size_t)args[0].toNumber());
                        push(GrassValue::Num((double)block_id));
                    }
                    else if (fn == "memory_free_block" && argc >= 1) {
                        bool ok = GrassMemory::free_block((int)args[0].toNumber());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if (fn == "memory_limit" && argc >= 1) {
                        GrassMemory::set_limit(args[0].toNumber());
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "memory_dump") {
                        vector<string> st_preview;
                        for (int i = (int)stack.size() - 1; i >= max(0, (int)stack.size() - 5); --i) {
                            st_preview.push_back(stack[i].toString());
                        }
                        vector<pair<string, string>> v_preview;
                        for (const auto& kv : vars) {
                            v_preview.push_back({kv.first, kv.second.toString()});
                        }
                        GrassMemory::dump(stack.size(), st_preview, v_preview);
                        push(GrassValue::Num(1.0));
                    }

                    // ===== БИБЛИОТЕКА !FastMath! И МАТЕМАТИКА =====
                    else if ((fn == "discor" || fn == "sqrt") && argc >= 1) push(GrassValue::Num(FastMath::discor(args[0].toNumber())));
                    else if (fn == "sin" && argc >= 1) push(GrassValue::Num(FastMath::sin(args[0].toNumber())));
                    else if (fn == "cos" && argc >= 1) push(GrassValue::Num(FastMath::cos(args[0].toNumber())));
                    else if (fn == "abs" && argc >= 1) push(GrassValue::Num(FastMath::abs(args[0].toNumber())));
                    else if (fn == "round" && argc >= 1) push(GrassValue::Num(FastMath::round(args[0].toNumber())));
                    else if (fn == "floor" && argc >= 1) push(GrassValue::Num(std::floor(args[0].toNumber())));
                    else if (fn == "ceil" && argc >= 1) push(GrassValue::Num(std::ceil(args[0].toNumber())));
                    else if (fn == "pow" && argc >= 2) push(GrassValue::Num(std::pow(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "clamp" && argc >= 3) push(GrassValue::Num(std::clamp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));
                    else if (fn == "min" && argc >= 2) push(GrassValue::Num(FastMath::min(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "max" && argc >= 2) push(GrassValue::Num(FastMath::max(args[0].toNumber(), args[1].toNumber())));
                    else if (fn == "rand" && argc >= 2) push(GrassValue::Num(FastMath::rand((int)args[0].toNumber(), (int)args[1].toNumber())));
                    else if (fn == "lerp" && argc >= 3) push(GrassValue::Num(FastMath::lerp(args[0].toNumber(), args[1].toNumber(), args[2].toNumber())));

                    // ===== БИБЛИОТЕКА !CoreMath! =====
                    else if (fn == "qb_init") push(GrassValue::Str(CoreMath::qb_init(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "h")       push(GrassValue::Str(CoreMath::h(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "x")       push(GrassValue::Str(CoreMath::x(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "cnot" && argc >= 2) push(GrassValue::Str(CoreMath::cnot(args[0].toString(), args[1].toString())));
                    else if (fn == "q_rand")  push(GrassValue::Num(CoreMath::q_rand(argc >= 1 ? args[0].toString() : "q0")));
                    else if (fn == "pi")      push(GrassValue::Str(CoreMath::pi()));
                    else if (fn == "gcd" && argc >= 2)   push(GrassValue::Num((double)CoreMath::gcd((long long)args[0].toNumber(), (long long)args[1].toNumber())));
                    else if (fn == "pow_e" && argc >= 2) push(GrassValue::Str(CoreMath::pow_e(args[0].toNumber(), args[1].toNumber())));

                    // ===== ФАЙЛОВАЯ СИСТЕМА И ОС =====
                    else if (fn == "creat" && argc >= 1) {
                        ofstream f(args[0].toString()); f.close();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "redact" && argc >= 2) {
                        ofstream f(args[0].toString()); f << args[1].toString(); f.close();
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "read" && argc >= 1) {
                        ifstream f(args[0].toString());
                        stringstream ss;
                        if (f.is_open()) ss << f.rdbuf();
                        push(GrassValue::Str(ss.str()));
                    }
                    else if ((fn == "exists" || fn == "connect") && argc >= 1) {
                        ifstream f(args[0].toString());
                        push(GrassValue::Num(f.is_open() ? 1.0 : 0.0));
                    }
                    else if (fn == "remove" && argc >= 1) {
                        bool ok = fs::remove(args[0].toString());
                        push(GrassValue::Num(ok ? 1.0 : 0.0));
                    }
                    else if ((fn == "open" || fn == "run") && argc >= 1) {
                        ShellExecuteA(NULL, "open", args[0].toString().c_str(), NULL, NULL, SW_SHOWNORMAL);
                        push(GrassValue::Num(1.0));
                    }

                    // ===== БАЗА ДАННЫХ GSE (storage.gdb) =====
                    else if (fn == "db_save" && argc >= 2) {
                        string k = args[0].toString();
                        string v = args[1].toString();
                        vars[k] = GrassValue::Str(v);
                        ofstream db("storage.gdb", ios::app);
                        if (db.is_open()) {
                            db << k << "=" << v << "\n";
                            db.close();
                        }
                        push(GrassValue::Num(1.0));
                    }
                    else if (fn == "db_get" && argc >= 1) {
                        string k = args[0].toString();
                        if (vars.count(k)) push(vars[k]);
                        else push(GrassValue::Str(""));
                    }
                    else if (vars.count(fn)) {
                        push(vars[fn]);
                    }
                    else {
                        push(GrassValue::Num(0.0));
                    }
                    break;
                }

                case OP_UI_INIT: {
                    HWND hConsole = GetConsoleWindow();
                    if (hConsole) ShowWindow(hConsole, SW_HIDE);

                    string title = readString();
                    int w = (int)readU32();
                    int h = (int)readU32();
                    string bg = readString();
                    string fon = readString();
                    if (vars.count(title)) title = vars[title].toString();
                    GrassGUI::InitWindow(title, w, h, bg, fon);
                    break;
                }
                case OP_UI_ADD_ELEM: {
                    string type = readString();
                    string id = readString();
                    string text = readString();
                    int x = (int)readU32();
                    int y = (int)readU32();
                    int w = (int)readU32();
                    int h = (int)readU32();
                    int align = (int)readU32();
                    string color = readString();
                    int fsize = (int)readU32();
                    string fstyle = readString();
                    bool glass = (readByte() != 0);
                    string run = readString();

                    if (vars.count(text)) text = vars[text].toString();

                    GrassGUI::AddElement(type, id, text, x, y, w, h, align, color, fsize, fstyle, glass, run);
                    break;
                }
                case OP_UI_SHOW: {
                    GrassGUI::Show();
                    break;
                }
            }
        }
    }
};

// ============================================================================
// 6. АВТО-УСТАНОВЩИК ЯЗЫКА, SVG-ИКОНКИ, ПОДСВЕТКИ И АВТОДОПОЛНЕНИЯ VS CODE
// ============================================================================
void InstallGrassGreenEcosystem() {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);
    fs::path exe_path(self_path);
    fs::path root_dir = exe_path.parent_path();
    if (root_dir.filename() == "bin") root_dir = root_dir.parent_path();

    cout << "\n=== [GrassGreen v3.0 Installer] ===\n";

    // 1. Создаём быстрые команды grass.cmd и gg.cmd
    {
        ofstream gcmd(root_dir / "grass.cmd");
        gcmd << "@echo off\n\"" << exe_path.string() << "\" ./! %*\n";
    }
    {
        ofstream ggcmd(root_dir / "gg.cmd");
        ggcmd << "@echo off\n\"" << exe_path.string() << "\" ./! %*\n";
    }
    cout << "[+] Созданы быстрые команды: grass <файл.grass> и gg <файл.grass>\n";

    // 2. Устанавливаем расширение в VS Code
    const char* userprofile = getenv("USERPROFILE");
    if (userprofile) {
        fs::path ext_dir = fs::path(userprofile) / ".vscode" / "extensions" / "m1oky.grassgreen-lang-3.0.0";
        fs::create_directories(ext_dir / "syntaxes");
        fs::create_directories(ext_dir / "snippets");

        // 2.1 Минималистичная векторная аватарка (logo.svg)
        ofstream svg(ext_dir / "logo.svg");
        svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">\n"
            << "  <defs>\n"
            << "    <linearGradient id=\"ggGrad\" x1=\"0%\" y1=\"100%\" x2=\"100%\" y2=\"0%\">\n"
            << "      <stop offset=\"0%\" stop-color=\"#00b359\"/>\n"
            << "      <stop offset=\"100%\" stop-color=\"#00ffcc\"/>\n"
            << "    </linearGradient>\n"
            << "  </defs>\n"
            << "  <rect x=\"8\" y=\"8\" width=\"112\" height=\"112\" rx=\"28\" fill=\"#0d1117\" stroke=\"url(#ggGrad)\" stroke-width=\"6\"/>\n"
            << "  <path d=\"M40 88 C40 52, 68 36, 88 28 C76 54, 72 72, 74 92\" fill=\"none\" stroke=\"url(#ggGrad)\" stroke-width=\"10\" stroke-linecap=\"round\"/>\n"
            << "  <path d=\"M36 92 C52 72, 72 64, 92 60\" fill=\"none\" stroke=\"#00ff88\" stroke-width=\"8\" stroke-linecap=\"round\"/>\n"
            << "  <circle cx=\"88\" cy=\"28\" r=\"6\" fill=\"#00ffcc\"/>\n"
            << "</svg>\n";
        svg.close();

        // Копия в корень проекта
        try {
            fs::copy_file(ext_dir / "logo.svg", root_dir / "logo.svg", fs::copy_options::overwrite_existing);
        } catch (...) {}

        // 2.2 Конфигурация скобок
        ofstream lconf(ext_dir / "language-configuration.json");
        lconf << "{\n"
              << "  \"comments\": { \"lineComment\": \"//\", \"blockComment\": [\"/*\", \"*/\"] },\n"
              << "  \"brackets\": [ [\"{\", \"}\"], [\"[\", \"]\"], [\"(\", \")\"] ],\n"
              << "  \"autoClosingPairs\": [\n"
              << "    { \"open\": \"{\", \"close\": \"}\" },\n"
              << "    { \"open\": \"[\", \"close\": \"]\" },\n"
              << "    { \"open\": \"(\", \"close\": \")\" },\n"
              << "    { \"open\": \"\\\"\", \"close\": \"\\\"\" },\n"
              << "    { \"open\": \"'\", \"close\": \"'\" },\n"
              << "    { \"open\": \"!\", \"close\": \"!\" }\n"
              << "  ]\n"
              << "}\n";
        lconf.close();

        // 2.3 Подсветка синтаксиса
        ofstream tm(ext_dir / "syntaxes" / "grassgreen.tmLanguage.json");
        tm << "{\n"
           << "  \"name\": \"GrassGreen\",\n"
           << "  \"scopeName\": \"source.grass\",\n"
           << "  \"patterns\": [\n"
           << "    { \"name\": \"comment.line.grass\", \"match\": \"(//.*$|^\\\\s*\\\\*.*$)\" },\n"
           << "    { \"name\": \"comment.block.grass\", \"begin\": \"/\\\\*|\\\\*\\\\*\", \"end\": \"\\\\*/|\\\\*\\\\*\" },\n"
           << "    { \"name\": \"entity.name.tag.library.grass\", \"match\": \"![A-Za-z0-9_]+!\" },\n"
           << "    { \"name\": \"keyword.control.directive.grass\", \"match\": \"@[A-Za-z0-9_]+\" },\n"
           << "    { \"name\": \"string.quoted.double.grass\", \"begin\": \"\\\"\", \"end\": \"\\\"\" },\n"
           << "    { \"name\": \"string.quoted.single.grass\", \"begin\": \"'\", \"end\": \"'\" },\n"
           << "    { \"name\": \"constant.other.color.grass\", \"match\": \"#[0-9a-fA-F]{3,8}\\\\b\" },\n"
           << "    { \"name\": \"keyword.control.grass\", \"match\": \"\\\\b(if|else|chek|check|for|while|again|loop|break|stop|stoped|return|exit|wait|sleep|window|cout|cin|endl|text)\\\\b\" },\n"
           << "    { \"name\": \"storage.type.grass\", \"match\": \"\\\\b(int|take|void|func|alt|fn|string|str|float|double|num|bool|auto|set|var|let|fany|invent)\\\\b\" },\n"
           << "    { \"name\": \"support.function.builtin.grass\", \"match\": \"\\\\b(main|input\\\\.getline|input|log|print|println|ask|random|random_txt|stop_random|play_random|memory_usit|memory_separ|memory_auto|memory_clean|memory_free|memory_alloc|memory_free_block|memory_limit|memory_dump|len|length|upper|lower|substr|replace|contains|trim|to_str|to_num|typeof|clear|cls|color|time|clock_ms|beep|msgbox|system|sqrt|discor|sin|cos|abs|round|floor|ceil|pow|clamp|min|max|rand|lerp|creat|redact|read|exists|remove|open|run|db_save|db_get)\\\\b\" },\n"
           << "    { \"name\": \"constant.numeric.grass\", \"match\": \"\\\\b\\\\d+(\\\\.\\\\d+)?\\\\b\" },\n"
           << "    { \"name\": \"constant.language.boolean.grass\", \"match\": \"\\\\b(true|false)\\\\b\" }\n"
           << "  ]\n"
           << "}\n";
        tm.close();

        // 2.4 Автодополнение (Snippets)
        ofstream snip(ext_dir / "snippets" / "grassgreen.json");
        snip << "{\n"
             << "  \"Main Entry Point\": {\n"
             << "    \"prefix\": [\"main\", \"int main\"],\n"
             << "    \"body\": [\"!GrassGreen!\", \"\", \"int main() {\", \"\\t$0\", \"\\treturn 0;\", \"}\"],\n"
             << "    \"description\": \"Главная функция int main() {}\"\n"
             << "  },\n"
             << "  \"Take Function\": {\n"
             << "    \"prefix\": \"take\",\n"
             << "    \"body\": [\"take ${1:func_name}(${2:args}) {\", \"\\t$0\", \"}\"],\n"
             << "    \"description\": \"Функция take (как void)\"\n"
             << "  },\n"
             << "  \"Input Word\": {\n"
             << "    \"prefix\": \"input\",\n"
             << "    \"body\": \"input(\\\"${1:Введите значение: }\\\", ${2:var});\",\n"
             << "    \"description\": \"Ввод текста без пробелов\"\n"
             << "  },\n"
             << "  \"Input Getline\": {\n"
             << "    \"prefix\": \"input.getline\",\n"
             << "    \"body\": \"input.getline(\\\"${1:Введите текст: }\\\", ${2:var});\",\n"
             << "    \"description\": \"Ввод текста с пробелами\"\n"
             << "  },\n"
             << "  \"For Loop C++\": {\n"
             << "    \"prefix\": \"for\",\n"
             << "    \"body\": [\"for (int ${1:i} = 0; ${1:i} < ${2:10}; ${1:i}++) {\", \"\\t$0\", \"}\"],\n"
             << "    \"description\": \"Цикл for в стиле C++\"\n"
             << "  }\n"
             << "}\n";
        snip.close();

        // 2.5 Манифест расширения package.json с logo.svg
        ofstream pkg(ext_dir / "package.json");
        pkg << "{\n"
            << "  \"name\": \"grassgreen-lang\",\n"
            << "  \"displayName\": \"GrassGreen Official\",\n"
            << "  \"description\": \"Official language support for GrassGreen (.grass)\",\n"
            << "  \"version\": \"3.0.0\",\n"
            << "  \"publisher\": \"m1oky\",\n"
            << "  \"engines\": { \"vscode\": \"^1.60.0\" },\n"
            << "  \"contributes\": {\n"
            << "    \"languages\": [{\n"
            << "      \"id\": \"grassgreen\",\n"
            << "      \"aliases\": [\"GrassGreen\", \"grass\"],\n"
            << "      \"extensions\": [\".grass\"],\n"
            << "      \"configuration\": \"./language-configuration.json\",\n"
            << "      \"icon\": { \"light\": \"./logo.svg\", \"dark\": \"./logo.svg\" }\n"
            << "    }],\n"
            << "    \"grammars\": [{\n"
            << "      \"language\": \"grassgreen\",\n"
            << "      \"scopeName\": \"source.grass\",\n"
            << "      \"path\": \"./syntaxes/grassgreen.tmLanguage.json\"\n"
            << "    }],\n"
            << "    \"snippets\": [{\n"
            << "      \"language\": \"grassgreen\",\n"
            << "      \"path\": \"./snippets/grassgreen.json\"\n"
            << "    }]\n"
            << "  }\n"
            << "}\n";
        pkg.close();
        cout << "[+] Установлено расширение VS Code (иконка logo.svg + подсветка + сниппеты)!\n";
    }

    // 3. Локальные настройки .vscode для горячей клавиши Ctrl+Shift+B
    fs::create_directories(root_dir / ".vscode");
    {
        ofstream vset(root_dir / ".vscode" / "settings.json");
        vset << "{\n  \"files.associations\": { \"*.grass\": \"grassgreen\" }\n}\n";
    }
    {
        ofstream tasks(root_dir / ".vscode" / "tasks.json");
        string esc_exe = exe_path.string();
        string norm;
        for (char c : esc_exe) {
            if (c == '\\') norm += "\\\\";
            else norm += c;
        }
        tasks << "{\n"
              << "  \"version\": \"2.0.0\",\n"
              << "  \"tasks\": [\n"
              << "    {\n"
              << "      \"label\": \"Run GrassGreen Script\",\n"
              << "      \"type\": \"shell\",\n"
              << "      \"command\": \"" << norm << "\",\n"
              << "      \"args\": [\"./!\", \"${file}\"],\n"
              << "      \"group\": { \"kind\": \"build\", \"isDefault\": true },\n"
              << "      \"presentation\": { \"reveal\": \"always\", \"panel\": \"shared\" }\n"
              << "    }\n"
              << "  ]\n"
              << "}\n";
    }
    cout << "[+] Настроена сборка по Ctrl+Shift+B!\n";

    // 4. Ассоциация файлов .grass в Windows
    string reg_cmd1 = "reg add \"HKCU\\Software\\Classes\\.grass\" /ve /d \"GrassGreenFile\" /f >nul 2>&1";
    string reg_cmd2 = "reg add \"HKCU\\Software\\Classes\\GrassGreenFile\\shell\\open\\command\" /ve /d \"\\\"" + exe_path.string() + "\\\" ./! \\\"%1\\\"\" /f >nul 2>&1";
    system(reg_cmd1.c_str());
    system(reg_cmd2.c_str());
    cout << "[+] Файлы .grass привязаны к движку GrassGreen!\n";
    cout << "=== Установка завершена! ===\n\n";
}

// ============================================================================
// 7. УПАКОВЩИК ДВОИЧНОГО БАЙТ-КОДА В САМОДОСТАТОЧНЫЙ .EXE (PE PACKER)
// ============================================================================
static const char MAGIC_FOOTER[8] = {'G','R','A','S','S','E','X','E'};

bool CheckAndRunEmbeddedBytecode() {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    ifstream self(self_path, ios::binary | ios::ate);
    if (!self.is_open()) return false;

    streamsize total_size = self.tellg();
    if (total_size < 12) return false;

    self.seekg(total_size - 12);
    uint32_t bc_size = 0;
    char magic[8];
    self.read((char*)&bc_size, 4);
    self.read(magic, 8);

    if (memcmp(magic, MAGIC_FOOTER, 8) != 0) return false;
    if (bc_size == 0 || (streamsize)(bc_size + 12) > total_size) return false;

    self.seekg(total_size - 12 - bc_size);
    vector<uint8_t> bytecode(bc_size);
    self.read((char*)bytecode.data(), bc_size);
    self.close();

    GrassVM vm(bytecode);
    vm.execute();
    return true;
}

bool BuildStandaloneExe(const string& output_exe, const vector<uint8_t>& bytecode) {
    char self_path[MAX_PATH];
    GetModuleFileNameA(NULL, self_path, MAX_PATH);

    ifstream src(self_path, ios::binary | ios::ate);
    if (!src.is_open()) return false;

    streamsize stub_size = src.tellg();
    if (stub_size >= 12) {
        src.seekg(stub_size - 12);
        uint32_t old_bc = 0;
        char magic[8];
        src.read((char*)&old_bc, 4);
        src.read(magic, 8);
        if (memcmp(magic, MAGIC_FOOTER, 8) == 0 && (streamsize)(old_bc + 12) < stub_size) {
            stub_size -= (old_bc + 12);
        }
    }

    src.seekg(0);
    vector<char> pe_stub((size_t)stub_size);
    src.read(pe_stub.data(), stub_size);
    src.close();

    ofstream dst(output_exe, ios::binary | ios::trunc);
    if (!dst.is_open()) return false;

    dst.write(pe_stub.data(), pe_stub.size());
    dst.write((const char*)bytecode.data(), bytecode.size());

    uint32_t bc_size = (uint32_t)bytecode.size();
    dst.write((const char*)&bc_size, 4);
    dst.write(MAGIC_FOOTER, 8);
    dst.close();
    return true;
}

bool CompileAndRunFile(string input_path, bool build_only) {
    if (input_path.find('.') == string::npos) {
        ifstream test_ext(input_path + ".grass");
        if (test_ext.is_open()) input_path += ".grass";
    }

    ifstream in(input_path, ios::binary);
    if (!in.is_open()) {
        cerr << "[GrassError]: Файл не найден -> " << input_path << endl;
        return false;
    }
    stringstream buffer;
    buffer << in.rdbuf();
    string source_code = buffer.str();
    in.close();

    Lexer lexer(source_code);
    vector<GrassToken> tokens = lexer.tokenize();

    BytecodeCompiler compiler(tokens, input_path);
    compiler.compileProgram();

    string output_exe = input_path;
    size_t dot = output_exe.rfind('.');
    if (dot != string::npos) output_exe = output_exe.substr(0, dot) + ".exe";
    else output_exe += ".exe";

    if (BuildStandaloneExe(output_exe, compiler.code)) {
        cout << "[GrassCompiler]: Скомпилировано " << compiler.code.size()
             << " байт двоичного кода -> " << output_exe << endl;
    } else {
        cerr << "[GrassWarning]: Не удалось перезаписать " << output_exe
             << " (закрой запущенное окно " << output_exe << ")." << endl;
    }

    if (!build_only) {
        cout << "[GrassVM]: Исполнение двоичного кода...\n" << endl;
        GrassVM vm(compiler.code);
        vm.execute();
    }
    return true;
}

string FindDefaultGrassFile() {
    ifstream f1("demo.grass");
    if (f1.is_open()) return "demo.grass";
    ifstream f2("main.grass");
    if (f2.is_open()) return "main.grass";

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA("*.grass", &fd);
    if (hFind != INVALID_HANDLE_VALUE) {
        string found = fd.cFileName;
        FindClose(hFind);
        return found;
    }
    return "";
}

// ============================================================================
// 8. ТОЧКА ВХОДА И КОНСОЛЬ РАЗРАБОТЧИКА
// ============================================================================
int main(int argc, char* argv[]) {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD origInMode = 0;
    if (hIn != INVALID_HANDLE_VALUE) GetConsoleMode(hIn, &origInMode);

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    if (CheckAndRunEmbeddedBytecode()) {
        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
        return 0;
    }

    if (argc >= 2) {
        bool build_only = false;
        string target_file = "";

        for (int i = 1; i < argc; ++i) {
            string arg = argv[i];
            if (arg == "--install" || arg == "install" || arg == "-i") {
                InstallGrassGreenEcosystem();
                return 0;
            } else if (arg == "--build" || arg == "-c" || arg == ".//") {
                build_only = true;
            } else if (arg == "--run" || arg == "-r" || arg == "./!") {
                build_only = false;
            } else {
                target_file = arg;
            }
        }

        if (target_file.empty()) target_file = FindDefaultGrassFile();

        if (!target_file.empty()) {
            CompileAndRunFile(target_file, build_only);
        } else {
            cerr << "[GrassError]: Не найден ни один .grass файл в папке." << endl;
        }

        if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
        return 0;
    }

    cout << "========================================================\n";
    cout << "  GrassGreen Native Bytecode Compiler v3.0 (by m1oky)\n";
    cout << "========================================================\n";
    cout << "Команды:\n";
    cout << "  install           — Установить иконку, подсветку VS Code и команду grass\n";
    cout << "  .// <файл.grass>  — Скомпилировать в автономный .exe\n";
    cout << "  ./! <файл.grass>  — Скомпилировать и сразу запустить\n";
    cout << "  exit              — Выход\n";
    cout << "========================================================\n";

    string def_file = FindDefaultGrassFile();
    if (!def_file.empty()) {
        cout << "[Найдено в папке]: " << def_file << " (можно вводить просто .// или ./!)\n\n";
    }

    string line;
    while (true) {
        cout << "GrassGreen> ";
        if (!getline(cin, line)) break;

        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r\n") - first + 1);

        if (line == "exit" || line == "quit") break;
        if (line == "install" || line == "--install") {
            InstallGrassGreenEcosystem();
            continue;
        }

        stringstream ss(line);
        string cmd, file_arg;
        ss >> cmd;
        getline(ss, file_arg);
        size_t f_start = file_arg.find_first_not_of(" \t");
        if (f_start != string::npos) file_arg = file_arg.substr(f_start);
        else file_arg = "";

        if (cmd == ".//" || cmd == "compile" || cmd == "build" || cmd == "-c") {
            string target = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            if (target.empty()) cout << "Укажи имя файла: .// demo.grass\n";
            else CompileAndRunFile(target, true);
        } else if (cmd == "./!" || cmd == "run" || cmd == "-r") {
            string target = file_arg.empty() ? FindDefaultGrassFile() : file_arg;
            if (target.empty()) cout << "Укажи имя файла: ./! demo.grass\n";
            else CompileAndRunFile(target, false);
        } else {
            CompileAndRunFile(cmd, false);
        }
    }

    if (hIn != INVALID_HANDLE_VALUE) SetConsoleMode(hIn, origInMode);
    return 0;
}