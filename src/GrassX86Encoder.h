#pragma once

#include "GrassBackendTypes.h"
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace grass::backend {

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

    enum class CondCode : uint8_t {
        O   = 0x0,
        NO  = 0x1,
        B   = 0x2, C = 0x2, NAE = 0x2,
        AE  = 0x3, NB = 0x3, NC = 0x3,
        E   = 0x4, Z = 0x4,
        NE  = 0x5, NZ = 0x5,
        BE  = 0x6, NA = 0x6,
        A   = 0x7, NBE = 0x7,
        S   = 0x8,
        NS  = 0x9,
        P   = 0xA, PE = 0xA,
        NP  = 0xB, PO = 0xB,
        L   = 0xC, NGE = 0xC,
        GE  = 0xD, NL = 0xD,
        LE  = 0xE, NG = 0xE,
        G   = 0xF, NLE = 0xF
    };

    using LabelId = int32_t;

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

    struct MemOp {
        Reg base = Reg::None;
        Reg index = Reg::None;
        uint8_t scale = 1; // 1, 2, 4, 8
        int32_t disp = 0;
        uint8_t size = 0; // 0=unspecified, 1, 2, 4, 8, 16
        bool is_rip_relative = false;
        bool is_abs32 = false;
        LabelId rip_label = -1;
        std::string rip_symbol;

        static MemOp base_disp(Reg b, int32_t d = 0, uint8_t s = 0) {
            MemOp m;
            m.base = b;
            m.disp = d;
            m.size = s;
            return m;
        }

        static MemOp base_index(Reg b, Reg idx, uint8_t sc = 1, int32_t d = 0, uint8_t s = 0) {
            MemOp m;
            m.base = b;
            m.index = idx;
            m.scale = sc;
            m.disp = d;
            m.size = s;
            return m;
        }

        static MemOp rip_rel(LabelId label, int32_t d = 0, uint8_t s = 0) {
            MemOp m;
            m.is_rip_relative = true;
            m.rip_label = label;
            m.disp = d;
            m.size = s;
            return m;
        }

        static MemOp rip_rel(const std::string& symbol, int32_t d = 0, uint8_t s = 0) {
            MemOp m;
            m.is_rip_relative = true;
            m.rip_symbol = symbol;
            m.disp = d;
            m.size = s;
            return m;
        }

        static MemOp abs32(uint32_t addr, uint8_t s = 0) {
            MemOp m;
            m.is_abs32 = true;
            m.disp = static_cast<int32_t>(addr);
            m.size = s;
            return m;
        }
    };

    // Shorthand helper functions
    inline MemOp mem(Reg base, int32_t disp = 0, uint8_t size = 0) {
        return MemOp::base_disp(base, disp, size);
    }

    inline MemOp mem(Reg base, Reg index, uint8_t scale = 1, int32_t disp = 0, uint8_t size = 0) {
        return MemOp::base_index(base, index, scale, disp, size);
    }

    inline MemOp mem_rip(LabelId label, int32_t disp = 0, uint8_t size = 0) {
        return MemOp::rip_rel(label, disp, size);
    }

    inline MemOp mem_rip(const std::string& symbol, int32_t disp = 0, uint8_t size = 0) {
        return MemOp::rip_rel(symbol, disp, size);
    }

    inline MemOp mem_abs(uint32_t addr, uint8_t size = 0) {
        return MemOp::abs32(addr, size);
    }

    class GrassX86Encoder {
    public:
        explicit GrassX86Encoder(TargetArch arch = TargetArch::X86_64);
        ~GrassX86Encoder() = default;

        // Configuration and State
        TargetArch arch() const;
        void reset();
        size_t code_size() const;
        const std::vector<uint8_t>& code() const;
        const std::vector<RelocEntry>& relocations() const;
        std::string dump_hex() const;
        CompiledUnit finish(const std::string& name = "");
        void emit_raw(const void* data, size_t size);
        void emit_raw_byte(uint8_t b);

        // Label management
        LabelId create_label();
        void bind_label(LabelId label);

        // Data movement
        void mov_rr(Reg dst, Reg src);
        void mov_ri64(Reg dst, int64_t imm);
        void mov_ri32(Reg dst, int32_t imm);
        void mov_rm(Reg dst, const MemOp& src);
        void mov_mr(const MemOp& dst, Reg src);
        void mov_mi32(const MemOp& dst, int32_t imm, uint8_t size = 0);
        void movzx_rr8(Reg dst, Reg src);
        void movzx_rr16(Reg dst, Reg src);
        void movzx_rm8(Reg dst, const MemOp& src);
        void movzx_rm16(Reg dst, const MemOp& src);
        void movsx_rr8(Reg dst, Reg src);
        void movsx_rr16(Reg dst, Reg src);
        void movsxd_rr32(Reg dst, Reg src);
        void movsx_rm8(Reg dst, const MemOp& src);
        void movsx_rm16(Reg dst, const MemOp& src);
        void movsxd_rm32(Reg dst, const MemOp& src);
        void lea_rm(Reg dst, const MemOp& src);
        void xchg_rr(Reg r1, Reg r2);
        void cmov_cc_rr(CondCode cc, Reg dst, Reg src);

        // Stack and control flow
        void push_r(Reg r);
        void push_i32(int32_t imm);
        void pop_r(Reg r);
        void call_label(LabelId label);
        void call_r(Reg r);
        void call_extern(const std::string& symbol);
        void jmp_label(LabelId label);
        void jmp_short_label(LabelId label);
        void jmp_r(Reg r);
        void jcc_label(CondCode cc, LabelId label);
        void jcc_short_label(CondCode cc, LabelId label);
        void ret();
        void ret_imm16(uint16_t imm);
        void nop();
        void nop_n(size_t bytes);
        void int3();
        void ud2();
        void syscall();
        void cpuid();
        void rdtsc();

        // Integer arithmetic and logic
        void add_rr(Reg dst, Reg src);
        void add_ri(Reg dst, int32_t imm);
        void add_rm(Reg dst, const MemOp& src);
        void add_mr(const MemOp& dst, Reg src);
        void sub_rr(Reg dst, Reg src);
        void sub_ri(Reg dst, int32_t imm);
        void sub_rm(Reg dst, const MemOp& src);
        void sub_mr(const MemOp& dst, Reg src);
        void imul_rr(Reg dst, Reg src);
        void imul_rri(Reg dst, Reg src, int32_t imm);
        void imul_rm(Reg dst, const MemOp& src);
        void idiv_r(Reg src);
        void div_r(Reg src);
        void neg_r(Reg r);
        void inc_r(Reg r);
        void dec_r(Reg r);
        void cqo();
        void cdq();
        void and_rr(Reg dst, Reg src);
        void and_ri(Reg dst, int32_t imm);
        void and_rm(Reg dst, const MemOp& src);
        void and_mr(const MemOp& dst, Reg src);
        void or_rr(Reg dst, Reg src);
        void or_ri(Reg dst, int32_t imm);
        void or_rm(Reg dst, const MemOp& src);
        void or_mr(const MemOp& dst, Reg src);
        void xor_rr(Reg dst, Reg src);
        void xor_ri(Reg dst, int32_t imm);
        void xor_rm(Reg dst, const MemOp& src);
        void xor_mr(const MemOp& dst, Reg src);
        void not_r(Reg r);
        void shl_ri(Reg r, uint8_t imm);
        void shl_rcl(Reg r);
        void shr_ri(Reg r, uint8_t imm);
        void shr_rcl(Reg r);
        void sar_ri(Reg r, uint8_t imm);
        void sar_rcl(Reg r);
        void rol_ri(Reg r, uint8_t imm);
        void ror_ri(Reg r, uint8_t imm);

        // Comparisons and bit operations
        void cmp_rr(Reg r1, Reg r2);
        void cmp_ri(Reg r, int32_t imm);
        void cmp_rm(Reg r, const MemOp& m);
        void cmp_mr(const MemOp& m, Reg r);
        void test_rr(Reg r1, Reg r2);
        void test_ri(Reg r, int32_t imm);
        void setcc_r(CondCode cc, Reg dst);
        void bsf_rr(Reg dst, Reg src);
        void bsr_rr(Reg dst, Reg src);
        void popcnt_rr(Reg dst, Reg src);
        void bswap_r(Reg r);

        // Atomic operations
        void lock_xadd_mr(const MemOp& dst, Reg src);
        void lock_cmpxchg_mr(const MemOp& dst, Reg src);
        void mfence();
        void lfence();
        void sfence();
        void pause();

        // Floating point (SSE2 scalar double / float)
        void movsd_xx(Reg dst, Reg src);
        void movsd_xm(Reg dst, const MemOp& src);
        void movsd_mx(const MemOp& dst, Reg src);
        void movss_xx(Reg dst, Reg src);
        void movss_xm(Reg dst, const MemOp& src);
        void movss_mx(const MemOp& dst, Reg src);
        void addsd_xx(Reg dst, Reg src);
        void subsd_xx(Reg dst, Reg src);
        void mulsd_xx(Reg dst, Reg src);
        void divsd_xx(Reg dst, Reg src);
        void sqrtsd_xx(Reg dst, Reg src);
        void ucomisd_xx(Reg dst, Reg src);
        void cvtsi2sd_xr(Reg dst, Reg src);
        void cvttsd2si_rx(Reg dst, Reg src);
        void xorpd_xx(Reg dst, Reg src);
        void pxor_xx(Reg dst, Reg src);

        // Additional SSE float operations
        void addss_xx(Reg dst, Reg src);
        void subss_xx(Reg dst, Reg src);
        void mulss_xx(Reg dst, Reg src);
        void divss_xx(Reg dst, Reg src);
        void sqrtss_xx(Reg dst, Reg src);
        void ucomiss_xx(Reg dst, Reg src);
        void cvtsi2ss_xr(Reg dst, Reg src);
        void cvttss2si_rx(Reg dst, Reg src);
        void xorps_xx(Reg dst, Reg src);

        // External symbol relocations
        void lea_symbol(Reg reg, const std::string& symbol);
        void mov_symbol_addr(Reg reg, const std::string& symbol);

    private:
        TargetArch arch_;
        std::vector<uint8_t> code_;
        std::vector<RelocEntry> relocations_;

        struct LabelState {
            int64_t offset = -1; // -1 if not yet bound
        };

        struct LabelPatch {
            LabelId label = -1;
            size_t patch_offset = 0;
            size_t inst_end_offset = 0;
            int32_t disp = 0;
            bool is_rel8 = false;
        };

        std::vector<LabelState> labels_;
        std::vector<LabelPatch> label_patches_;

        // Low-level emission helpers
        void emit_u8(uint8_t byte);
        void emit_u16(uint16_t word);
        void emit_u32(uint32_t dword);
        void emit_u64(uint64_t qword);

        void validate_reg(Reg r) const;
        void validate_arch_gpr(Reg r) const;
        void validate_xmm(Reg r) const;

        void emit_rex(bool w, bool r, bool x, bool b, bool force_rex = false);
        void emit_modrm_sib_disp(uint8_t reg_field, const MemOp& mem, size_t extra_inst_bytes = 0);

        void emit_alu_rr(uint8_t alu_code, Reg dst, Reg src);
        void emit_alu_ri(uint8_t alu_code, Reg dst, int32_t imm);
        void emit_alu_rm(uint8_t alu_code, Reg dst, const MemOp& src);
        void emit_alu_mr(uint8_t alu_code, const MemOp& dst, Reg src);
        void emit_shift_ri(uint8_t op_field, Reg r, uint8_t imm);
        void emit_shift_rcl(uint8_t op_field, Reg r);
    };

} // namespace grass::backend
