#include "GrassNativeDebug.h"

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#include <ucontext.h>
#include <sys/types.h>
#endif

namespace grass::debug {

    static const void* g_active_debug_table = nullptr;

#if defined(_WIN32)
    static PVOID g_veh_handle = nullptr;

    static bool is_valid_read_ptr(const void* ptr, size_t size) {
        if (!ptr) return false;
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        if (addr < 0x10000 || (addr & 7) != 0) return false;
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi)) return false;
        if ((mbi.State != MEM_COMMIT)) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        return true;
    }
#else
    static struct sigaction g_old_sigsegv;
    static struct sigaction g_old_sigfpe;
    static struct sigaction g_old_sigill;
    static struct sigaction g_old_sigbus;
    static bool g_sigactions_installed = false;
#endif

    GrassDebugTableBuilder::GrassDebugTableBuilder() {
        string_pool_.push_back('\0');
        string_map_[""] = 0;
    }

    void GrassDebugTableBuilder::clear() {
        entries_.clear();
        string_pool_.clear();
        string_map_.clear();
        string_pool_.push_back('\0');
        string_map_[""] = 0;
        text_base_rva_ = 0x1000;
        text_size_ = 0;
    }

    void GrassDebugTableBuilder::set_text_bounds(uint64_t text_base_rva, uint64_t text_size) {
        text_base_rva_ = text_base_rva;
        text_size_ = text_size;
    }

    uint32_t GrassDebugTableBuilder::get_or_add_string(const std::string& str) {
        auto it = string_map_.find(str);
        if (it != string_map_.end()) {
            return it->second;
        }
        uint32_t offset = static_cast<uint32_t>(string_pool_.size());
        string_pool_.insert(string_pool_.end(), str.begin(), str.end());
        string_pool_.push_back('\0');
        string_map_[str] = offset;
        return offset;
    }

    void GrassDebugTableBuilder::add_entry(uint32_t code_start, uint32_t code_end,
                                          const std::string& file, const std::string& func,
                                          uint32_t line, uint32_t col) {
        GrassDebugLineEntry entry;
        entry.code_start = code_start;
        entry.code_end = code_end;
        entry.file_offset = get_or_add_string(file);
        entry.func_offset = get_or_add_string(func);
        entry.line = line;
        entry.col = col;
        entries_.push_back(entry);
    }

    std::vector<uint8_t> GrassDebugTableBuilder::serialize() const {
        GrassDebugHeader header;
        header.magic = GRASS_DEBUG_MAGIC;
        header.version = GRASS_DEBUG_VERSION;
        header.entry_count = static_cast<uint32_t>(entries_.size());
        header.strings_size = static_cast<uint32_t>(string_pool_.size());
        header.text_base_rva = text_base_rva_;
        header.text_size = text_size_;

        size_t total_size = sizeof(GrassDebugHeader) +
                            entries_.size() * sizeof(GrassDebugLineEntry) +
                            string_pool_.size();

        std::vector<uint8_t> buffer(total_size);
        uint8_t* ptr = buffer.data();

        std::memcpy(ptr, &header, sizeof(GrassDebugHeader));
        ptr += sizeof(GrassDebugHeader);

        if (!entries_.empty()) {
            size_t entries_bytes = entries_.size() * sizeof(GrassDebugLineEntry);
            std::memcpy(ptr, entries_.data(), entries_bytes);
            ptr += entries_bytes;
        }

        if (!string_pool_.empty()) {
            std::memcpy(ptr, string_pool_.data(), string_pool_.size());
        }

        return buffer;
    }

    bool lookup_debug_location(const void* table_data, size_t table_size,
                               uint64_t rip, uint64_t module_base,
                               GrassDebugLocation& out_loc) {
        out_loc.rip = rip;
        out_loc.found = false;

        if (!table_data || table_size < sizeof(GrassDebugHeader)) {
            return false;
        }

        const auto* header = reinterpret_cast<const GrassDebugHeader*>(table_data);
        if (header->magic != GRASS_DEBUG_MAGIC || header->version != GRASS_DEBUG_VERSION) {
            return false;
        }

        size_t expected_size = sizeof(GrassDebugHeader) +
                               header->entry_count * sizeof(GrassDebugLineEntry) +
                               header->strings_size;
        if (table_size < expected_size) {
            return false;
        }

        const auto* entries = reinterpret_cast<const GrassDebugLineEntry*>(
            reinterpret_cast<const uint8_t*>(table_data) + sizeof(GrassDebugHeader));
        const char* strings = reinterpret_cast<const char*>(
            reinterpret_cast<const uint8_t*>(entries) + header->entry_count * sizeof(GrassDebugLineEntry));

        uint64_t rip_rva = (rip >= module_base) ? (rip - module_base) : rip;
        if (rip_rva < header->text_base_rva || rip_rva >= header->text_base_rva + header->text_size) {
            return false;
        }

        uint32_t text_offset = static_cast<uint32_t>(rip_rva - header->text_base_rva);

        for (uint32_t i = 0; i < header->entry_count; ++i) {
            const auto& e = entries[i];
            if (text_offset >= e.code_start && text_offset < e.code_end) {
                if (e.file_offset < header->strings_size) {
                    out_loc.file = &strings[e.file_offset];
                }
                if (e.func_offset < header->strings_size) {
                    out_loc.function = &strings[e.func_offset];
                }
                out_loc.line = e.line;
                out_loc.col = e.col;
                out_loc.found = true;
                return true;
            }
        }

        return false;
    }

    void set_active_debug_table(const void* debug_table) {
        g_active_debug_table = debug_table;
    }

#if defined(_WIN32)
    static LONG WINAPI vectored_crash_handler(PEXCEPTION_POINTERS pExceptionInfo) {
        DWORD code = pExceptionInfo->ExceptionRecord->ExceptionCode;

        if (code != EXCEPTION_ACCESS_VIOLATION &&
            code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
            code != EXCEPTION_STACK_OVERFLOW &&
            code != EXCEPTION_ILLEGAL_INSTRUCTION &&
            code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        const char* reason = "Unknown Exception";
        if (code == EXCEPTION_ACCESS_VIOLATION) reason = "EXCEPTION_ACCESS_VIOLATION (Segmentation Fault)";
        else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) reason = "EXCEPTION_INT_DIVIDE_BY_ZERO (Divide By Zero)";
        else if (code == EXCEPTION_STACK_OVERFLOW) reason = "EXCEPTION_STACK_OVERFLOW (Stack Overflow)";
        else if (code == EXCEPTION_ILLEGAL_INSTRUCTION) reason = "EXCEPTION_ILLEGAL_INSTRUCTION (Invalid Opcode)";
        else if (code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED) reason = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (Out of Bounds)";

        PCONTEXT ctx = pExceptionInfo->ContextRecord;
        uint64_t fault_rip = ctx->Rip;
        uint64_t fault_rbp = ctx->Rbp;
        uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));

        std::fprintf(stderr, "\n================================================================================\n");
        std::fprintf(stderr, "GRASS NATIVE RUNTIME CRASH DETECTED\n");
        std::fprintf(stderr, "Reason: %s (Code: 0x%08X)\n", reason, static_cast<unsigned int>(code));
        if (code == EXCEPTION_ACCESS_VIOLATION && pExceptionInfo->ExceptionRecord->NumberParameters >= 2) {
            ULONG_PTR access_type = pExceptionInfo->ExceptionRecord->ExceptionInformation[0];
            ULONG_PTR target_addr = pExceptionInfo->ExceptionRecord->ExceptionInformation[1];
            std::fprintf(stderr, "Attempted to %s address: 0x%016llX\n",
                         (access_type == 0 ? "read from" : (access_type == 1 ? "write to" : "execute")),
                         static_cast<unsigned long long>(target_addr));
        }
        std::fprintf(stderr, "Faulting Instruction Pointer (RIP): 0x%016llX\n", static_cast<unsigned long long>(fault_rip));
        std::fprintf(stderr, "================================================================================\n");
        std::fprintf(stderr, "Grass Native Stack Trace:\n");

        uint64_t current_rip = fault_rip;
        uint64_t current_rbp = fault_rbp;

        for (int frame = 0; frame < 64; ++frame) {
            GrassDebugLocation loc;
            bool resolved = false;

            if (g_active_debug_table) {
                resolved = lookup_debug_location(g_active_debug_table, 0x10000000, current_rip, module_base, loc);
            }

            if (resolved && loc.found) {
                std::fprintf(stderr, "  #%-2d 0x%016llX in %s at %s:%u:%u\n",
                             frame, static_cast<unsigned long long>(current_rip),
                             loc.function.empty() ? "<unknown>" : loc.function.c_str(),
                             loc.file.empty() ? "<unknown>" : loc.file.c_str(),
                             loc.line, loc.col);
            } else {
                std::fprintf(stderr, "  #%-2d 0x%016llX in <native code> (module+0x%llX)\n",
                             frame, static_cast<unsigned long long>(current_rip),
                             static_cast<unsigned long long>(current_rip - module_base));
            }

            if (!is_valid_read_ptr(reinterpret_cast<const void*>(current_rbp), 16)) {
                break;
            }

            const uint64_t* rbp_ptr = reinterpret_cast<const uint64_t*>(current_rbp);
            uint64_t next_rbp = rbp_ptr[0];
            uint64_t ret_addr = rbp_ptr[1];

            if (ret_addr == 0 || next_rbp <= current_rbp) {
                break;
            }
            current_rip = ret_addr;
            current_rbp = next_rbp;
        }

        std::fprintf(stderr, "================================================================================\n\n");
        std::fflush(stderr);

        ExitProcess(static_cast<UINT>(code));
        return EXCEPTION_EXECUTE_HANDLER;
    }
#else
    static void posix_signal_handler(int sig, siginfo_t* info, void* ucontext_raw) {
        const char* reason = "Unknown Signal";
        if (sig == SIGSEGV) reason = "SIGSEGV (Segmentation Fault)";
        else if (sig == SIGFPE) reason = "SIGFPE (Arithmetic Exception / Divide By Zero)";
        else if (sig == SIGILL) reason = "SIGILL (Illegal Instruction)";
        else if (sig == SIGBUS) reason = "SIGBUS (Bus Error)";

        ucontext_t* uc = reinterpret_cast<ucontext_t*>(ucontext_raw);
        uint64_t fault_rip = 0;
        uint64_t fault_rbp = 0;

#if defined(__x86_64__)
        fault_rip = static_cast<uint64_t>(uc->uc_mcontext.gregs[REG_RIP]);
        fault_rbp = static_cast<uint64_t>(uc->uc_mcontext.gregs[REG_RBP]);
#endif

        std::fprintf(stderr, "\n================================================================================\n");
        std::fprintf(stderr, "GRASS NATIVE RUNTIME CRASH DETECTED\n");
        std::fprintf(stderr, "Signal: %s (%d)\n", reason, sig);
        if (info && (sig == SIGSEGV || sig == SIGBUS)) {
            std::fprintf(stderr, "Faulting Memory Address: %p\n", info->si_addr);
        }
        std::fprintf(stderr, "Faulting Instruction Pointer (RIP): 0x%016llX\n", static_cast<unsigned long long>(fault_rip));
        std::fprintf(stderr, "================================================================================\n");
        std::fprintf(stderr, "Grass Native Stack Trace:\n");

        uint64_t current_rip = fault_rip;
        uint64_t current_rbp = fault_rbp;

        for (int frame = 0; frame < 64; ++frame) {
            GrassDebugLocation loc;
            bool resolved = false;

            if (g_active_debug_table) {
                resolved = lookup_debug_location(g_active_debug_table, 0x10000000, current_rip, 0, loc);
            }

            if (resolved && loc.found) {
                std::fprintf(stderr, "  #%-2d 0x%016llX in %s at %s:%u:%u\n",
                             frame, static_cast<unsigned long long>(current_rip),
                             loc.function.empty() ? "<unknown>" : loc.function.c_str(),
                             loc.file.empty() ? "<unknown>" : loc.file.c_str(),
                             loc.line, loc.col);
            } else {
                std::fprintf(stderr, "  #%-2d 0x%016llX in <native code>\n",
                             frame, static_cast<unsigned long long>(current_rip));
            }

            if (current_rbp == 0 || (current_rbp & 7) != 0 || current_rbp < 0x1000) {
                break;
            }

            const uint64_t* rbp_ptr = reinterpret_cast<const uint64_t*>(current_rbp);
            uint64_t next_rbp = rbp_ptr[0];
            uint64_t ret_addr = rbp_ptr[1];

            if (ret_addr == 0 || next_rbp <= current_rbp) {
                break;
            }
            current_rip = ret_addr;
            current_rbp = next_rbp;
        }

        std::fprintf(stderr, "================================================================================\n\n");
        std::fflush(stderr);

        _exit(128 + sig);
    }
#endif

    void install_crash_handler(const void* debug_table) {
        if (debug_table) {
            g_active_debug_table = debug_table;
        }

#if defined(_WIN32)
        if (!g_veh_handle) {
            g_veh_handle = AddVectoredExceptionHandler(1, vectored_crash_handler);
        }
#else
        if (!g_sigactions_installed) {
            struct sigaction sa;
            std::memset(&sa, 0, sizeof(sa));
            sa.sa_sigaction = posix_signal_handler;
            sa.sa_flags = SA_SIGINFO | SA_NODEFER;
            sigemptyset(&sa.sa_mask);

            sigaction(SIGSEGV, &sa, &g_old_sigsegv);
            sigaction(SIGFPE, &sa, &g_old_sigfpe);
            sigaction(SIGILL, &sa, &g_old_sigill);
            sigaction(SIGBUS, &sa, &g_old_sigbus);
            g_sigactions_installed = true;
        }
#endif
    }

    void uninstall_crash_handler() {
#if defined(_WIN32)
        if (g_veh_handle) {
            RemoveVectoredExceptionHandler(g_veh_handle);
            g_veh_handle = nullptr;
        }
#else
        if (g_sigactions_installed) {
            sigaction(SIGSEGV, &g_old_sigsegv, nullptr);
            sigaction(SIGFPE, &g_old_sigfpe, nullptr);
            sigaction(SIGILL, &g_old_sigill, nullptr);
            sigaction(SIGBUS, &g_old_sigbus, nullptr);
            g_sigactions_installed = false;
        }
#endif
    }

    void print_stack_trace_from_context(void* native_context, const void* debug_table) {
        if (!native_context) return;
        const void* table_to_use = debug_table ? debug_table : g_active_debug_table;

#if defined(_WIN32)
        PCONTEXT ctx = reinterpret_cast<PCONTEXT>(native_context);
        uint64_t current_rip = ctx->Rip;
        uint64_t current_rbp = ctx->Rbp;
        uintptr_t module_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(NULL));

        std::fprintf(stderr, "Stack Trace:\n");
        for (int frame = 0; frame < 64; ++frame) {
            GrassDebugLocation loc;
            bool ok = false;
            if (table_to_use) {
                ok = lookup_debug_location(table_to_use, 0x10000000, current_rip, module_base, loc);
            }
            if (ok && loc.found) {
                std::fprintf(stderr, "  #%-2d 0x%016llX in %s at %s:%u:%u\n",
                             frame, static_cast<unsigned long long>(current_rip),
                             loc.function.c_str(), loc.file.c_str(), loc.line, loc.col);
            } else {
                std::fprintf(stderr, "  #%-2d 0x%016llX in <native code> (module+0x%llX)\n",
                             frame, static_cast<unsigned long long>(current_rip),
                             static_cast<unsigned long long>(current_rip - module_base));
            }
            if (!is_valid_read_ptr(reinterpret_cast<const void*>(current_rbp), 16)) break;
            const uint64_t* rbp_ptr = reinterpret_cast<const uint64_t*>(current_rbp);
            uint64_t next_rbp = rbp_ptr[0];
            uint64_t ret_addr = rbp_ptr[1];
            if (ret_addr == 0 || next_rbp <= current_rbp) break;
            current_rip = ret_addr;
            current_rbp = next_rbp;
        }
#endif
    }

    std::string generate_runtime_crash_handler_c() {
        return std::string(R"GRASS_DEBUG_SRC(
/* ========================================================================= */
/* Grass Native Crash Handler and Stack Trace Engine                         */
/* ========================================================================= */

#pragma pack(push, 1)
typedef struct {
    uint32_t code_start;
    uint32_t code_end;
    uint32_t file_offset;
    uint32_t func_offset;
    uint32_t line;
    uint32_t col;
} GrassDebugLineEntryC;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_count;
    uint32_t strings_size;
    uint64_t text_base_rva;
    uint64_t text_size;
} GrassDebugHeaderC;
#pragma pack(pop)

/* Weak or exported reference to __grass_debug_table */
#if defined(_MSC_VER)
__declspec(selectany) const uint8_t __grass_debug_table[32] = { 0 };
#else
__attribute__((weak)) const uint8_t __grass_debug_table[32] = { 0 };
#endif

static const void* g_runtime_debug_table = (const void*)__grass_debug_table;

void grass_set_debug_table(const void* tbl) {
    g_runtime_debug_table = tbl;
}

static int grass_resolve_rip(const void* table_ptr, uint64_t rip, uintptr_t module_base,
                             const char** out_file, const char** out_func,
                             uint32_t* out_line, uint32_t* out_col) {
    if (!table_ptr) return 0;
    const GrassDebugHeaderC* hdr = (const GrassDebugHeaderC*)table_ptr;
    if (hdr->magic != 0x47524442 || hdr->version != 1) return 0;

    const GrassDebugLineEntryC* entries = (const GrassDebugLineEntryC*)((const uint8_t*)table_ptr + sizeof(GrassDebugHeaderC));
    const char* str_pool = (const char*)((const uint8_t*)entries + hdr->entry_count * sizeof(GrassDebugLineEntryC));

    uint64_t rip_rva = (rip >= (uint64_t)module_base) ? (rip - (uint64_t)module_base) : rip;
    if (rip_rva < hdr->text_base_rva || rip_rva >= hdr->text_base_rva + hdr->text_size) {
        return 0;
    }
    uint32_t offset = (uint32_t)(rip_rva - hdr->text_base_rva);

    for (uint32_t i = 0; i < hdr->entry_count; ++i) {
        if (offset >= entries[i].code_start && offset < entries[i].code_end) {
            *out_file = (entries[i].file_offset < hdr->strings_size) ? &str_pool[entries[i].file_offset] : "<unknown>";
            *out_func = (entries[i].func_offset < hdr->strings_size) ? &str_pool[entries[i].func_offset] : "<unknown>";
            *out_line = entries[i].line;
            *out_col = entries[i].col;
            return 1;
        }
    }
    return 0;
}

#if defined(_WIN32)
static int grass_c_is_valid_ptr(const void* ptr) {
    if (!ptr) return 0;
    uintptr_t addr = (uintptr_t)ptr;
    if (addr < 0x10000 || (addr & 7) != 0) return 0;
    MEMORY_BASIC_INFORMATION mbi;
    if (VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi)) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return 1;
}

static LONG WINAPI grass_veh_handler(PEXCEPTION_POINTERS pExc) {
    DWORD code = pExc->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION &&
        code != EXCEPTION_INT_DIVIDE_BY_ZERO &&
        code != EXCEPTION_STACK_OVERFLOW &&
        code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_ARRAY_BOUNDS_EXCEEDED) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    const char* reason = "Fatal Crash";
    if (code == EXCEPTION_ACCESS_VIOLATION) reason = "EXCEPTION_ACCESS_VIOLATION (Segmentation Fault)";
    else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) reason = "EXCEPTION_INT_DIVIDE_BY_ZERO (Divide By Zero)";
    else if (code == EXCEPTION_STACK_OVERFLOW) reason = "EXCEPTION_STACK_OVERFLOW (Stack Overflow)";
    else if (code == EXCEPTION_ILLEGAL_INSTRUCTION) reason = "EXCEPTION_ILLEGAL_INSTRUCTION (Illegal Instruction)";
    else if (code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED) reason = "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (Out of Bounds)";

    PCONTEXT ctx = pExc->ContextRecord;
    uint64_t rip = ctx->Rip;
    uint64_t rbp = ctx->Rbp;
    uintptr_t mod_base = (uintptr_t)GetModuleHandleA(NULL);

    fprintf(stderr, "\n================================================================================\n");
    fprintf(stderr, "GRASS NATIVE RUNTIME CRASH DETECTED\n");
    fprintf(stderr, "Reason: %s (Code: 0x%08X)\n", reason, (unsigned int)code);
    if (code == EXCEPTION_ACCESS_VIOLATION && pExc->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR atype = pExc->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR taddr = pExc->ExceptionRecord->ExceptionInformation[1];
        fprintf(stderr, "Attempted to %s address: 0x%016llX\n",
                (atype == 0 ? "read from" : (atype == 1 ? "write to" : "execute")),
                (unsigned long long)taddr);
    }
    fprintf(stderr, "Faulting Instruction Pointer (RIP): 0x%016llX\n", (unsigned long long)rip);
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "Grass Native Stack Trace:\n");

    for (int frame = 0; frame < 64; ++frame) {
        const char* f_name = NULL;
        const char* fn_name = NULL;
        uint32_t line = 0, col = 0;
        int ok = grass_resolve_rip(g_runtime_debug_table, rip, mod_base, &f_name, &fn_name, &line, &col);

        if (ok) {
            fprintf(stderr, "  #%-2d 0x%016llX in %s at %s:%u:%u\n",
                    frame, (unsigned long long)rip, fn_name, f_name, line, col);
        } else {
            fprintf(stderr, "  #%-2d 0x%016llX in <native code> (module+0x%llX)\n",
                    frame, (unsigned long long)rip, (unsigned long long)(rip - (uint64_t)mod_base));
        }

        if (!grass_c_is_valid_ptr((const void*)rbp)) break;

        const uint64_t* rbp_ptr = (const uint64_t*)rbp;
        uint64_t next_rbp = rbp_ptr[0];
        uint64_t ret_addr = rbp_ptr[1];
        if (ret_addr == 0 || next_rbp <= rbp) break;
        rip = ret_addr;
        rbp = next_rbp;
    }
    fprintf(stderr, "================================================================================\n\n");
    fflush(stderr);

    ExitProcess((UINT)code);
    return EXCEPTION_EXECUTE_HANDLER;
}

void grass_install_runtime_crash_handler(void) {
    AddVectoredExceptionHandler(1, grass_veh_handler);
}
#else
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

static void grass_posix_sigaction(int sig, siginfo_t* info, void* uctx) {
    const char* reason = "Fatal Signal";
    if (sig == SIGSEGV) reason = "SIGSEGV (Segmentation Fault)";
    else if (sig == SIGFPE) reason = "SIGFPE (Divide By Zero)";
    else if (sig == SIGILL) reason = "SIGILL (Illegal Instruction)";
    else if (sig == SIGBUS) reason = "SIGBUS (Bus Error)";

    ucontext_t* uc = (ucontext_t*)uctx;
    uint64_t rip = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
    uint64_t rbp = (uint64_t)uc->uc_mcontext.gregs[REG_RBP];

    fprintf(stderr, "\n================================================================================\n");
    fprintf(stderr, "GRASS NATIVE RUNTIME CRASH DETECTED\n");
    fprintf(stderr, "Signal: %s (%d)\n", reason, sig);
    if (info && (sig == SIGSEGV || sig == SIGBUS)) {
        fprintf(stderr, "Faulting Memory Address: %p\n", info->si_addr);
    }
    fprintf(stderr, "Faulting Instruction Pointer (RIP): 0x%016llX\n", (unsigned long long)rip);
    fprintf(stderr, "================================================================================\n");
    fprintf(stderr, "Grass Native Stack Trace:\n");

    for (int frame = 0; frame < 64; ++frame) {
        const char* f_name = NULL;
        const char* fn_name = NULL;
        uint32_t line = 0, col = 0;
        int ok = grass_resolve_rip(g_runtime_debug_table, rip, 0, &f_name, &fn_name, &line, &col);

        if (ok) {
            fprintf(stderr, "  #%-2d 0x%016llX in %s at %s:%u:%u\n",
                    frame, (unsigned long long)rip, fn_name, f_name, line, col);
        } else {
            fprintf(stderr, "  #%-2d 0x%016llX in <native code>\n",
                    frame, (unsigned long long)rip);
        }

        if (rbp == 0 || (rbp & 7) != 0 || rbp < 0x1000) break;

        const uint64_t* rbp_ptr = (const uint64_t*)rbp;
        uint64_t next_rbp = rbp_ptr[0];
        uint64_t ret_addr = rbp_ptr[1];
        if (ret_addr == 0 || next_rbp <= rbp) break;
        rip = ret_addr;
        rbp = next_rbp;
    }
    fprintf(stderr, "================================================================================\n\n");
    fflush(stderr);

    _exit(128 + sig);
}

void grass_install_runtime_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = grass_posix_sigaction;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}
#endif
)GRASS_DEBUG_SRC");
    }

} // namespace grass::debug
