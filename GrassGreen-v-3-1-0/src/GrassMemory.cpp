#include "GrassMemory.h"
#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace GrassMemory {
    static bool g_auto_mode = false;
    static double g_memory_limit_mb = 0.0;
    static int g_next_block_id = 1;
    static std::unordered_map<int, std::vector<uint8_t>> g_allocated_blocks;

    double usit() {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
            return (double)pmc.WorkingSetSize / (1024.0 * 1024.0);
        }
        return 0.0;
    }

    void separ() {
        // Сжатие и дефрагментация кучи процесса
        HANDLE hHeap = GetProcessHeap();
        if (hHeap) {
            HeapCompact(hHeap, 0);
        }
    }

    void set_auto(bool enable) {
        g_auto_mode = enable;
        if (enable) {
            separ();
            clean();
        }
    }

    bool is_auto() {
        return g_auto_mode;
    }

    void clean() {
        // Сброс рабочего набора страниц памяти обратно в систему
        EmptyWorkingSet(GetCurrentProcess());
    }

    std::string get_system_free() {
        MEMORYSTATUSEX statex;
        statex.dwLength = sizeof(statex);
        if (GlobalMemoryStatusEx(&statex)) {
            double total_gb = (double)statex.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
            double free_gb = (double)statex.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
            std::stringstream ss;
            ss << std::fixed << std::setprecision(2)
               << "Свободно: " << free_gb << " GB / Всего: " << total_gb << " GB";
            return ss.str();
        }
        return "Не удалось прочитать состояние памяти";
    }

    int alloc_block(size_t bytes) {
        if (bytes == 0) return 0;
        int id = g_next_block_id++;
        try {
            g_allocated_blocks[id].resize(bytes, 0);
            if (g_auto_mode) separ();
            return id;
        } catch (...) {
            return 0; // Недостаточно памяти
        }
    }

    bool free_block(int id) {
        auto it = g_allocated_blocks.find(id);
        if (it != g_allocated_blocks.end()) {
            it->second.clear();
            it->second.shrink_to_fit();
            g_allocated_blocks.erase(it);
            if (g_auto_mode) clean();
            return true;
        }
        return false;
    }

    void set_limit(double mb) {
        g_memory_limit_mb = mb;
    }

    bool is_limit_exceeded() {
        if (g_memory_limit_mb <= 0.0) return false;
        return usit() > g_memory_limit_mb;
    }

    void dump(size_t stack_size, const std::vector<std::string>& stack_preview,
              const std::vector<std::pair<std::string, std::string>>& vars_preview) {
        std::cout << "\n================ [ GRASSGREEN MEMORY DUMP ] ================\n";
        std::cout << "Потребление процесса: " << std::fixed << std::setprecision(3) << usit() << " MB\n";
        std::cout << "Статус авто-памяти:   " << (g_auto_mode ? "ВКЛЮЧЕН (AUTO-GC)" : "ВЫКЛЮЧЕН") << "\n";
        if (g_memory_limit_mb > 0.0) {
            std::cout << "Установлен лимит:     " << g_memory_limit_mb << " MB\n";
        }
        std::cout << "Выделено сырых блоков: " << g_allocated_blocks.size() << "\n";
        std::cout << "Размер стека ВМ:      " << stack_size << " элементов\n";

        if (!stack_preview.empty()) {
            std::cout << "-- Стек (верхние значения) --\n";
            for (size_t i = 0; i < stack_preview.size(); ++i) {
                std::cout << "  [" << i << "]: " << stack_preview[i] << "\n";
            }
        }

        std::cout << "-- Таблица переменных (" << vars_preview.size() << ") --\n";
        for (const auto& kv : vars_preview) {
            std::cout << "  $" << kv.first << " = \"" << kv.second << "\"\n";
        }
        std::cout << "============================================================\n\n";
    }
}