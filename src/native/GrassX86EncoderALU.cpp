#include "native/GrassX86Encoder.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace grass::backend {

    // == Stack and Control Flow ==

    void GrassX86Encoder::push_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 2) {
            emit_u8(0x66);
            emit_rex(false, false, false, reg_is_ext(r));
            emit_u8(static_cast<uint8_t>(0x50 + (reg_raw_index(r) & 7)));
        } else if (sz == 8 && arch_ == TargetArch::X86_64) {
            // In 64-bit mode, push defaults to 64-bit; REX.B needed if r >= 8
            emit_rex(false, false, false, reg_is_ext(r));
            emit_u8(static_cast<uint8_t>(0x50 + (reg_raw_index(r) & 7)));
        } else if (sz == 4 && arch_ == TargetArch::X86_32) {
            emit_u8(static_cast<uint8_t>(0x50 + (reg_raw_index(r) & 7)));
        } else {
            throw std::invalid_argument("Unsupported register size for push_r");
        }
    }

    void GrassX86Encoder::push_i32(int32_t imm) {
        if (imm >= -128 && imm <= 127) {
            emit_u8(0x6A);
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        } else {
            emit_u8(0x68);
            emit_u32(static_cast<uint32_t>(imm));
        }
    }

    void GrassX86Encoder::pop_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 2) {
            emit_u8(0x66);
            emit_rex(false, false, false, reg_is_ext(r));
            emit_u8(static_cast<uint8_t>(0x58 + (reg_raw_index(r) & 7)));
        } else if (sz == 8 && arch_ == TargetArch::X86_64) {
            emit_rex(false, false, false, reg_is_ext(r));
            emit_u8(static_cast<uint8_t>(0x58 + (reg_raw_index(r) & 7)));
        } else if (sz == 4 && arch_ == TargetArch::X86_32) {
            emit_u8(static_cast<uint8_t>(0x58 + (reg_raw_index(r) & 7)));
        } else {
            throw std::invalid_argument("Unsupported register size for pop_r");
        }
    }

    void GrassX86Encoder::call_label(LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in call_label");
        }
        emit_u8(0xE8);
        if (labels_[label].offset >= 0) {
            int64_t rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 4);
            emit_u32(static_cast<uint32_t>(rel));
        } else {
            LabelPatch patch;
            patch.label = label;
            patch.patch_offset = code_.size();
            patch.inst_end_offset = code_.size() + 4;
            patch.is_rel8 = false;
            label_patches_.push_back(patch);
            emit_u32(0);
        }
    }

    void GrassX86Encoder::call_r(Reg r) {
        validate_arch_gpr(r);
        if (arch_ == TargetArch::X86_64) {
            if (reg_size(r) != 8) {
                throw std::invalid_argument("call_r requires 64-bit register in 64-bit mode");
            }
        } else {
            if (reg_size(r) != 4) {
                throw std::invalid_argument("call_r requires 32-bit register in 32-bit mode");
            }
        }
        emit_rex(false, false, false, reg_is_ext(r));
        emit_u8(0xFF);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (2 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::call_extern(const std::string& symbol) {
        emit_u8(0xE8);
        RelocEntry re;
        re.section = SectionKind::Text;
        re.offset = code_.size();
        re.symbol_name = symbol;
        re.type = RelocType::Rel32;
        re.addend = -4;
        relocations_.push_back(re);
        emit_u32(0);
    }

    void GrassX86Encoder::jmp_label(LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in jmp_label");
        }
        if (labels_[label].offset >= 0) {
            int64_t rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 2);
            if (rel >= -128 && rel <= 127) {
                emit_u8(0xEB);
                emit_u8(static_cast<uint8_t>(rel & 0xFF));
                return;
            }
            rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 5);
            emit_u8(0xE9);
            emit_u32(static_cast<uint32_t>(rel));
        } else {
            // Forward jump emits rel32 for universal reach
            emit_u8(0xE9);
            LabelPatch patch;
            patch.label = label;
            patch.patch_offset = code_.size();
            patch.inst_end_offset = code_.size() + 4;
            patch.is_rel8 = false;
            label_patches_.push_back(patch);
            emit_u32(0);
        }
    }

    void GrassX86Encoder::jmp_short_label(LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in jmp_short_label");
        }
        emit_u8(0xEB);
        if (labels_[label].offset >= 0) {
            int64_t rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 1);
            if (rel < -128 || rel > 127) {
                throw std::runtime_error("jmp_short_label out of 8-bit range");
            }
            emit_u8(static_cast<uint8_t>(rel & 0xFF));
        } else {
            LabelPatch patch;
            patch.label = label;
            patch.patch_offset = code_.size();
            patch.inst_end_offset = code_.size() + 1;
            patch.is_rel8 = true;
            label_patches_.push_back(patch);
            emit_u8(0);
        }
    }

    void GrassX86Encoder::jmp_r(Reg r) {
        validate_arch_gpr(r);
        if (arch_ == TargetArch::X86_64) {
            if (reg_size(r) != 8) {
                throw std::invalid_argument("jmp_r requires 64-bit register in 64-bit mode");
            }
        } else {
            if (reg_size(r) != 4) {
                throw std::invalid_argument("jmp_r requires 32-bit register in 32-bit mode");
            }
        }
        emit_rex(false, false, false, reg_is_ext(r));
        emit_u8(0xFF);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (4 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::jcc_label(CondCode cc, LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in jcc_label");
        }
        uint8_t c = static_cast<uint8_t>(cc) & 0x0F;
        if (labels_[label].offset >= 0) {
            int64_t rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 2);
            if (rel >= -128 && rel <= 127) {
                emit_u8(static_cast<uint8_t>(0x70 + c));
                emit_u8(static_cast<uint8_t>(rel & 0xFF));
                return;
            }
            rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 6);
            emit_u8(0x0F);
            emit_u8(static_cast<uint8_t>(0x80 + c));
            emit_u32(static_cast<uint32_t>(rel));
        } else {
            emit_u8(0x0F);
            emit_u8(static_cast<uint8_t>(0x80 + c));
            LabelPatch patch;
            patch.label = label;
            patch.patch_offset = code_.size();
            patch.inst_end_offset = code_.size() + 4;
            patch.is_rel8 = false;
            label_patches_.push_back(patch);
            emit_u32(0);
        }
    }

    void GrassX86Encoder::jcc_short_label(CondCode cc, LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in jcc_short_label");
        }
        uint8_t c = static_cast<uint8_t>(cc) & 0x0F;
        emit_u8(static_cast<uint8_t>(0x70 + c));
        if (labels_[label].offset >= 0) {
            int64_t rel = labels_[label].offset - static_cast<int64_t>(code_.size() + 1);
            if (rel < -128 || rel > 127) {
                throw std::runtime_error("jcc_short_label out of 8-bit range");
            }
            emit_u8(static_cast<uint8_t>(rel & 0xFF));
        } else {
            LabelPatch patch;
            patch.label = label;
            patch.patch_offset = code_.size();
            patch.inst_end_offset = code_.size() + 1;
            patch.is_rel8 = true;
            label_patches_.push_back(patch);
            emit_u8(0);
        }
    }

    void GrassX86Encoder::ret() {
        emit_u8(0xC3);
    }

    void GrassX86Encoder::ret_imm16(uint16_t imm) {
        emit_u8(0xC2);
        emit_u16(imm);
    }

    void GrassX86Encoder::nop() {
        emit_u8(0x90);
    }

    void GrassX86Encoder::nop_n(size_t bytes) {
        // Multi-byte NOPs recommended by Intel
        while (bytes > 0) {
            if (bytes >= 9) {
                // 9-byte: 66 0F 1F 84 00 00 00 00 00
                const uint8_t n9[] = { 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
                for (uint8_t b : n9) emit_u8(b);
                bytes -= 9;
            } else if (bytes == 8) {
                const uint8_t n8[] = { 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
                for (uint8_t b : n8) emit_u8(b);
                bytes -= 8;
            } else if (bytes == 7) {
                const uint8_t n7[] = { 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };
                for (uint8_t b : n7) emit_u8(b);
                bytes -= 7;
            } else if (bytes == 6) {
                const uint8_t n6[] = { 0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
                for (uint8_t b : n6) emit_u8(b);
                bytes -= 6;
            } else if (bytes == 5) {
                const uint8_t n5[] = { 0x0F, 0x1F, 0x44, 0x00, 0x00 };
                for (uint8_t b : n5) emit_u8(b);
                bytes -= 5;
            } else if (bytes == 4) {
                const uint8_t n4[] = { 0x0F, 0x1F, 0x40, 0x00 };
                for (uint8_t b : n4) emit_u8(b);
                bytes -= 4;
            } else if (bytes == 3) {
                const uint8_t n3[] = { 0x0F, 0x1F, 0x00 };
                for (uint8_t b : n3) emit_u8(b);
                bytes -= 3;
            } else if (bytes == 2) {
                emit_u8(0x66);
                emit_u8(0x90);
                bytes -= 2;
            } else if (bytes == 1) {
                emit_u8(0x90);
                bytes -= 1;
            }
        }
    }

    void GrassX86Encoder::int3() {
        emit_u8(0xCC);
    }

    void GrassX86Encoder::ud2() {
        emit_u8(0x0F);
        emit_u8(0x0B);
    }

    void GrassX86Encoder::syscall() {
        if (arch_ == TargetArch::X86_32) {
            throw std::invalid_argument("syscall is only supported in 64-bit mode");
        }
        emit_u8(0x0F);
        emit_u8(0x05);
    }

    void GrassX86Encoder::cpuid() {
        emit_u8(0x0F);
        emit_u8(0xA2);
    }

    void GrassX86Encoder::rdtsc() {
        emit_u8(0x0F);
        emit_u8(0x31);
    }

    void GrassX86Encoder::in_al_dx() {
        emit_u8(0xEC);
    }

    void GrassX86Encoder::out_dx_al() {
        emit_u8(0xEE);
    }

    void GrassX86Encoder::cli() {
        emit_u8(0xFA);
    }

    void GrassX86Encoder::sti() {
        emit_u8(0xFB);
    }

    void GrassX86Encoder::hlt() {
        emit_u8(0xF4);
    }

    // == Integer Arithmetic and Logic ==

    void GrassX86Encoder::emit_alu_rr(uint8_t alu_code, Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz_dst = reg_size(dst);
        uint8_t sz_src = reg_size(src);
        if (sz_dst != sz_src) {
            throw std::invalid_argument("Operand size mismatch in ALU instruction");
        }

        if (sz_dst == 1) {
            bool dst_rex = reg_is_rex_byte(dst);
            bool src_rex = reg_is_rex_byte(src);
            bool dst_high = reg_is_high_byte(dst);
            bool src_high = reg_is_high_byte(src);
            bool dst_ext = reg_is_ext(dst);
            bool src_ext = reg_is_ext(src);
            if ((dst_rex || src_rex || dst_ext || src_ext) && (dst_high || src_high)) {
                throw std::invalid_argument("Cannot combine REX-byte or extended register with legacy high-byte register");
            }
            bool force_rex = dst_rex || src_rex;
            emit_rex(false, src_ext, false, dst_ext, force_rex);
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x00));
        } else {
            if (sz_dst == 2) emit_u8(0x66);
            emit_rex(sz_dst == 8, reg_is_ext(src), false, reg_is_ext(dst));
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x01));
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src) & 7) << 3) | (reg_raw_index(dst) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::emit_alu_ri(uint8_t alu_code, Reg dst, int32_t imm) {
        validate_arch_gpr(dst);
        uint8_t sz = reg_size(dst);

        if (sz == 1) {
            bool dst_rex = reg_is_rex_byte(dst);
            bool dst_high = reg_is_high_byte(dst);
            bool dst_ext = reg_is_ext(dst);
            if ((dst_rex || dst_ext) && dst_high) {
                throw std::invalid_argument("Cannot combine REX-byte or extended register with legacy high-byte register");
            }
            emit_rex(false, false, false, dst_ext, dst_rex);
            emit_u8(0x80);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((alu_code & 7) << 3) | (reg_raw_index(dst) & 7));
            emit_u8(modrm);
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(dst));

            if (imm >= -128 && imm <= 127) {
                emit_u8(0x83);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((alu_code & 7) << 3) | (reg_raw_index(dst) & 7));
                emit_u8(modrm);
                emit_u8(static_cast<uint8_t>(imm & 0xFF));
            } else {
                emit_u8(0x81);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((alu_code & 7) << 3) | (reg_raw_index(dst) & 7));
                emit_u8(modrm);
                if (sz == 2) {
                    emit_u16(static_cast<uint16_t>(imm & 0xFFFF));
                } else {
                    emit_u32(static_cast<uint32_t>(imm));
                }
            }
        }
    }

    void GrassX86Encoder::emit_alu_rm(uint8_t alu_code, Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz = reg_size(dst);
        bool force_rex = (sz == 1 && reg_is_rex_byte(dst));
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);

        if (sz == 1) {
            bool rex_needed = reg_is_ext(dst) || idx_ext || base_ext || force_rex;
            if (rex_needed && reg_is_high_byte(dst)) {
                throw std::invalid_argument("Cannot access legacy high-byte register (AH, CH, DH, BH) when REX prefix is required");
            }
            emit_rex(false, reg_is_ext(dst), idx_ext, base_ext, force_rex);
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x02));
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, reg_is_ext(dst), idx_ext, base_ext);
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x03));
        }
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::emit_alu_mr(uint8_t alu_code, const MemOp& dst, Reg src) {
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        bool force_rex = (sz == 1 && reg_is_rex_byte(src));
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);

        if (sz == 1) {
            bool rex_needed = reg_is_ext(src) || idx_ext || base_ext || force_rex;
            if (rex_needed && reg_is_high_byte(src)) {
                throw std::invalid_argument("Cannot access legacy high-byte register (AH, CH, DH, BH) when REX prefix is required");
            }
            emit_rex(false, reg_is_ext(src), idx_ext, base_ext, force_rex);
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x00));
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, reg_is_ext(src), idx_ext, base_ext);
            emit_u8(static_cast<uint8_t>((alu_code << 3) | 0x01));
        }
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::add_rr(Reg dst, Reg src) { emit_alu_rr(0, dst, src); }
    void GrassX86Encoder::add_ri(Reg dst, int32_t imm) { emit_alu_ri(0, dst, imm); }
    void GrassX86Encoder::add_rm(Reg dst, const MemOp& src) { emit_alu_rm(0, dst, src); }
    void GrassX86Encoder::add_mr(const MemOp& dst, Reg src) { emit_alu_mr(0, dst, src); }

    void GrassX86Encoder::sub_rr(Reg dst, Reg src) { emit_alu_rr(5, dst, src); }
    void GrassX86Encoder::sub_ri(Reg dst, int32_t imm) { emit_alu_ri(5, dst, imm); }
    void GrassX86Encoder::sub_rm(Reg dst, const MemOp& src) { emit_alu_rm(5, dst, src); }
    void GrassX86Encoder::sub_mr(const MemOp& dst, Reg src) { emit_alu_mr(5, dst, src); }

    void GrassX86Encoder::and_rr(Reg dst, Reg src) { emit_alu_rr(4, dst, src); }
    void GrassX86Encoder::and_ri(Reg dst, int32_t imm) { emit_alu_ri(4, dst, imm); }
    void GrassX86Encoder::and_rm(Reg dst, const MemOp& src) { emit_alu_rm(4, dst, src); }
    void GrassX86Encoder::and_mr(const MemOp& dst, Reg src) { emit_alu_mr(4, dst, src); }

    void GrassX86Encoder::or_rr(Reg dst, Reg src) { emit_alu_rr(1, dst, src); }
    void GrassX86Encoder::or_ri(Reg dst, int32_t imm) { emit_alu_ri(1, dst, imm); }
    void GrassX86Encoder::or_rm(Reg dst, const MemOp& src) { emit_alu_rm(1, dst, src); }
    void GrassX86Encoder::or_mr(const MemOp& dst, Reg src) { emit_alu_mr(1, dst, src); }

    void GrassX86Encoder::xor_rr(Reg dst, Reg src) { emit_alu_rr(6, dst, src); }
    void GrassX86Encoder::xor_ri(Reg dst, int32_t imm) { emit_alu_ri(6, dst, imm); }
    void GrassX86Encoder::xor_rm(Reg dst, const MemOp& src) { emit_alu_rm(6, dst, src); }
    void GrassX86Encoder::xor_mr(const MemOp& dst, Reg src) { emit_alu_mr(6, dst, src); }

    void GrassX86Encoder::imul_rr(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("imul_rr requires matching 16/32/64-bit operands");
        }
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xAF);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::imul_rri(Reg dst, Reg src, int32_t imm) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("imul_rri requires matching 16/32/64-bit operands");
        }
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        if (imm >= -128 && imm <= 127) {
            emit_u8(0x6B);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
            emit_u8(modrm);
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        } else {
            emit_u8(0x69);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
            emit_u8(modrm);
            if (sz == 2) {
                emit_u16(static_cast<uint16_t>(imm & 0xFFFF));
            } else {
                emit_u32(static_cast<uint32_t>(imm));
            }
        }
    }

    void GrassX86Encoder::imul_rm(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz = reg_size(dst);
        if (sz < 2) {
            throw std::invalid_argument("imul_rm requires 16/32/64-bit dst");
        }
        if (sz == 2) emit_u8(0x66);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0xAF);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::idiv_r(Reg src) {
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(src);
            emit_rex(false, false, false, reg_is_ext(src), force_rex);
            emit_u8(0xF6);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(src));
            emit_u8(0xF7);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (7 << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::div_r(Reg src) {
        validate_arch_gpr(src);
        uint8_t sz = reg_size(src);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(src);
            emit_rex(false, false, false, reg_is_ext(src), force_rex);
            emit_u8(0xF6);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(src));
            emit_u8(0xF7);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (6 << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::neg_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            emit_u8(0xF6);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xF7);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (3 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::inc_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            emit_u8(0xFE);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xFF);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (0 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::dec_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            emit_u8(0xFE);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xFF);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (1 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cqo() {
        if (arch_ == TargetArch::X86_32) {
            throw std::invalid_argument("cqo is only supported in 64-bit mode");
        }
        emit_rex(true, false, false, false);
        emit_u8(0x99);
    }

    void GrassX86Encoder::cdq() {
        emit_u8(0x99);
    }

    void GrassX86Encoder::not_r(Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            emit_u8(0xF6);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xF7);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | (2 << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::emit_shift_ri(uint8_t op_field, Reg r, uint8_t imm) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            if (imm == 1) {
                emit_u8(0xD0);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((op_field & 7) << 3) | (reg_raw_index(r) & 7));
                emit_u8(modrm);
            } else {
                emit_u8(0xC0);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((op_field & 7) << 3) | (reg_raw_index(r) & 7));
                emit_u8(modrm);
                emit_u8(imm);
            }
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            if (imm == 1) {
                emit_u8(0xD1);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((op_field & 7) << 3) | (reg_raw_index(r) & 7));
                emit_u8(modrm);
            } else {
                emit_u8(0xC1);
                uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((op_field & 7) << 3) | (reg_raw_index(r) & 7));
                emit_u8(modrm);
                emit_u8(imm);
            }
        }
    }

    void GrassX86Encoder::emit_shift_rcl(uint8_t op_field, Reg r) {
        validate_arch_gpr(r);
        uint8_t sz = reg_size(r);
        if (sz == 1) {
            bool force_rex = reg_is_rex_byte(r);
            emit_rex(false, false, false, reg_is_ext(r), force_rex);
            emit_u8(0xD2);
        } else {
            if (sz == 2) emit_u8(0x66);
            emit_rex(sz == 8, false, false, reg_is_ext(r));
            emit_u8(0xD3);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((op_field & 7) << 3) | (reg_raw_index(r) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::shl_ri(Reg r, uint8_t imm) { emit_shift_ri(4, r, imm); }
    void GrassX86Encoder::shl_rcl(Reg r) { emit_shift_rcl(4, r); }
    void GrassX86Encoder::shr_ri(Reg r, uint8_t imm) { emit_shift_ri(5, r, imm); }
    void GrassX86Encoder::shr_rcl(Reg r) { emit_shift_rcl(5, r); }
    void GrassX86Encoder::sar_ri(Reg r, uint8_t imm) { emit_shift_ri(7, r, imm); }
    void GrassX86Encoder::sar_rcl(Reg r) { emit_shift_rcl(7, r); }
    void GrassX86Encoder::rol_ri(Reg r, uint8_t imm) { emit_shift_ri(0, r, imm); }
    void GrassX86Encoder::ror_ri(Reg r, uint8_t imm) { emit_shift_ri(1, r, imm); }


} // namespace grass::backend
