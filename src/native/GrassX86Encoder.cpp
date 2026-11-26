#include "native/GrassX86Encoder.h"
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

    void GrassX86Encoder::validate_ymm(Reg r) const {
        validate_reg(r);
        if (!reg_is_ymm(r)) {
            throw std::invalid_argument("Expected YMM vector register operand");
        }
    }

    void GrassX86Encoder::emit_vex(bool l, uint8_t pp, uint8_t map_select, bool w,
                                   bool r_ext, bool x_ext, bool b_ext, uint8_t vvvv) {
        if (arch_ == TargetArch::X86_32) {
            if (r_ext || x_ext || b_ext || (vvvv >= 8)) {
                throw std::invalid_argument("Extended registers cannot be used in 32-bit VEX mode");
            }
        }
        uint8_t nR = r_ext ? 0 : 1;
        uint8_t nX = x_ext ? 0 : 1;
        uint8_t nB = b_ext ? 0 : 1;
        uint8_t nV = static_cast<uint8_t>((~vvvv) & 0x0F);
        uint8_t l_bit = l ? 1 : 0;
        uint8_t pp_bits = pp & 0x03;

        if (!x_ext && !b_ext && !w && map_select == 1) {
            // 2-byte VEX: C5 [~R ~vvvv L pp]
            emit_u8(0xC5);
            emit_u8(static_cast<uint8_t>((nR << 7) | (nV << 3) | (l_bit << 2) | pp_bits));
        } else {
            // 3-byte VEX: C4 [~R ~X ~B mmmmm] [W ~vvvv L pp]
            emit_u8(0xC4);
            emit_u8(static_cast<uint8_t>((nR << 7) | (nX << 6) | (nB << 5) | (map_select & 0x1F)));
            emit_u8(static_cast<uint8_t>(((w ? 1 : 0) << 7) | (nV << 3) | (l_bit << 2) | pp_bits));
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


} // namespace grass::backend
