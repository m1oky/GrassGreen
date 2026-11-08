// =============================================================================
//  GrassGreen 4.2 — Comprehensive Hardware Inspection Subsystem (!hardware!)
//  Real hardware monitoring: CPU, RAM, Disk, Network, GPU, Motherboard & BIOS
// =============================================================================
#ifndef GRASS_HARDWARE_H
#define GRASS_HARDWARE_H

#include <string>
#include <cstdint>

namespace GrassHardware {
    // ── 1. CPU (Процессор) ──
    std::string cpu_vendor();
    std::string cpu_brand();
    int64_t cpu_cores();
    int64_t cpu_threads();
    int64_t cpu_sockets();
    int64_t cpu_family();
    int64_t cpu_freq_mhz();
    int64_t cpu_base_freq_mhz();
    int64_t cpu_usage_percent();
    int64_t cpu_temp();
    int64_t cpu_cache_l1_kb();
    int64_t cpu_cache_l2_kb();
    int64_t cpu_cache_l3_kb();
    std::string cpu_virt();
    int64_t uptime_sec();
    std::string uptime_str();

    // ── 2. RAM (Оперативная память) ──
    int64_t ram_total_mb();
    int64_t ram_used_mb();
    int64_t ram_free_mb();
    int64_t ram_cached_mb();
    int64_t ram_speed_mhz();
    int64_t ram_temp();
    std::string ram_model();
    std::string ram_form();

    // ── 3. DISK (Дисковые накопители) ──
    int64_t disk_size_gb();
    int64_t disk_free_gb();
    std::string disk_format();
    std::string disk_model();
    std::string disk_type();
    int64_t disk_temp();
    int64_t disk_read_speed();
    int64_t disk_write_speed();
    int64_t disk_resp_time();
    int64_t disk_usage_percent();
    int64_t disk_is_system();
    int64_t pagefile_total_mb();
    int64_t pagefile_used_mb();

    // ── 4. NETWORK (Интернет и сеть) ──
    std::string net_adapter();
    std::string net_type();
    std::string net_ipv4();
    std::string net_ipv6();
    int64_t net_recv_speed_kb();
    int64_t net_send_speed_kb();
    int64_t net_recv_total_mb();
    int64_t net_send_total_mb();

    // ── 5. GPU (Графический процессор) ──
    std::string gpu_name();
    int64_t gpu_usage_percent();
    int64_t gpu_temp();
    int64_t gpu_vram_total_mb();
    int64_t gpu_vram_used_mb();
    int64_t gpu_vram_shared_mb();

    // ── 6. MOTHERBOARD & BIOS (Материнская плата и BIOS) ──
    std::string board_vendor();
    std::string board_model();
    std::string bios_vendor();
    std::string bios_date();
    std::string bios_version();
    int64_t pci_count();

    // ── 7. Low-level port I/O & Cycle Counter ──
    uint8_t inb(uint16_t port);
    void outb(uint16_t port, uint8_t val);
    uint64_t rdtsc();
    void reboot();
}

#endif // GRASS_HARDWARE_H
