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
        populate_baremetal(ctx);
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
        register_extern("SetConsoleTextAttribute", "kernel32.dll", i32_t, { ptr_t, i16_t }, backend::TargetABI::Win64);

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
