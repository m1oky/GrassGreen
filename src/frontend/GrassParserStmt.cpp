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

static bool isUIDSLContainerOrWidgetName(const std::string& t) {
    return t == "window" || t == "tabs" || t == "row" || t == "col" || t == "panel" ||
           t == "group" || t == "card" || t == "grid" || t == "vbox" || t == "hbox" ||
           t == "header-bar" || t == "tab" || t == "header" ||
           t == "button" || t == "label" || t == "enter" || t == "icon" ||
           t == "video" || t == "svg" || t == "clock" || t == "progress" ||
           t == "slider" || t == "checkbox" || t == "list" || t == "scroll-box";
}

static bool isUIDSLCallbackName(const std::string& t) {
    return t == "on_click" || t == "on-click" || t == "on_change" || t == "on-change" ||
           t == "every" || t == "on_timer" || t == "timer";
}

static bool isUIDSLPropertyName(const std::string& t) {
    if (t.find('-') != std::string::npos) return true;
    if (t.rfind("animate-", 0) == 0) return true;
    return t == "title" || t == "name" || t == "name_el" || t == "size" || t == "height" ||
           t == "color" || t == "bg" || t == "background" || t == "theme" || t == "fon" ||
           t == "header" || t == "iconP" || t == "id" || t == "pos" || t == "position" || t == "align" ||
           t == "radius" || t == "pad" || t == "shadow" || t == "glass" || t == "topmost" ||
           t == "layer" || t == "fullscreen" || t == "fullscrean" || t == "surface" ||
           t == "auto_size_display" || t == "layout" || t == "engine" || t == "vd_plus" ||
           t == "vdplus" || t == "particles" || t == "animate" || t == "gradient" ||
           t == "glow" || t == "border" || t == "placeholder" || t == "tooltip" ||
           t == "cursor" || t == "disabled" || t == "val" || t == "value" || t == "percent" ||
           t == "item" || t == "add_item" || t == "switch" || t == "switch_tab" ||
           t == "run" || t == "action" || t == "middle" || t == "center" || t == "left" ||
           t == "right" || t == "clock" || t == "clear" || t == "show";
}

bool GrassParser::isUIDSLBlockStart() const {
    if (peek().kind != TOK_IDENT) return false;
    const std::string& cmd = peek().text;
    if (!isUIDSLContainerOrWidgetName(cmd)) return false;

    const GrassToken& t1 = peek(1);
    if (t1.kind == TOK_LBRACE) return true;
    if (t1.kind == TOK_STRING) return true;
    if (t1.kind == TOK_IDENT && peek(2).kind == TOK_ASSIGN) return true;
    if (t1.kind == TOK_ASSIGN && (cmd == "tab" || cmd == "button" || cmd == "label" ||
        cmd == "enter" || cmd == "slider" || cmd == "checkbox" || cmd == "progress" ||
        cmd == "clock" || cmd == "list")) {
        return true;
    }
    if (t1.kind == TOK_LPAREN) {
        size_t idx = cur + 2;
        int pdepth = 1;
        while (idx < tokens.size() && pdepth > 0 && tokens[idx].kind != TOK_EOF) {
            if (tokens[idx].kind == TOK_LPAREN) pdepth++;
            else if (tokens[idx].kind == TOK_RPAREN) pdepth--;
            idx++;
        }
        if (idx < tokens.size() && tokens[idx].kind == TOK_LBRACE) return true;
    }
    return false;
}

std::unique_ptr<StmtAST> GrassParser::parseUIDSLBlock(bool is_window) {
    auto loc = advance().loc(filename); // consume keyword
    while (!check(TOK_LBRACE) && !check(TOK_SEMI) && !isAtEnd()) {
        if (match(TOK_LPAREN)) {
            int pdepth = 1;
            while (pdepth > 0 && !isAtEnd()) {
                if (peek().kind == TOK_LPAREN) pdepth++;
                else if (peek().kind == TOK_RPAREN) pdepth--;
                advance();
            }
        } else {
            advance();
        }
    }

    std::vector<std::unique_ptr<StmtAST>> stmts;
    if (match(TOK_LBRACE)) {
        while (!check(TOK_RBRACE) && !isAtEnd()) {
            if (match(TOK_SEMI)) continue;

            if (peek().kind == TOK_IDENT && isUIDSLCallbackName(peek().text)) {
                advance();
                if (match(TOK_LPAREN)) {
                    int pdepth = 1;
                    while (pdepth > 0 && !isAtEnd()) {
                        if (peek().kind == TOK_LPAREN) pdepth++;
                        else if (peek().kind == TOK_RPAREN) pdepth--;
                        advance();
                    }
                } else if (peek().kind == TOK_NUMBER) {
                    advance();
                }
                if (check(TOK_LBRACE)) {
                    auto cb_block = parseBlock();
                    if (cb_block) stmts.push_back(std::move(cb_block));
                    match(TOK_SEMI);
                } else {
                    match(TOK_SEMI);
                }
                continue;
            }

            if (isUIDSLBlockStart()) {
                bool sub_is_window = checkIdent("window");
                auto sub = parseUIDSLBlock(sub_is_window);
                if (sub) stmts.push_back(std::move(sub));
                continue;
            }

            if (peek().kind == TOK_IDENT && isUIDSLPropertyName(peek().text)) {
                std::string prop = advance().text;
                if (prop == "animate" && match(TOK_MINUS) && peek().kind == TOK_IDENT) {
                    advance();
                }
                if (match(TOK_LPAREN)) {
                    int pdepth = 1;
                    while (pdepth > 0 && !isAtEnd()) {
                        if (peek().kind == TOK_LPAREN) pdepth++;
                        else if (peek().kind == TOK_RPAREN) pdepth--;
                        advance();
                    }
                }
                match(TOK_SEMI);
                continue;
            }

            auto inner = parseStatement();
            if (inner) stmts.push_back(std::move(inner));
        }
        if (!match(TOK_RBRACE)) {
            reportError(peek().loc(filename), "Expected '}' at end of UI block", "Close UI block with '}'");
        }
        match(TOK_SEMI);
    } else {
        match(TOK_SEMI);
    }

    if (is_window) {
        std::vector<std::unique_ptr<ExprAST>> args;
        stmts.push_back(std::make_unique<ExprStmtAST>(loc, std::make_unique<CallExprAST>(loc, "gui_show", std::move(args))));
    }
    return std::make_unique<BlockStmtAST>(loc, std::move(stmts));
}

// Statements & Declarations
std::unique_ptr<StmtAST> GrassParser::parseStatement() {
    if (match(TOK_SEMI)) return nullptr;
    if (check(TOK_LBRACE)) return parseBlock();

    if (checkIdent("volatile")) {
        advance();
        return parseStatement();
    }
    if (checkIdent("clear") && (peek(1).kind == TOK_SEMI || peek(1).kind == TOK_LPAREN)) {
        auto loc = advance().loc(filename);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        std::vector<std::unique_ptr<ExprAST>> args;
        return std::make_unique<ExprStmtAST>(loc, std::make_unique<CallExprAST>(loc, "clear", std::move(args)));
    }
    if (checkIdent("not_clear")) {
        auto loc = advance().loc(filename);
        if (check(TOK_LBRACE)) {
            return parseBlock();
        }
        if (match(TOK_LPAREN)) {
            if (!check(TOK_RPAREN)) parseExpression();
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        std::vector<std::unique_ptr<ExprAST>> args;
        return std::make_unique<ExprStmtAST>(loc, std::make_unique<CallExprAST>(loc, "not_clear_on", std::move(args)));
    }
    if (isUIDSLBlockStart()) {
        return parseUIDSLBlock(checkIdent("window"));
    }
    if (checkIdent("agent") && peek(1).kind == TOK_LBRACE) {
        auto loc = advance().loc(filename);
        advance(); // '{'
        int depth = 1;
        while (depth > 0 && !isAtEnd()) {
            if (peek().kind == TOK_LBRACE) depth++;
            else if (peek().kind == TOK_RBRACE) depth--;
            advance();
        }
        match(TOK_SEMI);
        (void)loc;
        return nullptr;
    }
    if (checkIdent("create_serv") && peek(1).kind == TOK_LPAREN) {
        auto loc = advance().loc(filename);
        advance(); // '('
        int pdepth = 1;
        while (pdepth > 0 && !isAtEnd()) {
            if (peek().kind == TOK_LPAREN) pdepth++;
            else if (peek().kind == TOK_RPAREN) pdepth--;
            advance();
        }
        if (peek().kind == TOK_LBRACE) {
            advance(); // '{'
            int bdepth = 1;
            while (bdepth > 0 && !isAtEnd()) {
                if (peek().kind == TOK_LBRACE) bdepth++;
                else if (peek().kind == TOK_RBRACE) bdepth--;
                advance();
            }
        }
        match(TOK_SEMI);
        std::vector<std::unique_ptr<ExprAST>> args;
        return std::make_unique<ExprStmtAST>(loc, std::make_unique<CallExprAST>(loc, "create_serv", std::move(args)));
    }
    if (checkIdent("async") && peek(1).kind == TOK_SEMI) {
        advance();
        match(TOK_SEMI);
        return nullptr;
    }
    if (checkIdent("async") && peek(1).kind == TOK_IDENT) {
        auto loc = advance().loc(filename);
        auto inner = parseExpression();
        match(TOK_SEMI);
        std::vector<std::unique_ptr<ExprAST>> args;
        if (inner) args.push_back(std::move(inner));
        return std::make_unique<ExprStmtAST>(loc, std::make_unique<CallExprAST>(loc, "async", std::move(args)));
    }

    if (checkIdent("if") || checkIdent("check")) return parseIf();
    if (checkIdent("while") || checkIdent("loop")) return parseWhile();
    if (checkIdent("for") || checkIdent("again")) return parseFor();
    if (checkIdent("return")) return parseReturn();

    if (checkIdent("delete") && !(peek(1).kind == TOK_LPAREN && peek(2).kind == TOK_STRING)) {
        auto loc = advance().loc(filename);
        auto tgt = parseExpression();
        if (!match(TOK_SEMI)) {
            reportError(peek().loc(filename), "Expected ';' after delete statement", "Insert ';'");
            synchronize();
        }
        return std::make_unique<ExprStmtAST>(loc, std::make_unique<FreeExprAST>(loc, std::move(tgt)));
    }

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

    bool is_type = isTypeStart();

    if (is_type) {
        std::string tname;
        int ptr_depth = 0;
        parseTypeSpec(tname, ptr_depth);
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
            parseTypeSpec(tname, ptr_depth);
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
    bool is_direct_ctor = false;
    std::vector<std::unique_ptr<ExprAST>> ctor_args;

    if (match(TOK_ASSIGN)) {
        init = parseExpression();
    } else if (ptr_depth == 0 && check(TOK_LPAREN)) {
        advance(); // '('
        if (!check(TOK_RPAREN) && !isAtEnd()) {
            do {
                ctor_args.push_back(parseExpression());
            } while (match(TOK_COMMA));
        }
        if (!match(TOK_RPAREN)) {
            reportError(peek().loc(filename), "Expected ')' after constructor arguments for '" + vname + "'", "Close constructor arguments with ')'");
        }
        is_direct_ctor = true;
    }

    if (!match(TOK_SEMI)) {
        reportError(peek().loc(filename), "Expected ';' after variable declaration of '" + vname + "', found '" + peek().text + "'", "Insert ';' to end declaration");
        synchronize();
    }

    return std::make_unique<VarDeclStmtAST>(tok.loc(filename), type_name, vname, ptr_depth, is_const, std::move(init), is_direct_ctor, std::move(ctor_args));
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
            std::string tname;
            int ptr = 0;
            parseTypeSpec(tname, ptr);
            init = parseVarDecl(tname, ptr, false);
        } else {
            auto e = parseExpression();
            if (e && (peek().kind == TOK_ASSIGN || peek().kind == TOK_PLUS_ASSIGN ||
                      peek().kind == TOK_MINUS_ASSIGN || peek().kind == TOK_MUL_ASSIGN ||
                      peek().kind == TOK_DIV_ASSIGN)) {
                auto op_tok = advance();
                auto val = parseExpression();
                init = std::make_unique<AssignStmtAST>(loc, std::move(e), op_tok.kind, op_tok.text, std::move(val));
            } else if (e) {
                init = std::make_unique<ExprStmtAST>(loc, std::move(e));
            }
            match(TOK_SEMI);
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
        if (e && (peek().kind == TOK_ASSIGN || peek().kind == TOK_PLUS_ASSIGN ||
                  peek().kind == TOK_MINUS_ASSIGN || peek().kind == TOK_MUL_ASSIGN ||
                  peek().kind == TOK_DIV_ASSIGN)) {
            auto op_tok = advance();
            auto val = parseExpression();
            step = std::make_unique<AssignStmtAST>(loc, std::move(e), op_tok.kind, op_tok.text, std::move(val));
        } else if (e) {
            step = std::make_unique<ExprStmtAST>(loc, std::move(e));
        }
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

