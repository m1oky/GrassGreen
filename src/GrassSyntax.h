#ifndef GRASS_SYNTAX_H
#define GRASS_SYNTAX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cmath>
#include <sstream>

// 1. НАБОР ДВОИЧНЫХ ИНСТРУКЦИЙ (OPCODES)
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
    OP_UI_CMD        = 0x73,

    OP_JUMP          = 0xF0,
    OP_CALL_USER     = 0xF1
};

// 2. УНИВЕРСАЛЬНЫЙ ТИП ДАННЫХ ВМ (GRASS VALUE)
struct GrassValue {
    bool is_string = false;
    double num = 0.0;
    std::string str = "";

    static GrassValue Num(double v) { GrassValue g; g.is_string = false; g.num = v; return g; }
    static GrassValue Str(const std::string& s) { GrassValue g; g.is_string = true; g.str = s; return g; }

    std::string toString() const {
        if (is_string) return str;
        if (std::floor(num) == num && std::fabs(num) < 1e15) {
            return std::to_string((long long)num);
        }
        std::stringstream ss;
        ss << num;
        return ss.str();
    }

    double toNumber() const {
        if (!is_string) return num;
        try { return std::stod(str); } catch (...) { return 0.0; }
    }

    bool isTruthy() const {
        if (is_string) return !str.empty() && str != "0" && str != "false";
        return num != 0.0;
    }
};

// 3. ТОКЕНЫ И ЛЕКСЕР
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
    std::string text;
    double num_val;
    int line;

    GrassToken(GrassTokenKind k = TOK_EOF, const std::string& t = "", double n = 0.0, int l = 1)
        : kind(k), text(t), num_val(n), line(l) {}
};

class Lexer {
    std::string src;
    size_t pos = 0;
    int line = 1;
    bool at_line_start = true;

    static bool isIdentStart(unsigned char c);
    static bool isIdentCont(unsigned char c);

public:
    explicit Lexer(const std::string& source);
    std::vector<GrassToken> tokenize();
};

// 4. КОМПИЛЯТОР СИНТАКСИСА В БАЙТ-КОД (С ПРОВЕРКОЙ БИБЛИОТЕК И INT MAIN)
class BytecodeCompiler {
    std::vector<GrassToken> tokens;
    size_t cur = 0;
    std::string source_filename;
    int loop_counter = 0;
    bool inside_function = false;
    bool is_special_file = false; // Для чистых .gdb или !GrassWeb! файлов

    std::unordered_set<std::string> included_libs;
    std::vector<std::vector<size_t>> break_Fixups;
    std::unordered_map<std::string, uint32_t> func_table;

    struct UserFunction {
        uint32_t entry_ip;
        std::vector<std::string> param_names;
    };
    std::unordered_map<std::string, UserFunction> user_functions;

    GrassToken& peek(int offset = 0);
    GrassToken advance();
    bool match(GrassTokenKind kind);
    bool matchIdent(const std::string& name);

    void reportError(int line, const std::string& msg);
    bool requireLib(const std::string& lib_key, const std::string& display_name, const std::string& feature_desc, int line);
    void checkBuiltinLibraryRequirement(const std::string& fn, int line);

    static bool isTypeOrFuncKeyword(const std::string& s);
    static bool isVarKeyword(const std::string& s);

    void emitByte(uint8_t b);
    void emitU32(uint32_t v);
    void patchU32(size_t offset, uint32_t v);
    void emitDouble(double d);
    void emitString(const std::string& s);
    void emitUICmd(const std::string& cmd_line);

    void parseExpression();
    void parseLogicalOr();
    void parseLogicalAnd();
    void parseComparison();
    void parseTerm();
    void parseFactor();
    void parseUnary();
    void parsePrimary();

    std::string extractParenArgsAsString();
    bool parseSingleGuiDirective();
    void parseWindowBlock();
    void parseBlock();
    void parseStepStatement();
    void parseStatement();

public:
    std::vector<uint8_t> code;
    bool has_error = false;

    BytecodeCompiler(const std::vector<GrassToken>& toks, const std::string& fname);
    void compileProgram();
};

#endif // GRASS_SYNTAX_H