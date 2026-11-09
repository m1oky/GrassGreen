// =============================================================================
//  GrassGreen 4.0 — Hardware / Platform Abstraction Layer Implementation
// =============================================================================
#include "GrassHAL.h"
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <unordered_set>
#include <algorithm>
#include <mutex>
#if !defined(_WIN32)
#include <sys/mman.h>
#endif

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <x86intrin.h>
    #include <cpuid.h>
#endif

namespace GrassHAL {

    bool g_freestanding_mode = false;
    static uint64_t g_boot_tsc = 0;
    static std::string g_inmemory_clipboard = "GrassGreen Freestanding Clipboard";

    // ── In-Memory Virtual File System (RamFS) ──
    static std::unordered_map<std::string, std::string> g_vfs_files;
    static std::unordered_set<std::string> g_vfs_dirs;

    Framebuffer g_software_fb;

    void set_freestanding(bool enable) {
        g_freestanding_mode = enable;
        if (enable && g_boot_tsc == 0) {
            g_boot_tsc = read_tsc();
            // Инициализация базовых виртуальных директорий
            vfs_mkdir("/");
            vfs_mkdir("/bin");
            vfs_mkdir("/etc");
            vfs_mkdir("/tmp");
        }
    }

    bool is_freestanding() {
        return g_freestanding_mode;
    }

    uint64_t read_tsc() {
#if defined(_MSC_VER)
        return __rdtsc();
#elif defined(__GNUC__) || defined(__clang__)
        return __builtin_ia32_rdtsc();
#else
        return 0;
#endif
    }

    double get_elapsed_ms(uint64_t start_tsc) {
        uint64_t cur = read_tsc();
        uint64_t origin = (start_tsc == 0) ? g_boot_tsc : start_tsc;
        if (cur < origin) return 0.0;
        // Расчёт на основе тактовой частоты процессора (~2.5 GHz = 2 500 000 тактов на миллисекунду)
        return (double)(cur - origin) / 2500000.0;
    }

    std::string get_rtc_date() {
        // Freestanding RTC дата
        return "2026-11-04";
    }

    std::string get_rtc_time() {
        double ms = get_elapsed_ms(0);
        int total_sec = (int)(ms / 1000.0);
        int hours = (total_sec / 3600) % 24;
        int mins = (total_sec / 60) % 60;
        int secs = total_sec % 60;

        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hours, mins, secs);
        return std::string(buf);
    }

    void delay_ms(uint32_t ms) {
        uint64_t start = read_tsc();
        uint64_t target_ticks = (uint64_t)ms * 2500000ULL;
        while (read_tsc() - start < target_ticks) {
#if defined(__GNUC__) || defined(__clang__)
            __builtin_ia32_pause();
#elif defined(_MSC_VER)
            _mm_pause();
#endif
        }
    }

    uint32_t get_cpu_cores() {
#if (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
        unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
            unsigned int logical = (ebx >> 16) & 0xFF;
            if (logical > 0) return logical;
        }
#endif
        return 4; // Значение по умолчанию для bare-metal окружения
    }

    uint64_t get_total_ram_bytes() {
        // Базовый физический пул оперативной памяти (16 ГБ)
        return 16ULL * 1024ULL * 1024ULL * 1024ULL;
    }

    uint64_t get_free_ram_bytes() {
        return 8ULL * 1024ULL * 1024ULL * 1024ULL;
    }

    std::string get_system_name() {
        return "GrassGreen-BareMetal-x86_64";
    }

    std::string get_user_name() {
        return "root";
    }

    bool clipboard_set(const std::string& text) {
        g_inmemory_clipboard = text;
        return true;
    }

    std::string clipboard_get() {
        return g_inmemory_clipboard;
    }

    // ── Реализация RamFS (VFS) ──
    bool vfs_mkdir(const std::string& path) {
        g_vfs_dirs.insert(path);
        return true;
    }

    bool vfs_is_dir(const std::string& path) {
        return g_vfs_dirs.count(path) > 0;
    }

    int64_t vfs_size(const std::string& path) {
        auto it = g_vfs_files.find(path);
        if (it != g_vfs_files.end()) return (int64_t)it->second.size();
        return 0;
    }

    bool vfs_copy(const std::string& src, const std::string& dst) {
        auto it = g_vfs_files.find(src);
        if (it != g_vfs_files.end()) {
            g_vfs_files[dst] = it->second;
            return true;
        }
        return false;
    }

    std::string vfs_list(const std::string& path) {
        std::string res;
        for (const auto& d : g_vfs_dirs) {
            if (d != path && d.find(path) == 0) {
                res += d + "/\n";
            }
        }
        for (const auto& f : g_vfs_files) {
            if (f.first.find(path) == 0) {
                res += f.first + "\n";
            }
        }
        return res;
    }

    bool vfs_write(const std::string& path, const std::string& content) {
        g_vfs_files[path] = content;
        return true;
    }

    std::string vfs_read(const std::string& path) {
        auto it = g_vfs_files.find(path);
        if (it != g_vfs_files.end()) return it->second;
        return "";
    }

    bool vfs_exists(const std::string& path) {
        return g_vfs_files.count(path) > 0 || g_vfs_dirs.count(path) > 0;
    }

    bool vfs_remove(const std::string& path) {
        g_vfs_files.erase(path);
        g_vfs_dirs.erase(path);
        return true;
    }

    // ── Реализация Task Table ──
    int64_t proc_find(const std::string& name) {
        if (name == "init" || name == "system") return 1;
        if (name == "kernel" || name == "grass") return 2;
        return -1;
    }

    bool proc_kill(int64_t pid) {
        return pid > 2;
    }

    int64_t proc_mem(int64_t pid) {
        if (pid == 1) return 16;
        if (pid == 2) return 64;
        return 0;
    }

    bool proc_exists(int64_t pid) {
        return pid == 1 || pid == 2;
    }

    std::string proc_list() {
        return "PID 1: init [kernel]\nPID 2: grass_vm [runtime]\n";
    }

    // ── Software Framebuffer ──
    void Framebuffer::init(int w, int h, uint32_t bg) {
        width = w;
        height = h;
        pixels.assign(width * height, bg);
        initialized = true;
    }

    void Framebuffer::clear(uint32_t color) {
        if (!initialized) init(800, 600, color);
        std::fill(pixels.begin(), pixels.end(), color);
    }

    void Framebuffer::set_pixel(int x, int y, uint32_t color) {
        if (x >= 0 && x < width && y >= 0 && y < height) {
            pixels[y * width + x] = color;
        }
    }

    void Framebuffer::draw_rect(int x, int y, int w, int h, uint32_t color) {
        for (int py = y; py < y + h; ++py) {
            for (int px = x; px < x + w; ++px) {
                set_pixel(px, py, color);
            }
        }
    }

    void Framebuffer::draw_circle(int cx, int cy, int r, uint32_t color) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (dx * dx + dy * dy <= r * r) {
                    set_pixel(cx + dx, cy + dy, color);
                }
            }
        }
    }

    void Framebuffer::draw_line(int x1, int y1, int x2, int y2, uint32_t color) {
        int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
        int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
        int err = dx + dy, e2;
        while (true) {
            set_pixel(x1, y1, color);
            if (x1 == x2 && y1 == y2) break;
            e2 = 2 * err;
            if (e2 >= dy) { err += dy; x1 += sx; }
            if (e2 <= dx) { err += dx; y1 += sy; }
        }
    }

    // ── 7. Исполняемая память JIT (Freestanding / Bare-Metal) ──
    #if defined(_WIN32)
        #include <windows.h>
    #endif

    alignas(4096) static uint8_t g_freestanding_exec_pool[1024 * 1024]; // 1 МБ исполняемой памяти
    static size_t g_exec_pool_offset = 0;
    static std::mutex g_exec_pool_mtx;

    void* alloc_executable_memory(size_t size) {
        std::lock_guard<std::mutex> lock(g_exec_pool_mtx);
        if (is_freestanding()) {
            size = (size + 4095) & ~4095; // выравнивание по 4KB странице
            if (g_exec_pool_offset + size <= sizeof(g_freestanding_exec_pool)) {
                void* p = &g_freestanding_exec_pool[g_exec_pool_offset];
                g_exec_pool_offset += size;
                return p;
            }
            return nullptr;
        }
        #if defined(_WIN32)
        return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        #elif defined(__linux__) || defined(__APPLE__)
        void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return (p == MAP_FAILED) ? nullptr : p;
        #else
        return nullptr;
        #endif
    }

    void free_executable_memory(void* ptr, size_t size) {
        if (is_freestanding() || !ptr) {
            return;
        }
        #if defined(_WIN32)
        VirtualFree(ptr, 0, MEM_RELEASE);
        #elif defined(__linux__) || defined(__APPLE__)
        munmap(ptr, size);
        #endif
    }
}
