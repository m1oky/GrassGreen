#include "GrassX86Encoder.h"
#include <iomanip>
#include <sstream>
#include <cstring>
#include <algorithm>

namespace grass::backend {

    GrassX86Encoder::GrassX86Encoder(TargetArch arch)
        : arch_(arch) {
    }

    TargetArch GrassX86Encoder::arch() const {
        return arch_;
    }

    void GrassX86Encoder::reset() {
        code_.clear();
        relocations_.clear();
        labels_.clear();
        label_patches_.clear();
    }

    size_t GrassX86Encoder::code_size() const {
        return code_.size();
    }

    const std::vector<uint8_t>& GrassX86Encoder::code() const {
        return code_;
    }

    const std::vector<RelocEntry>& GrassX86Encoder::relocations() const {
        return relocations_;
    }

    std::string GrassX86Encoder::dump_hex() const {
        if (code_.empty()) {
            return std::string();
        }
        std::ostringstream oss;
        oss << std::hex << std::uppercase << std::setfill('0');
        for (size_t i = 0; i < code_.size(); i += 16) {
            oss << std::setw(8) << i << ": ";
            size_t chunk = std::min<size_t>(16, code_.size() - i);
            for (size_t j = 0; j < chunk; ++j) {
                oss << std::setw(2) << static_cast<unsigned int>(code_[i + j]);
                if (j + 1 < chunk) oss << " ";
            }
            if (i + 16 < code_.size()) oss << "\n";
        }
        return oss.str();
    }

    CompiledUnit GrassX86Encoder::finish(const std::string& name) {
        for (const auto& patch : label_patches_) {
            if (patch.label < 0 || static_cast<size_t>(patch.label) >= labels_.size() || labels_[patch.label].offset < 0) {
                throw std::runtime_error("Unresolved label reference: label id " + std::to_string(patch.label));
            }
        }

        CompiledUnit unit;
        unit.arch = arch_;
        unit.abi = (arch_ == TargetArch::X86_64) ? TargetABI::Win64 : TargetABI::Cdecl32;
        unit.text = code_;
        unit.relocations = relocations_;

        if (!name.empty()) {
            SymbolDef sym;
            sym.name = name;
            sym.section = SectionKind::Text;
            sym.offset = 0;
            sym.size = code_.size();
            sym.is_global = true;
            sym.is_external = false;
            unit.symbols.push_back(sym);
        }
        return unit;
    }

    void GrassX86Encoder::emit_raw(const void* data, size_t size) {
        if (data && size > 0) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(data);
            code_.insert(code_.end(), p, p + size);
        }
    }

    void GrassX86Encoder::emit_raw_byte(uint8_t b) {
        emit_u8(b);
    }

    void GrassX86Encoder::emit_u8(uint8_t byte) {
        code_.push_back(byte);
    }

    void GrassX86Encoder::emit_u16(uint16_t word) {
        code_.push_back(static_cast<uint8_t>(word & 0xFF));
        code_.push_back(static_cast<uint8_t>((word >> 8) & 0xFF));
    }

    void GrassX86Encoder::emit_u32(uint32_t dword) {
        code_.push_back(static_cast<uint8_t>(dword & 0xFF));
        code_.push_back(static_cast<uint8_t>((dword >> 8) & 0xFF));
        code_.push_back(static_cast<uint8_t>((dword >> 16) & 0xFF));
        code_.push_back(static_cast<uint8_t>((dword >> 24) & 0xFF));
    }

    void GrassX86Encoder::emit_u64(uint64_t qword) {
        emit_u32(static_cast<uint32_t>(qword & 0xFFFFFFFFULL));
        emit_u32(static_cast<uint32_t>((qword >> 32) & 0xFFFFFFFFULL));
    }

    void GrassX86Encoder::validate_reg(Reg r) const {
        if (r == Reg::None) {
            throw std::invalid_argument("Cannot use Reg::None as register operand");
        }
        if (arch_ == TargetArch::X86_32) {
            if (reg_size(r) == 8 || reg_is_ext(r) || reg_is_rex_byte(r)) {
                throw std::invalid_argument("64-bit / extended registers (R8-R15, SPL-DIL, 64-bit) are forbidden in 32-bit mode");
            }
            if (reg_is_xmm(r) && reg_raw_index(r) >= 8) {
                throw std::invalid_argument("XMM8-XMM15 registers are forbidden in 32-bit mode");
            }
        }
    }

    void GrassX86Encoder::validate_arch_gpr(Reg r) const {
        validate_reg(r);
        if (!reg_is_gpr(r)) {
            throw std::invalid_argument("Expected general purpose register");
        }
    }

    void GrassX86Encoder::validate_xmm(Reg r) const {
        validate_reg(r);
        if (!reg_is_xmm(r)) {
            throw std::invalid_argument("Expected XMM vector register operand");
        }
    }

    void GrassX86Encoder::emit_rex(bool w, bool r, bool x, bool b, bool force_rex) {
        if (arch_ == TargetArch::X86_32) {
            if (w || r || x || b || force_rex) {
                throw std::invalid_argument("REX prefix cannot be emitted in 32-bit mode");
            }
            return;
        }

        uint8_t rex = 0;
        if (w) rex |= 0x08;
        if (r) rex |= 0x04;
        if (x) rex |= 0x02;
        if (b) rex |= 0x01;

        if (rex != 0 || force_rex) {
            emit_u8(static_cast<uint8_t>(0x40 | rex));
        }
    }

    void GrassX86Encoder::emit_modrm_sib_disp(uint8_t reg_field, const MemOp& mem, size_t extra_inst_bytes) {
        // Handle explicit Reg::RIP as base register
        if (mem.base == Reg::RIP) {
            if (arch_ == TargetArch::X86_32) {
                throw std::invalid_argument("RIP-relative addressing is forbidden in 32-bit mode");
            }
            if (mem.index != Reg::None) {
                throw std::invalid_argument("RIP-relative addressing cannot have an index register");
            }
            MemOp rip_mem = mem;
            rip_mem.is_rip_relative = true;
            rip_mem.base = Reg::None;
            emit_modrm_sib_disp(reg_field, rip_mem, extra_inst_bytes);
            return;
        }

        if (mem.is_rip_relative) {
            if (arch_ == TargetArch::X86_32) {
                throw std::invalid_argument("RIP-relative addressing is forbidden in 32-bit mode");
            }
            // In 64-bit mode, mod=00, rm=5 encodes [RIP + disp32]
            uint8_t modrm = static_cast<uint8_t>((0 << 6) | ((reg_field & 7) << 3) | 5);
            emit_u8(modrm);

            if (mem.rip_label >= 0) {
                if (static_cast<size_t>(mem.rip_label) >= labels_.size()) {
                    throw std::invalid_argument("Invalid label id in MemOp::rip_rel");
                }
                if (labels_[mem.rip_label].offset >= 0) {
                    // Backward reference: label is already bound
                    int64_t next_rip = static_cast<int64_t>(code_.size() + 4 + extra_inst_bytes);
                    int64_t target = labels_[mem.rip_label].offset + static_cast<int64_t>(mem.disp);
                    int64_t rel = target - next_rip;
                    if (rel < -2147483648LL || rel > 2147483647LL) {
                        throw std::runtime_error("RIP-relative displacement out of 32-bit range");
                    }
                    emit_u32(static_cast<uint32_t>(static_cast<int32_t>(rel)));
                } else {
                    // Forward reference: register patch to be resolved in bind_label
                    LabelPatch patch;
                    patch.label = mem.rip_label;
                    patch.patch_offset = code_.size();
                    patch.inst_end_offset = code_.size() + 4 + extra_inst_bytes;
                    patch.disp = mem.disp;
                    patch.is_rel8 = false;
                    label_patches_.push_back(patch);
                    emit_u32(0);
                }
            } else if (!mem.rip_symbol.empty()) {
                RelocEntry re;
                re.section = SectionKind::Text;
                re.offset = code_.size();
                re.symbol_name = mem.rip_symbol;
                re.type = RelocType::Rel32;
                re.addend = mem.disp - 4 - static_cast<int64_t>(extra_inst_bytes);
                relocations_.push_back(re);
                emit_u32(static_cast<uint32_t>(mem.disp));
            } else {
                emit_u32(static_cast<uint32_t>(mem.disp));
            }
            return;
        }

        // Non-RIP addressing
        if (mem.base != Reg::None) {
            validate_reg(mem.base);
        }
        if (mem.index != Reg::None) {
            validate_reg(mem.index);
        }

        // Absolute 32-bit addressing (no base, no index)
        if (mem.base == Reg::None && mem.index == Reg::None) {
            if (arch_ == TargetArch::X86_32) {
                uint8_t modrm = static_cast<uint8_t>((0 << 6) | ((reg_field & 7) << 3) | 5);
                emit_u8(modrm);
                emit_u32(static_cast<uint32_t>(mem.disp));
            } else {
                // In 64-bit mode, absolute [disp32] requires SIB: rm=4, SIB=(scale 0, index 4, base 5)
                uint8_t modrm = static_cast<uint8_t>((0 << 6) | ((reg_field & 7) << 3) | 4);
                emit_u8(modrm);
                emit_u8(0x25); // SIB: scale=0, index=none(4), base=5 (disp32)
                emit_u32(static_cast<uint32_t>(mem.disp));
            }
            return;
        }

        // Index register without base
        if (mem.base == Reg::None && mem.index != Reg::None) {
            if (mem.index == Reg::RSP || mem.index == Reg::ESP) {
                throw std::invalid_argument("RSP/ESP cannot be used as SIB index register");
            }
            uint8_t scale_code = 0;
            if (mem.scale == 1) scale_code = 0;
            else if (mem.scale == 2) scale_code = 1;
            else if (mem.scale == 4) scale_code = 2;
            else if (mem.scale == 8) scale_code = 3;
            else throw std::invalid_argument("Invalid SIB scale factor, must be 1, 2, 4, or 8");

            uint8_t modrm = static_cast<uint8_t>((0 << 6) | ((reg_field & 7) << 3) | 4);
            emit_u8(modrm);
            uint8_t idx_bits = reg_raw_index(mem.index) & 7;
            uint8_t sib = static_cast<uint8_t>((scale_code << 6) | (idx_bits << 3) | 5); // base=5 indicates disp32
            emit_u8(sib);
            emit_u32(static_cast<uint32_t>(mem.disp));
            return;
        }

        // Base is present
        uint8_t base_raw = reg_raw_index(mem.base);
        uint8_t base_bits = base_raw & 7;

        bool has_index = (mem.index != Reg::None);
        uint8_t scale_code = 0;
        uint8_t idx_bits = 4; // 100b indicates no index

        if (has_index) {
            if (mem.index == Reg::RSP || mem.index == Reg::ESP) {
                throw std::invalid_argument("RSP/ESP cannot be used as SIB index register");
            }
            if (mem.scale == 1) scale_code = 0;
            else if (mem.scale == 2) scale_code = 1;
            else if (mem.scale == 4) scale_code = 2;
            else if (mem.scale == 8) scale_code = 3;
            else throw std::invalid_argument("Invalid SIB scale factor, must be 1, 2, 4, or 8");

            idx_bits = reg_raw_index(mem.index) & 7;
        }

        // SIB byte is required if index is present OR base is RSP/ESP/R12 (base_bits == 4)
        bool need_sib = has_index || (base_bits == 4);

        // Determine mod and displacement length
        uint8_t mod = 0;
        int disp_len = 0;

        if (base_bits == 5) {
            // RBP, EBP, R13: cannot use mod=00 because that encodes disp32/RIP-rel!
            if (mem.disp == 0) {
                mod = 1;
                disp_len = 1; // Mandatory disp8 = 0x00
            } else if (mem.disp >= -128 && mem.disp <= 127) {
                mod = 1;
                disp_len = 1;
            } else {
                mod = 2;
                disp_len = 4;
            }
        } else {
            if (mem.disp == 0) {
                mod = 0;
                disp_len = 0;
            } else if (mem.disp >= -128 && mem.disp <= 127) {
                mod = 1;
                disp_len = 1;
            } else {
                mod = 2;
                disp_len = 4;
            }
        }

        uint8_t rm = need_sib ? 4 : base_bits;
        uint8_t modrm = static_cast<uint8_t>((mod << 6) | ((reg_field & 7) << 3) | rm);
        emit_u8(modrm);

        if (need_sib) {
            uint8_t sib = static_cast<uint8_t>((scale_code << 6) | (idx_bits << 3) | base_bits);
            emit_u8(sib);
        }

        if (disp_len == 1) {
            emit_u8(static_cast<uint8_t>(mem.disp & 0xFF));
        } else if (disp_len == 4) {
            emit_u32(static_cast<uint32_t>(mem.disp));
        }
    }

    LabelId GrassX86Encoder::create_label() {
        LabelId id = static_cast<LabelId>(labels_.size());
        labels_.push_back({ -1 });
        return id;
    }

    void GrassX86Encoder::bind_label(LabelId label) {
        if (label < 0 || static_cast<size_t>(label) >= labels_.size()) {
            throw std::invalid_argument("Invalid label id in bind_label");
        }
        if (labels_[label].offset >= 0) {
            throw std::logic_error("Label already bound");
        }

        int64_t cur_offset = static_cast<int64_t>(code_.size());
        labels_[label].offset = cur_offset;

        // Backpatch all forward references
        for (const auto& patch : label_patches_) {
            if (patch.label == label) {
                int64_t target = cur_offset + static_cast<int64_t>(patch.disp);
                int64_t rel = target - static_cast<int64_t>(patch.inst_end_offset);
                if (patch.is_rel8) {
                    if (rel < -128 || rel > 127) {
                        throw std::runtime_error("Label displacement out of 8-bit range");
                    }
                    code_[patch.patch_offset] = static_cast<uint8_t>(rel & 0xFF);
                } else {
                    if (rel < -2147483648LL || rel > 2147483647LL) {
                        throw std::runtime_error("Label displacement out of 32-bit range");
                    }
                    int32_t rel32 = static_cast<int32_t>(rel);
                    std::memcpy(&code_[patch.patch_offset], &rel32, 4);
                }
            }
        }
    }

    // == Data Movement ==

    void GrassX86Encoder::mov_rr(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz_dst = reg_size(dst);
        uint8_t sz_src = reg_size(src);
        if (sz_dst != sz_src) {
            throw std::invalid_argument("Operand size mismatch in mov_rr");
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
            bool force_rex = (dst_rex || src_rex);
            emit_rex(false, reg_is_ext(src), false, reg_is_ext(dst), force_rex);
            emit_u8(0x88); // mov rm8, r8
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src) & 7) << 3) | (reg_raw_index(dst) & 7));
            emit_u8(modrm);
        } else {
            if (sz_dst == 2) {
                emit_u8(0x66);
            }
            emit_rex(sz_dst == 8, reg_is_ext(src), false, reg_is_ext(dst));
            emit_u8(0x89); // mov rm16/32/64, r16/32/64
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(src) & 7) << 3) | (reg_raw_index(dst) & 7));
            emit_u8(modrm);
        }
    }

    void GrassX86Encoder::mov_ri64(Reg dst, int64_t imm) {
        validate_arch_gpr(dst);
        if (arch_ == TargetArch::X86_32) {
            throw std::invalid_argument("mov_ri64 is not supported in 32-bit mode");
        }
        if (reg_size(dst) != 8) {
            throw std::invalid_argument("mov_ri64 requires 64-bit destination register");
        }

        emit_rex(true, false, false, reg_is_ext(dst));
        emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(dst) & 7)));
        emit_u64(static_cast<uint64_t>(imm));
    }

    void GrassX86Encoder::mov_ri32(Reg dst, int32_t imm) {
        validate_arch_gpr(dst);
        uint8_t sz = reg_size(dst);
        if (sz == 8) {
            // mov r64, imm32 (sign-extended): REX.W C7 /0 imm32
            emit_rex(true, false, false, reg_is_ext(dst));
            emit_u8(0xC7);
            uint8_t modrm = static_cast<uint8_t>((3 << 6) | (0 << 3) | (reg_raw_index(dst) & 7));
            emit_u8(modrm);
            emit_u32(static_cast<uint32_t>(imm));
        } else if (sz == 4) {
            emit_rex(false, false, false, reg_is_ext(dst));
            emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(dst) & 7)));
            emit_u32(static_cast<uint32_t>(imm));
        } else if (sz == 2) {
            emit_u8(0x66);
            emit_rex(false, false, false, reg_is_ext(dst));
            emit_u8(static_cast<uint8_t>(0xB8 + (reg_raw_index(dst) & 7)));
            emit_u16(static_cast<uint16_t>(imm & 0xFFFF));
        } else if (sz == 1) {
            bool force_rex = reg_is_rex_byte(dst);
            emit_rex(false, false, false, reg_is_ext(dst), force_rex);
            emit_u8(static_cast<uint8_t>(0xB0 + (reg_raw_index(dst) & 7)));
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        }
    }

    void GrassX86Encoder::mov_rm(Reg dst, const MemOp& src) {
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
            emit_u8(0x8A); // mov r8, rm8
        } else {
            if (sz == 2) {
                emit_u8(0x66);
            }
            emit_rex(sz == 8, reg_is_ext(dst), idx_ext, base_ext);
            emit_u8(0x8B); // mov r16/32/64, rm16/32/64
        }
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::mov_mr(const MemOp& dst, Reg src) {
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
            emit_u8(0x88); // mov rm8, r8
        } else {
            if (sz == 2) {
                emit_u8(0x66);
            }
            emit_rex(sz == 8, reg_is_ext(src), idx_ext, base_ext);
            emit_u8(0x89); // mov rm16/32/64, r16/32/64
        }
        emit_modrm_sib_disp(reg_raw_index(src) & 7, dst);
    }

    void GrassX86Encoder::mov_mi32(const MemOp& dst, int32_t imm, uint8_t size) {
        uint8_t eff_size = (size != 0) ? size : (dst.size != 0 ? dst.size : 4);
        bool base_ext = (dst.base != Reg::None) && reg_is_ext(dst.base);
        bool idx_ext = (dst.index != Reg::None) && reg_is_ext(dst.index);

        if (eff_size == 1) {
            emit_rex(false, false, idx_ext, base_ext);
            emit_u8(0xC6);
            emit_modrm_sib_disp(0, dst, 1);
            emit_u8(static_cast<uint8_t>(imm & 0xFF));
        } else if (eff_size == 2) {
            emit_u8(0x66);
            emit_rex(false, false, idx_ext, base_ext);
            emit_u8(0xC7);
            emit_modrm_sib_disp(0, dst, 2);
            emit_u16(static_cast<uint16_t>(imm & 0xFFFF));
        } else if (eff_size == 4) {
            emit_rex(false, false, idx_ext, base_ext);
            emit_u8(0xC7);
            emit_modrm_sib_disp(0, dst, 4);
            emit_u32(static_cast<uint32_t>(imm));
        } else if (eff_size == 8) {
            if (arch_ == TargetArch::X86_32) {
                throw std::invalid_argument("64-bit memory write is forbidden in 32-bit mode");
            }
            emit_rex(true, false, idx_ext, base_ext);
            emit_u8(0xC7);
            emit_modrm_sib_disp(0, dst, 4);
            emit_u32(static_cast<uint32_t>(imm));
        } else {
            throw std::invalid_argument("Invalid size for mov_mi32");
        }
    }

    void GrassX86Encoder::movzx_rr8(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        if (reg_size(src) != 1) {
            throw std::invalid_argument("movzx_rr8 requires 8-bit source register");
        }
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 2) {
            throw std::invalid_argument("movzx_rr8 requires 16/32/64-bit destination register");
        }
        if (sz_dst == 2) emit_u8(0x66);
        bool force_rex = reg_is_rex_byte(src);
        bool rex_present = (sz_dst == 8) || reg_is_ext(dst) || reg_is_ext(src) || force_rex;
        if (rex_present && reg_is_high_byte(src)) {
            throw std::invalid_argument("Cannot access legacy high-byte register (AH, CH, DH, BH) when REX prefix is required");
        }
        emit_rex(sz_dst == 8, reg_is_ext(dst), false, reg_is_ext(src), force_rex);
        emit_u8(0x0F);
        emit_u8(0xB6);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movzx_rr16(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        if (reg_size(src) != 2) {
            throw std::invalid_argument("movzx_rr16 requires 16-bit source register");
        }
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 4) {
            throw std::invalid_argument("movzx_rr16 requires 32-bit or 64-bit destination register");
        }
        emit_rex(sz_dst == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xB7);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movzx_rm8(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 2) {
            throw std::invalid_argument("movzx_rm8 requires 16/32/64-bit destination register");
        }
        if (sz_dst == 2) emit_u8(0x66);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz_dst == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0xB6);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movzx_rm16(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 4) {
            throw std::invalid_argument("movzx_rm16 requires 32-bit or 64-bit destination register");
        }
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz_dst == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0xB7);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movsx_rr8(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        if (reg_size(src) != 1) {
            throw std::invalid_argument("movsx_rr8 requires 8-bit source register");
        }
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 2) {
            throw std::invalid_argument("movsx_rr8 requires 16/32/64-bit destination register");
        }
        if (sz_dst == 2) emit_u8(0x66);
        bool force_rex = reg_is_rex_byte(src);
        bool rex_present = (sz_dst == 8) || reg_is_ext(dst) || reg_is_ext(src) || force_rex;
        if (rex_present && reg_is_high_byte(src)) {
            throw std::invalid_argument("Cannot access legacy high-byte register (AH, CH, DH, BH) when REX prefix is required");
        }
        emit_rex(sz_dst == 8, reg_is_ext(dst), false, reg_is_ext(src), force_rex);
        emit_u8(0x0F);
        emit_u8(0xBE);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movsx_rr16(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        if (reg_size(src) != 2) {
            throw std::invalid_argument("movsx_rr16 requires 16-bit source register");
        }
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 4) {
            throw std::invalid_argument("movsx_rr16 requires 32-bit or 64-bit destination register");
        }
        emit_rex(sz_dst == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(0xBF);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movsxd_rr32(Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        if (arch_ == TargetArch::X86_32) {
            throw std::invalid_argument("movsxd_rr32 is not supported in 32-bit mode");
        }
        if (reg_size(dst) != 8 || reg_size(src) != 4) {
            throw std::invalid_argument("movsxd_rr32 requires 64-bit dst and 32-bit src");
        }
        emit_rex(true, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x63);
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::movsx_rm8(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 2) {
            throw std::invalid_argument("movsx_rm8 requires 16/32/64-bit destination register");
        }
        if (sz_dst == 2) emit_u8(0x66);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz_dst == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0xBE);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movsx_rm16(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz_dst = reg_size(dst);
        if (sz_dst < 4) {
            throw std::invalid_argument("movsx_rm16 requires 32-bit or 64-bit destination register");
        }
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz_dst == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x0F);
        emit_u8(0xBF);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::movsxd_rm32(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        if (arch_ == TargetArch::X86_32) {
            throw std::invalid_argument("movsxd_rm32 is not supported in 32-bit mode");
        }
        if (reg_size(dst) != 8) {
            throw std::invalid_argument("movsxd_rm32 requires 64-bit dst");
        }
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(true, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x63);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::lea_rm(Reg dst, const MemOp& src) {
        validate_arch_gpr(dst);
        uint8_t sz = reg_size(dst);
        if (sz < 2) {
            throw std::invalid_argument("lea_rm requires 16/32/64-bit destination register");
        }
        if (sz == 2) emit_u8(0x66);
        bool base_ext = (src.base != Reg::None) && reg_is_ext(src.base);
        bool idx_ext = (src.index != Reg::None) && reg_is_ext(src.index);
        emit_rex(sz == 8, reg_is_ext(dst), idx_ext, base_ext);
        emit_u8(0x8D);
        emit_modrm_sib_disp(reg_raw_index(dst) & 7, src);
    }

    void GrassX86Encoder::xchg_rr(Reg r1, Reg r2) {
        validate_arch_gpr(r1);
        validate_arch_gpr(r2);
        uint8_t sz1 = reg_size(r1);
        uint8_t sz2 = reg_size(r2);
        if (sz1 != sz2) {
            throw std::invalid_argument("Operand size mismatch in xchg_rr");
        }
        if (sz1 == 1) {
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
            emit_rex(false, reg_is_ext(r1), false, reg_is_ext(r2), force_rex);
            emit_u8(0x86);
        } else {
            if (sz1 == 2) emit_u8(0x66);
            emit_rex(sz1 == 8, reg_is_ext(r1), false, reg_is_ext(r2));
            emit_u8(0x87);
        }
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(r1) & 7) << 3) | (reg_raw_index(r2) & 7));
        emit_u8(modrm);
    }

    void GrassX86Encoder::cmov_cc_rr(CondCode cc, Reg dst, Reg src) {
        validate_arch_gpr(dst);
        validate_arch_gpr(src);
        uint8_t sz = reg_size(dst);
        if (sz != reg_size(src) || sz < 2) {
            throw std::invalid_argument("cmov_cc_rr requires matching 16/32/64-bit operands");
        }
        if (sz == 2) emit_u8(0x66);
        emit_rex(sz == 8, reg_is_ext(dst), false, reg_is_ext(src));
        emit_u8(0x0F);
        emit_u8(static_cast<uint8_t>(0x40 + (static_cast<uint8_t>(cc) & 0x0F)));
        uint8_t modrm = static_cast<uint8_t>((3 << 6) | ((reg_raw_index(dst) & 7) << 3) | (reg_raw_index(src) & 7));
        emit_u8(modrm);
    }

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
