#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

GrassSema::GrassSema() {
    registerBuiltins();
}

void GrassSema::registerBuiltins() {
    auto add_builtin = [this](const std::string& name, const SemaType& ret, const std::vector<SemaType>& params, bool is_variadic = false) {
        FunctionSymbol sym;
        sym.name = name;
        sym.return_type = ret;
        sym.loc = SourceLocation("<builtin>", 0, 0);
        sym.is_builtin = true;
        sym.has_body = true;
        sym.is_variadic = is_variadic;
        for (size_t i = 0; i < params.size(); ++i) {
            sym.params.emplace_back("arg" + std::to_string(i), params[i]);
        }
        functions_[name] = sym;
    };

    // Standard I/O and utility
    add_builtin("print", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("println", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("io_print", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_str", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_int", SemaType::Int(), {SemaType::Int()});
    add_builtin("io_print_bool", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_ptr", SemaType::Int(), {SemaType::Any()});
    add_builtin("io_print_double", SemaType::Int(), {SemaType::Float()});
    add_builtin("io_print_newline", SemaType::Int(), {});
    add_builtin("len", SemaType::Int(), {SemaType::Any()});
    add_builtin("discor", SemaType::Float(), {SemaType::Float()});

    // Math functions
    add_builtin("sqrt", SemaType::Float(), {SemaType::Float()});
    add_builtin("sin", SemaType::Float(), {SemaType::Float()});
    add_builtin("cos", SemaType::Float(), {SemaType::Float()});
    add_builtin("abs", SemaType::Float(), {SemaType::Float()});
    add_builtin("round", SemaType::Float(), {SemaType::Float()});
    add_builtin("floor", SemaType::Float(), {SemaType::Float()});
    add_builtin("ceil", SemaType::Float(), {SemaType::Float()});
    add_builtin("pow", SemaType::Float(), {SemaType::Float(), SemaType::Float()});
    add_builtin("min", SemaType::Any(), {SemaType::Any(), SemaType::Any()});
    add_builtin("max", SemaType::Any(), {SemaType::Any(), SemaType::Any()});
    add_builtin("rand", SemaType::Int(), {}, true);
    add_builtin("lerp", SemaType::Float(), {SemaType::Float(), SemaType::Float(), SemaType::Float()});

    // Time functions
    add_builtin("start_time", SemaType::Void(), {});
    add_builtin("stop_time", SemaType::Float(), {});
    add_builtin("conclus_time", SemaType::String(), {});
    add_builtin("clock_ms", SemaType::Int(), {});
    add_builtin("now_ms", SemaType::Int(), {});
    add_builtin("sleep_ms", SemaType::Void(), {SemaType::Int()});
    add_builtin("time_now_ms", SemaType::Int(), {});
    add_builtin("time_now_us", SemaType::Int(), {});
    add_builtin("time_frequency", SemaType::Int(), {});
    add_builtin("time_counter", SemaType::Int(), {});
    add_builtin("time_sleep", SemaType::Int(), {SemaType::Int()});
    add_builtin("time_delay_ms", SemaType::Int(), {SemaType::Int()});
    add_builtin("get_date", SemaType::String(), {});
    add_builtin("get_time_str", SemaType::String(), {});

    // Additional Math functions
    add_builtin("tan", SemaType::Float(), {SemaType::Float()});
    add_builtin("clamp", SemaType::Any(), {SemaType::Any(), SemaType::Any(), SemaType::Any()});

    // CoreMath and Crypto
    add_builtin("sha256", SemaType::String(), {SemaType::Any()});
    add_builtin("pow_mod", SemaType::Int(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("crypto_rand", SemaType::Int(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("pi", SemaType::String(), {});
    add_builtin("gcd", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("pow_e", SemaType::String(), {SemaType::Float(), SemaType::Float()});
    add_builtin("qb_init", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("h", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("x", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("z", SemaType::String(), {SemaType::Any()}, true);
    add_builtin("cnot", SemaType::String(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("q_measure", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("q_rand", SemaType::Int(), {}, true);

    // File system (fsys)
    add_builtin("fs_mkdir", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_is_dir", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_size", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_list", SemaType::String(), {SemaType::String()});
    add_builtin("fs_open_read", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_open_write", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_open_append", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_close", SemaType::Int(), {SemaType::Any()});
    add_builtin("fs_get_size", SemaType::Int(), {SemaType::Any()});
    add_builtin("fs_read", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Int()});
    add_builtin("fs_write", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Int()});
    add_builtin("fs_delete", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_remove", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_exists", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_create", SemaType::Int(), {SemaType::String()});
    add_builtin("fs_write_all", SemaType::Int(), {SemaType::String(), SemaType::Any(), SemaType::Int()});
    add_builtin("fs_read_all", SemaType::Int(), {SemaType::String(), SemaType::Any(), SemaType::Int()});
    add_builtin("fs_copy", SemaType::Int(), {SemaType::String(), SemaType::String()});

    // System memory
    add_builtin("memory_free", SemaType::String(), {});
    add_builtin("memory_usit", SemaType::Float(), {});
    add_builtin("memory_alloc", SemaType::Int(), {SemaType::Int()});
    add_builtin("memory_free_block", SemaType::Int(), {SemaType::Int()});

    // Memory management
    add_builtin("alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("free", SemaType::Void(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("realloc", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("memcpy", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("memset", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int(), SemaType::Int()});
    add_builtin("mem_alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("mem_free", SemaType::Void(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("heap_alloc", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("heap_free", SemaType::Int(), {SemaType::Ptr(SemaType::Void())});
    add_builtin("heap_realloc", SemaType::Ptr(SemaType::Void()), {SemaType::Ptr(SemaType::Void()), SemaType::Int()});
    add_builtin("heap_get_process_heap", SemaType::Ptr(SemaType::Void()), {});

    // String operations
    add_builtin("strlen", SemaType::Int(), {SemaType::Any()});
    add_builtin("str_len", SemaType::Int(), {SemaType::Any()});
    add_builtin("strcmp", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_cmp", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("streq", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_eq", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("strcpy", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_copy", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("strcat", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_cat", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("strdup", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_dup", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_find", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_contains", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("str_trim", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("str_to_int", SemaType::Int(), {SemaType::Any()});
    add_builtin("int_to_str", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("str_substr", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Int(), SemaType::Int()});

    // System and OS
    add_builtin("exit", SemaType::Void(), {SemaType::Int()});
    add_builtin("sys_exit", SemaType::Void(), {SemaType::Int()});
    add_builtin("exec", SemaType::Int(), {SemaType::Any()});
    add_builtin("run", SemaType::Int(), {SemaType::Any()});
    add_builtin("sys_exec", SemaType::Int(), {SemaType::Any()});
    add_builtin("get_env", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("sys_get_env", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("set_env", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("sys_set_env", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("cli_args", SemaType::String(), {});
    add_builtin("get_cmd_line", SemaType::String(), {});
    add_builtin("user_name", SemaType::String(), {});
    add_builtin("pc_name", SemaType::String(), {});
    add_builtin("screen_w", SemaType::Int(), {});
    add_builtin("screen_h", SemaType::Int(), {});
    add_builtin("msgbox", SemaType::Int(), {SemaType::Any(), SemaType::Any()});

    // Networking
    add_builtin("net_init", SemaType::Int(), {});
    add_builtin("net_cleanup", SemaType::Int(), {});
    add_builtin("net_create_tcp", SemaType::Int(), {});
    add_builtin("net_socket", SemaType::Int(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("net_connect", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int()});
    add_builtin("net_bind", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("net_listen", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("net_accept", SemaType::Int(), {SemaType::Int()});
    add_builtin("net_send", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int()});
    add_builtin("net_recv", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int()});
    add_builtin("net_close", SemaType::Int(), {SemaType::Int()});

    // Threading
    add_builtin("thread_create", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("thread_join", SemaType::Int(), {SemaType::Any()});
    add_builtin("thread_detach", SemaType::Int(), {SemaType::Any()});
    add_builtin("thread_id", SemaType::Int(), {});
    add_builtin("thread_sleep", SemaType::Void(), {SemaType::Int()});
    add_builtin("mutex_create", SemaType::Ptr(SemaType::Void()), {});
    add_builtin("mutex_lock", SemaType::Int(), {SemaType::Any()});
    add_builtin("mutex_unlock", SemaType::Int(), {SemaType::Any()});
    add_builtin("mutex_destroy", SemaType::Int(), {SemaType::Any()});

    // Bare-metal & CPU intrinsics
    add_builtin("port_in8", SemaType::Int(), {SemaType::Int()});
    add_builtin("io_in8", SemaType::Int(), {SemaType::Int()});
    add_builtin("inb", SemaType::Int(), {SemaType::Int()});
    add_builtin("port_out8", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("io_out8", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("outb", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("cpu_cli", SemaType::Void(), {});
    add_builtin("cli", SemaType::Void(), {});
    add_builtin("cpu_sti", SemaType::Void(), {});
    add_builtin("sti", SemaType::Void(), {});
    add_builtin("cpu_hlt", SemaType::Void(), {});
    add_builtin("hlt", SemaType::Void(), {});

    // MMIO intrinsics
    add_builtin("mmio_read8", SemaType::Int(), {SemaType::Int()});
    add_builtin("mmio_read16", SemaType::Int(), {SemaType::Int()});
    add_builtin("mmio_read32", SemaType::Int(), {SemaType::Int()});
    add_builtin("mmio_read64", SemaType::Int(), {SemaType::Int()});
    add_builtin("mmio_write8", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("mmio_write16", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("mmio_write32", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("mmio_write64", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("memory_barrier", SemaType::Void(), {});
    add_builtin("read_barrier", SemaType::Void(), {});
    add_builtin("write_barrier", SemaType::Void(), {});

    // Win32 API and runtime intrinsics used by stdlib (kernel32.dll, user32.dll, ws2_32.dll, advapi32.dll)
    add_builtin("GetStdHandle", SemaType::Ptr(SemaType::Void()), {SemaType::Int()});
    add_builtin("lstrlenA", SemaType::Int(), {SemaType::Any()});
    add_builtin("lstrlenW", SemaType::Int(), {SemaType::Any()});
    add_builtin("lstrcpyA", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("lstrcatA", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("lstrcmpA", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("lstrcmpiA", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("WriteFile", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("ReadFile", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("CreateFileA", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("CloseHandle", SemaType::Int(), {SemaType::Any()});
    add_builtin("GetFileSizeEx", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("DeleteFileA", SemaType::Int(), {SemaType::Any()});
    add_builtin("CreateDirectoryA", SemaType::Int(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("GetFileAttributesA", SemaType::Int(), {SemaType::Any()});
    add_builtin("CopyFileA", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Int()});
    add_builtin("QueryPerformanceFrequency", SemaType::Int(), {SemaType::Any()});
    add_builtin("QueryPerformanceCounter", SemaType::Int(), {SemaType::Any()});
    add_builtin("GetTickCount64", SemaType::Int(), {});
    add_builtin("Sleep", SemaType::Void(), {SemaType::Int()});
    add_builtin("GetProcessHeap", SemaType::Ptr(SemaType::Void()), {});
    add_builtin("HeapAlloc", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any(), SemaType::Any()});
    add_builtin("HeapFree", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any()});
    add_builtin("HeapReAlloc", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any()});
    add_builtin("VirtualAlloc", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any(), SemaType::Int(), SemaType::Int()});
    add_builtin("VirtualFree", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Int()});
    add_builtin("LoadLibraryA", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("GetProcAddress", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any()});
    add_builtin("FreeLibrary", SemaType::Int(), {SemaType::Any()});
    add_builtin("GetLastError", SemaType::Int(), {});
    add_builtin("SetLastError", SemaType::Void(), {SemaType::Int()});
    add_builtin("GetCommandLineA", SemaType::String(), {});
    add_builtin("GetEnvironmentVariableA", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Int()});
    add_builtin("SetEnvironmentVariableA", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("GetComputerNameA", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("GetUserNameA", SemaType::Int(), {SemaType::Any(), SemaType::Any()});
    add_builtin("GetCurrentProcessId", SemaType::Int(), {});
    add_builtin("CreateProcessA", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Int(), SemaType::Int(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("WinExec", SemaType::Int(), {SemaType::Any(), SemaType::Int()});
    add_builtin("CreateThread", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Int(), SemaType::Any()}, true);
    add_builtin("WaitForSingleObject", SemaType::Int(), {SemaType::Any(), SemaType::Int()});
    add_builtin("CreateMutexA", SemaType::Ptr(SemaType::Void()), {SemaType::Any(), SemaType::Int(), SemaType::Any()}, true);
    add_builtin("ReleaseMutex", SemaType::Int(), {SemaType::Any()});
    add_builtin("MessageBoxA", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Int()}, true);
    add_builtin("MessageBoxW", SemaType::Int(), {SemaType::Any(), SemaType::Any(), SemaType::Any(), SemaType::Int()}, true);
    add_builtin("GetSystemMetrics", SemaType::Int(), {SemaType::Int()});
    add_builtin("WSAStartup", SemaType::Int(), {SemaType::Int(), SemaType::Any()});
    add_builtin("WSACleanup", SemaType::Int(), {});
    add_builtin("socket", SemaType::Int(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("connect", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int()});
    add_builtin("bind", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int()});
    add_builtin("listen", SemaType::Int(), {SemaType::Int(), SemaType::Int()});
    add_builtin("accept", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Any()}, true);
    add_builtin("send", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int(), SemaType::Int()});
    add_builtin("recv", SemaType::Int(), {SemaType::Int(), SemaType::Any(), SemaType::Int(), SemaType::Int()});
    add_builtin("closesocket", SemaType::Int(), {SemaType::Int()});
    add_builtin("htons", SemaType::Int(), {SemaType::Int()});
    add_builtin("ntohs", SemaType::Int(), {SemaType::Int()});
    add_builtin("inet_addr", SemaType::Int(), {SemaType::Any()});
    add_builtin("gethostbyname", SemaType::Ptr(SemaType::Void()), {SemaType::Any()});
    add_builtin("WSAGetLastError", SemaType::Int(), {});
    add_builtin("ExitProcess", SemaType::Void(), {SemaType::Int()});
}

void GrassSema::enterScope(Scope::Kind kind) {
    scope_pool_.push_back(std::make_unique<Scope>(kind, current_scope_));
    current_scope_ = scope_pool_.back().get();
}

void GrassSema::exitScope() {
    if (current_scope_) {
        current_scope_ = current_scope_->parent;
    }
}

void GrassSema::reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Error, loc, msg, hint);
}

void GrassSema::reportWarning(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Warning, loc, msg, hint);
}

bool GrassSema::hasErrors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

void GrassSema::printDiagnostics() const {
    for (const auto& d : diagnostics_) {
        std::cerr << d.format() << std::endl;
    }
}

std::string GrassSema::mangleName(const std::string& name) {
    std::string s = name;
    size_t p = 0;
    while ((p = s.find("::", p)) != std::string::npos) {
        s.replace(p, 2, "__");
        p += 2;
    }
    return s;
}

std::string GrassSema::currentNamespace() const {
    std::string ns;
    for (size_t i = 0; i < namespace_stack_.size(); ++i) {
        if (i > 0) ns += "::";
        ns += namespace_stack_[i];
    }
    return ns;
}

std::string GrassSema::qualifyName(const std::string& name) const {
    if (name.find("::") != std::string::npos) return name;
    std::string ns = currentNamespace();
    if (ns.empty()) return name;
    return ns + "::" + name;
}

const FunctionSymbol* GrassSema::findFunction(const std::string& name) const {
    // 1. Direct match
    auto it = functions_.find(name);
    if (it != functions_.end()) return &it->second;

    // 2. Mangled match
    std::string mangled = mangleName(name);
    it = functions_.find(mangled);
    if (it != functions_.end()) return &it->second;

    // 3. Namespace lookups
    if (name.find("::") == std::string::npos) {
        std::string cur_ns = currentNamespace();
        if (!cur_ns.empty()) {
            std::string q = cur_ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }

        auto us_it = using_symbols_.find(name);
        if (us_it != using_symbols_.end()) {
            it = functions_.find(us_it->second);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(us_it->second));
            if (it != functions_.end()) return &it->second;
        }

        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }
    } else {
        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }
    }
    return nullptr;
}

const StructSymbol* GrassSema::findStruct(const std::string& name) const {
    // 1. Direct match
    auto it = structs_.find(name);
    if (it != structs_.end()) return &it->second;

    // 2. Mangled match
    std::string mangled = mangleName(name);
    it = structs_.find(mangled);
    if (it != structs_.end()) return &it->second;

    // 3. Namespace lookups
    if (name.find("::") == std::string::npos) {
        std::string cur_ns = currentNamespace();
        if (!cur_ns.empty()) {
            std::string q = cur_ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }

        auto us_it = using_symbols_.find(name);
        if (us_it != using_symbols_.end()) {
            it = structs_.find(us_it->second);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(us_it->second));
            if (it != structs_.end()) return &it->second;
        }

        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }
    } else {
        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }
    }
    return nullptr;
}

SemaType GrassSema::resolveType(const std::string& type_name, int ptr_depth, const SourceLocation& loc) {
    SemaType base;
    if (type_name == "int" || type_name == "int32" || type_name == "int64" ||
        type_name == "i32" || type_name == "i64" ||
        type_name == "int32_t" || type_name == "int64_t" ||
        type_name == "uint32_t" || type_name == "uint64_t" ||
        type_name == "u32" || type_name == "u64" ||
        type_name == "u8" || type_name == "u16" || type_name == "i8" || type_name == "i16" ||
        type_name == "uint8_t" || type_name == "uint16_t" ||
        type_name == "int8_t" || type_name == "int16_t" ||
        type_name == "uint8" || type_name == "uint16" || type_name == "uint32" || type_name == "uint64" ||
        type_name == "int8" || type_name == "int16" ||
        type_name == "char" || type_name == "byte" || type_name == "size_t") {
        base = SemaType::Int();
    } else if (type_name == "float" || type_name == "double" || type_name == "f32" || type_name == "f64") {
        base = SemaType::Float();
    } else if (type_name == "bool") {
        base = SemaType::Bool();
    } else if (type_name == "string") {
        base = SemaType::String();
    } else if (type_name == "void") {
        base = SemaType::Void();
    } else if (type_name == "nullptr" || type_name == "null") {
        return SemaType::Nullptr();
    } else if (type_name == "var" || type_name == "auto" || type_name == "take" || type_name.empty()) {
        base = SemaType::Any();
    } else {
        const StructSymbol* st = findStruct(type_name);
        if (st) {
            base = SemaType::CustomStruct(st->name);
        } else {
            base = SemaType::CustomStruct(type_name);
        }
    }

    if (ptr_depth > 0) {
        SemaType ptr = base;
        for (int i = 0; i < ptr_depth; ++i) {
            ptr = SemaType::Ptr(ptr);
        }
        return ptr;
    }
    return base;
}

bool GrassSema::analyze(const ProgramAST& program) {
    diagnostics_.clear();
    scope_pool_.clear();
    scope_pool_.reserve(program.declarations.size() + 16);
    functions_.reserve(program.declarations.size() + 16);
    namespace_stack_.clear();
    using_namespaces_.clear();
    using_symbols_.clear();
    current_scope_ = nullptr;
    current_function_ = nullptr;
    current_struct_ = nullptr;

    if (program.is_strict) {
        strict_mode_ = true;
    }

    pass1_registerDeclarations(program);
    pass2_checkProgram(program);

    return !hasErrors();
}

void GrassSema::pass1_registerDeclarations(const ProgramAST& program) {
    auto register_decl = [&](auto& self, const DeclAST* decl) -> void {
        if (!decl) return;
        if (decl->node_kind == NodeKind::StructDecl) {
            auto st = static_cast<const StructDeclAST*>(decl);
            std::string qname = qualifyName(st->name);
            std::string mname = mangleName(qname);
            StructSymbol sym;
            sym.name = qname;
            sym.loc = st->loc;
            for (const auto& f : st->fields) {
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : st->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = m->is_destructor ? SemaType::Void() : resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.is_destructor = m->is_destructor;
                msym.struct_name = qname;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                if (m->is_destructor) {
                    sym.has_explicit_destructor = true;
                    sym.has_destructor = true;
                }
                sym.methods[m->name] = msym;
            }
            structs_[qname] = sym;
            if (qname != mname) structs_[mname] = sym;
            if (st->name != qname && !structs_.count(st->name)) structs_[st->name] = sym;
        } else if (decl->node_kind == NodeKind::ClassDecl) {
            auto cls = static_cast<const ClassDeclAST*>(decl);
            std::string qname = qualifyName(cls->name);
            std::string mname = mangleName(qname);
            StructSymbol sym;
            sym.name = qname;
            sym.loc = cls->loc;
            sym.base_class = cls->base_class;
            for (const auto& f : cls->fields) {
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : cls->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = m->is_destructor ? SemaType::Void() : resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.is_destructor = m->is_destructor;
                msym.struct_name = qname;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                if (m->is_destructor) {
                    sym.has_explicit_destructor = true;
                    sym.has_destructor = true;
                }
                sym.methods[m->name] = msym;
            }
            structs_[qname] = sym;
            if (qname != mname) structs_[mname] = sym;
            if (cls->name != qname && !structs_.count(cls->name)) structs_[cls->name] = sym;
        } else if (decl->node_kind == NodeKind::FunctionDecl) {
            auto fn = static_cast<const FunctionDeclAST*>(decl);
            std::string qname = qualifyName(fn->name);
            std::string mname = mangleName(qname);
            FunctionSymbol sym;
            sym.name = qname;
            sym.return_type = resolveType(fn->return_type, fn->return_pointer_depth, fn->loc);
            sym.loc = fn->loc;
            sym.has_body = (fn->body != nullptr);
            for (const auto& p : fn->params) {
                sym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
            }
            functions_[qname] = sym;
            if (qname != mname) functions_[mname] = sym;
            if (fn->name != qname && !functions_.count(fn->name)) functions_[fn->name] = sym;
        } else if (decl->node_kind == NodeKind::NamespaceDecl) {
            auto ns = static_cast<const NamespaceDeclAST*>(decl);
            namespace_stack_.push_back(ns->name);
            for (const auto& d : ns->declarations) {
                self(self, d.get());
            }
            namespace_stack_.pop_back();
        } else if (decl->node_kind == NodeKind::UsingDecl) {
            auto u = static_cast<const UsingDeclAST*>(decl);
            if (u->is_namespace) {
                using_namespaces_.push_back(u->path);
            } else {
                size_t last_col = u->path.rfind("::");
                std::string sname = (last_col != std::string::npos) ? u->path.substr(last_col + 2) : u->path;
                using_symbols_[sname] = u->path;
            }
        } else if (decl->node_kind == NodeKind::ModuleDecl) {
            auto mod = static_cast<const ModuleDeclAST*>(decl);
            for (const auto& st : mod->structs) self(self, st.get());
            for (const auto& cls : mod->classes) self(self, cls.get());
            for (const auto& fn : mod->functions) self(self, fn.get());
        }
    };

    for (const auto& decl : program.declarations) {
        register_decl(register_decl, decl.get());
    }

    // Transitive has_destructor propagation across struct fields and base classes
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& kv : structs_) {
            if (kv.second.has_destructor) continue;
            bool need_dtor = false;
            if (!kv.second.base_class.empty()) {
                const StructSymbol* base = findStruct(kv.second.base_class);
                if (base && base->has_destructor) need_dtor = true;
            }
            if (!need_dtor) {
                for (const auto& fname : kv.second.field_order) {
                    auto fit = kv.second.fields.find(fname);
                    if (fit != kv.second.fields.end() && fit->second.is_struct()) {
                        const StructSymbol* fstruct = findStruct(fit->second.name);
                        if (fstruct && fstruct->has_destructor) {
                            need_dtor = true;
                            break;
                        }
                    }
                }
            }
            if (need_dtor) {
                kv.second.has_destructor = true;
                changed = true;
            }
        }
    }
}

void GrassSema::pass2_checkProgram(const ProgramAST& program) {
    enterScope(Scope::Kind::Global);

    // 1. Check top-level statements
    for (const auto& stmt : program.statements) {
        checkStmt(stmt.get());
    }

    // 2. Check function bodies
    for (const auto& decl : program.declarations) {
        checkDecl(decl.get());
    }

    exitScope();
}

void GrassSema::checkDecl(const DeclAST* decl) {
    if (!decl) return;
    switch (decl->node_kind) {
        case NodeKind::FunctionDecl:
            checkFunctionDecl(static_cast<const FunctionDeclAST*>(decl));
            break;
        case NodeKind::StructDecl:
            checkStructDecl(static_cast<const StructDeclAST*>(decl));
            break;
        case NodeKind::ClassDecl:
            checkClassDecl(static_cast<const ClassDeclAST*>(decl));
            break;
        case NodeKind::ModuleDecl:
            checkModuleDecl(static_cast<const ModuleDeclAST*>(decl));
            break;
        case NodeKind::NamespaceDecl:
            checkNamespaceDecl(static_cast<const NamespaceDeclAST*>(decl));
            break;
        case NodeKind::UsingDecl:
            checkUsingDecl(static_cast<const UsingDeclAST*>(decl));
            break;
        default:
            break;
    }
}

void GrassSema::checkNamespaceDecl(const NamespaceDeclAST* ns) {
    namespace_stack_.push_back(ns->name);
    for (const auto& stmt : ns->statements) {
        checkStmt(stmt.get());
    }
    for (const auto& decl : ns->declarations) {
        checkDecl(decl.get());
    }
    namespace_stack_.pop_back();
}

void GrassSema::checkUsingDecl(const UsingDeclAST* u) {
    if (u->is_namespace) {
        using_namespaces_.push_back(u->path);
    } else {
        size_t last_col = u->path.rfind("::");
        std::string sname = (last_col != std::string::npos) ? u->path.substr(last_col + 2) : u->path;
        using_symbols_[sname] = u->path;
    }
}

void GrassSema::checkModuleDecl(const ModuleDeclAST* mod) {
    for (const auto& st : mod->structs) checkStructDecl(st.get());
    for (const auto& cls : mod->classes) checkClassDecl(cls.get());
    for (const auto& fn : mod->functions) checkFunctionDecl(fn.get());
}

void GrassSema::checkFunctionDecl(const FunctionDeclAST* func) {
    std::string qname = qualifyName(func->name);
    auto it = functions_.find(qname);
    if (it == functions_.end()) it = functions_.find(mangleName(qname));
    if (it == functions_.end()) it = functions_.find(func->name);
    if (it == functions_.end()) return;
    const FunctionSymbol& sym = it->second;

    current_function_ = &sym;
    enterScope(Scope::Kind::Function);

    // Declare parameters in function scope
    for (const auto& param : sym.params) {
        VariableSymbol vsym;
        vsym.name = param.first;
        vsym.type = param.second;
        vsym.loc = func->loc;
        vsym.is_initialized = true;
        if (!current_scope_->declareVariable(vsym)) {
            reportError(func->loc, "Duplicate parameter name '" + param.first + "' in function '" + func->name + "'.", "Parameter names must be unique");
        }
    }

    if (func->body) {
        checkBlockStmt(func->body.get());

        // Check return on all control paths for non-void functions
        if (!sym.return_type.is_void() && !sym.return_type.is_any()) {
            if (!returnsOnAllPaths(func->body.get())) {
                reportError(func->loc,
                    "Not all control paths return a value in function '" + func->name + "'.",
                    "Ensure every branch returns a value of type '" + sym.return_type.toString() + "'");
            }
        }
    }

    exitScope();
    current_function_ = nullptr;
}

void GrassSema::checkStructDecl(const StructDeclAST* st) {
    auto it = structs_.find(qualifyName(st->name));
    if (it == structs_.end()) it = structs_.find(st->name);
    if (it == structs_.end()) return;
    current_struct_ = &it->second;

    int dtor_count = 0;
    for (const auto& m : st->methods) {
        if (m->is_destructor) {
            dtor_count++;
            if (dtor_count > 1) {
                reportError(m->loc,
                    "Struct '" + st->name + "' cannot declare more than one destructor.",
                    "Keep at most one destructor declaration per struct");
            }
            std::string dname = (!m->name.empty() && m->name[0] == '~') ? m->name.substr(1) : m->name;
            if (dname != st->name) {
                reportError(m->loc,
                    "Destructor name '~" + dname + "' does not match struct name '" + st->name + "'.",
                    "Rename destructor to '~" + st->name + "'");
            }
            if (!m->params.empty()) {
                reportError(m->loc,
                    "Destructor '~" + st->name + "()' cannot take any parameters.",
                    "Remove parameters from '~" + st->name + "()'");
            }
        }
        auto mit = current_struct_->methods.find(m->name);
        if (mit == current_struct_->methods.end()) continue;
        const FunctionSymbol& msym = mit->second;

        current_function_ = &msym;
        enterScope(Scope::Kind::Function);

        // Add 'this' pointer in struct method scope
        VariableSymbol this_sym;
        this_sym.name = "this";
        this_sym.type = SemaType::CustomStruct(st->name);
        this_sym.loc = m->loc;
        this_sym.is_initialized = true;
        current_scope_->declareVariable(this_sym);

        for (const auto& p : msym.params) {
            VariableSymbol vsym;
            vsym.name = p.first;
            vsym.type = p.second;
            vsym.loc = m->loc;
            vsym.is_initialized = true;
            current_scope_->declareVariable(vsym);
        }

        if (m->body) {
            checkBlockStmt(m->body.get());
            if (!msym.return_type.is_void() && !msym.return_type.is_any()) {
                if (!returnsOnAllPaths(m->body.get())) {
                    reportError(m->loc,
                        "Not all control paths return a value in method '" + m->name + "'.",
                        "Ensure every branch returns a value of type '" + msym.return_type.toString() + "'");
                }
            }
        }

        exitScope();
        current_function_ = nullptr;
    }

    current_struct_ = nullptr;
}

void GrassSema::checkClassDecl(const ClassDeclAST* cls) {
    auto it = structs_.find(cls->name);
    if (it == structs_.end()) return;
    current_struct_ = &it->second;

    // Check base class existence and cycle detection
    if (!cls->base_class.empty()) {
        std::unordered_set<std::string> chain;
        chain.insert(cls->name);
        std::string cur = cls->base_class;
        while (!cur.empty()) {
            if (chain.count(cur)) {
                reportError(cls->loc, "Cyclic inheritance detected involving class '" + cur + "' and '" + cls->name + "'.", "Remove cyclic dependency in class hierarchy");
                break;
            }
            chain.insert(cur);
            const StructSymbol* base_sym = findStruct(cur);
            if (!base_sym) {
                reportError(cls->loc, "Base class '" + cur + "' not found for class '" + cls->name + "'.", "Declare base class before inheriting");
                break;
            }
            cur = base_sym->base_class;
        }
    }

    int dtor_count = 0;
    for (const auto& m : cls->methods) {
        if (m->is_destructor) {
            dtor_count++;
            if (dtor_count > 1) {
                reportError(m->loc,
                    "Class '" + cls->name + "' cannot declare more than one destructor.",
                    "Keep at most one destructor declaration per class");
            }
            std::string dname = (!m->name.empty() && m->name[0] == '~') ? m->name.substr(1) : m->name;
            if (dname != cls->name) {
                reportError(m->loc,
                    "Destructor name '~" + dname + "' does not match class name '" + cls->name + "'.",
                    "Rename destructor to '~" + cls->name + "'");
            }
            if (!m->params.empty()) {
                reportError(m->loc,
                    "Destructor '~" + cls->name + "()' cannot take any parameters.",
                    "Remove parameters from '~" + cls->name + "()'");
            }
        }
        auto mit = current_struct_->methods.find(m->name);
        if (mit == current_struct_->methods.end()) continue;
        const FunctionSymbol& msym = mit->second;

        current_function_ = &msym;
        enterScope(Scope::Kind::Function);

        VariableSymbol this_sym;
        this_sym.name = "this";
        this_sym.type = SemaType::CustomStruct(cls->name);
        this_sym.loc = m->loc;
        this_sym.is_initialized = true;
        current_scope_->declareVariable(this_sym);

        for (const auto& p : msym.params) {
            VariableSymbol vsym;
            vsym.name = p.first;
            vsym.type = p.second;
            vsym.loc = m->loc;
            vsym.is_initialized = true;
            current_scope_->declareVariable(vsym);
        }

        if (m->body) {
            checkBlockStmt(m->body.get());
            if (!msym.return_type.is_void() && !msym.return_type.is_any()) {
                if (!returnsOnAllPaths(m->body.get())) {
                    reportError(m->loc,
                        "Not all control paths return a value in method '" + m->name + "'.",
                        "Ensure every branch returns a value of type '" + msym.return_type.toString() + "'");
                }
            }
        }

        exitScope();
        current_function_ = nullptr;
    }

    current_struct_ = nullptr;
}

bool GrassSema::returnsOnAllPaths(const StmtAST* stmt) {
    if (!stmt) return false;
    switch (stmt->node_kind) {
        case NodeKind::ReturnStmt:
            return true;
        case NodeKind::BlockStmt: {
            auto block = static_cast<const BlockStmtAST*>(stmt);
            for (const auto& s : block->statements) {
                if (s->node_kind == NodeKind::ReturnStmt) return true;
                if (s->node_kind == NodeKind::BlockStmt || s->node_kind == NodeKind::IfStmt) {
                    if (returnsOnAllPaths(s.get())) return true;
                }
            }
            return false;
        }
        case NodeKind::IfStmt: {
            auto if_stmt = static_cast<const IfStmtAST*>(stmt);
            if (!if_stmt->else_branch) return false;
            return returnsOnAllPaths(if_stmt->then_branch.get()) &&
                   returnsOnAllPaths(if_stmt->else_branch.get());
        }
        default:
            return false;
    }
}

void GrassSema::checkStmt(const StmtAST* stmt) {
    if (!stmt) return;
    switch (stmt->node_kind) {
        case NodeKind::AssignStmt:
            checkAssignStmt(static_cast<const AssignStmtAST*>(stmt));
            break;
        case NodeKind::VarDeclStmt:
            checkVarDeclStmt(static_cast<const VarDeclStmtAST*>(stmt));
            break;
        case NodeKind::ExprStmt: {
            auto e = static_cast<const ExprStmtAST*>(stmt);
            checkExpr(e->expr.get());
            break;
        }
        case NodeKind::BlockStmt: {
            auto b = static_cast<const BlockStmtAST*>(stmt);
            enterScope(Scope::Kind::Block);
            checkBlockStmt(b);
            exitScope();
            break;
        }
        case NodeKind::IfStmt:
            checkIfStmt(static_cast<const IfStmtAST*>(stmt));
            break;
        case NodeKind::WhileStmt:
            checkWhileStmt(static_cast<const WhileStmtAST*>(stmt));
            break;
        case NodeKind::ForStmt:
            checkForStmt(static_cast<const ForStmtAST*>(stmt));
            break;
        case NodeKind::ReturnStmt:
            checkReturnStmt(static_cast<const ReturnStmtAST*>(stmt));
            break;
        case NodeKind::StreamIOStmt:
            checkStreamIOStmt(static_cast<const StreamIOStmtAST*>(stmt));
            break;
        default:
            break;
    }
}

void GrassSema::checkBlockStmt(const BlockStmtAST* block) {
    if (!block) return;
    for (const auto& s : block->statements) {
        checkStmt(s.get());
    }
}

void GrassSema::checkVarDeclStmt(const VarDeclStmtAST* vdecl) {
    if (current_scope_->isDeclaredInCurrentScope(vdecl->var_name)) {
        reportError(vdecl->loc,
            "Redeclaration of variable '" + vdecl->var_name + "' in the same scope.",
            "Variables cannot be redeclared in the same block");
        return;
    }

    SemaType decl_type = resolveType(vdecl->type_name, vdecl->pointer_depth, vdecl->loc);

    if (vdecl->type_name == "take" || (strict_mode_ && (vdecl->type_name == "auto" || vdecl->type_name == "var"))) {
        if (!vdecl->init_expr) {
            reportError(vdecl->loc,
                "Variable '" + vdecl->var_name + "' declared with 'take' must have an explicit initializer for type deduction.",
                "Provide initializer expression");
            return;
        }
        SemaType init_type = checkExpr(vdecl->init_expr.get());
        if (init_type.is_void()) {
            reportError(vdecl->loc,
                "Cannot deduce type for 'take' variable '" + vdecl->var_name + "' from 'void' expression.",
                "Initializer expression must return a non-void value");
            return;
        }
        decl_type = init_type;
    } else if (vdecl->init_expr) {
        SemaType init_type = checkExpr(vdecl->init_expr.get());
        if (decl_type.is_any()) {
            decl_type = init_type;
        } else if (!init_type.is_assignable_to(decl_type, strict_mode_)) {
            reportError(vdecl->loc,
                "Type mismatch in variable initialization: cannot initialize '" + decl_type.toString() +
                "' with value of type '" + init_type.toString() + "'.",
                "Ensure initializer expression matches variable type");
        }
    }

    VariableSymbol sym;
    sym.name = vdecl->var_name;
    sym.type = decl_type;
    sym.loc = vdecl->loc;
    sym.is_const = vdecl->is_const;
    sym.is_initialized = (vdecl->init_expr != nullptr);
    current_scope_->declareVariable(sym);

    if (decl_type.is_struct()) {
        const StructSymbol* st_sym = findStruct(decl_type.name);
        if (st_sym && st_sym->has_destructor) {
            current_scope_->registerRAIIVariable(sym);
        }
    }
}

void GrassSema::checkAssignStmt(const AssignStmtAST* assign) {
    if (!assign || !assign->target) return;

    if (assign->target->node_kind == NodeKind::LiteralExpr) {
        reportError(assign->loc, "Assignment target must be an lvalue.", "Cannot assign to literal value");
        return;
    }

    SemaType tgt_type;
    if (assign->target->node_kind == NodeKind::VariableExpr) {
        auto var = static_cast<const VariableExprAST*>(assign->target.get());
        VariableSymbol* sym = current_scope_->lookupVariable(var->name);
        if (!sym) {
            if (current_struct_) {
                auto fit = current_struct_->fields.find(var->name);
                if (fit != current_struct_->fields.end()) {
                    tgt_type = fit->second;
                } else {
                    reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
                    tgt_type = SemaType::Unknown();
                }
            } else {
                reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
                tgt_type = SemaType::Unknown();
            }
        } else {
            if (sym->is_const) {
                reportError(assign->loc, "Cannot assign to const variable '" + var->name + "'.", "Const variables cannot be reassigned");
            }
            tgt_type = sym->type;
        }
    } else {
        tgt_type = checkExpr(assign->target.get());
    }

    SemaType val_type = checkExpr(assign->value.get());
    if (!val_type.is_assignable_to(tgt_type)) {
        reportError(assign->loc,
            "Cannot assign value of type '" + val_type.toString() + "' to variable of type '" + tgt_type.toString() + "'.",
            "Ensure types match in assignment");
    }
}

void GrassSema::checkIfStmt(const IfStmtAST* if_stmt) {
    if (!if_stmt) return;
    SemaType cond_type = checkExpr(if_stmt->condition.get());
    if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
        !cond_type.is_any() && !cond_type.is_unknown()) {
        reportError(if_stmt->loc,
            "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
            "Condition must evaluate to bool or integer");
    }
    checkStmt(if_stmt->then_branch.get());
    if (if_stmt->else_branch) {
        checkStmt(if_stmt->else_branch.get());
    }
}

void GrassSema::checkWhileStmt(const WhileStmtAST* while_stmt) {
    if (!while_stmt) return;
    SemaType cond_type = checkExpr(while_stmt->condition.get());
    if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
        !cond_type.is_any() && !cond_type.is_unknown()) {
        reportError(while_stmt->loc,
            "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
            "Condition must evaluate to bool or integer");
    }
    checkStmt(while_stmt->body.get());
}

void GrassSema::checkForStmt(const ForStmtAST* for_stmt) {
    if (!for_stmt) return;
    enterScope(Scope::Kind::Block);
    if (for_stmt->init) checkStmt(for_stmt->init.get());
    if (for_stmt->condition) {
        SemaType cond_type = checkExpr(for_stmt->condition.get());
        if (!cond_type.is_bool() && !cond_type.is_numeric() && !cond_type.is_pointer() &&
            !cond_type.is_any() && !cond_type.is_unknown()) {
            reportError(for_stmt->loc,
                "Condition must be boolean or numeric expression, got '" + cond_type.toString() + "'.",
                "Condition must evaluate to bool or integer");
        }
    }
    if (for_stmt->step) checkStmt(for_stmt->step.get());
    checkStmt(for_stmt->body.get());
    exitScope();
}

void GrassSema::checkReturnStmt(const ReturnStmtAST* ret_stmt) {
    if (!current_function_) {
        reportError(ret_stmt->loc, "Return statement outside of function.", "Return statements can only be used inside functions");
        return;
    }

    if (current_function_->return_type.is_void()) {
        if (ret_stmt->value) {
            reportError(ret_stmt->loc,
                "Void function '" + current_function_->name + "' cannot return a value.",
                "Remove return value or change function return type");
        }
    } else {
        if (!ret_stmt->value) {
            reportError(ret_stmt->loc,
                "Non-void function '" + current_function_->name + "' must return a value of type '" + current_function_->return_type.toString() + "'.",
                "Provide return value");
        } else {
            SemaType val_type = checkExpr(ret_stmt->value.get());
            if (!val_type.is_assignable_to(current_function_->return_type)) {
                reportError(ret_stmt->loc,
                    "Function '" + current_function_->name + "' expected return type '" + current_function_->return_type.toString() + "', but got '" + val_type.toString() + "'.",
                    "Return expression must match declared function return type");
            }
        }
    }
}

void GrassSema::checkStreamIOStmt(const StreamIOStmtAST* io_stmt) {
    for (const auto& op : io_stmt->operands) {
        checkExpr(op.get());
    }
}

SemaType GrassSema::checkExpr(const ExprAST* expr) {
    if (!expr) return SemaType::Unknown();

    switch (expr->node_kind) {
        case NodeKind::LiteralExpr:
            return checkLiteralExpr(static_cast<const LiteralExprAST*>(expr));
        case NodeKind::VariableExpr:
            return checkVariableExpr(static_cast<const VariableExprAST*>(expr));
        case NodeKind::BinaryExpr:
            return checkBinaryExpr(static_cast<const BinaryExprAST*>(expr));
        case NodeKind::UnaryExpr:
            return checkUnaryExpr(static_cast<const UnaryExprAST*>(expr));
        case NodeKind::PostfixExpr:
            return checkPostfixExpr(static_cast<const PostfixExprAST*>(expr));
        case NodeKind::CallExpr:
            return checkCallExpr(static_cast<const CallExprAST*>(expr));
        case NodeKind::MemberAccessExpr:
            return checkMemberAccessExpr(static_cast<const MemberAccessExprAST*>(expr));
        case NodeKind::IndexExpr:
            return checkIndexExpr(static_cast<const IndexExprAST*>(expr));
        case NodeKind::ArrayLiteralExpr:
            return checkArrayLiteralExpr(static_cast<const ArrayLiteralExprAST*>(expr));
        case NodeKind::AllocExpr:
            return checkAllocExpr(static_cast<const AllocExprAST*>(expr));
        case NodeKind::FreeExpr:
            return checkFreeExpr(static_cast<const FreeExprAST*>(expr));
        case NodeKind::CastExpr:
            return checkCastExpr(static_cast<const CastExprAST*>(expr));
        case NodeKind::SizeofExpr:
            return checkSizeofExpr(static_cast<const SizeofExprAST*>(expr));
        default:
            return SemaType::Unknown();
    }
}

SemaType GrassSema::checkLiteralExpr(const LiteralExprAST* lit) {
    switch (lit->lit_kind) {
        case LiteralKind::Int: return SemaType::Int();
        case LiteralKind::Float: return SemaType::Float();
        case LiteralKind::String: return SemaType::String();
        case LiteralKind::Bool: return SemaType::Bool();
        case LiteralKind::Nil: return SemaType(SemaTypeKind::Pointer, "void", 1);
        case LiteralKind::Nullptr: return SemaType::Nullptr();
    }
    return SemaType::Unknown();
}

SemaType GrassSema::checkVariableExpr(const VariableExprAST* var) {
    if (var->name == "endl" || var->name == "std::endl") return SemaType::String();
    VariableSymbol* sym = current_scope_->lookupVariable(var->name);
    if (!sym) {
        // Check if it's a field of 'this' when inside a method
        if (current_struct_) {
            auto fit = current_struct_->fields.find(var->name);
            if (fit != current_struct_->fields.end()) return fit->second;
        }
        reportError(var->loc, "Undeclared variable '" + var->name + "'.", "Declare variable with type before using it");
        return SemaType::Unknown();
    }
    return sym->type;
}

SemaType GrassSema::checkBinaryExpr(const BinaryExprAST* bin) {
    if (!bin) return SemaType::Unknown();
    SemaType lhs = checkExpr(bin->lhs.get());
    SemaType rhs = checkExpr(bin->rhs.get());

    switch (bin->op) {
        case TOK_PLUS:
            if (lhs.is_pointer() && rhs.is_integral()) return lhs;
            if (rhs.is_pointer() && lhs.is_integral()) return rhs;
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_string() || rhs.is_string()) return SemaType::String();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '+': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands must be numeric or strings for concatenation");
            return SemaType::Unknown();

        case TOK_MINUS:
            if (lhs.is_pointer() && rhs.is_integral()) return lhs;
            if (lhs.is_pointer() && rhs.is_pointer()) {
                if (strict_mode_ && !lhs.equals(rhs) && lhs.name != "void" && rhs.name != "void") {
                    reportError(bin->loc, "Cannot subtract pointers of different types: '" + lhs.toString() + "' and '" + rhs.toString() + "'.");
                }
                return SemaType::Int();
            }
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '-': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands of arithmetic operations must be numeric");
            return SemaType::Unknown();

        case TOK_STAR:
        case TOK_SLASH:
        case TOK_MOD:
            if (strict_mode_ && (lhs.is_pointer() || rhs.is_pointer())) {
                reportError(bin->loc, "Cannot perform multiplication/division on pointer types: '" + lhs.toString() + "' and '" + rhs.toString() + "'.");
                return SemaType::Unknown();
            }
            if (lhs.kind == SemaTypeKind::Int && rhs.kind == SemaTypeKind::Int) return SemaType::Int();
            if (lhs.is_numeric() && rhs.is_numeric()) {
                if (bin->op == TOK_MOD) return SemaType::Int();
                if (lhs.kind == SemaTypeKind::Float || rhs.kind == SemaTypeKind::Float) return SemaType::Float();
                return SemaType::Int();
            }
            if (lhs.is_any() || rhs.is_any() || (!strict_mode_ && (lhs.is_unknown() || rhs.is_unknown()))) return SemaType::Any();
            reportError(bin->loc,
                "Invalid operands to binary operator '" + bin->op_str + "': '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                "Operands of arithmetic operations must be numeric");
            return SemaType::Unknown();

        case TOK_AMP:
        case TOK_PIPE:
        case TOK_CARET:
        case TOK_SHL:
        case TOK_SHR:
            if ((!lhs.is_integral() && !lhs.is_any() && !lhs.is_unknown()) ||
                (!rhs.is_integral() && !rhs.is_any() && !rhs.is_unknown())) {
                reportError(bin->loc,
                    "Bitwise operator '" + bin->op_str + "' requires integral operands, but got '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Bitwise operations are only valid on integer types");
                return SemaType::Unknown();
            }
            return SemaType::Int();

        case TOK_LT:
        case TOK_GT:
        case TOK_LTE:
        case TOK_GTE:
            if (lhs.is_pointer() && rhs.is_pointer()) {
                return SemaType::Bool();
            }
            if ((!lhs.is_numeric() && !lhs.is_any() && !lhs.is_unknown()) ||
                (!rhs.is_numeric() && !rhs.is_any() && !rhs.is_unknown())) {
                reportError(bin->loc,
                    "Comparison operator '" + bin->op_str + "' requires numeric or pointer operands, but got '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Compare numeric or pointer expressions");
            }
            return SemaType::Bool();

        case TOK_EQ:
        case TOK_NEQ:
            if (lhs.is_pointer() && rhs.is_pointer()) {
                return SemaType::Bool();
            }
            if ((lhs.is_pointer() && rhs.is_integral()) || (rhs.is_pointer() && lhs.is_integral())) {
                return SemaType::Bool();
            }
            if (strict_mode_ && (lhs.is_pointer() != rhs.is_pointer())) {
                reportError(bin->loc,
                    "Cannot compare pointer and non-pointer types '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Cast or compare matching types");
                return SemaType::Bool();
            }
            if (!lhs.is_assignable_to(rhs, strict_mode_) && !rhs.is_assignable_to(lhs, strict_mode_)) {
                reportError(bin->loc,
                    "Cannot compare incompatible types '" + lhs.toString() + "' and '" + rhs.toString() + "'.",
                    "Operands of '==' and '!=' must have matching types");
            }
            return SemaType::Bool();

        case TOK_AND:
        case TOK_OR:
            return SemaType::Bool();

        default:
            return SemaType::Unknown();
    }
}

SemaType GrassSema::checkUnaryExpr(const UnaryExprAST* un) {
    if (!un) return SemaType::Unknown();
    SemaType operand_type = checkExpr(un->operand.get());

    switch (un->op) {
        case TOK_MINUS:
            if (!operand_type.is_numeric() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Unary '-' requires numeric operand, got '" + operand_type.toString() + "'.", "Apply unary '-' only to numbers");
                return SemaType::Unknown();
            }
            return operand_type;

        case TOK_NOT:
            return SemaType::Bool();

        case TOK_TILDE:
            if (!operand_type.is_integral() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Bitwise not '~' requires integral operand, got '" + operand_type.toString() + "'.", "Apply bitwise '~' only to integers");
                return SemaType::Unknown();
            }
            return SemaType::Int();

        case TOK_STAR: // Dereference *ptr
            if (!operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Cannot dereference non-pointer type '" + operand_type.toString() + "'.", "Operand of unary '*' must be a pointer");
                return SemaType::Unknown();
            }
            if (operand_type.is_pointer() && operand_type.element_type) {
                return *operand_type.element_type;
            }
            return SemaType::Int(); // Default dereferenced type for untyped pointer

        case TOK_AMP: // Address-of &var
            if (un->operand &&
                un->operand->node_kind != NodeKind::VariableExpr &&
                un->operand->node_kind != NodeKind::MemberAccessExpr &&
                un->operand->node_kind != NodeKind::IndexExpr) {
                reportError(un->loc, "Cannot take address of non-lvalue expression.", "Operand of unary '&' must be an lvalue");
                return SemaType::Unknown();
            }
            return SemaType::Ptr(operand_type);

        case TOK_INC:
        case TOK_DEC:
            if (!operand_type.is_numeric() && !operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
                reportError(un->loc, "Increment/decrement requires numeric or pointer operand.", "Apply '++'/'--' to numeric variable");
                return SemaType::Unknown();
            }
            return operand_type;

        default:
            return operand_type;
    }
}

SemaType GrassSema::checkPostfixExpr(const PostfixExprAST* post) {
    SemaType operand_type = checkExpr(post->operand.get());
    if (!operand_type.is_numeric() && !operand_type.is_pointer() && !operand_type.is_any() && !operand_type.is_unknown()) {
        reportError(post->loc, "Postfix operator requires numeric or pointer operand.", "Apply '++'/'--' to numeric variable");
        return SemaType::Unknown();
    }
    return operand_type;
}

SemaType GrassSema::checkCallExpr(const CallExprAST* call) {
    // Check alloc and free builtins called via call expression
    if (call->callee == "alloc") {
        if (call->args.size() != 1) {
            reportError(call->loc, "Function 'alloc' expects 1 argument, but " + std::to_string(call->args.size()) + " were provided.", "Pass size in bytes to alloc()");
        } else {
            SemaType sz_type = checkExpr(call->args[0].get());
            if (!sz_type.is_integral() && !sz_type.is_any() && !sz_type.is_unknown()) {
                reportError(call->loc, "Argument to 'alloc()' must be an integer, got '" + sz_type.toString() + "'.", "Pass size in bytes as an integer");
            }
        }
        return SemaType::Ptr(SemaType::Void());
    }

    if (call->callee == "free") {
        if (call->args.size() != 1) {
            reportError(call->loc, "Function 'free' expects 1 argument, but " + std::to_string(call->args.size()) + " were provided.", "Pass pointer to free()");
        } else {
            SemaType tgt_type = checkExpr(call->args[0].get());
            if (!tgt_type.is_pointer() && !tgt_type.is_any() && !tgt_type.is_unknown()) {
                reportError(call->loc, "Argument to 'free()' must be a pointer, got '" + tgt_type.toString() + "'.", "Pass a pointer previously allocated with alloc()");
            }
        }
        return SemaType::Void();
    }

    const FunctionSymbol* func = findFunction(call->callee);
    if (!func) {
        // Check if it's a struct constructor: Point()
        if (structs_.count(call->callee)) {
            return SemaType::CustomStruct(call->callee);
        }
        reportError(call->loc, "Call to undeclared function '" + call->callee + "'.", "Check spelling or declare function before calling");
        return SemaType::Unknown();
    }

    // Argument count check
    if (!func->is_variadic && call->args.size() != func->params.size()) {
        reportError(call->loc,
            "Function '" + call->callee + "' expects " + std::to_string(func->params.size()) +
            " arguments, but " + std::to_string(call->args.size()) + " were provided.",
            "Ensure argument count matches function declaration");
    }

    // Argument type check
    size_t count = std::min(call->args.size(), func->params.size());
    for (size_t i = 0; i < count; ++i) {
        SemaType arg_type = checkExpr(call->args[i].get());
        const SemaType& param_type = func->params[i].second;
        if (!arg_type.is_assignable_to(param_type, strict_mode_)) {
            reportError(call->args[i]->loc,
                "Argument " + std::to_string(i + 1) + " of function '" + call->callee + "' expected type '" +
                param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                "Ensure argument type matches expected parameter type");
        }
    }

    return func->return_type;
}

SemaType GrassSema::checkMemberAccessExpr(const MemberAccessExprAST* mem) {
    if (!mem || !mem->object) return SemaType::Unknown();
    SemaType obj_type = checkExpr(mem->object.get());

    if (mem->is_arrow) {
        if (!obj_type.is_pointer()) {
            reportError(mem->loc,
                "Operator '->' requires a pointer type, got '" + obj_type.toString() + "'.",
                "Use '.' for value types or take address with '&'");
            return SemaType::Unknown();
        }
        if (obj_type.element_type) {
            obj_type = *obj_type.element_type;
        } else {
            obj_type = SemaType::Any();
        }
    } else {
        if (obj_type.is_pointer()) {
            if (strict_mode_) {
                reportError(mem->loc,
                    "Cannot use '.' on pointer type '" + obj_type.toString() + "'.",
                    "Use '->' for pointer member access");
                return SemaType::Unknown();
            } else if (obj_type.element_type) {
                obj_type = *obj_type.element_type;
            }
        }
    }

    // Built-in array methods: push, pop, len, size, clear
    if (obj_type.kind == SemaTypeKind::Array) {
        if (mem->is_method_call) {
            if (mem->member == "push") {
                for (const auto& arg : mem->method_args) {
                    checkExpr(arg.get());
                }
                return SemaType::Void();
            } else if (mem->member == "pop") {
                return obj_type.element_type ? *obj_type.element_type : SemaType::Int();
            } else if (mem->member == "len" || mem->member == "size") {
                return SemaType::Int();
            } else if (mem->member == "clear") {
                return SemaType::Void();
            }
        }
        reportError(mem->loc, "Member access '" + mem->member + "' on array type is not supported.", "Use .push(), .pop(), or .len()");
        return SemaType::Unknown();
    }

    if (!obj_type.is_struct() && !obj_type.is_any() && !obj_type.is_unknown()) {
        reportError(mem->loc,
            "Member access '" + mem->member + "' on non-struct type '" + obj_type.toString() + "'.",
            "Only struct and class objects have members");
        return SemaType::Unknown();
    }

    if (obj_type.is_any() || obj_type.is_unknown()) return SemaType::Any();

    const StructSymbol* st = findStruct(obj_type.name);
    if (!st) {
        reportError(mem->loc, "Undefined struct '" + obj_type.name + "'.", "Declare struct before using it");
        return SemaType::Unknown();
    }

    // Helper lambda to search fields across inheritance hierarchy with cycle guard
    auto find_field = [this](const StructSymbol* s, const std::string& field_name,
                             std::unordered_set<std::string>& visited, auto& self) -> const SemaType* {
        if (!s || visited.count(s->name)) return nullptr;
        visited.insert(s->name);
        auto fit = s->fields.find(field_name);
        if (fit != s->fields.end()) return &fit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, field_name, visited, self);
        }
        return nullptr;
    };

    // Helper lambda to search methods across inheritance hierarchy with cycle guard
    auto find_method = [this](const StructSymbol* s, const std::string& method_name,
                              std::unordered_set<std::string>& visited, auto& self) -> const FunctionSymbol* {
        if (!s || visited.count(s->name)) return nullptr;
        visited.insert(s->name);
        auto mit = s->methods.find(method_name);
        if (mit != s->methods.end()) return &mit->second;
        if (!s->base_class.empty()) {
            const StructSymbol* base = findStruct(s->base_class);
            if (base) return self(base, method_name, visited, self);
        }
        return nullptr;
    };

    std::unordered_set<std::string> visited_fields;
    std::unordered_set<std::string> visited_methods;

    if (mem->is_method_call) {
        const FunctionSymbol* method = find_method(st, mem->member, visited_methods, find_method);
        if (!method) {
            reportError(mem->loc,
                "Struct/Class '" + obj_type.name + "' has no method named '" + mem->member + "'.",
                "Check method name or declare method in struct");
            return SemaType::Unknown();
        }

        if (mem->method_args.size() != method->params.size()) {
            reportError(mem->loc,
                "Method '" + mem->member + "' expects " + std::to_string(method->params.size()) +
                " arguments, but " + std::to_string(mem->method_args.size()) + " were provided.",
                "Ensure argument count matches method declaration");
        }

        size_t count = std::min(mem->method_args.size(), method->params.size());
        for (size_t i = 0; i < count; ++i) {
            SemaType arg_type = checkExpr(mem->method_args[i].get());
            const SemaType& param_type = method->params[i].second;
            if (!arg_type.is_assignable_to(param_type, strict_mode_)) {
                reportError(mem->method_args[i]->loc,
                    "Argument " + std::to_string(i + 1) + " of method '" + mem->member + "' expected type '" +
                    param_type.toString() + "', but got '" + arg_type.toString() + "'.",
                    "Ensure argument type matches expected parameter type");
            }
        }

        return method->return_type;
    } else {
        const SemaType* ftype = find_field(st, mem->member, visited_fields, find_field);
        if (!ftype) {
            reportError(mem->loc,
                "Struct '" + obj_type.name + "' has no field named '" + mem->member + "'.",
                "Check struct declaration for available fields");
            return SemaType::Unknown();
        }
        return *ftype;
    }
}

SemaType GrassSema::checkIndexExpr(const IndexExprAST* idx) {
    if (!idx) return SemaType::Unknown();
    SemaType target_type = checkExpr(idx->target.get());
    SemaType idx_type = checkExpr(idx->index.get());

    if (!idx_type.is_integral() && !idx_type.is_any() && !idx_type.is_unknown()) {
        reportError(idx->loc, "Array index must be integer, got '" + idx_type.toString() + "'.", "Use integer value for indexing");
    }

    if (target_type.is_pointer() && target_type.element_type) {
        if (target_type.element_type->is_void()) {
            return SemaType::Int();
        }
        return *target_type.element_type;
    }
    if (target_type.is_string()) {
        return SemaType::Int(); // char
    }
    return SemaType::Any();
}

SemaType GrassSema::checkArrayLiteralExpr(const ArrayLiteralExprAST* arr) {
    SemaType elem_type = SemaType::Any();
    for (const auto& elem : arr->elements) {
        checkExpr(elem.get());
    }
    return SemaType(SemaTypeKind::Array, elem_type.name);
}

SemaType GrassSema::checkAllocExpr(const AllocExprAST* alloc) {
    if (alloc->size_expr) {
        SemaType sz_type = checkExpr(alloc->size_expr.get());
        if (!sz_type.is_integral() && !sz_type.is_any() && !sz_type.is_unknown()) {
            reportError(alloc->loc, "Argument to 'alloc()' must be an integer, got '" + sz_type.toString() + "'.", "Pass size in bytes as an integer");
        }
    }
    return SemaType::Ptr(resolveType(alloc->type_name, 0, alloc->loc));
}

SemaType GrassSema::checkFreeExpr(const FreeExprAST* free_expr) {
    SemaType tgt_type = checkExpr(free_expr->target.get());
    if (!tgt_type.is_pointer() && !tgt_type.is_any() && !tgt_type.is_unknown()) {
        reportError(free_expr->loc, "Argument to 'free()' must be a pointer, got '" + tgt_type.toString() + "'.", "Pass a pointer previously allocated with alloc()");
    }
    return SemaType::Void();
}

SemaType GrassSema::checkCastExpr(const CastExprAST* cast) {
    SemaType src_type = checkExpr(cast->expr.get());
    SemaType dst_type = resolveType(cast->target_type, cast->target_pointer_depth, cast->loc);
    return dst_type;
}

SemaType GrassSema::checkSizeofExpr(const SizeofExprAST* sz) {
    if (!sz->type_name.empty()) {
        resolveType(sz->type_name, sz->pointer_depth, sz->loc);
    } else if (sz->expr) {
        checkExpr(sz->expr.get());
    }
    return SemaType::Int();
}

} // namespace grass::sema
