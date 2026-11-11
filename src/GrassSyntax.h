#ifndef GRASS_SYNTAX_H
#define GRASS_SYNTAX_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <sstream>

#include "GrassArena.h"

// Глобальная арена для строк VM (определяется один раз в grassc.cpp)
extern GrassArena g_vm_arena;

// 1. НАБОР ДВОИЧНЫХ ИНСТРУКЦИЙ (OPCODES)
enum OpCode : uint8_t {
    OP_HALT          = 0x00,
    OP_CONST_NUM     = 0x01,
    OP_CONST_STR     = 0x02,
    OP_LOAD_VAR      = 0x03,
    OP_STORE_VAR     = 0x04,
    OP_POP           = 0x05,
    OP_NOP           = 0x0E,

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

    OP_NEW_ARRAY     = 0x80,
    OP_GET_INDEX     = 0x81,
    OP_SET_INDEX     = 0x82,
    OP_ARRAY_PUSH    = 0x83,
    OP_ARRAY_POP     = 0x84,
    OP_NEW_STRUCT    = 0x85,
    OP_GET_FIELD     = 0x86,
    OP_SET_FIELD     = 0x87,

    // ── Фьюз-инструкции и оптимизации (Bytecode Optimizer) ──
    OP_ADD_CONST     = 0x90,
    OP_SUB_CONST     = 0x91,
    OP_MUL_CONST     = 0x92,
    OP_DIV_CONST     = 0x93,
    OP_INC_VAR       = 0x94,
    OP_DEC_VAR       = 0x95,

    // ── Многопоточность (Multithreading & Async) ──
    OP_THREAD_SPAWN  = 0xA0,
    OP_ASYNC_SPAWN   = 0xA1,

    OP_JUMP          = 0xF0,
    OP_CALL_USER     = 0xF1,
    OP_PARAM_PROLOGUE = 0xF2,
    OP_PARAM_EPILOGUE = 0xF3
};

// 2. УНИВЕРСАЛЬНЫЙ ТИП ДАННЫХ ВМ (GRASS VALUE) — NaN-boxing (8 байт)
//
// Схема кодирования IEEE 754 NaN:
//   Обычный double:  любые 64 бит, НЕ попадающие в наши теги
//   QNAN   = 0x7FFC000000000000  (13 старших бит = quiet NaN marker)
//   STRUCT = 0x7FFB... | ptr48
//   NIL    = 0x7FFC... | 0
//   ARRAY  = 0x7FFD... | ptr48
//   BOOL   = 0x7FFE... | (0 или 1)
//   STR    = 0x7FFF... | ptr48
//
struct GrassArray;
struct GrassStructDef;
struct GrassStructInstance;

struct GrassValue {
    uint64_t bits;

    // ── Константы NaN-boxing ──
    static constexpr uint64_t QNAN       = 0x7FFC000000000000ULL;
    static constexpr uint64_t TAG_STRUCT = 0x7FFB000000000000ULL;
    static constexpr uint64_t TAG_NIL    = 0x7FFC000000000000ULL;
    static constexpr uint64_t TAG_ARRAY  = 0x7FFD000000000000ULL;
    static constexpr uint64_t TAG_BOOL   = 0x7FFE000000000000ULL;
    static constexpr uint64_t TAG_STR    = 0x7FFF000000000000ULL;
    static constexpr uint64_t PTR_MASK   = 0x0000FFFFFFFFFFFFULL; // 48-бит указатель

    // ── Конструктор (nil по умолчанию) ──
    GrassValue() : bits(TAG_NIL) {}

    // ── Фабрики ──
    static GrassValue Num(double v) {
        GrassValue g;
        std::memcpy(&g.bits, &v, 8);
        return g;
    }

    static GrassValue Str(const std::string& s) {
        GrassValue g;
        char* p = g_vm_arena.alloc_string(s.c_str(), s.size());
        g.bits = TAG_STR | ((uint64_t)(uintptr_t)p & PTR_MASK);
        return g;
    }

    static GrassValue Nil() {
        GrassValue g;
        g.bits = TAG_NIL;
        return g;
    }

    static GrassValue Bool(bool b) {
        GrassValue g;
        g.bits = TAG_BOOL | (b ? 1ULL : 0ULL);
        return g;
    }

    static GrassValue Array(GrassArray* a) {
        GrassValue g;
        g.bits = TAG_ARRAY | ((uint64_t)(uintptr_t)a & PTR_MASK);
        return g;
    }

    static GrassValue Struct(GrassStructInstance* s) {
        GrassValue g;
        g.bits = TAG_STRUCT | ((uint64_t)(uintptr_t)s & PTR_MASK);
        return g;
    }

    // ── Инспекция типа ──
    bool is_nil()    const { return bits == TAG_NIL; }
    bool is_str()    const { return (bits & 0xFFFF000000000000ULL) == TAG_STR; }
    bool is_string() const { return is_str(); }
    bool is_bool()   const { return (bits & 0xFFFF000000000000ULL) == TAG_BOOL; }
    bool is_array()  const { return (bits & 0xFFFF000000000000ULL) == TAG_ARRAY; }
    bool is_struct() const { return (bits & 0xFFFF000000000000ULL) == TAG_STRUCT; }
    bool is_num()    const {
        uint64_t t = bits & 0xFFFF000000000000ULL;
        return t != TAG_NIL && t != TAG_BOOL && t != TAG_STR && t != TAG_ARRAY && t != TAG_STRUCT;
    }
    bool isNumeric() const {
        if (is_num()) return true;
        if (is_str()) {
            const char* s = asCStr();
            if (!s || !*s) return false;
            if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
            if (!*s) return false;
            char* end = nullptr;
            (void)std::strtod(s, &end);
            if (end == s) return false;
            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
            return *end == '\0';
        }
        return false;
    }

    // ── Извлечение значений ──
    const char* asCStr() const {
        return (const char*)(uintptr_t)(bits & PTR_MASK);
    }

    GrassArray* asArray() const {
        return (GrassArray*)(uintptr_t)(bits & PTR_MASK);
    }

    GrassStructInstance* asStruct() const {
        return (GrassStructInstance*)(uintptr_t)(bits & PTR_MASK);
    }

    double toNumber() const {
        if (is_str()) {
            const char* s = asCStr();
            if (!s || !*s) return 0.0;
            if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) s += 3;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
            char* end = nullptr;
            double v = std::strtod(s, &end);
            return (end != s) ? v : 0.0;
        }
        if (is_bool()) return (bits & 1) ? 1.0 : 0.0;
        if (is_nil() || is_array() || is_struct()) return 0.0;
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }

    std::string toString() const;
    bool isTruthy() const;
};

// ── Определение структур и массивов GrassGreen 4.0 ──
struct GrassArray {
    std::vector<GrassValue> elements;
};

enum class GrassFieldType {
    U8,
    U16,
    U32,
    U64,
    INT,
    FLOAT,
    DOUBLE,
    BOOL,
    STRING,
    VALUE
};

struct GrassFieldDef {
    std::string name;
    GrassFieldType type;
    size_t offset;
    size_t size;
    size_t alignment;
};

struct GrassStructDef {
    std::string name;
    std::vector<GrassFieldDef> fields;
    std::unordered_map<std::string, size_t> field_indices;
    size_t total_size = 0;
    size_t alignment = 1;
};

struct GrassStructInstance {
    GrassStructDef* def = nullptr;
    uint8_t* data = nullptr; // память в g_vm_arena
};

inline std::string GrassValue::toString() const {
    if (is_str()) {
        const char* s = asCStr();
        return s ? std::string(s) : std::string();
    }
    if (is_nil())  return "nil";
    if (is_bool()) return (bits & 1) ? "true" : "false";
    if (is_array()) {
        GrassArray* arr = asArray();
        if (!arr) return "[]";
        std::string res = "[";
        for (size_t i = 0; i < arr->elements.size(); ++i) {
            if (i > 0) res += ", ";
            res += arr->elements[i].toString();
        }
        res += "]";
        return res;
    }
    if (is_struct()) {
        GrassStructInstance* st = asStruct();
        if (!st || !st->def) return "{struct}";
        return "{" + st->def->name + "}";
    }
    double d;
    std::memcpy(&d, &bits, 8);
    if (std::floor(d) == d && std::fabs(d) < 1e15) {
        return std::to_string((long long)d);
    }
    std::ostringstream ss;
    ss << d;
    return ss.str();
}

inline bool GrassValue::isTruthy() const {
    if (is_nil())  return false;
    if (is_bool()) return (bits & 1) != 0;
    if (is_array()) {
        GrassArray* arr = asArray();
        return arr && !arr->elements.empty();
    }
    if (is_struct()) return asStruct() != nullptr;
    if (is_str()) {
        const char* s = asCStr();
        if (!s || !*s) return false;
        if (s[0] == '0' && s[1] == '\0') return false;
        // "false"
        if (s[0]=='f'&&s[1]=='a'&&s[2]=='l'&&s[3]=='s'&&s[4]=='e'&&s[5]=='\0') return false;
        return true;
    }
    return toNumber() != 0.0;
}

extern std::unordered_map<std::string, GrassStructDef> g_struct_registry;

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
    TOK_SHR,
    TOK_DOT,
    TOK_COLON,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE
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
    int recursion_depth = 0;
    static constexpr int MAX_RECURSION_DEPTH = 256;

    std::unordered_set<std::string> included_libs;
    std::vector<std::vector<size_t>> break_Fixups;
    std::vector<std::vector<size_t>> continue_Fixups;
    std::unordered_map<std::string, uint32_t> func_table;

    struct FunctionContextGuard {
        bool& var;
        bool prev;
        FunctionContextGuard(bool& v, bool newVal) : var(v), prev(v) { var = newVal; }
        ~FunctionContextGuard() { var = prev; }
    };

    struct UserFunction {
        uint32_t entry_ip;
        std::vector<std::string> param_names;
    };
    std::unordered_map<std::string, UserFunction> user_functions;
    std::vector<std::pair<size_t, std::string>> thread_spawn_fixups;

    GrassToken& peek(int offset = 0);
    GrassToken advance();
    bool match(GrassTokenKind kind);
    bool matchIdent(const std::string& name);

    void reportError(int line, const std::string& msg);
    void synchronize();
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
    void parseStructDefinition();
    void parseVirtualStruct();
    void parseVFSBody();

public:
    std::vector<uint8_t> code;
    bool has_error = false;
    bool has_filesystem_struct = false;

    BytecodeCompiler(const std::vector<GrassToken>& toks, const std::string& fname);
    void compileProgram();
};

#endif // GRASS_SYNTAX_H
