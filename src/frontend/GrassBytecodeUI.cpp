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

string BytecodeCompiler::extractParenArgsAsString() {
    string res;
    if (match(TOK_LPAREN)) {
        bool first = true;
        while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF && peek().kind != TOK_RBRACE) {
            if (peek().kind == TOK_COMMA) {
                advance();
                res += ", ";
                first = true;
            } else {
                if (!first) res += " ";
                res += advance().text;
                first = false;
            }
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
    } else {
        match(TOK_ASSIGN);
        if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
            res = advance().text;
        }
    }
    match(TOK_SEMI);
    return res;
}

bool BytecodeCompiler::parseSingleGuiDirective() {
    if (match(TOK_SEMI)) return true;
    if (peek().kind != TOK_IDENT) return false;

    string cmd = peek().text;

    if (cmd.find("animate-") == 0) {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitUICmd(cmd);
        return true;
    }

    if (cmd == "show") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        return true;
    }

    // Контейнеры UI-DSL без обязательного заголовка: tabs { ... }, row { ... }, col { ... }, panel { ... }
    if (cmd == "tabs" || cmd == "row" || cmd == "col" || cmd == "panel" ||
        cmd == "group" || cmd == "card" || cmd == "grid" || cmd == "vbox" ||
        cmd == "hbox" || cmd == "header-bar") {
        advance();
        string container_arg = "";
        if (match(TOK_LPAREN)) {
            int pdepth = 1;
            while (pdepth > 0 && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LPAREN) pdepth++;
                else if (peek().kind == TOK_RPAREN) {
                    pdepth--;
                    if (pdepth == 0) { advance(); break; }
                }
                if (!container_arg.empty()) container_arg += " ";
                container_arg += advance().text;
            }
        } else if (peek().kind == TOK_STRING) {
            container_arg = advance().text;
        }
        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parseSingleGuiDirective()) advance();
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
            match(TOK_SEMI);
            return true;
        }
        match(TOK_SEMI);
        return true;
    }

    // Реактивный таймер: every(1000) { ... } / on_timer(ms) { ... }
    if (cmd == "every" || cmd == "on_timer" || cmd == "timer") {
        advance();
        string ms = "1000";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_NUMBER) {
                ms = to_string((int)advance().num_val);
            } else if (peek().kind == TOK_IDENT) {
                ms = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')' после интервала таймера every(...)!");
        } else if (peek().kind == TOK_NUMBER) {
            ms = to_string((int)advance().num_val);
        }

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для таймера every(" + ms + ")!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' таймера every!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("every(" + ms + ", " + to_string(callback_ip) + ")");
        return true;
    }

    // Обработчик события клика: on_click { ... } / on-click { ... }
    if (cmd == "on_click" || cmd == "on-click") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для обработчика on_click!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' обработчика on_click!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("on_click_callback(" + to_string(callback_ip) + ")");
        return true;
    }

    // Обработчик изменения ввода/ползунка: on_change { ... } / on-change { ... }
    if (cmd == "on_change" || cmd == "on-change") {
        advance();
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        if (!match(TOK_LBRACE)) {
            reportError(peek().line, "Ожидался блок '{ ... }' для обработчика on_change!");
            return true;
        }

        emitByte(OP_JUMP);
        size_t skip_patch = code.size();
        emitU32(0);

        uint32_t callback_ip = (uint32_t)code.size();

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' обработчика on_change!");
        match(TOK_SEMI);

        emitByte(OP_RET);
        patchU32(skip_patch, (uint32_t)code.size());

        emitUICmd("on_change_callback(" + to_string(callback_ip) + ")");
        return true;
    }

    // Элементы интерфейса: button, label, enter, icon, video, svg, clock, progress, slider, checkbox, list
    if (cmd == "button" || cmd == "label" || cmd == "enter" || cmd == "icon" || cmd == "video" || cmd == "svg" || cmd == "clock" ||
        cmd == "progress" || cmd == "slider" || cmd == "checkbox" || cmd == "list" || cmd == "scroll-box") {
        int el_line = peek().line;
        advance();
        string inline_id = "";
        if (peek().kind == TOK_IDENT && peek(1).kind == TOK_ASSIGN) {
            inline_id = advance().text;
        }
        match(TOK_ASSIGN);

        string txt = "";
        if (cmd == "clock" && peek().kind == TOK_LBRACE) {
            txt = "HH:mm:ss";
        } else if (cmd == "icon" || cmd == "video" || cmd == "svg") {
            if (match(TOK_LPAREN)) {
                if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) txt = advance().text;
                consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
            } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                txt = advance().text;
            }
        } else if (match(TOK_LPAREN)) {
            vector<GrassToken> expr_tokens;
            int paren_depth = 1;
            while (paren_depth > 0 && peek().kind != TOK_EOF) {
                if (peek().kind == TOK_LPAREN) paren_depth++;
                else if (peek().kind == TOK_RPAREN) {
                    paren_depth--;
                    if (paren_depth == 0) {
                        advance();
                        break;
                    }
                }
                expr_tokens.push_back(advance());
            }
            if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_STRING) {
                string s = expr_tokens[0].text;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                txt = s;
            } else if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_IDENT) {
                txt = "{" + expr_tokens[0].text + "}";
            } else {
                for (size_t ti = 0; ti < expr_tokens.size(); ++ti) {
                    const auto& tk = expr_tokens[ti];
                    if (tk.kind == TOK_STRING) {
                        string s = tk.text;
                        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                        txt += s;
                    } else if (tk.kind == TOK_IDENT) {
                        if (tk.text == "to_str" || tk.text == "to_string" || tk.text == "to_num") {
                            continue;
                        }
                        if (ti + 2 < expr_tokens.size() && expr_tokens[ti+1].kind == TOK_LPAREN && expr_tokens[ti+2].kind == TOK_RPAREN) {
                            txt += "{" + tk.text + "}";
                            ti += 2;
                        } else {
                            txt += "{" + tk.text + "}";
                        }
                    } else if (tk.kind == TOK_NUMBER) {
                        txt += tk.text;
                    }
                }
            }
        } else {
            vector<GrassToken> expr_tokens;
            while (peek().kind != TOK_LBRACE && peek().kind != TOK_SEMI && peek().kind != TOK_EOF) {
                expr_tokens.push_back(advance());
            }
            if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_STRING) {
                string s = expr_tokens[0].text;
                if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                txt = s;
            } else if (expr_tokens.size() == 1 && expr_tokens[0].kind == TOK_IDENT) {
                txt = "{" + expr_tokens[0].text + "}";
            } else {
                for (size_t ti = 0; ti < expr_tokens.size(); ++ti) {
                    const auto& tk = expr_tokens[ti];
                    if (tk.kind == TOK_STRING) {
                        string s = tk.text;
                        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
                        txt += s;
                    } else if (tk.kind == TOK_IDENT) {
                        if (tk.text == "to_str" || tk.text == "to_string" || tk.text == "to_num") {
                            continue;
                        }
                        if (ti + 2 < expr_tokens.size() && expr_tokens[ti+1].kind == TOK_LPAREN && expr_tokens[ti+2].kind == TOK_RPAREN) {
                            txt += "{" + tk.text + "}";
                            ti += 2;
                        } else {
                            txt += "{" + tk.text + "}";
                        }
                    } else if (tk.kind == TOK_NUMBER) {
                        txt += tk.text;
                    }
                }
            }
        }

        if (cmd == "icon" || cmd == "video" || cmd == "svg") {
            emitUICmd(cmd + "(" + txt + ")");
        } else {
            emitUICmd(cmd + " !!" + txt + "!!");
        }

        if (!inline_id.empty()) {
            emitUICmd("id(" + inline_id + ")");
        }

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parseSingleGuiDirective()) advance();
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
            match(TOK_SEMI);
            return true;
        }
        if (match(TOK_SEMI)) {
            return true;
        }
        reportError(el_line, "Элемент '" + cmd + "' в едином синтаксисе GrassGreen должен открывать блок свойств '{ ... }' или заканчиваться ';'!");
        return true;
    }

    // ── Система вкладок (Tabs System): tab = "main" { ... }, tab "profile" { ... }, tab("name") ──
    if (cmd == "tab") {
        advance();
        if (match(TOK_ASSIGN)) {
            // съедаем '=' если написано tab = ...
        }
        string tab_name = "main";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                tab_name = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
            tab_name = advance().text;
        }

        emitUICmd("tab(" + tab_name + ")");

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parseSingleGuiDirective()) advance();
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
            match(TOK_SEMI);
            emitUICmd("tab(__global__)");
            return true;
        }
        match(TOK_SEMI);
        return true;
    }

    if (cmd == "middle" || cmd == "center" || cmd == "left" || cmd == "right" ||
        cmd == "shadow" || cmd == "glass" || cmd == "clear" || cmd == "topmost" || cmd == "clock") {
        advance();
        string norm_cmd = (cmd == "center") ? "middle" : cmd;
        string arg = "";
        if (match(TOK_LPAREN)) {
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) arg += advance().text;
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        if (!arg.empty()) emitUICmd(norm_cmd + "(" + arg + ")");
        else emitUICmd(norm_cmd);
        return true;
    }

    if (cmd == "surface" || cmd == "auto_size_display" || cmd == "auto-size-display" ||
        cmd == "border-radius-app" || cmd == "border_radius_app" ||
        cmd == "topmost" || cmd == "z-index" || cmd == "z_index" || cmd == "layer" ||
        cmd == "clock" || cmd == "clock-format" || cmd == "clock_format" || cmd == "time-format" || cmd == "time_format" ||
        cmd == "header" || cmd == "header-color" || cmd == "header_color" ||
        cmd == "header-text-color" || cmd == "header_text_color" ||
        cmd == "header-height" || cmd == "header_height" ||
        cmd == "header-buttons" || cmd == "header_buttons" ||
        cmd == "header-buttons-pos" || cmd == "header_buttons_pos" ||
        cmd == "header-buttons-offset" || cmd == "header_buttons_offset" ||
        cmd == "header-title-align" || cmd == "header_title_align" ||
        cmd == "header-align" || cmd == "header_align" ||
        cmd == "window-border" || cmd == "window_border" ||
        cmd == "fullscreen" || cmd == "fullscrean" ||
        cmd == "layout" || cmd == "align-all" || cmd == "align_all" ||
        cmd == "center-all" || cmd == "center_all" ||
        cmd == "engine" || cmd == "vd_plus" || cmd == "vdplus" ||
        cmd == "particles" || cmd == "animate" ||
        cmd == "gradient" || cmd == "glow" || cmd == "border" || cmd == "placeholder" ||
        cmd == "placeholder-color" || cmd == "placeholder_color" ||
        cmd == "hover-color" || cmd == "hover_color" || cmd == "active-color" || cmd == "active_color" ||
        cmd == "title" || cmd == "name" || cmd == "size" || cmd == "size-top" || cmd == "height" ||
        cmd == "color" || cmd == "bg" || cmd == "background" || cmd == "theme" || cmd == "fon" ||
        cmd == "iconP" || cmd == "id" || cmd == "name_el" || cmd == "window-ob" || cmd == "pos" ||
        cmd == "position" || cmd == "align" || cmd == "text-color" || cmd == "text_color" ||
        cmd == "button-color" || cmd == "button_color" || cmd == "font-size" || cmd == "font_size" ||
        cmd == "font-style" || cmd == "font_style" || cmd == "font-weight" || cmd == "font_weight" ||
        cmd == "radius" || cmd == "pad" ||
        cmd == "click-sound" || cmd == "click_sound" || cmd == "click-synth" || cmd == "click_synth" ||
        cmd == "hover-synth" || cmd == "hover_synth" || cmd == "hover-sound" || cmd == "hover_sound" ||
        cmd == "tooltip" || cmd == "cursor" || cmd == "disabled" ||
        cmd == "val" || cmd == "value" || cmd == "percent" || cmd == "item" || cmd == "add_item" ||
        cmd == "click-ob" || cmd == "click-switch" || cmd == "click_switch" ||
        cmd == "switch" || cmd == "switch_tab" || cmd == "run" || cmd == "action") {
        advance();
        string arg = extractParenArgsAsString();

        if (cmd == "auto-size-display") cmd = "auto_size_display";
        else if (cmd == "border_radius_app") cmd = "border-radius-app";
        else if (cmd == "click_sound") cmd = "click-sound";
        else if (cmd == "click_synth") cmd = "click-synth";
        else if (cmd == "hover_synth") cmd = "hover-synth";
        else if (cmd == "hover_sound") cmd = "hover-sound";
        else if (cmd == "value" || cmd == "percent") cmd = "val";
        else if (cmd == "add_item") cmd = "item";
        else if (cmd == "fullscrean") cmd = "fullscreen";
        else if (cmd == "z_index" || cmd == "layer") cmd = "z-index";
        else if (cmd == "clock_format" || cmd == "time_format" || cmd == "time-format") cmd = "clock-format";
        else if (cmd == "align_all" || cmd == "align-all" || cmd == "center-all" || cmd == "center_all") cmd = "layout";
        else if (cmd == "click_switch") cmd = "click-switch";
        else if (cmd == "switch_tab") cmd = "switch";
        else if (cmd == "header_buttons_pos") cmd = "header-buttons-pos";
        else if (cmd == "header_buttons_offset") cmd = "header-buttons-offset";
        else if (cmd == "header_title_align" || cmd == "header_align" || cmd == "header-align") cmd = "header-title-align";
        else if (cmd == "height") cmd = "size-top";
        else if (cmd == "bg" || cmd == "background") cmd = "color";
        else if (cmd == "pos" || cmd == "position") cmd = "window-ob";
        else if (cmd == "text_color") cmd = "text-color";
        else if (cmd == "button_color") cmd = "button-color";
        else if (cmd == "hover_color") cmd = "hover-color";
        else if (cmd == "active_color") cmd = "active-color";
        else if (cmd == "header_color") cmd = "header-color";
        else if (cmd == "header_text_color") cmd = "header-text-color";
        else if (cmd == "header_height") cmd = "header-height";
        else if (cmd == "header_buttons") cmd = "header-buttons";
        else if (cmd == "window_border") cmd = "window-border";
        else if (cmd == "placeholder_color") cmd = "placeholder-color";
        else if (cmd == "font_size") cmd = "font-size";
        else if (cmd == "font_style") cmd = "font-style";
        else if (cmd == "font_weight") cmd = "font-weight";
        else if (cmd == "action") cmd = "run";

        if (cmd == "align") {
            if (arg == "center" || arg == "middle") emitUICmd("middle");
            else if (arg == "left") emitUICmd("left");
            else if (arg == "right") emitUICmd("right");
        } else if (cmd == "title" || cmd == "name") {
            emitUICmd(cmd + "(!!" + arg + "!!)");
        } else if (cmd == "header") {
            emitUICmd("header(" + arg + ")");
            if (match(TOK_LBRACE)) {
                while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                    if (peek().kind == TOK_IDENT) {
                        string hsub = peek().text;
                        if (hsub == "title" || hsub == "name") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-title(!!" + harg + "!!)");
                            continue;
                        } else if (hsub == "color" || hsub == "bg" || hsub == "background") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-color(" + harg + ")");
                            continue;
                        } else if (hsub == "text-color" || hsub == "text_color") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-text-color(" + harg + ")");
                            continue;
                        } else if (hsub == "height") {
                            advance();
                            string harg = extractParenArgsAsString();
                            match(TOK_SEMI);
                            emitUICmd("header-height(" + harg + ")");
                            continue;
                        }
                    }
                    if (!parseSingleGuiDirective()) advance();
                }
                consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' блока 'header'!");
                match(TOK_SEMI);
            }
        } else {
            emitUICmd(cmd + "(" + arg + ")");
        }
        return true;
    }

    // Любые прочие дефисные UI-директивы внутри UI-блока (border-radius(...), bg-color(...), font-weight(...))
    if (cmd.find('-') != string::npos) {
        advance();
        string arg = "";
        if (peek().kind == TOK_LPAREN) {
            arg = extractParenArgsAsString();
        } else {
            match(TOK_SEMI);
        }
        emitUICmd(arg.empty() ? cmd : (cmd + "(" + arg + ")"));
        return true;
    }

    return false;
}

void BytecodeCompiler::parseWindowBlock() {
    int win_line = peek(-1).line;
    requireLib("gui", "!interface!", "window { ... }", win_line);

    if (match(TOK_LPAREN)) {
        while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) advance();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
    }

    if (!match(TOK_LBRACE)) {
        reportError(win_line, "После ключевого слова 'window' обязателен блок в фигурных скобках '{ ... }'!");
        return;
    }

    emitByte(OP_UI_INIT);
    emitString("GrassGreen UI");
    emitU32(640);
    emitU32(480);
    emitString("#09090b");
    emitString("");

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (!parseSingleGuiDirective()) {
            parseStatement();
        }
    }
    consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}' блока 'window'!");
    match(TOK_SEMI);
    emitByte(OP_UI_SHOW);
}


bool BytecodeCompiler::parseStatementUIAndMedia() {
    // 2. БЛОК ОКНА ИНТЕРФЕЙСА: window { ... }
    if (matchIdent("window")) {
        parseWindowBlock();
        return true;
    }

    // Директива движка отображения: surface("auto" | "windowed" | "freestanding" | "borderless")
    if (matchIdent("surface")) {
        int sf_line = peek(-1).line;
        requireLib("gui", "!interface!", "surface()", sf_line);
        string mode = "auto";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                mode = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString(mode);
        emitByte(OP_CALL_BUILTIN); emitString("surface"); emitByte(1);
        emitByte(OP_POP);
        return true;
    }

    // Директива авто-масштабирования High-DPI: auto_size_display();
    if (matchIdent("auto_size_display")) {
        int as_line = peek(-1).line;
        requireLib("gui", "!interface!", "auto_size_display()", as_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("auto_size_display"); emitByte(0);
        emitByte(OP_POP);
        return true;
    }

    // Директива скругления углов окна приложения: border_radius_app(16) / border-radius-app(16)
    if (matchIdent("border_radius_app") || matchIdent("border-radius-app")) {
        int br_line = peek(-1).line;
        requireLib("gui", "!interface!", "border_radius_app()", br_line);
        string rad = "12";
        if (match(TOK_LPAREN)) {
            if (peek().kind == TOK_NUMBER || peek().kind == TOK_IDENT) {
                rad = advance().text;
            }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString("border-radius-app(" + rad + ")");
        emitByte(OP_CALL_BUILTIN); emitString("gui_cmd"); emitByte(1);
        emitByte(OP_POP);
        return true;
    }

    // ── БИБЛИОТЕКА !audio! ──
    if (matchIdent("tone")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "tone()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        uint8_t argc = 2;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 3;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("tone"); emitByte(argc);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("beep")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "beep()", aud_line);
        match(TOK_LPAREN);
        uint8_t argc = 0;
        if (peek().kind != TOK_RPAREN) {
            parseExpression();
            argc = 1;
            if (match(TOK_COMMA)) {
                parseExpression();
                argc = 2;
            }
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("beep"); emitByte(argc);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("synth_play") || matchIdent("synth")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "synth_play()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        uint8_t argc = 3;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 4;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("synth_play"); emitByte(argc);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("sound_play")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_play()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        uint8_t argc = 1;
        if (match(TOK_COMMA)) {
            parseExpression();
            argc = 2;
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_play"); emitByte(argc);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("sound_stop")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_stop()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_stop"); emitByte(0);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("sound_pause")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_pause()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_pause"); emitByte(0);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("sound_resume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_resume()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_resume"); emitByte(0);
        emitByte(OP_POP);
        return true;
    }

    if (matchIdent("sound_volume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_volume()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_volume"); emitByte(1);
        emitByte(OP_POP);
        return true;
    }

    // Блок настройки сетевого агента: agent { browser "..."; device "..."; }
    if (matchIdent("agent")) {
        int ag_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "agent { ... }", ag_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        string browser_val = "GrassGreenNet/3.1";
        string device_val = "Windows NT 10.0; Win64; x64";

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (matchIdent("browser")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) browser_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("device")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) device_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(browser_val);
        emitByte(OP_CONST_STR); emitString(device_val);
        emitByte(OP_CALL_BUILTIN); emitString("net_set_agent"); emitByte(2);
        emitByte(OP_POP);
        return true;
    }

    // Блок создания сервера: create_serv() { port: 443; IP: 127.0.0.1; password: 1234; certainIP: 127.0.0.1; }
    if (matchIdent("create_serv")) {
        int srv_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "create_serv()", srv_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);

        string ip_val = "127.0.0.1";
        double port_val = 443.0;
        string pass_val = "";
        string cert_ip_val = "127.0.0.1";

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (matchIdent("port")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind == TOK_NUMBER) port_val = advance().num_val;
                    else if (peek().kind != TOK_SEMI) { try { port_val = stod(advance().text); } catch (...) {} }
                    match(TOK_SEMI);
                } else if (matchIdent("IP") || matchIdent("ip")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) ip_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("password")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) pass_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("certainIP") || matchIdent("certain_ip")) {
                    if (!match(TOK_ASSIGN)) match(TOK_COLON);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) cert_ip_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            consume(TOK_RBRACE, "Ожидалась закрывающая фигурная скобка '}'!");
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(ip_val);
        emitByte(OP_CONST_NUM); emitDouble(port_val);
        emitByte(OP_CONST_STR); emitString(pass_val);
        emitByte(OP_CONST_STR); emitString(cert_ip_val);
        emitByte(OP_CALL_BUILTIN); emitString("create_serv"); emitByte(4);
        emitByte(OP_POP);
        return true;
    }

    // Команда асинхронного запуска: async worker(a, b);
    if (peek().kind == TOK_IDENT && peek().text == "async" && peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
        int as_line = peek().line;
        requireLib("thread", "!thread!", "async", as_line);
        advance(); // consume "async"
        string fn_target = advance().text; // consume fn name
        match(TOK_LPAREN);
        uint8_t argc = 0;
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            do {
                parseExpression();
                argc++;
            } while (match(TOK_COMMA));
        }
        consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        match(TOK_SEMI);

        emitByte(OP_ASYNC_SPAWN);
        if (user_functions.count(fn_target)) {
            emitU32(user_functions[fn_target].entry_ip);
        } else {
            thread_spawn_fixups.push_back({code.size(), fn_target});
            emitU32(0);
        }
        emitByte(argc);
        emitByte(OP_POP);
        return true;
    }

    // Команда синхронизации async
    if (matchIdent("async")) {
        int as_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "async", as_line);
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            else { emitByte(OP_CONST_NUM); emitDouble(10.0); }
            consume(TOK_RPAREN, "Ожидалась закрывающая круглая скобка ')'!");
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(10.0);
        }
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return true;
    }

    return false;
}
