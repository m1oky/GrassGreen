#include "native/GrassX86Encoder.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace grass::backend {

    // == Comparisons and Bit Operations ==

    void GrassX86Encoder::cmp_rr(Reg r1, Reg r2) { emit_alu_rr(7, r1, r2); }
    void GrassX86Encoder::cmp_ri(Reg r, int32_t imm) { emit_alu_ri(7, r, imm); }
    void GrassX86Encoder::cmp_rm(Reg r, const MemOp& m) { emit_alu_rm(7, r, m); }
    void GrassX86Encoder::cmp_mr(const MemOp& m, Reg r) { emit_alu_mr(7, m, r); }

    void GrassX86Encoder::test_rr(Reg r1, Reg r2) {
        validate_arch_gpr(r1);
        validate_arch_gpr(r2);
        uint8_t sz = reg_size(r1);
        if (sz != reg_size(r2)) {
            throw std::invalid_argument("Operand size mismatch in test_rr");
        }
        if (sz == 1) {
            bool dst_rex = reg_is_rex_byte(r1);
            bool src_rex = reg_is_rex_byte(r2);
            bool dst_high = reg_is_high_byte(r1);
            bool src_high = reg_is_high_byte(r2);
            bool dst_ext = reg_is_ext(r1);
            bool src_ext = reg_is_ext(r2);
            if ((dst_rex || src_rex || dst_ext || src_ext) && (dst_high || src_high)) {
                throw std::invalid_argument("Cannot combine REX-byte or extended register with legacy high-byte register");
            }
            bool force_rex = dst_rex || src_rex;
            emit_rex(false, src_ext, false, dst_ext, force_rex);
            emit_u8(0x84);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, reg_is_ext(r2), false, reg_is_ext(r1));
            emit_u8(0x85);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(r2) & 7) << 3) | (reg_raw_index(r1) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::test_ri(Reg r, int32_t imm) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool dst_rex = reg_is_rex_byte(r);
            bool dst_high = reg_is_high_byte(r);
            bool dst_ext = reg_is_ext(r);
            if ((dst_rex || dst_ext) && dst_high) {
                throw std::invalid_argument("Cannot combine REX-byte or extended register with legacy high-byte register");
            }
            emit_rex(false, false, false, dst_ext, dst_rex);
            emit_u8(0xF6);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | (0 << 3) | (reg_raw_index(r) & 7));
            emit_u8(modrm);
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xF7);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | (0 << 3) | (reg_raw_index(r) & 7));
            emit_u8(modrm);
            if (sz == 2) {
                emit_u16(static_cast<uint16_t>(imm & 0xFFFF));
            } else {
                emit_u32(static_cast<uint32_t>(imm));
            }
        }
    }

    void GrassX86Encoder::setcc_r(CondCode cc, Reg dst) {
        validate_arch_gpr(dst);
        if (reg_size(dst) != 1) {
            throw std::invalid_argument("setcc_r requires 8-bit destination register");
        }
        bool force_rex = reg_is_rex_byte(dst);
        emit_rex(false, false, false, reg_is_ext(dst), force_rex);
        emit_u8(0x0F);
        emit_u8(static_cast<uint8_t>(0x90 + (static_cast<uint8_t>(cc) & 0x0F)));
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (0 << 3) | (reg_raw_index(dst) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::bsf_rr(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("bsf_rr requires matching 16/32/64-bit operands");
        }
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xBC);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::bsr_rr(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("bsr_rr requires matching 16/32/64-bit operands");
        }
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xBD);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::popcnt_rr(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("popcnt_rr requires matching 16/32/64-bit operands");
        }
        emit_u8(0xF3);
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xB8);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::bswap_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 4) {
            emit_rex(false, false, false, reg_is_ext(r));
            emit_u8(0x0F);
            emit_u8(static_cast<uint8_t>(0xC8 + (reg_raw_index(r) & 7)));
        } else if (sz == 8 && arch_ == TargetArch::X86_64) {
            emit_rex(true, false, false, reg_is_ext(r));
            emit_u8(0x0F);
            emit_u8(static_cast<uint8_t>(0xC8 + (reg_raw_index(r) & 7)));
        } else {
            throw std::invalid_argument("bswap_r is only valid for 32-bit and 64-bit registers");
        }
    }

    // == Atomic Operations ==

    void GrassX86Encoder::lock_xadd_mr(const MemOp& dst, Reg src) {
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        emit_u8(0xF0); // LOCK prefix
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);

        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(src);
            emit_rex(false, reg_is_ext(src), idx_ext, base_ext, force_rex);
            emit_u8(0x0F);
            emit_u8(0xC0);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, reg_is_ext(src), idx_ext, base_ext);
            emit_u8(0x0F);
            emit_u8(0xC1);
        }
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::lock_cmpxchg_mr(const MemOp& dst, Reg src) {
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        emit_u8(0xF0); // LOCK prefix
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);

        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(src);
            emit_rex(false, reg_is_ext(src), idx_ext, base_ext, force_rex);
            emit_u8(0x0F);
            emit_u8(0xB0);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, reg_is_ext(src), idx_ext, base_ext);
            emit_u8(0x0F);
            emit_u8(0xB1);
        }
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::mfence() {
        emit_u8(0x0F);
        emit_u8(0xAE);
        emit_u8(0xF0);
    }

    void GrassX86Encoder::lfence() {
        emit_u8(0x0F);
        emit_u8(0xAE);
        emit_u8(0xE8);
    }

    void GrassX86Encoder::sfence() {
        emit_u8(0x0F);
        emit_u8(0xAE);
        emit_u8(0xF8);
    }

    void GrassX86Encoder::pause() {
        emit_u8(0xF3);
        emit_u8(0x90);
    }

    // == Floating Point (SSE2 scalar double / float) ==

    void GrassX86Encoder::movsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x10);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movsd_xm(Reg dst, const MemOp& src) {
        validate_xmm(dst);
        emit_u8(0xF2);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(false, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x10);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movsd_mx(const MemOp& dst, Reg src) {
        validate_xmm(src);
        emit_u8(0xF2);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_rex(false, reg_is_ext(src), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x11);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::movss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x10);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movss_xm(Reg dst, const MemOp& src) {
        validate_xmm(dst);
        emit_u8(0xF3);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(false, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x10);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movss_mx(const MemOp& dst, Reg src) {
        validate_xmm(src);
        emit_u8(0xF3);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_rex(false, reg_is_ext(src), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x11);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::addsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x58);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::subsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x5C);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::mulsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x59);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::divsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x5E);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::sqrtsd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF2);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x51);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::ucomisd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2E);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cvtsi2sd_xr(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        if (sz != 4 && sz != 8) {
            throw std::invalid_argument("cvtsi2sd_xr requires 32-bit or 64-bit source register");
        }
        emit_u8(0xF2);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2A);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cvttsd2si_rx(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_xmm(src);
        uint8_t sz = reg_size(dst);
        if (sz != 4 && sz != 8) {
            throw std::invalid_argument("cvttsd2si_rx requires 32-bit or 64-bit destination register");
        }
        emit_u8(0xF2);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2C);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::xorpd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x57);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::pxor_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xEF);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::addss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x58);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::subss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x5C);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::mulss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x59);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::divss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x5E);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::sqrtss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x51);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::ucomiss_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2E);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cvtsi2ss_xr(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        if (sz != 4 && sz != 8) {
            throw std::invalid_argument("cvtsi2ss_xr requires 32-bit or 64-bit source register");
        }
        emit_u8(0xF3);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2A);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cvttss2si_rx(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_xmm(src);
        uint8_t sz = reg_size(dst);
        if (sz != 4 && sz != 8) {
            throw std::invalid_argument("cvttss2si_rx requires 32-bit or 64-bit destination register");
        }
        emit_u8(0xF3);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x2C);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::xorps_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x57);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }


} // namespace grass::backend
