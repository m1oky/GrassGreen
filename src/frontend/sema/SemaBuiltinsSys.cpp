#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

void GrassSema::registerBuiltinsSys() {
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


    // System and OS
    add_builtin("exit", SemaType::Void(), {SemaType::Int()});
    add_builtin("sys_exit", SemaType::Void(), {SemaType::Int()});
    add_builtin("exec", SemaType::Int(), {SemaType::Any()});
    add_builtin("run", SemaType::Int(), {SemaType::Any()});
    add_builtin("sys_exec", SemaType::Int(), {SemaType::Any()});
    add_builtin("system", SemaType::Int(), {SemaType::Any()});
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


    // JSON (!json!)
    add_builtin("json_clear", SemaType::Void(), {});
    add_builtin("json_add", SemaType::Void(), {SemaType::String(), SemaType::String()});
    add_builtin("json_add_num", SemaType::Void(), {SemaType::String(), SemaType::Float()});
    add_builtin("json_build", SemaType::String(), {});
    add_builtin("json_valid", SemaType::Int(), {SemaType::String()});
    add_builtin("json_get", SemaType::String(), {SemaType::String(), SemaType::String()});
    add_builtin("json_set", SemaType::String(), {SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("json_minify", SemaType::String(), {SemaType::String()});

    // Proc (!proc!) & Sys extended (!sys!)
    add_builtin("proc_find", SemaType::Int(), {SemaType::String()});
    add_builtin("proc_kill", SemaType::Int(), {SemaType::Int()});
    add_builtin("proc_mem", SemaType::Int(), {SemaType::Int()});
    add_builtin("proc_exists", SemaType::Int(), {SemaType::Int()});
    add_builtin("proc_list", SemaType::String(), {});
    add_builtin("find", SemaType::Int(), {SemaType::String()});
    add_builtin("kill", SemaType::Int(), {SemaType::Int()});
    add_builtin("mem", SemaType::Int(), {SemaType::Int()});
    add_builtin("list", SemaType::String(), {});
    add_builtin("cpu_cores", SemaType::Int(), {});
    add_builtin("ram_total_mb", SemaType::Float(), {});
    add_builtin("ram_free_mb", SemaType::Float(), {});
    add_builtin("ram_used_mb", SemaType::Float(), {});
    add_builtin("uptime_sec", SemaType::Float(), {});
    add_builtin("clip_set", SemaType::Int(), {SemaType::String()});
    add_builtin("clip_get", SemaType::String(), {});

    // Hardware (!hardware!)
    add_builtin("hw_cpu_vendor", SemaType::String(), {});
    add_builtin("hw_cpu_brand", SemaType::String(), {});
    add_builtin("hw_cpu_cores", SemaType::Int(), {});
    add_builtin("hw_cpu_threads", SemaType::Int(), {});
    add_builtin("hw_cpu_sockets", SemaType::Int(), {});
    add_builtin("hw_cpu_family", SemaType::Int(), {});
    add_builtin("hw_cpu_freq", SemaType::Int(), {});
    add_builtin("hw_cpu_base_freq", SemaType::Int(), {});
    add_builtin("hw_cpu_usage", SemaType::Int(), {});
    add_builtin("hw_cpu_temp", SemaType::Int(), {});
    add_builtin("hw_cpu_cache_l1", SemaType::Int(), {});
    add_builtin("hw_cpu_cache_l2", SemaType::Int(), {});
    add_builtin("hw_cpu_cache_l3", SemaType::Int(), {});
    add_builtin("hw_cpu_virt", SemaType::String(), {});
    add_builtin("hw_uptime_sec", SemaType::Int(), {});
    add_builtin("hw_uptime_str", SemaType::String(), {});
    add_builtin("hw_ram_total", SemaType::Int(), {});
    add_builtin("hw_ram_used", SemaType::Int(), {});
    add_builtin("hw_ram_free", SemaType::Int(), {});
    add_builtin("hw_ram_cached", SemaType::Int(), {});
    add_builtin("hw_ram_speed", SemaType::Int(), {});
    add_builtin("hw_ram_temp", SemaType::Int(), {});
    add_builtin("hw_ram_model", SemaType::String(), {});
    add_builtin("hw_ram_form", SemaType::String(), {});
    add_builtin("hw_disk_size", SemaType::Int(), {});
    add_builtin("hw_disk_free", SemaType::Int(), {});
    add_builtin("hw_disk_format", SemaType::String(), {});
    add_builtin("hw_disk_model", SemaType::String(), {});
    add_builtin("hw_disk_type", SemaType::String(), {});
    add_builtin("hw_disk_temp", SemaType::Int(), {});
    add_builtin("hw_disk_read_speed", SemaType::Int(), {});
    add_builtin("hw_disk_write_speed", SemaType::Int(), {});
    add_builtin("hw_disk_resp_time", SemaType::Int(), {});
    add_builtin("hw_disk_usage", SemaType::Int(), {});
    add_builtin("hw_disk_is_system", SemaType::Int(), {});
    add_builtin("hw_pagefile_total", SemaType::Int(), {});
    add_builtin("hw_pagefile_used", SemaType::Int(), {});
    add_builtin("hw_net_adapter", SemaType::String(), {});
    add_builtin("hw_net_type", SemaType::String(), {});
    add_builtin("hw_net_ipv4", SemaType::String(), {});
    add_builtin("hw_net_ipv6", SemaType::String(), {});
    add_builtin("hw_net_recv_speed", SemaType::Int(), {});
    add_builtin("hw_net_send_speed", SemaType::Int(), {});
    add_builtin("hw_net_recv_total", SemaType::Int(), {});
    add_builtin("hw_net_send_total", SemaType::Int(), {});
    add_builtin("hw_gpu_name", SemaType::String(), {});
    add_builtin("hw_gpu_usage", SemaType::Int(), {});
    add_builtin("hw_gpu_temp", SemaType::Int(), {});
    add_builtin("hw_gpu_vram_total", SemaType::Int(), {});
    add_builtin("hw_gpu_vram_used", SemaType::Int(), {});
    add_builtin("hw_gpu_vram_shared", SemaType::Int(), {});
    add_builtin("hw_board_vendor", SemaType::String(), {});
    add_builtin("hw_board_model", SemaType::String(), {});
    add_builtin("hw_bios_vendor", SemaType::String(), {});
    add_builtin("hw_bios_date", SemaType::String(), {});
    add_builtin("hw_bios_version", SemaType::String(), {});
    add_builtin("hw_pci_count", SemaType::Int(), {});
    add_builtin("hw_rdtsc", SemaType::Int(), {});
    add_builtin("hw_reboot", SemaType::Void(), {});

    // Telegram (!tg!)
    add_builtin("tg_init", SemaType::Void(), {SemaType::String()});
    add_builtin("tg_poll", SemaType::String(), {}, true);
    add_builtin("tg_get_user", SemaType::String(), {});
    add_builtin("tg_get_chat_id", SemaType::String(), {});
    add_builtin("tg_get_msg_id", SemaType::String(), {});
    add_builtin("tg_get_callback", SemaType::String(), {});
    add_builtin("tg_send", SemaType::Int(), {SemaType::String(), SemaType::String()});
    add_builtin("tg_reply", SemaType::Int(), {SemaType::String()});
    add_builtin("tg_send_btn", SemaType::Int(), {SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("tg_send_inline", SemaType::Int(), {SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("tg_send_webapp", SemaType::Int(), {SemaType::String(), SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("tg_send_photo", SemaType::Int(), {SemaType::String(), SemaType::String()}, true);
    add_builtin("tg_send_dice", SemaType::Int(), {SemaType::String(), SemaType::String()});
    add_builtin("tg_edit", SemaType::Int(), {SemaType::String(), SemaType::String(), SemaType::String()});
    add_builtin("tg_del", SemaType::Int(), {SemaType::String(), SemaType::String()});
    add_builtin("telegram_init", SemaType::Void(), {SemaType::String()});
    add_builtin("send_message", SemaType::Int(), {SemaType::Any(), SemaType::String()});
    add_builtin("send_photo", SemaType::Int(), {SemaType::Any(), SemaType::String()}, true);
    add_builtin("send_document", SemaType::Int(), {SemaType::Any(), SemaType::String()});

    // LangGen (!langgen!)
    add_builtin("lang_new", SemaType::Int(), {SemaType::String()});
    add_builtin("lang_comment", SemaType::Int(), {SemaType::Int(), SemaType::String()});
    add_builtin("lang_rule", SemaType::Int(), {SemaType::Int(), SemaType::String(), SemaType::String()});
    add_builtin("lang_parse", SemaType::Int(), {SemaType::Int(), SemaType::String()});
    add_builtin("lang_count", SemaType::Int(), {SemaType::Int()});
    add_builtin("lang_cmd", SemaType::String(), {SemaType::Int(), SemaType::Int()});
    add_builtin("lang_arg", SemaType::String(), {SemaType::Int(), SemaType::Int(), SemaType::Int()});
    add_builtin("lang_eval", SemaType::Any(), {SemaType::Int(), SemaType::String()});
    add_builtin("lang_set_var", SemaType::Void(), {SemaType::Int(), SemaType::Any(), SemaType::Any()});
    add_builtin("lang_get_var", SemaType::Any(), {SemaType::Int(), SemaType::Any()});

    // Net high-level (!GrassNet!) & Backlog & MMIO 16/32 & GUI & Threads
    add_builtin("local_ip", SemaType::String(), {});
    add_builtin("resolve_ip", SemaType::String(), {SemaType::String()});
    add_builtin("ping", SemaType::Float(), {SemaType::String()}, true);
    add_builtin("net_status", SemaType::Int(), {});
    add_builtin("net_set_agent", SemaType::Int(), {SemaType::String(), SemaType::String()});
    add_builtin("API", SemaType::Void(), {SemaType::String()});
    add_builtin("create_serv", SemaType::Int(), {}, true);
    add_builtin("download", SemaType::Int(), {SemaType::String(), SemaType::String()});
    add_builtin("connect", SemaType::Int(), {SemaType::Any(), SemaType::Any()}, true);
    add_builtin("accept", SemaType::Any(), {}, true);
    add_builtin("net_close", SemaType::Int(), {}, true);

    add_builtin("fat_status", SemaType::Int(), {});
    add_builtin("ticks", SemaType::Int(), {});
    add_builtin("sleep", SemaType::Void(), {SemaType::Int()});
    add_builtin("threads", SemaType::Int(), {});
    add_builtin("clear", SemaType::Void(), {});
    add_builtin("cls", SemaType::Void(), {});
    add_builtin("clear_all", SemaType::Void(), {});
    add_builtin("cls_all", SemaType::Void(), {});
    add_builtin("not_clear", SemaType::Void(), {}, true);
    add_builtin("not_clear_on", SemaType::Void(), {});
    add_builtin("not_clear_off", SemaType::Void(), {});
    add_builtin("color", SemaType::Void(), {SemaType::Any()}, true);
    add_builtin("read_key", SemaType::String(), {});
    add_builtin("draw_box", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()}, true);
    add_builtin("menu", SemaType::Int(), {SemaType::String(), SemaType::Any()});
    add_builtin("progress_bar", SemaType::Void(), {SemaType::Int(), SemaType::Int(), SemaType::Int(), SemaType::Int()}, true);
    add_builtin("mouse_init", SemaType::Void(), {});
    add_builtin("mouse_x", SemaType::Int(), {});
    add_builtin("mouse_y", SemaType::Int(), {});
    add_builtin("mouse_btn", SemaType::Int(), {});
    add_builtin("mouse_down", SemaType::Bool(), {}, true);
    add_builtin("mouse_poll", SemaType::Void(), {});
    add_builtin("mouse_cursor", SemaType::Void(), {}, true);

    add_builtin("inw", SemaType::Int(), {SemaType::Int()});
    add_builtin("inl", SemaType::Int(), {SemaType::Int()});
    add_builtin("outw", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("outl", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("io_in16", SemaType::Int(), {SemaType::Int()});
    add_builtin("io_in32", SemaType::Int(), {SemaType::Int()});
    add_builtin("io_out16", SemaType::Void(), {SemaType::Int(), SemaType::Int()});
    add_builtin("io_out32", SemaType::Void(), {SemaType::Int(), SemaType::Int()});


    add_builtin("thread", SemaType::Int(), {SemaType::Any()}, true);
    add_builtin("async", SemaType::Int(), {SemaType::Any()}, true);

}

} // namespace grass::sema
