#include "frontend/GrassSyntax.h"
#include "baremetal/GrassKeyboard.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <set>

using namespace std;

extern std::unordered_map<std::string, GrassStructDef> g_struct_registry;

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
    if (parseStatementHeaderAndOS()) return;
    if (parseStatementUIAndMedia()) return;
    if (parseStatementControlAndVars()) return;
}

bool BytecodeCompiler::parseStatementHeaderAndOS() {
    if (match(TOK_SEMI)) return true;
    if (peek().kind == TOK_RBRACE || peek().kind == TOK_EOF) return true;
    if (peek().kind == TOK_LBRACE) {
        parseBlock();
        return true;
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
        } else if (lib == "!2DGraphics!" || lib == "!2dgraphics!" || lib == "!2d!" || lib == "!2D!" || lib == "!gfx2d!" || lib == "!Gfx2D!") {
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
        } else if (lib == "!interface!" || lib == "!gui!" || lib == "!GrassGUI!" || lib == "!disp!" || lib == "!Disp!") {
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
        return true;
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
                return true;
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
                return true;
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
                return true;
            } else {
                match(TOK_SEMI);
                emitByte(OP_CONST_NUM);
                emitDouble(8.0);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout(8);
                return true;
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
        return true;
    }

    if (peek().kind == TOK_IDENT && peek().text == "not_clear") {
        int nc_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "not_clear", nc_line);
        if (peek().kind == TOK_LBRACE) {
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            parseBlock();
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
            return true;
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
        return true;
    }

    if (peek().kind == TOK_IDENT && (peek().text == "clearable" || peek().text == "not_clear_off")) {
        int co_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "clearable", co_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
        return true;
    }

    // 0.3 КОМАНДЫ BAREMETAL И VFS (virtual_struct, physical_struct, ls, pwd, cd, reboot, poweroff, etc.)
    if (peek().kind == TOK_IDENT && peek().text == "virtual_struct") {
        parseVirtualStruct();
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    if (peek().kind == TOK_IDENT && (peek().text == "gui" || peek().text == "vd" || peek().text == "vd_desktop")) {
        string gname = peek().text;
        int gui_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", gname, gui_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("gui"); emitByte(0); emitByte(OP_POP);
        return true;
    }
    if (peek().kind == TOK_IDENT && peek().text == "reboot") {
        int rb_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "reboot", rb_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("reboot"); emitByte(0); emitByte(OP_POP);
        return true;
    }
    if (peek().kind == TOK_IDENT && peek().text == "poweroff") {
        int po_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "poweroff", po_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("poweroff"); emitByte(0); emitByte(OP_POP);
        return true;
    }
    if (peek().kind == TOK_IDENT && peek().text == "pwd") {
        int pwd_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "pwd", pwd_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("pwd"); emitByte(0); emitByte(OP_POP);
        return true;
    }
    if (peek().kind == TOK_IDENT && peek().text == "ls") {
        int ls_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "ls()", ls_line);
        if (!has_filesystem_struct) {
            reportError(ls_line, "Ошибка компиляции: вызов 'ls()' требует предварительного объявления файловой структуры через virtual_struct { ... } или physical_struct('путь')!");
            return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
    }
    if (peek().kind == TOK_IDENT && peek().text == "delete") {
        if (peek(1).kind == TOK_LPAREN && peek(2).kind == TOK_STRING) {
            int del_line = peek().line;
            advance();
            requireLib("baremetal", "!baremetal!", "delete", del_line);
            match(TOK_LPAREN);
            parseExpression();
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            match(TOK_SEMI);
            emitByte(OP_CALL_BUILTIN); emitString("delete"); emitByte(1); emitByte(OP_POP);
            return true;
        }
        advance(); // delete
        std::string tgt_struct;
        if (peek().kind == TOK_IDENT && var_struct_types.count(peek().text)) {
            tgt_struct = var_struct_types[peek().text];
        }
        parseExpression();
        match(TOK_SEMI);
        if (!tgt_struct.empty() && dtor_structs_.count(tgt_struct)) {
            std::string dtor_fn = tgt_struct + "__dtor";
            if (user_functions.count(dtor_fn)) {
                emitByte(OP_CALL_USER);
                emitU32(user_functions[dtor_fn].entry_ip);
            } else {
                emitByte(OP_CALL_USER);
                method_call_fixups.push_back({ code.size(), dtor_fn });
                emitU32(0);
            }
        }
        emitByte(OP_POP);
        return true;
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
        return true;
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
        return true;
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
        return true;
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
        return true;
    }

    // Top-level modifiers (public, private, protected, final, abstract, export)
    while (!inside_function && peek().kind == TOK_IDENT &&
           (peek().text == "public" || peek().text == "private" || peek().text == "protected" ||
            peek().text == "final" || peek().text == "abstract" || peek().text == "export")) {
        advance();
    }

    // Enum declarations: enum [class] Name { A, B = 1, ... };
    if (matchIdent("enum")) {
        if (matchIdent("class") || matchIdent("struct")) {}
        if (peek().kind == TOK_IDENT) {
            std::string ename = advance().text;
            if (match(TOK_COLON) && peek().kind == TOK_IDENT) advance();
            if (match(TOK_LBRACE)) {
                int64_t next_val = 0;
                auto& emap = enum_types_[ename];
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (match(TOK_COMMA) || match(TOK_SEMI)) continue;
                    if (peek().kind != TOK_IDENT) { advance(); continue; }
                    std::string mname = advance().text;
                    if (match(TOK_ASSIGN)) {
                        bool neg = match(TOK_MINUS);
                        if (peek().kind == TOK_NUMBER) {
                            int64_t v = static_cast<int64_t>(advance().num_val);
                            next_val = neg ? -v : v;
                        }
                    }
                    emap[mname] = next_val++;
                    match(TOK_COMMA);
                }
                consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' у enum!");
                match(TOK_SEMI);
            }
        }
        return true;
    }

    // 1. ОБЪЯВЛЕНИЕ ФУНКЦИЙ (int main() {}, take my_func() {}, void my_func() {}, Token* make() {})
    bool is_func_decl = false;
    if (!inside_function && peek().kind == TOK_IDENT &&
        (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text))) {
        size_t off = 1;
        while (peek(off).kind == TOK_STAR) off++;
        if (peek(off).kind == TOK_IDENT && peek(off + 1).kind == TOK_LPAREN) {
            size_t scan = off + 2;
            int pdepth = 1;
            while (peek(scan).kind != TOK_EOF && pdepth > 0) {
                if (peek(scan).kind == TOK_LPAREN) pdepth++;
                else if (peek(scan).kind == TOK_RPAREN) pdepth--;
                scan++;
            }
            if (pdepth == 0 && peek(scan).kind == TOK_LBRACE) {
                is_func_decl = true;
            }
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
                string pvec_elem;
                if (isVMVecTypeStart()) {
                    pvec_elem = parseVMVecInnerType();
                    while (match(TOK_STAR)) {}
                } else if (isTypeOrFuncKeyword(peek().text) || g_struct_registry.count(peek().text) || enum_types_.count(peek().text)) {
                    ptype = advance().text;
                    while (match(TOK_STAR)) {}
                }
                string pname = advance().text;
                params.push_back(pname);
                if (!pvec_elem.empty()) {
                    var_vec_types_[pname] = pvec_elem;
                } else if (!ptype.empty() && g_struct_registry.count(ptype)) {
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
        return true;
    }

    // Директива @strict
    if (peek().kind == TOK_IDENT && peek().text == "@strict") {
        advance();
        return true;
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
        return true;
    }

    // Директива using
    if (match(TOK_USING) || matchIdent("using")) {
        if (match(TOK_NAMESPACE) || matchIdent("namespace")) {}
        if (peek().kind == TOK_IDENT) advance();
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) advance();
        }
        match(TOK_SEMI);
        return true;
    }

    // Объявление структуры: struct Name { ... };
    if (matchIdent("struct") || matchIdent("class")) {
        parseStructDefinition();
        return true;
    }

    // Разрешаем объявление глобальных переменных (take, int, string, void и т.д.) на верхнем уровне
    if (!inside_function && !is_special_file) {
        bool is_var_decl = (peek().kind == TOK_IDENT &&
            (isVarKeyword(peek().text) || peek().text == "take" || peek().text == "volatile" || g_struct_registry.count(peek().text)));
        if (!is_var_decl) {
            reportError(peek().line, "Инструкция '" + peek().text + "' находится вне функции! Весь исполняемый код (кроме объявлений переменных через take/тип) должен быть внутри функций.");
            synchronize();
            return true;
        }
    }

    if (peek().kind == TOK_LBRACE) {
        parseBlock();
        return true;
    }

    if (peek().kind == TOK_SLASH && peek(1).kind == TOK_IDENT && peek(1).text == "n") {
        advance(); advance();
        match(TOK_SEMI);
        emitByte(OP_LOG_EMPTY);
        return true;
    }

    if (peek().kind == TOK_IDENT && peek().text == "text" && peek(1).kind == TOK_SEMI) {
        advance(); advance();
        emitByte(OP_LOG_EMPTY);
        return true;
    }

    if (matchIdent("end-table")) {
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN);
        emitString("table");
        emitByte(0);
        emitByte(OP_POP);
        return true;
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
        return true;
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
                return true;
            }
        } else {
            reportError(peek().line, "Ожидалось '{' или '(' после ключевого слова asm");
            return true;
        }
        emitByte(OP_CONST_STR); emitString(asm_body);
        emitByte(OP_CALL_BUILTIN); emitString("asm_run"); emitByte(1);
        emitByte(OP_POP);
        return true;
    }

    return false;
}
