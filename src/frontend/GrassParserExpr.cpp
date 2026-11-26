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
           s == "vec" || s == "vector" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "byte" || s == "size_t";
}

bool GrassParser::isTypeStart(size_t offset) const {
    if ((peek(offset).text == "vec" || peek(offset).text == "vector") && peek(offset + 1).kind == TOK_LT) {
        return true;
    }
    if (isTypeName(peek(offset).text)) return true;
    if (peek(offset).kind == TOK_IDENT) {
        size_t off = offset + 1;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_IDENT) return true;
    }
    return false;
}

bool GrassParser::parseTypeSpec(std::string& out_type, int& out_ptr_depth) {
    out_ptr_depth = 0;
    if ((peek().text == "vec" || peek().text == "vector") && peek(1).kind == TOK_LT) {
        advance(); // vec / vector
        advance(); // <
        std::string inner_t;
        int inner_ptr = 0;
        if (!parseTypeSpec(inner_t, inner_ptr)) {
            return false;
        }
        for (int i = 0; i < inner_ptr; ++i) inner_t += "*";
        if (!match(TOK_GT)) {
            reportError(peek().loc(filename), "Expected '>' closing vec<T> type", "Close template argument with '>'");
        }
        out_type = "vec<" + inner_t + ">";
        while (match(TOK_STAR)) out_ptr_depth++;
        return true;
    }
    if (peek().kind != TOK_IDENT) return false;
    out_type = advance().text;
    while (match(TOK_COLON_COLON)) {
        if (peek().kind == TOK_IDENT) {
            out_type += "::" + advance().text;
        } else break;
    }
    while (match(TOK_STAR)) out_ptr_depth++;
    return true;
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
            int ptr_depth = 0;
            if (peek().kind == TOK_IDENT) {
                parseTypeSpec(tname, ptr_depth);
            } else {
                reportError(peek().loc(filename), "Expected type name after 'as'", "Specify target type");
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
            std::string tname;
            int ptr_depth = 0;
            parseTypeSpec(tname, ptr_depth);
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

