#ifndef GRASS_HARDWARE_H
#define GRASS_HARDWARE_H

#include <string>
#include <cstdint>

namespace GrassHardware {
    // RAM Info (in Megabytes)
    int64_t ram_total_mb();
    int64_t ram_used_mb();
    int64_t ram_free_mb();

    // CPU Info
    std::string cpu_vendor();
    std::string cpu_brand();
    int64_t cpu_cores();
    int64_t cpu_family();
    int64_t cpu_freq_mhz();

    // Motherboard & BIOS Info
    std::string board_vendor();
    std::string board_model();
    std::string bios_vendor();
    std::string bios_date();

    // PCI Bus
    int64_t pci_count();

    // Low-level port I/O and CPU cycle counter
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t val);
    uint64_t rdtsc();
    void reboot();
}

#endif // GRASS_HARDWARE_H
