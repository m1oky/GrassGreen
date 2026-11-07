// =============================================================================
//  GrassGreen 4.1 — Встроенный ассемблер ядра (!asm!)
//  Низкоуровневые ассемблерные вставки, системные регистры, порты и прерывания
//  100% C++20 без внешних зависимостей (NASM / GAS)
// =============================================================================
#pragma once

#include "GrassSyntax.h"
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

namespace GrassASM {

    // Состояние виртуального/аппаратного процессора ядра
    struct CPUState {
        uint64_t rax = 0;
        uint64_t rbx = 0;
        uint64_t rcx = 0;
        uint64_t rdx = 0;
        uint64_t rsi = 0;
        uint64_t rdi = 0;
        uint64_t rbp = 0;
        uint64_t rsp = 0x7FFFFFF0;
        uint64_t r8 = 0, r9 = 0, r10 = 0, r11 = 0, r12 = 0, r13 = 0, r14 = 0, r15 = 0;
        uint64_t rip = 0;
        uint64_t rflags = 0x202; // IF = 1

        uint64_t cr0 = 0x80050033; // PG, WP, NE, ET, MP, PE
        uint64_t cr2 = 0;          // Page Fault Linear Address
        uint64_t cr3 = 0x1000;       // PML4 / Page Directory Base
        uint64_t cr4 = 0x6F0;        // PAE, OSFXSR, OSXMMEXCPT, FSGSBASE

        bool interrupts_enabled = true;
        bool halted = false;
    };

    // Инициализация подсистемы
    void initialize();

    // ── Управление прерываниями (Interrupts) ──
    void cli();
    void sti();
    void hlt();
    bool are_interrupts_enabled();

    // ── Порты ввода/вывода (I/O Ports) ──
    uint8_t  in8(uint16_t port);
    uint16_t in16(uint16_t port);
    uint32_t in32(uint16_t port);

    void out8(uint16_t port, uint8_t value);
    void out16(uint16_t port, uint16_t value);
    void out32(uint16_t port, uint32_t value);

    // ── Системные регистры процессора ──
    uint64_t read_cr0();
    void     write_cr0(uint64_t val);

    uint64_t read_cr2();
    void     write_cr2(uint64_t val);

    uint64_t read_cr3();
    void     write_cr3(uint64_t val);

    uint64_t read_cr4();
    void     write_cr4(uint64_t val);

    uint64_t read_rflags();
    void     write_rflags(uint64_t val);

    void     invlpg(uint64_t addr);
    uint64_t rdtsc();
    void     pause();
    void     nop();

    uint32_t cpuid_eax(uint32_t leaf);
    uint32_t cpuid_ebx(uint32_t leaf);
    uint32_t cpuid_ecx(uint32_t leaf);
    uint32_t cpuid_edx(uint32_t leaf);

    // ── Встроенный ассемблер (Inline Assembler Engine) ──
    // Транслирует строковый ассемблерный код x86_64 в машинные байты без NASM/GAS
    bool assemble(const std::string& asm_code, std::vector<uint8_t>& out_bytes, std::string& out_error);

    // Выполнение ассемблерного кода или шеллкода
    GrassValue run(const std::string& asm_code);
    GrassValue run_bytes(const std::vector<uint8_t>& bytes);
    GrassValue run_hex(const std::string& hex_bytes);

    // Получить последнее скомпилированное машинное представление
    std::string get_last_machine_hex();

    // Доступ к состоянию CPU
    CPUState& get_cpu_state();

} // namespace GrassASM
