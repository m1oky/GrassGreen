#pragma once

#include "native/GrassBackendTypes.h"
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

namespace grass::backend {

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

        // Bare-metal I/O and CPU control intrinsics
        void in_al_dx();
        void out_dx_al();
        void cli();
        void sti();
        void hlt();

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
