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

bool BytecodeCompiler::consume(GrassTokenKind kind, const string& error_msg) {
    if (peek().kind == kind) {
        advance();
        return true;
    }
    reportError(peek().line, error_msg, peek().col);
    return false;
}

void BytecodeCompiler::reportError(int line, const string& msg, int col, const string& hint) {
    diagnostics.push_back(SyntaxDiagnostic(DiagnosticSeverity::Error,
        SourceLocation(source_filename, line, col), msg, hint));
    cerr << "[GrassSyntax Error] (" << source_filename;
    if (!source_filename.empty()) cerr << ":";
    cerr << line << ":" << col << "): " << msg;
    if (!hint.empty()) cerr << " (подсказка: " << hint << ")";
    cerr << endl;
    has_error = true;
}

void BytecodeCompiler::reportError(const SourceLocation& loc, const string& msg, const string& hint) {
    reportError(loc.line, msg, loc.col, hint);
}

void BytecodeCompiler::synchronize() {
    if (peek().kind != TOK_EOF) advance();
    while (peek().kind != TOK_EOF) {
        if (peek(-1).kind == TOK_SEMI) return;
        switch (peek().kind) {
            case TOK_RBRACE:
            case TOK_LBRACE:
            case TOK_SEMI:
                return;
            case TOK_IDENT: {
                const string& txt = peek().text;
                if (isTypeOrFuncKeyword(txt) || isVarKeyword(txt) ||
                    txt == "struct" || txt == "if" || txt == "check" ||
                    txt == "while" || txt == "for" || txt == "again" ||
                    txt == "loop" || txt == "return" || txt == "break" ||
                    txt == "continue" || txt == "gout" || txt == "cout" ||
                    txt == "gin" || txt == "cin" || txt == "clear" ||
                    txt == "window" || txt == "surface" || txt == "tab") {
                    return;
                }
                break;
            }
            case TOK_LIB:
                return;
            default:
                break;
        }
        advance();
    }
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
    else if (fn == "qb_init" || fn == "h" || fn == "x" || fn == "z" || fn == "cnot" ||
             fn == "q_rand" || fn == "q_measure" || fn == "pi" || fn == "gcd" || fn == "pow_e" ||
             fn == "sha256" || fn == "pow_mod" || fn == "crypto_rand") {
        requireLib("coremath", "!CoreMath!", fn + "()", line);
    }
    // 2.1. Проверка !math3d!
    else if (fn.rfind("v3_", 0) == 0 || fn.rfind("m4_", 0) == 0) {
        requireLib("math3d", "!math3d!", fn + "()", line);
    }
    // 2.2. Проверка !gfx3d!
    else if (fn.rfind("g3d_", 0) == 0) {
        requireLib("gfx3d", "!gfx3d!", fn + "()", line);
    }
    // 2.3. Проверка !physics!
    else if (fn.rfind("phys_", 0) == 0) {
        requireLib("physics", "!physics!", fn + "()", line);
    }
    // 3. Проверка !memory!
    else if ((fn.find("memory_") == 0 && fn != "memory_barrier") || fn == "pack_u16" || fn == "pack_u32" || fn == "pack_u64") {
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
             fn == "fullscreen" || fn == "fullscrean" ||
             fn == "set_text" || fn == "get_text" || fn == "set_val" || fn == "get_val" ||
             fn == "set_color" || fn == "set_bg" || fn == "set_visible" ||
             fn == "hide" || fn == "show" || fn == "is_checked" ||
             fn == "add_item" || fn == "clear_list" || fn == "get_selected" ||
             fn == "on_timer") {
        requireLib("gui", "!interface!", fn + "()", line);
    }
    else if (fn == "bind_synth" || fn == "bind_sound") {
        if (!included_libs.count("gui") && !included_libs.count("audio")) {
            reportError(line, "Ошибка: вызов '" + fn + "()' требует подключения библиотеки !interface! или !audio!.");
        }
    }
    else if (fn == "attach_sound") {
        if (!included_libs.count("audio") && !included_libs.count("gui")) {
            reportError(line, "Ошибка: вызов 'attach_sound()' требует подключения библиотеки !audio!.");
        }
    }
    // 7. Проверка библиотеки !time!
    else if (fn == "start_time" || fn == "stop_time" || fn == "conclus_time" ||
             fn == "time" || fn == "clock_ms" || fn == "now_ms" || fn == "sleep_ms" ||
             fn == "time_now_ms" || fn == "time_now_us" || fn == "time_frequency" ||
             fn == "time_counter" || fn == "time_sleep" || fn == "time_delay_ms" ||
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
    else if (fn == "ram_total_mb" || fn == "ram_used_mb" || fn == "ram_free_mb") {
        if (!included_libs.count("sys") && !included_libs.count("hardware")) {
            requireLib("hardware", "!hardware! или !sys!", fn + "()", line);
        }
    }
    else if (fn == "clip_set" || fn == "clip_get" || fn == "screen_w" ||
             fn == "screen_h" || fn == "pc_name" || fn == "user_name" ||
             fn == "cpu_cores" ||
             fn == "get_env" || fn == "uptime_sec" || fn == "cli_args" ||
             fn == "system" || fn == "exec" || fn == "shell") {
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
             fn == "fs_copy"  || fn == "fs_list" || fn == "fs_read" || fn == "fs_write" ||
             fn == "fs_read_bytes" || fn == "fs_write_bytes" ||
             fn == "fs_exists" || fn == "fs_remove") {
        requireLib("fsys", "!fsys!", fn + "()", line);
    }
    // 12.1. Проверка библиотеки !ffi!
    else if (fn == "ffi_open" || fn == "ffi_sym" || fn == "ffi_call" || fn == "ffi_close") {
        requireLib("ffi", "!ffi!", fn + "()", line);
    }
    // 12.2. Проверка библиотеки !langgen!
    else if (fn == "lang_new" || fn == "lang_comment" || fn == "lang_rule" || fn == "lang_parse" ||
             fn == "lang_count" || fn == "lang_cmd" || fn == "lang_arg" || fn == "lang_eval" ||
             fn == "lang_set_var" || fn == "lang_get_var") {
        requireLib("langgen", "!langgen!", fn + "()", line);
    }
    // 13. Проверка библиотеки !tg!
    else if (fn == "tg_init" || fn == "tg_poll" || fn == "tg_get_user" ||
             fn == "tg_get_chat_id" || fn == "tg_get_msg_id" || fn == "tg_get_callback" ||
             fn == "tg_send" || fn == "tg_reply" || fn == "tg_send_btn" ||
             fn == "tg_send_inline" || fn == "tg_send_webapp" || fn == "tg_send_photo" ||
             fn == "tg_send_dice" || fn == "tg_edit" || fn == "tg_del" ||
             fn == "telegram_init" || fn == "send_message" || fn == "send_photo" || fn == "send_document") {
        requireLib("tg", "!tg!", fn + "()", line);
    }
    // 14. Проверка библиотеки !2DGraphics!
    else if (fn.rfind("g2d_", 0) == 0 ||
             fn == "gfx_init" || fn == "gfx_running" || fn == "gfx_clear" ||
             fn == "gfx_rect" || fn == "gfx_circle"  || fn == "gfx_line" ||
             fn == "gfx_text" || fn == "gfx_key"     || fn == "gfx_mouse_x" ||
             fn == "gfx_mouse_y" || fn == "gfx_mouse_down" || fn == "gfx_collide" ||
             fn == "gfx_frame" || fn == "gfx_close" ||
             fn == "create_canvas" || fn == "fill_color" || fn == "draw_rectangle" ||
             fn == "draw_circle" || fn == "draw_line" || fn == "save_image") {
        requireLib("2DGraphics", "!2DGraphics! / !2d!", fn + "()", line);
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
             fn == "gui" || fn == "vd" || fn == "vd_desktop" ||
             fn == "mouse_init" || fn == "mouse_x" || fn == "mouse_y" ||
             fn == "mouse_btn" || fn == "mouse_down" || fn == "mouse_poll" || fn == "mouse_cursor") {
        requireLib("baremetal", "!baremetal!", fn + "()", line);
    }
}

bool BytecodeCompiler::isTypeOrFuncKeyword(const string& s) {
    return s == "fun" || s == "take" || s == "void" || s == "func" || s == "alt" || s == "fn" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
}

bool BytecodeCompiler::isVarKeyword(const string& s) {
    return s == "set" || s == "var" || s == "let" || s == "take" || s == "fany" || s == "invent" ||
           s == "int" || s == "float" || s == "double" || s == "num" ||
           s == "str" || s == "string" || s == "bool" || s == "auto" || s == "array" || s == "volatile" || s == "void" ||
           s == "uint8" || s == "uint16" || s == "uint32" || s == "uint64" ||
           s == "int8" || s == "int16" || s == "int32" || s == "int64" ||
           s == "uint8_t" || s == "uint16_t" || s == "uint32_t" || s == "uint64_t" ||
           s == "int8_t" || s == "int16_t" || s == "int32_t" || s == "int64_t" ||
           s == "u8" || s == "u16" || s == "u32" || s == "u64" ||
           s == "i8" || s == "i16" || s == "i32" || s == "i64" ||
           s == "f32" || s == "f64" || s == "char" || s == "byte" || s == "size_t";
}

void BytecodeCompiler::emitByte(uint8_t b) { code.push_back(b); }

void BytecodeCompiler::pushVMScope() {
    vm_scopes_.emplace_back();
}

void BytecodeCompiler::registerVMVarType(const std::string& var_name, const std::string& struct_name) {
    if (vm_scopes_.empty()) pushVMScope();
    auto& frame = vm_scopes_.back();
    bool already_tracked = false;
    for (const auto& d : frame.declared_var_types) {
        if (d == var_name) { already_tracked = true; break; }
    }
    if (!already_tracked) {
        for (const auto& s : frame.shadowed_var_types) {
            if (s.first == var_name) { already_tracked = true; break; }
        }
    }
    if (!already_tracked) {
        auto it = var_struct_types.find(var_name);
        if (it != var_struct_types.end()) {
            frame.shadowed_var_types.push_back({ var_name, it->second });
        } else {
            frame.declared_var_types.push_back(var_name);
        }
    }
    if (!struct_name.empty()) {
        var_struct_types[var_name] = struct_name;
    } else {
        var_struct_types.erase(var_name);
    }
}

void BytecodeCompiler::registerVMRAIIVar(const std::string& var_name, const std::string& struct_name) {
    if (struct_name.empty()) return;
    bool is_vec_raii = (struct_name.rfind("__vec__:", 0) == 0);
    if (!is_vec_raii) {
        auto it = g_struct_registry.find(struct_name);
        if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    }
    if (vm_scopes_.empty()) pushVMScope();
    vm_scopes_.back().raii_vars.push_back({ var_name, struct_name });
}

bool BytecodeCompiler::isVMVecTypeStart(int offset) {
    return (peek(offset).text == "vec" || peek(offset).text == "vector") && peek(offset + 1).kind == TOK_LT;
}

std::string BytecodeCompiler::parseVMVecInnerType() {
    advance(); // vec / vector
    advance(); // <
    std::string inner;
    if (isVMVecTypeStart()) {
        inner = "vec<" + parseVMVecInnerType() + ">";
    } else {
        inner = advance().text;
        while (match(TOK_COLON_COLON)) {
            if (peek().kind == TOK_IDENT) inner += "::" + advance().text;
        }
    }
    while (match(TOK_STAR)) inner += "*";
    consume(TOK_GT, "Ожидалась '>' в типе vec<T>!");
    return inner;
}

void BytecodeCompiler::emitVMVecElemDtorLoop(const std::string& var_name, const std::string& elem_struct) {
    if (elem_struct.empty()) return;
    auto it = g_struct_registry.find(elem_struct);
    if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    std::string idx_var = "__vec_dtor_i_" + std::to_string(vm_temp_counter_++);
    emitByte(OP_LOAD_VAR); emitString(var_name);
    emitByte(OP_CALL_BUILTIN); emitString("__vec_size"); emitByte(1);
    emitByte(OP_CONST_NUM); emitDouble(1.0);
    emitByte(OP_SUB);
    emitByte(OP_DEF_VAR); emitString(idx_var);

    size_t loop_top = code.size();
    emitByte(OP_LOAD_VAR); emitString(idx_var);
    emitByte(OP_CONST_NUM); emitDouble(0.0);
    emitByte(OP_CMP_GTE);
    emitByte(OP_JMP_IF_FALSE);
    size_t exit_patch = code.size();
    emitU32(0);

    emitByte(OP_LOAD_VAR); emitString(idx_var);
    emitByte(OP_LOAD_VAR); emitString(var_name);
    emitByte(OP_GET_INDEX);
    std::string target_fn = elem_struct + "__dtor";
    if (user_functions.count(target_fn)) {
        emitByte(OP_CALL_USER);
        emitU32(user_functions[target_fn].entry_ip);
    } else {
        emitByte(OP_CALL_USER);
        method_call_fixups.push_back({ code.size(), target_fn });
        emitU32(0);
    }
    emitByte(OP_POP);

    emitByte(OP_LOAD_VAR); emitString(idx_var);
    emitByte(OP_CONST_NUM); emitDouble(1.0);
    emitByte(OP_SUB);
    emitByte(OP_STORE_VAR); emitString(idx_var);
    emitByte(OP_JMP); emitU32((uint32_t)loop_top);
    patchU32(exit_patch, (uint32_t)code.size());
}

void BytecodeCompiler::emitVMVecPopElemDtor(const std::string& var_name, const std::string& elem_struct) {
    if (elem_struct.empty()) return;
    auto it = g_struct_registry.find(elem_struct);
    if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    std::string sz_var = "__vec_pop_sz_" + std::to_string(vm_temp_counter_++);
    emitByte(OP_LOAD_VAR); emitString(var_name);
    emitByte(OP_CALL_BUILTIN); emitString("__vec_size"); emitByte(1);
    emitByte(OP_DEF_VAR); emitString(sz_var);

    emitByte(OP_LOAD_VAR); emitString(sz_var);
    emitByte(OP_CONST_NUM); emitDouble(0.0);
    emitByte(OP_CMP_GT);
    emitByte(OP_JMP_IF_FALSE);
    size_t skip_patch = code.size();
    emitU32(0);

    emitByte(OP_LOAD_VAR); emitString(sz_var);
    emitByte(OP_CONST_NUM); emitDouble(1.0);
    emitByte(OP_SUB);
    emitByte(OP_LOAD_VAR); emitString(var_name);
    emitByte(OP_GET_INDEX);
    std::string target_fn = elem_struct + "__dtor";
    if (user_functions.count(target_fn)) {
        emitByte(OP_CALL_USER);
        emitU32(user_functions[target_fn].entry_ip);
    } else {
        emitByte(OP_CALL_USER);
        method_call_fixups.push_back({ code.size(), target_fn });
        emitU32(0);
    }
    emitByte(OP_POP);
    patchU32(skip_patch, (uint32_t)code.size());
}

void BytecodeCompiler::emitVMDtorCall(const std::string& var_name, const std::string& struct_name) {
    if (struct_name.empty()) return;
    if (struct_name.rfind("__vec__:", 0) == 0) {
        std::string elem_struct = struct_name.substr(8);
        emitVMVecElemDtorLoop(var_name, elem_struct);
        emitByte(OP_LOAD_VAR); emitString(var_name);
        emitByte(OP_CALL_BUILTIN); emitString("__vec_destroy"); emitByte(1);
        emitByte(OP_POP);
        return;
    }
    auto it = g_struct_registry.find(struct_name);
    if (it == g_struct_registry.end() || !it->second.has_destructor) return;
    emitByte(OP_LOAD_VAR); emitString(var_name);
    std::string target_fn = struct_name + "__dtor";
    if (user_functions.count(target_fn)) {
        emitByte(OP_CALL_USER);
        emitU32(user_functions[target_fn].entry_ip);
    } else {
        emitByte(OP_CALL_USER);
        method_call_fixups.push_back({ code.size(), target_fn });
        emitU32(0);
    }
    emitByte(OP_POP);
}

void BytecodeCompiler::emitVMScopesCleanupTo(size_t target_depth) {
    for (size_t idx = vm_scopes_.size(); idx > target_depth; --idx) {
        const auto& vars = vm_scopes_[idx - 1].raii_vars;
        for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
            emitVMDtorCall(it->name, it->struct_type);
        }
    }
}

void BytecodeCompiler::emitVMFieldDtors(const std::string& struct_name) {
    if (struct_name.empty()) return;
    emitByte(OP_LOAD_VAR); emitString("this");
    std::string target_fn = struct_name + "__dtor_fields";
    if (user_functions.count(target_fn)) {
        emitByte(OP_CALL_USER);
        emitU32(user_functions[target_fn].entry_ip);
    } else {
        emitByte(OP_CALL_USER);
        method_call_fixups.push_back({ code.size(), target_fn });
        emitU32(0);
    }
    emitByte(OP_POP);
}

void BytecodeCompiler::popVMScope(bool emit_cleanups) {
    if (vm_scopes_.empty()) return;
    if (emit_cleanups) {
        const auto& vars = vm_scopes_.back().raii_vars;
        for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
            emitVMDtorCall(it->name, it->struct_type);
        }
    }
    for (const auto& d : vm_scopes_.back().declared_var_types) {
        var_struct_types.erase(d);
    }
    for (const auto& s : vm_scopes_.back().shadowed_var_types) {
        var_struct_types[s.first] = s.second;
    }
    vm_scopes_.pop_back();
}

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

std::string BytecodeCompiler::resolveUserFunc(const std::string& name) const {
    if (user_functions.count(name)) return name;
    std::string mangled = name;
    for (size_t p = 0; (p = mangled.find("::", p)) != std::string::npos; ) {
        mangled.replace(p, 2, "__");
    }
    if (user_functions.count(mangled)) return mangled;
    if (using_symbols_.count(name)) {
        std::string sym = using_symbols_.at(name);
        if (user_functions.count(sym)) return sym;
        std::string sym_m = sym;
        for (size_t p = 0; (p = sym_m.find("::", p)) != std::string::npos; ) {
            sym_m.replace(p, 2, "__");
        }
        if (user_functions.count(sym_m)) return sym_m;
    }
    for (const auto& ns : using_namespaces_) {
        std::string q = ns + "::" + name;
        if (user_functions.count(q)) return q;
        std::string qm = q;
        for (size_t p = 0; (p = qm.find("::", p)) != std::string::npos; ) {
            qm.replace(p, 2, "__");
        }
        if (user_functions.count(qm)) return qm;
    }
    return "";
}

