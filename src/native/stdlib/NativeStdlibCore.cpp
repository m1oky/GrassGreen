#include "native/GrassNativeStdlib.h"
#include <algorithm>

namespace grass::native {

    void GrassNativeStdlib::populate_io(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "grass_print_int", "io", v_t, { i64_t }, { "v" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_double", "io", v_t, { f64_t }, { "v" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_str", "io", v_t, { ptr_t }, { "s" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_newline", "io", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_bool", "io", v_t, { i64_t }, { "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_ptr", "io", v_t, { i64_t }, { "p" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_print_char", "io", v_t, { i64_t }, { "ch" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "gout_flush", "io", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_read_int", "io", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_read_str", "io", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_math(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "discor", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "sqrt", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "lerp", "math", f64_t, { f64_t, f64_t, f64_t }, { "a", "b", "t" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "sin", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cos", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "abs", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "round", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "floor", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "ceil", "math", f64_t, { f64_t }, { "x" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "pow", "math", f64_t, { f64_t, f64_t }, { "b", "e" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "rand", "math", i64_t, { i64_t, i64_t }, { "min", "max" }, false, true, false, "", backend::TargetABI::Win64 });

        // CoreMath & Quantum & Crypto
        register_function({ "sha256", "coremath", ptr_t, { ptr_t }, { "input" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "pow_mod", "coremath", i64_t, { i64_t, i64_t, i64_t }, { "base", "exp", "mod" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "crypto_rand", "coremath", i64_t, { i64_t, i64_t }, { "min", "max" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "pi", "coremath", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "gcd", "coremath", i64_t, { i64_t, i64_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "pow_e", "coremath", ptr_t, { f64_t, f64_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "qb_init", "coremath", ptr_t, { i64_t }, { "id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "h", "coremath", ptr_t, { i64_t }, { "id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "x", "coremath", ptr_t, { i64_t }, { "id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "z", "coremath", ptr_t, { i64_t }, { "id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cnot", "coremath", ptr_t, { i64_t, i64_t }, { "ctl", "tgt" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "q_measure", "coremath", i64_t, { i64_t }, { "id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "q_rand", "coremath", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });

        // 3D Math operations
        register_function({ "v3_create", "math3d", ptr_t, { f64_t, f64_t, f64_t }, { "x", "y", "z" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_add", "math3d", ptr_t, { ptr_t, ptr_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_sub", "math3d", ptr_t, { ptr_t, ptr_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_mul", "math3d", ptr_t, { ptr_t, f64_t }, { "a", "s" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_dot", "math3d", f64_t, { ptr_t, ptr_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_cross", "math3d", ptr_t, { ptr_t, ptr_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_length", "math3d", f64_t, { ptr_t }, { "a" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_normalize", "math3d", ptr_t, { ptr_t }, { "a" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "v3_lerp", "math3d", ptr_t, { ptr_t, ptr_t, f64_t }, { "a", "b", "t" }, false, true, false, "", backend::TargetABI::Win64 });

        register_function({ "m4_identity", "math3d", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_translate", "math3d", ptr_t, { ptr_t }, { "v" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_translate_xyz", "math3d", ptr_t, { f64_t, f64_t, f64_t }, { "x", "y", "z" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_rotate", "math3d", ptr_t, { f64_t, ptr_t }, { "angle", "axis" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_rotate_xyz", "math3d", ptr_t, { f64_t, f64_t, f64_t, f64_t }, { "angle", "ax", "ay", "az" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_scale", "math3d", ptr_t, { ptr_t }, { "v" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_scale_xyz", "math3d", ptr_t, { f64_t, f64_t, f64_t }, { "sx", "sy", "sz" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_look_at", "math3d", ptr_t, { ptr_t, ptr_t, ptr_t }, { "eye", "target", "up" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_perspective", "math3d", ptr_t, { f64_t, f64_t, f64_t, f64_t }, { "fov", "aspect", "near", "far" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "m4_mul", "math3d", ptr_t, { ptr_t, ptr_t }, { "a", "b" }, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_memory(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "grass_alloc", "memory", ptr_t, { i64_t }, { "size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_free", "memory", v_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_realloc", "memory", ptr_t, { ptr_t, i64_t }, { "ptr", "new_size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "alloc", "memory", ptr_t, { i64_t }, { "size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "free", "memory", v_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "realloc", "memory", ptr_t, { ptr_t, i64_t }, { "ptr", "new_size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memcpy", "memory", ptr_t, { ptr_t, ptr_t, i64_t }, { "dst", "src", "count" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memset", "memory", ptr_t, { ptr_t, i64_t, i64_t }, { "dst", "val", "count" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mem_alloc", "memory", ptr_t, { i64_t }, { "size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mem_free", "memory", v_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "heap_alloc", "memory", ptr_t, { i64_t }, { "size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "heap_free", "memory", i64_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "heap_realloc", "memory", ptr_t, { ptr_t, i64_t }, { "ptr", "new_size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "heap_get_process_heap", "memory", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_mem_barrier", "memory", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });

        register_function({ "memory_alloc", "memory", i64_t, { i64_t }, { "size" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memory_free_block", "memory", i64_t, { i64_t }, { "block_id" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memory_usit", "memory", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memory_free", "memory", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memory_clean", "memory", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });

        register_function({ "grass_array_create", "memory", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_array_push", "memory", v_t, { ptr_t, i64_t }, { "ptr", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_array_get", "memory", i64_t, { ptr_t, i64_t }, { "ptr", "idx" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_array_set", "memory", v_t, { ptr_t, i64_t, i64_t }, { "ptr", "idx", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_array_len", "memory", i64_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "len", "memory", i64_t, { ptr_t }, { "ptr" }, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_extra(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();
        ir::IRType* str_t = ctx.get_ptr_type(ctx.get_i8_type());

        // Sys extras
        register_function({ "pc_name", "sys", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "user_name", "sys", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "screen_w", "sys", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "screen_h", "sys", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "get_env", "sys", str_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });

        // Audio
        register_function({ "audio_init", "audio", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "synth_play", "audio", v_t, { ptr_t, f64_t, i64_t, f64_t }, { "wave", "freq", "dur_ms", "vol" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "synth", "audio", v_t, { ptr_t, f64_t, i64_t, f64_t }, { "wave", "freq", "dur_ms", "vol" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tone", "audio", v_t, { f64_t, i64_t, f64_t }, { "freq", "dur_ms", "vol" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "beep", "audio", v_t, { f64_t, i64_t }, { "freq", "dur_ms" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_play", "audio", i64_t, { ptr_t, i64_t }, { "path", "loop" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_stop", "audio", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_pause", "audio", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_resume", "audio", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_volume", "audio", v_t, { i64_t }, { "vol" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "sound_is_playing", "audio", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "bind_synth", "audio", v_t, { ptr_t, ptr_t, f64_t, i64_t, f64_t }, { "target", "wave", "freq", "dur", "vol" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "bind_sound", "audio", v_t, { ptr_t, ptr_t }, { "target", "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "attach_sound", "audio", v_t, { ptr_t, ptr_t, f64_t, i64_t, f64_t }, { "fn", "wave", "freq", "dur", "vol" }, false, false, false, "", backend::TargetABI::Win64 });

        // Telegram
        register_function({ "tg_init", "tg", v_t, { ptr_t }, { "token" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_poll", "tg", str_t, { i64_t }, { "timeout" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_get_user", "tg", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_get_chat_id", "tg", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_get_msg_id", "tg", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_get_callback", "tg", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send", "tg", i64_t, { ptr_t, ptr_t }, { "chat", "text" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_reply", "tg", i64_t, { ptr_t }, { "text" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send_btn", "tg", i64_t, { ptr_t, ptr_t, ptr_t }, { "chat", "text", "btns" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send_inline", "tg", i64_t, { ptr_t, ptr_t, ptr_t }, { "chat", "text", "btns" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send_webapp", "tg", i64_t, { ptr_t, ptr_t, ptr_t, ptr_t }, { "chat", "text", "title", "url" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send_photo", "tg", i64_t, { ptr_t, ptr_t, ptr_t }, { "chat", "url", "cap" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_send_dice", "tg", i64_t, { ptr_t, ptr_t }, { "chat", "emoji" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_edit", "tg", i64_t, { ptr_t, ptr_t, ptr_t }, { "chat", "msg_id", "text" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "tg_del", "tg", i64_t, { ptr_t, ptr_t }, { "chat", "msg_id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "telegram_init", "tg", v_t, { ptr_t }, { "token" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "send_message", "tg", i64_t, { i64_t, ptr_t }, { "chat_id", "text" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "send_photo", "tg", i64_t, { i64_t, ptr_t }, { "chat_id", "url" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "send_document", "tg", i64_t, { i64_t, ptr_t }, { "chat_id", "path" }, false, false, false, "", backend::TargetABI::Win64 });

        // LangGen
        register_function({ "lang_new", "langgen", i64_t, { ptr_t }, { "name" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_comment", "langgen", i64_t, { i64_t, ptr_t }, { "id", "prefix" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_rule", "langgen", i64_t, { i64_t, ptr_t, ptr_t }, { "id", "name", "pat" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_parse", "langgen", i64_t, { i64_t, ptr_t }, { "id", "src" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_count", "langgen", i64_t, { i64_t }, { "prog" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_cmd", "langgen", str_t, { i64_t, i64_t }, { "prog", "idx" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_arg", "langgen", str_t, { i64_t, i64_t, i64_t }, { "prog", "idx", "arg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_eval", "langgen", i64_t, { i64_t, ptr_t }, { "id", "expr" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_set_var", "langgen", v_t, { i64_t, ptr_t, i64_t }, { "id", "name", "val" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "lang_get_var", "langgen", i64_t, { i64_t, ptr_t }, { "id", "name" }, false, false, false, "", backend::TargetABI::Win64 });

        // Hardware
        register_function({ "hw_cpu_vendor", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_brand", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_cores", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_threads", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_sockets", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_family", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_freq", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_base_freq", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_usage", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_temp", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_cache_l1", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_cache_l2", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_cache_l3", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_cpu_virt", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_uptime_sec", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_uptime_str", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_total", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_used", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_free", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_cached", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_speed", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_temp", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_model", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_ram_form", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_size", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_free", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_format", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_model", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_type", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_temp", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_read_speed", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_write_speed", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_resp_time", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_usage", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_disk_is_system", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_pagefile_total", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_pagefile_used", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_adapter", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_type", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_ipv4", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_ipv6", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_recv_speed", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_send_speed", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_recv_total", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_net_send_total", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_name", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_usage", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_temp", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_vram_total", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_vram_used", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_gpu_vram_shared", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_board_vendor", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_board_model", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_bios_vendor", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_bios_date", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_bios_version", "hardware", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_pci_count", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_rdtsc", "hardware", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "hw_reboot", "hardware", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        // Net wrappers & Backlog & MMIO & GUI & Web & Thread
        register_function({ "net_connect_host", "net", i64_t, { ptr_t, i64_t }, { "host", "port" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "net_accept_srv", "net", str_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "net_close_srv", "net", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fat_status", "sys", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "ticks", "sys", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "threads", "sys", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_sleep_sec", "sys", v_t, { i64_t }, { "sec" }, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "inw", "mmio", i64_t, { i64_t }, { "port" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "inl", "mmio", i64_t, { i64_t }, { "port" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "outw", "mmio", v_t, { i64_t, i64_t }, { "port", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "outl", "mmio", v_t, { i64_t, i64_t }, { "port", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "inb", "mmio", i64_t, { i64_t }, { "port" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "outb", "mmio", v_t, { i64_t, i64_t }, { "port", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_read8", "mmio", i64_t, { i64_t }, { "addr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_read16", "mmio", i64_t, { i64_t }, { "addr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_read32", "mmio", i64_t, { i64_t }, { "addr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_read64", "mmio", i64_t, { i64_t }, { "addr" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_write8", "mmio", v_t, { i64_t, i64_t }, { "addr", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_write16", "mmio", v_t, { i64_t, i64_t }, { "addr", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_write32", "mmio", v_t, { i64_t, i64_t }, { "addr", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "mmio_write64", "mmio", v_t, { i64_t, i64_t }, { "addr", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "memory_barrier", "mmio", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "read_barrier", "mmio", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "write_barrier", "mmio", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });

        register_function({ "gui_cmd", "gui", v_t, { ptr_t }, { "cmd" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gui_show", "gui", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gui_get", "gui", str_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gui_set", "gui", v_t, { ptr_t, ptr_t }, { "k", "v" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "surface", "gui", v_t, { ptr_t }, { "mode" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "auto_size_display", "gui", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "set_text", "gui", v_t, { ptr_t, ptr_t }, { "id", "txt" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "get_text", "gui", str_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "set_val", "gui", v_t, { ptr_t, ptr_t }, { "id", "val" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "get_val", "gui", f64_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "set_color", "gui", v_t, { ptr_t, ptr_t }, { "id", "col" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "set_bg", "gui", v_t, { ptr_t, ptr_t }, { "id", "col" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "set_visible", "gui", v_t, { ptr_t, i64_t }, { "id", "vis" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "is_checked", "gui", i64_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "add_item", "gui", v_t, { ptr_t, ptr_t }, { "id", "item" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "clear_list", "gui", v_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "get_selected", "gui", str_t, { ptr_t }, { "id" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "switch_tab", "gui", v_t, { ptr_t }, { "tab" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fullscreen", "gui", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "web_start", "web", v_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "web_compile", "web", str_t, { ptr_t, ptr_t }, { "src", "out" }, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "grass_native_thread_spawn", "thread", i64_t, { ptr_t, ptr_t }, { "fn", "arg" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_native_thread_id", "thread", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
    }

} // namespace grass::native
