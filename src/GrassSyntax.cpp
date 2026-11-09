#include "GrassSyntax.h"
#include "GrassKeyboard.h"
#include <iostream>
#include <cstring>
#include <cctype>

using namespace std;

// ============================================================================
// РЕАЛИЗАЦИЯ ЛЕКСЕРА
// ============================================================================
bool Lexer::isIdentStart(unsigned char c) {
    return isalpha(c) || c == '_' || c >= 128;
}

bool Lexer::isIdentCont(unsigned char c) {
    return isalnum(c) || c == '_' || c >= 128;
}

Lexer::Lexer(const string& source) : src(source) {}

vector<GrassToken> Lexer::tokenize() {
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

        // Однострочный комментарий * в начале строки или // (игнорируем, если перед ними двоеточие, как в http:// и https://)
        if ((c == '*' && at_line_start) || 
            (c == '/' && pos + 1 < src.size() && src[pos + 1] == '/' && !(pos > 0 && src[pos - 1] == ':'))) {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }

        // Директивы #include
        if (c == '#' && pos + 7 < src.size() && src.substr(pos, 8) == "#include") {
            pos += 8;
            tokens.push_back(GrassToken(TOK_IDENT, "include", 0.0, line));
            continue;
        }
        if (c == '@') {
            while (pos < src.size() && src[pos] != '\n') pos++;
            continue;
        }

        // Hex-цвета (#00ffcc)
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
                } else if (src[pos] == '/' && pos + 1 < src.size() && src[pos + 1] == 'n' && (pos + 2 >= src.size() || !isalpha((unsigned char)src[pos + 2]))) {
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

        // 0x... (шестнадцатеричные числа)
        if (c == '0' && pos + 1 < src.size() && (src[pos + 1] == 'x' || src[pos + 1] == 'X')) {
            size_t start = pos;
            pos += 2;
            while (pos < src.size() && isxdigit((unsigned char)src[pos])) pos++;
            string hex_str = src.substr(start, pos - start);
            double val = 0.0;
            try { val = (double)std::stoull(hex_str, nullptr, 16); } catch (...) {}
            tokens.push_back(GrassToken(TOK_NUMBER, hex_str, val, line));
            continue;
        }
        // 0b... (двоичные числа)
        if (c == '0' && pos + 1 < src.size() && (src[pos + 1] == 'b' || src[pos + 1] == 'B')) {
            size_t start = pos;
            pos += 2;
            while (pos < src.size() && (src[pos] == '0' || src[pos] == '1')) pos++;
            string bin_str = src.substr(start, pos - start);
            double val = 0.0;
            try { val = (double)std::stoull(bin_str.substr(2), nullptr, 2); } catch (...) {}
            tokens.push_back(GrassToken(TOK_NUMBER, bin_str, val, line));
            continue;
        }

        // Числа и IPv4 адреса (например 127.0.0.1)
        if (isdigit((unsigned char)c)) {
            size_t start = pos;
            int dot_count = 0;
            while (pos < src.size() && (isdigit((unsigned char)src[pos]) || src[pos] == '.')) {
                if (src[pos] == '.') dot_count++;
                pos++;
            }
            string num_str = src.substr(start, pos - start);
            if (dot_count > 1) {
                tokens.push_back(GrassToken(TOK_STRING, num_str, 0.0, line));
            } else {
                double val = 0.0;
                try { val = stod(num_str); } catch (...) {}
                tokens.push_back(GrassToken(TOK_NUMBER, num_str, val, line));
            }
            continue;
        }

        // Дефисные UI-директивы (docs/interface.md)
        static const char* const kHyphenKeywords[] = {
            "header-buttons-offset", "header-buttons-pos", "header-text-color",
            "header-title-align", "auto-size-display", "border-radius-app",
            "placeholder-color", "header-buttons", "header-height",
            "header-color", "window-border", "active-color", "hover-color",
            "button-color", "click-switch", "header-align", "font-weight",
            "clock-format", "time-format", "center-all", "font-style",
            "text-color", "font-size", "window-ob", "align-all",
            "size-top", "click-ob", "z-index"
        };
        bool matched_hk = false;
        for (const char* kw : kHyphenKeywords) {
            size_t klen = strlen(kw);
            if (pos + klen <= src.size() && src.compare(pos, klen, kw) == 0) {
                if (pos + klen == src.size() || (!isalnum((unsigned char)src[pos + klen]) && src[pos + klen] != '_' && src[pos + klen] != '-')) {
                    tokens.push_back(GrassToken(TOK_IDENT, kw, 0.0, line));
                    pos += klen;
                    matched_hk = true;
                    break;
                }
            }
        }
        if (matched_hk) continue;

        // Идентификаторы
        if (isIdentStart((unsigned char)c)) {
            size_t start = pos;
            while (pos < src.size() && isIdentCont((unsigned char)src[pos])) {
                pos++;
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
            case ':':
            case '=': tokens.push_back(GrassToken(TOK_ASSIGN, "=", 0.0, line)); break;
            case '+': tokens.push_back(GrassToken(TOK_PLUS, "+", 0.0, line)); break;
            case '-': tokens.push_back(GrassToken(TOK_MINUS, "-", 0.0, line)); break;
            case '*': tokens.push_back(GrassToken(TOK_STAR, "*", 0.0, line)); break;
            case '/': tokens.push_back(GrassToken(TOK_SLASH, "/", 0.0, line)); break;
            case '%': tokens.push_back(GrassToken(TOK_MOD, "%", 0.0, line)); break;
            case '!': tokens.push_back(GrassToken(TOK_NOT, "!", 0.0, line)); break;
            case '<': tokens.push_back(GrassToken(TOK_LT, "<", 0.0, line)); break;
            case '>': tokens.push_back(GrassToken(TOK_GT, ">", 0.0, line)); break;
            case '.': tokens.push_back(GrassToken(TOK_DOT, ".", 0.0, line)); break;
            default: break;
        }
    }
    tokens.push_back(GrassToken(TOK_EOF, "", 0.0, line));
    return tokens;
}

// ============================================================================
// РЕАЛИЗАЦИЯ КОМПИЛЯТОРА И КОНТРОЛЯ СИНТАКСИСА
// ============================================================================
BytecodeCompiler::BytecodeCompiler(const vector<GrassToken>& toks, const string& fname)
    : tokens(toks), source_filename(fname) {}

GrassToken& BytecodeCompiler::peek(int offset) {
    if (cur + offset >= tokens.size()) return tokens.back();
    return tokens[cur + offset];
}

GrassToken BytecodeCompiler::advance() {
    GrassToken t = peek();
    if (cur < tokens.size() - 1) cur++;
    return t;
}

bool BytecodeCompiler::match(GrassTokenKind kind) {
    if (peek().kind == kind) { advance(); return true; }
    return false;
}

bool BytecodeCompiler::matchIdent(const string& name) {
    if (peek().kind == TOK_IDENT && peek().text == name) { advance(); return true; }
    return false;
}

void BytecodeCompiler::reportError(int line, const string& msg) {
    cerr << "[GrassSyntax Error] (строка " << line << "): " << msg << endl;
    has_error = true;
}

bool BytecodeCompiler::requireLib(const string& lib_key, const string& display_name, const string& feature_desc, int line) {
    if (!included_libs.count(lib_key)) {
        reportError(line, "Использование '" + feature_desc + "' запрещено без подключения библиотеки " + display_name + " в начале файла!");
        return false;
    }
    return true;
}

void BytecodeCompiler::checkBuiltinLibraryRequirement(const string& fn, int line) {
    // 1. Проверка !FastMath!
    if (fn == "discor" || fn == "sqrt" || fn == "sin" || fn == "cos" || fn == "abs" ||
        fn == "round" || fn == "floor" || fn == "ceil" || fn == "pow" || fn == "clamp" ||
        fn == "min" || fn == "max" || fn == "rand" || fn == "lerp") {
        requireLib("fastmath", "!FastMath!", fn + "()", line);
    }
    // 2. Проверка !CoreMath!
    else if (fn == "qb_init" || fn == "h" || fn == "x" || fn == "cnot" ||
             fn == "q_rand" || fn == "pi" || fn == "gcd" || fn == "pow_e") {
        requireLib("coremath", "!CoreMath!", fn + "()", line);
    }
    // 3. Проверка !memory!
    else if (fn.find("memory_") == 0 && fn != "memory_barrier") {
        requireLib("memory", "!memory!", fn + "()", line);
    }
    // 4. Проверка !random!
    else if (fn == "random" || fn == "random_txt" || fn == "stop_random" || fn == "play_random") {
        requireLib("random", "!random!", fn + "()", line);
    }
    // 5. Проверка !database!
    else if (fn.find("db_") == 0 || fn == "table" || fn == "user" || fn == "last_log" ||
             fn == "net_tunnel_protocol" || fn == "net_baypass_state" || fn == "net_bypass_state" ||
             fn == "net_gateway_route" || fn == "remember_me" || fn == "auth_token_hash" ||
             fn == "env_target_runtime" || fn == "env_exec_flags" || fn == "net_bypass_config" ||
             fn == "net_ping_ms" || fn == "env_allocated_ram" || fn == "env_java_path" ||
             fn == "sys_fps_stabilizer" || fn == "sys_gpu_force_integrated") {
        requireLib("db", "!database!", fn + "()", line);
    }
    // 6. Проверка !interface!
    else if (fn.find("gui_") == 0 || fn == "surface" || fn == "auto_size_display" ||
             fn == "border_radius_app" || fn == "border-radius-app" ||
             fn == "switch_tab" || fn == "switch" ||
             fn == "fullscreen" || fn == "fullscrean") {
        requireLib("gui", "!interface!", fn + "()", line);
    }
    // 7. Проверка библиотеки !time!
    else if (fn == "start_time" || fn == "stop_time" || fn == "conclus_time" ||
             fn == "time" || fn == "clock_ms" || fn == "now_ms" ||
             fn == "get_date" || fn == "get_time_str") {
        requireLib("time", "!time!", fn + "()", line);
    }
    // 8. Проверка библиотеки !GrassNet!
    else if (fn == "connect" || fn == "send" || fn == "accept" || fn == "create_serv" ||
             fn == "net_set_agent" || fn == "API" || fn == "ping" || fn == "download" ||
             fn == "resolve_ip" || fn == "local_ip" || fn == "net_status" || fn == "net_close") {
        requireLib("net", "!GrassNet!", fn + "()", line);
    }
    // 9. Проверка библиотеки !json!
    else if (fn == "json_get" || fn == "json_set" || fn == "json_valid" ||
             fn == "json_minify" || fn == "json_clear" || fn == "json_add" ||
             fn == "json_add_num" || fn == "json_build") {
        requireLib("json", "!json!", fn + "()", line);
    }
    // 10. Проверка библиотеки !sys!
    else if (fn == "clip_set" || fn == "clip_get" || fn == "screen_w" ||
             fn == "screen_h" || fn == "pc_name" || fn == "user_name" ||
             fn == "cpu_cores" || fn == "ram_total_mb" || fn == "ram_free_mb" ||
             fn == "get_env" || fn == "uptime_sec") {
        requireLib("sys", "!sys!", fn + "()", line);
    }
    // 11. Проверка библиотеки !proc!
    else if (fn == "proc_find" || fn == "proc_kill" || fn == "proc_mem" ||
             fn == "proc_exists" || fn == "proc_list" ||
             fn == "kill" || fn == "mem" || fn == "list") {
        requireLib("proc", "!proc!", fn + "()", line);
    }
    // 12. Проверка библиотеки !fsys!
    else if (fn == "fs_mkdir" || fn == "fs_is_dir" || fn == "fs_size" ||
             fn == "fs_copy"  || fn == "fs_list" || fn == "fs_read" || fn == "fs_write" || fn == "fs_exists" || fn == "fs_remove") {
        requireLib("fsys", "!fsys!", fn + "()", line);
    }
    // 13. Проверка библиотеки !tg!
    else if (fn == "tg_init" || fn == "tg_poll" || fn == "tg_get_user" ||
             fn == "tg_get_chat_id" || fn == "tg_get_msg_id" || fn == "tg_get_callback" ||
             fn == "tg_send" || fn == "tg_reply" || fn == "tg_send_btn" ||
             fn == "tg_send_inline" || fn == "tg_send_webapp" || fn == "tg_send_photo" ||
             fn == "tg_send_dice" || fn == "tg_edit" || fn == "tg_del") {
        requireLib("tg", "!tg!", fn + "()", line);
    }
    // 14. Проверка библиотеки !2DGraphics!
    else if (fn == "gfx_init" || fn == "gfx_running" || fn == "gfx_clear" ||
             fn == "gfx_rect" || fn == "gfx_circle"  || fn == "gfx_line" ||
             fn == "gfx_text" || fn == "gfx_key"     || fn == "gfx_mouse_x" ||
             fn == "gfx_mouse_y" || fn == "gfx_mouse_down" || fn == "gfx_collide" ||
             fn == "gfx_frame" || fn == "gfx_close") {
        requireLib("2DGraphics", "!2DGraphics!", fn + "()", line);
    }
    // 15. Проверка библиотеки !thread!
    else if (fn == "thread_join" || fn == "thread_detach" || fn == "thread_id" ||
             fn == "thread_sleep" || fn == "mutex_create" || fn == "mutex_lock" ||
             fn == "mutex_unlock" || fn == "mutex_destroy") {
        requireLib("thread", "!thread!", fn + "()", line);
    }
    // 16. Проверка библиотеки !mmio! / !io!
    else if (fn == "mmio_read8" || fn == "mmio_read16" || fn == "mmio_read32" || fn == "mmio_read64" ||
             fn == "mmio_write8" || fn == "mmio_write16" || fn == "mmio_write32" || fn == "mmio_write64" ||
             fn == "io_in8" || fn == "io_in16" || fn == "io_in32" ||
             fn == "inb" || fn == "inw" || fn == "inl" ||
             fn == "io_out8" || fn == "io_out16" || fn == "io_out32" ||
             fn == "outb" || fn == "outw" || fn == "outl" ||
             fn == "memory_barrier" || fn == "read_barrier" || fn == "write_barrier") {
        requireLib("mmio", "!mmio!", fn + "()", line);
    }
    // 17. Проверка библиотеки !audio!
    else if (fn == "tone" || fn == "beep" || fn == "synth_play" || fn == "synth" ||
             fn == "sound_play" || fn == "sound_stop" || fn == "sound_pause" ||
             fn == "sound_resume" || fn == "sound_volume" || fn == "sound_is_playing") {
        requireLib("audio", "!audio!", fn + "()", line);
    }
    // 18. Проверка библиотеки !asm!
    else if (fn == "cli" || fn == "sti" || fn == "hlt" ||
             fn == "in" || fn == "out" ||
             fn == "in8" || fn == "in16" || fn == "in32" ||
             fn == "out8" || fn == "out16" || fn == "out32" ||
             fn == "read_cr0" || fn == "write_cr0" ||
             fn == "read_cr2" || fn == "write_cr2" ||
             fn == "read_cr3" || fn == "write_cr3" ||
             fn == "read_cr4" || fn == "write_cr4" ||
             fn == "read_rflags" || fn == "write_rflags" ||
             fn == "invlpg" || fn == "rdtsc" || fn == "pause" || fn == "nop" ||
             fn == "cpuid_eax" || fn == "cpuid_ebx" || fn == "cpuid_ecx" || fn == "cpuid_edx" ||
             fn == "asm_run" || fn == "asm_bytes" || fn == "asm_hex" || fn == "asm_machine_hex") {
        requireLib("asm", "!asm!", fn + "()", line);
    }
    // 19. Проверка библиотеки !keyboard!
    else if (fn == "kbd_has_key" || fn == "kbd_available" ||
             fn == "kbd_read_scancode" || fn == "kbd_get_scancode" ||
             fn == "kbd_scancode_to_char" || fn == "kbd_scancode_to_ascii" ||
             fn == "kbd_getc" || fn == "kbd_poll" || fn == "kbd_read_line" ||
             fn == "kbd_is_shift" || fn == "kbd_is_caps" ||
             fn == "kbd_is_ctrl" || fn == "kbd_is_alt" ||
             fn == "kbd_is_key_pressed" || fn == "kbd_scancode_name" ||
             fn == "kbd_leds" || fn == "kbd_reset" || fn == "kbd_self_test" ||
             fn == "kbd_wait_ack" || fn == "kbd_set_layout" || fn == "kbd_get_layout") {
        requireLib("keyboard", "!keyboard!", fn + "()", line);
    }
    // 20. Проверка библиотеки !hardware!
    else if (fn.rfind("hw_", 0) == 0 ||
             fn == "cpu_vendor" || fn == "cpu_brand" || fn == "cpu_model" ||
             fn == "cpu_cores" || fn == "cpu_family" || fn == "cpu_freq" ||
             fn == "ram_total" || fn == "ram_used" || fn == "ram_free" || fn == "proc_mem" ||
             fn == "board_vendor" || fn == "board_model" ||
             fn == "bios_vendor" || fn == "bios_date" ||
             fn == "pci_count" || fn == "rdtsc") {
        requireLib("hardware", "!hardware!", fn + "()", line);
    }
    // 21. Проверка библиотеки !baremetal!
    else if (fn == "clear" || fn == "cls" || fn == "not_clear" || fn == "not_clear_on" || fn == "not_clear_off" ||
             fn == "clearable" || fn == "color" || fn == "read_key" ||
             fn == "draw_box" || fn == "menu" || fn == "progress_bar" ||
             fn == "reboot" || fn == "poweroff" ||
             fn == "ls" || fn == "pwd" || fn == "cd" || fn == "copy" || fn == "move" ||
             fn == "delete" || fn == "cr_folder" || fn == "cr_file" ||
             fn == "not_show" || fn == "specSign_show" ||
             fn == "virtual_struct" || fn == "physical_struct" ||
             fn == "bm_exec" || fn == "cat" || fn == "read_file" ||
             fn == "gui" || fn == "vd" || fn == "vd_desktop") {
        requireLib("baremetal", "!baremetal!", fn + "()", line);
    }
}

bool BytecodeCompiler::isTypeOrFuncKeyword(const string& s) {
    return s == "take" || s == "void" || s == "func" || s == "alt" || s == "fn" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile";
}

bool BytecodeCompiler::isVarKeyword(const string& s) {
    return s == "set" || s == "var" || s == "let" || s == "fany" || s == "invent" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile";
}

void BytecodeCompiler::emitByte(uint8_t b) { code.push_back(b); }

void BytecodeCompiler::emitU32(uint32_t v) {
    uint8_t bytes[4];
    memcpy(bytes, &v, 4);
    for (int i = 0; i < 4; ++i) code.push_back(bytes[i]);
}

void BytecodeCompiler::patchU32(size_t offset, uint32_t v) {
    if (offset + 4 <= code.size()) {
        memcpy(&code[offset], &v, 4);
    }
}

void BytecodeCompiler::emitDouble(double d) {
    uint8_t bytes[8];
    memcpy(bytes, &d, 8);
    for (int i = 0; i < 8; ++i) code.push_back(bytes[i]);
}

void BytecodeCompiler::emitString(const string& s) {
    emitU32((uint32_t)s.size());
    for (char c : s) code.push_back((uint8_t)c);
}

void BytecodeCompiler::emitUICmd(const string& cmd_line) {
    emitByte(OP_UI_CMD);
    emitString(cmd_line);
}

void BytecodeCompiler::parseExpression() {
    if (++recursion_depth > MAX_RECURSION_DEPTH) {
        reportError(peek().line, "Превышена максимальная глубина вложенности выражений (Stack Overflow Guard)!");
        --recursion_depth;
        return;
    }
    parseLogicalOr();
    --recursion_depth;
}

void BytecodeCompiler::parseLogicalOr() {
    parseLogicalAnd();
    while (match(TOK_OR) || matchIdent("or")) {
        parseLogicalAnd();
        emitByte(OP_OR);
    }
}

void BytecodeCompiler::parseLogicalAnd() {
    parseComparison();
    while (match(TOK_AND) || matchIdent("and")) {
        parseComparison();
        emitByte(OP_AND);
    }
}

void BytecodeCompiler::parseComparison() {
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

void BytecodeCompiler::parseTerm() {
    parseFactor();
    while (peek().kind == TOK_PLUS || peek().kind == TOK_MINUS) {
        GrassTokenKind op = advance().kind;
        parseFactor();
        emitByte(op == TOK_PLUS ? OP_ADD : OP_SUB);
    }
}

void BytecodeCompiler::parseFactor() {
    parseUnary();
    while (peek().kind == TOK_STAR || peek().kind == TOK_SLASH || peek().kind == TOK_MOD) {
        GrassTokenKind op = advance().kind;
        parseUnary();
        if (op == TOK_STAR) emitByte(OP_MUL);
        else if (op == TOK_SLASH) emitByte(OP_DIV);
        else emitByte(OP_MOD);
    }
}

void BytecodeCompiler::parseUnary() {
    if (match(TOK_MINUS)) { parseUnary(); emitByte(OP_NEG); return; }
    if (match(TOK_NOT))   { parseUnary(); emitByte(OP_NOT); return; }
    if (match(TOK_PLUS))  { parseUnary(); return; }
    parsePrimary();
}

void BytecodeCompiler::parsePrimary() {
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
        if (match(TOK_RBRACKET)) {
            emitByte(OP_NEW_ARRAY);
            emitU32(0);
            return;
        }
        uint32_t count = 0;
        do {
            parseExpression();
            count++;
        } while (match(TOK_COMMA));
        match(TOK_RBRACKET);
        emitByte(OP_NEW_ARRAY);
        emitU32(count);
        return;
    }
    if (peek().kind == TOK_IDENT) {
        int tok_line = peek().line;
        string name = advance().text;
        if (name == "true")  { emitByte(OP_CONST_NUM); emitDouble(1.0); return; }
        if (name == "false") { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }
        if (name == "endl")  { emitByte(OP_CONST_STR); emitString("\n"); return; }

        // 16 цветов VGA для библиотеки !baremetal!
        if (name == "BLACK")         { emitByte(OP_CONST_NUM); emitDouble(0.0); return; }
        if (name == "BLUE")          { emitByte(OP_CONST_NUM); emitDouble(1.0); return; }
        if (name == "GREEN")         { emitByte(OP_CONST_NUM); emitDouble(2.0); return; }
        if (name == "CYAN")          { emitByte(OP_CONST_NUM); emitDouble(3.0); return; }
        if (name == "RED")           { emitByte(OP_CONST_NUM); emitDouble(4.0); return; }
        if (name == "MAGENTA")       { emitByte(OP_CONST_NUM); emitDouble(5.0); return; }
        if (name == "BROWN")         { emitByte(OP_CONST_NUM); emitDouble(6.0); return; }
        if (name == "LIGHT_GRAY")    { emitByte(OP_CONST_NUM); emitDouble(7.0); return; }
        if (name == "DARK_GRAY")     { emitByte(OP_CONST_NUM); emitDouble(8.0); return; }
        if (name == "LIGHT_BLUE")    { emitByte(OP_CONST_NUM); emitDouble(9.0); return; }
        if (name == "LIGHT_GREEN")   { emitByte(OP_CONST_NUM); emitDouble(10.0); return; }
        if (name == "LIGHT_CYAN")    { emitByte(OP_CONST_NUM); emitDouble(11.0); return; }
        if (name == "LIGHT_RED")     { emitByte(OP_CONST_NUM); emitDouble(12.0); return; }
        if (name == "LIGHT_MAGENTA") { emitByte(OP_CONST_NUM); emitDouble(13.0); return; }
        if (name == "YELLOW")        { emitByte(OP_CONST_NUM); emitDouble(14.0); return; }
        if (name == "WHITE")         { emitByte(OP_CONST_NUM); emitDouble(15.0); return; }

        // Индексация массива: myArray[1]
        if (match(TOK_LBRACKET)) {
            parseExpression();
            match(TOK_RBRACKET);
            emitByte(OP_LOAD_VAR); emitString(name);
            emitByte(OP_GET_INDEX);
            return;
        }

        // Вызов метода или доступ к полю с точкой в токене: myArray.push(10), myArray.pop(), player.name
        size_t dot_pos = name.find('.');
        if (dot_pos != string::npos) {
            string obj_name = name.substr(0, dot_pos);
            string member = name.substr(dot_pos + 1);

            if (match(TOK_LPAREN)) {
                if (member == "push") {
                    parseExpression();
                    match(TOK_RPAREN);
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    emitByte(OP_ARRAY_PUSH);
                    return;
                } else if (member == "pop") {
                    match(TOK_RPAREN);
                    emitByte(OP_LOAD_VAR); emitString(obj_name);
                    emitByte(OP_ARRAY_POP);
                    return;
                }
            } else {
                emitByte(OP_LOAD_VAR); emitString(obj_name);
                emitByte(OP_GET_FIELD); emitString(member);
                return;
            }
        }

        // Точка как отдельный токен (с пробелами): obj . member
        if (match(TOK_DOT)) {
            string member = advance().text;
            if (match(TOK_LPAREN)) {
                if (member == "push") {
                    parseExpression();
                    match(TOK_RPAREN);
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_ARRAY_PUSH);
                    return;
                } else if (member == "pop") {
                    match(TOK_RPAREN);
                    emitByte(OP_LOAD_VAR); emitString(name);
                    emitByte(OP_ARRAY_POP);
                    return;
                }
            } else {
                emitByte(OP_LOAD_VAR); emitString(name);
                emitByte(OP_GET_FIELD); emitString(member);
                return;
            }
        }

        // Конструктор структуры: Player()
        if (g_struct_registry.count(name) && match(TOK_LPAREN)) {
            match(TOK_RPAREN);
            emitByte(OP_NEW_STRUCT);
            emitString(name);
            return;
        }

        // Создание потока или асинхронной задачи: thread(fn, args...) или async(fn, args...)
        if ((name == "thread" || name == "async") && match(TOK_LPAREN)) {
            requireLib("thread", "!thread!", name + "()", tok_line);
            string fn_target;
            if (peek().kind == TOK_IDENT || peek().kind == TOK_STRING) {
                fn_target = advance().text;
            } else {
                reportError(tok_line, "Ожидалось имя функции в качестве первого аргумента " + name + "()!");
            }
            uint8_t argc = 0;
            while (match(TOK_COMMA)) {
                parseExpression();
                argc++;
            }
            match(TOK_RPAREN);

            emitByte(name == "thread" ? OP_THREAD_SPAWN : OP_ASYNC_SPAWN);
            if (user_functions.count(fn_target)) {
                emitU32(user_functions[fn_target].entry_ip);
            } else {
                thread_spawn_fixups.push_back({code.size(), fn_target});
                emitU32(0);
            }
            emitByte(argc);
            return;
        }

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

            if (name == "ls" && !has_filesystem_struct) {
                reportError(tok_line, "Ошибка компиляции: вызов 'ls()' требует предварительного объявления файловой структуры через virtual_struct { ... } или physical_struct('путь')!");
            }
            checkBuiltinLibraryRequirement(name, tok_line);
            emitByte(OP_CALL_BUILTIN);
            emitString(name);
            emitByte(argc);
            return;
        }

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
        match(TOK_RPAREN);
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

    // Элементы интерфейса: button "Текст" { ... }, label "Текст" { ... }, enter "Текст" { ... }, clock "HH:mm:ss" { ... }
    if (cmd == "button" || cmd == "label" || cmd == "enter" || cmd == "icon" || cmd == "video" || cmd == "svg" || cmd == "clock") {
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
        } else if (match(TOK_LPAREN)) {
            bool first = true;
            while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
                if (!first) txt += " ";
                txt += advance().text;
                first = false;
            }
            match(TOK_RPAREN);
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT || peek().kind == TOK_NUMBER) {
            txt = advance().text;
        }

        if (cmd == "icon" || cmd == "video" || cmd == "svg") {
            emitUICmd(cmd + "(" + txt + ")");
        } else {
            emitUICmd(cmd + " !!" + txt + "!!");
        }

        if (!inline_id.empty()) {
            emitUICmd("id(" + inline_id + ")");
        }

        if (!match(TOK_LBRACE)) {
            reportError(el_line, "Элемент '" + cmd + "' в едином синтаксисе GrassGreen должен открывать блок свойств '{ ... }'!");
            return true;
        }

        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            if (!parseSingleGuiDirective()) advance();
        }
        match(TOK_RBRACE);
        match(TOK_SEMI);
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
            match(TOK_RPAREN);
        } else if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
            tab_name = advance().text;
        }

        emitUICmd("tab(" + tab_name + ")");

        if (match(TOK_LBRACE)) {
            while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
                if (!parseSingleGuiDirective()) advance();
            }
            match(TOK_RBRACE);
            match(TOK_SEMI);
            emitUICmd("tab(main)");
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
            match(TOK_RPAREN);
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
        cmd == "click-ob" || cmd == "click-switch" || cmd == "click_switch" ||
        cmd == "switch" || cmd == "switch_tab" || cmd == "run" || cmd == "action") {
        advance();
        string arg = extractParenArgsAsString();

        if (cmd == "auto-size-display") cmd = "auto_size_display";
        else if (cmd == "border_radius_app") cmd = "border-radius-app";
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
        } else {
            emitUICmd(cmd + "(" + arg + ")");
        }
        return true;
    }

    return false;
}

void BytecodeCompiler::parseWindowBlock() {
    int win_line = peek(-1).line;
    requireLib("gui", "!interface!", "window { ... }", win_line);

    if (match(TOK_LPAREN)) {
        while (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) advance();
        match(TOK_RPAREN);
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
            advance();
        }
    }
    match(TOK_RBRACE);
    match(TOK_SEMI);
    emitByte(OP_UI_SHOW);
}

void BytecodeCompiler::parseBlock() {
    if (match(TOK_LBRACE)) {
        while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
            parseStatement();
        }
        match(TOK_RBRACE);
    } else {
        reportError(peek().line, "Ожидалась открывающая фигурная скобка '{' для блока кода!");
        parseStatement();
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
    if (match(TOK_SEMI)) return;
    if (peek().kind == TOK_RBRACE || peek().kind == TOK_EOF) return;

    // 0. РЕГИСТРАЦИЯ ПОДКЛЮЧЕННЫХ БИБЛИОТЕК (!GrassGreen!, !interface!, !database! и т.д.)
    if (peek().kind == TOK_LIB) {
        string lib = advance().text;
        if (lib == "!GrassGreen!") {
            included_libs.insert("core");
        } else if (lib == "!sys!") {
            included_libs.insert("sys");
        } else if (lib == "!json!") {
            included_libs.insert("json");
        } else if (lib == "!tg!") {
            included_libs.insert("tg");
        } else if (lib == "!2DGraphics!") {
            included_libs.insert("2DGraphics");
        } else if (lib == "!proc!") {
            included_libs.insert("proc");
        } else if (lib == "!fsys!") {
            included_libs.insert("fsys");
        } else if (lib == "!GrassNet!" || lib == "!net!") {
            included_libs.insert("net");
        } else if (lib == "!interface!" || lib == "!gui!" || lib == "!GrassGUI!") {
            included_libs.insert("gui");
        } else if (lib == "!time!") {
            included_libs.insert("time");
        } else if (lib == "!FastMath!") {
            included_libs.insert("fastmath");
        } else if (lib == "!CoreMath!") {
            included_libs.insert("coremath");
        } else if (lib == "!memory!") {
            included_libs.insert("memory");
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
        } else if (lib == "!database!" || lib == "!gdb!") {
            included_libs.insert("db");
            if (!included_libs.count("core")) is_special_file = true;
            emitByte(OP_LOAD_DB);
        } else if (lib == "!audio!" || lib == "!sound!" || lib == "!GrassAudio!") {
            included_libs.insert("audio");
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
        } else {
            reportError(peek(-1).line, "Неизвестная библиотека: " + lib);
        }
        return;
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
                return;
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
                return;
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
                return;
            } else {
                match(TOK_SEMI);
                emitByte(OP_CONST_NUM);
                emitDouble(8.0);
                emitByte(OP_CALL_BUILTIN);
                emitString("use_utf");
                emitByte(1);
                emitByte(OP_POP);
                GrassKeyboard::set_layout(8);
                return;
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
        return;
    }

    if (peek().kind == TOK_IDENT && peek().text == "not_clear") {
        int nc_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "not_clear", nc_line);
        if (peek().kind == TOK_LBRACE) {
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            parseBlock();
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
            return;
        }
        if (match(TOK_LPAREN)) {
            if (match(TOK_RPAREN)) {
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            } else {
                parseExpression();
                match(TOK_RPAREN);
                emitByte(OP_CALL_BUILTIN); emitString("not_clear"); emitByte(1); emitByte(OP_POP);
            }
        } else {
            emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
        }
        match(TOK_SEMI);
        return;
    }

    if (peek().kind == TOK_IDENT && (peek().text == "clearable" || peek().text == "not_clear_off")) {
        int co_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "clearable", co_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
        return;
    }

    // 0.3 КОМАНДЫ BAREMETAL И VFS (virtual_struct, physical_struct, ls, pwd, cd, reboot, poweroff, etc.)
    if (peek().kind == TOK_IDENT && peek().text == "virtual_struct") {
        parseVirtualStruct();
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "physical_struct") {
        int ps_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "physical_struct", ps_line);
        has_filesystem_struct = true;
        match(TOK_LPAREN);
        string path = "";
        if (peek().kind == TOK_STRING) path = advance().text;
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString(path);
        emitByte(OP_CALL_BUILTIN); emitString("physical_struct"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "bm_exec") {
        int bme_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "bm_exec", bme_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("bm_exec"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && (peek().text == "cat" || peek().text == "read_file")) {
        string cname = peek().text;
        int cat_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", cname, cat_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cat"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && (peek().text == "gui" || peek().text == "vd" || peek().text == "vd_desktop")) {
        string gname = peek().text;
        int gui_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", gname, gui_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("gui"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "reboot") {
        int rb_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "reboot", rb_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("reboot"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "poweroff") {
        int po_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "poweroff", po_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("poweroff"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "pwd") {
        int pwd_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "pwd", pwd_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("pwd"); emitByte(0); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "ls") {
        int ls_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "ls()", ls_line);
        if (!has_filesystem_struct) {
            reportError(ls_line, "Ошибка компиляции: вызов 'ls()' требует предварительного объявления файловой структуры через virtual_struct { ... } или physical_struct('путь')!");
            return;
        }
        uint8_t argc = 0;
        if (match(TOK_LPAREN)) {
            if (!match(TOK_RPAREN)) {
                parseExpression();
                argc = 1;
                match(TOK_RPAREN);
            }
        }
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("ls"); emitByte(argc); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "cd") {
        int cd_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "cd", cd_line);
        if (match(TOK_LPAREN)) {
            parseExpression();
            match(TOK_RPAREN);
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
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "cr_folder") {
        int cf_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "cr_folder", cf_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cr_folder"); emitByte(1); emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("cr_file"); emitByte(argc); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "delete") {
        int del_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "delete", del_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("delete"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "copy") {
        int cp_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "copy", cp_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("copy"); emitByte(2); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "move") {
        int mv_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "move", mv_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_COMMA);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("move"); emitByte(2); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "not_show") {
        int ns_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "not_show", ns_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("not_show"); emitByte(1); emitByte(OP_POP);
        return;
    }
    if (peek().kind == TOK_IDENT && peek().text == "specSign_show") {
        int ss_line = peek().line;
        advance();
        requireLib("baremetal", "!baremetal!", "specSign_show", ss_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("specSign_show"); emitByte(1); emitByte(OP_POP);
        return;
    }

    // 1. ОБЪЯВЛЕНИЕ ФУНКЦИЙ (int main() {}, take my_func() {}, void my_func() {})
    if (peek().kind == TOK_IDENT && isTypeOrFuncKeyword(peek().text) &&
        peek(1).kind == TOK_IDENT && peek(2).kind == TOK_LPAREN) {
        int fn_line = peek().line;
        requireLib("core", "!GrassGreen!", "объявление функции", fn_line);

        advance();
        string fn_name = advance().text;
        vector<string> params;

        match(TOK_LPAREN);
        if (peek().kind != TOK_RPAREN && peek().kind != TOK_EOF) {
            while (true) {
                if (isTypeOrFuncKeyword(peek().text) && peek(1).kind == TOK_IDENT) {
                    advance();
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

        if (!params.empty()) {
            emitByte(OP_PARAM_PROLOGUE);
        }
        for (int i = (int)params.size() - 1; i >= 0; --i) {
            emitByte(OP_STORE_VAR);
            emitString(params[i]);
        }
        if (!params.empty()) {
            emitByte(OP_PARAM_EPILOGUE);
        }

        bool prev_inside = inside_function;
        inside_function = true;
        parseBlock();
        inside_function = prev_inside;

        emitByte(OP_CONST_NUM);
        emitDouble(0.0);
        emitByte(OP_RET);

        patchU32(skip_patch, (uint32_t)code.size());
        return;
    }

    // Объявление структуры: struct Name { ... };
    if (matchIdent("struct")) {
        parseStructDefinition();
        return;
    }

    // Запрет на выполнение кода вне int main() {} или пользовательских функций
    if (!inside_function && !is_special_file) {
        reportError(peek().line, "Инструкция '" + peek().text + "' находится вне функции! Весь исполняемый код должен быть внутри int main() { ... }.");
        advance();
        return;
    }

    if (peek().kind == TOK_LBRACE) {
        parseBlock();
        return;
    }

    if (peek().kind == TOK_SLASH && peek(1).kind == TOK_IDENT && peek(1).text == "n") {
        advance(); advance();
        match(TOK_SEMI);
        emitByte(OP_LOG_EMPTY);
        return;
    }

    if (peek().kind == TOK_IDENT && peek().text == "text" && peek(1).kind == TOK_SEMI) {
        advance(); advance();
        emitByte(OP_LOG_EMPTY);
        return;
    }

    if (matchIdent("end-table")) {
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN);
        emitString("table");
        emitByte(0);
        emitByte(OP_POP);
        return;
    }

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
                match(TOK_RPAREN);
                match(TOK_SEMI);
            } else {
                parseExpression();
                match(TOK_RPAREN);
                match(TOK_SEMI);
                emitByte(OP_CALL_BUILTIN); emitString("asm_run"); emitByte(1);
                emitByte(OP_POP);
                return;
            }
        } else {
            reportError(peek().line, "Ожидалось '{' или '(' после ключевого слова asm");
            return;
        }
        emitByte(OP_CONST_STR); emitString(asm_body);
        emitByte(OP_CALL_BUILTIN); emitString("asm_run"); emitByte(1);
        emitByte(OP_POP);
        return;
    }

    // 2. БЛОК ОКНА ИНТЕРФЕЙСА: window { ... }
    if (matchIdent("window")) {
        parseWindowBlock();
        return;
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
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString(mode);
        emitByte(OP_CALL_BUILTIN); emitString("surface"); emitByte(1);
        emitByte(OP_POP);
        return;
    }

    // Директива авто-масштабирования High-DPI: auto_size_display();
    if (matchIdent("auto_size_display")) {
        int as_line = peek(-1).line;
        requireLib("gui", "!interface!", "auto_size_display()", as_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("auto_size_display"); emitByte(0);
        emitByte(OP_POP);
        return;
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
            match(TOK_RPAREN);
        }
        match(TOK_SEMI);
        emitByte(OP_CONST_STR); emitString("border-radius-app(" + rad + ")");
        emitByte(OP_CALL_BUILTIN); emitString("gui_cmd"); emitByte(1);
        emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("tone"); emitByte(argc);
        emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("beep"); emitByte(argc);
        emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("synth_play"); emitByte(argc);
        emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_play"); emitByte(argc);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_stop")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_stop()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_stop"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_pause")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_pause()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_pause"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_resume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_resume()", aud_line);
        if (match(TOK_LPAREN)) match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_resume"); emitByte(0);
        emitByte(OP_POP);
        return;
    }

    if (matchIdent("sound_volume")) {
        int aud_line = peek(-1).line;
        requireLib("audio", "!audio!", "sound_volume()", aud_line);
        match(TOK_LPAREN);
        parseExpression();
        match(TOK_RPAREN);
        match(TOK_SEMI);
        emitByte(OP_CALL_BUILTIN); emitString("sound_volume"); emitByte(1);
        emitByte(OP_POP);
        return;
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
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) browser_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("device")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) device_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            match(TOK_RBRACE);
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(browser_val);
        emitByte(OP_CONST_STR); emitString(device_val);
        emitByte(OP_CALL_BUILTIN); emitString("net_set_agent"); emitByte(2);
        emitByte(OP_POP);
        return;
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
                    match(TOK_ASSIGN);
                    if (peek().kind == TOK_NUMBER) port_val = advance().num_val;
                    else if (peek().kind != TOK_SEMI) { try { port_val = stod(advance().text); } catch (...) {} }
                    match(TOK_SEMI);
                } else if (matchIdent("IP") || matchIdent("ip")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) ip_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("password")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) pass_val = advance().text;
                    match(TOK_SEMI);
                } else if (matchIdent("certainIP") || matchIdent("certain_ip")) {
                    match(TOK_ASSIGN);
                    if (peek().kind != TOK_SEMI && peek().kind != TOK_RBRACE) cert_ip_val = advance().text;
                    match(TOK_SEMI);
                } else {
                    advance();
                }
            }
            match(TOK_RBRACE);
        }
        match(TOK_SEMI);

        emitByte(OP_CONST_STR); emitString(ip_val);
        emitByte(OP_CONST_NUM); emitDouble(port_val);
        emitByte(OP_CONST_STR); emitString(pass_val);
        emitByte(OP_CONST_STR); emitString(cert_ip_val);
        emitByte(OP_CALL_BUILTIN); emitString("create_serv"); emitByte(4);
        emitByte(OP_POP);
        return;
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
        match(TOK_RPAREN);
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
        return;
    }

    // Команда синхронизации async
    if (matchIdent("async")) {
        int as_line = peek(-1).line;
        requireLib("net", "!GrassNet!", "async", as_line);
        if (match(TOK_LPAREN)) {
            if (peek().kind != TOK_RPAREN) parseExpression();
            else { emitByte(OP_CONST_NUM); emitDouble(10.0); }
            match(TOK_RPAREN);
        } else {
            emitByte(OP_CONST_NUM);
            emitDouble(10.0);
        }
        match(TOK_SEMI);
        emitByte(OP_WAIT);
        return;
    }

    // 3. ПОТОКОВЫЙ ВЫВОД GRASSGREEN / C++: gout / cout << ... << endl;
    if (matchIdent("gout") || matchIdent("cout")) {
        while (match(TOK_SHL)) {
            if (matchIdent("endl")) {
                emitByte(OP_LOG_EMPTY);
            } else if (matchIdent("not_clear")) {
                requireLib("baremetal", "!baremetal!", "not_clear", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_on"); emitByte(0); emitByte(OP_POP);
            } else if (matchIdent("clearable")) {
                requireLib("baremetal", "!baremetal!", "clearable", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("not_clear_off"); emitByte(0); emitByte(OP_POP);
            } else if (matchIdent("clear") || matchIdent("cls")) {
                requireLib("baremetal", "!baremetal!", "clear", peek(-1).line);
                emitByte(OP_CALL_BUILTIN); emitString("clear"); emitByte(0); emitByte(OP_POP);
            } else {
                parseExpression();
                emitByte(OP_PRINT_RAW);
            }
        }
        match(TOK_SEMI);
        return;
    }

    // Потоковый ввод GRASSGREEN / C++: gin / cin >> var;
    if (matchIdent("gin") || matchIdent("cin")) {
        while (match(TOK_SHR)) {
            if (peek().kind == TOK_IDENT) {
                string vname = advance().text;
                emitByte(OP_CALL_BUILTIN);
                emitString("gin");
                emitByte(0);
                emitByte(OP_STORE_VAR);
                emitString(vname);
            }
        }
        match(TOK_SEMI);
        return;
    }

    // 4. Ключевое слово 'log' устарело и запрещено (используется только потоковый вывод gout)
    if (matchIdent("log")) {
        reportError(peek(-1).line, "Функция 'log' устарела и полностью удалена из GrassGreen! Используйте потоковый вывод: gout << ... << endl;");
        return;
    }

    // ВЫВОД В КОНСОЛЬ (print / println)
    if (matchIdent("print") || matchIdent("println")) {
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

    // 5. ВВОД: gin / input / ask
    if (peek().kind == TOK_IDENT &&
        (peek().text == "gin" || peek().text == "gin.getline" ||
         peek().text == "input" || peek().text == "input.getline" || peek().text == "ask")) {
        size_t saved_cur = cur;
        string fn_name = advance().text;
        string builtin_target = (fn_name == "ask" || fn_name == "gin.getline" || fn_name == "input.getline") ? "gin.getline" : "gin";

        if (match(TOK_LPAREN)) {
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

    // Объявление экземпляра структуры: Player player; или Player player = ...;
    if (peek().kind == TOK_IDENT && g_struct_registry.count(peek().text) && peek(1).kind == TOK_IDENT) {
        string s_name = advance().text;
        string v_name = advance().text;
        if (match(TOK_ASSIGN)) {
            parseExpression();
        } else {
            emitByte(OP_NEW_STRUCT);
            emitString(s_name);
        }
        emitByte(OP_STORE_VAR);
        emitString(v_name);
        match(TOK_SEMI);
        return;
    }

    // 6. ОБЪЯВЛЕНИЕ ПЕРЕМЕННЫХ: volatile, take, set, int, string, float, bool, array
    if (peek().kind == TOK_IDENT && peek().text == "volatile") {
        advance();
    }
    if (peek().kind == TOK_IDENT && (isVarKeyword(peek().text) || peek().text == "take")) {
        string decl_kw = advance().text;
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
            } else if (decl_kw == "array") {
                emitByte(OP_NEW_ARRAY);
                emitU32(0);
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

    // 7. УСЛОВИЯ: if, check, chek
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

    // 8. ЦИКЛ FOR
    if (matchIdent("for")) {
        match(TOK_LPAREN);
        parseStatement();

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

    // 9. ЦИКЛ WHILE
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

    // 10. ЦИКЛ AGAIN(N)
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

    // Присваивание по индексу массива: myArray[1] = 20;
    if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
        string arr_name = advance().text;
        match(TOK_LBRACKET);
        parseExpression(); // index
        match(TOK_RBRACKET);
        if (match(TOK_ASSIGN)) {
            parseExpression(); // value
            match(TOK_SEMI);
            emitByte(OP_LOAD_VAR); emitString(arr_name);
            emitByte(OP_SET_INDEX);
            return;
        }
    }

    // 11. ПРИСВАИВАНИЯ И ИНКРЕМЕНТЫ
    if (peek().kind == TOK_IDENT) {
        string ident_text = peek().text;
        size_t dot = ident_text.find('.');
        if (dot != string::npos && peek(1).kind == TOK_ASSIGN) {
            string obj_name = ident_text.substr(0, dot);
            string field_name = ident_text.substr(dot + 1);
            advance(); // consume token with dot
            match(TOK_ASSIGN);
            parseExpression(); // value
            match(TOK_SEMI);
            emitByte(OP_LOAD_VAR); emitString(obj_name);
            emitByte(OP_SET_FIELD); emitString(field_name);
            return;
        }
        if (peek(1).kind == TOK_DOT && peek(2).kind == TOK_IDENT && peek(3).kind == TOK_ASSIGN) {
            string obj_name = advance().text;
            advance(); // consume '.'
            string field_name = advance().text;
            advance(); // consume '='
            parseExpression(); // value
            match(TOK_SEMI);
            emitByte(OP_LOAD_VAR); emitString(obj_name);
            emitByte(OP_SET_FIELD); emitString(field_name);
            return;
        }
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

void BytecodeCompiler::compileProgram() {
    while (peek().kind != TOK_EOF) {
        if (is_special_file) break; // ВЕБ-ФАЙЛЫ НЕ ПАРСЯТСЯ КАК БАЙТ-КОД C++
        if (peek().kind == TOK_RBRACE) {
            advance();
            continue;
        }
        parseStatement();
        if (is_special_file) break;
    }

    if (!is_special_file) {
        if (!included_libs.count("core")) {
            reportError(1, "Отсутствует главный заголовок языка !GrassGreen! в начале файла!");
        }
        if (!user_functions.count("main")) {
            reportError(1, "Отсутствует точка входа 'int main() { ... }'!");
        } else {
            emitByte(OP_CALL_USER);
            emitU32(user_functions["main"].entry_ip);
            emitByte(OP_POP);
        }

        for (const auto& fix : thread_spawn_fixups) {
            if (user_functions.count(fix.second)) {
                patchU32(fix.first, user_functions[fix.second].entry_ip);
            } else {
                reportError(1, "Неизвестная функция для thread / async: '" + fix.second + "'!");
            }
        }
    }

    emitByte(OP_HALT);
}

void BytecodeCompiler::parseStructDefinition() {
    if (peek().kind != TOK_IDENT) {
        reportError(peek().line, "Ожидалось имя структуры после 'struct'!");
        return;
    }
    string sname = advance().text;
    if (!match(TOK_LBRACE)) {
        reportError(peek().line, "Ожидалось '{' после имени структуры!");
        return;
    }

    GrassStructDef def;
    def.name = sname;
    size_t cur_offset = 0;
    size_t max_align = 1;

    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        // Поддержка двух вариантов объявления полей:
        // 1. C-style: string name; или int hp;
        // 2. RoadMap-style: hp: u32, или x: u64
        string f_type, f_name;
        if (peek(1).kind == TOK_ASSIGN) { // ':' транслируется в лексере в TOK_ASSIGN
            f_name = advance().text;
            advance(); // consume ':' / '='
            f_type = advance().text;
            if (match(TOK_COMMA) || match(TOK_SEMI)) {}
        } else {
            f_type = advance().text;
            f_name = advance().text;
            if (match(TOK_SEMI) || match(TOK_COMMA)) {}
        }

        GrassFieldType ft = GrassFieldType::VALUE;
        size_t f_size = 8;
        size_t f_align = 8;

        if (f_type == "u8" || f_type == "uint8" || f_type == "char" || f_type == "byte") {
            ft = GrassFieldType::U8; f_size = 1; f_align = 1;
        } else if (f_type == "bool") {
            ft = GrassFieldType::BOOL; f_size = 1; f_align = 1;
        } else if (f_type == "u16" || f_type == "uint16" || f_type == "short") {
            ft = GrassFieldType::U16; f_size = 2; f_align = 2;
        } else if (f_type == "u32" || f_type == "uint32") {
            ft = GrassFieldType::U32; f_size = 4; f_align = 4;
        } else if (f_type == "int" || f_type == "i32") {
            ft = GrassFieldType::INT; f_size = 4; f_align = 4;
        } else if (f_type == "float" || f_type == "f32") {
            ft = GrassFieldType::FLOAT; f_size = 4; f_align = 4;
        } else if (f_type == "u64" || f_type == "uint64") {
            ft = GrassFieldType::U64; f_size = 8; f_align = 8;
        } else if (f_type == "double" || f_type == "f64" || f_type == "num") {
            ft = GrassFieldType::DOUBLE; f_size = 8; f_align = 8;
        } else if (f_type == "string" || f_type == "str") {
            ft = GrassFieldType::STRING; f_size = 8; f_align = 8;
        } else {
            ft = GrassFieldType::VALUE; f_size = 8; f_align = 8;
        }

        // C-ABI Data Alignment: выравнивание смещения поля
        cur_offset = (cur_offset + f_align - 1) & ~(f_align - 1);

        GrassFieldDef fd;
        fd.name = f_name;
        fd.type = ft;
        fd.offset = cur_offset;
        fd.size = f_size;
        fd.alignment = f_align;

        def.field_indices[f_name] = def.fields.size();
        def.fields.push_back(fd);

        cur_offset += f_size;
        if (f_align > max_align) max_align = f_align;
    }

    match(TOK_RBRACE);
    match(TOK_SEMI); // опциональный ';'

    def.alignment = max_align;
    def.total_size = (cur_offset + max_align - 1) & ~(max_align - 1);
    if (def.total_size == 0) def.total_size = 1;

    g_struct_registry[sname] = def;
}

void BytecodeCompiler::parseVFSBody() {
    while (peek().kind != TOK_RBRACE && peek().kind != TOK_EOF) {
        if (matchIdent("folder") || matchIdent("dir")) {
            string name;
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                name = advance().text;
            }
            emitByte(OP_CONST_STR); emitString(name);
            emitByte(OP_CALL_BUILTIN); emitString("cr_folder"); emitByte(1); emitByte(OP_POP);

            if (peek().kind == TOK_LBRACE) {
                advance(); // {
                emitByte(OP_CONST_STR); emitString(name);
                emitByte(OP_CALL_BUILTIN); emitString("cd"); emitByte(1); emitByte(OP_POP);

                parseVFSBody();
                match(TOK_RBRACE);

                emitByte(OP_CONST_STR); emitString("..");
                emitByte(OP_CALL_BUILTIN); emitString("cd"); emitByte(1); emitByte(OP_POP);
            }
            match(TOK_SEMI);
        } else if (matchIdent("file")) {
            string name;
            string fcontent = "";
            if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                name = advance().text;
            }
            if (match(TOK_COMMA)) {
                if (peek().kind == TOK_STRING || peek().kind == TOK_IDENT) {
                    fcontent = advance().text;
                }
            }
            emitByte(OP_CONST_STR); emitString(name);
            emitByte(OP_CONST_STR); emitString(fcontent);
            emitByte(OP_CALL_BUILTIN); emitString("cr_file"); emitByte(2); emitByte(OP_POP);
            match(TOK_SEMI);
        } else if (match(TOK_SEMI)) {
            continue;
        } else {
            bool saved = inside_function;
            inside_function = true;
            parseStatement();
            inside_function = saved;
        }
    }
}

void BytecodeCompiler::parseVirtualStruct() {
    has_filesystem_struct = true;
    int vs_line = peek().line;
    advance(); // virtual_struct
    requireLib("baremetal", "!baremetal!", "virtual_struct", vs_line);

    emitByte(OP_CALL_BUILTIN);
    emitString("virtual_struct");
    emitByte(0);
    emitByte(OP_POP);

    if (match(TOK_LPAREN)) {
        match(TOK_RPAREN);
        match(TOK_SEMI);
        return;
    }

    if (match(TOK_LBRACE)) {
        parseVFSBody();
        match(TOK_RBRACE);
    }
}
