// =============================================================================
//  GrassGreen 4.1 — Встроенный ассемблер ядра (!asm!)
//  Реализация ассемблера, регистров процессора и инструкций ядра
//  100% C++20 без внешних зависимостей (NASM / GAS)
// =============================================================================
#include "GrassASM.h"
#include "GrassMMIO.h"
#include "GrassHAL.h"
#include "GrassJIT.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
#else
#include <unistd.h>
#include <x86intrin.h>
#endif

namespace GrassASM {

    static CPUState g_cpu;
    static std::string g_last_hex = "";

    void initialize() {
        g_cpu.rax = 0;
        g_cpu.rbx = 0;
        g_cpu.rcx = 0;
        g_cpu.rdx = 0;
        g_cpu.rsi = 0;
        g_cpu.rdi = 0;
        g_cpu.rbp = 0;
        g_cpu.rsp = 0x7FFFFFF0;
        g_cpu.rflags = 0x202; // IF = 1, bit 1 = 1
        g_cpu.cr0 = 0x80050033;
        g_cpu.cr2 = 0;
        g_cpu.cr3 = 0x1000;
        g_cpu.cr4 = 0x6F0;
        g_cpu.interrupts_enabled = true;
        g_cpu.halted = false;
    }

    CPUState& get_cpu_state() {
        return g_cpu;
    }

    std::string get_last_machine_hex() {
        return g_last_hex;
    }

    // ── Управление прерываниями ──
    void cli() {
        g_cpu.interrupts_enabled = false;
        g_cpu.rflags &= ~0x200ULL;
        if (GrassHAL::is_freestanding()) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
            __asm__ volatile("cli" ::: "memory");
#endif
        }
    }

    void sti() {
        g_cpu.interrupts_enabled = true;
        g_cpu.rflags |= 0x200ULL;
        if (GrassHAL::is_freestanding()) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
            __asm__ volatile("sti" ::: "memory");
#endif
        }
    }

    void hlt() {
        g_cpu.halted = true;
        if (GrassHAL::is_freestanding()) {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
            __asm__ volatile("hlt");
#endif
        } else {
#if defined(_WIN32)
            Sleep(1);
#else
            usleep(1000);
#endif
        }
    }

    bool are_interrupts_enabled() {
        return g_cpu.interrupts_enabled;
    }

    // ── Порты ввода/вывода ──
    uint8_t in8(uint16_t port) {
        uint8_t v = GrassMMIO::in8(port);
        g_cpu.rax = (g_cpu.rax & ~0xFFULL) | v;
        return v;
    }

    uint16_t in16(uint16_t port) {
        uint16_t v = GrassMMIO::in16(port);
        g_cpu.rax = (g_cpu.rax & ~0xFFFFULL) | v;
        return v;
    }

    uint32_t in32(uint16_t port) {
        uint32_t v = GrassMMIO::in32(port);
        g_cpu.rax = v;
        return v;
    }

    void out8(uint16_t port, uint8_t value) {
        GrassMMIO::out8(port, value);
    }

    void out16(uint16_t port, uint16_t value) {
        GrassMMIO::out16(port, value);
    }

    void out32(uint16_t port, uint32_t value) {
        GrassMMIO::out32(port, value);
    }

    // ── Системные регистры процессора ──
    uint64_t read_cr0() {
        return g_cpu.cr0;
    }

    void write_cr0(uint64_t val) {
        g_cpu.cr0 = val;
    }

    uint64_t read_cr2() {
        return g_cpu.cr2;
    }

    void write_cr2(uint64_t val) {
        g_cpu.cr2 = val;
    }

    uint64_t read_cr3() {
        return g_cpu.cr3;
    }

    void write_cr3(uint64_t val) {
        g_cpu.cr3 = val;
        if (GrassHAL::is_freestanding()) {
#if defined(__GNUC__) && defined(__x86_64__)
            __asm__ volatile("mov %0, %%cr3" :: "r"(val) : "memory");
#endif
        }
    }

    uint64_t read_cr4() {
        return g_cpu.cr4;
    }

    void write_cr4(uint64_t val) {
        g_cpu.cr4 = val;
    }

    uint64_t read_rflags() {
        return g_cpu.rflags;
    }

    void write_rflags(uint64_t val) {
        g_cpu.rflags = val;
        g_cpu.interrupts_enabled = (val & 0x200ULL) != 0;
    }

    void invlpg(uint64_t addr) {
        if (GrassHAL::is_freestanding()) {
#if defined(__GNUC__) && defined(__x86_64__)
            __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
#endif
        }
    }

    uint64_t rdtsc() {
        uint64_t t = GrassHAL::read_tsc();
        g_cpu.rdx = (t >> 32);
        g_cpu.rax = (t & 0xFFFFFFFFULL);
        return t;
    }

    void pause() {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        __asm__ volatile("pause");
#elif defined(_MSC_VER)
        _mm_pause();
#endif
    }

    void nop() {
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        __asm__ volatile("nop");
#endif
    }

    uint32_t cpuid_eax(uint32_t leaf) {
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
#if defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(leaf), "c"(0));
#elif defined(_MSC_VER)
        int info[4]; __cpuid(info, (int)leaf);
        eax = info[0]; ebx = info[1]; ecx = info[2]; edx = info[3];
#endif
        g_cpu.rax = eax; g_cpu.rbx = ebx; g_cpu.rcx = ecx; g_cpu.rdx = edx;
        return eax;
    }

    uint32_t cpuid_ebx(uint32_t leaf) { cpuid_eax(leaf); return (uint32_t)g_cpu.rbx; }
    uint32_t cpuid_ecx(uint32_t leaf) { cpuid_eax(leaf); return (uint32_t)g_cpu.rcx; }
    uint32_t cpuid_edx(uint32_t leaf) { cpuid_eax(leaf); return (uint32_t)g_cpu.rdx; }

    // =========================================================================
    //  ВСТРОЕННЫЙ x86_64 АССЕМБЛЕР (ZERO DEPENDENCIES)
    // =========================================================================

    static std::string trim(const std::string& s) {
        size_t start = 0;
        while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) start++;
        size_t end = s.size();
        while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) end--;
        return s.substr(start, end - start);
    }

    static std::string to_lower(std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    static bool parse_number(const std::string& s, uint64_t& out_val) {
        if (s.empty()) return false;
        std::string str = to_lower(s);
        if (str.rfind("0x", 0) == 0) {
            try { out_val = std::stoull(str.substr(2), nullptr, 16); return true; } catch (...) { return false; }
        }
        if (str.back() == 'h') {
            try { out_val = std::stoull(str.substr(0, str.size() - 1), nullptr, 16); return true; } catch (...) { return false; }
        }
        if (str.rfind("0b", 0) == 0) {
            try { out_val = std::stoull(str.substr(2), nullptr, 2); return true; } catch (...) { return false; }
        }
        try { out_val = std::stoull(str, nullptr, 10); return true; } catch (...) { return false; }
    }

    enum RegKind { ASM_REG_NONE, ASM_REG_64, ASM_REG_32, ASM_REG_16, ASM_REG_8, ASM_REG_CR };

    struct RegInfo {
        RegKind kind;
        int id; // 0=rax/eax/ax/al, 1=rcx, 2=rdx, 3=rbx, 4=rsp, 5=rbp, 6=rsi, 7=rdi, 8..15=r8..r15
    };

    static RegInfo parse_reg(const std::string& raw) {
        std::string r = to_lower(trim(raw));
        if (r == "rax") return { ASM_REG_64, 0 };
        if (r == "rcx") return { ASM_REG_64, 1 };
        if (r == "rdx") return { ASM_REG_64, 2 };
        if (r == "rbx") return { ASM_REG_64, 3 };
        if (r == "rsp") return { ASM_REG_64, 4 };
        if (r == "rbp") return { ASM_REG_64, 5 };
        if (r == "rsi") return { ASM_REG_64, 6 };
        if (r == "rdi") return { ASM_REG_64, 7 };
        if (r == "r8")  return { ASM_REG_64, 8 };
        if (r == "r9")  return { ASM_REG_64, 9 };
        if (r == "r10") return { ASM_REG_64, 10 };
        if (r == "r11") return { ASM_REG_64, 11 };
        if (r == "r12") return { ASM_REG_64, 12 };
        if (r == "r13") return { ASM_REG_64, 13 };
        if (r == "r14") return { ASM_REG_64, 14 };
        if (r == "r15") return { ASM_REG_64, 15 };

        if (r == "eax") return { ASM_REG_32, 0 };
        if (r == "ecx") return { ASM_REG_32, 1 };
        if (r == "edx") return { ASM_REG_32, 2 };
        if (r == "ebx") return { ASM_REG_32, 3 };
        if (r == "esp") return { ASM_REG_32, 4 };
        if (r == "ebp") return { ASM_REG_32, 5 };
        if (r == "esi") return { ASM_REG_32, 6 };
        if (r == "edi") return { ASM_REG_32, 7 };
        if (r == "r8d") return { ASM_REG_32, 8 };
        if (r == "r9d") return { ASM_REG_32, 9 };
        if (r == "r10d")return { ASM_REG_32, 10 };
        if (r == "r11d")return { ASM_REG_32, 11 };
        if (r == "r12d")return { ASM_REG_32, 12 };
        if (r == "r13d")return { ASM_REG_32, 13 };
        if (r == "r14d")return { ASM_REG_32, 14 };
        if (r == "r15d")return { ASM_REG_32, 15 };

        if (r == "ax") return { ASM_REG_16, 0 };
        if (r == "cx") return { ASM_REG_16, 1 };
        if (r == "dx") return { ASM_REG_16, 2 };
        if (r == "bx") return { ASM_REG_16, 3 };
        if (r == "sp") return { ASM_REG_16, 4 };
        if (r == "bp") return { ASM_REG_16, 5 };
        if (r == "si") return { ASM_REG_16, 6 };
        if (r == "di") return { ASM_REG_16, 7 };

        if (r == "al") return { ASM_REG_8, 0 };
        if (r == "cl") return { ASM_REG_8, 1 };
        if (r == "dl") return { ASM_REG_8, 2 };
        if (r == "bl") return { ASM_REG_8, 3 };

        if (r == "cr0") return { ASM_REG_CR, 0 };
        if (r == "cr2") return { ASM_REG_CR, 2 };
        if (r == "cr3") return { ASM_REG_CR, 3 };
        if (r == "cr4") return { ASM_REG_CR, 4 };

        return { ASM_REG_NONE, -1 };
    }

    static void emit_u32(std::vector<uint8_t>& b, uint32_t v) {
        b.push_back((uint8_t)(v & 0xFF));
        b.push_back((uint8_t)((v >> 8) & 0xFF));
        b.push_back((uint8_t)((v >> 16) & 0xFF));
        b.push_back((uint8_t)((v >> 24) & 0xFF));
    }

    static void emit_u64(std::vector<uint8_t>& b, uint64_t v) {
        for (int i = 0; i < 8; ++i) b.push_back((uint8_t)((v >> (i * 8)) & 0xFF));
    }

    bool assemble(const std::string& asm_code, std::vector<uint8_t>& out_bytes, std::string& out_error) {
        out_bytes.clear();
        out_error.clear();

        // 1. Разбиваем на строки/инструкции (по переводам строк и точкам с запятой)
        std::vector<std::string> lines;
        std::string cur;
        for (char c : asm_code) {
            if (c == '\n' || c == ';') {
                if (!cur.empty()) { lines.push_back(cur); cur.clear(); }
            } else {
                cur.push_back(c);
            }
        }
        if (!cur.empty()) lines.push_back(cur);

        for (auto& raw_line : lines) {
            std::string line = trim(raw_line);
            // Удаляем комментарии // или #
            size_t cpos = line.find("//");
            if (cpos != std::string::npos) line = line.substr(0, cpos);
            cpos = line.find('#');
            if (cpos != std::string::npos) line = line.substr(0, cpos);
            line = trim(line);
            if (line.empty()) continue;

            // Метки: label:
            if (line.back() == ':') continue;

            // Выделяем мнемонику и операнды
            size_t sp = line.find_first_of(" \t");
            std::string op = to_lower(sp == std::string::npos ? line : line.substr(0, sp));
            std::string args_str = sp == std::string::npos ? "" : trim(line.substr(sp + 1));

            // Разбиваем операнды по запятой
            std::vector<std::string> args;
            if (!args_str.empty()) {
                std::stringstream ss(args_str);
                std::string item;
                while (std::getline(ss, item, ',')) {
                    item = trim(item);
                    if (!item.empty()) args.push_back(item);
                }
            }

            // ── Базовые без-операндные инструкции ──
            if (op == "nop") {
                out_bytes.push_back(0x90);
            } else if (op == "cli") {
                out_bytes.push_back(0xFA);
            } else if (op == "sti") {
                out_bytes.push_back(0xFB);
            } else if (op == "hlt") {
                out_bytes.push_back(0xF4);
            } else if (op == "pause") {
                out_bytes.push_back(0xF3); out_bytes.push_back(0x90);
            } else if (op == "cld") {
                out_bytes.push_back(0xFC);
            } else if (op == "std") {
                out_bytes.push_back(0xFD);
            } else if (op == "ret") {
                out_bytes.push_back(0xC3);
            } else if (op == "syscall") {
                out_bytes.push_back(0x0F); out_bytes.push_back(0x05);
            } else if (op == "sysret") {
                out_bytes.push_back(0x0F); out_bytes.push_back(0x07);
            } else if (op == "rdtsc") {
                out_bytes.push_back(0x0F); out_bytes.push_back(0x31);
            } else if (op == "cpuid") {
                out_bytes.push_back(0x0F); out_bytes.push_back(0xA2);
            } else if (op == "wbinvd") {
                out_bytes.push_back(0x0F); out_bytes.push_back(0x09);
            }

            // ── db: сырые байты (db 0x90, 0xFA, 0xFB) ──
            else if (op == "db" || op == ".byte") {
                for (const auto& a : args) {
                    uint64_t val = 0;
                    if (parse_number(a, val)) {
                        out_bytes.push_back((uint8_t)(val & 0xFF));
                    }
                }
            }

            // ── Прерывания: int imm8 ──
            else if (op == "int" && args.size() >= 1) {
                uint64_t iv = 0;
                parse_number(args[0], iv);
                if (iv == 3) {
                    out_bytes.push_back(0xCC); // int3
                } else {
                    out_bytes.push_back(0xCD);
                    out_bytes.push_back((uint8_t)(iv & 0xFF));
                }
            }

            // ── Порты: in al/ax/eax, dx / imm ──
            else if (op == "in" && args.size() >= 2) {
                RegInfo rdst = parse_reg(args[0]);
                RegInfo rsrc = parse_reg(args[1]);
                uint64_t imm = 0;
                bool is_imm = parse_number(args[1], imm);

                if (rsrc.kind == ASM_REG_16 && rsrc.id == 2) { // dx
                    if (rdst.kind == ASM_REG_8 && rdst.id == 0) out_bytes.push_back(0xEC);        // in al, dx
                    else if (rdst.kind == ASM_REG_16 && rdst.id == 0) { out_bytes.push_back(0x66); out_bytes.push_back(0xED); } // in ax, dx
                    else if (rdst.kind == ASM_REG_32 && rdst.id == 0) out_bytes.push_back(0xED); // in eax, dx
                } else if (is_imm && rdst.kind == ASM_REG_8 && rdst.id == 0) {
                    out_bytes.push_back(0xE4); out_bytes.push_back((uint8_t)(imm & 0xFF)); // in al, imm8
                }
            }

            // ── Порты: out dx/imm, al/ax/eax ──
            else if (op == "out" && args.size() >= 2) {
                RegInfo rdst = parse_reg(args[0]);
                RegInfo rsrc = parse_reg(args[1]);
                uint64_t imm = 0;
                bool is_imm = parse_number(args[0], imm);

                if (rdst.kind == ASM_REG_16 && rdst.id == 2) { // dx
                    if (rsrc.kind == ASM_REG_8 && rsrc.id == 0) out_bytes.push_back(0xEE);        // out dx, al
                    else if (rsrc.kind == ASM_REG_16 && rsrc.id == 0) { out_bytes.push_back(0x66); out_bytes.push_back(0xEF); } // out dx, ax
                    else if (rsrc.kind == ASM_REG_32 && rsrc.id == 0) out_bytes.push_back(0xEF); // out dx, eax
                } else if (is_imm && rsrc.kind == ASM_REG_8 && rsrc.id == 0) {
                    out_bytes.push_back(0xE6); out_bytes.push_back((uint8_t)(imm & 0xFF)); // out imm8, al
                }
            }

            // ── push reg / pop reg ──
            else if (op == "push" && args.size() >= 1) {
                RegInfo r = parse_reg(args[0]);
                if (r.kind == ASM_REG_64) {
                    if (r.id >= 8) { out_bytes.push_back(0x41); out_bytes.push_back((uint8_t)(0x50 + (r.id - 8))); }
                    else { out_bytes.push_back((uint8_t)(0x50 + r.id)); }
                }
            }
            else if (op == "pop" && args.size() >= 1) {
                RegInfo r = parse_reg(args[0]);
                if (r.kind == ASM_REG_64) {
                    if (r.id >= 8) { out_bytes.push_back(0x41); out_bytes.push_back((uint8_t)(0x58 + (r.id - 8))); }
                    else { out_bytes.push_back((uint8_t)(0x58 + r.id)); }
                }
            }

            // ── invlpg [reg] ──
            else if (op == "invlpg" && args.size() >= 1) {
                std::string target = args[0];
                if (target.front() == '[') target = target.substr(1);
                if (target.back() == ']') target.pop_back();
                RegInfo r = parse_reg(target);
                int rid = (r.id >= 0) ? r.id : 0;
                out_bytes.push_back(0x0F); out_bytes.push_back(0x01);
                out_bytes.push_back((uint8_t)(0x38 | (rid & 7)));
            }

            // ── mov dst, src ──
            else if (op == "mov" && args.size() >= 2) {
                RegInfo rdst = parse_reg(args[0]);
                RegInfo rsrc = parse_reg(args[1]);
                uint64_t imm = 0;
                bool is_imm = parse_number(args[1], imm);

                // 1. mov reg, crX (mov eax, cr0 / mov rax, cr3)
                if ((rdst.kind == ASM_REG_64 || rdst.kind == ASM_REG_32) && rsrc.kind == ASM_REG_CR) {
                    out_bytes.push_back(0x0F);
                    out_bytes.push_back(0x20);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                }
                // 2. mov crX, reg (mov cr0, eax / mov cr3, rax)
                else if (rdst.kind == ASM_REG_CR && (rsrc.kind == ASM_REG_64 || rsrc.kind == ASM_REG_32)) {
                    out_bytes.push_back(0x0F);
                    out_bytes.push_back(0x22);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rdst.id & 7) << 3) | (rsrc.id & 7)));
                }
                // 3. mov reg, imm
                else if (is_imm) {
                    if (rdst.kind == ASM_REG_64) {
                        out_bytes.push_back((uint8_t)(rdst.id >= 8 ? 0x49 : 0x48));
                        out_bytes.push_back((uint8_t)(0xB8 + (rdst.id & 7)));
                        emit_u64(out_bytes, imm);
                    } else if (rdst.kind == ASM_REG_32) {
                        if (rdst.id >= 8) out_bytes.push_back(0x41);
                        out_bytes.push_back((uint8_t)(0xB8 + (rdst.id & 7)));
                        emit_u32(out_bytes, (uint32_t)imm);
                    }
                }
                // 4. mov reg, reg
                else if (rdst.kind == ASM_REG_64 && rsrc.kind == ASM_REG_64) {
                    uint8_t rex = 0x48;
                    if (rsrc.id >= 8) rex |= 0x04;
                    if (rdst.id >= 8) rex |= 0x01;
                    out_bytes.push_back(rex);
                    out_bytes.push_back(0x89);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                } else if (rdst.kind == ASM_REG_32 && rsrc.kind == ASM_REG_32) {
                    uint8_t rex = 0x40;
                    if (rsrc.id >= 8) rex |= 0x04;
                    if (rdst.id >= 8) rex |= 0x01;
                    if (rex != 0x40) out_bytes.push_back(rex);
                    out_bytes.push_back(0x89);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                }
            }

            // ── xor reg, reg ──
            else if (op == "xor" && args.size() >= 2) {
                RegInfo rdst = parse_reg(args[0]);
                RegInfo rsrc = parse_reg(args[1]);
                if (rdst.kind == ASM_REG_64 && rsrc.kind == ASM_REG_64) {
                    out_bytes.push_back(0x48); out_bytes.push_back(0x31);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                } else if (rdst.kind == ASM_REG_32 && rsrc.kind == ASM_REG_32) {
                    out_bytes.push_back(0x31);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                }
            }

            // ── add / sub / or / and reg, imm / reg ──
            else if ((op == "add" || op == "sub" || op == "or" || op == "and") && args.size() >= 2) {
                RegInfo rdst = parse_reg(args[0]);
                RegInfo rsrc = parse_reg(args[1]);
                uint64_t imm = 0;
                bool is_imm = parse_number(args[1], imm);

                uint8_t sub_op = 0; // add=0, or=1, and=4, sub=5
                if (op == "or") sub_op = 1;
                else if (op == "and") sub_op = 4;
                else if (op == "sub") sub_op = 5;

                if (is_imm) {
                    if (rdst.kind == ASM_REG_32) {
                        out_bytes.push_back(0x81);
                        out_bytes.push_back((uint8_t)(0xC0 | (sub_op << 3) | (rdst.id & 7)));
                        emit_u32(out_bytes, (uint32_t)imm);
                    }
                } else if (rdst.kind == ASM_REG_32 && rsrc.kind == ASM_REG_32) {
                    uint8_t base_byte = 0x01; // add
                    if (op == "or") base_byte = 0x09;
                    else if (op == "and") base_byte = 0x21;
                    else if (op == "sub") base_byte = 0x29;
                    out_bytes.push_back(base_byte);
                    out_bytes.push_back((uint8_t)(0xC0 | ((rsrc.id & 7) << 3) | (rdst.id & 7)));
                }
            }

            // ── inc / dec reg ──
            else if ((op == "inc" || op == "dec") && args.size() >= 1) {
                RegInfo r = parse_reg(args[0]);
                uint8_t sub_op = (op == "dec") ? 1 : 0;
                if (r.kind == ASM_REG_32) {
                    out_bytes.push_back(0xFF);
                    out_bytes.push_back((uint8_t)(0xC0 | (sub_op << 3) | (r.id & 7)));
                }
            }

            else {
                // Если инструкция неизвестна, логируем предупреждение
                out_error = "Неизвестная или неподдерживаемая ассемблерная инструкция: " + line;
            }
        }

        // Обновляем шестнадцатеричное представление последнего машинного кода
        std::stringstream ss;
        for (size_t i = 0; i < out_bytes.size(); ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)out_bytes[i] << " ";
        }
        g_last_hex = ss.str();
        return true;
    }

    // ── Выполнение ассемблерного кода ──
    GrassValue run(const std::string& asm_code) {
        std::vector<uint8_t> bytes;
        std::string err;
        if (!assemble(asm_code, bytes, err)) {
            std::cerr << "[GrassASM Error]: " << err << "\n";
            return GrassValue::Num(0.0);
        }
        return run_bytes(bytes);
    }

    GrassValue run_hex(const std::string& hex_str) {
        std::vector<uint8_t> bytes;
        std::stringstream ss(hex_str);
        std::string item;
        while (ss >> item) {
            uint64_t v = 0;
            if (parse_number("0x" + item, v)) bytes.push_back((uint8_t)v);
        }
        return run_bytes(bytes);
    }

    GrassValue run_bytes(const std::vector<uint8_t>& bytes) {
        if (bytes.empty()) return GrassValue::Num(0.0);

        // В Freestanding / Bare-Metal режиме: выполняем машинный код аппаратно
        if (GrassHAL::is_freestanding()) {
            void* exec_mem = GrassHAL::alloc_executable_memory(bytes.size() + 1);
            if (exec_mem) {
                std::memcpy(exec_mem, bytes.data(), bytes.size());
                // Гарантируем возврат ret
                if (bytes.back() != 0xC3) {
                    ((uint8_t*)exec_mem)[bytes.size()] = 0xC3;
                }
                typedef uint64_t (*KernelFn)();
                KernelFn fn = (KernelFn)exec_mem;
                uint64_t res = fn();
                g_cpu.rax = res;
                return GrassValue::Num((double)res);
            }
        }

        // В режиме ОС (Win32/Linux Ring 3):
        // Чтобы защищенные инструкции ядра (CLI, STI, HLT, MOV CR0) не привели к #GP(0) от хостовой ОС,
        // эмулируем системные регистры и состояние CPU
        for (size_t i = 0; i < bytes.size(); ++i) {
            uint8_t b = bytes[i];
            if (b == 0xFA) { cli(); }
            else if (b == 0xFB) { sti(); }
            else if (b == 0xF4) { hlt(); }
            else if (b == 0x90) { nop(); }
            else if (b == 0xF3 && i + 1 < bytes.size() && bytes[i + 1] == 0x90) { pause(); i++; }
            else if (b == 0x0F && i + 1 < bytes.size() && bytes[i + 1] == 0x31) { rdtsc(); i++; }
            else if (b == 0xEC) { in8((uint16_t)g_cpu.rdx); }
            else if (b == 0xEE) { out8((uint16_t)g_cpu.rdx, (uint8_t)g_cpu.rax); }
            else if (b == 0xED) { in32((uint16_t)g_cpu.rdx); }
            else if (b == 0xEF) { out32((uint16_t)g_cpu.rdx, (uint32_t)g_cpu.rax); }
            else if (b == 0x0F && i + 2 < bytes.size() && bytes[i + 1] == 0x20) {
                // mov reg, crX
                int cr_idx = (bytes[i + 2] >> 3) & 7;
                uint64_t val = (cr_idx == 0) ? g_cpu.cr0 : (cr_idx == 2 ? g_cpu.cr2 : (cr_idx == 3 ? g_cpu.cr3 : g_cpu.cr4));
                g_cpu.rax = val;
                i += 2;
            }
            else if (b == 0x0F && i + 2 < bytes.size() && bytes[i + 1] == 0x22) {
                // mov crX, reg
                int cr_idx = (bytes[i + 2] >> 3) & 7;
                uint64_t val = g_cpu.rax;
                if (cr_idx == 0) g_cpu.cr0 = val;
                else if (cr_idx == 2) g_cpu.cr2 = val;
                else if (cr_idx == 3) g_cpu.cr3 = val;
                else if (cr_idx == 4) g_cpu.cr4 = val;
                i += 2;
            }
            else if (b == 0x48 && i + 9 < bytes.size() && (bytes[i + 1] & 0xF8) == 0xB8) {
                // mov reg64, imm64
                uint64_t imm = 0;
                std::memcpy(&imm, &bytes[i + 2], 8);
                int rid = bytes[i + 1] - 0xB8;
                if (rid == 0) g_cpu.rax = imm;
                else if (rid == 1) g_cpu.rcx = imm;
                else if (rid == 2) g_cpu.rdx = imm;
                else if (rid == 3) g_cpu.rbx = imm;
                i += 9;
            }
            else if ((b & 0xF8) == 0xB8 && i + 4 < bytes.size()) {
                // mov reg32, imm32
                uint32_t imm = 0;
                std::memcpy(&imm, &bytes[i + 1], 4);
                int rid = b - 0xB8;
                if (rid == 0) g_cpu.rax = imm;
                else if (rid == 1) g_cpu.rcx = imm;
                else if (rid == 2) g_cpu.rdx = imm;
                else if (rid == 3) g_cpu.rbx = imm;
                i += 4;
            }
            else if (b == 0x31 && i + 1 < bytes.size()) {
                // xor reg32, reg32
                if (bytes[i + 1] == 0xC0) g_cpu.rax = 0;
                i++;
            }
        }

        return GrassValue::Num((double)g_cpu.rax);
    }

} // namespace GrassASM
