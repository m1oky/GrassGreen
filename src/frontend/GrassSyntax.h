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
#include <memory>

#include "vm/GrassArena.h"

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
    OP_DEF_VAR       = 0x06,
    OP_NOP           = 0x0E,

    OP_ADD           = 0x10,
    OP_SUB           = 0x11,
    OP_MUL           = 0x12,
    OP_DIV           = 0x13,
    OP_NEG           = 0x14,
    OP_MOD           = 0x15,
    OP_BIT_AND       = 0x16,
    OP_BIT_OR        = 0x17,
    OP_BIT_XOR       = 0x18,
    OP_BIT_NOT       = 0x19,
    OP_BIT_SHL       = 0x1A,
    OP_BIT_SHR       = 0x1B,

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
    OP_ADDR_VAR      = 0x88,
    OP_DEREF         = 0x89,
    OP_STORE_DEREF   = 0x8A,
    OP_ADDR_INDEX    = 0x8B,
    OP_ADDR_FIELD    = 0x8C,

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
struct GrassVMPtr;

struct GrassValue {
    uint64_t bits;

    // ── Константы NaN-boxing ──
    static constexpr uint64_t TAG_PTR    = 0x7FFA000000000000ULL;
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
    static GrassValue Ptr(GrassVMPtr* p) {
        GrassValue g;
        g.bits = TAG_PTR | ((uint64_t)(uintptr_t)p & PTR_MASK);
        return g;
    }
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
    bool is_ptr()    const { return (bits & 0xFFFF000000000000ULL) == TAG_PTR; }
    bool is_str()    const { return (bits & 0xFFFF000000000000ULL) == TAG_STR; }
    bool is_string() const { return is_str(); }
    bool is_bool()   const { return (bits & 0xFFFF000000000000ULL) == TAG_BOOL; }
    bool is_array()  const { return (bits & 0xFFFF000000000000ULL) == TAG_ARRAY; }
    bool is_struct() const { return (bits & 0xFFFF000000000000ULL) == TAG_STRUCT; }
    bool is_num()    const {
        uint64_t t = bits & 0xFFFF000000000000ULL;
        return t != TAG_PTR && t != TAG_NIL && t != TAG_BOOL && t != TAG_STR && t != TAG_ARRAY && t != TAG_STRUCT;
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

    GrassVMPtr* asPtr() const {
        return (GrassVMPtr*)(uintptr_t)(bits & PTR_MASK);
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
        if (is_ptr()) return (double)(uintptr_t)asPtr();
        if (is_nil() || is_array() || is_struct()) return 0.0;
        double d;
        std::memcpy(&d, &bits, 8);
        return d;
    }

    std::string toString() const;
    bool isTruthy() const;
};

struct GrassVMPtr {
    GrassValue* cell = nullptr;
    std::vector<GrassValue>* vec = nullptr;
    int64_t vec_idx = 0;
    uint8_t* raw = nullptr;
    size_t raw_size = 0;
    int64_t raw_off = 0;
    GrassStructInstance* struct_inst = nullptr;
    int64_t elem_size = 8;
    bool is_scalar_u8 = false;
    uint8_t scalar_kind = 0; // 0=raw/byte, 1=U8, 2=U16, 3=U32, 4=INT, 5=FLOAT, 6=DOUBLE, 7=U64, 8=BOOL, 9=STRING
};

// ── Определение структур и массивов GrassGreen 4.0 ──
struct GrassArray {
    std::vector<GrassValue> elements;
    bool is_vec = false;
    size_t vec_capacity = 0;
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
    std::string struct_name;
};

struct GrassStructDef {
    std::string name;
    std::vector<GrassFieldDef> fields;
    std::unordered_map<std::string, size_t> field_indices;
    std::vector<std::string> methods;
    size_t total_size = 0;
    size_t alignment = 1;
    bool has_destructor = false;
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
    if (is_ptr()) {
        GrassVMPtr* p = asPtr();
        if (!p) return "0x0";
        std::ostringstream ss;
        ss << "0x" << std::hex << (uintptr_t)p;
        return ss.str();
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
    if (is_ptr())  return asPtr() != nullptr;
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
    TOK_TILDE,
    TOK_COLON_COLON,
    TOK_ARROW,
    TOK_NAMESPACE,
    TOK_USING,
    TOK_AS,
    TOK_SIZEOF,
    TOK_NULLPTR
};

struct SourceLocation {
    std::string file;
    int line = 1;
    int col = 1;

    SourceLocation() = default;
    SourceLocation(const std::string& f, int l, int c) : file(f), line(l), col(c) {}
    SourceLocation(int l, int c) : file(""), line(l), col(c) {}

    std::string toString() const {
        if (file.empty()) return std::to_string(line) + ":" + std::to_string(col);
        return file + ":" + std::to_string(line) + ":" + std::to_string(col);
    }
};

enum class DiagnosticSeverity {
    Error,
    Warning,
    Note
};

struct SyntaxDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceLocation loc;
    std::string message;
    std::string hint;

    SyntaxDiagnostic() = default;
    SyntaxDiagnostic(DiagnosticSeverity sev, const SourceLocation& l, const std::string& msg, const std::string& h = "")
        : severity(sev), loc(l), message(msg), hint(h) {}

    std::string format() const {
        std::string s = "[";
        if (severity == DiagnosticSeverity::Error) s += "Error";
        else if (severity == DiagnosticSeverity::Warning) s += "Warning";
        else s += "Note";
        s += "] " + loc.toString() + ": " + message;
        if (!hint.empty()) {
            s += " (hint: " + hint + ")";
        }
        return s;
    }
};

struct GrassToken {
    GrassTokenKind kind;
    std::string text;
    double num_val;
    int line;
    int col;

    GrassToken(GrassTokenKind k = TOK_EOF, const std::string& t = "", double n = 0.0, int l = 1, int c = 1)
        : kind(k), text(t), num_val(n), line(l), col(c) {}

    SourceLocation loc(const std::string& fname = "") const {
        return SourceLocation(fname, line, col);
    }
};

class Lexer {
    std::string src;
    std::string filename;
    size_t pos = 0;
    int line = 1;
    size_t line_start_pos = 0;
    bool at_line_start = true;

    static bool isIdentStart(unsigned char c);
    static bool isIdentCont(unsigned char c);

public:
    explicit Lexer(const std::string& source, const std::string& fname = "");
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
    bool in_stream_io = false;
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
    std::unordered_map<std::string, std::vector<std::string>> struct_methods;
    std::unordered_map<std::string, std::vector<std::string>> method_to_structs;
    std::unordered_map<std::string, std::string> var_struct_types;
    std::string current_struct_context;
    std::vector<std::pair<size_t, std::string>> method_call_fixups;
    std::string current_ns_;
    std::vector<std::string> using_namespaces_;
    std::unordered_map<std::string, std::string> using_symbols_;

    struct VMScopeVar {
        std::string name;
        std::string struct_type;
    };
    struct VMScopeFrame {
        std::vector<VMScopeVar> raii_vars;
        std::vector<std::pair<std::string, std::string>> shadowed_var_types;
        std::vector<std::string> declared_var_types;
    };
    std::vector<VMScopeFrame> vm_scopes_;
    std::vector<size_t> loop_scope_depths_;
    std::vector<size_t> continue_scope_depths_;
    int vm_temp_counter_ = 0;
    std::string current_dtor_struct_;
    std::unordered_map<std::string, bool> dtor_structs_;
    std::unordered_map<std::string, std::string> var_vec_types_;

    void pushVMScope();
    void popVMScope(bool emit_cleanups = true);
    void registerVMVarType(const std::string& var_name, const std::string& struct_name);
    void registerVMRAIIVar(const std::string& var_name, const std::string& struct_name);
    void emitVMDtorCall(const std::string& var_name, const std::string& struct_name);
    void emitVMScopesCleanupTo(size_t target_depth);
    void emitVMFieldDtors(const std::string& struct_name);
    bool isVMVecTypeStart(int offset = 0);
    std::string parseVMVecInnerType();
    void emitVMVecElemDtorLoop(const std::string& var_name, const std::string& elem_struct);
    void emitVMVecPopElemDtor(const std::string& var_name, const std::string& elem_struct);

    std::string resolveUserFunc(const std::string& name) const;

    GrassToken& peek(int offset = 0);
    GrassToken advance();
    bool match(GrassTokenKind kind);
    bool matchIdent(const std::string& name);
    bool consume(GrassTokenKind kind, const std::string& error_msg);

    void reportError(int line, const std::string& msg, int col = 1, const std::string& hint = "");
    void reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint = "");
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
    void parseBitwiseOr();
    void parseBitwiseXor();
    void parseBitwiseAnd();
    void parseComparison();
    void parseShift();
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
    std::vector<SyntaxDiagnostic> diagnostics;

    BytecodeCompiler(const std::vector<GrassToken>& toks, const std::string& fname);
    void compileProgram();
    const std::vector<SyntaxDiagnostic>& getDiagnostics() const { return diagnostics; }
};

// ============================================================================
// 5. ABSTRACT SYNTAX TREE (AST) AND PARSER WITH ERROR RECOVERY
// ============================================================================

enum class NodeKind : uint8_t {
    LiteralExpr,
    VariableExpr,
    BinaryExpr,
    UnaryExpr,
    PostfixExpr,
    CallExpr,
    MemberAccessExpr,
    IndexExpr,
    ArrayLiteralExpr,
    AllocExpr,
    FreeExpr,
    CastExpr,
    SizeofExpr,

    ExprStmt,
    VarDeclStmt,
    AssignStmt,
    BlockStmt,
    IfStmt,
    WhileStmt,
    ForStmt,
    ReturnStmt,
    BreakStmt,
    ContinueStmt,
    StreamIOStmt,

    FunctionDecl,
    StructDecl,
    ClassDecl,
    ModuleDecl,
    NamespaceDecl,
    UsingDecl,
    Program
};

struct ASTNode {
    NodeKind node_kind;
    SourceLocation loc;
    virtual ~ASTNode() = default;
    explicit ASTNode(NodeKind k, const SourceLocation& l = SourceLocation()) : node_kind(k), loc(l) {}
};

struct ExprAST : public ASTNode {
    explicit ExprAST(NodeKind k, const SourceLocation& l = SourceLocation()) : ASTNode(k, l) {}
};

enum class LiteralKind {
    Int,
    Float,
    String,
    Bool,
    Nil,
    Nullptr
};

struct LiteralExprAST : public ExprAST {
    LiteralKind lit_kind;
    std::string str_val;
    double num_val = 0.0;
    bool bool_val = false;

    LiteralExprAST(const SourceLocation& l, int64_t val)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::Int), str_val(std::to_string(val)), num_val((double)val) {}
    LiteralExprAST(const SourceLocation& l, double val)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::Float), str_val(std::to_string(val)), num_val(val) {}
    LiteralExprAST(const SourceLocation& l, const std::string& val)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::String), str_val(val) {}
    LiteralExprAST(const SourceLocation& l, bool val)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::Bool), bool_val(val), num_val(val ? 1.0 : 0.0) {}
    explicit LiteralExprAST(const SourceLocation& l)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::Nil) {}
    LiteralExprAST(const SourceLocation& l, std::nullptr_t)
        : ExprAST(NodeKind::LiteralExpr, l), lit_kind(LiteralKind::Nullptr), str_val("nullptr"), num_val(0.0) {}
};

struct CastExprAST : public ExprAST {
    std::string target_type;
    int target_pointer_depth = 0;
    std::unique_ptr<ExprAST> expr;
    bool is_c_style = false;

    CastExprAST(const SourceLocation& l, const std::string& ttype, int ptr_depth, std::unique_ptr<ExprAST> e, bool c_style = false)
        : ExprAST(NodeKind::CastExpr, l), target_type(ttype), target_pointer_depth(ptr_depth), expr(std::move(e)), is_c_style(c_style) {}
};

struct SizeofExprAST : public ExprAST {
    std::string type_name;
    int pointer_depth = 0;
    std::unique_ptr<ExprAST> expr;

    SizeofExprAST(const SourceLocation& l, const std::string& tname, int ptr_depth, std::unique_ptr<ExprAST> e = nullptr)
        : ExprAST(NodeKind::SizeofExpr, l), type_name(tname), pointer_depth(ptr_depth), expr(std::move(e)) {}
};

struct VariableExprAST : public ExprAST {
    std::string name;
    VariableExprAST(const SourceLocation& l, const std::string& n) : ExprAST(NodeKind::VariableExpr, l), name(n) {}
};

struct BinaryExprAST : public ExprAST {
    GrassTokenKind op;
    std::string op_str;
    std::unique_ptr<ExprAST> lhs;
    std::unique_ptr<ExprAST> rhs;

    BinaryExprAST(const SourceLocation& l, GrassTokenKind o, const std::string& os,
                  std::unique_ptr<ExprAST> left, std::unique_ptr<ExprAST> right)
        : ExprAST(NodeKind::BinaryExpr, l), op(o), op_str(os), lhs(std::move(left)), rhs(std::move(right)) {}
};

struct UnaryExprAST : public ExprAST {
    GrassTokenKind op;
    std::string op_str;
    std::unique_ptr<ExprAST> operand;
    bool is_prefix = true;

    UnaryExprAST(const SourceLocation& l, GrassTokenKind o, const std::string& os,
                 std::unique_ptr<ExprAST> opnd, bool prefix = true)
        : ExprAST(NodeKind::UnaryExpr, l), op(o), op_str(os), operand(std::move(opnd)), is_prefix(prefix) {}
};

struct PostfixExprAST : public ExprAST {
    GrassTokenKind op;
    std::string op_str;
    std::unique_ptr<ExprAST> operand;

    PostfixExprAST(const SourceLocation& l, GrassTokenKind o, const std::string& os,
                   std::unique_ptr<ExprAST> opnd)
        : ExprAST(NodeKind::PostfixExpr, l), op(o), op_str(os), operand(std::move(opnd)) {}
};

struct CallExprAST : public ExprAST {
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;

    CallExprAST(const SourceLocation& l, const std::string& name, std::vector<std::unique_ptr<ExprAST>> a)
        : ExprAST(NodeKind::CallExpr, l), callee(name), args(std::move(a)) {}
};

struct MemberAccessExprAST : public ExprAST {
    std::unique_ptr<ExprAST> object;
    std::string member;
    bool is_arrow = false;
    bool is_method_call = false;
    std::vector<std::unique_ptr<ExprAST>> method_args;

    MemberAccessExprAST(const SourceLocation& l, std::unique_ptr<ExprAST> obj, const std::string& mem, bool arrow = false)
        : ExprAST(NodeKind::MemberAccessExpr, l), object(std::move(obj)), member(mem), is_arrow(arrow) {}
};

struct IndexExprAST : public ExprAST {
    std::unique_ptr<ExprAST> target;
    std::unique_ptr<ExprAST> index;

    IndexExprAST(const SourceLocation& l, std::unique_ptr<ExprAST> t, std::unique_ptr<ExprAST> idx)
        : ExprAST(NodeKind::IndexExpr, l), target(std::move(t)), index(std::move(idx)) {}
};

struct ArrayLiteralExprAST : public ExprAST {
    std::vector<std::unique_ptr<ExprAST>> elements;

    ArrayLiteralExprAST(const SourceLocation& l, std::vector<std::unique_ptr<ExprAST>> elems)
        : ExprAST(NodeKind::ArrayLiteralExpr, l), elements(std::move(elems)) {}
};

struct AllocExprAST : public ExprAST {
    std::string type_name;
    std::unique_ptr<ExprAST> size_expr;

    AllocExprAST(const SourceLocation& l, const std::string& tname, std::unique_ptr<ExprAST> sz = nullptr)
        : ExprAST(NodeKind::AllocExpr, l), type_name(tname), size_expr(std::move(sz)) {}
};

struct FreeExprAST : public ExprAST {
    std::unique_ptr<ExprAST> target;

    FreeExprAST(const SourceLocation& l, std::unique_ptr<ExprAST> tgt)
        : ExprAST(NodeKind::FreeExpr, l), target(std::move(tgt)) {}
};

// Statements
struct StmtAST : public ASTNode {
    explicit StmtAST(NodeKind k, const SourceLocation& l = SourceLocation()) : ASTNode(k, l) {}
};

struct ExprStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> expr;

    ExprStmtAST(const SourceLocation& l, std::unique_ptr<ExprAST> e)
        : StmtAST(NodeKind::ExprStmt, l), expr(std::move(e)) {}
};

struct VarDeclStmtAST : public StmtAST {
    std::string type_name;
    std::string var_name;
    int pointer_depth = 0;
    bool is_const = false;
    std::unique_ptr<ExprAST> init_expr;

    VarDeclStmtAST(const SourceLocation& l, const std::string& tname, const std::string& vname,
                   int ptr_depth = 0, bool c = false, std::unique_ptr<ExprAST> init = nullptr)
        : StmtAST(NodeKind::VarDeclStmt, l), type_name(tname), var_name(vname), pointer_depth(ptr_depth), is_const(c), init_expr(std::move(init)) {}
};

struct AssignStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> target;
    GrassTokenKind op;
    std::string op_str;
    std::unique_ptr<ExprAST> value;

    AssignStmtAST(const SourceLocation& l, std::unique_ptr<ExprAST> tgt, GrassTokenKind o, const std::string& os, std::unique_ptr<ExprAST> val)
        : StmtAST(NodeKind::AssignStmt, l), target(std::move(tgt)), op(o), op_str(os), value(std::move(val)) {}
};

struct BlockStmtAST : public StmtAST {
    std::vector<std::unique_ptr<StmtAST>> statements;

    BlockStmtAST(const SourceLocation& l, std::vector<std::unique_ptr<StmtAST>> stmts = {})
        : StmtAST(NodeKind::BlockStmt, l), statements(std::move(stmts)) {}
};

struct IfStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> then_branch;
    std::unique_ptr<StmtAST> else_branch;

    IfStmtAST(const SourceLocation& l, std::unique_ptr<ExprAST> cond,
              std::unique_ptr<StmtAST> then_b, std::unique_ptr<StmtAST> else_b = nullptr)
        : StmtAST(NodeKind::IfStmt, l), condition(std::move(cond)), then_branch(std::move(then_b)), else_branch(std::move(else_b)) {}
};

struct WhileStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> body;

    WhileStmtAST(const SourceLocation& l, std::unique_ptr<ExprAST> cond, std::unique_ptr<StmtAST> b)
        : StmtAST(NodeKind::WhileStmt, l), condition(std::move(cond)), body(std::move(b)) {}
};

struct ForStmtAST : public StmtAST {
    std::unique_ptr<StmtAST> init;
    std::unique_ptr<ExprAST> condition;
    std::unique_ptr<StmtAST> step;
    std::unique_ptr<StmtAST> body;

    ForStmtAST(const SourceLocation& l, std::unique_ptr<StmtAST> i, std::unique_ptr<ExprAST> cond,
               std::unique_ptr<StmtAST> s, std::unique_ptr<StmtAST> b)
        : StmtAST(NodeKind::ForStmt, l), init(std::move(i)), condition(std::move(cond)), step(std::move(s)), body(std::move(b)) {}
};

struct ReturnStmtAST : public StmtAST {
    std::unique_ptr<ExprAST> value;

    ReturnStmtAST(const SourceLocation& l, std::unique_ptr<ExprAST> val = nullptr)
        : StmtAST(NodeKind::ReturnStmt, l), value(std::move(val)) {}
};

struct BreakStmtAST : public StmtAST {
    explicit BreakStmtAST(const SourceLocation& l) : StmtAST(NodeKind::BreakStmt, l) {}
};

struct ContinueStmtAST : public StmtAST {
    explicit ContinueStmtAST(const SourceLocation& l) : StmtAST(NodeKind::ContinueStmt, l) {}
};

struct StreamIOStmtAST : public StmtAST {
    bool is_output = true;
    std::vector<std::unique_ptr<ExprAST>> operands;

    StreamIOStmtAST(const SourceLocation& l, bool out, std::vector<std::unique_ptr<ExprAST>> ops)
        : StmtAST(NodeKind::StreamIOStmt, l), is_output(out), operands(std::move(ops)) {}
};

// Declarations
struct ParamDecl {
    std::string type_name;
    std::string name;
    int pointer_depth = 0;
    SourceLocation loc;

    ParamDecl(const std::string& t, const std::string& n, int ptr, const SourceLocation& l)
        : type_name(t), name(n), pointer_depth(ptr), loc(l) {}
};

struct FieldDecl {
    std::string type_name;
    std::string name;
    int pointer_depth = 0;
    SourceLocation loc;

    FieldDecl(const std::string& t, const std::string& n, int ptr, const SourceLocation& l)
        : type_name(t), name(n), pointer_depth(ptr), loc(l) {}
};

struct DeclAST : public ASTNode {
    explicit DeclAST(NodeKind k, const SourceLocation& l = SourceLocation()) : ASTNode(k, l) {}
};

struct FunctionDeclAST : public DeclAST {
    std::string return_type;
    int return_pointer_depth = 0;
    std::string name;
    std::vector<ParamDecl> params;
    std::unique_ptr<BlockStmtAST> body;
    bool is_method = false;
    bool is_virtual = false;
    bool is_destructor = false;

    FunctionDeclAST(const SourceLocation& l, const std::string& ret_type, int ret_ptr,
                    const std::string& n, std::vector<ParamDecl> p,
                    std::unique_ptr<BlockStmtAST> b, bool method = false, bool virt = false,
                    bool dtor = false)
        : DeclAST(NodeKind::FunctionDecl, l), return_type(ret_type), return_pointer_depth(ret_ptr),
          name(n), params(std::move(p)), body(std::move(b)),
          is_method(method), is_virtual(virt), is_destructor(dtor) {}
};

struct StructDeclAST : public DeclAST {
    std::string name;
    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;

    StructDeclAST(const SourceLocation& l, const std::string& n,
                  std::vector<FieldDecl> f, std::vector<std::unique_ptr<FunctionDeclAST>> m)
        : DeclAST(NodeKind::StructDecl, l), name(n), fields(std::move(f)), methods(std::move(m)) {}
};

struct ClassDeclAST : public DeclAST {
    std::string name;
    std::string base_class;
    std::vector<FieldDecl> fields;
    std::vector<std::unique_ptr<FunctionDeclAST>> methods;

    ClassDeclAST(const SourceLocation& l, const std::string& n, const std::string& base,
                 std::vector<FieldDecl> f, std::vector<std::unique_ptr<FunctionDeclAST>> m)
        : DeclAST(NodeKind::ClassDecl, l), name(n), base_class(base), fields(std::move(f)), methods(std::move(m)) {}
};

struct ModuleDeclAST : public DeclAST {
    std::string name;
    std::vector<std::unique_ptr<FunctionDeclAST>> functions;
    std::vector<std::unique_ptr<StructDeclAST>> structs;
    std::vector<std::unique_ptr<ClassDeclAST>> classes;

    ModuleDeclAST(const SourceLocation& l, const std::string& n)
        : DeclAST(NodeKind::ModuleDecl, l), name(n) {}
};

struct NamespaceDeclAST : public DeclAST {
    std::string name;
    std::vector<std::unique_ptr<DeclAST>> declarations;
    std::vector<std::unique_ptr<StmtAST>> statements;

    NamespaceDeclAST(const SourceLocation& l, const std::string& n,
                     std::vector<std::unique_ptr<DeclAST>> d,
                     std::vector<std::unique_ptr<StmtAST>> s = {})
        : DeclAST(NodeKind::NamespaceDecl, l), name(n), declarations(std::move(d)), statements(std::move(s)) {}
};

struct UsingDeclAST : public DeclAST {
    std::string path;
    bool is_namespace = true;

    UsingDeclAST(const SourceLocation& l, const std::string& p, bool ns)
        : DeclAST(NodeKind::UsingDecl, l), path(p), is_namespace(ns) {}
};

struct ProgramAST : public ASTNode {
    std::vector<std::string> included_libs;
    std::vector<std::unique_ptr<DeclAST>> declarations;
    std::vector<std::unique_ptr<StmtAST>> statements;
    std::vector<SyntaxDiagnostic> diagnostics;
    bool is_strict = false;

    explicit ProgramAST(const SourceLocation& l = SourceLocation()) : ASTNode(NodeKind::Program, l) {}
};

// ============================================================================
// PARSER WITH ERROR RECOVERY
// ============================================================================

class GrassParser {
private:
    std::vector<GrassToken> tokens;
    size_t cur = 0;
    std::string filename;
    std::vector<SyntaxDiagnostic> diagnostics;
    std::unordered_set<std::string> custom_types;
    int error_count = 0;
    int parser_depth_ = 0;
    static constexpr int MAX_ERRORS = 100;
    static constexpr int MAX_PARSER_DEPTH = 512;

    const GrassToken& peek(int offset = 0) const;
    GrassToken advance();
    bool match(GrassTokenKind kind);
    bool matchIdent(const std::string& name);
    bool check(GrassTokenKind kind) const;
    bool checkIdent(const std::string& name) const;
    bool isAtEnd() const;

    void reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint = "");
    void synchronize();

    // Expressions
    std::unique_ptr<ExprAST> parseExpression();
    std::unique_ptr<ExprAST> parseAssignment();
    std::unique_ptr<ExprAST> parseLogicalOr();
    std::unique_ptr<ExprAST> parseLogicalAnd();
    std::unique_ptr<ExprAST> parseBitwiseOr();
    std::unique_ptr<ExprAST> parseBitwiseXor();
    std::unique_ptr<ExprAST> parseBitwiseAnd();
    std::unique_ptr<ExprAST> parseEquality();
    std::unique_ptr<ExprAST> parseRelational();
    std::unique_ptr<ExprAST> parseShift();
    std::unique_ptr<ExprAST> parseAdditive();
    std::unique_ptr<ExprAST> parseMultiplicative();
    std::unique_ptr<ExprAST> parseUnary();
    std::unique_ptr<ExprAST> parsePostfix();
    std::unique_ptr<ExprAST> parsePrimary();

    // Statements & Declarations
    std::unique_ptr<StmtAST> parseStatement();
    std::unique_ptr<BlockStmtAST> parseBlock();
    std::unique_ptr<StmtAST> parseVarDecl(const std::string& type_name, int ptr_depth, bool is_const);
    std::unique_ptr<StmtAST> parseIf();
    std::unique_ptr<StmtAST> parseWhile();
    std::unique_ptr<StmtAST> parseFor();
    std::unique_ptr<StmtAST> parseReturn();
    std::unique_ptr<StmtAST> parseStreamIO(bool is_output);

    std::unique_ptr<DeclAST> parseDeclaration();
    std::unique_ptr<FunctionDeclAST> parseFunction(const std::string& ret_type, int ret_ptr, const std::string& name);
    std::unique_ptr<StructDeclAST> parseStruct();
    std::unique_ptr<ClassDeclAST> parseClass();
    std::unique_ptr<ModuleDeclAST> parseModule();
    std::unique_ptr<NamespaceDeclAST> parseNamespace();
    std::unique_ptr<UsingDeclAST> parseUsing();

    bool isTypeName(const std::string& s) const;
    bool isTypeStart(size_t offset = 0) const;
    bool parseTypeSpec(std::string& out_type, int& out_ptr_depth);
    static bool isVarKeyword(const std::string& s);

public:
    explicit GrassParser(const std::vector<GrassToken>& toks, const std::string& fname = "");
    std::unique_ptr<ProgramAST> parseProgram();
    const std::vector<SyntaxDiagnostic>& getDiagnostics() const { return diagnostics; }
    bool hasErrors() const { return !diagnostics.empty(); }
};

#endif // GRASS_SYNTAX_H
