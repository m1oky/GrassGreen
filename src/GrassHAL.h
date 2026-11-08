// =============================================================================
//  GrassGreen 4.0 — Hardware / Platform Abstraction Layer (GrassHAL)
//  Файл создан Архитектором. Реализация — Кодер Ядра и Кодер Библиотек.
// =============================================================================
#ifndef GRASS_HAL_H
#define GRASS_HAL_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>

namespace GrassHAL {

    // ── Глобальный флаг режима Freestanding / Bare-Metal ──
    extern bool g_freestanding_mode;

    void set_freestanding(bool enable);
    bool is_freestanding();

    // ── 1. Время и аппаратный таймер (TSC / RTC) ──
    uint64_t read_tsc();
    double get_elapsed_ms(uint64_t start_tsc);
    std::string get_rtc_date();
    std::string get_rtc_time();
    void delay_ms(uint32_t ms);

    // ── 2. Аппаратная информация (CPUID / RAM) ──
    uint32_t get_cpu_cores();
    uint64_t get_total_ram_bytes();
    uint64_t get_free_ram_bytes();
    std::string get_system_name();
    std::string get_user_name();

    // ── 3. Буфер обмена (In-Memory Fallback) ──
    bool clipboard_set(const std::string& text);
    std::string clipboard_get();

    // ── 4. Виртуальная файловая система (In-Memory VFS / RamFS) ──
    bool vfs_mkdir(const std::string& path);
    bool vfs_is_dir(const std::string& path);
    int64_t vfs_size(const std::string& path);
    bool vfs_copy(const std::string& src, const std::string& dst);
    std::string vfs_list(const std::string& path);
    bool vfs_write(const std::string& path, const std::string& content);
    std::string vfs_read(const std::string& path);
    bool vfs_exists(const std::string& path);
    bool vfs_remove(const std::string& path);

    // ── 5. Задачи и процессы (Task Table) ──
    int64_t proc_find(const std::string& name);
    bool proc_kill(int64_t pid);
    int64_t proc_mem(int64_t pid);
    bool proc_exists(int64_t pid);
    std::string proc_list();

    // ── 6. Графика (Software Framebuffer) ──
    struct Framebuffer {
        int width = 800;
        int height = 600;
        std::vector<uint32_t> pixels; // 0xAARRGGBB
        bool initialized = false;

        void init(int w, int h, uint32_t bg = 0xFF0D1117);
        void clear(uint32_t color);
        void set_pixel(int x, int y, uint32_t color);
        void draw_rect(int x, int y, int w, int h, uint32_t color);
        void draw_circle(int cx, int cy, int r, uint32_t color);
        void draw_line(int x1, int y1, int x2, int y2, uint32_t color);
    };

    // ── 7. Исполняемая память JIT (Freestanding / Bare-Metal) ──
    void* alloc_executable_memory(size_t size);
    void free_executable_memory(void* ptr, size_t size);

    extern Framebuffer g_software_fb;
}

#endif // GRASS_HAL_H
