#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace grass::backend {

    enum class TargetArch : uint8_t {
        X86_64,
        X86_32
    };

    enum class TargetABI : uint8_t {
        Win64,
        SysV64,
        Cdecl32,
        Baremetal64,
        Baremetal32
    };

    enum class SectionKind : uint8_t {
        Text,
        RoData,
        Data,
        Bss
    };

    enum class RelocType : uint8_t {
        Rel32,
        Abs64,
        Abs32
    };

    enum class Reg : uint16_t {
        // 64-bit General Purpose Registers
        RAX = 0x0030, RCX = 0x0031, RDX = 0x0032, RBX = 0x0033,
        RSP = 0x0034, RBP = 0x0035, RSI = 0x0036, RDI = 0x0037,
        R8  = 0x0038, R9  = 0x0039, R10 = 0x003A, R11 = 0x003B,
        R12 = 0x003C, R13 = 0x003D, R14 = 0x003E, R15 = 0x003F,

        // 32-bit General Purpose Registers
        EAX  = 0x0020, ECX  = 0x0021, EDX  = 0x0022, EBX  = 0x0023,
        ESP  = 0x0024, EBP  = 0x0025, ESI  = 0x0026, EDI  = 0x0027,
        R8D  = 0x0028, R9D  = 0x0029, R10D = 0x002A, R11D = 0x002B,
        R12D = 0x002C, R13D = 0x002D, R14D = 0x002E, R15D = 0x002F,

        // 16-bit General Purpose Registers
        AX   = 0x0010, CX   = 0x0011, DX   = 0x0012, BX   = 0x0013,
        SP   = 0x0014, BP   = 0x0015, SI   = 0x0016, DI   = 0x0017,
        R8W  = 0x0018, R9W  = 0x0019, R10W = 0x001A, R11W = 0x001B,
        R12W = 0x001C, R13W = 0x001D, R14W = 0x001E, R15W = 0x001F,

        // 8-bit Low General Purpose Registers
        AL   = 0x0000, CL   = 0x0001, DL   = 0x0002, BL   = 0x0003,
        SPL  = 0x0004, BPL  = 0x0005, SIL  = 0x0006, DIL  = 0x0007,
        R8B  = 0x0008, R9B  = 0x0009, R10B = 0x000A, R11B = 0x000B,
        R12B = 0x000C, R13B = 0x000D, R14B = 0x000E, R15B = 0x000F,

        // 8-bit High General Purpose Registers (legacy AH, CH, DH, BH)
        AH   = 0x0044, CH   = 0x0045, DH   = 0x0046, BH   = 0x0047,

        // Instruction Pointer
        RIP  = 0x0060,

        // 128-bit SSE / Vector Registers
        XMM0  = 0x0050, XMM1  = 0x0051, XMM2  = 0x0052, XMM3  = 0x0053,
        XMM4  = 0x0054, XMM5  = 0x0055, XMM6  = 0x0056, XMM7  = 0x0057,
        XMM8  = 0x0058, XMM9  = 0x0059, XMM10 = 0x005A, XMM11 = 0x005B,
        XMM12 = 0x005C, XMM13 = 0x005D, XMM14 = 0x005E, XMM15 = 0x005F,

        // Invalid / Sentinel
        None = 0xFFFF
    };

    // Helper functions for inspecting registers
    inline uint8_t reg_raw_index(Reg r) {
        return static_cast<uint8_t>(static_cast<uint16_t>(r) & 0x000F);
    }

    inline uint8_t reg_kind(Reg r) {
        return static_cast<uint8_t>((static_cast<uint16_t>(r) >> 4) & 0x000F);
    }

    inline uint8_t reg_size(Reg r) {
        switch (reg_kind(r)) {
            case 0: return 1; // 8-bit low
            case 1: return 2; // 16-bit
            case 2: return 4; // 32-bit
            case 3: return 8; // 64-bit
            case 4: return 1; // 8-bit high
            case 5: return 8; // XMM scalar double default size in bytes
            case 6: return 8; // RIP
            default: return 0;
        }
    }

    inline bool reg_is_gpr(Reg r) {
        return reg_kind(r) <= 4;
    }

    inline Reg reg_with_size(Reg r, uint8_t sz) {
        if (!reg_is_gpr(r)) return r;
        uint8_t idx = reg_raw_index(r);
        switch (sz) {
            case 1: return static_cast<Reg>(0x0000 | idx);
            case 2: return static_cast<Reg>(0x0010 | idx);
            case 4: return static_cast<Reg>(0x0020 | idx);
            case 8: return static_cast<Reg>(0x0030 | idx);
            default: return r;
        }
    }

    inline bool reg_is_xmm(Reg r) {
        return reg_kind(r) == 5;
    }

    inline bool reg_is_rip(Reg r) {
        return reg_kind(r) == 6;
    }

    inline bool reg_is_ext(Reg r) {
        return reg_raw_index(r) >= 8;
    }

    inline bool reg_is_high_byte(Reg r) {
        return reg_kind(r) == 4;
    }

    inline bool reg_is_rex_byte(Reg r) {
        return reg_kind(r) == 0 && (reg_raw_index(r) >= 4);
    }

    struct RelocEntry {
        SectionKind section = SectionKind::Text;
        uint64_t offset = 0;
        std::string symbol_name;
        RelocType type = RelocType::Rel32;
        int64_t addend = 0;
    };

    struct SymbolDef {
        std::string name;
        SectionKind section = SectionKind::Text;
        uint64_t offset = 0;
        uint64_t size = 0;
        bool is_global = true;
        bool is_external = false;
    };

    struct CompiledUnit {
        TargetArch arch = TargetArch::X86_64;
        TargetABI abi = TargetABI::Win64;
        std::vector<uint8_t> text;
        std::vector<uint8_t> rodata;
        std::vector<uint8_t> data;
        uint64_t bss_size = 0;
        std::vector<SymbolDef> symbols;
        std::vector<RelocEntry> relocations;
    };

} // namespace grass::backend
