#include "native/GrassNativeStdlib.h"
#include <algorithm>

namespace grass::native {

    GrassNativeStdlib& GrassNativeStdlib::instance() {
        static GrassNativeStdlib inst;
        return inst;
    }

    GrassNativeStdlib::GrassNativeStdlib() = default;

    void GrassNativeStdlib::initialize(ir::IRContext& ctx) {
        if (initialized_ && current_ctx_ == &ctx) return;
        initialized_ = true;
        current_ctx_ = &ctx;
        registry_.clear();
        extern_registry_.clear();
        module_map_.clear();

        populate_io(ctx);
        populate_fsys(ctx);
        populate_math(ctx);
        populate_time(ctx);
        populate_sys(ctx);
        populate_proc(ctx);
        populate_net(ctx);
        populate_json(ctx);
        populate_disp(ctx);
        populate_game2d(ctx);
        populate_physics3d(ctx);
        populate_memory(ctx);
        populate_thread(ctx);
        populate_ffi(ctx);
        populate_extra(ctx);

        register_standard_os_externs(ctx);
    }

    void GrassNativeStdlib::register_function(const NativeFunctionDesc& desc) {
        registry_[desc.name] = desc;
        if (!desc.module.empty()) {
            module_map_[desc.module].push_back(desc.name);
        }
    }

    void GrassNativeStdlib::register_extern(const std::string& symbol,
                                           const std::string& lib_name,
                                           ir::IRType* ret_type,
                                           const std::vector<ir::IRType*>& param_types,
                                           backend::TargetABI abi) {
        NativeFunctionDesc desc;
        desc.name = symbol;
        desc.module = "extern";
        desc.return_type = ret_type;
        desc.param_types = param_types;
        desc.is_variadic = false;
        desc.is_freestanding = false;
        desc.is_extern = true;
        desc.extern_library = lib_name;
        desc.abi = abi;

        extern_registry_[symbol] = desc;
        register_function(desc);
    }

    const NativeFunctionDesc* GrassNativeStdlib::find_function(const std::string& name) const {
        auto it = registry_.find(name);
        return (it != registry_.end()) ? &it->second : nullptr;
    }

    bool GrassNativeStdlib::has_function(const std::string& name) const {
        return registry_.find(name) != registry_.end();
    }

    bool GrassNativeStdlib::is_available_in_profile(const std::string& name, RuntimeProfile profile) const {
        const NativeFunctionDesc* desc = find_function(name);
        if (!desc) return false;
        if (profile == RuntimeProfile::Freestanding) {
            return desc->is_freestanding;
        }
        return true;
    }

    std::vector<std::string> GrassNativeStdlib::get_supported_modules() const {
        std::vector<std::string> mods;
        for (const auto& kv : module_map_) {
            mods.push_back(kv.first);
        }
        std::sort(mods.begin(), mods.end());
        return mods;
    }

    std::vector<NativeFunctionDesc> GrassNativeStdlib::get_module_functions(const std::string& module_name) const {
        std::vector<NativeFunctionDesc> res;
        auto it = module_map_.find(module_name);
        if (it != module_map_.end()) {
            for (const auto& fn_name : it->second) {
                const NativeFunctionDesc* d = find_function(fn_name);
                if (d) res.push_back(*d);
            }
        }
        return res;
    }

    void GrassNativeStdlib::register_all_into_module(ir::IRModule& module, RuntimeProfile profile) {
        for (const auto& kv : registry_) {
            const NativeFunctionDesc& desc = kv.second;
            if (profile == RuntimeProfile::Freestanding && !desc.is_freestanding) {
                continue;
            }
            if (module.get_function(desc.name)) {
                continue;
            }
            ir::IRFunction* fn = module.create_function(desc.name, desc.return_type);
            fn->is_declaration = true;
            for (size_t i = 0; i < desc.param_types.size(); ++i) {
                std::string arg_name = (i < desc.param_names.size()) ? desc.param_names[i] : ("arg" + std::to_string(i));
                fn->add_argument(arg_name, desc.param_types[i]);
            }
        }
    }

    void GrassNativeStdlib::register_module_into_module(ir::IRModule& module, const std::string& module_name, RuntimeProfile profile) {
        auto it = module_map_.find(module_name);
        if (it == module_map_.end()) return;

        for (const auto& fn_name : it->second) {
            const NativeFunctionDesc* desc = find_function(fn_name);
            if (!desc) continue;
            if (profile == RuntimeProfile::Freestanding && !desc->is_freestanding) {
                continue;
            }
            if (module.get_function(desc->name)) {
                continue;
            }
            ir::IRFunction* fn = module.create_function(desc->name, desc->return_type);
            for (size_t i = 0; i < desc->param_types.size(); ++i) {
                std::string arg_name = (i < desc->param_names.size()) ? desc->param_names[i] : ("arg" + std::to_string(i));
                fn->add_argument(arg_name, desc->param_types[i]);
            }
        }
    }

    const NativeFunctionDesc* GrassNativeStdlib::find_extern(const std::string& symbol) const {
        auto it = extern_registry_.find(symbol);
        return (it != extern_registry_.end()) ? &it->second : nullptr;
    }

    std::vector<std::string> GrassNativeStdlib::get_registered_externs() const {
        std::vector<std::string> syms;
        for (const auto& kv : extern_registry_) {
            syms.push_back(kv.first);
        }
        std::sort(syms.begin(), syms.end());
        return syms;
    }

    /* ========================================================================= */
    /* Populating System Modules                                                 */
    /* ========================================================================= */

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

    void GrassNativeStdlib::populate_disp(ir::IRContext& ctx) {
        (void)ctx;
    }

    void GrassNativeStdlib::populate_game2d(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "g2d_window", "game2d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_is_open", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_clear", "game2d", v_t, { i64_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_clear_rgb", "game2d", v_t, { f64_t, f64_t, f64_t }, { "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_present", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_dt", "game2d", f64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_key_down", "game2d", i64_t, { i64_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_key_down_str", "game2d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_x", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_y", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_mouse_btn", "game2d", i64_t, { i64_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_cam_shake", "game2d", v_t, { f64_t, f64_t }, { "intensity", "duration" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_emit_particles", "game2d", v_t, { f64_t, f64_t, i64_t, i64_t }, { "x", "y", "count", "color" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_emit_particles_ex", "game2d", v_t, { f64_t, f64_t, i64_t, i64_t, f64_t, f64_t }, { "x", "y", "count", "color", "speed", "lifetime" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_update_particles", "game2d", v_t, { f64_t }, { "dt" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_cam_set", "game2d", v_t, { f64_t, f64_t, f64_t, f64_t }, { "x", "y", "zoom", "angle" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_load_tex", "game2d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_draw_sprite", "game2d", v_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "tex", "x", "y", "w", "h", "angle", "alpha" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_draw_frame", "game2d", v_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "tex", "fx", "fy", "fw", "fh", "x", "y", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_bind_framebuffer", "game2d", v_t, { ptr_t, i64_t, i64_t, i64_t }, { "addr", "w", "h", "pitch" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_rect", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, i64_t }, { "x", "y", "w", "h", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_circle", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "cx", "cy", "r", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, i64_t, i64_t }, { "x1", "y1", "x2", "y2", "hex", "width" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_text", "game2d", v_t, { i64_t, i64_t, ptr_t, i64_t, i64_t }, { "x", "y", "str", "size", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g2d_close", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "gfx_init", "game2d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_running", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_clear", "game2d", v_t, { ptr_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_rect", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, ptr_t }, { "x", "y", "w", "h", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_circle", "game2d", v_t, { i64_t, i64_t, i64_t, ptr_t }, { "x", "y", "r", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t, ptr_t, i64_t }, { "x1", "y1", "x2", "y2", "hex", "w" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_text", "game2d", v_t, { i64_t, i64_t, ptr_t, i64_t, ptr_t }, { "x", "y", "str", "sz", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_key", "game2d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_x", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_y", "game2d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_mouse_down", "game2d", i64_t, { ptr_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_collide", "game2d", i64_t, { f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "x1", "y1", "w1", "h1", "x2", "y2", "w2", "h2" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_frame", "game2d", v_t, { i64_t }, { "fps" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "gfx_close", "game2d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "create_canvas", "game2d", i64_t, { i64_t, i64_t }, { "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "fill_color", "game2d", v_t, { ptr_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_rectangle", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "x", "y", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_circle", "game2d", v_t, { i64_t, i64_t, i64_t }, { "cx", "cy", "r" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "draw_line", "game2d", v_t, { i64_t, i64_t, i64_t, i64_t }, { "x1", "y1", "x2", "y2" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "save_image", "game2d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
    }

    void GrassNativeStdlib::populate_physics3d(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* f64_t = ctx.get_f64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        register_function({ "g3d_window", "physics3d", i64_t, { ptr_t, i64_t, i64_t }, { "title", "w", "h" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_is_open", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_clear", "physics3d", v_t, { i64_t }, { "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_clear_rgb", "physics3d", v_t, { f64_t, f64_t, f64_t }, { "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_present", "physics3d", v_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dt", "physics3d", f64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_key_down", "physics3d", i64_t, { i64_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_key_down_str", "physics3d", i64_t, { ptr_t }, { "key" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_x", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_y", "physics3d", i64_t, {}, {}, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_mouse_btn", "physics3d", i64_t, { i64_t }, { "btn" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dir_light", "physics3d", v_t, { ptr_t, i64_t }, { "dir", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_dir_light_rgb", "physics3d", v_t, { ptr_t, f64_t, f64_t, f64_t, f64_t }, { "dir", "r", "g", "b", "intensity" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_point_light", "physics3d", v_t, { ptr_t, f64_t, f64_t, f64_t, f64_t }, { "pos", "r", "g", "b", "range" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_fog", "physics3d", v_t, { f64_t, f64_t, f64_t, f64_t }, { "r", "g", "b", "density" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_skybox", "physics3d", v_t, { i64_t, i64_t }, { "top_rgb", "bottom_rgb" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_bind_framebuffer", "physics3d", v_t, { ptr_t, i64_t, i64_t, i64_t }, { "addr", "w", "h", "pitch" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_camera", "physics3d", v_t, { ptr_t, ptr_t, f64_t }, { "pos", "target", "fov" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_camera_xyz", "physics3d", v_t, { f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "px", "py", "pz", "tx", "ty", "tz", "fov" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_cube", "physics3d", i64_t, { f64_t }, { "size" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_box", "physics3d", i64_t, { f64_t, f64_t, f64_t }, { "w", "h", "d" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_sphere", "physics3d", i64_t, { f64_t, i64_t }, { "r", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_plane", "physics3d", i64_t, { f64_t, f64_t }, { "w", "d" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_plane_segs", "physics3d", i64_t, { f64_t, f64_t, i64_t }, { "w", "d", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_load_obj", "physics3d", i64_t, { ptr_t }, { "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_texture", "physics3d", v_t, { i64_t, ptr_t }, { "mesh_id", "path" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_color", "physics3d", v_t, { i64_t, i64_t }, { "mesh_id", "hex" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_set_color_rgb", "physics3d", v_t, { i64_t, f64_t, f64_t, f64_t }, { "mesh_id", "r", "g", "b" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_transform", "physics3d", v_t, { i64_t, ptr_t, ptr_t, ptr_t }, { "mesh_id", "pos", "rot", "scale" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_transform_mat", "physics3d", v_t, { i64_t, ptr_t }, { "mesh_id", "mat" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "g3d_draw", "physics3d", v_t, { i64_t }, { "mesh_id" }, false, false, false, "", backend::TargetABI::Win64 });

        register_function({ "phys_world", "physics3d", i64_t, { f64_t, f64_t, f64_t }, { "gx", "gy", "gz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_step", "physics3d", v_t, { i64_t, f64_t, i64_t }, { "world", "dt", "substeps" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_box", "physics3d", i64_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "world", "px", "py", "pz", "sx", "sy", "sz", "mass" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_sphere", "physics3d", i64_t, { i64_t, f64_t, f64_t, f64_t, f64_t, f64_t }, { "world", "px", "py", "pz", "r", "mass" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_cloth", "physics3d", i64_t, { i64_t, ptr_t, f64_t, f64_t, i64_t }, { "world", "pos", "w", "h", "segs" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_link", "physics3d", v_t, { i64_t, i64_t, i64_t, f64_t, f64_t }, { "world", "a", "b", "stiff", "brk" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_get_pos", "physics3d", ptr_t, { i64_t, i64_t }, { "world", "body" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_get_rot", "physics3d", ptr_t, { i64_t, i64_t }, { "world", "body" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_set_pos", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "x", "y", "z" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_set_vel", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "vx", "vy", "vz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_impulse", "physics3d", v_t, { i64_t, i64_t, f64_t, f64_t, f64_t }, { "world", "body", "fx", "fy", "fz" }, false, false, false, "", backend::TargetABI::Win64 });
        register_function({ "phys_raycast", "physics3d", i64_t, { i64_t, ptr_t, ptr_t, f64_t }, { "world", "o", "d", "max_dist" }, false, false, false, "", backend::TargetABI::Win64 });
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
        register_function({ "clear", "sys", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "not_clear_on", "sys", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });
        register_function({ "not_clear_off", "sys", v_t, {}, {}, false, true, false, "", backend::TargetABI::Win64 });

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

    /* ========================================================================= */
    /* OS FFI (extern) Catalog                                                   */
    /* ========================================================================= */

    void GrassNativeStdlib::register_standard_os_externs(ir::IRContext& ctx) {
        ir::IRType* v_t   = ctx.get_void_type();
        ir::IRType* i16_t = ctx.get_i16_type();
        ir::IRType* i32_t = ctx.get_i32_type();
        ir::IRType* i64_t = ctx.get_i64_type();
        ir::IRType* ptr_t = ctx.get_ptr_type();

        // 1. kernel32.dll (Win64)
        register_extern("ExitProcess", "kernel32.dll", v_t, { i32_t }, backend::TargetABI::Win64);
        register_extern("GetStdHandle", "kernel32.dll", ptr_t, { i32_t }, backend::TargetABI::Win64);
        register_extern("WriteFile", "kernel32.dll", i32_t, { ptr_t, ptr_t, i32_t, ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("ReadFile", "kernel32.dll", i32_t, { ptr_t, ptr_t, i32_t, ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("CreateFileA", "kernel32.dll", ptr_t, { ptr_t, i32_t, i32_t, ptr_t, i32_t, i32_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("CloseHandle", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("GetFileSizeEx", "kernel32.dll", i32_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("DeleteFileA", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("CreateDirectoryA", "kernel32.dll", i32_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("GetFileAttributesA", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("CopyFileA", "kernel32.dll", i32_t, { ptr_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("lstrlenA", "kernel32.dll", i64_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("lstrlenW", "kernel32.dll", i64_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("Sleep", "kernel32.dll", v_t, { i32_t }, backend::TargetABI::Win64);
        register_extern("GetTickCount64", "kernel32.dll", i64_t, {}, backend::TargetABI::Win64);
        register_extern("QueryPerformanceFrequency", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("QueryPerformanceCounter", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("GetProcessHeap", "kernel32.dll", ptr_t, {}, backend::TargetABI::Win64);
        register_extern("HeapAlloc", "kernel32.dll", ptr_t, { ptr_t, i32_t, i64_t }, backend::TargetABI::Win64);
        register_extern("HeapFree", "kernel32.dll", i32_t, { ptr_t, i32_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("HeapReAlloc", "kernel32.dll", ptr_t, { ptr_t, i32_t, ptr_t, i64_t }, backend::TargetABI::Win64);
        register_extern("LoadLibraryA", "kernel32.dll", ptr_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("GetProcAddress", "kernel32.dll", ptr_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("FreeLibrary", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("VirtualAlloc", "kernel32.dll", ptr_t, { ptr_t, i64_t, i32_t, i32_t }, backend::TargetABI::Win64);
        register_extern("VirtualFree", "kernel32.dll", i32_t, { ptr_t, i64_t, i32_t }, backend::TargetABI::Win64);
        register_extern("GetCommandLineA", "kernel32.dll", ptr_t, {}, backend::TargetABI::Win64);
        register_extern("GetEnvironmentVariableA", "kernel32.dll", i32_t, { ptr_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("SetEnvironmentVariableA", "kernel32.dll", i32_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("GetComputerNameA", "kernel32.dll", i32_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("CreateProcessA", "kernel32.dll", i32_t, { ptr_t, ptr_t, ptr_t, ptr_t, i32_t, i32_t, ptr_t, ptr_t, ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("WinExec", "kernel32.dll", i32_t, { ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("CreateThread", "kernel32.dll", ptr_t, { ptr_t, i64_t, ptr_t, ptr_t, i32_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("WaitForSingleObject", "kernel32.dll", i32_t, { ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("CreateMutexA", "kernel32.dll", ptr_t, { ptr_t, i32_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("ReleaseMutex", "kernel32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("GetLastError", "kernel32.dll", i32_t, {}, backend::TargetABI::Win64);
        register_extern("SetLastError", "kernel32.dll", v_t, { i32_t }, backend::TargetABI::Win64);

        // 2. user32.dll (Win64)
        register_extern("MessageBoxA", "user32.dll", i32_t, { ptr_t, ptr_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("MessageBoxW", "user32.dll", i32_t, { ptr_t, ptr_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("GetSystemMetrics", "user32.dll", i32_t, { i32_t }, backend::TargetABI::Win64);
        register_extern("OpenClipboard", "user32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("CloseClipboard", "user32.dll", i32_t, {}, backend::TargetABI::Win64);
        register_extern("GetClipboardData", "user32.dll", ptr_t, { i32_t }, backend::TargetABI::Win64);
        register_extern("SetClipboardData", "user32.dll", ptr_t, { i32_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("EmptyClipboard", "user32.dll", i32_t, {}, backend::TargetABI::Win64);
        register_extern("GetAsyncKeyState", "user32.dll", i16_t, { i32_t }, backend::TargetABI::Win64);

        // 3. ws2_32.dll (Win64)
        register_extern("WSAStartup", "ws2_32.dll", i32_t, { i16_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("WSACleanup", "ws2_32.dll", i32_t, {}, backend::TargetABI::Win64);
        register_extern("socket", "ws2_32.dll", i64_t, { i32_t, i32_t, i32_t }, backend::TargetABI::Win64);
        register_extern("connect", "ws2_32.dll", i32_t, { i64_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("send", "ws2_32.dll", i32_t, { i64_t, ptr_t, i32_t, i32_t }, backend::TargetABI::Win64);
        register_extern("recv", "ws2_32.dll", i32_t, { i64_t, ptr_t, i32_t, i32_t }, backend::TargetABI::Win64);
        register_extern("closesocket", "ws2_32.dll", i32_t, { i64_t }, backend::TargetABI::Win64);
        register_extern("bind", "ws2_32.dll", i32_t, { i64_t, ptr_t, i32_t }, backend::TargetABI::Win64);
        register_extern("listen", "ws2_32.dll", i32_t, { i64_t, i32_t }, backend::TargetABI::Win64);
        register_extern("accept", "ws2_32.dll", i64_t, { i64_t, ptr_t, ptr_t }, backend::TargetABI::Win64);
        register_extern("htons", "ws2_32.dll", i16_t, { i16_t }, backend::TargetABI::Win64);
        register_extern("ntohs", "ws2_32.dll", i16_t, { i16_t }, backend::TargetABI::Win64);
        register_extern("inet_addr", "ws2_32.dll", i32_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("gethostbyname", "ws2_32.dll", ptr_t, { ptr_t }, backend::TargetABI::Win64);
        register_extern("WSAGetLastError", "ws2_32.dll", i32_t, {}, backend::TargetABI::Win64);

        // 4. advapi32.dll (Win64)
        register_extern("GetUserNameA", "advapi32.dll", i32_t, { ptr_t, ptr_t }, backend::TargetABI::Win64);

        // 4. libc (SysV64)
        register_extern("printf", "libc.so.6", i32_t, { ptr_t }, backend::TargetABI::SysV64);
        register_extern("puts", "libc.so.6", i32_t, { ptr_t }, backend::TargetABI::SysV64);
        register_extern("malloc", "libc.so.6", ptr_t, { i64_t }, backend::TargetABI::SysV64);
        register_extern("free", "libc.so.6", v_t, { ptr_t }, backend::TargetABI::SysV64);
        register_extern("realloc", "libc.so.6", ptr_t, { ptr_t, i64_t }, backend::TargetABI::SysV64);
        register_extern("memcpy", "libc.so.6", ptr_t, { ptr_t, ptr_t, i64_t }, backend::TargetABI::SysV64);
        register_extern("memset", "libc.so.6", ptr_t, { ptr_t, i32_t, i64_t }, backend::TargetABI::SysV64);
        register_extern("exit", "libc.so.6", v_t, { i32_t }, backend::TargetABI::SysV64);
        register_extern("clock_gettime", "libc.so.6", i32_t, { i32_t, ptr_t }, backend::TargetABI::SysV64);
        register_extern("sleep", "libc.so.6", i32_t, { i32_t }, backend::TargetABI::SysV64);
        register_extern("usleep", "libc.so.6", i32_t, { i32_t }, backend::TargetABI::SysV64);
        register_extern("dlopen", "libc.so.6", ptr_t, { ptr_t, i32_t }, backend::TargetABI::SysV64);
        register_extern("dlsym", "libc.so.6", ptr_t, { ptr_t, ptr_t }, backend::TargetABI::SysV64);
        register_extern("dlclose", "libc.so.6", i32_t, { ptr_t }, backend::TargetABI::SysV64);
    }

} // namespace grass::native
