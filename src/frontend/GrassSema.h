#ifndef GRASS_SEMA_H
#define GRASS_SEMA_H

#include "frontend/GrassSyntax.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <iostream>

namespace grass::sema {

enum class SemaTypeKind {
    Void,
    Int,
    Float,
    Bool,
    String,
    Pointer,
    Array,
    Vec,
    Struct,
    Unknown,
    Any
};

struct SemaType {
    SemaTypeKind kind = SemaTypeKind::Unknown;
    std::string name;
    int pointer_depth = 0;
    std::shared_ptr<SemaType> element_type = nullptr;

    SemaType() = default;
    SemaType(SemaTypeKind k, const std::string& n = "", int ptr = 0)
        : kind(k), name(n), pointer_depth(ptr) {}

    static const SemaType& Void() { static const SemaType t(SemaTypeKind::Void); return t; }
    static const SemaType& Int() { static const SemaType t(SemaTypeKind::Int); return t; }
    static const SemaType& Float() { static const SemaType t(SemaTypeKind::Float); return t; }
    static const SemaType& Bool() { static const SemaType t(SemaTypeKind::Bool); return t; }
    static const SemaType& String() { static const SemaType t(SemaTypeKind::String); return t; }
    static const SemaType& Any() { static const SemaType t(SemaTypeKind::Any); return t; }
    static const SemaType& Unknown() { static const SemaType t(SemaTypeKind::Unknown); return t; }
    static SemaType Ptr(const SemaType& base) {
        SemaType t(SemaTypeKind::Pointer, base.name, base.pointer_depth + 1);
        t.element_type = std::make_shared<SemaType>(base);
        return t;
    }
    static SemaType Nullptr() {
        SemaType t(SemaTypeKind::Pointer, "nullptr", 1);
        t.element_type = std::make_shared<SemaType>(SemaType::Void());
        return t;
    }
    static SemaType CustomStruct(const std::string& sname) {
        return SemaType(SemaTypeKind::Struct, sname);
    }
    static SemaType Vec(const SemaType& elem) {
        SemaType t(SemaTypeKind::Vec, "vec<" + elem.toString() + ">", 0);
        t.element_type = std::make_shared<SemaType>(elem);
        return t;
    }

    bool is_pointer() const { return kind == SemaTypeKind::Pointer || pointer_depth > 0; }
    bool is_numeric() const { return (kind == SemaTypeKind::Int || kind == SemaTypeKind::Float) && !is_pointer(); }
    bool is_integral() const { return kind == SemaTypeKind::Int && !is_pointer(); }
    bool is_bool() const { return kind == SemaTypeKind::Bool && !is_pointer(); }
    bool is_void() const { return kind == SemaTypeKind::Void && !is_pointer(); }
    bool is_string() const { return kind == SemaTypeKind::String && !is_pointer(); }
    bool is_vec() const { return kind == SemaTypeKind::Vec && !is_pointer(); }
    bool is_struct() const { return kind == SemaTypeKind::Struct && !is_pointer(); }
    bool is_unknown() const { return kind == SemaTypeKind::Unknown; }
    bool is_any() const { return kind == SemaTypeKind::Any; }

    bool equals(const SemaType& other) const {
        if (kind == SemaTypeKind::Any || other.kind == SemaTypeKind::Any) return true;
        if (kind == SemaTypeKind::Unknown || other.kind == SemaTypeKind::Unknown) return true;
        if (pointer_depth != other.pointer_depth) return false;
        if (kind != other.kind) return false;
        if (kind == SemaTypeKind::Struct) return name == other.name;
        if (kind == SemaTypeKind::Pointer || kind == SemaTypeKind::Vec) {
            if (element_type && other.element_type) return element_type->equals(*other.element_type);
            return name == other.name;
        }
        return true;
    }

    bool is_assignable_to(const SemaType& target, bool strict = false) const {
        if (strict) {
            if (target.is_pointer() && !is_pointer()) {
                if (kind == SemaTypeKind::Pointer && (name == "void" || name == "nullptr")) return true;
                if (kind == SemaTypeKind::Int || kind == SemaTypeKind::Any) return true;
                return false;
            }
            if (!target.is_pointer() && is_pointer()) {
                if (target.kind == SemaTypeKind::Int || target.kind == SemaTypeKind::Any) return true;
                return false;
            }
            if (target.is_pointer() && is_pointer()) {
                if (target.name == "void" || name == "void" || name == "nullptr" || equals(target)) return true;
                return false;
            }
            if (target.kind == SemaTypeKind::Float && kind == SemaTypeKind::Int) return true;
            if (kind == target.kind && pointer_depth == target.pointer_depth) return true;
            return equals(target);
        }
        if (kind == target.kind && pointer_depth == target.pointer_depth) {
            if (kind == SemaTypeKind::Int || kind == SemaTypeKind::Float || kind == SemaTypeKind::Bool || kind == SemaTypeKind::Void || kind == SemaTypeKind::String) return true;
        }
        if (target.is_any() || is_any()) return true;
        if (target.is_unknown() || is_unknown()) return true;
        if (target.is_pointer() && is_pointer()) {
            if (target.name == "void" || name == "void" || name == "nullptr" || equals(target)) return true;
            return false;
        }
        if (!target.is_pointer() && is_pointer()) {
            if (target.kind == SemaTypeKind::Int || target.kind == SemaTypeKind::Any) return true;
            return false;
        }
        if (target.is_pointer() && !is_pointer()) {
            if (kind == SemaTypeKind::Int || kind == SemaTypeKind::Any) return true;
            return false;
        }
        if (target.kind == SemaTypeKind::Float && kind == SemaTypeKind::Int) return true;
        return equals(target);
    }

    std::string toString() const {
        std::string s;
        switch (kind) {
            case SemaTypeKind::Void: s = "void"; break;
            case SemaTypeKind::Int: s = "int"; break;
            case SemaTypeKind::Float: s = "float"; break;
            case SemaTypeKind::Bool: s = "bool"; break;
            case SemaTypeKind::String: s = "string"; break;
            case SemaTypeKind::Pointer:
                s = (element_type ? element_type->toString() : (name.empty() ? "void" : name)) + "*";
                break;
            case SemaTypeKind::Array: s = name + "[]"; break;
            case SemaTypeKind::Vec:
                s = "vec<" + (element_type ? element_type->toString() : "any") + ">";
                break;
            case SemaTypeKind::Struct: s = name; break;
            case SemaTypeKind::Any: s = "any"; break;
            default: s = "unknown"; break;
        }
        return s;
    }
};

struct VariableSymbol {
    std::string name;
    SemaType type;
    SourceLocation loc;
    bool is_const = false;
    bool is_initialized = false;
};

struct FunctionSymbol {
    std::string name;
    SemaType return_type;
    std::vector<std::pair<std::string, SemaType>> params;
    SourceLocation loc;
    bool is_builtin = false;
    bool has_body = false;
    bool is_method = false;
    bool is_variadic = false;
    bool is_destructor = false;
    std::string struct_name;
};

struct StructSymbol {
    std::string name;
    std::unordered_map<std::string, SemaType> fields;
    std::vector<std::string> field_order;
    std::unordered_map<std::string, FunctionSymbol> methods;
    SourceLocation loc;
    std::string base_class;
    bool has_destructor = false;
    bool has_explicit_destructor = false;
};

struct ModuleSymbol {
    std::string name;
    std::unordered_map<std::string, FunctionSymbol> functions;
    std::unordered_map<std::string, StructSymbol> structs;
    SourceLocation loc;
};

struct SemaDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceLocation loc;
    std::string message;
    std::string hint;

    SemaDiagnostic() = default;
    SemaDiagnostic(DiagnosticSeverity sev, const SourceLocation& l, const std::string& msg, const std::string& h = "")
        : severity(sev), loc(l), message(msg), hint(h) {}

    std::string format() const {
        std::string s = "[";
        if (severity == DiagnosticSeverity::Error) s += "Sema Error";
        else if (severity == DiagnosticSeverity::Warning) s += "Sema Warning";
        else s += "Sema Note";
        s += "] " + loc.toString() + ": " + message;
        if (!hint.empty()) s += " (hint: " + hint + ")";
        return s;
    }
};

class Scope {
public:
    enum class Kind { Global, Function, Block, Struct };
    Kind kind;
    Scope* parent = nullptr;
    std::unordered_map<std::string, VariableSymbol> variables;
    std::vector<VariableSymbol> raii_variables;

    explicit Scope(Kind k, Scope* p = nullptr) : kind(k), parent(p) {}

    bool declareVariable(const VariableSymbol& sym) {
        if (variables.count(sym.name)) {
            return false;
        }
        variables[sym.name] = sym;
        return true;
    }

    void registerRAIIVariable(const VariableSymbol& sym) {
        raii_variables.push_back(sym);
    }

    VariableSymbol* lookupVariable(const std::string& name) {
        for (Scope* s = this; s != nullptr; s = s->parent) {
            auto it = s->variables.find(name);
            if (it != s->variables.end()) return &it->second;
        }
        return nullptr;
    }

    bool isDeclaredInCurrentScope(const std::string& name) const {
        return variables.count(name) > 0;
    }
};

class GrassSema {
private:
    std::unordered_map<std::string, FunctionSymbol> functions_;
    std::unordered_map<std::string, StructSymbol> structs_;
    std::unordered_map<std::string, ModuleSymbol> modules_;
    std::vector<std::unique_ptr<Scope>> scope_pool_;
    Scope* current_scope_ = nullptr;
    const FunctionSymbol* current_function_ = nullptr;
    const StructSymbol* current_struct_ = nullptr;
    std::vector<SemaDiagnostic> diagnostics_;

    bool strict_mode_ = false;
    std::vector<std::string> namespace_stack_;
    std::vector<std::string> using_namespaces_;
    std::unordered_map<std::string, std::string> using_symbols_;

    void enterScope(Scope::Kind kind);
    void exitScope();
    void reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint = "");
    void reportWarning(const SourceLocation& loc, const std::string& msg, const std::string& hint = "");

    void registerBuiltins();
    SemaType resolveType(const std::string& type_name, int ptr_depth = 0, const SourceLocation& loc = {});

    std::string currentNamespace() const;
    std::string qualifyName(const std::string& name) const;

    // Analysis passes
    void pass1_registerDeclarations(const ProgramAST& program);
    void pass2_checkProgram(const ProgramAST& program);

    void checkDecl(const DeclAST* decl);
    void checkFunctionDecl(const FunctionDeclAST* func);
    void checkStructDecl(const StructDeclAST* st);
    void checkClassDecl(const ClassDeclAST* cls);
    void checkModuleDecl(const ModuleDeclAST* mod);
    void checkNamespaceDecl(const NamespaceDeclAST* ns);
    void checkUsingDecl(const UsingDeclAST* u);

    void checkStmt(const StmtAST* stmt);
    void checkBlockStmt(const BlockStmtAST* block);
    void checkVarDeclStmt(const VarDeclStmtAST* vdecl);
    void checkAssignStmt(const AssignStmtAST* assign);
    void checkIfStmt(const IfStmtAST* if_stmt);
    void checkWhileStmt(const WhileStmtAST* while_stmt);
    void checkForStmt(const ForStmtAST* for_stmt);
    void checkReturnStmt(const ReturnStmtAST* ret_stmt);
    void checkStreamIOStmt(const StreamIOStmtAST* io_stmt);

    SemaType checkExpr(const ExprAST* expr);
    SemaType checkLiteralExpr(const LiteralExprAST* lit);
    SemaType checkVariableExpr(const VariableExprAST* var);
    SemaType checkBinaryExpr(const BinaryExprAST* bin);
    SemaType checkUnaryExpr(const UnaryExprAST* un);
    SemaType checkPostfixExpr(const PostfixExprAST* post);
    SemaType checkCallExpr(const CallExprAST* call);
    SemaType checkMemberAccessExpr(const MemberAccessExprAST* mem);
    SemaType checkIndexExpr(const IndexExprAST* idx);
    SemaType checkArrayLiteralExpr(const ArrayLiteralExprAST* arr);
    SemaType checkAllocExpr(const AllocExprAST* alloc);
    SemaType checkFreeExpr(const FreeExprAST* free_expr);
    SemaType checkCastExpr(const CastExprAST* cast);
    SemaType checkSizeofExpr(const SizeofExprAST* sz);

    bool returnsOnAllPaths(const StmtAST* stmt);

public:
    GrassSema();
    void set_strict(bool s) { strict_mode_ = s; }
    bool is_strict() const { return strict_mode_; }
    static std::string mangleName(const std::string& name);

    bool analyze(const ProgramAST& program);
    const std::vector<SemaDiagnostic>& getDiagnostics() const { return diagnostics_; }
    bool hasErrors() const;
    void printDiagnostics() const;

    const FunctionSymbol* findFunction(const std::string& name) const;
    const StructSymbol* findStruct(const std::string& name) const;
};

} // namespace grass::sema

#endif // GRASS_SEMA_H
