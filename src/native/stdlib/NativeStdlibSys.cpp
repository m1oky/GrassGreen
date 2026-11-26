#include "native/GrassNativeStdlib.h"
#include <algorithm>

namespace grass::native {

    void GrassNativeStdlib::populate_fsys(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "fs_mkdir", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_is_dir", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_size", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_copy", "fsys", i64_t, { ptr_t, ptr_t }, { "src", "dst" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_list", "fsys", ptr_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_read", "fsys", ptr_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_write", "fsys", i64_t, { ptr_t, ptr_t }, { "path", "content" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_exists", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_remove", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_read_bytes", "fsys", ptr_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_write_bytes", "fsys", i64_t, { ptr_t, ptr_t, i64_t }, { "path", "bytes", "len" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_open_read", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_open_write", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_open_append", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_close", "fsys", i64_t, { i64_t }, { "handle" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_get_size", "fsys", i64_t, { i64_t }, { "handle" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_delete", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_create", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_write_all", "fsys", i64_t, { ptr_t, ptr_t, i64_t }, { "path", "buf", "len" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fs_read_all", "fsys", i64_t, { ptr_t, ptr_t, i64_t }, { "path", "buf", "len" }, false, false, false, "", backend::TargetABI::Win64 });

        // Aliases
        register_function({ "creat", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "redact", "fsys", i64_t, { ptr_t, ptr_t }, { "path", "content" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "read", "fsys", ptr_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "exists", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "remove", "fsys", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_time(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();
        ir::IRType* str_t = ctx.get_ptr_type(ctx.get_i8_type());

        register_function({ "start_time", "time", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "stop_time", "time", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "conclus_time", "time", str_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "get_date", "time", str_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "get_time_str", "time", str_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "now_ms", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "sleep_ms", "time", v_t, { i64_t }, { "ms" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_now_ms", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_now_us", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_frequency", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_counter", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_sleep", "time", i64_t, { i64_t }, { "ms" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time_delay_ms", "time", i64_t, { i64_t }, { "ms" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "timestamp", "time", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "grass_sleep", "time", v_t, { i64_t }, { "ms" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "time", "time", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "clock_ms", "time", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_sys(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "cpu_cores", "sys", i64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "ram_total_mb", "sys", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "ram_free_mb", "sys", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "ram_used_mb", "sys", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "uptime_sec", "sys", f64_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "clip_set", "sys", i64_t, { ptr_t }, { "text" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "clip_get", "sys", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "system", "sys", i64_t, { ptr_t }, { "cmd" }, false, false, false, "", backend::TargetABI::Win64 });

        // Bare-metal I/O and CPU intrinsics (available in Freestanding profile)
        ir::IRType* void_t = ctx.get_void_type();
        register_function({ "port_in8", "sys", i64_t, { i64_t }, { "port" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "port_out8", "sys", void_t, { i64_t, i64_t }, { "port", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cpu_cli", "sys", void_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cpu_sti", "sys", void_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "cpu_hlt", "sys", void_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_proc(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "proc_find", "proc", i64_t, { ptr_t }, { "name" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "proc_kill", "proc", i64_t, { i64_t }, { "pid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "proc_mem", "proc", i64_t, { i64_t }, { "pid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "proc_exists", "proc", i64_t, { i64_t }, { "pid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "proc_list", "proc", ptr_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "find", "proc", i64_t, { ptr_t }, { "name" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "kill", "proc", i64_t, { i64_t }, { "pid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mem", "proc", i64_t, { i64_t }, { "pid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "list", "proc", ptr_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_net(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "local_ip", "net", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "resolve_ip", "net", ptr_t, { ptr_t }, { "host" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "ping", "net", f64_t, { ptr_t, i64_t }, { "host", "port" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "net_set_agent", "net", i64_t, { ptr_t, ptr_t }, { "browser", "device" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "API", "net", ptr_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "create_serv", "net", i64_t, { ptr_t, i64_t, ptr_t, ptr_t }, { "ip", "port", "pass", "certain" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "download", "net", i64_t, { ptr_t, ptr_t }, { "url", "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "net_status", "net", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_json(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "json_clear", "json", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_add", "json", v_t, { ptr_t, ptr_t }, { "key", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_add_num", "json", v_t, { ptr_t, f64_t }, { "key", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_build", "json", ptr_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_valid", "json", i64_t, { ptr_t }, { "json_str" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_get", "json", ptr_t, { ptr_t, ptr_t }, { "json_str", "key" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_set", "json", ptr_t, { ptr_t, ptr_t, ptr_t }, { "json_str", "key", "val" }, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "json_minify", "json", ptr_t, { ptr_t }, { "json_str" }, false, true, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_thread(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();

        register_function({ "thread_join", "thread", i64_t, { i64_t }, { "tid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "thread_detach", "thread", i64_t, { i64_t }, { "tid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "thread_id", "thread", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "thread_sleep", "thread", v_t, { i64_t }, { "ms" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mutex_create", "thread", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mutex_lock", "thread", i64_t, { i64_t }, { "mid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mutex_unlock", "thread", i64_t, { i64_t }, { "mid" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "mutex_destroy", "thread", i64_t, { i64_t }, { "mid" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_ffi(ir::IRContext& ctx) {
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "ffi_open", "ffi", i64_t, { ptr_t }, { "lib_name" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "ffi_sym", "ffi", i64_t, { i64_t, ptr_t }, { "handle", "func_name" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "ffi_call", "ffi", i64_t, { i64_t, ptr_t }, { "func_ptr", "args" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "ffi_close", "ffi", i64_t, { i64_t }, { "handle" }, false, false, false, "", backend::TargetABI::Win64 });
    }

} // namespace grass::native
