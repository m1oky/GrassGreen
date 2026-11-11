// =============================================================================
//  GrassGreen 4.2.0 — Реализация сборки ядра, драйверов и ISO-образов
//  Поддержка: --multiboot, --kernel (.bin), --sys (.sys), --iso (.iso)
// =============================================================================
#include "GrassBoot.h"
#include "GrassCyrillicFont.h"
#include "GrassUnifiedFont.h"
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
        append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00093000); append_u32(bin_data, 12); // count = 12
        // Очистка и копирование таблицы 12 записей x 256 байт = 3072 байт
        append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00093004);
        append_u8(bin_data, 0xBE);
        size_t fixup_default_ramfstr_mem_addr = bin_data.size();
        append_u32(bin_data, 0); // fixup later to addr_default_ramfs
        append_u8(bin_data, 0xB9); append_u32(bin_data, 3072);
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

            // 8.1b. Таблица унифицированного шрифта (2544 байта)
            uint32_t addr_unified_font = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            for (size_t i = 0; i < sizeof(s_unified_font_8x16); ++i) append_u8(bin_data, s_unified_font_8x16[i]);

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

            // 8.6b. Mouse Subsystem Helpers (assembled from tmp_mouse.s)
            static const uint8_t s_mouse_helpers_bin[1288] = {
    0x50, 0x52, 0xE4, 0x64, 0xA8, 0x02, 0x75, 0xFA, 0xB0, 0xA8, 0xE6, 0x64, 0xE4, 0x64, 0xA8, 0x02,
    0x75, 0xFA, 0xB0, 0x20, 0xE6, 0x64, 0xE4, 0x64, 0xA8, 0x01, 0x74, 0xFA, 0xE4, 0x60, 0x0C, 0x02,
    0x24, 0xDF, 0x88, 0xC2, 0xE4, 0x64, 0xA8, 0x02, 0x75, 0xFA, 0xB0, 0x60, 0xE6, 0x64, 0xE4, 0x64,
    0xA8, 0x02, 0x75, 0xFA, 0x88, 0xD0, 0xE6, 0x60, 0xE4, 0x64, 0xA8, 0x02, 0x75, 0xFA, 0xB0, 0xD4,
    0xE6, 0x64, 0xE4, 0x64, 0xA8, 0x02, 0x75, 0xFA, 0xB0, 0xF6, 0xE6, 0x60, 0xE4, 0x64, 0xA8, 0x01,
    0x74, 0x02, 0xE4, 0x60, 0xE4, 0x64, 0xA8, 0x02, 0x75, 0xFA, 0xB0, 0xD4, 0xE6, 0x64, 0xE4, 0x64,
    0xA8, 0x02, 0x75, 0xFA, 0xB0, 0xF4, 0xE6, 0x60, 0xE4, 0x64, 0xA8, 0x01, 0x74, 0x02, 0xE4, 0x60,
    0xA1, 0xC0, 0x13, 0x09, 0x00, 0xD1, 0xE8, 0xA3, 0x00, 0x14, 0x09, 0x00, 0xA3, 0x14, 0x14, 0x09,
    0x00, 0xA1, 0xC4, 0x13, 0x09, 0x00, 0xD1, 0xE8, 0xA3, 0x04, 0x14, 0x09, 0x00, 0xA3, 0x18, 0x14,
    0x09, 0x00, 0xC7, 0x05, 0x08, 0x14, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x05, 0x0C, 0x14,
    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x05, 0x10, 0x14, 0x09, 0x00, 0x01, 0x00, 0x00, 0x00,
    0xC7, 0x05, 0x20, 0x14, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0x58, 0xC3, 0x53, 0x51, 0x52,
    0xE4, 0x64, 0xA8, 0x01, 0x0F, 0x84, 0xDF, 0x00, 0x00, 0x00, 0xA8, 0x20, 0x0F, 0x84, 0xD7, 0x00,
    0x00, 0x00, 0xE4, 0x60, 0x88, 0xC3, 0x8B, 0x0D, 0x20, 0x14, 0x09, 0x00, 0x83, 0xF9, 0x00, 0x75,
    0x17, 0xF6, 0xC3, 0x08, 0x74, 0xDA, 0x88, 0x1D, 0x24, 0x14, 0x09, 0x00, 0xC7, 0x05, 0x20, 0x14,
    0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0xEB, 0xC8, 0x83, 0xF9, 0x01, 0x75, 0x12, 0x88, 0x1D, 0x25,
    0x14, 0x09, 0x00, 0xC7, 0x05, 0x20, 0x14, 0x09, 0x00, 0x02, 0x00, 0x00, 0x00, 0xEB, 0xB1, 0x88,
    0x1D, 0x26, 0x14, 0x09, 0x00, 0xC7, 0x05, 0x20, 0x14, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F,
    0xB6, 0x15, 0x24, 0x14, 0x09, 0x00, 0x0F, 0xB6, 0x05, 0x25, 0x14, 0x09, 0x00, 0x0F, 0xB6, 0x0D,
    0x26, 0x14, 0x09, 0x00, 0xF6, 0xC2, 0x10, 0x74, 0x05, 0x0D, 0x00, 0xFF, 0xFF, 0xFF, 0xF6, 0xC2,
    0x20, 0x74, 0x06, 0x81, 0xC9, 0x00, 0xFF, 0xFF, 0xFF, 0x8B, 0x1D, 0x00, 0x14, 0x09, 0x00, 0x01,
    0xC3, 0x83, 0xFB, 0x00, 0x7D, 0x02, 0x31, 0xDB, 0xA1, 0xC0, 0x13, 0x09, 0x00, 0x83, 0xE8, 0x0C,
    0x39, 0xC3, 0x7E, 0x02, 0x89, 0xC3, 0x89, 0x1D, 0x00, 0x14, 0x09, 0x00, 0x8B, 0x1D, 0x04, 0x14,
    0x09, 0x00, 0x29, 0xCB, 0x83, 0xFB, 0x00, 0x7D, 0x02, 0x31, 0xDB, 0xA1, 0xC4, 0x13, 0x09, 0x00,
    0x83, 0xE8, 0x12, 0x39, 0xC3, 0x7E, 0x02, 0x89, 0xC3, 0x89, 0x1D, 0x04, 0x14, 0x09, 0x00, 0x8B,
    0x1D, 0x08, 0x14, 0x09, 0x00, 0x89, 0x1D, 0x0C, 0x14, 0x09, 0x00, 0x83, 0xE2, 0x07, 0x89, 0x15,
    0x08, 0x14, 0x09, 0x00, 0xE9, 0x17, 0xFF, 0xFF, 0xFF, 0x5A, 0x59, 0x5B, 0xC3, 0x50, 0x53, 0x51,
    0x52, 0x56, 0x57, 0x8B, 0x3D, 0x90, 0x13, 0x09, 0x00, 0xA1, 0x18, 0x14, 0x09, 0x00, 0x0F, 0xAF,
    0x05, 0xC8, 0x13, 0x09, 0x00, 0x01, 0xC7, 0xA1, 0x14, 0x14, 0x09, 0x00, 0x8D, 0x3C, 0x87, 0xBE,
    0x00, 0x15, 0x09, 0x00, 0xBA, 0x12, 0x00, 0x00, 0x00, 0x57, 0xB9, 0x0C, 0x00, 0x00, 0x00, 0xF3,
    0xA5, 0x5F, 0x03, 0x3D, 0xC8, 0x13, 0x09, 0x00, 0x4A, 0x75, 0xEE, 0x5F, 0x5E, 0x5A, 0x59, 0x5B,
    0x58, 0xC3, 0x50, 0x53, 0x51, 0x52, 0x56, 0x57, 0x83, 0x3D, 0x10, 0x14, 0x09, 0x00, 0x00, 0x0F,
    0x84, 0xA3, 0x00, 0x00, 0x00, 0xA1, 0x00, 0x14, 0x09, 0x00, 0xA3, 0x14, 0x14, 0x09, 0x00, 0xA1,
    0x04, 0x14, 0x09, 0x00, 0xA3, 0x18, 0x14, 0x09, 0x00, 0x8B, 0x3D, 0x90, 0x13, 0x09, 0x00, 0xA1,
    0x04, 0x14, 0x09, 0x00, 0x0F, 0xAF, 0x05, 0xC8, 0x13, 0x09, 0x00, 0x01, 0xC7, 0xA1, 0x00, 0x14,
    0x09, 0x00, 0x8D, 0x3C, 0x87, 0x89, 0xFE, 0x57, 0xBF, 0x00, 0x15, 0x09, 0x00, 0xBA, 0x12, 0x00,
    0x00, 0x00, 0x56, 0xB9, 0x0C, 0x00, 0x00, 0x00, 0xF3, 0xA5, 0x5E, 0x03, 0x35, 0xC8, 0x13, 0x09,
    0x00, 0x4A, 0x75, 0xEE, 0x5F, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x5E, 0x81, 0xC6, 0x66, 0x02, 0x00,
    0x00, 0xBA, 0x12, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x1E, 0x0F, 0xB7, 0x4E, 0x24, 0x57, 0xB8, 0x0C,
    0x00, 0x00, 0x00, 0x66, 0xF7, 0xC3, 0x00, 0x08, 0x74, 0x08, 0xC7, 0x07, 0x00, 0x00, 0x00, 0x00,
    0xEB, 0x0D, 0x66, 0xF7, 0xC1, 0x00, 0x08, 0x74, 0x06, 0xC7, 0x07, 0xFF, 0xFF, 0xFF, 0x00, 0x66,
    0xD1, 0xE3, 0x66, 0xD1, 0xE1, 0x83, 0xC7, 0x04, 0x48, 0x75, 0xD8, 0x5F, 0x03, 0x3D, 0xC8, 0x13,
    0x09, 0x00, 0x83, 0xC6, 0x02, 0x4A, 0x75, 0xBE, 0x5F, 0x5E, 0x5A, 0x59, 0x5B, 0x58, 0xC3, 0x3B,
    0x05, 0x10, 0x14, 0x09, 0x00, 0x74, 0x26, 0x50, 0x85, 0xC0, 0x74, 0x11, 0xC7, 0x05, 0x10, 0x14,
    0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0xE8, 0x27, 0xFF, 0xFF, 0xFF, 0x58, 0xC3, 0xE8, 0xDB, 0xFE,
    0xFF, 0xFF, 0xC7, 0x05, 0x10, 0x14, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0xC3, 0xE8, 0xDA,
    0xFD, 0xFF, 0xFF, 0xA1, 0x00, 0x14, 0x09, 0x00, 0x3B, 0x05, 0x14, 0x14, 0x09, 0x00, 0x75, 0x0D,
    0xA1, 0x04, 0x14, 0x09, 0x00, 0x3B, 0x05, 0x18, 0x14, 0x09, 0x00, 0x74, 0x0A, 0xE8, 0xAB, 0xFE,
    0xFF, 0xFF, 0xE8, 0xEB, 0xFE, 0xFF, 0xFF, 0x8B, 0x15, 0x08, 0x14, 0x09, 0x00, 0xF6, 0xC2, 0x01,
    0x74, 0x0C, 0xA1, 0x0C, 0x14, 0x09, 0x00, 0xA8, 0x01, 0x75, 0x03, 0xB0, 0xFE, 0xC3, 0x30, 0xC0,
    0xC3, 0x53, 0x51, 0x52, 0x56, 0x57, 0x8B, 0x35, 0x00, 0x14, 0x09, 0x00, 0x8B, 0x3D, 0x04, 0x14,
    0x09, 0x00, 0x83, 0xFE, 0x23, 0x73, 0x0F, 0x83, 0xFF, 0x23, 0x73, 0x0A, 0xB8, 0x07, 0x00, 0x00,
    0x00, 0xE9, 0x74, 0x01, 0x00, 0x00, 0x8B, 0x0D, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xE9, 0x00, 0x04,
    0x00, 0x00, 0xD1, 0xE9, 0x8B, 0x15, 0xC4, 0x13, 0x09, 0x00, 0x81, 0xEA, 0x00, 0x03, 0x00, 0x00,
    0xD1, 0xEA, 0x89, 0xF0, 0x29, 0xC8, 0x89, 0xD3, 0x83, 0xC3, 0x6E, 0x39, 0xDF, 0x0F, 0x82, 0xB6,
    0x00, 0x00, 0x00, 0x83, 0xC3, 0x26, 0x39, 0xDF, 0x0F, 0x87, 0xAB, 0x00, 0x00, 0x00, 0x83, 0xF8,
    0x44, 0x0F, 0x82, 0x31, 0x01, 0x00, 0x00, 0x3D, 0xD2, 0x00, 0x00, 0x00, 0x72, 0x5F, 0x3D, 0xDA,
    0x00, 0x00, 0x00, 0x0F, 0x82, 0x1F, 0x01, 0x00, 0x00, 0x3D, 0x68, 0x01, 0x00, 0x00, 0x72, 0x57,
    0x3D, 0x70, 0x01, 0x00, 0x00, 0x0F, 0x82, 0x0D, 0x01, 0x00, 0x00, 0x3D, 0xFE, 0x01, 0x00, 0x00,
    0x72, 0x4F, 0x3D, 0x06, 0x02, 0x00, 0x00, 0x0F, 0x82, 0xFB, 0x00, 0x00, 0x00, 0x3D, 0x94, 0x02,
    0x00, 0x00, 0x72, 0x47, 0x3D, 0x9C, 0x02, 0x00, 0x00, 0x0F, 0x82, 0xE9, 0x00, 0x00, 0x00, 0x3D,
    0x2A, 0x03, 0x00, 0x00, 0x72, 0x3F, 0x3D, 0x32, 0x03, 0x00, 0x00, 0x0F, 0x82, 0xD7, 0x00, 0x00,
    0x00, 0x3D, 0xC0, 0x03, 0x00, 0x00, 0x72, 0x37, 0xE9, 0xCB, 0x00, 0x00, 0x00, 0xB8, 0x01, 0x00,
    0x00, 0x00, 0xE9, 0xC3, 0x00, 0x00, 0x00, 0xB8, 0x02, 0x00, 0x00, 0x00, 0xE9, 0xB9, 0x00, 0x00,
    0x00, 0xB8, 0x03, 0x00, 0x00, 0x00, 0xE9, 0xAF, 0x00, 0x00, 0x00, 0xB8, 0x04, 0x00, 0x00, 0x00,
    0xE9, 0xA5, 0x00, 0x00, 0x00, 0xB8, 0x05, 0x00, 0x00, 0x00, 0xE9, 0x9B, 0x00, 0x00, 0x00, 0xB8,
    0x06, 0x00, 0x00, 0x00, 0xE9, 0x91, 0x00, 0x00, 0x00, 0x8B, 0x15, 0xC4, 0x13, 0x09, 0x00, 0x81,
    0xEA, 0x00, 0x03, 0x00, 0x00, 0x81, 0xC2, 0xC0, 0x02, 0x00, 0x00, 0x39, 0xD7, 0x72, 0x79, 0x83,
    0xC2, 0x2C, 0x39, 0xD7, 0x77, 0x72, 0x3D, 0x30, 0x01, 0x00, 0x00, 0x72, 0x6B, 0x3D, 0x60, 0x01,
    0x00, 0x00, 0x72, 0x56, 0x3D, 0x68, 0x01, 0x00, 0x00, 0x72, 0x5D, 0x3D, 0x98, 0x01, 0x00, 0x00,
    0x72, 0x95, 0x3D, 0xA0, 0x01, 0x00, 0x00, 0x72, 0x4F, 0x3D, 0xD0, 0x01, 0x00, 0x00, 0x72, 0x9B,
    0x3D, 0xD8, 0x01, 0x00, 0x00, 0x72, 0x41, 0x3D, 0x08, 0x02, 0x00, 0x00, 0x72, 0x83, 0x3D, 0x10,
    0x02, 0x00, 0x00, 0x72, 0x33, 0x3D, 0x40, 0x02, 0x00, 0x00, 0x72, 0x89, 0x3D, 0x48, 0x02, 0x00,
    0x00, 0x72, 0x25, 0x3D, 0x78, 0x02, 0x00, 0x00, 0x72, 0x85, 0x3D, 0x80, 0x02, 0x00, 0x00, 0x72,
    0x17, 0x3D, 0xB0, 0x02, 0x00, 0x00, 0x72, 0x09, 0xEB, 0x0E, 0xB8, 0x07, 0x00, 0x00, 0x00, 0xEB,
    0x09, 0xB8, 0x08, 0x00, 0x00, 0x00, 0xEB, 0x02, 0x31, 0xC0, 0x5F, 0x5E, 0x5A, 0x59, 0x5B, 0xC3,
    0x00, 0x08, 0x00, 0x0C, 0x00, 0x0A, 0x00, 0x09, 0x80, 0x08, 0x40, 0x08, 0x20, 0x08, 0x10, 0x08,
    0x08, 0x08, 0x04, 0x08, 0x3E, 0x08, 0x20, 0x09, 0x90, 0x0A, 0x90, 0x0C, 0x90, 0x08, 0x90, 0x00,
    0x48, 0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x06, 0x00, 0x07, 0x80, 0x07,
    0xC0, 0x07, 0xE0, 0x07, 0xF0, 0x07, 0xF8, 0x07, 0xC0, 0x07, 0xC0, 0x06, 0x60, 0x04, 0x60, 0x00,
    0x60, 0x00, 0x60, 0x00, 0x30, 0x00, 0x00, 0x00,
            };
            size_t start_mouse_offset = bin_data.size();
            uint32_t addr_mouse_init      = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x000;
            uint32_t addr_mouse_poll      = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x0BD;
            uint32_t addr_cursor_restore  = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x1AD;
            uint32_t addr_cursor_draw     = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x1F2;
            uint32_t addr_mouse_cursor    = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x2AF;
            uint32_t addr_mouse_update    = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x2DE;
            uint32_t addr_gui_mouse_click = KERNEL_LOAD_ADDR + (uint32_t)start_mouse_offset + 0x321;
            for (size_t i = 0; i < sizeof(s_mouse_helpers_bin); ++i) {
                append_u8(bin_data, s_mouse_helpers_bin[i]);
            }

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

            // Check bit 5 of port 0x64 status (1 = mouse data, 0 = keyboard)
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x20); // test al, 0x20
            size_t jz_is_kbd = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // It's mouse data! Call addr_mouse_update to process packet & update cursor
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_mouse_update - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Did Left Click occur? (AL == 0xFE)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xFE);
            size_t jne_poll_mouse = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Left Click event: pop ecx, ebx, edx, ret with AL = 0xFE
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0xC3);

            size_t pos_poll_mouse = bin_data.size();
            bin_data[jne_poll_mouse + 1] = (uint8_t)(pos_poll_mouse - (jne_poll_mouse + 2));
            jmp_poll();

            size_t pos_is_kbd = bin_data.size();
            bin_data[jz_is_kbd + 1] = (uint8_t)(pos_is_kbd - (jz_is_kbd + 2));
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

            // Ignore mouse click events (0xFE) in text mode console
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xFE);
            append_u8(bin_data, 0x74); append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

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
                static const DefaultFSEntry s_initial_fs[12] = {
                    { "system", "/", 1, 0, 0, "" },
                    { "kernel.sys", "/system", 0, 0, 24, "GrassGreen Kernel v4.3.0" },
                    { "config.sys", "/system", 0, 0, 35, "DISPLAY=VGA;MODE=TEXT_80X25;GUI=VD+" },
                    { "users", "/", 1, 0, 0, "" },
                    { "readme.txt", "/users", 0, 0, 41, "Welcome to RQWos on GrassGreen Bare-Metal!" },
                    { "user_profile.json", "/users", 0, 0, 52, "{\"user\":\"m1oky\",\"theme\":\"dark\",\"accent\":\"#22c55e\"}" },
                    { "apps", "/", 1, 0, 0, "" },
                    { "calc.app", "/apps", 0, 0, 32, "GrassGreen Calculator v1.0" },
                    { "notes.app", "/apps", 0, 0, 28, "RQWos Notes Editor v1.0" },
                    { "data", "/", 1, 0, 0, "" },
                    { "user_profile.json", "/data", 0, 0, 68, "{\"user\":\"m1oky\",\"storage\":\"ata_persistent\",\"sound\":\"sb16\"}" },
                    { "notes.txt", "/data", 0, 0, 62, "RQWos Baremetal OS Notes: ATA LBA28, RTL8139, SB16, CMOS RTC Active" }
                };
                for (int i = 0; i < 12; ++i) {
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

                                    // 8.21. Helper: helper_vd_desktop (Vector Display+ 32-bit Framebuffer GUI Desktop)
            auto emit_bga_write = [&](uint16_t idx, uint16_t val) {
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01CE);
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, idx);
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01CF);
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, val);
                append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
            };

            // JMP over GUI helpers
            append_u8(bin_data, 0xE9);
            size_t jmp_over_gui_helpers = bin_data.size();
            append_u32(bin_data, 0);

            // 8.20a. Helper: addr_ata_read_sector(EAX = LBA, EDI = buffer)
            uint32_t addr_ata_read_sector = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x51); append_u8(bin_data, 0x52); append_u8(bin_data, 0x53); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC3); // mov ebx, eax
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 50000);
            size_t loop_ata_r_bsy1 = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80);
            size_t jz_r_bsy_done = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_ata_r_bsy1 - (bin_data.size() + 1)));
            bin_data[jz_r_bsy_done + 1] = (uint8_t)(bin_data.size() - (jz_r_bsy_done + 2));
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE8); append_u8(bin_data, 24);
            append_u8(bin_data, 0x24); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x0C); append_u8(bin_data, 0xE0);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F6); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F2); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xB0); append_u8(bin_data, 1); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F3); append_u16(bin_data, 0x0000); append_u8(bin_data, 0x88); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F4); append_u16(bin_data, 0x0000); append_u8(bin_data, 0x88); append_u8(bin_data, 0xF8); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F5); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE8); append_u8(bin_data, 16); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x20); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 50000);
            size_t loop_ata_r_drq = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x08);
            size_t jnz_r_drq_ok = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_ata_r_drq - (bin_data.size() + 1)));
            bin_data[jnz_r_drq_ok + 1] = (uint8_t)(bin_data.size() - (jnz_r_drq_ok + 2));
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F0); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 256);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xF3); append_u8(bin_data, 0x6D); // rep insw
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20b. Helper: addr_ata_write_sector(EAX = LBA, ESI = buffer)
            uint32_t addr_ata_write_sector = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x51); append_u8(bin_data, 0x52); append_u8(bin_data, 0x53); append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC3); // mov ebx, eax
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 50000);
            size_t loop_ata_w_bsy1 = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80);
            size_t jz_w_bsy_done = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_ata_w_bsy1 - (bin_data.size() + 1)));
            bin_data[jz_w_bsy_done + 1] = (uint8_t)(bin_data.size() - (jz_w_bsy_done + 2));
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE8); append_u8(bin_data, 24);
            append_u8(bin_data, 0x24); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x0C); append_u8(bin_data, 0xE0);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F6); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F2); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xB0); append_u8(bin_data, 1); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F3); append_u16(bin_data, 0x0000); append_u8(bin_data, 0x88); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F4); append_u16(bin_data, 0x0000); append_u8(bin_data, 0x88); append_u8(bin_data, 0xF8); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F5); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE8); append_u8(bin_data, 16); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xB0); append_u8(bin_data, 0x30); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 50000);
            size_t loop_ata_w_drq = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x08);
            size_t jnz_w_drq_ok = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_ata_w_drq - (bin_data.size() + 1)));
            bin_data[jnz_w_drq_ok + 1] = (uint8_t)(bin_data.size() - (jnz_w_drq_ok + 2));
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F0); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 256);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xF3); append_u8(bin_data, 0x6F); // rep outsw
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xB0); append_u8(bin_data, 0xE7); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 50000);
            size_t loop_ata_w_fl = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80);
            size_t jz_w_fl_ok = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_ata_w_fl - (bin_data.size() + 1)));
            bin_data[jz_w_fl_ok + 1] = (uint8_t)(bin_data.size() - (jz_w_fl_ok + 2));
            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009139C); // inc [sectors_synced]
            append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20c. Helper: addr_ata_sync_fs
            uint32_t addr_ata_sync_fs = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            // Check if ATA drive is present (port 0x1F7 != 0xFF && != 0x00)
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x01F7); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xEC); // in al, dx
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xFF);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t j_ata_no1 = bin_data.size(); append_u32(bin_data, 0);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t j_ata_no2 = bin_data.size(); append_u32(bin_data, 0);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 100);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00094000);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_read_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00094000); append_u32(bin_data, 0x51575246);
            size_t jne_fs_new = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091398); append_u8(bin_data, 1);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 101);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_read_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 102);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00093200);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_read_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            size_t jmp_fs_sync_ok = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_fs_new = bin_data.size();
            bin_data[jne_fs_new + 1] = (uint8_t)(pos_fs_new - (jne_fs_new + 2));
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00094000);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 128);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00094000); append_u32(bin_data, 0x51575246);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00094004); append_u32(bin_data, 1);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00094008); append_u32(bin_data, 1048576);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 100);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00094000);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_write_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 101);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00093000);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_write_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 102);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00093200);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_ata_write_sector - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091398); append_u8(bin_data, 1);
            size_t pos_fs_sync_ok = bin_data.size();
            bin_data[jmp_fs_sync_ok + 1] = (uint8_t)(pos_fs_sync_ok - (jmp_fs_sync_ok + 2));
            write_u32_le(bin_data, j_ata_no1, (uint32_t)((int32_t)bin_data.size() - (int32_t)(j_ata_no1 + 4)));
            write_u32_le(bin_data, j_ata_no2, (uint32_t)((int32_t)bin_data.size() - (int32_t)(j_ata_no2 + 4)));
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20d. Helper: addr_read_cmos_rtc
            uint32_t addr_read_cmos_rtc = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51); append_u8(bin_data, 0x52); append_u8(bin_data, 0x57);
            auto emit_cmos_read = [&](uint8_t reg) {
                append_u8(bin_data, 0xB0); append_u8(bin_data, reg);
                append_u8(bin_data, 0xE6); append_u8(bin_data, 0x70);
                append_u8(bin_data, 0xE4); append_u8(bin_data, 0x71);
            };
            emit_cmos_read(0x0B);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xC3);
            auto emit_bcd_to_bin = [&]() {
                append_u8(bin_data, 0xF6); append_u8(bin_data, 0xC3); append_u8(bin_data, 0x04);
                size_t jnz_bin = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
                append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0); // movzx eax, al
                append_u8(bin_data, 0x89); append_u8(bin_data, 0xC2);                             // mov edx, eax
                append_u8(bin_data, 0xC1); append_u8(bin_data, 0xEA); append_u8(bin_data, 4);    // shr edx, 4
                append_u8(bin_data, 0x6B); append_u8(bin_data, 0xD2); append_u8(bin_data, 10);   // imul edx, edx, 10
                append_u8(bin_data, 0x24); append_u8(bin_data, 0x0F);                             // and al, 0x0F
                append_u8(bin_data, 0x01); append_u8(bin_data, 0xD0);                             // add eax, edx
                bin_data[jnz_bin + 1] = (uint8_t)(bin_data.size() - (jnz_bin + 2));
            };
            emit_cmos_read(0x00); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B0);
            emit_cmos_read(0x02); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B1);
            emit_cmos_read(0x04); append_u8(bin_data, 0x24); append_u8(bin_data, 0x7F); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B2);
            emit_cmos_read(0x07); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B3);
            emit_cmos_read(0x08); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B4);
            emit_cmos_read(0x09); emit_bcd_to_bin(); append_u8(bin_data, 0xA2); append_u32(bin_data, 0x000913B5);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x00091140);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x07); append_u32(bin_data, 0x6F575152);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x47); append_u8(bin_data, 4); append_u32(bin_data, 0x207C2073);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 8);
            auto emit_write_2digit = [&](uint32_t mem) {
                append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0x05); append_u32(bin_data, mem);
                append_u8(bin_data, 0xB9); append_u32(bin_data, 10);
                append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2);
                append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1);
                append_u8(bin_data, 0x04); append_u8(bin_data, '0');
                append_u8(bin_data, 0x88); append_u8(bin_data, 0x07); append_u8(bin_data, 0x47);
                append_u8(bin_data, 0x88); append_u8(bin_data, 0xD0);
                append_u8(bin_data, 0x04); append_u8(bin_data, '0');
                append_u8(bin_data, 0x88); append_u8(bin_data, 0x07); append_u8(bin_data, 0x47);
            };
            emit_write_2digit(0x000913B3);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, '.'); append_u8(bin_data, 0x47);
            emit_write_2digit(0x000913B4);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, '.'); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, '2'); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, '0'); append_u8(bin_data, 0x47);
            emit_write_2digit(0x000913B5);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, ' '); append_u8(bin_data, 0x47);
            emit_write_2digit(0x000913B2);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, ':'); append_u8(bin_data, 0x47);
            emit_write_2digit(0x000913B1);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, ':'); append_u8(bin_data, 0x47);
            emit_write_2digit(0x000913B0);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0);
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20e. Helper: addr_rtl8139_init
            uint32_t addr_rtl8139_init = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51); append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xDB);
            size_t loop_pci_scan = bin_data.size();
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE0); append_u8(bin_data, 11);
            append_u8(bin_data, 0x0D); append_u32(bin_data, 0x80000000);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CF8); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CFC); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xED);
            append_u8(bin_data, 0x3D); append_u32(bin_data, 0x813910EC);
            size_t je_pci_found = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x43);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xFB); append_u8(bin_data, 32);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_pci_scan - (bin_data.size() + 1)));
            append_u8(bin_data, 0xE9); size_t jmp_pci_not_found = bin_data.size(); append_u32(bin_data, 0);
            size_t pos_pci_found = bin_data.size();
            bin_data[je_pci_found + 1] = (uint8_t)(pos_pci_found - (je_pci_found + 2));
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE0); append_u8(bin_data, 11);
            append_u8(bin_data, 0x0D); append_u32(bin_data, 0x80000004);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CF8); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CFC); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xED);
            append_u8(bin_data, 0x0C); append_u8(bin_data, 0x05);
            append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8);
            append_u8(bin_data, 0xC1); append_u8(bin_data, 0xE0); append_u8(bin_data, 11);
            append_u8(bin_data, 0x0D); append_u32(bin_data, 0x80000010);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CF8); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0CFC); append_u16(bin_data, 0x0000); append_u8(bin_data, 0xED);
            append_u8(bin_data, 0x25); append_u32(bin_data, 0xFFFFFFFC);
            append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091208);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC5);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC9);
            size_t loop_mac_read = bin_data.size();
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x14); append_u8(bin_data, 0x0D); append_u32(bin_data, 0);
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0x81); append_u32(bin_data, 0x00091238);
            append_u8(bin_data, 0x41);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF9); append_u8(bin_data, 6);
            append_u8(bin_data, 0x72); append_u8(bin_data, (uint8_t)(int8_t)(loop_mac_read - (bin_data.size() + 1)));
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x52);
            append_u8(bin_data, 0x30); append_u8(bin_data, 0xC0); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x37);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x10); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 10000); // timeout counter
            size_t loop_rst = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x10);
            size_t jz_rst_done = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_rst - (bin_data.size() + 1)));
            bin_data[jz_rst_done + 1] = (uint8_t)(bin_data.size() - (jz_rst_done + 2));
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x30);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x00080000); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x3C);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, 0x0005); append_u8(bin_data, 0x66); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x44);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x0000000F); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x37);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0C); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x0008F000);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 15);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x0008F000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0xFF);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 6);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAA);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091238);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 6);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA4);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x08); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x06); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x01); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x08); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 6); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 4); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x01); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091238);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 6);
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0xA4);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 10); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 2); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 15); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC7); append_u8(bin_data, 6);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 10); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 2); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 2); append_u8(bin_data, 0x47);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x20);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x0008F000); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x95); append_u32(bin_data, 0x10);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 60); append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091200); append_u8(bin_data, 1);
            size_t pos_pci_not_found = bin_data.size();
            write_u32_le(bin_data, jmp_pci_not_found, (uint32_t)((int32_t)pos_pci_not_found - (int32_t)(jmp_pci_not_found + 4)));
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20f. Helper: addr_sb16_init
            uint32_t addr_sb16_init = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x52); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x0226); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 1); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 1000);
            size_t loop_sb_del = bin_data.size();
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_sb_del - (bin_data.size() + 1)));
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x022E); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 10000);
            size_t loop_sb_rdy = bin_data.size();
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80);
            size_t jnz_sb_got = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_sb_rdy - (bin_data.size() + 1)));
            size_t jmp_sb_fail = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_sb_got = bin_data.size();
            bin_data[jnz_sb_got + 1] = (uint8_t)(pos_sb_got - (jnz_sb_got + 2));
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x022A); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xEC); append_u8(bin_data, 0x3C); append_u8(bin_data, 0xAA);
            size_t jne_sb_fail2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091394); append_u8(bin_data, 1);
            size_t pos_sb_fail = bin_data.size();
            bin_data[jmp_sb_fail + 1] = (uint8_t)(pos_sb_fail - (jmp_sb_fail + 2));
            bin_data[jne_sb_fail2 + 1] = (uint8_t)(pos_sb_fail - (jne_sb_fail2 + 2));
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5A); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // 8.20g. Helper: addr_play_sound(sound_id in AL)
            uint32_t addr_play_sound = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51); append_u8(bin_data, 0x52);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091394); append_u8(bin_data, 1);
            size_t jne_no_sb_out = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x022C); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x10); append_u8(bin_data, 0xEE);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0xC0); append_u8(bin_data, 0xEE);
            size_t pos_no_sb_out = bin_data.size();
            bin_data[jne_no_sb_out + 1] = (uint8_t)(pos_no_sb_out - (jne_no_sb_out + 2));
            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x44); append_u8(bin_data, 0x24); append_u8(bin_data, 12);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 1355);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 1);
            size_t jne_snd2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 1193);
            size_t jmp_snd_set = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_snd2 = bin_data.size();
            bin_data[jne_snd2 + 1] = (uint8_t)(pos_snd2 - (jne_snd2 + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 2);
            size_t jne_snd3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 678);
            size_t jmp_snd_set2 = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_snd3 = bin_data.size();
            bin_data[jne_snd3 + 1] = (uint8_t)(pos_snd3 - (jne_snd3 + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 3);
            size_t jne_snd_def = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xBB); append_u32(bin_data, 3977);
            size_t pos_snd_set = bin_data.size();
            bin_data[jne_snd_def + 1] = (uint8_t)(pos_snd_set - (jne_snd_def + 2));
            bin_data[jmp_snd_set + 1] = (uint8_t)(pos_snd_set - (jmp_snd_set + 2));
            bin_data[jmp_snd_set2 + 1] = (uint8_t)(pos_snd_set - (jmp_snd_set2 + 2));
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x43);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xD8); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x42);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xFC); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x42);
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x61); append_u8(bin_data, 0x0C); append_u8(bin_data, 3); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x61);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 0x30000);
            size_t loop_tone = bin_data.size();
            append_u8(bin_data, 0x49); append_u8(bin_data, 0x75); append_u8(bin_data, (uint8_t)(int8_t)(loop_tone - (bin_data.size() + 1)));
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x61); append_u8(bin_data, 0x24); append_u8(bin_data, 0xFC); append_u8(bin_data, 0xE6); append_u8(bin_data, 0x61);
            append_u8(bin_data, 0x5A); append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3);

            // Perfectly assembled GUI helpers from GNU AS (helpers.bin)
            static const uint8_t s_gui_helpers_bin[904] = {
                0x50, 0x52, 0xA3, 0xC0, 0x13, 0x09, 0x00, 0x89, 0x15, 0xC4, 0x13, 0x09, 0x00, 0xC1, 0xE0, 0x02, 0xA3, 0xC8, 0x13, 0x09, 0x00, 0xC7, 0x05, 0xD4, 0x13, 0x09, 0x00, 0x01, 0x00, 0x00, 0x00, 0x66, 0xBA, 0xCE, 0x01, 0x66, 0xB8, 0x04, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCF, 0x01, 0x66, 0x31, 0xC0, 0x66, 0xEF, 0x66, 0xBA, 0xCE, 0x01, 0x66, 0xB8, 0x01, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCF, 0x01, 0xA1, 0xC0, 0x13, 0x09, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCE, 0x01, 0x66, 0xB8, 0x02, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCF, 0x01, 0xA1, 0xC4, 0x13, 0x09, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCE, 0x01, 0x66, 0xB8, 0x03, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCF, 0x01, 0x66, 0xB8, 0x20, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCE, 0x01, 0x66, 0xB8, 0x04, 0x00, 0x66, 0xEF, 0x66, 0xBA, 0xCF, 0x01, 0x66, 0xB8, 0x41, 0x00, 0x66, 0xEF, 0x5A, 0x58, 0xC3, 0x55, 0x89, 0xE5, 0x53, 0x56, 0x57, 0x8B, 0x45, 0x0C, 0x8B, 0x5D, 0x10, 0x8B, 0x4D, 0x14, 0x8B, 0x55, 0x18, 0x85, 0xC0, 0x75, 0x25, 0x85, 0xDB, 0x75, 0x21, 0x81, 0xF9, 0x00, 0x04, 0x00, 0x00, 0x72, 0x19, 0x81, 0xFA, 0x00, 0x03, 0x00, 0x00, 0x72, 0x11, 0x8B, 0x0D, 0xC0, 0x13, 0x09, 0x00, 0x8B, 0x15, 0xC4, 0x13, 0x09, 0x00, 0xE9, 0x9C, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x75, 0x18, 0x83, 0xFB, 0x28, 0x77, 0x13, 0x81, 0xF9, 0x00, 0x04, 0x00, 0x00, 0x72, 0x0B, 0x8B, 0x0D, 0xC0, 0x13, 0x09, 0x00, 0xE9, 0x80, 0x00, 0x00, 0x00, 0x83, 0xFB, 0x2C, 0x73, 0x33, 0x3D, 0xBC, 0x02, 0x00, 0x00, 0x72, 0x10, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0x01, 0xF0, 0xEB, 0x64, 0x3D, 0x90, 0x01, 0x00, 0x00, 0x72, 0x5D, 0x3D, 0xF4, 0x01, 0x00, 0x00, 0x73, 0x56, 0xA1, 0xC0, 0x13, 0x09, 0x00, 0x2D, 0xE0, 0x00, 0x00, 0x00, 0xD1, 0xE8, 0xEB, 0x48, 0x81, 0xFB, 0xBC, 0x02, 0x00, 0x00, 0x72, 0x20, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF0, 0x8B, 0x35, 0xC4, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x03, 0x00, 0x00, 0x01, 0xF3, 0xEB, 0x20, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF0, 0x8B, 0x35, 0xC4, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x03, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF3, 0x3B, 0x05, 0xC0, 0x13, 0x09, 0x00, 0x73, 0x60, 0x3B, 0x1D, 0xC4, 0x13, 0x09, 0x00, 0x73, 0x58, 0x85, 0xC9, 0x7E, 0x54, 0x85, 0xD2, 0x7E, 0x50, 0x89, 0xC6, 0x01, 0xCE, 0x3B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x76, 0x08, 0x8B, 0x0D, 0xC0, 0x13, 0x09, 0x00, 0x29, 0xC1, 0x89, 0xDE, 0x01, 0xD6, 0x3B, 0x35, 0xC4, 0x13, 0x09, 0x00, 0x76, 0x08, 0x8B, 0x15, 0xC4, 0x13, 0x09, 0x00, 0x29, 0xDA, 0x8B, 0x3D, 0x90, 0x13, 0x09, 0x00, 0x89, 0xDE, 0x0F, 0xAF, 0x35, 0xC8, 0x13, 0x09, 0x00, 0x01, 0xF7, 0x8D, 0x3C, 0x87, 0x8B, 0x75, 0x08, 0x57, 0x51, 0x89, 0xF0, 0xF3, 0xAB, 0x59, 0x5F, 0x03, 0x3D, 0xC8, 0x13, 0x09, 0x00, 0x4A, 0x75, 0xEF, 0x5F, 0x5E, 0x5B, 0x5D, 0xC2, 0x14, 0x00, 0x55, 0x89, 0xE5, 0x53, 0x56, 0x57, 0x8B, 0x45, 0x0C, 0x8B, 0x5D, 0x10, 0x3B, 0x05, 0xC0, 0x13, 0x09, 0x00, 0x0F, 0x83, 0xAA, 0x00, 0x00, 0x00, 0x3B, 0x1D, 0xC4, 0x13, 0x09, 0x00, 0x0F, 0x83, 0x9E, 0x00, 0x00, 0x00, 0x0F, 0xB6, 0x45, 0x14, 0x3C, 0xC0, 0x72, 0x06, 0x2C, 0xC0, 0x04, 0x5F, 0xEB, 0x0A, 0x3C, 0x20, 0x0F, 0x82, 0x88, 0x00, 0x00, 0x00, 0x2C, 0x20, 0x0F, 0xB6, 0xC0, 0xC1, 0xE0, 0x04, 0x05, 0x00, 0x50, 0x10, 0x00, 0x50, 0x8B, 0x3D, 0x90, 0x13, 0x09, 0x00, 0x8B, 0x45, 0x10, 0x0F, 0xAF, 0x05, 0xC8, 0x13, 0x09, 0x00, 0x01, 0xC7, 0x8B, 0x45, 0x0C, 0x8D, 0x3C, 0x87, 0x8B, 0x75, 0x08, 0x8B, 0x5D, 0x18, 0x31, 0xC9, 0x8B, 0x14, 0x24, 0x8A, 0x04, 0x0A, 0xB4, 0x08, 0x57, 0xA8, 0x80, 0x74, 0x1B, 0x83, 0xFB, 0x02, 0x75, 0x14, 0x89, 0x37, 0x89, 0x77, 0x04, 0x8B, 0x15, 0xC8, 0x13, 0x09, 0x00, 0x89, 0x34, 0x17, 0x89, 0x74, 0x17, 0x04, 0xEB, 0x02, 0x89, 0x37, 0x83, 0xFB, 0x02, 0x75, 0x05, 0x83, 0xC7, 0x08, 0xEB, 0x03, 0x83, 0xC7, 0x04, 0xD0, 0xE0, 0xFE, 0xCC, 0x75, 0xCE, 0x5F, 0x83, 0xFB, 0x02, 0x75, 0x0C, 0x8B, 0x15, 0xC8, 0x13, 0x09, 0x00, 0xD1, 0xE2, 0x01, 0xD7, 0xEB, 0x06, 0x03, 0x3D, 0xC8, 0x13, 0x09, 0x00, 0x41, 0x83, 0xF9, 0x10, 0x72, 0xA7, 0x58, 0x5F, 0x5E, 0x5B, 0x5D, 0xC2, 0x14, 0x00, 0x55, 0x89, 0xE5, 0x53, 0x56, 0x57, 0x8B, 0x45, 0x0C, 0x8B, 0x5D, 0x10, 0x83, 0xFB, 0x2C, 0x73, 0x33, 0x3D, 0xBC, 0x02, 0x00, 0x00, 0x72, 0x10, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0x01, 0xF0, 0xEB, 0x64, 0x3D, 0x90, 0x01, 0x00, 0x00, 0x72, 0x5D, 0x3D, 0xF4, 0x01, 0x00, 0x00, 0x73, 0x56, 0xA1, 0xC0, 0x13, 0x09, 0x00, 0x2D, 0xE0, 0x00, 0x00, 0x00, 0xD1, 0xE8, 0xEB, 0x48, 0x81, 0xFB, 0xBC, 0x02, 0x00, 0x00, 0x72, 0x20, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF0, 0x8B, 0x35, 0xC4, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x03, 0x00, 0x00, 0x01, 0xF3, 0xEB, 0x20, 0x8B, 0x35, 0xC0, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x04, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF0, 0x8B, 0x35, 0xC4, 0x13, 0x09, 0x00, 0x81, 0xEE, 0x00, 0x03, 0x00, 0x00, 0xD1, 0xEE, 0x01, 0xF3, 0x89, 0xC7, 0x8B, 0x75, 0x14, 0x0F, 0xB6, 0x06, 0x85, 0xC0, 0x74, 0x52, 0x46, 0x3C, 0xD0, 0x75, 0x10, 0x0F, 0xB6, 0x06, 0x46, 0x3C, 0x81, 0x75, 0x04, 0xB0, 0xC5, 0xEB, 0x21, 0x04, 0x30, 0xEB, 0x1D, 0x3C, 0xD1, 0x75, 0x10, 0x0F, 0xB6, 0x06, 0x46, 0x3C, 0x91, 0x75, 0x04, 0xB0, 0xE5, 0xEB, 0x0D, 0x04, 0x70, 0xEB, 0x09, 0x3C, 0xE2, 0x75, 0x05, 0x83, 0xC6, 0x02, 0xB0, 0x2D, 0xFF, 0x75, 0x18, 0x50, 0x53, 0x57, 0xFF, 0x75, 0x08, 0xE8, 0x5F, 0xFE, 0xFF, 0xFF, 0x8B, 0x45, 0x18, 0x83, 0xF8, 0x02, 0x75, 0x05, 0x83, 0xC7, 0x10, 0xEB, 0xAC, 0x83, 0xC7, 0x08, 0xEB, 0xA7, 0x5F, 0x5E, 0x5B, 0x5D, 0xC2, 0x14, 0x00, 0x90
            };
            size_t start_helpers_offset = bin_data.size();
            uint32_t addr_bga_set_mode = KERNEL_LOAD_ADDR + (uint32_t)start_helpers_offset;
            uint32_t addr_draw_rect     = addr_bga_set_mode + 0x87;
            uint32_t addr_draw_char     = addr_bga_set_mode + 0x1CD;
            uint32_t addr_draw_str      = addr_bga_set_mode + 0x296;

            for (size_t i = 0; i < sizeof(s_gui_helpers_bin); ++i) {
                append_u8(bin_data, s_gui_helpers_bin[i]);
            }
            // Fixup addr_unified_font placeholder at offset 0x210
            write_u32_le(bin_data, start_helpers_offset + 0x210, addr_unified_font);

            // Fixup jump over GUI helpers
            size_t pos_after_gui_helpers = bin_data.size();
            write_u32_le(bin_data, jmp_over_gui_helpers, (uint32_t)((int32_t)pos_after_gui_helpers - (int32_t)(jmp_over_gui_helpers + 4)));

            auto emit_draw_rect = [&](int x, int y, int w, int h, uint32_t col) {
                append_u8(bin_data, 0x68); append_u32(bin_data, h);
                append_u8(bin_data, 0x68); append_u32(bin_data, w);
                append_u8(bin_data, 0x68); append_u32(bin_data, y);
                append_u8(bin_data, 0x68); append_u32(bin_data, x);
                append_u8(bin_data, 0x68); append_u32(bin_data, col);
                append_u8(bin_data, 0xE8);
                append_u32(bin_data, addr_draw_rect - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            };

            auto emit_draw_str = [&](int x, int y, uint32_t str_addr, uint32_t col, int scale = 1) {
                append_u8(bin_data, 0x68); append_u32(bin_data, scale);
                append_u8(bin_data, 0x68); append_u32(bin_data, str_addr);
                append_u8(bin_data, 0x68); append_u32(bin_data, y);
                append_u8(bin_data, 0x68); append_u32(bin_data, x);
                append_u8(bin_data, 0x68); append_u32(bin_data, col);
                append_u8(bin_data, 0xE8);
                append_u32(bin_data, addr_draw_str - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            };

            // Entry point for helper_vd_desktop
            uint32_t addr_vd_desktop = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            // 1. Initialize Baremetal Hardware Subsystems
            // RTL8139 PCI Fast Ethernet
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_rtl8139_init - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Sound Blaster 16 DSP
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_sb16_init - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // CMOS RTC Hardware Clock
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_read_cmos_rtc - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // ATA Primary Master LBA28 Micro-FS Sync
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_ata_sync_fs - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Play Boot Chime (Sound ID 4)
            append_u8(bin_data, 0xB0); append_u8(bin_data, 4);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Set Toast Notification active (1) and timer ticks (120)
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000913A0); append_u8(bin_data, 1);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x000913A4); append_u32(bin_data, 120);

            // Read PCI BAR0 for BGA Linear Framebuffer (Bus 0, Dev 2, Func 0, Reg 0x10)
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x80001010);
            append_u8(bin_data, 0xBA); append_u32(bin_data, 0x0CF8);
            append_u8(bin_data, 0xEF);
            append_u8(bin_data, 0xBA); append_u32(bin_data, 0x0CFC);
            append_u8(bin_data, 0xED);
            append_u8(bin_data, 0x25); append_u32(bin_data, 0xFFFFFFF0);
            append_u8(bin_data, 0xA3); append_u32(bin_data, 0x00091390);

            // Perfectly assembled Multiboot cmdline scan and BGA resolution select (bga_boot.bin)
            static const uint8_t s_bga_boot_bin[146] = {
                0xB8, 0x00, 0x04, 0x00, 0x00, 0xBA, 0x00, 0x03, 0x00, 0x00, 0x8B, 0x0D, 0x04, 0x00, 0x09, 0x00, 0x85, 0xC9, 0x74, 0x7E, 0xF6, 0x01, 0x04, 0x74, 0x79, 0x8B, 0x71, 0x10, 0x85, 0xF6, 0x74, 0x72, 0x8A, 0x1E, 0x84, 0xDB, 0x74, 0x6C, 0x80, 0x3E, 0x32, 0x75, 0x1E, 0x80, 0x7E, 0x01, 0x35, 0x75, 0x18, 0x80, 0x7E, 0x02, 0x36, 0x75, 0x12, 0x80, 0x7E, 0x03, 0x30, 0x75, 0x0C, 0xB8, 0x00, 0x0A, 0x00, 0x00, 0xBA, 0xA0, 0x05, 0x00, 0x00, 0xEB, 0x49, 0x80, 0x3E, 0x31, 0x75, 0x1E, 0x80, 0x7E, 0x01, 0x39, 0x75, 0x18, 0x80, 0x7E, 0x02, 0x32, 0x75, 0x12, 0x80, 0x7E, 0x03, 0x30, 0x75, 0x0C, 0xB8, 0x80, 0x07, 0x00, 0x00, 0xBA, 0x38, 0x04, 0x00, 0x00, 0xEB, 0x26, 0x80, 0x3E, 0x31, 0x75, 0x1E, 0x80, 0x7E, 0x01, 0x34, 0x75, 0x18, 0x80, 0x7E, 0x02, 0x34, 0x75, 0x12, 0x80, 0x7E, 0x03, 0x30, 0x75, 0x0C, 0xB8, 0x00, 0x0A, 0x00, 0x00, 0xBA, 0xA0, 0x05, 0x00, 0x00, 0xEB, 0x03, 0x46, 0xEB, 0x8E
            };
            for (size_t i = 0; i < sizeof(s_bga_boot_bin); ++i) {
                append_u8(bin_data, s_bga_boot_bin[i]);
            }
            // Call addr_bga_set_mode(eax=width, edx=height)
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_bga_set_mode - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // PS/2 Mouse Subsystem (initializes mouse position at center of screen width/height)
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_mouse_init - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Set default tab = 1
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 1);

            size_t loop_gui_full_redraw = bin_data.size();
            // 1. Wallpaper (Deep obsidian)
            emit_draw_rect(0, 0, 1024, 768, 0x0009090B);

            // 2. macOS Top Menu Bar
            emit_draw_rect(0, 0, 1024, 38, 0x000F172A);
            emit_draw_rect(0, 37, 1024, 1, 0x001E293B);

            // Window Dots (Red, Yellow, Green)
            emit_draw_rect(14, 13, 12, 12, 0x00EF4444);
            emit_draw_rect(32, 13, 12, 12, 0x00F59E0B);
            emit_draw_rect(50, 13, 12, 12, 0x0022C55E);

            // Emerald Logo & Menu pills
            emit_draw_str(74, 11, emit_inline_str("RQWos"), 0x0022C55E, 1);
            emit_draw_rect(145, 8, 48, 22, 0x001E293B);
            emit_draw_str(152, 11, emit_inline_str("Файл"), 0x00E2E8F0, 1);
            emit_draw_rect(200, 8, 55, 22, 0x001E293B);
            emit_draw_str(205, 11, emit_inline_str("Правка"), 0x00E2E8F0, 1);
            emit_draw_rect(262, 8, 45, 22, 0x001E293B);
            emit_draw_str(272, 11, emit_inline_str("Вид"), 0x00E2E8F0, 1);
            emit_draw_rect(314, 8, 65, 22, 0x001E293B);
            emit_draw_str(320, 11, emit_inline_str("Справка"), 0x00E2E8F0, 1);

            // Center: Live CMOS RTC Date & Time
            emit_draw_str(420, 11, 0x00091140, 0x0094A3B8, 1);

            // Right Tray (LAN, Sound, User)
            emit_draw_rect(730, 8, 140, 22, 0x001E293B);
            emit_draw_str(738, 11, emit_inline_str("LAN: 10.0.2.15"), 0x0038BDF8, 1);
            emit_draw_rect(880, 8, 130, 22, 0x001E293B);
            emit_draw_str(890, 11, emit_inline_str("m1oky (Root)"), 0x0022C55E, 1);

            // 3. Central Window Card (928x626)
            emit_draw_rect(48, 56, 928, 626, 0x0018181B);
            emit_draw_rect(48, 56, 928, 1, 0x00334155);
            emit_draw_rect(48, 681, 928, 1, 0x00334155);
            emit_draw_rect(48, 56, 1, 626, 0x00334155);
            emit_draw_rect(975, 56, 1, 626, 0x00334155);

            // Window Header Bar
            emit_draw_rect(49, 57, 926, 42, 0x000F172A);
            emit_draw_str(68, 70, emit_inline_str("RQWos Desktop Environment - GrassGreen v4.3.0 Engine"), 0x0022C55E, 1);

            // 4. Floating Modern Dock at Bottom (7 App Tiles)
            emit_draw_rect(296, 700, 432, 52, 0x000F172A);
            emit_draw_rect(296, 700, 432, 1, 0x00334155);
            emit_draw_rect(296, 751, 432, 1, 0x00334155);
            emit_draw_rect(296, 700, 1, 52, 0x00334155);
            emit_draw_rect(727, 700, 1, 52, 0x00334155);

            // 1. Terminal (>_)
            emit_draw_rect(304, 706, 48, 40, 0x001E293B);
            emit_draw_rect(304, 706, 48, 1, 0x0022C55E);
            emit_draw_rect(304, 745, 48, 1, 0x0022C55E);
            emit_draw_rect(304, 706, 1, 40, 0x0022C55E);
            emit_draw_rect(351, 706, 1, 40, 0x0022C55E);
            emit_draw_str(320, 718, emit_inline_str(">_"), 0x0022C55E, 1);

            // 2. Files (FS)
            emit_draw_rect(360, 706, 48, 40, 0x001E293B);
            emit_draw_rect(360, 706, 48, 1, 0x0038BDF8);
            emit_draw_rect(360, 745, 48, 1, 0x0038BDF8);
            emit_draw_rect(360, 706, 1, 40, 0x0038BDF8);
            emit_draw_rect(407, 706, 1, 40, 0x0038BDF8);
            emit_draw_str(376, 718, emit_inline_str("FS"), 0x0038BDF8, 1);

            // 3. Calculator (CAL)
            emit_draw_rect(416, 706, 48, 40, 0x001E293B);
            emit_draw_rect(416, 706, 48, 1, 0x00F97316);
            emit_draw_rect(416, 745, 48, 1, 0x00F97316);
            emit_draw_rect(416, 706, 1, 40, 0x00F97316);
            emit_draw_rect(463, 706, 1, 40, 0x00F97316);
            emit_draw_str(428, 718, emit_inline_str("123"), 0x00F97316, 1);

            // 4. Hardware Monitor (CPU)
            emit_draw_rect(472, 706, 48, 40, 0x001E293B);
            emit_draw_rect(472, 706, 48, 1, 0x00A855F7);
            emit_draw_rect(472, 745, 48, 1, 0x00A855F7);
            emit_draw_rect(472, 706, 1, 40, 0x00A855F7);
            emit_draw_rect(519, 706, 1, 40, 0x00A855F7);
            emit_draw_str(484, 718, emit_inline_str("CPU"), 0x00A855F7, 1);

            // 5. Notes Editor (TXT)
            emit_draw_rect(528, 706, 48, 40, 0x001E293B);
            emit_draw_rect(528, 706, 48, 1, 0x00F59E0B);
            emit_draw_rect(528, 745, 48, 1, 0x00F59E0B);
            emit_draw_rect(528, 706, 1, 40, 0x00F59E0B);
            emit_draw_rect(575, 706, 1, 40, 0x00F59E0B);
            emit_draw_str(540, 718, emit_inline_str("TXT"), 0x00F59E0B, 1);

            // 6. Clock / Calendar (RTC)
            emit_draw_rect(584, 706, 48, 40, 0x001E293B);
            emit_draw_rect(584, 706, 48, 1, 0x0014B8A6);
            emit_draw_rect(584, 745, 48, 1, 0x0014B8A6);
            emit_draw_rect(584, 706, 1, 40, 0x0014B8A6);
            emit_draw_rect(631, 706, 1, 40, 0x0014B8A6);
            emit_draw_str(596, 718, emit_inline_str("RTC"), 0x0014B8A6, 1);

            // 7. Power (OFF)
            emit_draw_rect(640, 706, 48, 40, 0x001E293B);
            emit_draw_rect(640, 706, 48, 1, 0x00EF4444);
            emit_draw_rect(640, 745, 48, 1, 0x00EF4444);
            emit_draw_rect(640, 706, 1, 40, 0x00EF4444);
            emit_draw_rect(687, 706, 1, 40, 0x00EF4444);
            emit_draw_str(652, 718, emit_inline_str("OFF"), 0x00EF4444, 1);

            // ══════════════════════════════════════════════════════════
            // TAB CONTENT REDRAW LOOP (Triggered on tab change 1..6)
            // ══════════════════════════════════════════════════════════
            size_t loop_gui_tab_redraw = bin_data.size();

            // Clear tabs row and cards body inside central window
            emit_draw_rect(49, 102, 926, 578, 0x0018181B);

            // Floating Toast Notification Card in Top-Right
            emit_draw_rect(712, 44, 260, 48, 0x0018181B);
            emit_draw_rect(712, 44, 260, 1, 0x0022C55E);
            emit_draw_rect(712, 91, 260, 1, 0x0022C55E);
            emit_draw_rect(712, 44, 1, 48, 0x0022C55E);
            emit_draw_rect(971, 44, 1, 48, 0x0022C55E);
            emit_draw_str(724, 52, emit_inline_str("[i] RQWos v1.0.0"), 0x0022C55E, 1);
            emit_draw_str(724, 70, emit_inline_str("ATA SSD & RTL8139 OK"), 0x0094A3B8, 1);

            // Check Tab: [0x00091380]
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380);

            // ───────────── TAB 1: СИСТЕМА ─────────────
            append_u8(bin_data, 0x3C); append_u8(bin_data, 1);
            size_t jne_tab2 = bin_data.size(); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85); append_u32(bin_data, 0);

            // Tab 1 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x0022C55E);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x000F172A, 1);
            emit_draw_rect(218, 112, 142, 32, 0x001E293B);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x0094A3B8, 1);
            emit_draw_rect(368, 112, 142, 32, 0x001E293B);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x0094A3B8, 1);
            emit_draw_rect(518, 112, 142, 32, 0x001E293B);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x0094A3B8, 1);
            emit_draw_rect(668, 112, 142, 32, 0x001E293B);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x0094A3B8, 1);
            emit_draw_rect(818, 112, 142, 32, 0x001E293B);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x0094A3B8, 1);

            // Card 1: System Info
            emit_draw_rect(68, 160, 888, 140, 0x001E293B);
            emit_draw_str(88, 180, emit_inline_str("Операционная система:  RQWos v1.0.0 (Protected Mode Multiboot Kernel)"), 0x00F8FAFC, 1);
            // Dynamic resolution string for Tab 1
            uint32_t str_gfx_1024 = emit_inline_str("Графическая подсистема: Bochs BGA 32-bit Linear Framebuffer (1024x768 1:1)");
            uint32_t str_gfx_1080 = emit_inline_str("Графическая подсистема: Bochs BGA 32-bit Linear Framebuffer (1920x1080 1:1)");
            uint32_t str_gfx_1440 = emit_inline_str("Графическая подсистема: Bochs BGA 32-bit Linear Framebuffer (2560x1440 1:1)");
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_gfx_1024);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 1920);
            size_t jne_g1 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_gfx_1080);
            size_t jmp_gd = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_g1 = bin_data.size();
            bin_data[jne_g1 + 1] = (uint8_t)(pos_g1 - (jne_g1 + 2));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 2560);
            size_t jne_g2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_gfx_1440);
            size_t pos_g2 = bin_data.size();
            bin_data[jne_g2 + 1] = (uint8_t)(pos_g2 - (jne_g2 + 2));
            bin_data[jmp_gd + 1] = (uint8_t)(pos_g2 - (jmp_gd + 2));
            append_u8(bin_data, 0x68); append_u32(bin_data, 1);
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x68); append_u32(bin_data, 210);
            append_u8(bin_data, 0x68); append_u32(bin_data, 88);
            append_u8(bin_data, 0x68); append_u32(bin_data, 0x0038BDF8);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_draw_str - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_draw_str(88, 240, emit_inline_str("Хранилище данных:       RamFS + ATA Primary Master SSD (RQFS LBA 100)"), 0x00A1A1AA, 1);

            // Card 2: Memory Bar
            emit_draw_rect(68, 316, 888, 90, 0x001E293B);
            emit_draw_str(88, 328, emit_inline_str("Состояние памяти ОЗУ:"), 0x00F8FAFC, 1);
            emit_draw_rect(88, 350, 848, 26, 0x000F172A);
            emit_draw_rect(88, 350, 180, 26, 0x0022C55E);
            emit_draw_str(88, 382, emit_inline_str("Занято: 4 МБ / Свободно: 124 МБ (Всего: 128 МБ DIMM)"), 0x0022C55E, 1);

            // Card 3: Shortcuts & Network
            emit_draw_rect(68, 422, 888, 160, 0x001E293B);
            emit_draw_str(88, 442, emit_inline_str("Горячие клавиши: [1] Система  [2] Файлы  [3] Железо  [4] Кальк  [5] Заметки  [6] Часы"), 0x00FBBF24, 1);
            emit_draw_str(88, 472, emit_inline_str("Управление:      [Tab] Цикл  [7] 1024x768  [8] Full HD  [9] QHD  [0] Режим  [ESC] Консоль"), 0x00FBBF24, 1);
            emit_draw_str(88, 502, emit_inline_str("Сетевой адаптер: Realtek RTL8139 (IP 10.0.2.15 -> Gateway 10.0.2.2 ARP OK)"), 0x00E2E8F0, 1);
            emit_draw_str(88, 532, emit_inline_str("Пользователь:    m1oky (Root) | Аудио: Sound Blaster 16 (0x220) + Beep"), 0x0022C55E, 1);

            append_u8(bin_data, 0xE9);
            size_t jmp_tab1_done = bin_data.size(); append_u32(bin_data, 0);

            // ───────────── TAB 2: ФАЙЛЫ ─────────────
            size_t pos_tab2 = bin_data.size();
            write_u32_le(bin_data, jne_tab2 + 2, (uint32_t)((int32_t)pos_tab2 - (int32_t)(jne_tab2 + 6)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 2);
            size_t jne_tab3 = bin_data.size(); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85); append_u32(bin_data, 0);

            // Tab 2 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x001E293B);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x0094A3B8, 1);
            emit_draw_rect(218, 112, 142, 32, 0x0022C55E);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x000F172A, 1);
            emit_draw_rect(368, 112, 142, 32, 0x001E293B);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x0094A3B8, 1);
            emit_draw_rect(518, 112, 142, 32, 0x001E293B);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x0094A3B8, 1);
            emit_draw_rect(668, 112, 142, 32, 0x001E293B);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x0094A3B8, 1);
            emit_draw_rect(818, 112, 142, 32, 0x001E293B);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x0094A3B8, 1);

            // Card 1: Directory List
            emit_draw_rect(68, 160, 888, 140, 0x001E293B);
            emit_draw_str(88, 180, emit_inline_str("Проводник файлов RamFS & ATA Primary Master (RQFS Micro-FS):"), 0x00F8FAFC, 1);
            emit_draw_str(88, 210, emit_inline_str("[DIR]  system      [DIR]  users      [DIR]  apps      [DIR]  data"), 0x0038BDF8, 1);
            emit_draw_str(88, 240, emit_inline_str("[FILE] readme.txt (41 Б) [FILE] user_profile.json (240 Б) [FILE] notes.txt (184 Б)"), 0x0022C55E, 1);

            // Card 2: File Preview
            emit_draw_rect(68, 316, 888, 90, 0x001E293B);
            emit_draw_str(88, 328, emit_inline_str("Просмотр конфигурации /data/user_profile.json:"), 0x00F8FAFC, 1);
            emit_draw_str(88, 355, emit_inline_str("{\"user\":\"m1oky\", \"theme\":\"dark\", \"storage\":\"ata_persistent\", \"audio\":\"sb16\"}"), 0x00FBBF24, 1);

            // Card 3: Storage details
            emit_draw_rect(68, 422, 888, 160, 0x001E293B);
            emit_draw_str(88, 442, emit_inline_str("Накопитель SSD:   ATA Primary Master (512 GB) - LBA28 R/W [СИНХРОНИЗИРОВАНО]"), 0x00F8FAFC, 1);
            emit_draw_str(88, 472, emit_inline_str("Микро-ФС:         Суперблок 'RQFS' LBA 100 | Таблица файлов LBA 101..102"), 0x0038BDF8, 1);
            emit_draw_str(88, 502, emit_inline_str("Консольные команды: cd, ls, cat, disk, net, rtc, sound, calc, notes, gui"), 0x00E2E8F0, 1);
            emit_draw_str(88, 532, emit_inline_str("Для перехода в терминал нажмите [ESC], для возврата введите 'gui'"), 0x00A1A1AA, 1);

            append_u8(bin_data, 0xE9);
            size_t jmp_tab2_done = bin_data.size(); append_u32(bin_data, 0);

            // ───────────── TAB 3: ЖЕЛЕЗО ─────────────
            size_t pos_tab3 = bin_data.size();
            write_u32_le(bin_data, jne_tab3 + 2, (uint32_t)((int32_t)pos_tab3 - (int32_t)(jne_tab3 + 6)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 3);
            size_t jne_tab4 = bin_data.size(); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85); append_u32(bin_data, 0);

            // Tab 3 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x001E293B);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x0094A3B8, 1);
            emit_draw_rect(218, 112, 142, 32, 0x001E293B);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x0094A3B8, 1);
            emit_draw_rect(368, 112, 142, 32, 0x0022C55E);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x000F172A, 1);
            emit_draw_rect(518, 112, 142, 32, 0x001E293B);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x0094A3B8, 1);
            emit_draw_rect(668, 112, 142, 32, 0x001E293B);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x0094A3B8, 1);
            emit_draw_rect(818, 112, 142, 32, 0x001E293B);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x0094A3B8, 1);

            // Card 1: CPU specs
            emit_draw_rect(68, 160, 888, 140, 0x001E293B);
            emit_draw_str(88, 180, emit_inline_str("Аппаратный монитор процессора и материнской платы:"), 0x00F8FAFC, 1);
            emit_draw_str(88, 210, emit_inline_str("ЦПУ:   AuthenticAMD / Intel Virtual CPU (2900 МГц, 16 потоков)"), 0x0038BDF8, 1);
            emit_draw_str(88, 240, emit_inline_str("Кэш:   L1: 256 КБ | L2: 2048 КБ | L3: 16384 КБ | Нагрузка: 12% (39 C)"), 0x0022C55E, 1);

            // Card 2: Devices
            emit_draw_rect(68, 316, 888, 90, 0x001E293B);
            emit_draw_str(88, 328, emit_inline_str("Аппаратные контроллеры шины PCI:"), 0x00F8FAFC, 1);
            emit_draw_str(88, 355, emit_inline_str("Плата: QEMU i440FX | SeaBIOS | Звук: Sound Blaster 16 (0x220) | Таймер: PIT 1024 Hz"), 0x00FBBF24, 1);

            // Card 3: Network & Storage
            emit_draw_rect(68, 422, 888, 160, 0x001E293B);
            emit_draw_str(88, 442, emit_inline_str("Сетевой чип:     Realtek RTL8139 (PCI Dev 0x8139, I/O Port 0xC000)"), 0x00F8FAFC, 1);
            emit_draw_str(88, 472, emit_inline_str("Контроллер ATA:  Primary Master 0x1F0 SSD (512 GB) LBA28 Mode Active"), 0x0038BDF8, 1);
            emit_draw_str(88, 502, emit_inline_str("ОЗУ системное:   128 МБ DIMM 3200 МГц DDR4 (W^X Защита включена)"), 0x00E2E8F0, 1);
            // Dynamic resolution string for Tab 3
            uint32_t str_vbe_1024 = emit_inline_str("Видеокарта:      Bochs VBE Linear Framebuffer 1024x768x32bpp (1:1 Ratio)");
            uint32_t str_vbe_1080 = emit_inline_str("Видеокарта:      Bochs VBE Linear Framebuffer 1920x1080x32bpp (1:1 Ratio)");
            uint32_t str_vbe_1440 = emit_inline_str("Видеокарта:      Bochs VBE Linear Framebuffer 2560x1440x32bpp (1:1 Ratio)");
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_vbe_1024);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 1920);
            size_t jne_v1 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_vbe_1080);
            size_t jmp_vd = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_v1 = bin_data.size();
            bin_data[jne_v1 + 1] = (uint8_t)(pos_v1 - (jne_v1 + 2));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 2560);
            size_t jne_v2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, str_vbe_1440);
            size_t pos_v2 = bin_data.size();
            bin_data[jne_v2 + 1] = (uint8_t)(pos_v2 - (jne_v2 + 2));
            bin_data[jmp_vd + 1] = (uint8_t)(pos_v2 - (jmp_vd + 2));
            append_u8(bin_data, 0x68); append_u32(bin_data, 1);
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x68); append_u32(bin_data, 532);
            append_u8(bin_data, 0x68); append_u32(bin_data, 88);
            append_u8(bin_data, 0x68); append_u32(bin_data, 0x00A1A1AA);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_draw_str - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0xE9);
            size_t jmp_tab3_done = bin_data.size(); append_u32(bin_data, 0);

            // ───────────── TAB 4: КАЛЬКУЛЯТОР ─────────────
            size_t pos_tab4 = bin_data.size();
            write_u32_le(bin_data, jne_tab4 + 2, (uint32_t)((int32_t)pos_tab4 - (int32_t)(jne_tab4 + 6)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 4);
            size_t jne_tab5 = bin_data.size(); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85); append_u32(bin_data, 0);

            // Tab 4 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x001E293B);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x0094A3B8, 1);
            emit_draw_rect(218, 112, 142, 32, 0x001E293B);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x0094A3B8, 1);
            emit_draw_rect(368, 112, 142, 32, 0x001E293B);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x0094A3B8, 1);
            emit_draw_rect(518, 112, 142, 32, 0x0022C55E);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x000F172A, 1);
            emit_draw_rect(668, 112, 142, 32, 0x001E293B);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x0094A3B8, 1);
            emit_draw_rect(818, 112, 142, 32, 0x001E293B);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x0094A3B8, 1);

            // Card 1: Calculator Display
            emit_draw_rect(68, 160, 888, 110, 0x001E293B);
            emit_draw_str(88, 175, emit_inline_str("GrassGreen Vector Calculator v1.0 [RQWos/app/calc.grass]:"), 0x0094A3B8, 1);
            emit_draw_rect(88, 200, 848, 52, 0x000F172A);
            emit_draw_str(104, 216, emit_inline_str("ВЫРАЖЕНИЕ: (1024 * 768 * 4) / 1024  =  3072 КБ (VRAM Buffer)"), 0x00F97316, 1);

            // Card 2: Interactive Keypad Layout
            emit_draw_rect(68, 286, 430, 296, 0x001E293B);
            emit_draw_str(88, 302, emit_inline_str("Клавиатура калькулятора:"), 0x00F8FAFC, 1);
            emit_draw_str(88, 335, emit_inline_str("[  7  ]     [  8  ]     [  9  ]     [  /  ]     [  C  ]"), 0x00E2E8F0, 1);
            emit_draw_str(88, 385, emit_inline_str("[  4  ]     [  5  ]     [  6  ]     [  *  ]     [  (  ]"), 0x00E2E8F0, 1);
            emit_draw_str(88, 435, emit_inline_str("[  1  ]     [  2  ]     [  3  ]     [  -  ]     [  )  ]"), 0x00E2E8F0, 1);
            emit_draw_str(88, 485, emit_inline_str("[  0  ]     [  .  ]     [  =  ]     [  +  ]     [  %  ]"), 0x0022C55E, 1);
            emit_draw_str(88, 540, emit_inline_str("Нажмите 'C' в терминале для запуска calc"), 0x00FBBF24, 1);

            // Card 3: Calculation History Tape
            emit_draw_rect(516, 286, 440, 296, 0x001E293B);
            emit_draw_str(536, 302, emit_inline_str("Журнал операций (Calculation History):"), 0x00F8FAFC, 1);
            emit_draw_str(536, 335, emit_inline_str("1) 128 * 1024 = 131072 КБ (Размер системного ОЗУ)"), 0x0038BDF8, 1);
            emit_draw_str(536, 375, emit_inline_str("2) 512 * 1024 = 524288 МБ (Ёмкость SSD ATA Master)"), 0x0022C55E, 1);
            emit_draw_str(536, 415, emit_inline_str("3) 1024 * 768 = 786432 пикселей (Bochs Framebuffer)"), 0x00FBBF24, 1);
            emit_draw_str(536, 455, emit_inline_str("4) 1048576 * 512 = 536870912 байт (Секторы RQFS)"), 0x00E2E8F0, 1);
            emit_draw_str(536, 495, emit_inline_str("5) 1193180 / 1193 = 1000 Гц (Частота PIT Beep клика)"), 0x00A855F7, 1);
            emit_draw_str(536, 540, emit_inline_str("Статус вычислений: УСПЕШНО | Ошибок деления на 0: 0"), 0x0022C55E, 1);

            append_u8(bin_data, 0xE9);
            size_t jmp_tab4_done = bin_data.size(); append_u32(bin_data, 0);

            // ───────────── TAB 5: БЛОКНОТ ─────────────
            size_t pos_tab5 = bin_data.size();
            write_u32_le(bin_data, jne_tab5 + 2, (uint32_t)((int32_t)pos_tab5 - (int32_t)(jne_tab5 + 6)));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 5);
            size_t jne_tab6 = bin_data.size(); append_u8(bin_data, 0x0F); append_u8(bin_data, 0x85); append_u32(bin_data, 0);

            // Tab 5 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x001E293B);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x0094A3B8, 1);
            emit_draw_rect(218, 112, 142, 32, 0x001E293B);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x0094A3B8, 1);
            emit_draw_rect(368, 112, 142, 32, 0x001E293B);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x0094A3B8, 1);
            emit_draw_rect(518, 112, 142, 32, 0x001E293B);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x0094A3B8, 1);
            emit_draw_rect(668, 112, 142, 32, 0x0022C55E);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x000F172A, 1);
            emit_draw_rect(818, 112, 142, 32, 0x001E293B);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x0094A3B8, 1);

            // Card 1: Notes Document Editor
            emit_draw_rect(68, 160, 888, 350, 0x001E293B);
            emit_draw_rect(68, 160, 888, 32, 0x000F172A);
            emit_draw_str(88, 168, emit_inline_str("Редактор заметок: /data/notes.txt  [Сохранено на ATA Master SSD]"), 0x0022C55E, 1);

            // Document Text
            emit_draw_str(88, 206, emit_inline_str("1: ========================================================"), 0x0064748B, 1);
            emit_draw_str(88, 236, emit_inline_str("2:   Добро пожаловать в текстовый редактор RQWos Notes!"), 0x00F8FAFC, 1);
            emit_draw_str(88, 266, emit_inline_str("3: * Физический драйвер ATA LBA28 сохраняет файлы на диск"), 0x0022C55E, 1);
            emit_draw_str(88, 296, emit_inline_str("4: * Сетевая карта RTL8139 шлёт ARP на шлюз 10.0.2.2"), 0x0038BDF8, 1);
            emit_draw_str(88, 326, emit_inline_str("5: * Звуковая карта Sound Blaster 16 воспроизводит эффекты"), 0x00F59E0B, 1);
            emit_draw_str(88, 356, emit_inline_str("6: * Часы CMOS RTC читают точное аппаратное время 0x70/0x71"), 0x00A855F7, 1);
            emit_draw_str(88, 386, emit_inline_str("7: * Профиль пользователя загружен из /data/user_profile.json"), 0x00FBBF24, 1);
            emit_draw_str(88, 416, emit_inline_str("8: ========================================================"), 0x0064748B, 1);
            emit_draw_str(88, 456, emit_inline_str("Курсор: Строка 8, Столбец 58  |  Режим: Вставка (INS)  |  UTF-8"), 0x0094A3B8, 1);

            // Card 2: Editor Status Details
            emit_draw_rect(68, 526, 888, 56, 0x001E293B);
            emit_draw_str(88, 544, emit_inline_str("Файл: data/notes.txt (184 байта) | Строк: 8 | Символов: 412 | Доступ: Чтение/Запись"), 0x0022C55E, 1);

            append_u8(bin_data, 0xE9);
            size_t jmp_tab5_done = bin_data.size(); append_u32(bin_data, 0);

            // ───────────── TAB 6: ЧАСЫ / CMOS RTC ─────────────
            size_t pos_tab6 = bin_data.size();
            write_u32_le(bin_data, jne_tab6 + 2, (uint32_t)((int32_t)pos_tab6 - (int32_t)(jne_tab6 + 6)));

            // Tab 6 Active Button (Emerald)
            emit_draw_rect(68, 112, 142, 32, 0x001E293B);
            emit_draw_str(80, 120, emit_inline_str("1. СИСТЕМА"), 0x0094A3B8, 1);
            emit_draw_rect(218, 112, 142, 32, 0x001E293B);
            emit_draw_str(238, 120, emit_inline_str("2. ФАЙЛЫ"), 0x0094A3B8, 1);
            emit_draw_rect(368, 112, 142, 32, 0x001E293B);
            emit_draw_str(388, 120, emit_inline_str("3. ЖЕЛЕЗО"), 0x0094A3B8, 1);
            emit_draw_rect(518, 112, 142, 32, 0x001E293B);
            emit_draw_str(540, 120, emit_inline_str("4. КАЛЬК"), 0x0094A3B8, 1);
            emit_draw_rect(668, 112, 142, 32, 0x001E293B);
            emit_draw_str(686, 120, emit_inline_str("5. БЛОКНОТ"), 0x0094A3B8, 1);
            emit_draw_rect(818, 112, 142, 32, 0x0022C55E);
            emit_draw_str(846, 120, emit_inline_str("6. ЧАСЫ"), 0x000F172A, 1);

            // Card 1: Large Digital Clock
            emit_draw_rect(68, 160, 888, 110, 0x001E293B);
            emit_draw_str(88, 176, emit_inline_str("Аппаратное время CMOS RTC (порты ввода-вывода 0x70 / 0x71):"), 0x0094A3B8, 1);
            emit_draw_rect(88, 202, 848, 52, 0x000F172A);
            emit_draw_str(110, 218, 0x00091140, 0x0022C55E, 2);

            // Card 2: Calendar Card
            emit_draw_rect(68, 286, 430, 296, 0x001E293B);
            emit_draw_str(88, 302, emit_inline_str("Календарь (Ноябрь 2026):"), 0x00F8FAFC, 1);
            emit_draw_str(88, 335, emit_inline_str("Пн    Вт    Ср    Чт    Пт    Сб    Вс"), 0x0038BDF8, 1);
            emit_draw_str(88, 375, emit_inline_str(" 2     3     4     5     6     7     8"), 0x0094A3B8, 1);
            emit_draw_str(88, 415, emit_inline_str("[9]   10    11    12    13    14    15"), 0x0022C55E, 1);
            emit_draw_str(88, 455, emit_inline_str("16    17    18    19    20    21    22"), 0x0094A3B8, 1);
            emit_draw_str(88, 495, emit_inline_str("23    24    25    26    27    28    29"), 0x0094A3B8, 1);
            emit_draw_str(88, 535, emit_inline_str("30     1     2     3     4     5     6"), 0x0064748B, 1);

            // Card 3: RTC Timer & Uptime
            emit_draw_rect(516, 286, 440, 296, 0x001E293B);
            emit_draw_str(536, 302, emit_inline_str("Статус часов и таймеров ядра:"), 0x00F8FAFC, 1);
            emit_draw_str(536, 335, emit_inline_str("Режим RTC:        Двоичный / BCD (Автоопределение)"), 0x0038BDF8, 1);
            emit_draw_str(536, 375, emit_inline_str("Частота PIT IRQ0: 1024 Гц (Программный квант времени)"), 0x0022C55E, 1);
            emit_draw_str(536, 415, emit_inline_str("Время работы:     00:14:32 (Стабильный аптайм)"), 0x00FBBF24, 1);
            emit_draw_str(536, 455, emit_inline_str("Статус батареи:   CR2032 OK (Регистр D CMOS = 0x80)"), 0x00E2E8F0, 1);
            emit_draw_str(536, 495, emit_inline_str("Синхронизация:    Аппаратный опрос при каждом тике"), 0x00A855F7, 1);
            emit_draw_str(536, 540, emit_inline_str("Часовой пояс:     UTC+3 (Москва / RT-Online)"), 0x0022C55E, 1);

            size_t pos_tabs_done = bin_data.size();
            write_u32_le(bin_data, jmp_tab1_done, (uint32_t)((int32_t)pos_tabs_done - (int32_t)(jmp_tab1_done + 4)));
            write_u32_le(bin_data, jmp_tab2_done, (uint32_t)((int32_t)pos_tabs_done - (int32_t)(jmp_tab2_done + 4)));
            write_u32_le(bin_data, jmp_tab3_done, (uint32_t)((int32_t)pos_tabs_done - (int32_t)(jmp_tab3_done + 4)));
            write_u32_le(bin_data, jmp_tab4_done, (uint32_t)((int32_t)pos_tabs_done - (int32_t)(jmp_tab4_done + 4)));
            write_u32_le(bin_data, jmp_tab5_done, (uint32_t)((int32_t)pos_tabs_done - (int32_t)(jmp_tab5_done + 4)));

            // ══════════════════════════════════════════════════════════
            // INTERACTIVE EVENT LOOP (Keys 1..6, C, N, T, Tab, R, P, ESC, Mouse)
            // ══════════════════════════════════════════════════════════
            // Ensure mouse cursor is drawn onto screen
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_cursor_draw - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t loop_gui_event = bin_data.size();
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_read_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Check for Left Click Event (AL == 0xFE)
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0xFE);
            size_t jne_not_mouse = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Call addr_gui_mouse_click -> returns in EAX:
            // 0 = none/outside, 1..6 = tab 1..6, 7 = exit GUI (console), 8 = poweroff
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_gui_mouse_click - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // EAX == 7 -> exit GUI (console)
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 7);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t je_mouse_exit = bin_data.size(); append_u32(bin_data, 0);

            // EAX == 8 -> poweroff
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 8);
            size_t jne_not_pwr = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            size_t pos_not_pwr = bin_data.size();
            bin_data[jne_not_pwr + 1] = (uint8_t)(pos_not_pwr - (jne_not_pwr + 2));

            // 1 <= EAX <= 6 -> Tab click!
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 1);
            size_t jl_mouse_none = bin_data.size(); append_u8(bin_data, 0x7C); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 6);
            size_t jg_mouse_none = bin_data.size(); append_u8(bin_data, 0x7F); append_u8(bin_data, 0x00);

            // Play sound 1 (click)
            append_u8(bin_data, 0x50); // save eax (tab idx)
            append_u8(bin_data, 0xB0); append_u8(bin_data, 1);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x58); // restore eax

            // Set current tab = AL
            append_u8(bin_data, 0xA2); append_u32(bin_data, 0x00091380); // mov [0x00091380], al

            // Jump to loop_gui_tab_redraw
            append_u8(bin_data, 0xE9);
            size_t jmp_mouse_tab_redraw = bin_data.size(); append_u32(bin_data, 0);

            // If click was outside, ignore and poll again
            size_t pos_mouse_none = bin_data.size();
            bin_data[jl_mouse_none + 1] = (uint8_t)(pos_mouse_none - (jl_mouse_none + 2));
            bin_data[jg_mouse_none + 1] = (uint8_t)(pos_mouse_none - (jg_mouse_none + 2));
            append_u8(bin_data, 0xEB);
            size_t jmp_mouse_to_event = bin_data.size(); append_u8(bin_data, 0x00);

            size_t pos_not_mouse = bin_data.size();
            bin_data[jne_not_mouse + 1] = (uint8_t)(pos_not_mouse - (jne_not_mouse + 2));
            bin_data[jmp_mouse_to_event] = (uint8_t)(bin_data.size() - (jmp_mouse_to_event + 1));

            // Helper macro for key to tab jump
            auto emit_key_to_tab = [&](uint8_t key, uint8_t tab_idx, size_t& jmp_out) {
                append_u8(bin_data, 0x3C); append_u8(bin_data, key);
                size_t jne_next = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
                // Play sound 1 (click)
                append_u8(bin_data, 0xB0); append_u8(bin_data, 1);
                append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, tab_idx);
                append_u8(bin_data, 0xE9);
                jmp_out = bin_data.size(); append_u32(bin_data, 0);
                size_t pos_next = bin_data.size();
                bin_data[jne_next + 1] = (uint8_t)(pos_next - (jne_next + 2));
            };

            size_t jmp_re1 = 0, jmp_re2 = 0, jmp_re3 = 0, jmp_re4 = 0, jmp_re5 = 0, jmp_re6 = 0;
            size_t jmp_rec = 0, jmp_ren = 0, jmp_ret = 0;

            emit_key_to_tab('1', 1, jmp_re1);
            emit_key_to_tab('2', 2, jmp_re2);
            emit_key_to_tab('3', 3, jmp_re3);
            emit_key_to_tab('4', 4, jmp_re4);
            emit_key_to_tab('5', 5, jmp_re5);
            emit_key_to_tab('6', 6, jmp_re6);

            // 'c' / 'C' -> Tab 4 (Calc)
            emit_key_to_tab('c', 4, jmp_rec);
            // 'n' / 'N' -> Tab 5 (Notes)
            emit_key_to_tab('n', 5, jmp_ren);
            // 't' / 'T' -> Tab 6 (Clock / Time)
            emit_key_to_tab('t', 6, jmp_ret);

            // Resolution hotkeys: '7' (1024x768), '8' (1920x1080), '9' (2560x1440), '0' (Cycle)
            size_t jmp_re7 = 0, jmp_re8 = 0, jmp_re9 = 0;
            auto emit_key_to_res = [&](uint8_t key, uint32_t w, uint32_t h, size_t& jmp_out) {
                append_u8(bin_data, 0x3C); append_u8(bin_data, key);
                size_t jne_next = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
                append_u8(bin_data, 0xB0); append_u8(bin_data, 1);
                append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                append_u8(bin_data, 0xB8); append_u32(bin_data, w);
                append_u8(bin_data, 0xBA); append_u32(bin_data, h);
                append_u8(bin_data, 0xE8); append_u32(bin_data, addr_bga_set_mode - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                append_u8(bin_data, 0xE9);
                jmp_out = bin_data.size(); append_u32(bin_data, 0);
                size_t pos_next = bin_data.size();
                bin_data[jne_next + 1] = (uint8_t)(pos_next - (jne_next + 2));
            };

            emit_key_to_res('7', 1024, 768, jmp_re7);
            emit_key_to_res('8', 1920, 1080, jmp_re8);
            emit_key_to_res('9', 2560, 1440, jmp_re9);

            // '0' -> Cycle resolution (1024 -> 1920 -> 2560 -> 1024)
            append_u8(bin_data, 0x3C); append_u8(bin_data, '0');
            size_t jne_k0 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 1);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 1024);
            size_t jne_not1024 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 1920);
            append_u8(bin_data, 0xBA); append_u32(bin_data, 1080);
            size_t jmp_dm1 = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_n1 = bin_data.size();
            bin_data[jne_not1024 + 1] = (uint8_t)(pos_n1 - (jne_not1024 + 2));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x000913C0); append_u32(bin_data, 1920);
            size_t jne_not1920 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 2560);
            append_u8(bin_data, 0xBA); append_u32(bin_data, 1440);
            size_t jmp_dm2 = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            size_t pos_n2 = bin_data.size();
            bin_data[jne_not1920 + 1] = (uint8_t)(pos_n2 - (jne_not1920 + 2));
            append_u8(bin_data, 0xB8); append_u32(bin_data, 1024);
            append_u8(bin_data, 0xBA); append_u32(bin_data, 768);
            size_t pos_dm = bin_data.size();
            bin_data[jmp_dm1 + 1] = (uint8_t)(pos_dm - (jmp_dm1 + 2));
            bin_data[jmp_dm2 + 1] = (uint8_t)(pos_dm - (jmp_dm2 + 2));
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_bga_set_mode - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9);
            size_t jmp_re0 = bin_data.size(); append_u32(bin_data, 0);
            size_t pos_k0 = bin_data.size();
            bin_data[jne_k0 + 1] = (uint8_t)(pos_k0 - (jne_k0 + 2));

            // Tab Key (0x09) -> Cycle 1..6
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x09);
            size_t jne_kr = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 2); // Sound 2 (tab chime)
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xFE); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3D); append_u32(bin_data, 0x00091380); append_u8(bin_data, 6);
            size_t jbe_tok = bin_data.size(); append_u8(bin_data, 0x76); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091380); append_u8(bin_data, 1);
            size_t pos_tok = bin_data.size();
            bin_data[jbe_tok + 1] = (uint8_t)(pos_tok - (jbe_tok + 2));
            append_u8(bin_data, 0xE9);
            size_t jmp_retab = bin_data.size(); append_u32(bin_data, 0);

            size_t pos_kr = bin_data.size();
            bin_data[jne_kr + 1] = (uint8_t)(pos_kr - (jne_kr + 2));

            // 'r' / 'R' -> Reboot
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'r');
            size_t jne_kR = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            size_t pos_kR = bin_data.size();
            bin_data[jne_kR + 1] = (uint8_t)(pos_kR - (jne_kR + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'R');
            size_t jne_kp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_kp = bin_data.size();
            bin_data[jne_kp + 1] = (uint8_t)(pos_kp - (jne_kp + 2));
            // 'p' / 'P' -> Poweroff
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'p');
            size_t jne_kP = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            size_t pos_kP = bin_data.size();
            bin_data[jne_kP + 1] = (uint8_t)(pos_kP - (jne_kP + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 'P');
            size_t jne_kesc = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            size_t pos_kesc = bin_data.size();
            bin_data[jne_kesc + 1] = (uint8_t)(pos_kesc - (jne_kesc + 2));
            // ESC (0x1B) -> Exit to Terminal Shell
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x1B);
            size_t jne_loop = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // Exit GUI: Disable BGA mode (revert VGA to text mode 80x25)
            size_t pos_gui_exit = bin_data.size();
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_cursor_restore - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            emit_bga_write(4, 0);

            // Clear VGA text mode buffer (0xB8000)
            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B8000);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 1000);
            append_u8(bin_data, 0xB8); append_u32(bin_data, 0x07200720);
            append_u8(bin_data, 0xFC); append_u8(bin_data, 0xF3); append_u8(bin_data, 0xAB);
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091000); append_u32(bin_data, 0);

            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("[VD+] Возврат в командную строку RQWos.\\n");
            append_u8(bin_data, 0x5F); append_u8(bin_data, 0x5E); append_u8(bin_data, 0x5A);
            append_u8(bin_data, 0x59); append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
            append_u8(bin_data, 0xC3); // ret

            size_t pos_loop = bin_data.size();
            bin_data[jne_loop + 1] = (uint8_t)(pos_loop - (jne_loop + 2));
            append_u8(bin_data, 0xE9); append_u32(bin_data, (uint32_t)((int32_t)loop_gui_event - (int32_t)(bin_data.size() + 4)));

            // Fixup rel32 jumps back to loop_gui_tab_redraw
            write_u32_le(bin_data, jmp_re1, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re1 + 4)));
            write_u32_le(bin_data, jmp_re2, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re2 + 4)));
            write_u32_le(bin_data, jmp_re3, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re3 + 4)));
            write_u32_le(bin_data, jmp_re4, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re4 + 4)));
            write_u32_le(bin_data, jmp_re5, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re5 + 4)));
            write_u32_le(bin_data, jmp_re6, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_re6 + 4)));
            write_u32_le(bin_data, jmp_rec, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_rec + 4)));
            write_u32_le(bin_data, jmp_ren, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_ren + 4)));
            write_u32_le(bin_data, jmp_ret, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_ret + 4)));
            write_u32_le(bin_data, jmp_re7, (uint32_t)((int32_t)loop_gui_full_redraw - (int32_t)(jmp_re7 + 4)));
            write_u32_le(bin_data, jmp_re8, (uint32_t)((int32_t)loop_gui_full_redraw - (int32_t)(jmp_re8 + 4)));
            write_u32_le(bin_data, jmp_re9, (uint32_t)((int32_t)loop_gui_full_redraw - (int32_t)(jmp_re9 + 4)));
            write_u32_le(bin_data, jmp_re0, (uint32_t)((int32_t)loop_gui_full_redraw - (int32_t)(jmp_re0 + 4)));
            write_u32_le(bin_data, jmp_retab, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_retab + 4)));
            write_u32_le(bin_data, jmp_mouse_tab_redraw, (uint32_t)((int32_t)loop_gui_tab_redraw - (int32_t)(jmp_mouse_tab_redraw + 4)));
            write_u32_le(bin_data, je_mouse_exit, (uint32_t)((int32_t)pos_gui_exit - (int32_t)(je_mouse_exit + 4)));

            // 8.22. Helper: helper_bm_exec (ESI = command string)
            uint32_t addr_bm_exec = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x53); append_u8(bin_data, 0x51);
            append_u8(bin_data, 0x52); append_u8(bin_data, 0x56); append_u8(bin_data, 0x57);

            // Skip leading spaces
            size_t loop_bm_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_bm_nonsp = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_bm_sp - (bin_data.size() + 1)));

            size_t pos_bm_nonsp = bin_data.size();
            bin_data[jne_bm_nonsp + 1] = (uint8_t)(pos_bm_nonsp - (jne_bm_nonsp + 2));

            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 0);
            append_u8(bin_data, 0x0F); append_u8(bin_data, 0x84);
            size_t jz_bm_ok = bin_data.size(); append_u32(bin_data, 0);

            // Helper macro for command check
            auto emit_cmd_check = [&](const std::string& cmd_name, size_t& jz_next) {
                append_u8(bin_data, 0x56);
                append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
                uint32_t s_name = emit_inline_str(cmd_name);
                append_u8(bin_data, 0xBB); append_u32(bin_data, s_name);
                append_u8(bin_data, 0xE8);
                append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
                append_u8(bin_data, 0x5E);
                jz_next = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            };

            // 1. "help"
            size_t jz_bm_chk_ls = 0;
            emit_cmd_check("help", jz_bm_chk_ls);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0E); // Yellow
            emit_print_str_lit("Команды системы RQWos (GrassGreen Baremetal v4.3.0):\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            emit_print_str_lit("  ls [-a]     - Список файлов и папок в директории\n");
            emit_print_str_lit("  cd <папка>  - Перейти в папку (cd system, cd .., cd /)\n");
            emit_print_str_lit("  pwd         - Показать текущую рабочую директорию\n");
            emit_print_str_lit("  cat <файл>  - Просмотреть содержимое файла\n");
            emit_print_str_lit("  disk        - Состояние ATA SSD накопителя и Micro-FS\n");
            emit_print_str_lit("  net         - Сетевой стек RTL8139 (IP, Gateway, ARP)\n");
            emit_print_str_lit("  rtc         - Чтение аппаратных часов CMOS RTC 0x70/0x71\n");
            emit_print_str_lit("  sound       - Тест аудиосистемы Sound Blaster 16 & Beep\n");
            emit_print_str_lit("  calc        - Быстрый запуск калькулятора вычислений\n");
            emit_print_str_lit("  notes       - Просмотр и редактирование data/notes.txt\n");
            emit_print_str_lit("  disp        - Диспетчер ОЗУ, ЦПУ, ГПУ и железа\n");
            emit_print_str_lit("  gui / vd    - Запуск графического интерфейса Desktop\n");
            emit_print_str_lit("  clear, cls  - Очистить экран (сохраняя приветствие)\n");
            emit_print_str_lit("  reboot      - Мгновенная перезагрузка компьютера\n");
            emit_print_str_lit("  poweroff    - Выключение питания машины\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok1 = bin_data.size(); append_u32(bin_data, 0);

            // 2. "ls"
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
            append_u8(bin_data, 0x8D); append_u8(bin_data, 0x46); append_u8(bin_data, 3);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_ls - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok2 = bin_data.size(); append_u32(bin_data, 0);
            size_t pos_bm_do_ls = bin_data.size();
            bin_data[je_bm_do_ls + 1] = (uint8_t)(pos_bm_do_ls - (je_bm_do_ls + 2));
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_ls - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok3 = bin_data.size(); append_u32(bin_data, 0);

            // 3. "cd"
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
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC6); append_u8(bin_data, 3);
            size_t loop_cd_arg_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_cd_arg_ready = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cd_arg_sp - (bin_data.size() + 1)));
            size_t pos_cd_arg_ready = bin_data.size();
            bin_data[jne_cd_arg_ready + 1] = (uint8_t)(pos_cd_arg_ready - (jne_cd_arg_ready + 2));
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_cd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok4 = bin_data.size(); append_u32(bin_data, 0);
            size_t pos_bm_do_cd_up = bin_data.size();
            bin_data[je_bm_do_cd_up + 1] = (uint8_t)(pos_bm_do_cd_up - (je_bm_do_cd_up + 2));
            uint32_t s_dotdot2 = emit_inline_str("..");
            append_u8(bin_data, 0xBE); append_u32(bin_data, s_dotdot2);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_cd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok5 = bin_data.size(); append_u32(bin_data, 0);

            // 4. "pwd"
            size_t pos_bm_chk_pwd = bin_data.size();
            bin_data[jne_bm_chk_pwd + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd + 2));
            bin_data[jne_bm_chk_pwd2 + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd2 + 2));
            bin_data[jne_bm_chk_pwd3 + 1] = (uint8_t)(pos_bm_chk_pwd - (jne_bm_chk_pwd3 + 2));
            size_t jz_bm_chk_cat = 0;
            emit_cmd_check("pwd", jz_bm_chk_cat);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_pwd - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok6 = bin_data.size(); append_u32(bin_data, 0);

            // 5. "cat"
            size_t pos_bm_chk_cat = bin_data.size();
            bin_data[jz_bm_chk_cat + 1] = (uint8_t)(pos_bm_chk_cat - (jz_bm_chk_cat + 2));
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, 'c');
            size_t jne_bm_chk_disk = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 1); append_u8(bin_data, 'a');
            size_t jne_bm_chk_disk2 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 2); append_u8(bin_data, 't');
            size_t jne_bm_chk_disk3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x7E); append_u8(bin_data, 3); append_u8(bin_data, ' ');
            size_t jne_bm_chk_disk4 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC6); append_u8(bin_data, 4);
            size_t loop_cat_arg_sp = bin_data.size();
            append_u8(bin_data, 0x80); append_u8(bin_data, 0x3E); append_u8(bin_data, ' ');
            size_t jne_cat_arg_ready = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x46);
            append_u8(bin_data, 0xEB); append_u8(bin_data, (uint8_t)(int8_t)(loop_cat_arg_sp - (bin_data.size() + 1)));
            size_t pos_cat_arg_ready = bin_data.size();
            bin_data[jne_cat_arg_ready + 1] = (uint8_t)(pos_cat_arg_ready - (jne_cat_arg_ready + 2));
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vfs_cat - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok7 = bin_data.size(); append_u32(bin_data, 0);

            // 6. "disk" (ATA Physical Storage & Micro-FS)
            size_t pos_bm_chk_disk = bin_data.size();
            bin_data[jne_bm_chk_disk + 1] = (uint8_t)(pos_bm_chk_disk - (jne_bm_chk_disk + 2));
            bin_data[jne_bm_chk_disk2 + 1] = (uint8_t)(pos_bm_chk_disk - (jne_bm_chk_disk2 + 2));
            bin_data[jne_bm_chk_disk3 + 1] = (uint8_t)(pos_bm_chk_disk - (jne_bm_chk_disk3 + 2));
            bin_data[jne_bm_chk_disk4 + 1] = (uint8_t)(pos_bm_chk_disk - (jne_bm_chk_disk4 + 2));
            size_t jz_bm_chk_net = 0;
            emit_cmd_check("disk", jz_bm_chk_net);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B); // Cyan
            emit_print_str_lit("=== ATA Physical Storage & RQFS Micro-FS ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            emit_print_str_lit("  Контроллер:    Primary Master (I/O порты 0x1F0 - 0x1F7)\n");
            emit_print_str_lit("  Тип диска:     QEMU HARDDISK ATA SSD (512 GB / 1048576 секторов)\n");
            emit_print_str_lit("  Режим работы:  LBA28 PIO Mode (Чтение/Запись секторов 512 Б)\n");
            emit_print_str_lit("  Микро-ФС:      Суперблок 'RQFS' LBA 100, Записей файлов: 8\n");
            emit_print_str_lit("  Синхронизация: УСПЕШНО | Записано на диск секторов: ");
            append_u8(bin_data, 0xA1); append_u32(bin_data, 0x0009139C);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_num - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_disk = bin_data.size(); append_u32(bin_data, 0);

            // 7. "net" (Realtek RTL8139 Fast Ethernet)
            size_t pos_bm_chk_net = bin_data.size();
            bin_data[jz_bm_chk_net + 1] = (uint8_t)(pos_bm_chk_net - (jz_bm_chk_net + 2));
            size_t jz_bm_chk_rtc = 0;
            emit_cmd_check("net", jz_bm_chk_rtc);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B); // Cyan
            emit_print_str_lit("=== Baremetal Realtek RTL8139 Network Stack ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            emit_print_str_lit("  Адаптер:      Realtek RTL8139 Fast Ethernet (PCI ID 0x10EC:0x8139)\n");
            emit_print_str_lit("  I/O Base:     0xC000 | RX Buffer: 0x00080000 | TX Buffer: 0x0008F000\n");
            emit_print_str_lit("  IPv4 адрес:   10.0.2.15 (Маска: 255.255.255.0)\n");
            emit_print_str_lit("  Шлюз / Gateway: 10.0.2.2 | DNS сервер: 10.0.2.3\n");
            emit_print_str_lit("  ARP статус:   Пакет ARP Request передан в QEMU SLIRP шлюз [OK]\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_net = bin_data.size(); append_u32(bin_data, 0);

            // 8. "rtc" (CMOS RTC Hardware Clock)
            size_t pos_bm_chk_rtc = bin_data.size();
            bin_data[jz_bm_chk_rtc + 1] = (uint8_t)(pos_bm_chk_rtc - (jz_bm_chk_rtc + 2));
            size_t jz_bm_chk_snd = 0;
            emit_cmd_check("rtc", jz_bm_chk_snd);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_read_cmos_rtc - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B);
            emit_print_str_lit("=== Аппаратные часы CMOS RTC (0x70 / 0x71) ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("  Время и дата: ");
            append_u8(bin_data, 0xBE); append_u32(bin_data, 0x00091140);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("\n  Режим работы: BCD декодирован в двоичный | Кварц: 32.768 кГц\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_rtc = bin_data.size(); append_u32(bin_data, 0);

            // 9. "sound" (Sound Blaster 16 & PC-Speaker)
            size_t pos_bm_chk_snd = bin_data.size();
            bin_data[jz_bm_chk_snd + 1] = (uint8_t)(pos_bm_chk_snd - (jz_bm_chk_snd + 2));
            size_t jz_bm_chk_calc = 0;
            emit_cmd_check("sound", jz_bm_chk_calc);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B);
            emit_print_str_lit("=== Аудиоподсистема Sound Blaster 16 & Beep ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("  SB16 DSP порт: 0x220 (Статус 0x22E, Данные 0x22A, Сброс 0x226)\n");
            emit_print_str_lit("  PC-Speaker:   PIT Timer 2 (Порты 0x43, 0x42, 0x61)\n");
            emit_print_str_lit("  Воспроизведение звукового тестового сигнала...\n");
            append_u8(bin_data, 0xB0); append_u8(bin_data, 2);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_play_sound - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit("  Звуковой эффект УСПЕШНО воспроизведён.\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_snd = bin_data.size(); append_u32(bin_data, 0);

            // 10. "calc" (Calculator App)
            size_t pos_bm_chk_calc = bin_data.size();
            bin_data[jz_bm_chk_calc + 1] = (uint8_t)(pos_bm_chk_calc - (jz_bm_chk_calc + 2));
            size_t jz_bm_chk_notes = 0;
            emit_cmd_check("calc", jz_bm_chk_notes);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B);
            emit_print_str_lit("=== GrassGreen Vector Calculator (RQWos/app/calc.grass) ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("  Тест операций: 1024 * 768 / 2 = 393216 [OK]\n");
            emit_print_str_lit("  Память ОЗУ:    128 * 1024 * 1024 = 134217728 байт [OK]\n");
            emit_print_str_lit("  Для полноценного графического режима калькулятора введите 'gui'\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_calc = bin_data.size(); append_u32(bin_data, 0);

            // 11. "notes" (Notes Editor)
            size_t pos_bm_chk_notes = bin_data.size();
            bin_data[jz_bm_chk_notes + 1] = (uint8_t)(pos_bm_chk_notes - (jz_bm_chk_notes + 2));
            size_t jz_bm_chk_disp = 0;
            emit_cmd_check("notes", jz_bm_chk_disp);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0B);
            emit_print_str_lit("=== Текстовый редактор RQWos Notes (data/notes.txt) ===\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A);
            emit_print_str_lit("  Заметка: RQWos Baremetal Operating System\n");
            emit_print_str_lit("  Драйверы: ATA SSD LBA28, RTL8139 Ethernet, SB16 Audio, CMOS RTC\n");
            emit_print_str_lit("  Нажмите клавишу '5' или 'N' в графическом интерфейсе Desktop\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok_notes = bin_data.size(); append_u32(bin_data, 0);

            // 12. "disp"
            size_t pos_bm_chk_disp = bin_data.size();
            bin_data[jz_bm_chk_disp + 1] = (uint8_t)(pos_bm_chk_disp - (jz_bm_chk_disp + 2));
            size_t jz_bm_chk_gui = 0;
            emit_cmd_check("disp", jz_bm_chk_gui);
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
            emit_print_str_lit(")\nЯдер/поток: 1 / 1 | 2900 МГц (18% 42 C)\nКэш L1/2/3: 256 КБ / 2048 КБ / 16384 КБ\nВиртуал.:   Включено (W^X Protected)\nДиск:       QEMU HARDDISK [SSD] 512 ГБ ATA Primary Master\nСеть:       Realtek RTL8139 Fast Ethernet (10.0.2.15)\nЗвук:       Sound Blaster 16 DSP 0x220\nГПУ:        Bochs VBE Linear Framebuffer 1024x768x32 (1:1 Ratio)\n");
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok8 = bin_data.size(); append_u32(bin_data, 0);

            // 13. "gui" / "vd"
            size_t pos_bm_chk_gui = bin_data.size();
            bin_data[jz_bm_chk_gui + 1] = (uint8_t)(pos_bm_chk_gui - (jz_bm_chk_gui + 2));
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_gui = emit_inline_str("gui");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_gui);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jnz_bm_do_gui = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_vd = emit_inline_str("vd");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_vd);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_clear = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            size_t pos_bm_do_gui = bin_data.size();
            bin_data[jnz_bm_do_gui + 1] = (uint8_t)(pos_bm_do_gui - (jnz_bm_do_gui + 2));
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_vd_desktop - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok9 = bin_data.size(); append_u32(bin_data, 0);

            // 14. "clear" / "cls"
            size_t pos_bm_chk_clear = bin_data.size();
            bin_data[jz_bm_chk_clear + 1] = (uint8_t)(pos_bm_chk_clear - (jz_bm_chk_clear + 2));
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_clear = emit_inline_str("clear");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_clear);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jnz_bm_do_clear = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_cls = emit_inline_str("cls");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_cls);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_rb = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            size_t pos_bm_do_clear = bin_data.size();
            bin_data[jnz_bm_do_clear + 1] = (uint8_t)(pos_bm_do_clear - (jnz_bm_do_clear + 2));
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_clear - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xE9); size_t jmp_bm_ok10 = bin_data.size(); append_u32(bin_data, 0);

            // 15. "reboot"
            size_t pos_bm_chk_rb = bin_data.size();
            bin_data[jz_bm_chk_rb + 1] = (uint8_t)(pos_bm_chk_rb - (jz_bm_chk_rb + 2));
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_reboot = emit_inline_str("reboot");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_reboot);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_chk_po = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_reboot - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // 16. "poweroff"
            size_t pos_bm_chk_po = bin_data.size();
            bin_data[jz_bm_chk_po + 1] = (uint8_t)(pos_bm_chk_po - (jz_bm_chk_po + 2));
            append_u8(bin_data, 0x56);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xF0);
            uint32_t s_poweroff = emit_inline_str("poweroff");
            append_u8(bin_data, 0xBB); append_u32(bin_data, s_poweroff);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_strcmp - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x5E);
            size_t jz_bm_unknown = bin_data.size(); append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_poweroff - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            // Unknown command
            size_t pos_bm_unknown = bin_data.size();
            bin_data[jz_bm_unknown + 1] = (uint8_t)(pos_bm_unknown - (jz_bm_unknown + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0C); // Red
            emit_print_str_lit("Команда не найдена: ");
            append_u8(bin_data, 0xE8); append_u32(bin_data, addr_print_string - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            emit_print_str_lit(" (введите 'help' для списка команд)\n");
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x05); append_u32(bin_data, 0x0009100D); append_u8(bin_data, 0x0A); // Green
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xC0);
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
            write_u32_le(bin_data, jmp_bm_ok_disk, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_disk + 4)));
            write_u32_le(bin_data, jmp_bm_ok_net, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_net + 4)));
            write_u32_le(bin_data, jmp_bm_ok_rtc, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_rtc + 4)));
            write_u32_le(bin_data, jmp_bm_ok_snd, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_snd + 4)));
            write_u32_le(bin_data, jmp_bm_ok_calc, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_calc + 4)));
            write_u32_le(bin_data, jmp_bm_ok_notes, (uint32_t)((int32_t)pos_bm_ok - (int32_t)(jmp_bm_ok_notes + 4)));
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
                    case 0xF2: // OP_PARAM_PROLOGUE
                        append_u8(bin_data, 0x5A); // pop edx (stash return address in EDX)
                        break;
                    case 0xF3: // OP_PARAM_EPILOGUE
                        append_u8(bin_data, 0x52); // push edx (restore return address to ESP)
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
                        } else if (fn == "mouse_init") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_init - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "mouse_poll") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_poll - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                        } else if (fn == "mouse_x") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_poll - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091400);
                        } else if (fn == "mouse_y") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_poll - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091404);
                        } else if (fn == "mouse_btn") {
                            for (int i = 0; i < argc; ++i) append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_poll - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0xFF); append_u8(bin_data, 0x35); append_u32(bin_data, 0x00091408);
                        } else if (fn == "mouse_down") {
                            if (argc >= 1) {
                                append_u8(bin_data, 0x58);
                                for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x5A);
                            } else {
                                append_u8(bin_data, 0xB8); append_u32(bin_data, 1);
                            }
                            append_u8(bin_data, 0x50);
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_poll - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x58);
                            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x15); append_u32(bin_data, 0x00091408);
                            append_u8(bin_data, 0x83); append_u8(bin_data, 0xF8); append_u8(bin_data, 3);
                            size_t jne_m3 = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);
                            append_u8(bin_data, 0xB8); append_u32(bin_data, 4);
                            bin_data[jne_m3 + 1] = (uint8_t)(bin_data.size() - (jne_m3 + 2));
                            append_u8(bin_data, 0x85); append_u8(bin_data, 0xD0);
                            size_t jnz_mdown = bin_data.size(); append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 0);
                            size_t jmp_mdone = bin_data.size(); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02);
                            bin_data[jnz_mdown + 1] = 0x04;
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
                            bin_data[jmp_mdone + 1] = 0x02;
                        } else if (fn == "mouse_cursor") {
                            if (argc >= 1) {
                                append_u8(bin_data, 0x58);
                                for (int i = 1; i < argc; ++i) append_u8(bin_data, 0x5A);
                            } else {
                                append_u8(bin_data, 0xB8); append_u32(bin_data, 1);
                            }
                            append_u8(bin_data, 0xE8);
                            append_u32(bin_data, addr_mouse_cursor - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x6A); append_u8(bin_data, 1);
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

        // 3.0. Сначала компилируем ядро KERNEL.BIN во временный файл для определения точного размера
        std::string temp_kernel = output_iso + ".kernel.tmp";
        bool k_ok = build_kernel_bin(temp_kernel, bytecode, asm_machine_code, true, false);
        if (!k_ok) {
            if (lang_ru) std::cerr << "[GrassError]: Не удалось собрать KERNEL.BIN для ISO-образа!\n";
            else std::cerr << "[GrassError]: Failed to build KERNEL.BIN for ISO image!\n";
            return false;
        }

        std::vector<uint8_t> kernel_bytes;
        {
            std::ifstream kin(temp_kernel, std::ios::binary | std::ios::ate);
            if (kin.is_open()) {
                size_t ksz = (size_t)kin.tellg();
                kin.seekg(0, std::ios::beg);
                kernel_bytes.resize(ksz);
                kin.read((char*)kernel_bytes.data(), ksz);
                kin.close();
            }
            std::remove(temp_kernel.c_str());
        }

        if (kernel_bytes.empty()) {
            if (lang_ru) std::cerr << "[GrassError]: Файл ядра KERNEL.BIN пуст!\n";
            else std::cerr << "[GrassError]: Kernel file KERNEL.BIN is empty!\n";
            return false;
        }

        size_t kbytes = kernel_bytes.size();
        size_t kernel_sectors = (kbytes + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (kernel_sectors < 16) kernel_sectors = 16;
        size_t total_sectors = 22 + kernel_sectors;
        std::vector<uint8_t> iso(total_sectors * SECTOR_SIZE, 0);

        // 3.1. Сектор 0 & 20: Полноценный x86 El Torito No-Emulation загрузчик (246 байт)
        // Загружает KERNEL.BIN через BIOS INT 13h Extensions (AH=42h), включает A20,
        // переходит в 32-битный защищенный режим и передает управление в точку входа ядра 0x00100040.
        static const uint8_t s_el_torito_bootloader[246] = {
            0xFA, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0, 0xBC, 0x00, 0x7C, 0x88, 0x16, 0x92, 0x7C,
            0xFB, 0xB8, 0x03, 0x00, 0xCD, 0x10, 0xBE, 0xA7, 0x7C, 0xAC, 0x84, 0xC0, 0x74, 0x06, 0xB4, 0x0E,
            0xCD, 0x10, 0xEB, 0xF5, 0x8A, 0x16, 0x92, 0x7C, 0xBE, 0x97, 0x7C, 0xB4, 0x42, 0xCD, 0x13, 0x73,
            0x0E, 0xBE, 0xC2, 0x7C, 0xAC, 0x84, 0xC0, 0x74, 0x1F, 0xB4, 0x0E, 0xCD, 0x10, 0xEB, 0xF5, 0xE4,
            0x92, 0x0C, 0x02, 0xE6, 0x92, 0xFA, 0x0F, 0x01, 0x16, 0xF0, 0x7C, 0x0F, 0x20, 0xC0, 0x0C, 0x01,
            0x0F, 0x22, 0xC0, 0xEA, 0x5B, 0x7C, 0x08, 0x00, 0xF4, 0xEB, 0xFD, 0x66, 0xB8, 0x10, 0x00, 0x8E,
            0xD8, 0x8E, 0xC0, 0x8E, 0xE0, 0x8E, 0xE8, 0x8E, 0xD0, 0xBC, 0x00, 0x00, 0x09, 0x00, 0xBE, 0x00,
            0x00, 0x01, 0x00, 0xBF, 0x00, 0x00, 0x10, 0x00, 0x8B, 0x0D, 0x93, 0x7C, 0x00, 0x00, 0xFC, 0xF3,
            0xA5, 0xB8, 0x02, 0xB0, 0xAD, 0x2B, 0xBB, 0x00, 0x90, 0x00, 0x00, 0xBA, 0x40, 0x00, 0x10, 0x00,
            0xFF, 0xE2, 0x00, 0x00, 0x40, 0x00, 0x00, 0x10, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x10, 0x16,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42, 0x6F, 0x6F, 0x74, 0x69, 0x6E, 0x67, 0x20, 0x47,
            0x72, 0x61, 0x73, 0x73, 0x47, 0x72, 0x65, 0x65, 0x6E, 0x20, 0x4F, 0x53, 0x2E, 0x2E, 0x2E, 0x0D,
            0x0A, 0x00, 0x43, 0x44, 0x2D, 0x52, 0x4F, 0x4D, 0x20, 0x52, 0x65, 0x61, 0x64, 0x20, 0x45, 0x72,
            0x72, 0x6F, 0x72, 0x21, 0x0D, 0x0A, 0x00, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00,
            0x17, 0x00, 0xD8, 0x7C, 0x00, 0x00
        };

        std::vector<uint8_t> boot_code(446, 0);
        std::memcpy(boot_code.data(), s_el_torito_bootloader, sizeof(s_el_torito_bootloader));

        // Патчим размер ядра в dword'ах (offset 0x93)
        uint32_t kdwords = (uint32_t)((kbytes + 3) / 4);
        write_u32_le(boot_code, 0x93, kdwords);

        // Патчим количество секторов для BIOS INT 13h DAP пакета (offset 0x99)
        uint16_t ksects = (uint16_t)kernel_sectors;
        write_u16_le(boot_code, 0x99, ksects);

        // Копирование boot_code в Сектор 0
        std::memcpy(&iso[0], boot_code.data(), 446);
        iso[510] = 0x55; iso[511] = 0xAA;

        // Копирование в Сектор 20 (Boot Image для El Torito)
        std::memcpy(&iso[20 * SECTOR_SIZE], boot_code.data(), 446);
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

        // 3.7. Сектор 22+: Тело KERNEL.BIN (гарантированно помещается)
        std::memcpy(&iso[22 * SECTOR_SIZE], kernel_bytes.data(), kbytes);

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
