// =============================================================================
//  GrassGreen 4.2.0 — Hardware & Motherboard Inspection Subsystem (!hardware!)
//  Предоставляет высокоуровневый опрос процессора, памяти, платы и BIOS
// =============================================================================
#include "GrassHardware.h"
#include <cstring>
#include <iostream>
#include <thread>
#include <chrono>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#else
#include <unistd.h>
#include <fstream>
#include <cpuid.h>
#endif

namespace GrassHardware {

    // ── 1. Оперативная память (RAM) ──
    int64_t ram_total_mb() {
#if defined(_WIN32)
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (int64_t)(memInfo.ullTotalPhys / (1024 * 1024));
        }
#else
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("MemTotal:", 0) == 0) {
                int64_t kb = 0;
                sscanf(line.c_str(), "MemTotal: %ld", &kb);
                return kb / 1024;
            }
        }
#endif
        return 128; // fallback (QEMU default)
    }

    int64_t ram_used_mb() {
#if defined(_WIN32)
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            uint64_t usedBytes = memInfo.ullTotalPhys - memInfo.ullAvailPhys;
            return (int64_t)(usedBytes / (1024 * 1024));
        }
#else
        int64_t total = ram_total_mb();
        int64_t free = ram_free_mb();
        return total > free ? (total - free) : 4;
#endif
        return 4; // kernel reserved fallback
    }

    int64_t ram_free_mb() {
#if defined(_WIN32)
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        if (GlobalMemoryStatusEx(&memInfo)) {
            return (int64_t)(memInfo.ullAvailPhys / (1024 * 1024));
        }
#else
        std::ifstream f("/proc/meminfo");
        std::string line;
        while (std::getline(f, line)) {
            if (line.rfind("MemAvailable:", 0) == 0) {
                int64_t kb = 0;
                sscanf(line.c_str(), "MemAvailable: %ld", &kb);
                return kb / 1024;
            }
        }
#endif
        int64_t tot = ram_total_mb();
        return tot > 4 ? (tot - 4) : 124;
    }

    // ── 2. Процессор (CPU) ──
    std::string cpu_vendor() {
        char vendor[13] = { 0 };
#if defined(_WIN32)
        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 0);
        memcpy(vendor,     &cpuInfo[1], 4); // EBX
        memcpy(vendor + 4, &cpuInfo[3], 4); // EDX
        memcpy(vendor + 8, &cpuInfo[2], 4); // ECX
#elif defined(__x86_64__) || defined(__i386__)
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
            memcpy(vendor,     &ebx, 4);
            memcpy(vendor + 4, &edx, 4);
            memcpy(vendor + 8, &ecx, 4);
        }
#else
        strncpy(vendor, "GenericCPU", 12);
#endif
        return std::string(vendor);
    }

    std::string cpu_brand() {
        char brand[49] = { 0 };
#if defined(_WIN32)
        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 0x80000000);
        if ((unsigned int)cpuInfo[0] >= 0x80000004) {
            __cpuid((int*)(brand),      0x80000002);
            __cpuid((int*)(brand + 16), 0x80000003);
            __cpuid((int*)(brand + 32), 0x80000004);
            brand[48] = '\0';
            // Trim leading spaces
            char* p = brand;
            while (*p == ' ') p++;
            return std::string(p);
        }
#endif
        return cpu_vendor() + " Processor";
    }

    int64_t cpu_cores() {
        unsigned int c = std::thread::hardware_concurrency();
        return c > 0 ? (int64_t)c : 1;
    }

    int64_t cpu_family() {
#if defined(_WIN32)
        int cpuInfo[4] = { 0 };
        __cpuid(cpuInfo, 1);
        int family = (cpuInfo[0] >> 8) & 0x0F;
        int extFamily = (cpuInfo[0] >> 20) & 0xFF;
        return (int64_t)(family == 0x0F ? (family + extFamily) : family);
#else
        return 6;
#endif
    }

    int64_t cpu_freq_mhz() {
#if defined(_WIN32)
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            DWORD mhz = 0;
            DWORD size = sizeof(mhz);
            if (RegQueryValueExA(hKey, "~MHz", NULL, NULL, (LPBYTE)&mhz, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return (int64_t)mhz;
            }
            RegCloseKey(hKey);
        }
#endif
        return 2400; // standard virtual frequency
    }

    // ── 3. Материнская плата и BIOS ──
#if defined(_WIN32)
    static std::string ReadBiosReg(const char* valName, const std::string& fallback) {
        HKEY hKey;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            char buf[256] = { 0 };
            DWORD size = sizeof(buf);
            if (RegQueryValueExA(hKey, valName, NULL, NULL, (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                return std::string(buf);
            }
            RegCloseKey(hKey);
        }
        return fallback;
    }
#endif

    std::string board_vendor() {
#if defined(_WIN32)
        return ReadBiosReg("BaseBoardManufacturer", "QEMU / Standard PC");
#else
        return "QEMU";
#endif
    }

    std::string board_model() {
#if defined(_WIN32)
        return ReadBiosReg("BaseBoardProduct", "Standard PC (i440FX + PIIX, 1996)");
#else
        return "Standard PC (i440FX)";
#endif
    }

    std::string bios_vendor() {
#if defined(_WIN32)
        return ReadBiosReg("BIOSVendor", "SeaBIOS");
#else
        return "SeaBIOS";
#endif
    }

    std::string bios_date() {
#if defined(_WIN32)
        return ReadBiosReg("BIOSReleaseDate", "04/01/2014");
#else
        return "04/01/2014";
#endif
    }

    // ── 4. PCI Bus ──
    int64_t pci_count() {
        return 5; // Стандартный набор QEMU: Host Bridge, ISA Bridge, IDE, VGA, Network
    }

    // ── 5. Низкоуровневые порты и системные вызовы ──
    uint8_t inb(uint16_t port) {
        (void)port;
        return 0;
    }

    void outb(uint16_t port, uint8_t val) {
        (void)port;
        (void)val;
    }

    uint64_t rdtsc() {
#if defined(_WIN32)
        return __rdtsc();
#elif defined(__x86_64__) || defined(__i386__)
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
        return ((uint64_t)hi << 32) | lo;
#else
        return 0;
#endif
    }

    void reboot() {
#if defined(_WIN32)
        ExitProcess(0);
#else
        exit(0);
#endif
    }
}
