#include "native/GrassObjWriter.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace grass::backend {

    std::vector<WinLibraryImport> GrassObjWriter::get_default_windows_imports() {
        return {
            {
                "KERNEL32.dll",
                {
                    "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
                    "HeapAlloc", "HeapFree", "GetProcessHeap",
                    "QueryPerformanceCounter", "QueryPerformanceFrequency",
                    "Sleep", "LoadLibraryA", "GetProcAddress",
                    "GetLocalTime", "VirtualAlloc", "VirtualFree",
                    "GetLastError", "CreateFileA", "CloseHandle"
                }
            },
            {
                "USER32.dll",
                {
                    "MessageBoxA"
                }
            },
            {
                "WS2_32.dll",
                {
                    "WSAStartup", "WSACleanup", "socket", "connect", "send", "recv", "closesocket"
                }
            }
        };
    }

    bool GrassObjWriter::generate_import_table(const std::vector<WinLibraryImport>& imports,
                                              uint32_t idata_rva,
                                              BinaryBuffer& out_idata,
                                              std::unordered_map<std::string, uint32_t>& out_iat_offsets,
                                              std::string* out_error,
                                              uint32_t* out_iat_rva,
                                              uint32_t* out_iat_size) {
        (void)out_error;
        if (imports.empty()) {
            out_idata.clear();
            out_iat_offsets.clear();
            if (out_iat_rva) *out_iat_rva = 0;
            if (out_iat_size) *out_iat_size = 0;
            return true;
        }

        out_idata.clear();
        out_iat_offsets.clear();

        size_t num_dlls = imports.size();
        size_t dir_table_size = (num_dlls + 1) * 20;
        size_t current_offset = dir_table_size;

        std::vector<size_t> ilt_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            ilt_offsets[i] = current_offset;
            current_offset += (imports[i].symbols.size() + 1) * 8;
        }

        std::vector<size_t> iat_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            iat_offsets[i] = current_offset;
            current_offset += (imports[i].symbols.size() + 1) * 8;
        }

        size_t iat_start_off = iat_offsets.empty() ? 0 : iat_offsets[0];
        size_t iat_total_bytes = current_offset - iat_start_off;

        std::vector<size_t> name_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            name_offsets[i] = current_offset;
            current_offset += imports[i].dll_name.size() + 1;
        }

        if (current_offset % 2 != 0) current_offset++;

        std::vector<std::vector<size_t>> hint_name_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            hint_name_offsets[i].resize(imports[i].symbols.size());
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                hint_name_offsets[i][s] = current_offset;
                current_offset += 2 + imports[i].symbols[s].size() + 1;
                if (current_offset % 2 != 0) current_offset++;
            }
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            uint32_t ilt_rva = static_cast<uint32_t>(idata_rva + ilt_offsets[i]);
            uint32_t name_rva = static_cast<uint32_t>(idata_rva + name_offsets[i]);
            uint32_t iat_rva = static_cast<uint32_t>(idata_rva + iat_offsets[i]);

            out_idata.emit_u32(ilt_rva);
            out_idata.emit_u32(0);
            out_idata.emit_u32(0);
            out_idata.emit_u32(name_rva);
            out_idata.emit_u32(iat_rva);
        }
        out_idata.emit_zeros(20);

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                uint64_t hint_name_rva = idata_rva + hint_name_offsets[i][s];
                out_idata.emit_u64(hint_name_rva);
            }
            out_idata.emit_u64(0);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                size_t entry_offset = out_idata.size();
                out_iat_offsets[imports[i].symbols[s]] = static_cast<uint32_t>(idata_rva + entry_offset);
                out_iat_offsets["__imp_" + imports[i].symbols[s]] = static_cast<uint32_t>(idata_rva + entry_offset);
                uint64_t hint_name_rva = idata_rva + hint_name_offsets[i][s];
                out_idata.emit_u64(hint_name_rva);
            }
            out_idata.emit_u64(0);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            out_idata.emit_cstring(imports[i].dll_name);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                if (out_idata.size() % 2 != 0) out_idata.emit_u8(0);
                out_idata.emit_u16(static_cast<uint16_t>(s));
                out_idata.emit_cstring(imports[i].symbols[s]);
            }
        }

        if (out_iat_rva) *out_iat_rva = static_cast<uint32_t>(idata_rva + iat_start_off);
        if (out_iat_size) *out_iat_size = static_cast<uint32_t>(iat_total_bytes);

        return true;
    }

    static std::string detect_dll_for_symbol(const std::string& raw_name) {
        std::string s = raw_name;
        if (s.rfind("__imp_", 0) == 0) {
            s = s.substr(6);
        }

        static const std::unordered_set<std::string> k32_syms = {
            "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
            "HeapAlloc", "HeapFree", "GetProcessHeap", "HeapReAlloc",
            "QueryPerformanceCounter", "QueryPerformanceFrequency",
            "GetTickCount64", "Sleep", "LoadLibraryA", "GetProcAddress",
            "FreeLibrary", "GetLocalTime", "GetSystemTime",
            "VirtualAlloc", "VirtualFree",
            "GetLastError", "SetLastError", "CreateFileA", "CloseHandle",
            "GetCommandLineA", "GetModuleHandleA", "RtlZeroMemory",
            "GetEnvironmentVariableA", "SetEnvironmentVariableA",
            "GetComputerNameA", "CreateProcessA", "WinExec",
            "CreateDirectoryA", "GetFileAttributesA", "CopyFileA",
            "CreateThread", "WaitForSingleObject", "CreateMutexA", "ReleaseMutex",
            "TlsAlloc", "TlsGetValue", "TlsSetValue", "TlsFree",
            "GetProcessAffinityMask", "SetProcessAffinityMask",
            "AddVectoredExceptionHandler", "RemoveVectoredExceptionHandler",
            "RtlCaptureContext", "RtlLookupFunctionEntry", "RtlVirtualUnwind",
            "RaiseException", "lstrlenA", "lstrlenW", "lstrcpyA", "lstrcatA",
            "lstrcmpA", "lstrcmpiA", "FormatMessageA",
            "GetFileSizeEx", "DeleteFileA", "SetConsoleTextAttribute"
        };
        if (k32_syms.find(s) != k32_syms.end()) return "KERNEL32.dll";

        static const std::unordered_set<std::string> u32_syms = {
            "MessageBoxA", "MessageBoxW", "GetDesktopWindow", "GetSystemMetrics",
            "OpenClipboard", "CloseClipboard", "GetClipboardData", "SetClipboardData",
            "EmptyClipboard", "GetAsyncKeyState"
        };
        if (u32_syms.find(s) != u32_syms.end()) return "USER32.dll";

        static const std::unordered_set<std::string> ws2_syms = {
            "WSAStartup", "WSACleanup", "socket", "connect", "send", "recv", "closesocket",
            "bind", "listen", "accept", "htons", "ntohs", "inet_addr", "gethostbyname",
            "WSAGetLastError"
        };
        if (ws2_syms.find(s) != ws2_syms.end()) return "WS2_32.dll";

        static const std::unordered_set<std::string> advapi_syms = {
            "GetUserNameA", "RegOpenKeyExA", "RegCloseKey", "RegQueryValueExA"
        };
        if (advapi_syms.find(s) != advapi_syms.end()) return "ADVAPI32.dll";

        static const std::unordered_set<std::string> crt_syms = {
            "printf", "puts", "putchar", "scanf", "getchar",
            "malloc", "free", "calloc", "realloc",
            "exit", "_exit", "abort",
            "memset", "memcpy", "memmove", "memcmp",
            "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat",
            "sprintf", "snprintf", "fprintf", "fputs",
            "sin", "cos", "tan", "sqrt", "pow", "floor", "ceil", "fabs",
            "atoi", "atof", "atol", "strtoll", "strtoull"
        };
        if (crt_syms.find(s) != crt_syms.end()) return "msvcrt.dll";

        static const std::unordered_set<std::string> vm_dll_syms = {
            "create_canvas", "fill_color", "draw_rectangle", "draw_circle", "draw_line", "save_image",
            "audio_init", "synth_play", "synth", "tone", "beep", "sound_play", "sound_stop", "sound_pause",
            "sound_resume", "sound_volume", "sound_is_playing", "bind_synth", "bind_sound", "attach_sound",
            "pc_name", "user_name", "screen_w", "screen_h", "cpu_cores", "ram_total_mb", "ram_free_mb",
            "ram_used_mb", "uptime_sec", "clip_set", "clip_get", "get_env",
            "find", "kill", "mem", "list",
            "telegram_init", "send_message", "send_photo", "send_document",
            "local_ip", "resolve_ip", "ping", "net_status", "net_set_agent", "API", "create_serv",
            "net_connect_host", "net_accept_srv", "net_close_srv", "download",
            "fat_status", "ticks", "threads", "grass_sleep_sec", "clear", "cls", "clear_all", "cls_all",
            "not_clear", "not_clear_on", "not_clear_off",
            "color", "grass_bm_color_int", "grass_bm_color_str",
            "read_key", "draw_box", "menu", "progress_bar",
            "mouse_init", "mouse_x", "mouse_y", "mouse_btn", "mouse_down", "mouse_down_str",
            "mouse_poll", "mouse_cursor",
            "get_date", "get_time_str", "memory_usit", "memory_free", "memory_clean",
            "fs_mkdir", "fs_is_dir", "fs_size", "fs_copy", "fs_list", "fs_read", "fs_write", "fs_exists", "fs_remove",
            "inw", "inl", "outw", "outl", "inb", "outb",
            "memory_barrier", "read_barrier", "write_barrier",
            "surface", "auto_size_display", "set_text", "get_text", "set_val", "get_val",
            "set_color", "set_bg", "set_visible", "is_checked", "add_item", "clear_list",
            "get_selected", "switch_tab", "fullscreen", "web_start", "web_compile",
            "grass_native_thread_spawn", "grass_native_thread_id"
        };
        if (s.rfind("g2d_", 0) == 0 || s.rfind("gfx_", 0) == 0 ||
            s.rfind("g3d_", 0) == 0 || s.rfind("phys_", 0) == 0 ||
            s.rfind("v3_", 0) == 0 || s.rfind("m4_", 0) == 0 ||
            s.rfind("hw_", 0) == 0 || s.rfind("json_", 0) == 0 ||
            s.rfind("proc_", 0) == 0 || s.rfind("tg_", 0) == 0 ||
            s.rfind("lang_", 0) == 0 || s.rfind("gui_", 0) == 0 ||
            s.rfind("mmio_", 0) == 0 || vm_dll_syms.find(s) != vm_dll_syms.end()) {
            return "grassvm.dll";
        }

        return "KERNEL32.dll";
    }



    static const uint8_t s_pe_dos_stub[64] = {
        0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68,
        0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
        0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20,
        0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    bool GrassObjWriter::write_pe_executable(const CompiledUnit& unit,
                                            BinaryBuffer& out,
                                            std::string* out_error,
                                            const std::string& entry_point_name) {
        std::vector<WinLibraryImport> empty_imports;
        return write_pe_executable(unit, empty_imports, out, out_error, entry_point_name);
    }

    bool GrassObjWriter::write_pe_executable(const CompiledUnit& unit,
                                            const std::vector<WinLibraryImport>& custom_imports,
                                            BinaryBuffer& out,
                                            std::string* out_error,
                                            const std::string& entry_point_name) {
        CompiledUnit local_unit = unit;
        inject_missing_win64_runtime(local_unit);

        if (!validate_compiled_unit(local_unit, out_error)) {
            return false;
        }
        if (local_unit.arch != TargetArch::X86_64) {
            if (out_error) *out_error = "PE32+ executable writer only supports X86_64 architecture";
            return false;
        }
        if (local_unit.text.empty()) {
            if (out_error) *out_error = "Cannot generate PE executable with empty .text section";
            return false;
        }

        // 1. Locate entry point offset in .text
        uint32_t entry_offset = 0;
        bool entry_found = false;
        std::string target_entry = entry_point_name;
        if (target_entry.empty() || target_entry == "main") {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text && s.name == "_start") {
                    target_entry = "_start";
                    break;
                }
            }
        }
        if (!target_entry.empty()) {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text && s.name == target_entry) {
                    entry_offset = static_cast<uint32_t>(s.offset);
                    entry_found = true;
                    break;
                }
            }
        }
        if (!entry_found) {
            const char* candidates[] = {"_start", "main", "start", "WinMain"};
            for (const char* cand : candidates) {
                for (const auto& s : local_unit.symbols) {
                    if (!s.is_external && s.section == SectionKind::Text && s.name == cand) {
                        entry_offset = static_cast<uint32_t>(s.offset);
                        entry_found = true;
                        break;
                    }
                }
                if (entry_found) break;
            }
        }
        if (!entry_found) {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text) {
                    entry_offset = static_cast<uint32_t>(s.offset);
                    entry_found = true;
                    break;
                }
            }
        }

        std::unordered_set<std::string> internal_syms;
        for (const auto& s : local_unit.symbols) {
            if (!s.is_external) internal_syms.insert(s.name);
        }

        // 2. Determine import libraries and symbols
        std::vector<WinLibraryImport> final_imports;
        if (!custom_imports.empty()) {
            final_imports = custom_imports;
        } else {
            std::unordered_set<std::string> needed_externals;
            for (const auto& r : local_unit.relocations) {
                if (internal_syms.find(r.symbol_name) == internal_syms.end()) {
                    std::string n = r.symbol_name;
                    if (n.rfind("__imp_", 0) == 0) n = n.substr(6);
                    if (internal_syms.find(n) == internal_syms.end()) {
                        needed_externals.insert(n);
                    }
                }
            }
            if (needed_externals.empty()) {
                needed_externals.insert("ExitProcess");
            }

            std::unordered_map<std::string, std::vector<std::string>> groups;
            std::vector<std::string> dlls;
            for (const auto& sym : needed_externals) {
                std::string dll = detect_dll_for_symbol(sym);
                if (groups.find(dll) == groups.end()) {
                    dlls.push_back(dll);
                }
                groups[dll].push_back(sym);
            }
            std::sort(dlls.begin(), dlls.end(), [](const std::string& a, const std::string& b) {
                auto prio = [](const std::string& s) {
                    if (s == "KERNEL32.dll") return 0;
                    if (s == "USER32.dll") return 1;
                    if (s == "WS2_32.dll") return 2;
                    if (s == "ADVAPI32.dll") return 3;
                    if (s == "msvcrt.dll") return 4;
                    return 5;
                };
                int pa = prio(a), pb = prio(b);
                if (pa != pb) return pa < pb;
                return a < b;
            });
            for (const auto& dll : dlls) {
                WinLibraryImport imp;
                imp.dll_name = dll;
                imp.symbols = groups[dll];
                std::sort(imp.symbols.begin(), imp.symbols.end());
                final_imports.push_back(std::move(imp));
            }
        }

        // 3. Prepare section buffers
        std::vector<uint8_t> text_data = local_unit.text;
        std::unordered_map<std::string, uint32_t> thunk_offsets;
        for (const auto& imp : final_imports) {
            for (const auto& sym : imp.symbols) {
                uint32_t thunk_start = static_cast<uint32_t>(text_data.size());
                thunk_offsets[sym] = thunk_start;
                thunk_offsets["__imp_" + sym] = thunk_start;
                // Emit: FF 25 00 00 00 00 (jmp qword ptr [rip + disp32])
                text_data.push_back(0xFF);
                text_data.push_back(0x25);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
            }
        }

        const uint64_t image_base = PE_DEFAULT_IMAGE_BASE;
        const uint32_t sec_align = PE_DEFAULT_SECTION_ALIGNMENT;
        const uint32_t file_align = PE_DEFAULT_FILE_ALIGNMENT;

        auto align_up = [](uint64_t val, uint64_t align) -> uint64_t {
            return (val + align - 1) / align * align;
        };

        // Layout RVAs and file offsets
        uint32_t text_rva = 0x1000;
        uint32_t text_file_offset = 0x200;
        uint32_t text_vsize = static_cast<uint32_t>(text_data.size());
        uint32_t text_raw_size = static_cast<uint32_t>(align_up(text_vsize, file_align));

        uint32_t rdata_rva = static_cast<uint32_t>(align_up(text_rva + text_vsize, sec_align));
        uint32_t rdata_file_offset = text_file_offset + text_raw_size;

        size_t rodata_raw_len = local_unit.rodata.size();
        size_t idata_offset_in_rdata = align_up(rodata_raw_len, 16);
        uint32_t idata_rva = static_cast<uint32_t>(rdata_rva + idata_offset_in_rdata);

        BinaryBuffer idata_buf;
        std::unordered_map<std::string, uint32_t> iat_offsets;
        uint32_t iat_rva = 0;
        uint32_t iat_size = 0;
        if (!generate_import_table(final_imports, idata_rva, idata_buf, iat_offsets, out_error, &iat_rva, &iat_size)) {
            return false;
        }

        std::vector<uint8_t> rdata_data = local_unit.rodata;
        if (idata_offset_in_rdata > rdata_data.size()) {
            rdata_data.resize(idata_offset_in_rdata, 0);
        }
        rdata_data.insert(rdata_data.end(), idata_buf.data(), idata_buf.data() + idata_buf.size());
        uint32_t rdata_vsize = static_cast<uint32_t>(rdata_data.size());
        uint32_t rdata_raw_size = static_cast<uint32_t>(align_up(rdata_vsize, file_align));

        uint32_t data_rva = static_cast<uint32_t>(align_up(rdata_rva + rdata_vsize, sec_align));
        uint32_t data_file_offset = rdata_file_offset + rdata_raw_size;
        std::vector<uint8_t> data_data = local_unit.data;
        uint32_t data_content_size = static_cast<uint32_t>(data_data.size());
        uint32_t data_vsize = std::max<uint32_t>(16, static_cast<uint32_t>(data_content_size + local_unit.bss_size));
        uint32_t data_raw_size = (data_content_size > 0) ? static_cast<uint32_t>(align_up(data_content_size, file_align)) : file_align;

        uint32_t size_of_image = static_cast<uint32_t>(align_up(data_rva + data_vsize, sec_align));

        // 4. Patch thunk displacements to point to IAT slots
        for (const auto& imp : final_imports) {
            for (const auto& sym : imp.symbols) {
                uint32_t thunk_start = thunk_offsets[sym];
                uint32_t disp_off = thunk_start + 2;
                uint64_t disp_va = image_base + text_rva + disp_off;
                uint64_t next_rip = disp_va + 4;
                uint64_t slot_va = image_base + iat_offsets[sym];
                int32_t disp32 = static_cast<int32_t>(static_cast<int64_t>(slot_va) - static_cast<int64_t>(next_rip));
                text_data[disp_off]     = static_cast<uint8_t>(disp32 & 0xFF);
                text_data[disp_off + 1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                text_data[disp_off + 2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                text_data[disp_off + 3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
            }
        }

        // 5. Build symbol address map
        std::unordered_map<std::string, uint64_t> sym_vas;
        for (const auto& s : local_unit.symbols) {
            if (s.is_external) continue;
            uint64_t va = 0;
            if (s.section == SectionKind::Text) va = image_base + text_rva + s.offset;
            else if (s.section == SectionKind::RoData) va = image_base + rdata_rva + s.offset;
            else if (s.section == SectionKind::Data) va = image_base + data_rva + s.offset;
            else if (s.section == SectionKind::Bss) va = image_base + data_rva + data_content_size + s.offset;
            sym_vas[s.name] = va;
        }
        for (const auto& [name, rva] : iat_offsets) {
            sym_vas["__imp_" + name] = image_base + rva;
            if (internal_syms.find(name) == internal_syms.end()) {
                auto it_thunk = thunk_offsets.find(name);
                if (it_thunk != thunk_offsets.end()) {
                    sym_vas[name] = image_base + text_rva + it_thunk->second;
                } else {
                    sym_vas[name] = image_base + rva;
                }
            }
        }

        // 6. Relocation patching
        for (const auto& rel : local_unit.relocations) {
            uint64_t site_va = 0;
            uint8_t* patch_ptr = nullptr;

            if (rel.section == SectionKind::Text) {
                if (rel.offset + 4 > text_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .text: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + text_rva + rel.offset;
                patch_ptr = text_data.data() + rel.offset;
            } else if (rel.section == SectionKind::RoData) {
                if (rel.offset + 4 > rdata_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .rodata: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + rdata_rva + rel.offset;
                patch_ptr = rdata_data.data() + rel.offset;
            } else if (rel.section == SectionKind::Data) {
                if (rel.offset + 4 > data_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .data: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + data_rva + rel.offset;
                patch_ptr = data_data.data() + rel.offset;
            } else {
                if (out_error) *out_error = "Unsupported relocation section";
                return false;
            }

            // Direct CALL [rip + disp32] (FF 15) or JMP [rip + disp32] (FF 25)
            if (rel.section == SectionKind::Text && rel.offset >= 2 && patch_ptr) {
                uint8_t op0 = text_data[rel.offset - 2];
                uint8_t op1 = text_data[rel.offset - 1];
                if (op0 == 0xFF && (op1 == 0x15 || op1 == 0x25)) {
                    std::string clean = rel.symbol_name;
                    if (clean.rfind("__imp_", 0) == 0) clean = clean.substr(6);
                    auto it_iat = iat_offsets.find(clean);
                    if (it_iat != iat_offsets.end()) {
                        uint64_t slot_va = image_base + it_iat->second;
                        uint64_t next_rip = site_va + 4;
                        int32_t disp32 = static_cast<int32_t>(static_cast<int64_t>(slot_va) - static_cast<int64_t>(next_rip));
                        patch_ptr[0] = static_cast<uint8_t>(disp32 & 0xFF);
                        patch_ptr[1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                        patch_ptr[2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                        patch_ptr[3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
                        continue;
                    }
                }
            }

            // Standard relocation target lookup
            auto it_sym = sym_vas.find(rel.symbol_name);
            if (it_sym == sym_vas.end()) {
                std::string clean = rel.symbol_name;
                if (clean.rfind("__imp_", 0) == 0) clean = clean.substr(6);
                it_sym = sym_vas.find(clean);
                if (it_sym == sym_vas.end()) {
                    if (out_error) *out_error = "Unresolved symbol in PE executable: " + rel.symbol_name;
                    return false;
                }
            }
            uint64_t sym_va = it_sym->second;

            if (rel.type == RelocType::Rel32) {
                int64_t addend = (rel.addend == 0) ? -4 : rel.addend;
                int64_t disp = static_cast<int64_t>(sym_va + addend) - static_cast<int64_t>(site_va);
                int32_t disp32 = static_cast<int32_t>(disp);
                patch_ptr[0] = static_cast<uint8_t>(disp32 & 0xFF);
                patch_ptr[1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                patch_ptr[2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                patch_ptr[3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
            } else if (rel.type == RelocType::Abs64) {
                if (rel.offset + 8 > ((rel.section == SectionKind::Data) ? data_data.size() : text_data.size())) {
                    if (out_error) *out_error = "Abs64 relocation out of range";
                    return false;
                }
                uint64_t abs_val = sym_va + rel.addend;
                for (size_t i = 0; i < 8; ++i) {
                    patch_ptr[i] = static_cast<uint8_t>((abs_val >> (i * 8)) & 0xFF);
                }
            } else if (rel.type == RelocType::Abs32) {
                uint32_t abs_val = static_cast<uint32_t>(sym_va + rel.addend);
                patch_ptr[0] = static_cast<uint8_t>(abs_val & 0xFF);
                patch_ptr[1] = static_cast<uint8_t>((abs_val >> 8) & 0xFF);
                patch_ptr[2] = static_cast<uint8_t>((abs_val >> 16) & 0xFF);
                patch_ptr[3] = static_cast<uint8_t>((abs_val >> 24) & 0xFF);
            }
        }

        // 7. Assemble PE executable file
        PE_DosHeader dos_hdr{};
        dos_hdr.e_magic = PE_DOS_MAGIC;
        dos_hdr.e_cblp = 0x0090;
        dos_hdr.e_cp = 0x0003;
        dos_hdr.e_cparhdr = 0x0004;
        dos_hdr.e_maxalloc = 0xFFFF;
        dos_hdr.e_sp = 0x00B8;
        dos_hdr.e_lfarlc = 0x0040;
        dos_hdr.e_lfanew = 0x0080;

        COFF_FileHeader file_hdr{};
        file_hdr.machine = COFF_MACHINE_AMD64;
        file_hdr.number_of_sections = 3;
        file_hdr.time_date_stamp = 0;
        file_hdr.pointer_to_symbol_table = 0;
        file_hdr.number_of_symbols = 0;
        file_hdr.size_of_optional_header = sizeof(PE32Plus_OptionalHeader);
        file_hdr.characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

        PE32Plus_OptionalHeader opt_hdr{};
        opt_hdr.magic = PE_OPTIONAL_HDR64_MAGIC;
        opt_hdr.major_linker_version = 14;
        opt_hdr.minor_linker_version = 0;
        opt_hdr.size_of_code = text_raw_size;
        opt_hdr.size_of_initialized_data = rdata_raw_size + data_raw_size;
        opt_hdr.size_of_uninitialized_data = static_cast<uint32_t>(local_unit.bss_size);
        opt_hdr.address_of_entry_point = text_rva + entry_offset;
        opt_hdr.base_of_code = text_rva;
        opt_hdr.image_base = image_base;
        opt_hdr.section_alignment = sec_align;
        opt_hdr.file_alignment = file_align;
        opt_hdr.major_operating_system_version = 6;
        opt_hdr.minor_operating_system_version = 0;
        opt_hdr.major_image_version = 0;
        opt_hdr.minor_image_version = 0;
        opt_hdr.major_subsystem_version = 6;
        opt_hdr.minor_subsystem_version = 0;
        opt_hdr.win32_version_value = 0;
        opt_hdr.size_of_image = size_of_image;
        opt_hdr.size_of_headers = 0x200;
        opt_hdr.check_sum = 0;
        opt_hdr.subsystem = PE_SUBSYSTEM_WINDOWS_CUI;
        opt_hdr.dll_characteristics = 0x8100;
        opt_hdr.size_of_stack_reserve = 0x100000;
        opt_hdr.size_of_stack_commit = 0x1000;
        opt_hdr.size_of_heap_reserve = 0x100000;
        opt_hdr.size_of_heap_commit = 0x1000;
        opt_hdr.loader_flags = 0;
        opt_hdr.number_of_rva_and_sizes = 16;

        opt_hdr.data_directories[1].virtual_address = idata_rva;
        opt_hdr.data_directories[1].size = static_cast<uint32_t>((final_imports.size() + 1) * 20);
        opt_hdr.data_directories[12].virtual_address = iat_rva;
        opt_hdr.data_directories[12].size = iat_size;

        COFF_SectionHeader sec_text{};
        std::memcpy(sec_text.name, ".text\0\0\0", 8);
        sec_text.virtual_size = text_vsize;
        sec_text.virtual_address = text_rva;
        sec_text.size_of_raw_data = text_raw_size;
        sec_text.pointer_to_raw_data = text_file_offset;
        sec_text.characteristics = COFF_SCN_CNT_CODE | COFF_SCN_MEM_EXECUTE | COFF_SCN_MEM_READ;

        COFF_SectionHeader sec_rdata{};
        std::memcpy(sec_rdata.name, ".rdata\0\0", 8);
        sec_rdata.virtual_size = rdata_vsize;
        sec_rdata.virtual_address = rdata_rva;
        sec_rdata.size_of_raw_data = rdata_raw_size;
        sec_rdata.pointer_to_raw_data = rdata_file_offset;
        sec_rdata.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ;

        COFF_SectionHeader sec_data{};
        std::memcpy(sec_data.name, ".data\0\0\0", 8);
        sec_data.virtual_size = data_vsize;
        sec_data.virtual_address = data_rva;
        sec_data.size_of_raw_data = data_raw_size;
        sec_data.pointer_to_raw_data = data_file_offset;
        sec_data.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE;

        out.clear();
        out.emit_bytes(&dos_hdr, sizeof(dos_hdr));
        out.emit_bytes(s_pe_dos_stub, sizeof(s_pe_dos_stub));
        out.emit_u32(PE_SIGNATURE);
        out.emit_bytes(&file_hdr, sizeof(file_hdr));
        out.emit_bytes(&opt_hdr, sizeof(opt_hdr));
        out.emit_bytes(&sec_text, sizeof(sec_text));
        out.emit_bytes(&sec_rdata, sizeof(sec_rdata));
        out.emit_bytes(&sec_data, sizeof(sec_data));
        out.align_to(file_align, 0);

        out.emit_bytes(text_data.data(), text_data.size());
        out.align_to(file_align, 0);

        out.emit_bytes(rdata_data.data(), rdata_data.size());
        out.align_to(file_align, 0);

        if (!data_data.empty()) {
            out.emit_bytes(data_data.data(), data_data.size());
        }
        if (out.size() < data_file_offset + data_raw_size) {
            out.emit_zeros((data_file_offset + data_raw_size) - out.size());
        }

        return true;
    }

    bool GrassObjWriter::write_pe_executable_file(const CompiledUnit& unit,
                                                 const std::string& filename,
                                                 std::string* out_error,
                                                 const std::string& entry_point_name) {
        BinaryBuffer buf;
        if (!write_pe_executable(unit, buf, out_error, entry_point_name)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write PE executable to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_pe_executable_file(const CompiledUnit& unit,
                                                 const std::vector<WinLibraryImport>& custom_imports,
                                                 const std::string& filename,
                                                 std::string* out_error,
                                                 const std::string& entry_point_name) {
        BinaryBuffer buf;
        if (!write_pe_executable(unit, custom_imports, buf, out_error, entry_point_name)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write PE executable to " + filename;
            return false;
        }
        return true;
    }


} // namespace grass::backend
