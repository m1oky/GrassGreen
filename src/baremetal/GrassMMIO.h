// =============================================================================
//  GrassGreen 4.0 — Memory-Mapped I/O & Volatile Hardware Access (GrassMMIO)
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

namespace GrassMMIO {

    // Инициализация подсистемы MMIO / Port I/O
    void initialize();

    // ── 1. MMIO: Чтение из физической/виртуальной памяти (volatile) ──
    uint8_t  read8(uint64_t address);
    uint16_t read16(uint64_t address);
    uint32_t read32(uint64_t address);
    uint64_t read64(uint64_t address);

    // ── 2. MMIO: Запись в физическую/виртуальную память (volatile) ──
    void write8(uint64_t address, uint8_t value);
    void write16(uint64_t address, uint16_t value);
    void write32(uint64_t address, uint32_t value);
    void write64(uint64_t address, uint64_t value);

    // ── 3. x86 Port I/O (Аппаратные порты inb/inw/inl) ──
    uint8_t  in8(uint16_t port);
    uint16_t in16(uint16_t port);
    uint32_t in32(uint16_t port);

    // ── 4. x86 Port I/O (Аппаратные порты outb/outw/outl) ──
    void out8(uint16_t port, uint8_t value);
    void out16(uint16_t port, uint16_t value);
    void out32(uint16_t port, uint32_t value);

    // ── 5. Аппаратные барьеры памяти (Memory Barriers) ──
    void memory_barrier();
    void read_barrier();
    void write_barrier();

} // namespace GrassMMIO
