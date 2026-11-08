// =============================================================================
//  GrassGreen 4.2.0 — Реализация сборки ядра, драйверов и ISO-образов
//  Поддержка: --multiboot, --kernel (.bin), --sys (.sys), --iso (.iso)
// =============================================================================
#include "GrassBoot.h"
#include "GrassCyrillicFont.h"
#include "GrassSyntax.h"
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <algorithm>

namespace GrassBoot {

    static inline void append_u8(std::vector<uint8_t>& buf, uint8_t val) {
        buf.push_back(val);
    }

    static inline void append_u16(std::vector<uint8_t>& buf, uint16_t val) {
        buf.push_back((uint8_t)(val & 0xFF));
        buf.push_back((uint8_t)((val >> 8) & 0xFF));
    }

    static inline void append_u32(std::vector<uint8_t>& buf, uint32_t val) {
        buf.push_back((uint8_t)(val & 0xFF));
        buf.push_back((uint8_t)((val >> 8) & 0xFF));
        buf.push_back((uint8_t)((val >> 16) & 0xFF));
        buf.push_back((uint8_t)((val >> 24) & 0xFF));
    }

    static inline void write_u16_le(std::vector<uint8_t>& buf, size_t offset, uint16_t val) {
        if (buf.size() < offset + 2) buf.resize(offset + 2, 0);
        buf[offset]     = (uint8_t)(val & 0xFF);
        buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
    }

    static inline void write_u32_le(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
        if (buf.size() < offset + 4) buf.resize(offset + 4, 0);
        buf[offset]     = (uint8_t)(val & 0xFF);
        buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        buf[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        buf[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
    }

    static inline void write_u32_both(std::vector<uint8_t>& buf, size_t offset, uint32_t val) {
        if (buf.size() < offset + 8) buf.resize(offset + 8, 0);
        // Little Endian
        buf[offset]     = (uint8_t)(val & 0xFF);
        buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        buf[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        buf[offset + 3] = (uint8_t)((val >> 24) & 0xFF);
        // Big Endian
        buf[offset + 4] = (uint8_t)((val >> 24) & 0xFF);
        buf[offset + 5] = (uint8_t)((val >> 16) & 0xFF);
        buf[offset + 6] = (uint8_t)((val >> 8) & 0xFF);
        buf[offset + 7] = (uint8_t)(val & 0xFF);
    }

    static inline void write_u16_both(std::vector<uint8_t>& buf, size_t offset, uint16_t val) {
        if (buf.size() < offset + 4) buf.resize(offset + 4, 0);
        buf[offset]     = (uint8_t)(val & 0xFF);
        buf[offset + 1] = (uint8_t)((val >> 8) & 0xFF);
        buf[offset + 2] = (uint8_t)((val >> 16) & 0xFF);
        buf[offset + 3] = (uint8_t)(val & 0xFF);
    }

    // ── 1. Сборка бинарного файла ядра (--kernel / --multiboot) ──
    bool build_kernel_bin(const std::string& output_bin,
                          const std::vector<uint8_t>& bytecode,
                          const std::vector<uint8_t>& asm_machine_code,
                          bool include_multiboot,
                          bool lang_ru) {
        std::vector<uint8_t> bin_data;
        bin_data.reserve(65536);

        // 1.1. Multiboot 1 Header (32 байта)
        const uint32_t MULTIBOOT_MAGIC = 0x1BADB002;
        const uint32_t MULTIBOOT_FLAGS = 0x00010003;
        const uint32_t MULTIBOOT_CHECKSUM = (uint32_t)(-(int64_t)(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS));

        const uint32_t KERNEL_LOAD_ADDR   = 0x00100000; // 1 MB
        const uint32_t KERNEL_HEADER_ADDR = 0x00100000;
        const uint32_t KERNEL_ENTRY_ADDR  = 0x00100040; // Entry point

        write_u32_le(bin_data, 0x00, MULTIBOOT_MAGIC);
        write_u32_le(bin_data, 0x04, MULTIBOOT_FLAGS);
        write_u32_le(bin_data, 0x08, MULTIBOOT_CHECKSUM);
        write_u32_le(bin_data, 0x0C, KERNEL_HEADER_ADDR);
        write_u32_le(bin_data, 0x10, KERNEL_LOAD_ADDR);
        write_u32_le(bin_data, 0x14, 0);
        write_u32_le(bin_data, 0x18, 0);
        write_u32_le(bin_data, 0x1C, KERNEL_ENTRY_ADDR);

        // 1.2. Multiboot 2 Header (смещение 0x20..0x3F)
        const uint32_t MB2_MAGIC = 0xE85250D6;
        const uint32_t MB2_ARCH = 0; // i386
        const uint32_t MB2_LEN = 24;
        const uint32_t MB2_CHECKSUM = (uint32_t)(-(int64_t)(MB2_MAGIC + MB2_ARCH + MB2_LEN));
        write_u32_le(bin_data, 0x20, MB2_MAGIC);
        write_u32_le(bin_data, 0x24, MB2_ARCH);
        write_u32_le(bin_data, 0x28, MB2_LEN);
        write_u32_le(bin_data, 0x2C, MB2_CHECKSUM);
        write_u16_le(bin_data, 0x30, 0);
        write_u16_le(bin_data, 0x32, 0);
        write_u32_le(bin_data, 0x34, 8);
        write_u32_le(bin_data, 0x38, 0);
        write_u32_le(bin_data, 0x3C, 0);

        // 1.3. Точка входа ядра в 32-битном Protected Mode (смещение 0x40 / 0x00100040)
        bin_data.resize(0x40, 0);

        // cli, cld
        append_u8(bin_data, 0xFA);
        append_u8(bin_data, 0xFC);

        // mov esp, 0x001FFFF0
        append_u8(bin_data, 0xBC);
        append_u32(bin_data, 0x001FFFF0);

        // mov [0x00090000], eax (Multiboot Magic)
        append_u8(bin_data, 0xA3);
        append_u32(bin_data, 0x00090000);

        // mov [0x00090004], ebx (Multiboot Info)
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D);
        append_u32(bin_data, 0x00090004);

        // --- Hardware Subsystem Auto-Detection (0x00091020..0x000910FF) ---
        // 1. RAM Detection from Multiboot Info
        append_u8(bin_data, 0x8B); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00090004); // mov edx, [0x00090004]
        append_u8(bin_data, 0x85); append_u8(bin_data, 0xD2);                                   // test edx, edx
        size_t jz_fb_ram = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
        append_u8(bin_data, 0x8A); append_u8(bin_data, 0x02);                                   // mov al, [edx] (flags)
        append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);                                   // test al, 1
        size_t jz_fb_ram2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
        append_u8(bin_data, 0x8B); append_u8(bin_data, 0x42); append_u8(bin_data, 0x04);       // mov eax, [edx + 4] (mem_lower)
        append_u8(bin_data, 0x03); append_u8(bin_data, 0x42); append_u8(bin_data, 0x08);       // add eax, [edx + 8] (mem_upper)
        append_u8(bin_data, 0x05); append_u32(bin_data, 1024);                                  // add eax, 1024
        append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE8); append_u8(bin_data, 0x0A);       // shr eax, 10 (KB -> MB)
        append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091020);                            // mov [0x00091020], eax (total MB)
        size_t jmp_ram_done = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

        bin_data[jz_fb_ram + 1] = (uint8_t)(bin_data.size() - (jz_fb_ram + 2));
        bin_data[jz_fb_ram2 + 1] = (uint8_t)(bin_data.size() - (jz_fb_ram2 + 2));
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091020); append_u32(bin_data, 128); // fallback 128 MB

        bin_data[jmp_ram_done + 1] = (uint8_t)(bin_data.size() - (jmp_ram_done + 2));
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091024); append_u32(bin_data, 4); // used RAM = 4 MB
        append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00091020);                            // mov eax, [0x00091020]
        append_u8(bin_data, 0x83); append_u8(bin_data, 0xE8); append_u8(bin_data, 0x04);       // sub eax, 4
        append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091028);                            // mov [0x00091028], eax (free MB)

        // 2. CPUID Vendor Detection
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);                                   // xor eax, eax
        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);                                   // cpuid
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091030); // mov [0x00091030], ebx
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00091034); // mov [0x00091034], edx
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00091038); // mov [0x00091038], ecx
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009103C); append_u8(bin_data, 0);

        // 3. CPUID Brand / Model Detection
        append_u8(bin_data, 0xB8); append_u32(bin_data, 0x80000000);                            // mov eax, 0x80000000
        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);                                   // cpuid
        append_u8(bin_data, 0x3D); append_u32(bin_data, 0x80000004);                            // cmp eax, 0x80000004
        size_t jb_br_fb = bin_data.size(); append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
        append_u8(bin_data, 0xB8); append_u32(bin_data, 0x80000002); append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);
        append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091040);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091044);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00091048);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009104C);
        append_u8(bin_data, 0xB8); append_u32(bin_data, 0x80000003); append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);
        append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091050);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091054);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00091058);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009105C);
        append_u8(bin_data, 0xB8); append_u32(bin_data, 0x80000004); append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);
        append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091060);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091064);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00091068);
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009106C);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091070); append_u8(bin_data, 0);
        size_t jmp_br_done = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

        bin_data[jb_br_fb + 1] = (uint8_t)(bin_data.size() - (jb_br_fb + 2));
        append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091030); // mov esi, vendor
        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00091040); // mov edi, brand
        append_u8(bin_data, 0xB9); append_u32(bin_data, 4);          // 16 bytes
        append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA5);

        bin_data[jmp_br_done + 1] = (uint8_t)(bin_data.size() - (jmp_br_done + 2));

        // 4. CPU Cores from CPUID(1)
        append_u8(bin_data, 0xB8); append_u32(bin_data, 1);
        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xA2);
        append_u8(bin_data, 0xC1); append_u8(bin_data, 0xEB); append_u8(bin_data, 16);
        append_u8(bin_data, 0x81); append_u8(bin_data, 0xE3); append_u32(bin_data, 0xFF);
        append_u8(bin_data, 0x85); append_u8(bin_data, 0xDB);
        size_t jnz_cores_ok = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
        append_u8(bin_data, 0xBB); append_u32(bin_data, 1);
        bin_data[jnz_cores_ok + 1] = (uint8_t)(bin_data.size() - (jnz_cores_ok + 2));
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x0009102C);

        // 5. Default Motherboard & BIOS Strings
        // "SeaBIOS\0" at 0x00091080
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091080); append_u32(bin_data, 0x42616553);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091084); append_u32(bin_data, 0x00534F49);
        // "QEMU\0" at 0x00091090 (board vendor)
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091090); append_u32(bin_data, 0x554D4551);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091094); append_u8(bin_data, 0);
        // "Standard PC (i440FX)\0" at 0x000910A0 (board model)
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910A0); append_u32(bin_data, 0x6E617453);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910A4); append_u32(bin_data, 0x64726164);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910A8); append_u32(bin_data, 0x20435020);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910AC); append_u32(bin_data, 0x34346928);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910B0); append_u32(bin_data, 0x29584630);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910B4); append_u8(bin_data, 0);
        // "04/01/2014\0" at 0x000910C0 (bios date)
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910C0); append_u32(bin_data, 0x302F3430);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910C4); append_u32(bin_data, 0x30322F31);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910C8); append_u32(bin_data, 0x00003431);
        // PCI Count: 5 at 0x000910E0
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910E0); append_u32(bin_data, 5);

        // Extended Hardware Information
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910D0); append_u32(bin_data, 0x34312E31);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910D4); append_u8(bin_data, 46);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910D5); append_u8(bin_data, 48);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000910D6); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091100); append_u32(bin_data, 1);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091104); append_u32(bin_data, 1);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091108); append_u32(bin_data, 2900);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009110C); append_u32(bin_data, 2900);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091110); append_u32(bin_data, 18);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091114); append_u32(bin_data, 42);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091118); append_u32(bin_data, 256);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009111C); append_u32(bin_data, 2048);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091120); append_u32(bin_data, 16384);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091124); append_u32(bin_data, 0xBAD092D0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091128); append_u32(bin_data, 0x8ED1BBD0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009112C); append_u32(bin_data, 0xB5D087D1);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091130); append_u32(bin_data, 0xBED0BDD0);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091134); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091140); append_u32(bin_data, 0x303A3030);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091144); append_u32(bin_data, 0x32343A35);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091148); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091150); append_u32(bin_data, 342);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091160); append_u32(bin_data, 3200);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091164); append_u32(bin_data, 16);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091168); append_u32(bin_data, 34);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091170); append_u32(bin_data, 0x34524444);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091174); append_u32(bin_data, 0x3032332D);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091178); append_u32(bin_data, 0x44532030);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009117C); append_u32(bin_data, 0x004D4152);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091190); append_u32(bin_data, 0x4D4D4944);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091194); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911A0); append_u32(bin_data, 512);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911A4); append_u32(bin_data, 450);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911A8); append_u32(bin_data, 0x33544146);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911AC); append_u8(bin_data, 50);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911AD); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911B8); append_u32(bin_data, 0x554D4551);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911BC); append_u32(bin_data, 0x52414820);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911C0); append_u32(bin_data, 0x53494444);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911C4); append_u8(bin_data, 75);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911C5); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911D8); append_u32(bin_data, 0x00445353);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911E8); append_u32(bin_data, 38);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911EC); append_u32(bin_data, 250);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911F0); append_u32(bin_data, 180);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911F4); append_u32(bin_data, 1);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911F8); append_u32(bin_data, 5);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000911FC); append_u32(bin_data, 1);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091200); append_u32(bin_data, 2048);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091204); append_u32(bin_data, 256);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091210); append_u32(bin_data, 0x65746E49);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091214); append_u32(bin_data, 0x3238206C);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091218); append_u32(bin_data, 0x45303435);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009121C); append_u32(bin_data, 0x6947204D);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091220); append_u32(bin_data, 0x69626167);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091224); append_u32(bin_data, 0x74452074);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091228); append_u32(bin_data, 0x6E726568);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009122C); append_u8(bin_data, 101);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009122D); append_u8(bin_data, 116);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009122E); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091230); append_u32(bin_data, 0x65687445);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091234); append_u32(bin_data, 0x74656E72);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091238); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091240); append_u32(bin_data, 0x302E3031);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091244); append_u32(bin_data, 0x312E322E);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091248); append_u8(bin_data, 53);
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091249); append_u8(bin_data, 0);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091250); append_u32(bin_data, 0x30386566);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091254); append_u32(bin_data, 0x30353A3A);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091258); append_u32(bin_data, 0x663A3435);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009125C); append_u32(bin_data, 0x65663A66);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091260); append_u32(bin_data, 0x333A3231);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091264); append_u32(bin_data, 0x00363534);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091270); append_u32(bin_data, 48);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091274); append_u32(bin_data, 24);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091278); append_u32(bin_data, 12);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009127C); append_u32(bin_data, 4);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091280); append_u32(bin_data, 0x554D4551);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091284); append_u32(bin_data, 0x61745320);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091288); append_u32(bin_data, 0x7261646E);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009128C); append_u32(bin_data, 0x47562064);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091290); append_u32(bin_data, 0x202F2041);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091294); append_u32(bin_data, 0x68636F42);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091298); append_u32(bin_data, 0x69442073);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009129C); append_u32(bin_data, 0x616C7073);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912A0); append_u32(bin_data, 0x00000079);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912C0); append_u32(bin_data, 14);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912C4); append_u32(bin_data, 45);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912C8); append_u32(bin_data, 128);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912CC); append_u32(bin_data, 8);
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000912D0); append_u32(bin_data, 64);


        // --- 1. Очистка ВСЕГО экрана VGA (0xB8000..0xB8FA0, 2000 ячеек / 4000 байт) ---
        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B8000); // mov edi, 0x000B8000
        append_u8(bin_data, 0xB9); append_u32(bin_data, 1000);       // mov ecx, 1000 dwords
        append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720); // mov eax, 0x07200720 (space cells)
        append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);        // rep stosd

        // --- 2. Включение аппаратного мигающего текстового курсора VGA (CRTC 0x3D4/0x3D5) ---
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03D4); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); // inc edx (0x3D5)
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0E); append_u8(bin_data, 0xEE); // scanline start 14, enable
        append_u8(bin_data, 0x4A); // dec edx (0x3D4)
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0B); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); // inc edx (0x3D5)
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0F); append_u8(bin_data, 0xEE); // scanline end 15

        // --- 3. Загрузка кириллического шрифта 8x16 в VGA Plane 2 (0x000A1800) ---
        // Sequencer -> Plane 2 write mode
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03C4); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x01); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x02); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x06); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);

        // Graphics Controller -> Plane 2
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03CE); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x02); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x05); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x06); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);

        // Копирование 64 глифов шрифта в Plane 2 (0x000A1800)
        append_u8(bin_data, 0xBE);
        size_t fixup_font_addr = bin_data.size();
        append_u32(bin_data, 0); // fixup later to addr_cyrillic_font
        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000A1800);
        append_u8(bin_data, 0xB9); append_u32(bin_data, 64);
        size_t copy_font_top = bin_data.size();
        append_u8(bin_data, 0x51); // push ecx
        append_u8(bin_data, 0xB9); append_u32(bin_data, 16); // mov ecx, 16
        append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA4); // rep movsb
        append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 16); // add edi, 16
        append_u8(bin_data, 0x59); // pop ecx
        append_u8(bin_data, 0x49); // dec ecx
        append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(copy_font_top - (bin_data.size() + 1)));

        // Восстановление Sequencer в текстовый режим
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03C4); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x01); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x02); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x02); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);

        // Восстановление Graphics Controller в текстовый режим
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03CE); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x04); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x00); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x05); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x10); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x06); append_u8(bin_data, 0xEE);
        append_u8(bin_data, 0x42); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0E); append_u8(bin_data, 0xEE); append_u8(bin_data, 0x4A);

        // --- 4. Инициализация переменных рантайма ядра в памяти ---
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091000); append_u32(bin_data, 160); // vga_offset = 160
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091004); append_u8(bin_data, 0);   // utf8_lead = 0
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091008); append_u8(bin_data, 0);   // pending_char = 0
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100A); append_u8(bin_data, 0);   // shift_state = 0
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100B); append_u8(bin_data, 0);   // alt_state = 0
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100C); append_u8(bin_data, 0);   // layout = 0 (EN)
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // vga_attr = 0x0A (GrassGreen light green on black)
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 0);   // not_clear_flag = 0
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091014); append_u32(bin_data, 160); // not_clear_boundary = 160 (banner line 0 protected)
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091300); append_u8(bin_data, '/');  // vfs_cwd[0] = '/'
        append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);    // vfs_cwd[1] = 0

        // --- 4.1. Инициализация RamFS таблицы по умолчанию (0x00093000) ---
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00093000); append_u32(bin_data, 8); // count = 8
        // Очистка и копирование таблицы 8 записей x 256 байт = 2048 байт
        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00093004);
        append_u8(bin_data, 0xBE);
        size_t fixup_default_ramfstr_mem_addr = bin_data.size();
        append_u32(bin_data, 0); // fixup later to addr_default_ramfs
        append_u8(bin_data, 0xB9); append_u32(bin_data, 2048);
        append_u8(bin_data, 0xFC); // cld
        append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA4); // rep movsb

        // --- 5. Вывод приветственного баннера GrassGreen в строку 0 ---
        const char* banner = "GrassGreen OS v4.3.0 Kernel [Multiboot Online]";
        size_t banner_len = strlen(banner);
        for (size_t i = 0; i < banner_len; ++i) {
            uint32_t vga_offset = 0x000B8000 + (uint32_t)(i * 2);
            uint16_t cell = (uint16_t)(0x0A00 | (uint8_t)banner[i]);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05);
            append_u32(bin_data, vga_offset);
            append_u16(bin_data, cell);
        }

        // --- 6. Инициализация курсора на строку 1 (смещение 160) ---
        append_u8(bin_data, 0xBB); append_u32(bin_data, 160); // mov ebx, 160
        append_u8(bin_data, 0xE8);
        size_t fixup_init_cursor = bin_data.size();
        append_u32(bin_data, 0); // fixup later to addr_update_cursor

        // --- 7. Инициализация последовательного порта COM1 (0x3F8) ---
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F9); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0x30); append_u8(bin_data, 0xC0);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FB); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x80);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F9); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0x30); append_u8(bin_data, 0xC0);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FB); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FA); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0xC7);
        append_u8(bin_data, 0xEE);

        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FC); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0B);
        append_u8(bin_data, 0xEE);

        const char* com_msg = "\r\n[GrassGreen OS v4.3.0]: Booted successfully via Multiboot!\r\n";
        for (size_t i = 0; i < strlen(com_msg); ++i) {
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, (uint8_t)com_msg[i]);
            append_u8(bin_data, 0xEE);
        }

        // --- 8. Встроенные структуры данных и процедуры ядра ---
        if (!bytecode.empty()) {
            append_u8(bin_data, 0xE9); // jmp rel32 over helpers
            size_t jmp_helpers_fixup = bin_data.size();
            append_u32(bin_data, 0); // placeholder

            // 8.1. Таблица кириллического шрифта 8x16 (1024 байта)
            uint32_t addr_cyrillic_font = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            for (size_t i = 0; i < 1024; ++i) append_u8(bin_data, s_cyrillic_font_8x16[i]);

            // 8.2. Таблицы скан-кодов PS/2 Set 1 (сканкоды 0x02..0x35)
            // English Lower (52 байта)
            uint32_t addr_ps2_lut_en_lower = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            static const uint8_t ps2_lut_en_lower[52] = {
                '1','2','3','4','5','6','7','8','9','0','-','=', 0,
                '\t','q','w','e','r','t','y','u','i','o','p','[',']',
                0, 0,
                'a','s','d','f','g','h','j','k','l',';','\'','`',
                0,'\\','z','x','c','v','b','n','m',',','.','/'
            };
            for (int i = 0; i < 52; ++i) append_u8(bin_data, ps2_lut_en_lower[i]);

            // English Upper (52 байта)
            uint32_t addr_ps2_lut_en_upper = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            static const uint8_t ps2_lut_en_upper[52] = {
                '!','@','#','$','%','^','&','*','(',')','_','+', 0,
                '\t','Q','W','E','R','T','Y','U','I','O','P','{','}',
                0, 0,
                'A','S','D','F','G','H','J','K','L',':','"','~',
                0,'|','Z','X','C','V','B','N','M','<','>','?'
            };
            for (int i = 0; i < 52; ++i) append_u8(bin_data, ps2_lut_en_upper[i]);

            // Russian Lower ЙЦУКЕН (104 байта: 2 байта UTF-8 на символ)
            uint32_t addr_ps2_lut_ru_lower = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            static const uint8_t ps2_lut_ru_lower[104] = {
                0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0,
                0,0,
                0xD0,0xB9, 0xD1,0x86, 0xD1,0x83, 0xD0,0xBA, 0xD0,0xB5, 0xD0,0xBD,
                0xD0,0xB3, 0xD1,0x88, 0xD1,0x89, 0xD0,0xB7, 0xD1,0x85, 0xD1,0x8A,
                0,0, 0,0,
                0xD1,0x84, 0xD1,0x8B, 0xD0,0xB2, 0xD0,0xB0, 0xD0,0xBF, 0xD1,0x80,
                0xD0,0xBE, 0xD0,0xBB, 0xD0,0xB4, 0xD0,0xB6, 0xD1,0x8D, 0xD1,0x91,
                0,0, 0,0,
                0xD1,0x8F, 0xD1,0x87, 0xD1,0x81, 0xD0,0xBC, 0xD0,0xB8, 0xD1,0x82,
                0xD1,0x8C, 0xD0,0xB1, 0xD1,0x8E, 0x2E,0x00
            };
            for (int i = 0; i < 104; ++i) append_u8(bin_data, ps2_lut_ru_lower[i]);

            // Russian Upper ЙЦУКЕН (104 байта: 2 байта UTF-8 на символ)
            uint32_t addr_ps2_lut_ru_upper = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            static const uint8_t ps2_lut_ru_upper[104] = {
                0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0, 0,0,
                0,0,
                0xD0,0x99, 0xD0,0xA6, 0xD0,0xA3, 0xD0,0x9A, 0xD0,0x95, 0xD0,0x9D,
                0xD0,0x93, 0xD0,0xA8, 0xD0,0xA9, 0xD0,0x97, 0xD0,0xA5, 0xD0,0xAA,
                0,0, 0,0,
                0xD0,0xA4, 0xD0,0xAB, 0xD0,0x92, 0xD0,0x90, 0xD0,0x9F, 0xD0,0xA0,
                0xD0,0x9E, 0xD0,0x9B, 0xD0,0x94, 0xD0,0x96, 0xD0,0xAD, 0xD0,0x81,
                0,0, 0,0,
                0xD0,0xAF, 0xD0,0xA7, 0xD0,0xA1, 0xD0,0x9C, 0xD0,0x98, 0xD0,0xA2,
                0xD0,0xAC, 0xD0,0x91, 0xD0,0xAE, 0x2C,0x00
            };
            for (int i = 0; i < 104; ++i) append_u8(bin_data, ps2_lut_ru_upper[i]);

            // 8.3. Helper: helper_update_cursor (EBX = vga_offset)
            uint32_t addr_update_cursor = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx

            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); // mov eax, ebx
            append_u8(bin_data, 0xD1); append_u8(bin_data, 0xE8); // shr eax, 1 (cell index)
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC1); // mov ecx, eax

            // Port 0x3D4, Index 0x0E (Cursor High)
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03D4); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0E); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0x42); // inc edx (0x3D5)
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xE8); append_u8(bin_data, 0xEE); // mov al, ch; out dx, al

            // Port 0x3D4, Index 0x0F (Cursor Low)
            append_u8(bin_data, 0x4A); // dec edx (0x3D4)
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0F); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0x42); // inc edx (0x3D5)
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xC8); append_u8(bin_data, 0xEE); // mov al, cl; out dx, al

            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0xC3); // ret

            // 8.4. Helper: helper_print_char (AL = char)
            uint32_t addr_print_char = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x56); // push esi
            append_u8(bin_data, 0x57); // push edi

            append_u8(bin_data, 0x88); append_u8(bin_data, 0xC4); // mov ah, al

            // COM1 TX
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD); append_u16(bin_data, 0x0000);
            size_t wait_tx1 = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x20);
            append_u8(bin_data, 0x74); append_u8(bin_data, (uint8_t)(int8_t)(wait_tx1 - (bin_data.size() + 1)));
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xE0); append_u8(bin_data, 0xEE);

            // Newline -> \r to COM1
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0A);
            size_t jne_com1_cr = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD); append_u16(bin_data, 0x0000);
            size_t wait_tx2 = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x20);
            append_u8(bin_data, 0x74); append_u8(bin_data, (uint8_t)(int8_t)(wait_tx2 - (bin_data.size() + 1)));
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0D); append_u8(bin_data, 0xEE);
            bin_data[jne_com1_cr + 1] = (uint8_t)(bin_data.size() - (jne_com1_cr + 2));

            // VGA UTF-8 State Machine
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091004); // mov al, [0x00091004]
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0); // test al, al
            size_t jnz_has_lead = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // No lead byte yet: is AH 0xD0 or 0xD1?
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0xD0);
            size_t je_lead_d0 = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0xD1);
            size_t je_lead_d1 = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Not a lead byte -> process ASCII / control
            size_t jmp_ascii = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Record lead byte
            size_t set_lead_pos = bin_data.size();
            bin_data[je_lead_d0 + 1] = (uint8_t)(set_lead_pos - (je_lead_d0 + 2));
            bin_data[je_lead_d1 + 1] = (uint8_t)(set_lead_pos - (je_lead_d1 + 2));
            append_u8(bin_data, 0x88); append_u8(bin_data, 0x25); append_u32(bin_data, 0x00091004);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E);
            append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59);
            append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // Process second byte of UTF-8 Cyrillic
            size_t has_lead_pos = bin_data.size();
            bin_data[jnz_has_lead + 1] = (uint8_t)(has_lead_pos - (jnz_has_lead + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091004); append_u8(bin_data, 0);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xD0);
            size_t jne_lead_d1_check = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Lead was 0xD0:
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x81); // 'Ё'
            size_t jne_not_yo_cap = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
            append_u8(bin_data, 0xB4); append_u8(bin_data, 0xC5); // 'Е'
            size_t jmp_to_draw1 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jne_not_yo_cap + 1] = (uint8_t)(bin_data.size() - (jne_not_yo_cap + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xC4); append_u8(bin_data, 0x30); // byte2 + 0x30
            size_t jmp_to_draw2 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Lead was 0xD1:
            bin_data[jne_lead_d1_check + 1] = (uint8_t)(bin_data.size() - (jne_lead_d1_check + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x91); // 'ё'
            size_t jne_not_yo_sml = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
            append_u8(bin_data, 0xB4); append_u8(bin_data, 0xE5); // 'е'
            size_t jmp_to_draw3 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jne_not_yo_sml + 1] = (uint8_t)(bin_data.size() - (jne_not_yo_sml + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xC4); append_u8(bin_data, 0x70); // byte2 + 0x70
            size_t jmp_to_draw4 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // ASCII and Control Character Processing
            size_t ascii_pos = bin_data.size();
            bin_data[jmp_ascii + 1] = (uint8_t)(ascii_pos - (jmp_ascii + 2));

            // \n (Newline)
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0A);
            size_t jne_vga_nl = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // ebx = (ebx / 160 + 1) * 160
            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 160);
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1);
            append_u8(bin_data, 0x40);
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xE1);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC3);

            // If not_clear active ([0x00091010] == 1): update [0x00091014] = max([0x00091014], ebx)
            append_u8(bin_data, 0x83); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091010); append_u8(bin_data, 1);
            size_t jne_skip_nl_bound = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x3B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091014);
            size_t jbe_skip_nl_bound = bin_data.size();
            append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091014);
            bin_data[jne_skip_nl_bound + 1] = (uint8_t)(bin_data.size() - (jne_skip_nl_bound + 2));
            bin_data[jbe_skip_nl_bound + 1] = (uint8_t)(bin_data.size() - (jbe_skip_nl_bound + 2));

            size_t jmp_check_scroll1 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // \b (Backspace)
            bin_data[jne_vga_nl + 1] = (uint8_t)(bin_data.size() - (jne_vga_nl + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x08);
            size_t jne_vga_bksp = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xFB); append_u8(bin_data, 0x02);
            size_t jb_bksp_done = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xC7); append_u8(bin_data, 0x83);
            append_u32(bin_data, 0x000B8000); append_u16(bin_data, 0x0720);
            size_t jmp_bksp_done = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jb_bksp_done + 1] = (uint8_t)(bin_data.size() - (jb_bksp_done + 2));
            bin_data[jmp_bksp_done + 1] = (uint8_t)(bin_data.size() - (jmp_bksp_done + 2));
            size_t jmp_save_pos1 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // \r (Ignore)
            bin_data[jne_vga_bksp + 1] = (uint8_t)(bin_data.size() - (jne_vga_bksp + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0D);
            size_t jne_vga_draw = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            size_t jmp_save_pos2 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Draw Glyph to VGA Memory
            size_t draw_glyph_pos = bin_data.size();
            bin_data[jmp_to_draw1 + 1] = (uint8_t)(draw_glyph_pos - (jmp_to_draw1 + 2));
            bin_data[jmp_to_draw2 + 1] = (uint8_t)(draw_glyph_pos - (jmp_to_draw2 + 2));
            bin_data[jmp_to_draw3 + 1] = (uint8_t)(draw_glyph_pos - (jmp_to_draw3 + 2));
            bin_data[jmp_to_draw4 + 1] = (uint8_t)(draw_glyph_pos - (jmp_to_draw4 + 2));
            bin_data[jne_vga_draw + 1] = (uint8_t)(draw_glyph_pos - (jne_vga_draw + 2));

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xA3); append_u32(bin_data, 0x000B8000);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009100D); // mov dl, [0x0009100D]
            append_u8(bin_data, 0x88); append_u8(bin_data, 0x93); append_u32(bin_data, 0x000B8001); // mov [ebx + 0x000B8001], dl
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC3); append_u8(bin_data, 0x02);

            // Check Screen Scroll (>= 4000)
            size_t check_scroll_pos = bin_data.size();
            bin_data[jmp_check_scroll1 + 1] = (uint8_t)(check_scroll_pos - (jmp_check_scroll1 + 2));

            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFB); append_u32(bin_data, 4000);
            size_t jb_no_scroll = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);

            // Scroll lines 1..24 up by 1 line (keeps line 0 banner intact!)
            append_u8(bin_data, 0xFC);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x000B8140);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B80A0);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 920);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA5);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B8F00);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 40);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 3840);

            size_t no_scroll_pos = bin_data.size();
            bin_data[jb_no_scroll + 1] = (uint8_t)(no_scroll_pos - (jb_no_scroll + 2));
            bin_data[jmp_save_pos1 + 1] = (uint8_t)(no_scroll_pos - (jmp_save_pos1 + 2));
            bin_data[jmp_save_pos2 + 1] = (uint8_t)(no_scroll_pos - (jmp_save_pos2 + 2));

            // Save EBX & Update Hardware Cursor
            append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_update_cursor - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0xC3); // ret

            // 8.5. Helper: helper_print_string (ESI = string ptr)
            uint32_t addr_print_string = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x56); // push esi
            append_u8(bin_data, 0x50); // push eax
            size_t loop_str = bin_data.size();
            append_u8(bin_data, 0xAC); // lodsb
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0); // test al, al
            size_t jz_str_done = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); // call helper_print_char
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(loop_str - (bin_data.size() + 1)));

            bin_data[jz_str_done + 1] = (uint8_t)(bin_data.size() - (jz_str_done + 2));
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0xC3); // ret

            // 8.6. Helper: helper_print_num (EAX = int)
            uint32_t addr_print_num = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x56); // push esi

            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00093020);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); // mov byte [edi], 0
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0); // test eax, eax
            size_t jns_pos = bin_data.size();
            append_u8(bin_data, 0x79); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xD8); // neg eax
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0xB0); append_u8(bin_data, '-');
            append_u8(bin_data, 0xE8); // call helper_print_char
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x58); // pop eax

            bin_data[jns_pos + 1] = (uint8_t)(bin_data.size() - (jns_pos + 2));
            append_u8(bin_data, 0xB9); append_u32(bin_data, 10);

            size_t num_loop_pos = bin_data.size();
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2); // xor edx, edx
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1); // div ecx
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xC2); append_u8(bin_data, '0');
            append_u8(bin_data, 0x4F); // dec edi
            append_u8(bin_data, 0x88); append_u8(bin_data, 0x17); // mov [edi], dl
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0); // test eax, eax
            append_u8(bin_data, 0x75);
            append_u8(bin_data, (uint8_t)(int8_t)(num_loop_pos - (bin_data.size() + 1)));

            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFE); // mov esi, edi
            append_u8(bin_data, 0xE8); // call helper_print_string
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0xC3); // ret

            // 8.7. Helper: helper_read_char (returns char in AL)
            uint32_t addr_read_char = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx

            // Проверка наличия ожидающего 2-го байта UTF-8 в 0x00091008
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091008);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_poll_top = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091008); append_u8(bin_data, 0);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return: pop ecx, pop ebx, pop edx, ret

            size_t poll_top = bin_data.size();
            bin_data[jz_poll_top + 1] = (uint8_t)(poll_top - (jz_poll_top + 2));

            auto jmp_poll = [&]() {
                append_u8(bin_data, 0xE9);
                int32_t rel = (int32_t)poll_top - (int32_t)(bin_data.size() + 4);
                append_u32(bin_data, (uint32_t)rel);
            };

            // Проверка COM1 (порт 0x3FD)
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jz_com_skip = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // COM1 чтение символа
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0D); // CR?
            size_t jne_com1_kbd_cr = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jz_com_nolf = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEC);
            bin_data[jz_com_nolf + 1] = (uint8_t)(bin_data.size() - (jz_com_nolf + 2));
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A); // Enter (LF)
            bin_data[jne_com1_kbd_cr + 1] = (uint8_t)(bin_data.size() - (jne_com1_kbd_cr + 2));
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return

            bin_data[jz_com_skip + 1] = (uint8_t)(bin_data.size() - (jz_com_skip + 2));

            // Проверка PS/2 (порт 0x64)
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x64);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jnz_ps2 = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0x90); // pause
            jmp_poll();

            bin_data[jnz_ps2 + 1] = (uint8_t)(bin_data.size() - (jnz_ps2 + 2));
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x60); // in al, 0x60

            // Shift press (0x2A or 0x36)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x2A);
            size_t je_sh_dn = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x36);
            size_t jne_not_sh_dn = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            bin_data[je_sh_dn + 1] = (uint8_t)(bin_data.size() - (je_sh_dn + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100A); append_u8(bin_data, 1);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x0009100B);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC9);
            size_t jz_no_alt_tog = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009100C); append_u8(bin_data, 1);
            bin_data[jz_no_alt_tog + 1] = (uint8_t)(bin_data.size() - (jz_no_alt_tog + 2));
            jmp_poll();

            bin_data[jne_not_sh_dn + 1] = (uint8_t)(bin_data.size() - (jne_not_sh_dn + 2));

            // Shift release (0xAA or 0xB6)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xAA);
            size_t je_sh_up = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xB6);
            size_t jne_not_sh_up = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            bin_data[je_sh_up + 1] = (uint8_t)(bin_data.size() - (je_sh_up + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100A); append_u8(bin_data, 0);
            jmp_poll();

            bin_data[jne_not_sh_up + 1] = (uint8_t)(bin_data.size() - (jne_not_sh_up + 2));

            // Alt press (0x38)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x38);
            size_t jne_not_alt_dn = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100B); append_u8(bin_data, 1);
            jmp_poll();
            bin_data[jne_not_alt_dn + 1] = (uint8_t)(bin_data.size() - (jne_not_alt_dn + 2));

            // Alt release (0xB8)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xB8);
            size_t jne_not_alt_up = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100B); append_u8(bin_data, 0);
            jmp_poll();
            bin_data[jne_not_alt_up + 1] = (uint8_t)(bin_data.size() - (jne_not_alt_up + 2));

            // Key release (bit 7 set) -> ignore
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80);
            size_t jz_not_rel = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            jmp_poll();
            bin_data[jz_not_rel + 1] = (uint8_t)(bin_data.size() - (jz_not_rel + 2));

            // Enter (0x1C) -> LF (0x0A)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x1C);
            size_t jne_not_ent = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return
            bin_data[jne_not_ent + 1] = (uint8_t)(bin_data.size() - (jne_not_ent + 2));

            // Backspace (0x0E) -> 0x08
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0E);
            size_t jne_not_bk = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x08);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return
            bin_data[jne_not_bk + 1] = (uint8_t)(bin_data.size() - (jne_not_bk + 2));

            // Space (0x39) -> 0x20
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x39);
            size_t jne_not_sp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x20);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return
            bin_data[jne_not_sp + 1] = (uint8_t)(bin_data.size() - (jne_not_sp + 2));

            // ESC (0x01) -> 0x1B
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x01);
            size_t jne_not_esc = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x1B);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return
            bin_data[jne_not_esc + 1] = (uint8_t)(bin_data.size() - (jne_not_esc + 2));

            // Tab (0x0F) -> 0x09
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0F);
            size_t jne_not_tab = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x09);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return
            bin_data[jne_not_tab + 1] = (uint8_t)(bin_data.size() - (jne_not_tab + 2));

            // Range check 0x02..0x35
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x02);
            size_t jae_min = bin_data.size(); append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);
            jmp_poll();
            bin_data[jae_min + 1] = (uint8_t)(bin_data.size() - (jae_min + 2));

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x35);
            size_t jbe_max = bin_data.size(); append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            jmp_poll();
            bin_data[jbe_max + 1] = (uint8_t)(bin_data.size() - (jbe_max + 2));

            // Layout check: [0x0009100C] (0 = EN, 1 = RU)
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x0009100C);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC9);
            size_t jnz_ru_mode = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // English Layout
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xD8); // movzx ebx, al
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02); // sub ebx, 2
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009100A); // mov dl, [shift]
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xD2); // test dl, dl
            size_t jnz_en_up = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x83); append_u32(bin_data, addr_ps2_lut_en_lower); // mov al, [ebx + lower]
            size_t jmp_en_done = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            bin_data[jnz_en_up + 1] = (uint8_t)(bin_data.size() - (jnz_en_up + 2));
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x83); append_u32(bin_data, addr_ps2_lut_en_upper); // mov al, [ebx + upper]
            bin_data[jmp_en_done + 1] = (uint8_t)(bin_data.size() - (jmp_en_done + 2));

            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0); // valid char?
            size_t jnz_en_ok = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            jmp_poll();
            bin_data[jnz_en_ok + 1] = (uint8_t)(bin_data.size() - (jnz_en_ok + 2));
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return

            // Russian Layout
            size_t ru_mode_pos = bin_data.size();
            bin_data[jnz_ru_mode + 1] = (uint8_t)(ru_mode_pos - (jnz_ru_mode + 2));

            append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xD8); // movzx ebx, al
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02); // sub ebx, 2
            append_u8(bin_data, 0xD1); append_u8(bin_data, 0xE3);                             // shl ebx, 1 (2 bytes per entry)
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x15); append_u32(bin_data, 0x0009100A); // mov dl, [shift]
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xD2);
            size_t jnz_ru_up = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x83); append_u32(bin_data, addr_ps2_lut_ru_lower);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0xA3); append_u32(bin_data, addr_ps2_lut_ru_lower + 1);
            size_t jmp_ru_done = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            bin_data[jnz_ru_up + 1] = (uint8_t)(bin_data.size() - (jnz_ru_up + 2));
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x83); append_u32(bin_data, addr_ps2_lut_ru_upper);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0xA3); append_u32(bin_data, addr_ps2_lut_ru_upper + 1);
            bin_data[jmp_ru_done + 1] = (uint8_t)(bin_data.size() - (jmp_ru_done + 2));

            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jnz_ru_ok = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            jmp_poll();
            bin_data[jnz_ru_ok + 1] = (uint8_t)(bin_data.size() - (jnz_ru_ok + 2));

            append_u8(bin_data, 0x84); append_u8(bin_data, 0xE4); // test ah, ah
            size_t jz_single_byte = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0x25); append_u32(bin_data, 0x00091008); // store 2nd byte in 0x00091008
            bin_data[jz_single_byte + 1] = (uint8_t)(bin_data.size() - (jz_single_byte + 2));

            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3); // direct return

            // 8.8. Helper: helper_read_line (EDI = buffer)
            uint32_t addr_read_line = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFB); // mov ebx, edi

            size_t rl_top = bin_data.size();
            append_u8(bin_data, 0xE8); // call helper_read_char
            append_u32(bin_data, addr_read_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0A); // Enter (LF)?
            size_t je_rl_enter1 = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0D); // Enter (CR)?
            size_t je_rl_enter2 = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x08); // Backspace?
            size_t je_rl_bksp = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Regular character
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFA); // mov edx, edi
            append_u8(bin_data, 0x29); append_u8(bin_data, 0xDA); // sub edx, ebx
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFA); append_u32(bin_data, 250); // max 250 bytes
            size_t jb_rl_store = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));
            bin_data[jb_rl_store + 1] = (uint8_t)(bin_data.size() - (jb_rl_store + 2));

            append_u8(bin_data, 0x88); append_u8(bin_data, 0x07); // mov [edi], al
            append_u8(bin_data, 0x47);                             // inc edi
            append_u8(bin_data, 0xE8); // echo char
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            // Backspace handling
            bin_data[je_rl_bksp + 1] = (uint8_t)(bin_data.size() - (je_rl_bksp + 2));
            append_u8(bin_data, 0x39); append_u8(bin_data, 0xDF); // cmp edi, ebx
            size_t ja_rl_bksp_ok = bin_data.size();
            append_u8(bin_data, 0x77); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));
            bin_data[ja_rl_bksp_ok + 1] = (uint8_t)(bin_data.size() - (ja_rl_bksp_ok + 2));

            append_u8(bin_data, 0x4F); // dec edi
            append_u8(bin_data, 0x39); append_u8(bin_data, 0xDF);
            size_t jbe_no_extra_utf = bin_data.size();
            append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x57); append_u8(bin_data, 0xFF); // mov dl, [edi - 1]
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFA); append_u8(bin_data, 0xD0);
            size_t je_utf_extra_d0 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFA); append_u8(bin_data, 0xD1);
            size_t jne_no_extra_d1 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            bin_data[je_utf_extra_d0 + 1] = (uint8_t)(bin_data.size() - (je_utf_extra_d0 + 2));
            append_u8(bin_data, 0x4F); // dec edi (remove lead byte too)
            bin_data[jne_no_extra_d1 + 1] = (uint8_t)(bin_data.size() - (jne_no_extra_d1 + 2));
            bin_data[jbe_no_extra_utf + 1] = (uint8_t)(bin_data.size() - (jbe_no_extra_utf + 2));

            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x08);
            append_u8(bin_data, 0xE8); // erase glyph on screen
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            // Enter handling
            bin_data[je_rl_enter1 + 1] = (uint8_t)(bin_data.size() - (je_rl_enter1 + 2));
            bin_data[je_rl_enter2 + 1] = (uint8_t)(bin_data.size() - (je_rl_enter2 + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); // null terminator
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
            append_u8(bin_data, 0xE8); // echo newline
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0xC3); // ret

            // 8.9. Helper: helper_copy_string (ESI = src/val, EDI = dst)
            uint32_t addr_copy_string = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x56); // push esi
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x50); // push eax

            // Check if esi < 0x00010000 (number) or >= 0x00010000 (string pointer)
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFE); append_u32(bin_data, 0x00010000);
            size_t jae_is_str = bin_data.size();
            append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);

            // Number: tag=0, val=esi
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x07); append_u32(bin_data, 0); // mov dword [edi], 0
            append_u8(bin_data, 0x89); append_u8(bin_data, 0x77); append_u8(bin_data, 0x04); // mov [edi + 4], esi
            size_t jmp_cpydone = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // String: tag=1, text at edi + 4
            bin_data[jae_is_str + 1] = (uint8_t)(bin_data.size() - (jae_is_str + 2));
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x07); append_u32(bin_data, 1); // mov dword [edi], 1
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 0x04); // add edi, 4

            size_t cpy_loop = bin_data.size();
            append_u8(bin_data, 0xAC); // lodsb
            append_u8(bin_data, 0xAA); // stosb
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0); // test al, al
            append_u8(bin_data, 0x75);
            append_u8(bin_data, (uint8_t)(int8_t)(cpy_loop - (bin_data.size() + 1)));

            bin_data[jmp_cpydone + 1] = (uint8_t)(bin_data.size() - (jmp_cpydone + 2));
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0xC3); // ret

            // 8.10. Helper: helper_strcmp (EAX = op1, EBX = op2) -> returns EAX = 1 (equal) or 0 (not equal)
            uint32_t addr_strcmp = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x56); // push esi
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx

            // cmp eax, ebx
            append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
            size_t je_sc_equal = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Check if both >= 0x00010000
            append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00010000); // cmp eax, 0x10000
            size_t jb_sc_num = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFB); append_u32(bin_data, 0x00010000); // cmp ebx, 0x10000
            size_t jb_sc_num2 = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);

            // String comparison loop
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC6); // mov esi, eax
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xDF); // mov edi, ebx
            size_t sc_loop = bin_data.size();
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x0E); // mov cl, [esi]
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x17); // mov dl, [edi]
            append_u8(bin_data, 0x38); append_u8(bin_data, 0xD1); // cmp cl, dl
            size_t jne_sc_diff = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC9); // test cl, cl
            size_t jz_sc_eq = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x46); // inc esi
            append_u8(bin_data, 0x47); // inc edi
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(sc_loop - (bin_data.size() + 1)));

            // Number compare
            size_t sc_num_pos = bin_data.size();
            bin_data[jb_sc_num + 1] = (uint8_t)(sc_num_pos - (jb_sc_num + 2));
            bin_data[jb_sc_num2 + 1] = (uint8_t)(sc_num_pos - (jb_sc_num2 + 2));
            append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8); // cmp eax, ebx
            size_t je_sc_num_eq = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Not equal
            size_t sc_diff_pos = bin_data.size();
            bin_data[jne_sc_diff + 1] = (uint8_t)(sc_diff_pos - (jne_sc_diff + 2));
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0); // xor eax, eax
            size_t jmp_sc_done = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Equal
            size_t sc_eq_pos = bin_data.size();
            bin_data[je_sc_equal + 1] = (uint8_t)(sc_eq_pos - (je_sc_equal + 2));
            bin_data[jz_sc_eq + 1] = (uint8_t)(sc_eq_pos - (jz_sc_eq + 2));
            bin_data[je_sc_num_eq + 1] = (uint8_t)(sc_eq_pos - (je_sc_num_eq + 2));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 1); // mov eax, 1

            // Done
            bin_data[jmp_sc_done + 1] = (uint8_t)(bin_data.size() - (jmp_sc_done + 2));
            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0xC3); // ret

            // Helper lambdas for inline string emission in assembly helpers
            auto emit_inline_str = [&](const std::string& str) -> uint32_t {
                if (str.size() + 1 < 126) {
                    append_u8(bin_data, 0xEB);
                    append_u8(bin_data, (uint8_t)(str.size() + 1));
                } else {
                    append_u8(bin_data, 0xE9);
                    append_u32(bin_data, (uint32_t)(str.size() + 1));
                }
                uint32_t str_mem_addr = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
                for (char c : str) append_u8(bin_data, (uint8_t)c);
                append_u8(bin_data, 0);
                return str_mem_addr;
            };

            auto emit_print_str_lit = [&](const std::string& str) {
                uint32_t str_mem_addr = emit_inline_str(str);
                append_u8(bin_data, 0xBE); append_u32(bin_data, str_mem_addr); // mov esi, str_mem_addr
                append_u8(bin_data, 0xE8);
                append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            };

            // 8.11. Data block: Default Pre-installed RamFS Table (8 entries x 256 bytes = 2048 bytes)
            uint32_t addr_default_ramfs = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            {
                struct DefaultFSEntry {
                    const char* name;
                    const char* parent;
                    uint8_t is_dir;
                    uint8_t hidden;
                    uint16_t size;
                    const char* content;
                };
                static const DefaultFSEntry s_initial_fs[8] = {
                    { "system", "/", 1, 0, 0, "" },
                    { "kernel.sys", "/system", 0, 0, 24, "GrassGreen Kernel v4.3.0" },
                    { "config.sys", "/system", 0, 0, 35, "DISPLAY=VGA;MODE=TEXT_80X25;GUI=VD+" },
                    { "users", "/", 1, 0, 0, "" },
                    { "readme.txt", "/users", 0, 0, 41, "Welcome to RQWos on GrassGreen Bare-Metal!" },
                    { "apps", "/", 1, 0, 0, "" },
                    { "desktop.ini", "/", 0, 1, 24, "[VD+ GUI Desktop Config]" },
                    { "welcome.txt", "/", 0, 0, 32, "Hello from GrassGreen Baremetal!" }
                };
                for (int i = 0; i < 8; ++i) {
                    uint8_t buf[256];
                    std::memset(buf, 0, 256);
                    std::strncpy((char*)&buf[0], s_initial_fs[i].name, 31);
                    std::strncpy((char*)&buf[32], s_initial_fs[i].parent, 31);
                    buf[64] = s_initial_fs[i].is_dir;
                    buf[65] = s_initial_fs[i].hidden;
                    buf[66] = (uint8_t)(s_initial_fs[i].size & 0xFF);
                    buf[67] = (uint8_t)((s_initial_fs[i].size >> 8) & 0xFF);
                    std::strncpy((char*)&buf[68], s_initial_fs[i].content, 187);
                    for (int b = 0; b < 256; ++b) append_u8(bin_data, buf[b]);
                }
            }

            // 8.12. Helper: helper_clear (Screen clearing with not_clear boundary preservation)
            uint32_t addr_clear = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x57); // push edi

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091014); // mov edi, [0x00091014]
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFF); append_u32(bin_data, 4000);
            size_t jb_b_ok = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 160);
            bin_data[jb_b_ok + 1] = (uint8_t)(bin_data.size() - (jb_b_ok + 2));

            append_u8(bin_data, 0xB9); append_u32(bin_data, 4000);
            append_u8(bin_data, 0x29); append_u8(bin_data, 0xF9); // sub ecx, edi
            size_t jbe_c_none = bin_data.size();
            append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE9); append_u8(bin_data, 2); // shr ecx, 2
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x000B8000); // add edi, 0xB8000
            append_u8(bin_data, 0xFC); // cld
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720); // space
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB); // rep stosd
            bin_data[jbe_c_none + 1] = (uint8_t)(bin_data.size() - (jbe_c_none + 2));

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091014);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFB); append_u32(bin_data, 4000);
            size_t jb_c_ok = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 160);
            bin_data[jb_c_ok + 1] = (uint8_t)(bin_data.size() - (jb_c_ok + 2));
            append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_update_cursor - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0xC3); // ret

            // 8.13. Helper: helper_reboot (Reliable PCI 0xCF9 + 8042 0x64 + Triple fault)
            uint32_t addr_reboot = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CF9); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x06); append_u8(bin_data, 0xEE); // out dx, al
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0xFE); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x64);
            append_u8(bin_data, 0x6A); append_u8(bin_data, 0x00); append_u8(bin_data, 0x6A); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x01); append_u8(bin_data, 0x1C); append_u8(bin_data, 0x24); // lidt [esp]
            append_u8(bin_data, 0xCC); // int 3
            append_u8(bin_data, 0xFA); append_u8(bin_data, 0xF4);
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD);

            // 8.14. Helper: helper_poweroff (QEMU 0x604, Bochs 0xB004, VBox 0x4004, APM)
            uint32_t addr_poweroff = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, 0x2000);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0604); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0xB004); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, 0x3400);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x4004); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xFA); append_u8(bin_data, 0xF4);
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD);

            // 8.15. Helper: helper_vfs_cr_folder (ESI = name)
            uint32_t addr_vfs_cr_folder = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            append_u8(bin_data, 0x85); append_u8(bin_data, 0xF6); // test esi, esi
            size_t jz_cf_ret = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0); // cmp byte [esi], 0
            size_t jz_cf_ret2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Append new entry:
            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00093000); // mov eax, [0x00093000]
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 80); // cmp eax, 80
            size_t jae_cf_done = bin_data.size(); append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC7); // mov edi, eax
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE7); append_u8(bin_data, 8); // shl edi, 8
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x00093004); // add edi, 0x00093004

            // Zero 256 bytes
            append_u8(bin_data, 0x57); append_u8(bin_data, 0x50);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 64);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0x58); append_u8(bin_data, 0x5F);

            // Copy name into edi+0 (max 31 chars)
            append_u8(bin_data, 0x57); append_u8(bin_data, 0x56);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 31);
            size_t loop_cp_n = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cp_n = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_cp_n - (bin_data.size() + 1)));
            bin_data[jz_cp_n + 1] = (uint8_t)(bin_data.size() - (jz_cp_n + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5F);

            // Copy cwd (0x00091300) into edi+32
            append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 32);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 31);
            size_t loop_cp_p = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cp_p = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_cp_p - (bin_data.size() + 1)));
            bin_data[jz_cp_p + 1] = (uint8_t)(bin_data.size() - (jz_cp_p + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5F);

            // is_dir = 1
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x47); append_u8(bin_data, 64); append_u8(bin_data, 1);
            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00093000); // inc [0x00093000]

            size_t pos_cf_done = bin_data.size();
            bin_data[jz_cf_ret + 1] = (uint8_t)(pos_cf_done - (jz_cf_ret + 2));
            bin_data[jz_cf_ret2 + 1] = (uint8_t)(pos_cf_done - (jz_cf_ret2 + 2));
            bin_data[jae_cf_done + 1] = (uint8_t)(pos_cf_done - (jae_cf_done + 2));

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // 8.16. Helper: helper_vfs_cr_file (ESI = name, EBX = content)
            uint32_t addr_vfs_cr_file = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            append_u8(bin_data, 0x85); append_u8(bin_data, 0xF6); // test esi, esi
            size_t jz_cfl_ret = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0);
            size_t jz_cfl_ret2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00093000); // mov eax, [0x00093000]
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 80);
            size_t jae_cfl_done = bin_data.size(); append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC7);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE7); append_u8(bin_data, 8);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x00093004);

            // Zero 256 bytes
            append_u8(bin_data, 0x57); append_u8(bin_data, 0x50);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 64);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0x58); append_u8(bin_data, 0x5F);

            // Copy name into edi+0 (max 31 chars)
            append_u8(bin_data, 0x57); append_u8(bin_data, 0x56);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 31);
            size_t loop_cfl_n = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cfl_n = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_cfl_n - (bin_data.size() + 1)));
            bin_data[jz_cfl_n + 1] = (uint8_t)(bin_data.size() - (jz_cfl_n + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5F);

            // Copy cwd (0x00091300) into edi+32
            append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 32);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 31);
            size_t loop_cfl_p = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cfl_p = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_cfl_p - (bin_data.size() + 1)));
            bin_data[jz_cfl_p + 1] = (uint8_t)(bin_data.size() - (jz_cfl_p + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5F);

            // is_dir = 0
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x47); append_u8(bin_data, 64); append_u8(bin_data, 0);

            // Copy content (EBX) into edi+68 (max 180 chars)
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xDB); // test ebx, ebx
            size_t jz_no_cont = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x7F); append_u8(bin_data, 68); // lea edi, [edi + 68]
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xDE); // mov esi, ebx
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC9); // xor ecx, ecx (length counter)
            size_t loop_cfl_c = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cfl_c = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x41); // inc ecx
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xF9); append_u32(bin_data, 180);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_cfl_c - (bin_data.size() + 1)));
            bin_data[jz_cfl_c + 1] = (uint8_t)(bin_data.size() - (jz_cfl_c + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5F); // restore edi
            append_u8(bin_data, 0x66); append_u8(bin_data, 0x89); append_u8(bin_data, 0x4F); append_u8(bin_data, 66); // mov [edi + 66], cx

            size_t pos_no_cont = bin_data.size();
            bin_data[jz_no_cont + 1] = (uint8_t)(pos_no_cont - (jz_no_cont + 2));

            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00093000); // inc [0x00093000]

            size_t pos_cfl_done = bin_data.size();
            bin_data[jz_cfl_ret + 1] = (uint8_t)(pos_cfl_done - (jz_cfl_ret + 2));
            bin_data[jz_cfl_ret2 + 1] = (uint8_t)(pos_cfl_done - (jz_cfl_ret2 + 2));
            bin_data[jae_cfl_done + 1] = (uint8_t)(pos_cfl_done - (jae_cfl_done + 2));

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // 8.17. Helper: helper_vfs_pwd
            uint32_t addr_vfs_pwd = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            append_u8(bin_data, 0x5E);
            append_u8(bin_data, 0xC3);

            // 8.18. Helper: helper_vfs_cd (ESI = target dir string)
            uint32_t addr_vfs_cd = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            append_u8(bin_data, 0x85); append_u8(bin_data, 0xF6); // test esi, esi
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t jz_cd_done1 = bin_data.size(); append_u32(bin_data, 0);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0); // cmp byte [esi], 0
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t jz_cd_done2 = bin_data.size(); append_u32(bin_data, 0);

            // Check if ESI == "/"
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, '/');
            size_t jne_cd_chk_dotdot = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, 0);
            size_t jne_cd_chk_dotdot2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            // Reset to "/"
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091300); append_u8(bin_data, '/');
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_slash = bin_data.size(); append_u32(bin_data, 0);

            // Check if ESI == ".."
            size_t pos_cd_chk_dotdot = bin_data.size();
            bin_data[jne_cd_chk_dotdot + 1] = (uint8_t)(pos_cd_chk_dotdot - (jne_cd_chk_dotdot + 2));
            bin_data[jne_cd_chk_dotdot2 + 1] = (uint8_t)(pos_cd_chk_dotdot - (jne_cd_chk_dotdot2 + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, '.');
            size_t jne_cd_find_folder = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, '.');
            size_t jne_cd_find_folder2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, 0);
            size_t jne_cd_find_folder3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Go up one dir in 0x00091300
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t jz_cd_already_root = bin_data.size(); append_u32(bin_data, 0);

            // Find end of cwd
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00091300);
            size_t loop_cd_end = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3F); append_u8(bin_data, 0);
            size_t jz_cd_found_end = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x47); // inc edi
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_end - (bin_data.size() + 1)));

            size_t pos_cd_found_end = bin_data.size();
            bin_data[jz_cd_found_end + 1] = (uint8_t)(pos_cd_found_end - (jz_cd_found_end + 2));
            // Scan backwards for '/'
            size_t loop_cd_back = bin_data.size();
            append_u8(bin_data, 0x4F); // dec edi
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFF); append_u32(bin_data, 0x00091300);
            size_t jbe_cd_root = bin_data.size(); append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3F); append_u8(bin_data, '/');
            size_t je_cd_cut = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_back - (bin_data.size() + 1)));

            size_t pos_cd_root = bin_data.size();
            bin_data[jbe_cd_root + 1] = (uint8_t)(pos_cd_root - (jbe_cd_root + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091300); append_u8(bin_data, '/');
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_up = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_cd_cut = bin_data.size();
            bin_data[je_cd_cut + 1] = (uint8_t)(pos_cd_cut - (je_cd_cut + 2));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFF); append_u32(bin_data, 0x00091300);
            size_t jne_cd_trunc = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_cut = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_cd_trunc = bin_data.size();
            bin_data[jne_cd_trunc + 1] = (uint8_t)(pos_cd_trunc - (jne_cd_trunc + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_trunc = bin_data.size(); append_u32(bin_data, 0);

            // Search folder in RamFS:
            size_t pos_cd_find_folder = bin_data.size();
            bin_data[jne_cd_find_folder + 1] = (uint8_t)(pos_cd_find_folder - (jne_cd_find_folder + 2));
            bin_data[jne_cd_find_folder2 + 1] = (uint8_t)(pos_cd_find_folder - (jne_cd_find_folder2 + 2));
            bin_data[jne_cd_find_folder3 + 1] = (uint8_t)(pos_cd_find_folder - (jne_cd_find_folder3 + 2));

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC9);
            size_t jz_cd_not_found = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2); // edx = 0

            size_t loop_cd_scan = bin_data.size();
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD7);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE7); append_u8(bin_data, 8);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x00093004);

            // is_dir == 1 (edi + 64)
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7F); append_u8(bin_data, 64); append_u8(bin_data, 1);
            size_t jne_cd_next = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // parent == cwd (0x00091300)
            append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x47); append_u8(bin_data, 32);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E);
            size_t jz_cd_next = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // name == ESI
            append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF8);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF3);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E);
            size_t jnz_cd_match = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            size_t pos_cd_next = bin_data.size();
            bin_data[jne_cd_next + 1] = (uint8_t)(pos_cd_next - (jne_cd_next + 2));
            bin_data[jz_cd_next + 1] = (uint8_t)(pos_cd_next - (jz_cd_next + 2));
            append_u8(bin_data, 0x42); // inc edx
            append_u8(bin_data, 0x3B); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_scan - (bin_data.size() + 1)));

            // Not found
            size_t pos_cd_not_found = bin_data.size();
            bin_data[jz_cd_not_found + 1] = (uint8_t)(pos_cd_not_found - (jz_cd_not_found + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0C); // Light Red
            emit_print_str_lit("cd: папка не найдена: ");
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Light Green
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_fail = bin_data.size(); append_u32(bin_data, 0);

            // Match found! Update cwd
            size_t pos_cd_match = bin_data.size();
            bin_data[jnz_cd_match + 1] = (uint8_t)(pos_cd_match - (jnz_cd_match + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
            size_t jz_cd_append_to_slash = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Non-root: find end of cwd, append '/', then copy name
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00091300);
            size_t loop_cd_end2 = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3F); append_u8(bin_data, 0);
            size_t jz_cd_found_end2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_end2 - (bin_data.size() + 1)));

            size_t pos_cd_found_end2 = bin_data.size();
            bin_data[jz_cd_found_end2 + 1] = (uint8_t)(pos_cd_found_end2 - (jz_cd_found_end2 + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, '/');
            append_u8(bin_data, 0x47); // inc edi
            // copy name from ESI
            size_t loop_cd_cp_name1 = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cd_cp_name1 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_cp_name1 - (bin_data.size() + 1)));
            bin_data[jz_cd_cp_name1 + 1] = (uint8_t)(bin_data.size() - (jz_cd_cp_name1 + 2));
            append_u8(bin_data, 0xE9);
            size_t jmp_cd_done_ok1 = bin_data.size(); append_u32(bin_data, 0);

            // Root slash: copy name to 0x00091301
            size_t pos_cd_append_to_slash = bin_data.size();
            bin_data[jz_cd_append_to_slash + 1] = (uint8_t)(pos_cd_append_to_slash - (jz_cd_append_to_slash + 2));
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00091301);
            size_t loop_cd_cp_name2 = bin_data.size();
            append_u8(bin_data, 0xAC); append_u8(bin_data, 0xAA); append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            size_t jz_cd_cp_name2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_cp_name2 - (bin_data.size() + 1)));
            bin_data[jz_cd_cp_name2 + 1] = (uint8_t)(bin_data.size() - (jz_cd_cp_name2 + 2));

            // Done CD
            size_t pos_cd_all_done = bin_data.size();
            write_u32_le(bin_data, jz_cd_done1, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jz_cd_done1 + 4)));
            write_u32_le(bin_data, jz_cd_done2, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jz_cd_done2 + 4)));
            write_u32_le(bin_data, jmp_cd_done_slash, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_slash + 4)));
            write_u32_le(bin_data, jz_cd_already_root, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jz_cd_already_root + 4)));
            write_u32_le(bin_data, jmp_cd_done_up, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_up + 4)));
            write_u32_le(bin_data, jmp_cd_done_cut, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_cut + 4)));
            write_u32_le(bin_data, jmp_cd_done_trunc, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_trunc + 4)));
            write_u32_le(bin_data, jmp_cd_done_fail, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_fail + 4)));
            write_u32_le(bin_data, jmp_cd_done_ok1, (uint32_t)((int32_t)pos_cd_all_done - (int32_t)(jmp_cd_done_ok1 + 4)));

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // 8.19. Helper: helper_vfs_cat (ESI = filename)
            uint32_t addr_vfs_cat = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            append_u8(bin_data, 0x85); append_u8(bin_data, 0xF6);
            size_t jz_cat_done = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0);
            size_t jz_cat_done2 = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC9);
            size_t jz_cat_not_found = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2); // edx = 0

            size_t loop_cat_scan = bin_data.size();
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD7);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE7); append_u8(bin_data, 8);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x00093004);

            // is_dir == 0 (edi + 64)
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7F); append_u8(bin_data, 64); append_u8(bin_data, 0);
            size_t jne_cat_next = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // parent == cwd (0x00091300)
            append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x47); append_u8(bin_data, 32);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E);
            size_t jz_cat_next = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // name == ESI
            append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF8);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF3);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E);
            size_t jnz_cat_found = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            size_t pos_cat_next = bin_data.size();
            bin_data[jne_cat_next + 1] = (uint8_t)(pos_cat_next - (jne_cat_next + 2));
            bin_data[jz_cat_next + 1] = (uint8_t)(pos_cat_next - (jz_cat_next + 2));
            append_u8(bin_data, 0x42);
            append_u8(bin_data, 0x3B); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_cat_scan - (bin_data.size() + 1)));

            // Cat not found
            size_t pos_cat_not_found = bin_data.size();
            bin_data[jz_cat_not_found + 1] = (uint8_t)(pos_cat_not_found - (jz_cat_not_found + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0C);
            emit_print_str_lit("cat: файл не найден: ");
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            size_t jmp_cat_done_fail = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Found! Print content at edi+68
            size_t pos_cat_found = bin_data.size();
            bin_data[jnz_cat_found + 1] = (uint8_t)(pos_cat_found - (jnz_cat_found + 2));
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x77); append_u8(bin_data, 68); // lea esi, [edi + 68]
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0F); // White
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // restore Green

            size_t pos_cat_all_done = bin_data.size();
            bin_data[jz_cat_done + 1] = (uint8_t)(pos_cat_all_done - (jz_cat_done + 2));
            bin_data[jz_cat_done2 + 1] = (uint8_t)(pos_cat_all_done - (jz_cat_done2 + 2));
            bin_data[jmp_cat_done_fail + 1] = (uint8_t)(pos_cat_all_done - (jmp_cat_done_fail + 2));

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // 8.20. Helper: helper_vfs_ls (EAX = flags ptr or 0)
            uint32_t addr_vfs_ls = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            // check if flags has "-a"
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xDB); // ebx = 0 (show_all)
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0); // test eax, eax
            size_t jz_ls_no_flags = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x38); append_u8(bin_data, '-');
            size_t jne_ls_no_flags = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x78); append_u8(bin_data, 1); append_u8(bin_data, 'a');
            size_t jne_ls_no_flags2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 1); // ebx = 1 (show_all)

            size_t pos_ls_flags_done = bin_data.size();
            bin_data[jz_ls_no_flags + 1] = (uint8_t)(pos_ls_flags_done - (jz_ls_no_flags + 2));
            bin_data[jne_ls_no_flags + 1] = (uint8_t)(pos_ls_flags_done - (jne_ls_no_flags + 2));
            bin_data[jne_ls_no_flags2 + 1] = (uint8_t)(pos_ls_flags_done - (jne_ls_no_flags2 + 2));

            // Print header: "Содержимое папки " + cwd + ":\n"
            emit_print_str_lit("Содержимое папки ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(":\n");

            // If show_all: print . and ..
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xDB);
            size_t jz_ls_no_dots = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B); // Cyan
            emit_print_str_lit("  <DIR>      .\n  <DIR>      ..\n");

            size_t pos_ls_no_dots = bin_data.size();
            bin_data[jz_ls_no_dots + 1] = (uint8_t)(pos_ls_no_dots - (jz_ls_no_dots + 2));

            // Scan RamFS table
            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x0D); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC9);
            size_t jz_ls_done = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2); // edx = 0

            size_t loop_ls_scan = bin_data.size();
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD7);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE7); append_u8(bin_data, 8);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xC7); append_u32(bin_data, 0x00093004);

            // parent == cwd (0x00091300)
            append_u8(bin_data, 0x53); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x47); append_u8(bin_data, 32);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 0x00091300);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5B);
            size_t jz_ls_next = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // hidden check
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7F); append_u8(bin_data, 65); append_u8(bin_data, 1);
            size_t jne_ls_visible = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xDB); // if hidden && !show_all -> skip
            size_t jz_ls_next_hidden = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            size_t pos_ls_visible = bin_data.size();
            bin_data[jne_ls_visible + 1] = (uint8_t)(pos_ls_visible - (jne_ls_visible + 2));

            // is_dir == 1 ?
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7F); append_u8(bin_data, 64); append_u8(bin_data, 1);
            size_t jne_ls_file = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Print DIR in Cyan
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B); // Cyan
            emit_print_str_lit("  <DIR>      ");
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFE); // mov esi, edi
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            size_t jmp_ls_entry_done = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Print FILE in Light Green
            size_t pos_ls_file = bin_data.size();
            bin_data[jne_ls_file + 1] = (uint8_t)(pos_ls_file - (jne_ls_file + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Light Green
            emit_print_str_lit("  <FILE>     ");
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFE); // mov esi, edi
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("  (");
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB7); append_u8(bin_data, 0x47); append_u8(bin_data, 66); // movzx eax, word [edi + 66]
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" Б)\n");

            size_t pos_ls_entry_done = bin_data.size();
            bin_data[jmp_ls_entry_done + 1] = (uint8_t)(pos_ls_entry_done - (jmp_ls_entry_done + 2));
            bin_data[jz_ls_next + 1] = (uint8_t)(pos_ls_entry_done - (jz_ls_next + 2));
            bin_data[jz_ls_next_hidden + 1] = (uint8_t)(pos_ls_entry_done - (jz_ls_next_hidden + 2));

            append_u8(bin_data, 0x42); // inc edx
            append_u8(bin_data, 0x3B); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_ls_scan - (bin_data.size() + 1)));

            size_t pos_ls_done = bin_data.size();
            bin_data[jz_ls_done + 1] = (uint8_t)(pos_ls_done - (jz_ls_done + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // restore Green

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            // 8.21. Helper: helper_vd_desktop (Vector Display+ Interactive Bare-Metal GUI Desktop)
            uint32_t addr_vd_desktop = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            // Set tab = 1 at 0x00091380
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 1);

            size_t loop_desktop_redraw = bin_data.size();
            // Clear entire VGA 4000 bytes with space
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B8000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 1000);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            // Reset cursor to 0
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091000); append_u32(bin_data, 0);

            // Line 0: Header
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x1F); // White on Blue
            emit_print_str_lit(" [ GrassGreen VD+ Desktop Engine v4.3.0 | RQWos Core ]                          ");

            // Line 1: Top border
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Light Green
            emit_print_str_lit("+==============================================================================+");

            // Line 2: Tabs header
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); // mov al, [0x00091380]
            append_u8(bin_data, 0x3C); append_u8(bin_data, 1);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85);
            size_t jne_dt_tab2 = bin_data.size(); append_u32(bin_data, 0);
            emit_print_str_lit("|  [* 1. СИСТЕМА *]       [ 2. ФАЙЛЫ ]       [ 3. ЖЕЛЕЗО ]                     |");
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_tabs_done = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_tab2 = bin_data.size();
            write_u32_le(bin_data, jne_dt_tab2, (uint32_t)((int32_t)pos_dt_tab2 - (int32_t)(jne_dt_tab2 + 4)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 2);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85);
            size_t jne_dt_tab3 = bin_data.size(); append_u32(bin_data, 0);
            emit_print_str_lit("|  [ 1. СИСТЕМА ]       [* 2. ФАЙЛЫ *]       [ 3. ЖЕЛЕЗО ]                     |");
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_tabs_done2 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_tab3 = bin_data.size();
            write_u32_le(bin_data, jne_dt_tab3, (uint32_t)((int32_t)pos_dt_tab3 - (int32_t)(jne_dt_tab3 + 4)));
            emit_print_str_lit("|  [ 1. СИСТЕМА ]       [ 2. ФАЙЛЫ ]       [* 3. ЖЕЛЕЗО *]                     |");

            size_t pos_dt_tabs_done = bin_data.size();
            write_u32_le(bin_data, jmp_dt_tabs_done, (uint32_t)((int32_t)pos_dt_tabs_done - (int32_t)(jmp_dt_tabs_done + 4)));
            write_u32_le(bin_data, jmp_dt_tabs_done2, (uint32_t)((int32_t)pos_dt_tabs_done - (int32_t)(jmp_dt_tabs_done2 + 4)));

            // Line 3: Middle border
            emit_print_str_lit("+------------------------------------------------------------------------------+");

            // Tab Content
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 1);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85);
            size_t jne_dt_body2 = bin_data.size(); append_u32(bin_data, 0);

            // Tab 1 Body (18 lines)
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Операционная система:  RQWos (GrassGreen Kernel v4.3.0)                     |");
            emit_print_str_lit("|  Архитектура ядра:      x86 Protected Mode (Multiboot Compliant)             |");
            emit_print_str_lit("|  Графическая подсистема:VD+ (Vector Display Plus TUI/GUI Engine)             |");
            emit_print_str_lit("|  Файловая структура:    RamFS (Виртуальная структура активна)                |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Состояние памяти ОЗУ:                                                       |");
            emit_print_str_lit("|  [================================....]  4 МБ / 128 МБ                       |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Управление окнами:                                                          |");
            emit_print_str_lit("|    [1] - Общие сведения об ОС                                                |");
            emit_print_str_lit("|    [2] - Проводник файлов (RamFS)                                            |");
            emit_print_str_lit("|    [3] - Аппаратный мониторинг процессора и устройств                        |");
            emit_print_str_lit("|    [ESC] - Выход обратно в терминал RQWos                                    |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Статус сети:   Подключено к шлюзу 10.0.2.2 (DNS: 10.0.2.3)                  |");
            emit_print_str_lit("|  Пользователь:  m1oky (Права: Root / Superuser)                              |");
            emit_print_str_lit("|                                                                              |");
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_body_done = bin_data.size(); append_u32(bin_data, 0);

            // Tab 2 Body (18 lines)
            size_t pos_dt_body2 = bin_data.size();
            write_u32_le(bin_data, jne_dt_body2, (uint32_t)((int32_t)pos_dt_body2 - (int32_t)(jne_dt_body2 + 4)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 2);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85);
            size_t jne_dt_body3 = bin_data.size(); append_u32(bin_data, 0);

            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Проводник файлов RamFS:                                                     |");
            emit_print_str_lit("|  +- Содержимое папки / -------------+ +- Просмотр файла -------------------+ |");
            emit_print_str_lit("|  |  [DIR]   system                  | | Имя:     readme.txt                | |");
            emit_print_str_lit("|  |  [DIR]   users                   | | Размер:  41 байт                   | |");
            emit_print_str_lit("|  |  [DIR]   apps                    | |                                    | |");
            emit_print_str_lit("|  |  [FILE]  welcome.txt (32 Б)      | | Содержимое:                        | |");
            emit_print_str_lit("|  |  [FILE]  desktop.ini (скрытый)   | |  \"Welcome to RQWos on              | |");
            emit_print_str_lit("|  |                                  | |   GrassGreen Bare-Metal!\"          | |");
            emit_print_str_lit("|  +----------------------------------+ +------------------------------------+ |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Для навигации в консоли используйте команды: cd, ls, cat                    |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Примонтированные накопители:                                                |");
            emit_print_str_lit("|    [hd0]  ATA Primary Master  - QEMU HARDDISK (512 ГБ)                       |");
            emit_print_str_lit("|    [ram0] RamFS High-Speed    - 16 МБ (Виртуальная структура активна)        |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|                                                                              |");
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_body_done2 = bin_data.size(); append_u32(bin_data, 0);

            // Tab 3 Body (18 lines)
            size_t pos_dt_body3 = bin_data.size();
            write_u32_le(bin_data, jne_dt_body3, (uint32_t)((int32_t)pos_dt_body3 - (int32_t)(jne_dt_body3 + 4)));

            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  Аппаратный монитор железа в реальном времени:                               |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|  ЦПУ Вендор:    AuthenticAMD / GenuineIntel                                  |");
            emit_print_str_lit("|  ЦПУ Модель:    QEMU Virtual CPU 2.5+                                        |");
            emit_print_str_lit("|  Ядра / Потоки: 1 ядро / 1 поток (Базовая частота: 2900 МГц)                 |");
            emit_print_str_lit("|  Кэш L1/L2/L3:  256 КБ / 2048 КБ / 16384 КБ                                  |");
            emit_print_str_lit("|  Всего ОЗУ:     128 МБ (DIMM DDR4 3200 МГц)                                  |");
            emit_print_str_lit("|  Свободно ОЗУ:  124 МБ                                                       |");
            emit_print_str_lit("|  Материнская:   QEMU Standard PC (i440FX)                                    |");
            emit_print_str_lit("|  Версия BIOS:   SeaBIOS (04/01/2014)                                         |");
            emit_print_str_lit("|  Видеоадаптер:  QEMU Standard VGA (128 МБ VRAM)                              |");
            emit_print_str_lit("|  Сетевой чип:   Intel 82540EM Gigabit Ethernet (10.0.2.15)                   |");
            emit_print_str_lit("|  Температура:   ЦПУ: 42 C | ГПУ: 45 C                                        |");
            emit_print_str_lit("|  Виртуализация: Включено (VT-x / AMD-V аппаратная поддержка)                 |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|                                                                              |");
            emit_print_str_lit("|                                                                              |");

            size_t pos_dt_body_done = bin_data.size();
            write_u32_le(bin_data, jmp_dt_body_done, (uint32_t)((int32_t)pos_dt_body_done - (int32_t)(jmp_dt_body_done + 4)));
            write_u32_le(bin_data, jmp_dt_body_done2, (uint32_t)((int32_t)pos_dt_body_done - (int32_t)(jmp_dt_body_done2 + 4)));

            // Line 22: Bottom border
            emit_print_str_lit("+==============================================================================+");

            // Line 23: Status bar
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x70); // Black on Gray
            emit_print_str_lit(" [Tab/1,2,3]: Вкладки | [ESC]: Консоль | [R]: Перезагрузка | [P]: Выключение    ");

            // Move cursor to row 24 column 0 (offset 3840)
            append_u8(bin_data, 0xBB); append_u32(bin_data, 3840);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_update_cursor - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Event Loop
            size_t loop_dt_event = bin_data.size();
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_read_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x3C); append_u8(bin_data, '1');
            size_t jne_dt_k2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 1);
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_re1 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_k2 = bin_data.size();
            bin_data[jne_dt_k2 + 1] = (uint8_t)(pos_dt_k2 - (jne_dt_k2 + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, '2');
            size_t jne_dt_k3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 2);
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_re2 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_k3 = bin_data.size();
            bin_data[jne_dt_k3 + 1] = (uint8_t)(pos_dt_k3 - (jne_dt_k3 + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, '3');
            size_t jne_dt_ktab = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 3);
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_re3 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_ktab = bin_data.size();
            bin_data[jne_dt_ktab + 1] = (uint8_t)(pos_dt_ktab - (jne_dt_ktab + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x09); // Tab
            size_t jne_dt_kr = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xFE); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); // inc [0x00091380]
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091380); append_u8(bin_data, 3);
            size_t jbe_dt_tab_ok = bin_data.size(); append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 1);
            size_t pos_dt_tab_ok = bin_data.size();
            bin_data[jbe_dt_tab_ok + 1] = (uint8_t)(pos_dt_tab_ok - (jbe_dt_tab_ok + 2));
            append_u8(bin_data, 0xE9);
            size_t jmp_dt_re4 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_dt_kr = bin_data.size();
            bin_data[jne_dt_kr + 1] = (uint8_t)(pos_dt_kr - (jne_dt_kr + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'r');
            size_t jne_dt_kR = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_dt_kR = bin_data.size();
            bin_data[jne_dt_kR + 1] = (uint8_t)(pos_dt_kR - (jne_dt_kR + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'R');
            size_t jne_dt_kp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_dt_kp = bin_data.size();
            bin_data[jne_dt_kp + 1] = (uint8_t)(pos_dt_kp - (jne_dt_kp + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'p');
            size_t jne_dt_kP = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_dt_kP = bin_data.size();
            bin_data[jne_dt_kP + 1] = (uint8_t)(pos_dt_kP - (jne_dt_kP + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'P');
            size_t jne_dt_kesc = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_dt_kesc = bin_data.size();
            bin_data[jne_dt_kesc + 1] = (uint8_t)(pos_dt_kesc - (jne_dt_kesc + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x1B); // ESC
            size_t jne_dt_loop = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Exit Desktop: Clear screen, restore color, print resume notice
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_clear - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("[VD+] Возврат в командную строку RQWos.\n");
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            size_t pos_dt_loop = bin_data.size();
            bin_data[jne_dt_loop + 1] = (uint8_t)(pos_dt_loop - (jne_dt_loop + 2));
            // Loop back to wait for key
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_dt_event - (bin_data.size() + 1)));

            // Fixup rel32 jumps back to loop_desktop_redraw
            write_u32_le(bin_data, jmp_dt_re1, (uint32_t)((int32_t)loop_desktop_redraw - (int32_t)(jmp_dt_re1 + 4)));
            write_u32_le(bin_data, jmp_dt_re2, (uint32_t)((int32_t)loop_desktop_redraw - (int32_t)(jmp_dt_re2 + 4)));
            write_u32_le(bin_data, jmp_dt_re3, (uint32_t)((int32_t)loop_desktop_redraw - (int32_t)(jmp_dt_re3 + 4)));
            write_u32_le(bin_data, jmp_dt_re4, (uint32_t)((int32_t)loop_desktop_redraw - (int32_t)(jmp_dt_re4 + 4)));

            // 8.22. Helper: helper_bm_exec (ESI = command string)
            uint32_t addr_bm_exec = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            // Skip leading spaces
            size_t loop_bm_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_bm_nonsp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46); // inc esi
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_bm_sp - (bin_data.size() + 1)));

            size_t pos_bm_nonsp = bin_data.size();
            bin_data[jne_bm_nonsp + 1] = (uint8_t)(pos_bm_nonsp - (jne_bm_nonsp + 2));

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t jz_bm_ok = bin_data.size(); append_u32(bin_data, 0);

            // 1. Check "help"
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0); // mov eax, esi
            uint32_t s_help = emit_inline_str("help");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_help);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_ls = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0E); // Yellow
            emit_print_str_lit("Команды системы RQWos (GrassGreen Baremetal v4.3.0):\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            emit_print_str_lit("  ls [-a]     - Список файлов и папок в директории\n");
            emit_print_str_lit("  cd <папка>  - Перейти в папку (напр.: cd system, cd .., cd /)\n");
            emit_print_str_lit("  pwd         - Показать текущую рабочую директорию\n");
            emit_print_str_lit("  cat <файл>  - Просмотреть содержимое файла\n");
            emit_print_str_lit("  disp        - Диспетчер памяти и опрос железа\n");
            emit_print_str_lit("  gui / vd    - Запуск графического интерфейса VD+ Desktop\n");
            emit_print_str_lit("  clear       - Очистить экран (сохраняя приветствие)\n");
            emit_print_str_lit("  reboot      - Мгновенная перезагрузка компьютера\n");
            emit_print_str_lit("  poweroff    - Выключение питания машины\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok1 = bin_data.size(); append_u32(bin_data, 0);

            // 2. Check "ls"
            size_t pos_bm_chk_ls = bin_data.size();
            bin_data[jz_bm_chk_ls + 1] = (uint8_t)(pos_bm_chk_ls - (jz_bm_chk_ls + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 'l');
            size_t jne_bm_chk_cd = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, 's');
            size_t jne_bm_chk_cd2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, 0);
            size_t je_bm_do_ls = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, ' ');
            size_t jne_bm_chk_cd3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // ls with args
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x46); append_u8(bin_data, 3); // lea eax, [esi + 3]
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_ls - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok2 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_bm_do_ls = bin_data.size();
            bin_data[je_bm_do_ls + 1] = (uint8_t)(pos_bm_do_ls - (je_bm_do_ls + 2));
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0); // eax = 0
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_ls - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok3 = bin_data.size(); append_u32(bin_data, 0);

            // 3. Check "cd"
            size_t pos_bm_chk_cd = bin_data.size();
            bin_data[jne_bm_chk_cd + 1] = (uint8_t)(pos_bm_chk_cd - (jne_bm_chk_cd + 2));
            bin_data[jne_bm_chk_cd2 + 1] = (uint8_t)(pos_bm_chk_cd - (jne_bm_chk_cd2 + 2));
            bin_data[jne_bm_chk_cd3 + 1] = (uint8_t)(pos_bm_chk_cd - (jne_bm_chk_cd3 + 2));

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 'c');
            size_t jne_bm_chk_pwd = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, 'd');
            size_t jne_bm_chk_pwd2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, 0);
            size_t je_bm_do_cd_up = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, ' ');
            size_t jne_bm_chk_pwd3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // cd <arg>
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC6); append_u8(bin_data, 3); // add esi, 3
            size_t loop_cd_arg_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_cd_arg_ready = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_arg_sp - (bin_data.size() + 1)));

            size_t pos_cd_arg_ready = bin_data.size();
            bin_data[jne_cd_arg_ready + 1] = (uint8_t)(pos_cd_arg_ready - (jne_cd_arg_ready + 2));
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_cd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok4 = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_bm_do_cd_up = bin_data.size();
            bin_data[je_bm_do_cd_up + 1] = (uint8_t)(pos_bm_do_cd_up - (je_bm_do_cd_up + 2));
            uint32_t s_dotdot2 = emit_inline_str("..");
            append_u8(bin_data, 0xBE); append_u32(bin_data, s_dotdot2);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_cd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok5 = bin_data.size(); append_u32(bin_data, 0);

            // 4. Check "pwd"
            size_t pos_bm_chk_pwd = bin_data.size();
            bin_data[jne_bm_chk_pwd + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd + 2));
            bin_data[jne_bm_chk_pwd2 + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd2 + 2));
            bin_data[jne_bm_chk_pwd3 + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd3 + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_pwd = emit_inline_str("pwd");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_pwd);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_cat = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_pwd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok6 = bin_data.size(); append_u32(bin_data, 0);

            // 5. Check "cat"
            size_t pos_bm_chk_cat = bin_data.size();
            bin_data[jz_bm_chk_cat + 1] = (uint8_t)(pos_bm_chk_cat - (jz_bm_chk_cat + 2));

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 'c');
            size_t jne_bm_chk_disp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, 'a');
            size_t jne_bm_chk_disp2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, 't');
            size_t jne_bm_chk_disp3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 3); append_u8(bin_data, ' ');
            size_t jne_bm_chk_disp4 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC6); append_u8(bin_data, 4); // add esi, 4
            size_t loop_cat_arg_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_cat_arg_ready = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cat_arg_sp - (bin_data.size() + 1)));

            size_t pos_cat_arg_ready = bin_data.size();
            bin_data[jne_cat_arg_ready + 1] = (uint8_t)(pos_cat_arg_ready - (jne_cat_arg_ready + 2));
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vfs_cat - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok7 = bin_data.size(); append_u32(bin_data, 0);

            // 6. Check "disp"
            size_t pos_bm_chk_disp = bin_data.size();
            bin_data[jne_bm_chk_disp + 1] = (uint8_t)(pos_bm_chk_disp - (jne_bm_chk_disp + 2));
            bin_data[jne_bm_chk_disp2 + 1] = (uint8_t)(pos_bm_chk_disp - (jne_bm_chk_disp2 + 2));
            bin_data[jne_bm_chk_disp3 + 1] = (uint8_t)(pos_bm_chk_disp - (jne_bm_chk_disp3 + 2));
            bin_data[jne_bm_chk_disp4 + 1] = (uint8_t)(pos_bm_chk_disp - (jne_bm_chk_disp4 + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_disp = emit_inline_str("disp");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_disp);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_gui = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("Всего ОЗУ:  ");
            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00091020);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" МБ (3200 МГц DIMM)\nЗанято ОЗУ: ");
            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00091024);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" МБ | Кэш: 16 МБ\nСвободно:   ");
            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x00091028);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" МБ\nПлата:      ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091090);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x000910A0);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\nBIOS:       ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091080);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" (");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x000910C0);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(")\nЦПУ:        ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091030);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" (");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091040);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(")\nЯдер/поток: 1 / 1 | 2900 МГц (18% 42 C)\nКэш L1/2/3: 256 КБ / 2048 КБ / 16384 КБ\nВиртуал.:   Включено\nДиск:       QEMU HARDDISK [SSD] 512 ГБ\nСеть:       Intel 82540EM Gigabit Ethernet (10.0.2.15)\nГПУ:        QEMU Standard VGA | VRAM: 128 МБ | 45 C\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok8 = bin_data.size(); append_u32(bin_data, 0);

            // 7. Check "gui" or "vd"
            size_t pos_bm_chk_gui = bin_data.size();
            bin_data[jz_bm_chk_gui + 1] = (uint8_t)(pos_bm_chk_gui - (jz_bm_chk_gui + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_gui = emit_inline_str("gui");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_gui);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jnz_bm_do_gui = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_vd = emit_inline_str("vd");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_vd);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_clear = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            size_t pos_bm_do_gui = bin_data.size();
            bin_data[jnz_bm_do_gui + 1] = (uint8_t)(pos_bm_do_gui - (jnz_bm_do_gui + 2));
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_vd_desktop - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok9 = bin_data.size(); append_u32(bin_data, 0);

            // 8. Check "clear" or "cls"
            size_t pos_bm_chk_clear = bin_data.size();
            bin_data[jz_bm_chk_clear + 1] = (uint8_t)(pos_bm_chk_clear - (jz_bm_chk_clear + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_clear = emit_inline_str("clear");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_clear);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jnz_bm_do_clear = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_cls = emit_inline_str("cls");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_cls);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_rb = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            size_t pos_bm_do_clear = bin_data.size();
            bin_data[jnz_bm_do_clear + 1] = (uint8_t)(pos_bm_do_clear - (jnz_bm_do_clear + 2));
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_clear - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok10 = bin_data.size(); append_u32(bin_data, 0);

            // 9. Check "reboot"
            size_t pos_bm_chk_rb = bin_data.size();
            bin_data[jz_bm_chk_rb + 1] = (uint8_t)(pos_bm_chk_rb - (jz_bm_chk_rb + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_reboot = emit_inline_str("reboot");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_reboot);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_po = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // 10. Check "poweroff"
            size_t pos_bm_chk_po = bin_data.size();
            bin_data[jz_bm_chk_po + 1] = (uint8_t)(pos_bm_chk_po - (jz_bm_chk_po + 2));

            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_poweroff = emit_inline_str("poweroff");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_poweroff);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_unknown = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Unknown command
            size_t pos_bm_unknown = bin_data.size();
            bin_data[jz_bm_unknown + 1] = (uint8_t)(pos_bm_unknown - (jz_bm_unknown + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0C); // Red
            emit_print_str_lit("Команда не найдена: ");
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" (введите 'help' для списка команд)\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0); // eax = 0
            size_t jmp_bm_final_ret = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Command succeeded
            size_t pos_bm_ok = bin_data.size();
            write_u32_le(bin_data, jz_bm_ok, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jz_bm_ok + 4)));
            write_u32_le(bin_data, jmp_bm_ok1, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok1 + 4)));
            write_u32_le(bin_data, jmp_bm_ok2, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok2 + 4)));
            write_u32_le(bin_data, jmp_bm_ok3, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok3 + 4)));
            write_u32_le(bin_data, jmp_bm_ok4, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok4 + 4)));
            write_u32_le(bin_data, jmp_bm_ok5, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok5 + 4)));
            write_u32_le(bin_data, jmp_bm_ok6, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok6 + 4)));
            write_u32_le(bin_data, jmp_bm_ok7, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok7 + 4)));
            write_u32_le(bin_data, jmp_bm_ok8, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok8 + 4)));
            write_u32_le(bin_data, jmp_bm_ok9, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok9 + 4)));
            write_u32_le(bin_data, jmp_bm_ok10, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok10 + 4)));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 1); // eax = 1

            size_t pos_bm_final_ret = bin_data.size();
            bin_data[jmp_bm_final_ret + 1] = (uint8_t)(pos_bm_final_ret - (jmp_bm_final_ret + 2));

            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B);
            append_u8(bin_data, 0xC3); // ret


            // Fixup jump over helpers
            uint32_t after_helpers_offset = (uint32_t)bin_data.size();
            uint32_t rel_helpers = after_helpers_offset - (uint32_t)(jmp_helpers_fixup + 4);
            write_u32_le(bin_data, jmp_helpers_fixup, rel_helpers);

            // Fixup initial font load, ramfs and cursor calls
            write_u32_le(bin_data, fixup_font_addr, addr_cyrillic_font);
            write_u32_le(bin_data, fixup_default_ramfstr_mem_addr, addr_default_ramfs);
            write_u32_le(bin_data, fixup_init_cursor, addr_update_cursor - (KERNEL_LOAD_ADDR + (uint32_t)fixup_init_cursor + 4));

            // Execute inline ASM if present
            if (!asm_machine_code.empty()) {
                for (uint8_t b : asm_machine_code) append_u8(bin_data, b);
            }

            // --- 9. Трансляция байт-кода программы в нативный x86 Protected Mode ---
            struct StrFixup { size_t mc_pos; std::string text; };
            std::vector<StrFixup> str_fixups;

            struct BranchFixup { size_t mc_pos; uint32_t target_bc; };
            std::vector<BranchFixup> branch_fixups;

            std::unordered_map<uint32_t, size_t> bc_to_mc;
            std::unordered_map<std::string, uint32_t> var_slots;
            uint32_t next_var_addr = 0x00098000;

            size_t ip = 0;
            auto readU8 = [&]() -> uint8_t {
                return (ip < bytecode.size()) ? bytecode[ip++] : 0;
            };
            auto readU32 = [&]() -> uint32_t {
                if (ip + 4 > bytecode.size()) { ip = bytecode.size(); return 0; }
                uint32_t val = (uint32_t)bytecode[ip] |
                               ((uint32_t)bytecode[ip + 1] << 8) |
                               ((uint32_t)bytecode[ip + 2] << 16) |
                               ((uint32_t)bytecode[ip + 3] << 24);
                ip += 4;
                return val;
            };
            auto readDouble = [&]() -> double {
                if (ip + 8 > bytecode.size()) { ip = bytecode.size(); return 0.0; }
                double v = 0.0;
                std::memcpy(&v, &bytecode[ip], 8);
                ip += 8;
                return v;
            };
            auto readString = [&]() -> std::string {
                uint32_t len = readU32();
                if (ip + len > bytecode.size()) { ip = bytecode.size(); return ""; }
                std::string s((const char*)&bytecode[ip], len);
                ip += len;
                return s;
            };

            while (ip < bytecode.size()) {
                uint32_t cur_bc = (uint32_t)ip;
                bc_to_mc[cur_bc] = bin_data.size();
                uint8_t op = readU8();

                switch (op) {
                    case 0x00: // OP_HALT
                        append_u8(bin_data, 0xFA); // cli
                        append_u8(bin_data, 0xF4); // hlt
                        append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD); // jmp -3
                        break;
                    case 0x01: { // OP_CONST_NUM
                        double d = readDouble();
                        int32_t val = (int32_t)d;
                        append_u8(bin_data, 0x68); append_u32(bin_data, (uint32_t)val);
                        break;
                    }
                    case 0x02: { // OP_CONST_STR
                        std::string s = readString();
                        append_u8(bin_data, 0x68);
                        str_fixups.push_back({bin_data.size(), s});
                        append_u32(bin_data, 0);
                        break;
                    }
                    case 0x03: { // OP_LOAD_VAR
                        std::string vname = readString();
                        if (vname == "proc_mem" || vname == "ram_used") {
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091024);
                            break;
                        } else if (vname == "ram_total") {
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091020);
                            break;
                        } else if (vname == "ram_free") {
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091028);
                            break;
                        }
                        if (var_slots.find(vname) == var_slots.end()) {
                            var_slots[vname] = next_var_addr;
                            next_var_addr += 256;
                        }
                        uint32_t var_addr = var_slots[vname];
                        // Check type tag at var_addr: 0 = number, 1 = string
                        append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, var_addr); append_u32(bin_data, 0);
                        size_t jne_load_str = bin_data.size();
                        append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
                        // Number: push dword [var_addr + 4]
                        append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, var_addr + 4);
                        size_t jmp_load_done = bin_data.size();
                        append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
                        // String: push (var_addr + 4)
                        bin_data[jne_load_str + 1] = (uint8_t)(bin_data.size() - (jne_load_str + 2));
                        append_u8(bin_data, 0x68); append_u32(bin_data, var_addr + 4);
                        bin_data[jmp_load_done + 1] = (uint8_t)(bin_data.size() - (jmp_load_done + 2));
                        break;
                    }
                    case 0x04: { // OP_STORE_VAR
                        std::string vname = readString();
                        if (var_slots.find(vname) == var_slots.end()) {
                            var_slots[vname] = next_var_addr;
                            next_var_addr += 256;
                        }
                        append_u8(bin_data, 0x5E); // pop esi
                        append_u8(bin_data, 0xBF); append_u32(bin_data, var_slots[vname]); // mov edi, var_addr
                        append_u8(bin_data, 0xE8); // call helper_copy_string
                        append_u32(bin_data, addr_copy_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        break;
                    }
                    case 0x05: // OP_POP
                        append_u8(bin_data, 0x58); // pop eax
                        break;
                    case 0x0E: // OP_NOP
                        append_u8(bin_data, 0x90);
                        break;
                    case 0x10: // OP_ADD
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x01); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x11: // OP_SUB
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x29); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x12: // OP_MUL
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xAF); append_u8(bin_data, 0xC3);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x13: // OP_DIV
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x99); append_u8(bin_data, 0xF7); append_u8(bin_data, 0xFB);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x20: // OP_CMP_EQ
                        append_u8(bin_data, 0x5B); // pop ebx
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0xE8); // call helper_strcmp
                        append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    case 0x21: // OP_CMP_NEQ
                        append_u8(bin_data, 0x5B); // pop ebx
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0xE8); // call helper_strcmp
                        append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        append_u8(bin_data, 0x83); append_u8(bin_data, 0xF0); append_u8(bin_data, 0x01); // xor eax, 1
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    case 0x22: // OP_CMP_LT
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x9C); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x23: // OP_CMP_GT
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x9F); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x24: // OP_CMP_LTE
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x9E); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x25: // OP_CMP_GTE
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x9D); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x30:   // OP_JMP
                    case 0xF0: { // OP_JUMP
                        uint32_t target_bc = readU32();
                        append_u8(bin_data, 0xE9);
                        branch_fixups.push_back({bin_data.size(), target_bc});
                        append_u32(bin_data, 0);
                        break;
                    }
                    case 0x31: { // OP_JMP_IF_FALSE
                        uint32_t target_bc = readU32();
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0); // test eax, eax
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84); // jz rel32
                        branch_fixups.push_back({bin_data.size(), target_bc});
                        append_u32(bin_data, 0);
                        break;
                    }
                    case 0x32:   // OP_CALL_FUNC
                    case 0xF1: { // OP_CALL_USER
                        uint32_t target_bc = readU32();
                        append_u8(bin_data, 0xE8); // call rel32
                        branch_fixups.push_back({bin_data.size(), target_bc});
                        append_u32(bin_data, 0);
                        break;
                    }
                    case 0x33: // OP_RET
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0xC3); // ret
                        break;
                    case 0x40: { // OP_LOG
                        append_u8(bin_data, 0x5E); // pop esi
                        append_u8(bin_data, 0x81); append_u8(bin_data, 0xFE); append_u32(bin_data, 0x00010000);
                        size_t jae_log = bin_data.size();
                        append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);
                        append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0); // mov eax, esi
                        append_u8(bin_data, 0xE8); // call print_num
                        append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        size_t jmp_log_done = bin_data.size();
                        append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
                        bin_data[jae_log + 1] = (uint8_t)(bin_data.size() - (jae_log + 2));
                        append_u8(bin_data, 0xE8); // call print_string
                        append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        bin_data[jmp_log_done + 1] = (uint8_t)(bin_data.size() - (jmp_log_done + 2));
                        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
                        append_u8(bin_data, 0xE8); // call print_char
                        append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        break;
                    }
                    case 0x41: { // OP_LOG_EMPTY
                        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
                        append_u8(bin_data, 0xE8);
                        append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        break;
                    }
                    case 0x42: { // OP_WAIT
                        append_u8(bin_data, 0x59); // pop ecx
                        append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE1); append_u8(bin_data, 16); // shl ecx, 16
                        size_t wait_lbl = bin_data.size();
                        append_u8(bin_data, 0x90); // nop
                        append_u8(bin_data, 0x49); // dec ecx
                        append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(wait_lbl - (bin_data.size() + 1))); // jnz wait_lbl
                        break;
                    }
                    case 0x43: { // OP_ASK
                        append_u8(bin_data, 0x5E); // pop esi (prompt)
                        append_u8(bin_data, 0xE8);
                        append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00092000);
                        append_u8(bin_data, 0xE8);
                        append_u32(bin_data, addr_read_line - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        append_u8(bin_data, 0x68); append_u32(bin_data, 0x00092000);
                        break;
                    }
                    case 0x44: { // OP_PRINT_RAW
                        append_u8(bin_data, 0x5E); // pop esi
                        append_u8(bin_data, 0x81); append_u8(bin_data, 0xFE); append_u32(bin_data, 0x00010000);
                        size_t jae_praw = bin_data.size();
                        append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);
                        append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0); // mov eax, esi
                        append_u8(bin_data, 0xE8); // call print_num
                        append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        size_t jmp_praw_done = bin_data.size();
                        append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
                        bin_data[jae_praw + 1] = (uint8_t)(bin_data.size() - (jae_praw + 2));
                        append_u8(bin_data, 0xE8); // call print_string
                        append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        bin_data[jmp_praw_done + 1] = (uint8_t)(bin_data.size() - (jmp_praw_done + 2));
                        break;
                    }
                    case 0x50: { // OP_CALL_BUILTIN
                        std::string fn = readString();
                        uint8_t argc = readU8();
                        if (fn == "gin" || fn == "input" || fn == "kbd_read_line") {
                            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00092000); // mov edi, 0x00092000
                            append_u8(bin_data, 0xE8); // call read_line
                            append_u32(bin_data, addr_read_line - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x68); append_u32(bin_data, 0x00092000); // push 0x00092000
                        } else if (fn == "clear" || fn == "cls") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_clear - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "clear_all" || fn == "cls_all") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091014); append_u32(bin_data, 160);
                            append_u8(bin_data, 0xFC);
                            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B80A0);
                            append_u8(bin_data, 0xB9); append_u32(bin_data, 960);
                            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720);
                            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
                            append_u8(bin_data, 0xBB); append_u32(bin_data, 160);
                            append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_update_cursor - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "not_clear_on") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 1);
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "not_clear_off") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 0);
                            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091000);
                            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2);
                            append_u8(bin_data, 0xB9); append_u32(bin_data, 160);
                            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1);
                            append_u8(bin_data, 0x85); append_u8(bin_data, 0xD2);
                            size_t jz_col0 = bin_data.size();
                            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
                            append_u8(bin_data, 0x40);
                            bin_data[jz_col0 + 1] = (uint8_t)(bin_data.size() - (jz_col0 + 2));
                            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xE1);
                            append_u8(bin_data, 0x3B); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091014);
                            size_t jbe_skip_off = bin_data.size();
                            append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
                            append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091014);
                            bin_data[jbe_skip_off + 1] = (uint8_t)(bin_data.size() - (jbe_skip_off + 2));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "not_clear") {
                            if (argc >= 1) {
                                append_u8(bin_data, 0x58);
                                for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
                                append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
                                size_t jz_nc_off = bin_data.size();
                                append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
                                append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 1);
                                size_t jmp_nc_done = bin_data.size();
                                append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
                                bin_data[jz_nc_off + 1] = (uint8_t)(bin_data.size() - (jz_nc_off + 2));
                                append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 0);
                                bin_data[jmp_nc_done + 1] = (uint8_t)(bin_data.size() - (jmp_nc_done + 2));
                            } else {
                                append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091010); append_u32(bin_data, 1);
                            }
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                                                } else if (fn == "hw_ram_total" || fn == "ram_total") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091020);
} else if (fn == "hw_ram_used" || fn == "ram_used" || fn == "proc_mem") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091024);
} else if (fn == "hw_ram_free" || fn == "ram_free") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091028);
} else if (fn == "hw_ram_cached") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091164);
} else if (fn == "hw_ram_speed") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091160);
} else if (fn == "hw_ram_temp") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091168);
} else if (fn == "hw_ram_model") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091170);
} else if (fn == "hw_ram_form") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091190);
} else if (fn == "hw_cpu_vendor" || fn == "cpu_vendor") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091030);
} else if (fn == "hw_cpu_brand" || fn == "cpu_brand" || fn == "cpu_model") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091040);
} else if (fn == "hw_cpu_cores" || fn == "cpu_cores") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009102C);
} else if (fn == "hw_cpu_threads" || fn == "hw_cpu_logical") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091100);
} else if (fn == "hw_cpu_sockets") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091104);
} else if (fn == "hw_cpu_family" || fn == "cpu_family") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009102C);
} else if (fn == "hw_cpu_freq" || fn == "cpu_freq") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091108);
} else if (fn == "hw_cpu_base_freq") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009110C);
} else if (fn == "hw_cpu_usage") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091110);
} else if (fn == "hw_cpu_temp") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091114);
} else if (fn == "hw_cpu_cache_l1") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091118);
} else if (fn == "hw_cpu_cache_l2") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009111C);
} else if (fn == "hw_cpu_cache_l3") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091120);
} else if (fn == "hw_cpu_virt") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091124);
} else if (fn == "hw_uptime_str") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091140);
} else if (fn == "hw_uptime_sec") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091150);
} else if (fn == "hw_disk_size") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911A0);
} else if (fn == "hw_disk_free") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911A4);
} else if (fn == "hw_disk_format") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000911A8);
} else if (fn == "hw_disk_model") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000911B8);
} else if (fn == "hw_disk_type") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000911D8);
} else if (fn == "hw_disk_temp") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911E8);
} else if (fn == "hw_disk_read_speed") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911EC);
} else if (fn == "hw_disk_write_speed") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911F0);
} else if (fn == "hw_disk_resp_time") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911F4);
} else if (fn == "hw_disk_usage" || fn == "hw_disk_active") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911F8);
} else if (fn == "hw_disk_is_system") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000911FC);
} else if (fn == "hw_pagefile_total") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091200);
} else if (fn == "hw_pagefile_used") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091204);
} else if (fn == "hw_net_adapter") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091210);
} else if (fn == "hw_net_type") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091230);
} else if (fn == "hw_net_ipv4") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091240);
} else if (fn == "hw_net_ipv6") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091250);
} else if (fn == "hw_net_recv_speed") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091270);
} else if (fn == "hw_net_send_speed") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091274);
} else if (fn == "hw_net_recv_total") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091278);
} else if (fn == "hw_net_send_total") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x0009127C);
} else if (fn == "hw_gpu_name") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091280);
} else if (fn == "hw_gpu_usage") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000912C0);
} else if (fn == "hw_gpu_temp") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000912C4);
} else if (fn == "hw_gpu_vram_total") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000912C8);
} else if (fn == "hw_gpu_vram_used") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000912CC);
} else if (fn == "hw_gpu_vram_shared") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000912D0);
} else if (fn == "hw_board_vendor" || fn == "board_vendor") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091090);
} else if (fn == "hw_board_model" || fn == "board_model") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000910A0);
} else if (fn == "hw_bios_vendor" || fn == "bios_vendor") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091080);
} else if (fn == "hw_bios_date" || fn == "bios_date") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000910C0);
} else if (fn == "hw_bios_version") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x000910D0);
} else if (fn == "hw_pci_count" || fn == "pci_count") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x000910E0);
} else if (fn == "hw_rdtsc" || fn == "rdtsc") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x0F); append_u8(bin_data, 0x31);
    append_u8(bin_data, 0x50);
} else if (fn == "reboot" || fn == "hw_reboot") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "poweroff") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "color") {
    if (argc >= 2) {
        append_u8(bin_data, 0x5B); // pop ebx (bg)
        append_u8(bin_data, 0x58); // pop eax (fg)
        for (int i = 2; i < argc; ++i) append_u8(bin_data, 0x58);
        append_u8(bin_data, 0x83); append_u8(bin_data, 0xE0); append_u8(bin_data, 0x0F);
        append_u8(bin_data, 0x83); append_u8(bin_data, 0xE3); append_u8(bin_data, 0x0F);
        append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE3); append_u8(bin_data, 4);
        append_u8(bin_data, 0x09); append_u8(bin_data, 0xD8);
        append_u8(bin_data, 0xA2); append_u32(bin_data, 0x0009100D); // mov [0x0009100D], al
    } else if (argc >= 1) {
        append_u8(bin_data, 0x58); // pop eax
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
        append_u8(bin_data, 0x83); append_u8(bin_data, 0xE0); append_u8(bin_data, 0x0F);
        append_u8(bin_data, 0xA2); append_u32(bin_data, 0x0009100D); // mov [0x0009100D], al
    }
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "read_key") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_read_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x50); // push eax
} else if (fn == "pwd") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x68); append_u32(bin_data, 0x00091300); // push static root "/"
} else if (fn == "ls") {
    if (argc >= 1) {
        append_u8(bin_data, 0x58); // pop eax (flags/arg)
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
    } else {
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0); // xor eax, eax
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vfs_ls - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "cd") {
    if (argc >= 1) {
        append_u8(bin_data, 0x5E); // pop esi (path)
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
    } else {
        uint32_t s_dd = emit_inline_str("..");
        append_u8(bin_data, 0xBE); append_u32(bin_data, s_dd);
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vfs_cd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "cat" || fn == "read_file") {
    if (argc >= 1) {
        append_u8(bin_data, 0x5E); // pop esi (filename)
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
    } else {
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xF6);
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vfs_cat - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "cr_folder") {
    if (argc >= 1) {
        append_u8(bin_data, 0x5E); // pop esi (name)
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
    } else {
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xF6);
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vfs_cr_folder - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "cr_file") {
    if (argc >= 2) {
        append_u8(bin_data, 0x5B); // pop ebx (content)
        append_u8(bin_data, 0x5E); // pop esi (name)
        for (int i = 2; i < argc; ++i) append_u8(bin_data, 0x58);
    } else if (argc == 1) {
        append_u8(bin_data, 0x5E);
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xDB);
    } else {
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xF6);
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xDB);
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vfs_cr_file - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "virtual_struct" || fn == "physical_struct") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091300); append_u8(bin_data, '/');
    append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091301); append_u8(bin_data, 0);
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "bm_exec") {
    if (argc >= 1) {
        append_u8(bin_data, 0x5E); // pop esi (command line)
        for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x58);
    } else {
        append_u8(bin_data, 0x31); append_u8(bin_data, 0xF6);
    }
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_bm_exec - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x50); // push eax
} else if (fn == "gui" || fn == "vd" || fn == "vd_desktop") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0xE8);
    append_u32(bin_data, addr_vd_desktop - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "copy" || fn == "move" || fn == "delete" || fn == "not_show" || fn == "specSign_show") {
    for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
    append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
} else if (fn == "use_utf" || fn == "use_locale" || fn == "set_layout" || fn == "kbd_set_layout") {
                            for (int i = 0; i < argc; ++i) {
                                append_u8(bin_data, 0x58); // pop eax
                            }
                            // Если eax == 1: включить русскую раскладку (1), иначе английскую (0)
                            append_u8(bin_data, 0x3D); append_u32(bin_data, 1);
                            size_t jne_set_en = bin_data.size();
                            append_u8(bin_data, 0x75); append_u8(bin_data, 0x0E);
                            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100C); append_u8(bin_data, 1);
                            size_t jmp_set_done = bin_data.size();
                            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x0A);
                            bin_data[jne_set_en + 1] = 0x0E;
                            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100C); append_u8(bin_data, 0);
                            bin_data[jmp_set_done + 1] = 0x0A;
                            // Push 1.0 (для OP_POP)
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 0x01);
                        } else {
                            for (int i = 0; i < argc; ++i) {
                                append_u8(bin_data, 0x58); // pop eax
                            }
                        }
                        break;
                    }
                    case 0x60: // OP_LOAD_DB
                        break;
                    case 0x61: { // OP_START_WEB
                        std::string host = readString();
                        break;
                    }
                    case 0x70: { // OP_UI_INIT
                        std::string title = readString();
                        uint32_t w = readU32();
                        uint32_t h = readU32();
                        std::string bg = readString();
                        std::string fon = readString();
                        break;
                    }
                    case 0x71: { // OP_UI_ADD_ELEM
                        std::string type = readString();
                        std::string id = readString();
                        std::string text = readString();
                        uint32_t x = readU32();
                        uint32_t y = readU32();
                        uint32_t w = readU32();
                        uint32_t h = readU32();
                        uint32_t align = readU32();
                        std::string color = readString();
                        uint32_t fsize = readU32();
                        std::string fstyle = readString();
                        uint8_t glass = readU8();
                        std::string run = readString();
                        break;
                    }
                    case 0x72: { // OP_UI_SHOW
                        // Launch freestanding interactive TUI/GUI Desktop
                        append_u8(bin_data, 0xE8);
                        append_u32(bin_data, addr_vd_desktop - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                        break;
                    }
                    case 0x73: { // OP_UI_CMD
                        std::string cmd_line = readString();
                        break;
                    }
                    case 0x80: { // OP_NEW_ARRAY
                        uint32_t count = readU32();
                        for (uint32_t i = 0; i < count; ++i) append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x68); append_u32(bin_data, 0x00098000);
                        break;
                    }
                    case 0x81: { // OP_GET_INDEX
                        append_u8(bin_data, 0x5B); // pop idx
                        append_u8(bin_data, 0x58); // pop arr
                        append_u8(bin_data, 0x6A); append_u8(bin_data, 0); // push 0
                        break;
                    }
                    case 0x82: { // OP_SET_INDEX
                        append_u8(bin_data, 0x58); // pop val
                        append_u8(bin_data, 0x5B); // pop idx
                        append_u8(bin_data, 0x5E); // pop arr
                        break;
                    }
                    case 0x83: { // OP_ARRAY_PUSH
                        append_u8(bin_data, 0x58); // pop val
                        append_u8(bin_data, 0x5E); // pop arr
                        break;
                    }
                    case 0x84: { // OP_ARRAY_POP
                        append_u8(bin_data, 0x5E); // pop arr
                        append_u8(bin_data, 0x6A); append_u8(bin_data, 0); // push 0
                        break;
                    }
                    case 0x85: { // OP_NEW_STRUCT
                        std::string sname = readString();
                        append_u8(bin_data, 0x68); append_u32(bin_data, 0x00098000);
                        break;
                    }
                    case 0x86: { // OP_GET_FIELD
                        std::string fname = readString();
                        append_u8(bin_data, 0x58); // pop struct
                        append_u8(bin_data, 0x6A); append_u8(bin_data, 0); // push 0
                        break;
                    }
                    case 0x87: { // OP_SET_FIELD
                        std::string fname = readString();
                        append_u8(bin_data, 0x58); // pop val
                        append_u8(bin_data, 0x5E); // pop struct
                        break;
                    }
                    case 0x90: { // OP_ADD_CONST
                        double d = readDouble();
                        int32_t val = (int32_t)d;
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0x05); append_u32(bin_data, (uint32_t)val); // add eax, val
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    }
                    case 0x91: { // OP_SUB_CONST
                        double d = readDouble();
                        int32_t val = (int32_t)d;
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0x2D); append_u32(bin_data, (uint32_t)val); // sub eax, val
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    }
                    case 0x92: { // OP_MUL_CONST
                        double d = readDouble();
                        int32_t val = (int32_t)d;
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0x69); append_u8(bin_data, 0xC0); append_u32(bin_data, (uint32_t)val); // imul eax, eax, val
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    }
                    case 0x93: { // OP_DIV_CONST
                        double d = readDouble();
                        int32_t val = (int32_t)d;
                        if (val == 0) val = 1;
                        append_u8(bin_data, 0x58); // pop eax
                        append_u8(bin_data, 0x99); // cdq
                        append_u8(bin_data, 0xBB); append_u32(bin_data, (uint32_t)val); // mov ebx, val
                        append_u8(bin_data, 0xF7); append_u8(bin_data, 0xFB); // idiv ebx
                        append_u8(bin_data, 0x50); // push eax
                        break;
                    }
                    case 0x94: { // OP_INC_VAR
                        std::string vname = readString();
                        if (var_slots.find(vname) == var_slots.end()) {
                            var_slots[vname] = next_var_addr;
                            next_var_addr += 256;
                        }
                        uint32_t vaddr = var_slots[vname] + 4;
                        append_u8(bin_data, 0xFF); append_u8(bin_data, 0x05); append_u32(bin_data, vaddr);
                        break;
                    }
                    case 0x95: { // OP_DEC_VAR
                        std::string vname = readString();
                        if (var_slots.find(vname) == var_slots.end()) {
                            var_slots[vname] = next_var_addr;
                            next_var_addr += 256;
                        }
                        uint32_t vaddr = var_slots[vname] + 4;
                        append_u8(bin_data, 0xFF); append_u8(bin_data, 0x0D); append_u32(bin_data, vaddr);
                        break;
                    }
                    case 0xA0: { // OP_THREAD_SPAWN
                        uint32_t addr = readU32();
                        break;
                    }
                    case 0xA1: { // OP_ASYNC_SPAWN
                        uint32_t addr = readU32();
                        break;
                    }
                    default:
                        break;
                }
            }

            bc_to_mc[(uint32_t)bytecode.size()] = bin_data.size();

            // Исправление адресов переходов
            for (const auto& bf : branch_fixups) {
                auto it = bc_to_mc.find(bf.target_bc);
                if (it != bc_to_mc.end()) {
                    uint32_t target_mc = (uint32_t)it->second;
                    uint32_t src_mc = (uint32_t)(bf.mc_pos + 4);
                    int32_t rel = (int32_t)(target_mc - src_mc);
                    write_u32_le(bin_data, bf.mc_pos, (uint32_t)rel);
                }
            }

            // Безопасный останов в конце программы
            append_u8(bin_data, 0xFA); // cli
            append_u8(bin_data, 0xF4); // hlt
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD); // jmp -3

            // Размещение строковых констант
            std::unordered_map<std::string, uint32_t> emitted_strings;
            for (const auto& sf : str_fixups) {
                if (emitted_strings.find(sf.text) == emitted_strings.end()) {
                    uint32_t str_linear_addr = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
                    emitted_strings[sf.text] = str_linear_addr;
                    for (char c : sf.text) append_u8(bin_data, (uint8_t)c);
                    append_u8(bin_data, 0); // null
                }
                write_u32_le(bin_data, sf.mc_pos, emitted_strings[sf.text]);
            }
        } else {
            if (!asm_machine_code.empty()) {
                for (uint8_t b : asm_machine_code) append_u8(bin_data, b);
            }
            append_u8(bin_data, 0xFA); // cli
            append_u8(bin_data, 0xF4); // hlt
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD); // jmp -3
        }

        // 1.10. Встраивание байт-кода программы для VM/JIT ядра
        while (bin_data.size() % 16 != 0) append_u8(bin_data, 0x90);

        const char KERNEL_MAGIC[8] = {'G','R','A','S','S','B','I','N'};
        for (int i = 0; i < 8; ++i) append_u8(bin_data, KERNEL_MAGIC[i]);
        uint32_t bc_sz = (uint32_t)bytecode.size();
        append_u32(bin_data, bc_sz);
        bin_data.insert(bin_data.end(), bytecode.begin(), bytecode.end());

        // Запись в выходной файл .bin
        std::ofstream out(output_bin, std::ios::binary);
        if (!out.is_open()) {
            if (lang_ru) std::cerr << "[GrassError]: Не удалось создать файл ядра -> " << output_bin << "\n";
            else std::cerr << "[GrassError]: Failed to create kernel file -> " << output_bin << "\n";
            return false;
        }
        out.write((const char*)bin_data.data(), bin_data.size());
        out.close();

        if (lang_ru) {
            std::cout << "[GrassKernel]: Скомпилировано ядро операционной системы ("
                      << bin_data.size() << " байт) -> " << output_bin << "\n"
                      << "               Заголовок Multiboot 1/2: OK | Точка входа: 0x00100040\n";
        } else {
            std::cout << "[GrassKernel]: Compiled OS bare-metal kernel ("
                      << bin_data.size() << " bytes) -> " << output_bin << "\n"
                      << "               Multiboot 1/2 Header: OK | Entry Point: 0x00100040\n";
        }
        return true;
    }

    bool build_driver_sys(const std::string& output_sys,
                          const std::vector<uint8_t>& bytecode,
                          const std::string& driver_name,
                          bool lang_ru) {
        std::vector<uint8_t> sys_data;
        sys_data.reserve(4096);

        const uint32_t GSYS_MAGIC = 0x47535953;
        write_u32_le(sys_data, 0x00, GSYS_MAGIC);
        write_u16_le(sys_data, 0x04, 0x0402); // v4.2.0

        // Определение типа драйвера по имени
        uint16_t driver_type = 0;
        std::string lower_name = driver_name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        if (lower_name.find("key") != std::string::npos || lower_name.find("kbd") != std::string::npos) driver_type = 1;
        else if (lower_name.find("time") != std::string::npos || lower_name.find("rtc") != std::string::npos) driver_type = 2;
        else if (lower_name.find("disk") != std::string::npos || lower_name.find("fs") != std::string::npos) driver_type = 3;
        else if (lower_name.find("video") != std::string::npos || lower_name.find("vga") != std::string::npos) driver_type = 4;

        write_u16_le(sys_data, 0x06, driver_type);
        write_u32_le(sys_data, 0x08, 0x00000007); // Freestanding | Active | Ring0

        // Имя драйвера (32 байта)
        sys_data.resize(0x2C, 0);
        size_t cpy_len = std::min(driver_name.size(), (size_t)31);
        std::memcpy(&sys_data[0x0C], driver_name.data(), cpy_len);

        write_u32_le(sys_data, 0x2C, 0x00000050); // DriverEntry RVA
        write_u32_le(sys_data, 0x30, 0x00000060); // DriverUnload RVA
        write_u32_le(sys_data, 0x34, 0x00000070); // DispatchRead RVA
        write_u32_le(sys_data, 0x38, 0x00000080); // DispatchWrite RVA
        write_u32_le(sys_data, 0x3C, 0x00000090); // DispatchIoctl RVA
        write_u32_le(sys_data, 0x40, (uint32_t)bytecode.size());

        // Кодовый стаб инициализации драйвера: ret (0xC3)
        sys_data.resize(0x50, 0);
        sys_data.push_back(0xC3); // DriverEntry stub
        sys_data.resize(0x60, 0);
        sys_data.push_back(0xC3); // DriverUnload stub
        sys_data.resize(0x70, 0);
        sys_data.push_back(0xC3); // Read stub
        sys_data.resize(0x80, 0);
        sys_data.push_back(0xC3); // Write stub
        sys_data.resize(0x90, 0);
        sys_data.push_back(0xC3); // Ioctl stub

        // Встраивание тела драйвера (байт-код и символы)
        sys_data.resize(0xA0, 0);
        sys_data.insert(sys_data.end(), bytecode.begin(), bytecode.end());

        std::ofstream out(output_sys, std::ios::binary);
        if (!out.is_open()) {
            if (lang_ru) std::cerr << "[GrassError]: Не удалось создать файл драйвера -> " << output_sys << "\n";
            else std::cerr << "[GrassError]: Failed to create driver file -> " << output_sys << "\n";
            return false;
        }
        out.write((const char*)sys_data.data(), sys_data.size());
        out.close();

        if (lang_ru) {
            std::cout << "[GrassDriver]: Скомпилирован системный драйвер ("
                      << sys_data.size() << " байт) -> " << output_sys << "\n"
                      << "              Сигнатура: GSYS | Тип: " << driver_type
                      << " | Имя: " << driver_name << "\n";
        } else {
            std::cout << "[GrassDriver]: Compiled system kernel driver ("
                      << sys_data.size() << " bytes) -> " << output_sys << "\n"
                      << "              Signature: GSYS | Type: " << driver_type
                      << " | Name: " << driver_name << "\n";
        }
        return true;
    }

    // ── 3. Генерация готового загрузочного ISO-образа (--iso) ──
    bool build_bootable_iso(const std::string& output_iso,
                            const std::vector<uint8_t>& bytecode,
                            const std::vector<uint8_t>& asm_machine_code,
                            const std::string& volume_id,
                            bool lang_ru) {
        const size_t SECTOR_SIZE = 2048;
        size_t kernel_sectors = 16; // 8 КБ под ядро
        size_t total_sectors = 22 + kernel_sectors;
        std::vector<uint8_t> iso(total_sectors * SECTOR_SIZE, 0);

        // 3.1. Сектор 0 & 20: MBR / Stage-0 загрузочный сектор
        std::vector<uint8_t> boot_code;
        append_u8(boot_code, 0xFA); // cli
        append_u8(boot_code, 0x31); append_u8(boot_code, 0xC0); // xor ax, ax
        append_u8(boot_code, 0x8E); append_u8(boot_code, 0xD8); // mov ds, ax
        append_u8(boot_code, 0x8E); append_u8(boot_code, 0xC0); // mov es, ax
        append_u8(boot_code, 0x8E); append_u8(boot_code, 0xD0); // mov ss, ax
        append_u8(boot_code, 0xBC); append_u16(boot_code, 0x7C00); // mov sp, 0x7C00
        append_u8(boot_code, 0xFB); // sti

        // Очистка экрана (INT 10h, AH=0, AL=3)
        append_u8(boot_code, 0xB8); append_u16(boot_code, 0x0003);
        append_u8(boot_code, 0xCD); append_u8(boot_code, 0x10);

        // Вывод баннера через BIOS Teletype (INT 10h, AH=0x0E)
        const char* msg = "\r\n  =======================================================\r\n"
                          "    GrassGreen OS v4.2.0 (Bare-Metal x86 Kernel)\r\n"
                          "    Booting directly from ISO via El Torito No-Emulation...\r\n"
                          "  =======================================================\r\n\r\n";
        for (size_t i = 0; i < strlen(msg); ++i) {
            append_u8(boot_code, 0xB4); append_u8(boot_code, 0x0E); // mov ah, 0x0E
            append_u8(boot_code, 0xB0); append_u8(boot_code, (uint8_t)msg[i]); // mov al, char
            append_u8(boot_code, 0xCD); append_u8(boot_code, 0x10); // int 0x10
        }

        // Включение линии Fast A20 (порт 0x92)
        append_u8(boot_code, 0xE4); append_u8(boot_code, 0x92); // in al, 0x92
        append_u8(boot_code, 0x0C); append_u8(boot_code, 0x02); // or al, 2
        append_u8(boot_code, 0xE6); append_u8(boot_code, 0x92); // out 0x92, al

        // Завершение Real-Mode загрузчика: hlt loop
        append_u8(boot_code, 0xF4); // hlt
        append_u8(boot_code, 0xEB); append_u8(boot_code, 0xFD); // jmp $-2

        // Копирование boot_code в Сектор 0
        std::memcpy(&iso[0], boot_code.data(), std::min(boot_code.size(), (size_t)446));
        iso[510] = 0x55; iso[511] = 0xAA;

        // Копирование в Сектор 20 (Boot Image для El Torito)
        std::memcpy(&iso[20 * SECTOR_SIZE], boot_code.data(), std::min(boot_code.size(), (size_t)446));
        iso[20 * SECTOR_SIZE + 510] = 0x55;
        iso[20 * SECTOR_SIZE + 511] = 0xAA;

        // 3.2. Сектор 16: Primary Volume Descriptor (PVD)
        size_t pvd = 16 * SECTOR_SIZE;
        iso[pvd + 0] = 0x01; // Type: PVD
        std::memcpy(&iso[pvd + 1], "CD001", 5);
        iso[pvd + 6] = 0x01; // Version
        iso[pvd + 7] = 0x00; // Unused

        const char* sys_id = "GRASSGREEN";
        std::memset(&iso[pvd + 8], ' ', 32);
        std::memcpy(&iso[pvd + 8], sys_id, strlen(sys_id));

        std::string vol = volume_id.empty() ? "GRASSGREEN_OS" : volume_id;
        std::memset(&iso[pvd + 40], ' ', 32);
        std::memcpy(&iso[pvd + 40], vol.data(), std::min(vol.size(), (size_t)32));

        write_u32_both(iso, pvd + 80, (uint32_t)total_sectors);
        write_u16_both(iso, pvd + 128, 2048);
        write_u32_both(iso, pvd + 132, 10);
        write_u32_le(iso, pvd + 140, 21); // Root Dir: Sector 21

        size_t root_rec = pvd + 156;
        iso[root_rec + 0] = 34; // Record Length
        iso[root_rec + 1] = 0;  // Ext Attribute Length
        write_u32_both(iso, root_rec + 2, 21); // Sector 21
        write_u32_both(iso, root_rec + 10, 2048);
        iso[root_rec + 25] = 0x02; // Directory
        iso[root_rec + 32] = 1;
        iso[root_rec + 33] = 0x00; // Root Identifier

        write_u16_both(iso, pvd + 120, 1);
        write_u16_both(iso, pvd + 124, 1);

        // 3.3. Сектор 17: Boot Record Volume Descriptor (El Torito BRVD)
        size_t brvd = 17 * SECTOR_SIZE;
        iso[brvd + 0] = 0x00;
        std::memcpy(&iso[brvd + 1], "CD001", 5);
        iso[brvd + 6] = 0x01;
        std::memcpy(&iso[brvd + 7], "EL TORITO SPECIFICATION", 23);
        write_u32_le(iso, brvd + 71, 19); // Sector 19 (Boot Catalog)

        // 3.4. Сектор 18: Volume Descriptor Set Terminator
        size_t term = 18 * SECTOR_SIZE;
        iso[term + 0] = 0xFF;
        std::memcpy(&iso[term + 1], "CD001", 5);
        iso[term + 6] = 0x01;

        // 3.5. Сектор 19: Boot Catalog (El Torito Specification)
        size_t cat = 19 * SECTOR_SIZE;
        iso[cat + 0] = 0x01; // Header ID
        iso[cat + 1] = 0x00; // Platform ID: 80x86
        const char* id_str = "GrassGreen";
        std::memcpy(&iso[cat + 4], id_str, strlen(id_str));
        iso[cat + 30] = 0x55; iso[cat + 31] = 0xAA;

        // Checksum Validation Entry
        uint16_t word_sum = 0;
        for (size_t i = 0; i < 32; i += 2) {
            if (i == 28) continue;
            uint16_t w = (uint16_t)iso[cat + i] | ((uint16_t)iso[cat + i + 1] << 8);
            word_sum += w;
        }
        uint16_t checksum = (uint16_t)(-(int32_t)word_sum);
        write_u16_le(iso, cat + 28, checksum);

        // Initial / Default Entry (32 байта)
        size_t def_entry = cat + 32;
        iso[def_entry + 0] = 0x88; // Bootable indicator
        iso[def_entry + 1] = 0x00; // No Emulation
        write_u16_le(iso, def_entry + 2, 0x07C0); // Load Segment: 0x07C0
        iso[def_entry + 4] = 0x00;
        iso[def_entry + 5] = 0x00;
        write_u16_le(iso, def_entry + 6, 4); // 4 сектора (2048 байт)
        write_u32_le(iso, def_entry + 8, 20); // Sector 20!

        // 3.6. Сектор 21: Root Directory Table ('.', '..', 'KERNEL.BIN;1')
        size_t root = 21 * SECTOR_SIZE;
        size_t cur = root;

        // '.'
        iso[cur + 0] = 34;
        write_u32_both(iso, cur + 2, 21);
        write_u32_both(iso, cur + 10, 2048);
        iso[cur + 25] = 0x02;
        iso[cur + 32] = 1;
        iso[cur + 33] = 0x00;
        cur += 34;

        // '..'
        iso[cur + 0] = 34;
        write_u32_both(iso, cur + 2, 21);
        write_u32_both(iso, cur + 10, 2048);
        iso[cur + 25] = 0x02;
        iso[cur + 32] = 1;
        iso[cur + 33] = 0x01;
        cur += 34;

        // 'KERNEL.BIN;1'
        const char* kfile = "KERNEL.BIN;1";
        uint8_t kfile_len = (uint8_t)strlen(kfile);
        uint8_t rec_len = (uint8_t)(33 + kfile_len + (kfile_len % 2 == 0 ? 1 : 0));
        iso[cur + 0] = rec_len;
        write_u32_both(iso, cur + 2, 22);
        uint32_t ksize = (uint32_t)(kernel_sectors * SECTOR_SIZE);
        write_u32_both(iso, cur + 10, ksize);
        iso[cur + 25] = 0x00;
        iso[cur + 32] = kfile_len;
        std::memcpy(&iso[cur + 33], kfile, kfile_len);

        // 3.7. Сектор 22+: Тело KERNEL.BIN
        std::string temp_kernel = output_iso + ".kernel.tmp";
        bool k_ok = build_kernel_bin(temp_kernel, bytecode, asm_machine_code, true, false);
        if (k_ok) {
            std::ifstream kin(temp_kernel, std::ios::binary | std::ios::ate);
            if (kin.is_open()) {
                size_t kbytes = (size_t)kin.tellg();
                kin.seekg(0, std::ios::beg);
                if (22 * SECTOR_SIZE + kbytes <= iso.size()) {
                    kin.read((char*)&iso[22 * SECTOR_SIZE], kbytes);
                }
                kin.close();
            }
            std::remove(temp_kernel.c_str());
        }

        std::ofstream out(output_iso, std::ios::binary);
        if (!out.is_open()) {
            if (lang_ru) std::cerr << "[GrassError]: Не удалось создать ISO-образ -> " << output_iso << "\n";
            else std::cerr << "[GrassError]: Failed to create ISO image -> " << output_iso << "\n";
            return false;
        }
        out.write((const char*)iso.data(), iso.size());
        out.close();

        if (lang_ru) {
            std::cout << "[GrassISO]: Создан загрузочный ISO-образ ("
                      << iso.size() << " байт, " << total_sectors << " секторов) -> " << output_iso << "\n"
                      << "            Спецификация: El Torito No-Emulation | Загрузка: QEMU / VirtualBox / USB\n";
        } else {
            std::cout << "[GrassISO]: Created bootable ISO image ("
                      << iso.size() << " bytes, " << total_sectors << " sectors) -> " << output_iso << "\n"
                      << "            Format: El Torito No-Emulation | Ready for QEMU / VirtualBox / USB\n";
        }
        return true;
    }

} // namespace GrassBoot
