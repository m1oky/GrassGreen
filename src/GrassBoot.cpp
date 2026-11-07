// =============================================================================
//  GrassGreen 4.2.0 — Реализация сборки ядра, драйверов и ISO-образов
//  Поддержка: --multiboot, --kernel (.bin), --sys (.sys), --iso (.iso)
// =============================================================================
#include "GrassBoot.h"
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

        // 1.1. Multiboot 1 Header (размещается строго в первых 8192 байтах с выравниванием по 4 байтам)
        const uint32_t MULTIBOOT_MAGIC = 0x1BADB002;
        // Флаги: бит 0 (выравнивание модулей по 4K), бит 1 (информация о памяти), бит 16 (AOUT_KLUDGE для плоских .bin)
        const uint32_t MULTIBOOT_FLAGS = 0x00010003;
        const uint32_t MULTIBOOT_CHECKSUM = (uint32_t)(-(int64_t)(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS));

        const uint32_t KERNEL_LOAD_ADDR   = 0x00100000; // 1 MB (стандартная точка загрузки ядра)
        const uint32_t KERNEL_HEADER_ADDR = 0x00100000;
        const uint32_t KERNEL_ENTRY_ADDR  = 0x00100040; // Точка входа в Protected Mode (смещение 0x40)

        // Запись заголовка Multiboot 1 (32 байта, смещение 0x00)
        write_u32_le(bin_data, 0x00, MULTIBOOT_MAGIC);
        write_u32_le(bin_data, 0x04, MULTIBOOT_FLAGS);
        write_u32_le(bin_data, 0x08, MULTIBOOT_CHECKSUM);
        write_u32_le(bin_data, 0x0C, KERNEL_HEADER_ADDR);
        write_u32_le(bin_data, 0x10, KERNEL_LOAD_ADDR);
        write_u32_le(bin_data, 0x14, 0); // load_end_addr = 0 (загрузчик читает весь файл)
        write_u32_le(bin_data, 0x18, 0); // bss_end_addr = 0
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
        // Завершающий тег Multiboot 2 (type = 0, size = 8)
        write_u16_le(bin_data, 0x30, 0);
        write_u16_le(bin_data, 0x32, 0);
        write_u32_le(bin_data, 0x34, 8);
        write_u32_le(bin_data, 0x38, 0);
        write_u32_le(bin_data, 0x3C, 0);

        // 1.3. Точка входа ядра в 32-битном Protected Mode (смещение 0x40 / 0x00100040)
        bin_data.resize(0x40, 0);

        // cli (0xFA), cld (0xFC)
        append_u8(bin_data, 0xFA);
        append_u8(bin_data, 0xFC);

        // mov esp, 0x001FFFF0 (BC F0 FF 1F 00)
        append_u8(bin_data, 0xBC);
        append_u32(bin_data, 0x001FFFF0);

        // mov [0x00090000], eax (Multiboot Magic: A3 00 00 09 00)
        append_u8(bin_data, 0xA3);
        append_u32(bin_data, 0x00090000);

        // mov [0x00090004], ebx (Multiboot Info: 89 1D 04 00 09 00)
        append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D);
        append_u32(bin_data, 0x00090004);

        // Инициализация последовательного порта COM1 (0x3F8)
        // mov dx, 0x3F9; xor al, al; out dx, al (отключение прерываний COM1)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F9); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0x30); append_u8(bin_data, 0xC0);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3FB; mov al, 0x80; out dx, al (DLAB)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FB); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x80);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3F8; mov al, 0x03; out dx, al (divisor 38400)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3F9; xor al, al; out dx, al (divisor high)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F9); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0x30); append_u8(bin_data, 0xC0);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3FB; mov al, 0x03; out dx, al (8N1)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FB); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x03);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3FA; mov al, 0xC7; out dx, al (Enable 16-byte FIFO & clear buffers)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FA); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0xC7);
        append_u8(bin_data, 0xEE);

        // mov dx, 0x3FC; mov al, 0x0B; out dx, al (Modem Control: DTR, RTS, OUT2)
        append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FC); append_u16(bin_data, 0x0000);
        append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0B);
        append_u8(bin_data, 0xEE);

        // 1.4. Вывод приветственного баннера в видеотранслятор VGA (0xB8000)
        const char* banner = "GrassGreen OS v4.2.0 Kernel [Multiboot Online]";
        size_t banner_len = strlen(banner);
        for (size_t i = 0; i < banner_len; ++i) {
            uint32_t vga_offset = 0x000B8000 + (uint32_t)(i * 2);
            uint16_t cell = (uint16_t)(0x0A00 | (uint8_t)banner[i]);
            // mov word [vga_offset], cell -> 66 C7 05 <offset> <cell>
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05);
            append_u32(bin_data, vga_offset);
            append_u16(bin_data, cell);
        }

        // Вывод сообщения в COM1 (0x3F8)
        const char* com_msg = "\r\n[GrassGreen OS v4.2.0]: Booted successfully via Multiboot!\r\n";
        for (size_t i = 0; i < strlen(com_msg); ++i) {
            // mov dx, 0x3F8; mov al, ch; out dx, al
            append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8); append_u16(bin_data, 0x0000);
            append_u8(bin_data, 0xB0); append_u8(bin_data, (uint8_t)com_msg[i]);
            append_u8(bin_data, 0xEE);
        }

        // 1.5. Нативный 32-битный Protected Mode рантайм для ядра GrassGreen
        if (!bytecode.empty()) {
            // Прыжок через встроенные процедуры ядра
            append_u8(bin_data, 0xE9); // jmp rel32
            size_t jmp_helpers_fixup = bin_data.size();
            append_u32(bin_data, 0); // placeholder

            // PS/2 Lookup Table (52 байта, сканкоды 0x02..0x35)
            uint32_t addr_ps2_table = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            static const uint8_t ps2_lut[52] = {
                '1','2','3','4','5','6','7','8','9','0','-','=', 0,
                '\t','q','w','e','r','t','y','u','i','o','p','[',']',
                0, 0,
                'a','s','d','f','g','h','j','k','l',';','\'','`',
                0,'\\','z','x','c','v','b','n','m',',','.','/'
            };
            for (int i = 0; i < 52; ++i) append_u8(bin_data, ps2_lut[i]);

            // --- Helper 1: helper_print_char (AL = char) ---
            uint32_t addr_print_char = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xC4); // mov ah, al (save char in AH)

            // COM1 TX
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD);
            size_t wait_tx1 = bin_data.size();
            append_u8(bin_data, 0xEC); // in al, dx
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x20); // test al, 0x20
            append_u8(bin_data, 0x74);
            append_u8(bin_data, (uint8_t)(int8_t)(wait_tx1 - (bin_data.size() + 1)));

            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8);
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xE0); // mov al, ah (restore char)
            append_u8(bin_data, 0xEE); // out dx, al

            // Newline? Send '\r' to COM1
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0A); // cmp ah, 0x0A
            size_t jne_skip_cr = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD);
            size_t wait_tx2 = bin_data.size();
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x20);
            append_u8(bin_data, 0x74);
            append_u8(bin_data, (uint8_t)(int8_t)(wait_tx2 - (bin_data.size() + 1)));

            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0D);
            append_u8(bin_data, 0xEE);

            bin_data[jne_skip_cr + 1] = (uint8_t)(bin_data.size() - (jne_skip_cr + 2));

            // VGA Display Step
            append_u8(bin_data, 0x8B); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000); // mov ebx, [0x91000]
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0A); // cmp ah, 0x0A
            size_t jne_vga_nl = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // ebx = (ebx / 160 + 1) * 160
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xD8);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2);
            append_u8(bin_data, 0xB9); append_u32(bin_data, 160);
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1);
            append_u8(bin_data, 0x40);
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xE1);
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xC3);
            size_t jmp_bounds = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jne_vga_nl + 1] = (uint8_t)(bin_data.size() - (jne_vga_nl + 2));

            // Backspace 0x08
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x08); // cmp ah, 0x08
            size_t jne_bksp = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x83); append_u8(bin_data, 0xFB); append_u8(bin_data, 0x02);
            size_t jb_vga = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xC7); append_u8(bin_data, 0x83);
            append_u32(bin_data, 0x000B8000);
            append_u16(bin_data, 0x0720);
            size_t jmp_done1 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jne_bksp + 1] = (uint8_t)(bin_data.size() - (jne_bksp + 2));

            // CR 0x0D -> ignore
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xFC); append_u8(bin_data, 0x0D); // cmp ah, 0x0D
            size_t je_cr = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Printable character: write to VGA
            append_u8(bin_data, 0x88); append_u8(bin_data, 0xA3); append_u32(bin_data, 0x000B8000); // mov [ebx + 0xB8000], ah
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x83); append_u32(bin_data, 0x000B8001);
            append_u8(bin_data, 0x0A); // GrassGreen bright green
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xC3); append_u8(bin_data, 0x02);

            bin_data[jmp_bounds + 1] = (uint8_t)(bin_data.size() - (jmp_bounds + 2));
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFB); append_u32(bin_data, 4000);
            size_t jb_bounds = bin_data.size();
            append_u8(bin_data, 0x72); append_u8(bin_data, 0x02);
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xDB);

            size_t vga_done_pos = bin_data.size();
            bin_data[jb_vga + 1]   = (uint8_t)(vga_done_pos - (jb_vga + 2));
            bin_data[jmp_done1 + 1] = (uint8_t)(vga_done_pos - (jmp_done1 + 2));
            bin_data[je_cr + 1]    = (uint8_t)(vga_done_pos - (je_cr + 2));

            append_u8(bin_data, 0x89); append_u8(bin_data, 0x1D); append_u32(bin_data, 0x00091000);
            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0x59); // pop ecx
            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0xC3); // ret

            // --- Helper 2: helper_print_string (ESI = string ptr) ---
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
            append_u8(bin_data, 0xEB); // jmp loop_str
            append_u8(bin_data, (uint8_t)(int8_t)(loop_str - (bin_data.size() + 1)));

            bin_data[jz_str_done + 1] = (uint8_t)(bin_data.size() - (jz_str_done + 2));
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0xC3); // ret

            // --- Helper 3: helper_print_num (EAX = int) ---
            uint32_t addr_print_num = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x51); // push ecx
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x56); // push esi

            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x0008E020);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00); // mov byte [edi], 0

            append_u8(bin_data, 0x85); append_u8(bin_data, 0xC0); // test eax, eax
            size_t jns_pos = bin_data.size();
            append_u8(bin_data, 0x79); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xD8); // neg eax
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0xB0); append_u8(bin_data, '-');
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0x58); // pop eax

            bin_data[jns_pos + 1] = (uint8_t)(bin_data.size() - (jns_pos + 2));
            append_u8(bin_data, 0xB9); append_u32(bin_data, 10);

            size_t num_loop_pos = bin_data.size();
            append_u8(bin_data, 0x31); append_u8(bin_data, 0xD2); // xor edx, edx
            append_u8(bin_data, 0xF7); append_u8(bin_data, 0xF1); // div ecx
            append_u8(bin_data, 0x80); append_u8(bin_data, 0xC2); append_u8(bin_data, '0'); // add dl, '0'
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

            // --- Helper 4: helper_read_char (AL = returned char) ---
            uint32_t addr_read_char = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x52); // push edx
            append_u8(bin_data, 0x53); // push ebx

            size_t poll_top = bin_data.size();
            // COM1 check
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD);
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jnz_com = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // PS/2 check
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x64);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jnz_ps2 = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // pause; jmp poll_top
            append_u8(bin_data, 0xF3); append_u8(bin_data, 0x90);
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(poll_top - (bin_data.size() + 1)));

            // COM1 ready
            bin_data[jnz_com + 1] = (uint8_t)(bin_data.size() - (jnz_com + 2));
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8);
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0D);
            size_t jne_com_cr = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x00);

            // It was 0x0D: check if 0x0A is waiting in FIFO
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03FD);
            append_u8(bin_data, 0xEC);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x01);
            size_t jz_no_lf = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x06);
            append_u8(bin_data, 0x66); append_u8(bin_data, 0xBA); append_u16(bin_data, 0x03F8);
            append_u8(bin_data, 0xEC); // consume 0x0A
            bin_data[jz_no_lf + 1] = 0x06;

            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
            size_t jmp_got1 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);
            bin_data[jne_com_cr + 1] = (uint8_t)(bin_data.size() - (jne_com_cr + 2));

            // PS/2 ready
            bin_data[jnz_ps2 + 1] = (uint8_t)(bin_data.size() - (jnz_ps2 + 2));
            append_u8(bin_data, 0xE4); append_u8(bin_data, 0x60);
            append_u8(bin_data, 0xA8); append_u8(bin_data, 0x80); // key release?
            append_u8(bin_data, 0x75);
            append_u8(bin_data, (uint8_t)(int8_t)(poll_top - (bin_data.size() + 1)));

            // Enter 0x1C
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x1C);
            size_t jne_ps2_enter = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
            size_t jmp_got2 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Backspace 0x0E
            bin_data[jne_ps2_enter + 1] = (uint8_t)(bin_data.size() - (jne_ps2_enter + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0E);
            size_t jne_ps2_bksp = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x08);
            size_t jmp_got3 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Space 0x39
            bin_data[jne_ps2_bksp + 1] = (uint8_t)(bin_data.size() - (jne_ps2_bksp + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x39);
            size_t jne_ps2_space = bin_data.size();
            append_u8(bin_data, 0x75); append_u8(bin_data, 0x04);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x20);
            size_t jmp_got4 = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            // Table lookup
            bin_data[jne_ps2_space + 1] = (uint8_t)(bin_data.size() - (jne_ps2_space + 2));
            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x02);
            append_u8(bin_data, 0x72);
            append_u8(bin_data, (uint8_t)(int8_t)(poll_top - (bin_data.size() + 1)));

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x35);
            append_u8(bin_data, 0x77);
            append_u8(bin_data, (uint8_t)(int8_t)(poll_top - (bin_data.size() + 1)));

            append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xD8);
            append_u8(bin_data, 0x83); append_u8(bin_data, 0xEB); append_u8(bin_data, 0x02);
            append_u8(bin_data, 0x8A); append_u8(bin_data, 0x83); append_u32(bin_data, addr_ps2_table);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x74);
            append_u8(bin_data, (uint8_t)(int8_t)(poll_top - (bin_data.size() + 1)));

            // got_char:
            size_t got_char_pos = bin_data.size();
            bin_data[jmp_got1 + 1] = (uint8_t)(got_char_pos - (jmp_got1 + 2));
            bin_data[jmp_got2 + 1] = (uint8_t)(got_char_pos - (jmp_got2 + 2));
            bin_data[jmp_got3 + 1] = (uint8_t)(got_char_pos - (jmp_got3 + 2));
            bin_data[jmp_got4 + 1] = (uint8_t)(got_char_pos - (jmp_got4 + 2));

            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x5A); // pop edx
            append_u8(bin_data, 0xC3); // ret

            // --- Helper 5: helper_read_line (EDI = buffer) ---
            uint32_t addr_read_line = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x53); // push ebx
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFB); // mov ebx, edi

            size_t rl_top = bin_data.size();
            append_u8(bin_data, 0xE8); // call helper_read_char
            append_u32(bin_data, addr_read_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x0A); // Enter
            size_t je_rl_enter = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            append_u8(bin_data, 0x3C); append_u8(bin_data, 0x08); // Backspace
            size_t je_rl_bksp = bin_data.size();
            append_u8(bin_data, 0x74); append_u8(bin_data, 0x00);

            // Regular char
            append_u8(bin_data, 0x89); append_u8(bin_data, 0xFA);
            append_u8(bin_data, 0x29); append_u8(bin_data, 0xDA);
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFA); append_u32(bin_data, 250);
            append_u8(bin_data, 0x73);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            append_u8(bin_data, 0x88); append_u8(bin_data, 0x07);
            append_u8(bin_data, 0x47);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            // rl_bksp
            bin_data[je_rl_bksp + 1] = (uint8_t)(bin_data.size() - (je_rl_bksp + 2));
            append_u8(bin_data, 0x39); append_u8(bin_data, 0xDF);
            append_u8(bin_data, 0x76);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            append_u8(bin_data, 0x4F); // dec edi
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x08);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
            append_u8(bin_data, 0xEB);
            append_u8(bin_data, (uint8_t)(int8_t)(rl_top - (bin_data.size() + 1)));

            // rl_enter
            bin_data[je_rl_enter + 1] = (uint8_t)(bin_data.size() - (je_rl_enter + 2));
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xB0); append_u8(bin_data, 0x0A);
            append_u8(bin_data, 0xE8);
            append_u32(bin_data, addr_print_char - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));

            append_u8(bin_data, 0x5B); // pop ebx
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0xC3); // ret

            // --- Helper 6: helper_copy_string (ESI = src, EDI = dst) ---
            uint32_t addr_copy_string = KERNEL_LOAD_ADDR + (uint32_t)bin_data.size();
            append_u8(bin_data, 0x56); // push esi
            append_u8(bin_data, 0x57); // push edi
            append_u8(bin_data, 0x50); // push eax
            append_u8(bin_data, 0x81); append_u8(bin_data, 0xFE); append_u32(bin_data, 0x00010000);
            size_t jae_cpy = bin_data.size();
            append_u8(bin_data, 0x73); append_u8(bin_data, 0x00);
            append_u8(bin_data, 0xC6); append_u8(bin_data, 0x07); append_u8(bin_data, 0x00);
            size_t jmp_cpydone = bin_data.size();
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0x00);

            bin_data[jae_cpy + 1] = (uint8_t)(bin_data.size() - (jae_cpy + 2));
            size_t cpy_loop = bin_data.size();
            append_u8(bin_data, 0xAC);
            append_u8(bin_data, 0xAA);
            append_u8(bin_data, 0x84); append_u8(bin_data, 0xC0);
            append_u8(bin_data, 0x75);
            append_u8(bin_data, (uint8_t)(int8_t)(cpy_loop - (bin_data.size() + 1)));

            bin_data[jmp_cpydone + 1] = (uint8_t)(bin_data.size() - (jmp_cpydone + 2));
            append_u8(bin_data, 0x58); // pop eax
            append_u8(bin_data, 0x5F); // pop edi
            append_u8(bin_data, 0x5E); // pop esi
            append_u8(bin_data, 0xC3); // ret

            // Fixup jump over helpers
            uint32_t after_helpers_offset = (uint32_t)bin_data.size();
            uint32_t rel_helpers = after_helpers_offset - (uint32_t)(jmp_helpers_fixup + 4);
            write_u32_le(bin_data, jmp_helpers_fixup, rel_helpers);

            // Инициализация позиции VGA курсора: строка 1 (смещение 160)
            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05);
            append_u32(bin_data, 0x00091000);
            append_u32(bin_data, 160);

            // Исполнение inline asm вставок (если есть)
            if (!asm_machine_code.empty()) {
                for (uint8_t b : asm_machine_code) append_u8(bin_data, b);
            }

            // Трансляция байт-кода программы в машинный код Protected Mode
            struct StrFixup { size_t mc_pos; std::string text; };
            std::vector<StrFixup> str_fixups;

            struct BranchFixup { size_t mc_pos; uint32_t target_bc; };
            std::vector<BranchFixup> branch_fixups;

            std::unordered_map<uint32_t, size_t> bc_to_mc;
            std::unordered_map<std::string, uint32_t> var_slots;
            uint32_t next_var_addr = 0x00080000;

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
                        if (var_slots.find(vname) == var_slots.end()) {
                            var_slots[vname] = next_var_addr;
                            next_var_addr += 256;
                        }
                        append_u8(bin_data, 0x68); append_u32(bin_data, var_slots[vname]);
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
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x94); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
                        break;
                    case 0x21: // OP_CMP_NEQ
                        append_u8(bin_data, 0x5B); append_u8(bin_data, 0x58);
                        append_u8(bin_data, 0x39); append_u8(bin_data, 0xD8);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0x95); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x0F); append_u8(bin_data, 0xB6); append_u8(bin_data, 0xC0);
                        append_u8(bin_data, 0x50);
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
                        if (fn == "gin" || fn == "input") {
                            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x0008F000); // mov edi, 0x8F000
                            append_u8(bin_data, 0xE8); // call read_line
                            append_u32(bin_data, addr_read_line - (KERNEL_LOAD_ADDR + (uint32_t)bin_data.size() + 4));
                            append_u8(bin_data, 0x68); append_u32(bin_data, 0x0008F000); // push 0x8F000
                        } else if (fn == "clear") {
                            append_u8(bin_data, 0xC7); append_u8(bin_data, 0x05); append_u32(bin_data, 0x00091000); append_u32(bin_data, 0);
                            append_u8(bin_data, 0xBF); append_u32(bin_data, 0x000B8000);
                            append_u8(bin_data, 0xB9); append_u32(bin_data, 2000);
                            append_u8(bin_data, 0x66); append_u8(bin_data, 0xB8); append_u16(bin_data, 0x0720);
                            append_u8(bin_data, 0xF3); append_u8(bin_data, 0x66); append_u8(bin_data, 0xAB);
                        }
                        break;
                    }
                    default:
                        break;
                }
            }

            bc_to_mc[(uint32_t)bytecode.size()] = bin_data.size();

            // Исправление относительных адресов переходов
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
                    append_u8(bin_data, 0); // null-терминатор
                }
                write_u32_le(bin_data, sf.mc_pos, emitted_strings[sf.text]);
            }
        } else {
            // Если байт-кода нет — только asm вставки и останов
            if (!asm_machine_code.empty()) {
                for (uint8_t b : asm_machine_code) append_u8(bin_data, b);
            }
            append_u8(bin_data, 0xFA); // cli
            append_u8(bin_data, 0xF4); // hlt
            append_u8(bin_data, 0xEB); append_u8(bin_data, 0xFD); // jmp -3
        }

        // 1.7. Встраивание байт-кода программы для VM/JIT ядра
        while (bin_data.size() % 16 != 0) append_u8(bin_data, 0x90); // nop

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

    // ── 2. Сборка системного драйвера ядра (--sys) ──
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
        size_t kernel_sectors = 4; // 8 КБ под ядро
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
