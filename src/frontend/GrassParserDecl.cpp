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
                parseTypeSpec(ptype, pptr);
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
        if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_COLON || peek(1).kind == TOK_ASSIGN)) {
            std::string fname = advance().text;
            advance(); // ':' or '='
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
            fields.emplace_back(tname, fname, ptr, peek(-1).loc(filename));
            match(TOK_COMMA);
            match(TOK_SEMI);
            continue;
        }
        if (isTypeName(peek().text)) {
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
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
        if (peek().kind == TOK_IDENT && (peek(1).kind == TOK_COLON || peek(1).kind == TOK_ASSIGN)) {
            std::string fname = advance().text;
            advance(); // ':' or '='
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
            fields.emplace_back(tname, fname, ptr, peek(-1).loc(filename));
            match(TOK_COMMA);
            match(TOK_SEMI);
            continue;
        }
        if (isTypeName(peek().text)) {
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
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
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
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
            bool is_type = isTypeStart();
            if (is_type) {
                std::string type_name;
                int ptr_depth = 0;
                parseTypeSpec(type_name, ptr_depth);

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
            std::string lib = advance().text;
            program->included_libs.push_back(lib);
            if (lib == "!GrassWeb!" || lib == "!GREEN_WWW1!") {
                while (!isAtEnd()) advance();
                break;
            }
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
            } else if (checkIdent("fn") || checkIdent("fun")) {
                advance();
                std::string fname = advance().text;
                auto fn = parseFunction("auto", 0, fname);
                if (fn) program->declarations.push_back(std::move(fn));
            } else {
                bool is_type = isTypeStart();
                if (is_type) {
                    std::string type_name;
                    int ptr_depth = 0;
                    parseTypeSpec(type_name, ptr_depth);

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
