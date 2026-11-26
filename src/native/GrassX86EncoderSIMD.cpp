#include "native/GrassX86Encoder.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace grass::backend {

    // == Packed SIMD (SSE2 128-bit XMM) ==

    void GrassX86Encoder::movdqu_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0xF3);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x6F);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::movdqu_xm(Reg dst, const MemOp& src) {
        validate_xmm(dst);
        emit_u8(0xF3);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(false, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x6F);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movdqu_mx(const MemOp& dst, Reg src) {
        validate_xmm(src);
        emit_u8(0xF3);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_rex(false, reg_is_ext(src), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x7F);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::movupd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x10);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::movupd_xm(Reg dst, const MemOp& src) {
        validate_xmm(dst);
        emit_u8(0x66);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(false, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x10);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movupd_mx(const MemOp& dst, Reg src) {
        validate_xmm(src);
        emit_u8(0x66);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_rex(false, reg_is_ext(src), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0x11);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::paddq_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xD4);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::psubq_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xFB);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::addpd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x58);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::subpd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x5C);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::mulpd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x59);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::haddpd_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x7C);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::punpckhqdq_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x6D);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::movhlps_xx(Reg dst, Reg src) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0x12);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::psrldq_xi(Reg dst, uint8_t imm) {
        validate_xmm(dst);
        emit_u8(0x66);
        emit_rex(false, false, false, reg_is_ext(dst));
        emit_u8(0x0F);
        emit_u8(0x73);
        emit_u8(static_cast<uint8_t>((3 << 6) | (3 << 3) | (reg_raw_index(dst) & 7)));
        emit_u8(imm);
    }

    void GrassX86Encoder::shufpd_xxi(Reg dst, Reg src, uint8_t imm) {
        validate_xmm(dst);
        validate_xmm(src);
        emit_u8(0x66);
        emit_rex(false, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xC6);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
        emit_u8(imm);
    }

    void GrassX86Encoder::movq_rx(Reg dst_gpr, Reg src_xmm) {
        validate_arch_gpr(dst_gpr);
        validate_xmm(src_xmm);
        if (reg_size(dst_gpr) != 8 || arch_ != TargetArch::X86_64) {
            throw std::invalid_argument("movq_rx requires 64-bit GPR in X86_64 mode");
        }
        emit_u8(0x66);
        emit_rex(true, reg_is_ext(src_xmm), false, reg_is_ext(dst_gpr));
        emit_u8(0x0F);
        emit_u8(0x7E);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src_xmm) & 7) << 3) | (reg_raw_index(dst_gpr) & 7)));
    }

    void GrassX86Encoder::movq_xr(Reg dst_xmm, Reg src_gpr) {
        validate_xmm(dst_xmm);
        validate_arch_gpr(src_gpr);
        if (reg_size(src_gpr) != 8 || arch_ != TargetArch::X86_64) {
            throw std::invalid_argument("movq_xr requires 64-bit GPR in X86_64 mode");
        }
        emit_u8(0x66);
        emit_rex(true, reg_is_ext(dst_xmm), false, reg_is_ext(src_gpr));
        emit_u8(0x0F);
        emit_u8(0x6E);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst_xmm) & 7) << 3) | (reg_raw_index(src_gpr) & 7)));
    }

    // == Packed SIMD (AVX2 256-bit YMM with VEX) ==

    void GrassX86Encoder::vmovdqu_yy(Reg dst, Reg src) {
        validate_ymm(dst);
        validate_ymm(src);
        emit_vex(true, 2, 1, false, reg_is_ext(dst), false, reg_is_ext(src), 0);
        emit_u8(0x6F);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::vmovdqu_ym(Reg dst, const MemOp& src) {
        validate_ymm(dst);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_vex(true, 2, 1, false, reg_is_ext(dst), idx_ext, base_ext, 0);
        emit_u8(0x6F);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::vmovdqu_my(const MemOp& dst, Reg src) {
        validate_ymm(src);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_vex(true, 2, 1, false, reg_is_ext(src), idx_ext, base_ext, 0);
        emit_u8(0x7F);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::vmovupd_yy(Reg dst, Reg src) {
        validate_ymm(dst);
        validate_ymm(src);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src), 0);
        emit_u8(0x10);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7)));
    }

    void GrassX86Encoder::vmovupd_ym(Reg dst, const MemOp& src) {
        validate_ymm(dst);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), idx_ext, base_ext, 0);
        emit_u8(0x10);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::vmovupd_my(const MemOp& dst, Reg src) {
        validate_ymm(src);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);
        emit_vex(true, 1, 1, false, reg_is_ext(src), idx_ext, base_ext, 0);
        emit_u8(0x11);
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::vpaddq_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0xD4);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vpsubq_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0xFB);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vaddpd_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0x58);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vsubpd_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0x5C);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vmulpd_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0x59);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vpxor_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0xEF);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vhaddpd_yyy(Reg dst, Reg src1, Reg src2) {
        validate_ymm(dst); validate_ymm(src1); validate_ymm(src2);
        emit_vex(true, 1, 1, false, reg_is_ext(dst), false, reg_is_ext(src2), reg_raw_index(src1));
        emit_u8(0x7C);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src2) & 7)));
    }

    void GrassX86Encoder::vextracti128_xyi(Reg dst_xmm, Reg src_ymm, uint8_t imm) {
        validate_xmm(dst_xmm);
        validate_ymm(src_ymm);
        emit_vex(true, 1, 3, false, reg_is_ext(src_ymm), false, reg_is_ext(dst_xmm), 0);
        emit_u8(0x39);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src_ymm) & 7) << 3) | (reg_raw_index(dst_xmm) & 7)));
        emit_u8(imm);
    }

    void GrassX86Encoder::vextractf128_xyi(Reg dst_xmm, Reg src_ymm, uint8_t imm) {
        validate_xmm(dst_xmm);
        validate_ymm(src_ymm);
        emit_vex(true, 1, 3, false, reg_is_ext(src_ymm), false, reg_is_ext(dst_xmm), 0);
        emit_u8(0x19);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src_ymm) & 7) << 3) | (reg_raw_index(dst_xmm) & 7)));
        emit_u8(imm);
    }

    void GrassX86Encoder::vpbroadcastq_yx(Reg dst_ymm, Reg src_xmm) {
        validate_ymm(dst_ymm);
        validate_xmm(src_xmm);
        emit_vex(true, 1, 2, false, reg_is_ext(dst_ymm), false, reg_is_ext(src_xmm), 0);
        emit_u8(0x59);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst_ymm) & 7) << 3) | (reg_raw_index(src_xmm) & 7)));
    }

    void GrassX86Encoder::vbroadcastsd_yx(Reg dst_ymm, Reg src_xmm) {
        validate_ymm(dst_ymm);
        validate_xmm(src_xmm);
        emit_vex(true, 1, 2, false, reg_is_ext(dst_ymm), false, reg_is_ext(src_xmm), 0);
        emit_u8(0x19);
        emit_u8(static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst_ymm) & 7) << 3) | (reg_raw_index(src_xmm) & 7)));
    }

    void GrassX86Encoder::vzeroupper() {
        emit_u8(0xC5);
        emit_u8(0xF8);
        emit_u8(0x77);
    }

    // == External Symbol Relocations ==

    void GrassX86Encoder::lea_symbol(Reg reg, const std::string& symbol) {
        validate_arch_gpr(reg);
        if (arch_ == TargetArch::X86_64) {
            if (reg_size(reg) != 8) {
                throw std::invalid_argument("lea_symbol requires 64-bit register in 64-bit mode");
            }
            // lea reg, [rip + 0]
            emit_rex(true, reg_is_ext(reg), false, false);
            emit_u8(0x8D);
            uint8_t modrm = static_cast<uint8_t>((0 << 6) | ((reg_raw_index(reg) & 7) << 3) | 5);
            emit_u8(modrm);
            RelocEntry re;
            re.section = SectionKind::Text;
            re.offset = code_.size();
            re.symbol_name = symbol;
            re.type = RelocType::Rel32;
            re.addend = -4;
            relocations_.push_back(re);
            emit_u32(0);
        } else {
            if (reg_size(reg) != 4) {
                throw std::invalid_argument("lea_symbol requires 32-bit register in 32-bit mode");
            }
            // In 32-bit: mov reg, imm32 with Abs32 reloc
            emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(reg) & 7)));
            RelocEntry re;
            re.section = SectionKind::Text;
            re.offset = code_.size();
            re.symbol_name = symbol;
            re.type = RelocType::Abs32;
            re.addend = 0;
            relocations_.push_back(re);
            emit_u32(0);
        }
    }

    void GrassX86Encoder::mov_symbol_addr(Reg reg, const std::string& symbol) {
        validate_arch_gpr(reg);
        if (arch_ == TargetArch::X86_64) {
            if (reg_size(reg) != 8) {
                throw std::invalid_argument("mov_symbol_addr requires 64-bit register in 64-bit mode");
            }
            // mov reg, imm64
            emit_rex(true, false, false, reg_is_ext(reg));
            emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(reg) & 7)));
            RelocEntry re;
            re.section = SectionKind::Text;
            re.offset = code_.size();
            re.symbol_name = symbol;
            re.type = RelocType::Abs64;
            re.addend = 0;
            relocations_.push_back(re);
            emit_u64(0);
        } else {
            if (reg_size(reg) != 4) {
                throw std::invalid_argument("mov_symbol_addr requires 32-bit register in 32-bit mode");
            }
            // mov reg, imm32
            emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(reg) & 7)));
            RelocEntry re;
            re.section = SectionKind::Text;
            re.offset = code_.size();
            re.symbol_name = symbol;
            re.type = RelocType::Abs32;
            re.addend = 0;
            relocations_.push_back(re);
            emit_u32(0);
        }
    }


} // namespace grass::backend
