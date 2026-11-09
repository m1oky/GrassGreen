// =============================================================================
//  GrassGreen 4.0 — Memory-Mapped I/O & Volatile Hardware Access Implementation
//  Файл создан Архитектором. Реализация — Кодер Ядра («Занятой-Core»).
// =============================================================================
#include "GrassMMIO.h"
#include "GrassHAL.h"
#include <cstring>
#include <unordered_map>
#include <mutex>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace GrassMMIO {

    // ── Симуляция низкоуровневой физической памяти (2 МБ: IVT, BDA, VGA Text 0xB8000) ──
    constexpr size_t LOW_MEM_SIZE = 2 * 1024 * 1024;
    alignas(4096) static uint8_t g_low_mem[LOW_MEM_SIZE];

    // ── Симуляция аппаратных портов ввода-вывода (65536 портов x86) ──
    static uint8_t g_io_ports[65536];

    // ── Симуляция высоких физических адресов (PCIe BAR, APIC, ACPI) ──
    static std::unordered_map<uint64_t, uint8_t> g_high_mmio;
    static std::mutex g_mmio_mutex;

    static bool is_valid_user_ptr(uint64_t address, size_t size, bool write_access) {
        // Запрещаем доступ к сырой виртуальной памяти хост-процесса в hosted режиме (Security Sandbox Guard)
        return false;
    }

    void initialize() {
        std::lock_guard<std::mutex> lock(g_mmio_mutex);
        std::memset(g_low_mem, 0, sizeof(g_low_mem));
        std::memset(g_io_ports, 0, sizeof(g_io_ports));
        g_high_mmio.clear();
    }

    // ── 1. MMIO READ ──

    uint8_t read8(uint64_t address) {
        if (GrassHAL::is_freestanding()) {
            return *(volatile uint8_t*)(uintptr_t)address;
        }
        if (address < LOW_MEM_SIZE) {
            return *(volatile uint8_t*)&g_low_mem[address];
        }
        if (is_valid_user_ptr(address, 1, false)) {
            return *(volatile uint8_t*)(uintptr_t)address;
        }
        std::lock_guard<std::mutex> lock(g_mmio_mutex);
        auto it = g_high_mmio.find(address);
        return (it != g_high_mmio.end()) ? it->second : 0;
    }

    uint16_t read16(uint64_t address) {
        if (GrassHAL::is_freestanding()) {
            return *(volatile uint16_t*)(uintptr_t)address;
        }
        if (address + 1 < LOW_MEM_SIZE) {
            return *(volatile uint16_t*)&g_low_mem[address];
        }
        if (is_valid_user_ptr(address, 2, false)) {
            return *(volatile uint16_t*)(uintptr_t)address;
        }
        uint16_t val = (uint16_t)read8(address);
        val |= ((uint16_t)read8(address + 1) << 8);
        return val;
    }

    uint32_t read32(uint64_t address) {
        if (GrassHAL::is_freestanding()) {
            return *(volatile uint32_t*)(uintptr_t)address;
        }
        if (address + 3 < LOW_MEM_SIZE) {
            return *(volatile uint32_t*)&g_low_mem[address];
        }
        if (is_valid_user_ptr(address, 4, false)) {
            return *(volatile uint32_t*)(uintptr_t)address;
        }
        uint32_t val = (uint32_t)read16(address);
        val |= ((uint32_t)read16(address + 2) << 16);
        return val;
    }

    uint64_t read64(uint64_t address) {
        if (GrassHAL::is_freestanding()) {
            return *(volatile uint64_t*)(uintptr_t)address;
        }
        if (address + 7 < LOW_MEM_SIZE) {
            return *(volatile uint64_t*)&g_low_mem[address];
        }
        if (is_valid_user_ptr(address, 8, false)) {
            return *(volatile uint64_t*)(uintptr_t)address;
        }
        uint64_t val = (uint64_t)read32(address);
        val |= ((uint64_t)read32(address + 4) << 32);
        return val;
    }

    // ── 2. MMIO WRITE ──

    void write8(uint64_t address, uint8_t value) {
        if (GrassHAL::is_freestanding()) {
            *(volatile uint8_t*)(uintptr_t)address = value;
            return;
        }
        if (address < LOW_MEM_SIZE) {
            *(volatile uint8_t*)&g_low_mem[address] = value;
            return;
        }
        if (is_valid_user_ptr(address, 1, true)) {
            *(volatile uint8_t*)(uintptr_t)address = value;
            return;
        }
        std::lock_guard<std::mutex> lock(g_mmio_mutex);
        g_high_mmio[address] = value;
    }

    void write16(uint64_t address, uint16_t value) {
        if (GrassHAL::is_freestanding()) {
            *(volatile uint16_t*)(uintptr_t)address = value;
            return;
        }
        if (address + 1 < LOW_MEM_SIZE) {
            *(volatile uint16_t*)&g_low_mem[address] = value;
            return;
        }
        if (is_valid_user_ptr(address, 2, true)) {
            *(volatile uint16_t*)(uintptr_t)address = value;
            return;
        }
        write8(address, (uint8_t)(value & 0xFF));
        write8(address + 1, (uint8_t)((value >> 8) & 0xFF));
    }

    void write32(uint64_t address, uint32_t value) {
        if (GrassHAL::is_freestanding()) {
            *(volatile uint32_t*)(uintptr_t)address = value;
            return;
        }
        if (address + 3 < LOW_MEM_SIZE) {
            *(volatile uint32_t*)&g_low_mem[address] = value;
            return;
        }
        if (is_valid_user_ptr(address, 4, true)) {
            *(volatile uint32_t*)(uintptr_t)address = value;
            return;
        }
        write16(address, (uint16_t)(value & 0xFFFF));
        write16(address + 2, (uint16_t)((value >> 16) & 0xFFFF));
    }

    void write64(uint64_t address, uint64_t value) {
        if (GrassHAL::is_freestanding()) {
            *(volatile uint64_t*)(uintptr_t)address = value;
            return;
        }
        if (address + 7 < LOW_MEM_SIZE) {
            *(volatile uint64_t*)&g_low_mem[address] = value;
            return;
        }
        if (is_valid_user_ptr(address, 8, true)) {
            *(volatile uint64_t*)(uintptr_t)address = value;
            return;
        }
        write32(address, (uint32_t)(value & 0xFFFFFFFF));
        write32(address + 4, (uint32_t)((value >> 32) & 0xFFFFFFFF));
    }

    // ── 3. x86 Port I/O (IN) ──

    uint8_t in8(uint16_t port) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            uint8_t ret;
            __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
            return ret;
#endif
        }
        return g_io_ports[port];
    }

    uint16_t in16(uint16_t port) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            uint16_t ret;
            __asm__ volatile ("inw %1, %0" : "=a"(ret) : "Nd"(port));
            return ret;
#endif
        }
        uint16_t val = (uint16_t)g_io_ports[port];
        val |= ((uint16_t)g_io_ports[(uint16_t)(port + 1)] << 8);
        return val;
    }

    uint32_t in32(uint16_t port) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            uint32_t ret;
            __asm__ volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
            return ret;
#endif
        }
        uint32_t val = (uint32_t)in16(port);
        val |= ((uint32_t)in16((uint16_t)(port + 2)) << 16);
        return val;
    }

    // ── 4. x86 Port I/O (OUT) ──

    void out8(uint16_t port, uint8_t value) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
            return;
#endif
        }
        g_io_ports[port] = value;
    }

    void out16(uint16_t port, uint16_t value) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
            return;
#endif
        }
        g_io_ports[port] = (uint8_t)(value & 0xFF);
        g_io_ports[(uint16_t)(port + 1)] = (uint8_t)((value >> 8) & 0xFF);
    }

    void out32(uint16_t port, uint32_t value) {
        if (GrassHAL::is_freestanding()) {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
            return;
#endif
        }
        out16(port, (uint16_t)(value & 0xFFFF));
        out16((uint16_t)(port + 2), (uint16_t)((value >> 16) & 0xFFFF));
    }

    // ── 5. Аппаратные барьеры памяти ──

    void memory_barrier() {
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __builtin_ia32_mfence();
#endif
    }

    void read_barrier() {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __builtin_ia32_lfence();
#endif
    }

    void write_barrier() {
        __atomic_thread_fence(__ATOMIC_RELEASE);
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __builtin_ia32_sfence();
#endif
    }

} // namespace GrassMMIO
