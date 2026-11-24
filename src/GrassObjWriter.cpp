#include "GrassObjWriter.h"
#include <iostream>
#include <stdexcept>

namespace grass::backend {

    // =========================================================================
    // 1. BinaryBuffer Implementation
    // =========================================================================

    BinaryBuffer::BinaryBuffer(size_t initial_capacity) {
        buffer_.reserve(initial_capacity);
    }

    void BinaryBuffer::emit_u8(uint8_t val) {
        buffer_.push_back(val);
    }

    void BinaryBuffer::emit_u16(uint16_t val) {
        buffer_.push_back(static_cast<uint8_t>(val & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    }

    void BinaryBuffer::emit_u32(uint32_t val) {
        buffer_.push_back(static_cast<uint8_t>(val & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
        buffer_.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    }

    void BinaryBuffer::emit_u64(uint64_t val) {
        for (size_t i = 0; i < 8; ++i) {
            buffer_.push_back(static_cast<uint8_t>((val >> (i * 8)) & 0xFF));
        }
    }

    void BinaryBuffer::emit_i8(int8_t val) {
        emit_u8(static_cast<uint8_t>(val));
    }

    void BinaryBuffer::emit_i16(int16_t val) {
        emit_u16(static_cast<uint16_t>(val));
    }

    void BinaryBuffer::emit_i32(int32_t val) {
        emit_u32(static_cast<uint32_t>(val));
    }

    void BinaryBuffer::emit_i64(int64_t val) {
        emit_u64(static_cast<uint64_t>(val));
    }

    void BinaryBuffer::emit_bytes(const void* src, size_t count) {
        if (src && count > 0) {
            const uint8_t* p = static_cast<const uint8_t*>(src);
            buffer_.insert(buffer_.end(), p, p + count);
        }
    }

    void BinaryBuffer::emit_bytes(const std::vector<uint8_t>& bytes) {
        buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    }

    void BinaryBuffer::emit_string(const std::string& str) {
        emit_bytes(str.data(), str.size());
    }

    void BinaryBuffer::emit_cstring(const std::string& str) {
        emit_bytes(str.data(), str.size());
        emit_u8(0);
    }

    void BinaryBuffer::emit_zeros(size_t count) {
        if (count > 0) {
            buffer_.insert(buffer_.end(), count, 0);
        }
    }

    void BinaryBuffer::align_to(size_t alignment, uint8_t pad_byte) {
        if (alignment > 1) {
            size_t rem = buffer_.size() % alignment;
            if (rem != 0) {
                size_t pad = alignment - rem;
                buffer_.insert(buffer_.end(), pad, pad_byte);
            }
        }
    }

    void BinaryBuffer::patch_u8(size_t offset, uint8_t val) {
        if (offset < buffer_.size()) {
            buffer_[offset] = val;
        }
    }

    void BinaryBuffer::patch_u16(size_t offset, uint16_t val) {
        if (offset + 2 <= buffer_.size()) {
            buffer_[offset]     = static_cast<uint8_t>(val & 0xFF);
            buffer_[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        }
    }

    void BinaryBuffer::patch_u32(size_t offset, uint32_t val) {
        if (offset + 4 <= buffer_.size()) {
            buffer_[offset]     = static_cast<uint8_t>(val & 0xFF);
            buffer_[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
            buffer_[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            buffer_[offset + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
        }
    }

    void BinaryBuffer::patch_u64(size_t offset, uint64_t val) {
        if (offset + 8 <= buffer_.size()) {
            for (size_t i = 0; i < 8; ++i) {
                buffer_[offset + i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
            }
        }
    }

    void BinaryBuffer::patch_i32(size_t offset, int32_t val) {
        patch_u32(offset, static_cast<uint32_t>(val));
    }

    void BinaryBuffer::patch_i64(size_t offset, int64_t val) {
        patch_u64(offset, static_cast<uint64_t>(val));
    }

    void BinaryBuffer::patch_bytes(size_t offset, const void* src, size_t count) {
        if (src && offset + count <= buffer_.size()) {
            std::memcpy(buffer_.data() + offset, src, count);
        }
    }

    uint8_t BinaryBuffer::read_u8(size_t offset) const {
        if (offset < buffer_.size()) {
            return buffer_[offset];
        }
        return 0;
    }

    uint16_t BinaryBuffer::read_u16(size_t offset) const {
        if (offset + 2 <= buffer_.size()) {
            return static_cast<uint16_t>(buffer_[offset]) |
                  (static_cast<uint16_t>(buffer_[offset + 1]) << 8);
        }
        return 0;
    }

    uint32_t BinaryBuffer::read_u32(size_t offset) const {
        if (offset + 4 <= buffer_.size()) {
            return  static_cast<uint32_t>(buffer_[offset]) |
                   (static_cast<uint32_t>(buffer_[offset + 1]) << 8) |
                   (static_cast<uint32_t>(buffer_[offset + 2]) << 16) |
                   (static_cast<uint32_t>(buffer_[offset + 3]) << 24);
        }
        return 0;
    }

    uint64_t BinaryBuffer::read_u64(size_t offset) const {
        if (offset + 8 <= buffer_.size()) {
            uint64_t val = 0;
            for (size_t i = 0; i < 8; ++i) {
                val |= (static_cast<uint64_t>(buffer_[offset + i]) << (i * 8));
            }
            return val;
        }
        return 0;
    }

    int32_t BinaryBuffer::read_i32(size_t offset) const {
        return static_cast<int32_t>(read_u32(offset));
    }

    int64_t BinaryBuffer::read_i64(size_t offset) const {
        return static_cast<int64_t>(read_u64(offset));
    }

    size_t BinaryBuffer::size() const noexcept {
        return buffer_.size();
    }

    bool BinaryBuffer::empty() const noexcept {
        return buffer_.empty();
    }

    void BinaryBuffer::clear() noexcept {
        buffer_.clear();
    }

    void BinaryBuffer::reserve(size_t capacity) {
        buffer_.reserve(capacity);
    }

    const uint8_t* BinaryBuffer::data() const noexcept {
        return buffer_.data();
    }

    uint8_t* BinaryBuffer::data() noexcept {
        return buffer_.data();
    }

    const std::vector<uint8_t>& BinaryBuffer::get_bytes() const noexcept {
        return buffer_;
    }

    std::vector<uint8_t>& BinaryBuffer::get_bytes() noexcept {
        return buffer_;
    }

    bool BinaryBuffer::write_to_file(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }
        if (!buffer_.empty()) {
            out.write(reinterpret_cast<const char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
        }
        return out.good();
    }

    bool BinaryBuffer::read_from_file(const std::string& path) {
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            return false;
        }
        std::streamsize fsize = in.tellg();
        in.seekg(0, std::ios::beg);
        buffer_.resize(static_cast<size_t>(fsize));
        if (fsize > 0) {
            in.read(reinterpret_cast<char*>(buffer_.data()), fsize);
        }
        return in.good();
    }

    // =========================================================================
    // 2. StringTableBuilder Implementation
    // =========================================================================

    uint32_t StringTableBuilder::add_coff(const std::string& str) {
        auto it = coff_offsets_.find(str);
        if (it != coff_offsets_.end()) {
            return it->second;
        }
        uint32_t offset = static_cast<uint32_t>(coff_total_size_);
        coff_offsets_[str] = offset;
        coff_strings_.push_back(str);
        strings_.push_back(str);
        coff_total_size_ += str.size() + 1;
        return offset;
    }

    uint32_t StringTableBuilder::add_elf(const std::string& str) {
        if (str.empty()) {
            return 0;
        }
        auto it = elf_offsets_.find(str);
        if (it != elf_offsets_.end()) {
            return it->second;
        }
        uint32_t offset = static_cast<uint32_t>(elf_total_size_);
        elf_offsets_[str] = offset;
        elf_strings_.push_back(str);
        strings_.push_back(str);
        elf_total_size_ += str.size() + 1;
        return offset;
    }

    uint32_t StringTableBuilder::get_offset(const std::string& str) const {
        auto it_elf = elf_offsets_.find(str);
        if (it_elf != elf_offsets_.end()) {
            return it_elf->second;
        }
        auto it_coff = coff_offsets_.find(str);
        if (it_coff != coff_offsets_.end()) {
            return it_coff->second;
        }
        return 0;
    }

    uint32_t StringTableBuilder::get_offset_coff(const std::string& str) const {
        auto it = coff_offsets_.find(str);
        return (it != coff_offsets_.end()) ? it->second : 0;
    }

    uint32_t StringTableBuilder::get_offset_elf(const std::string& str) const {
        if (str.empty()) return 0;
        auto it = elf_offsets_.find(str);
        return (it != elf_offsets_.end()) ? it->second : 0;
    }

    bool StringTableBuilder::contains(const std::string& str) const {
        return elf_offsets_.find(str) != elf_offsets_.end() ||
               coff_offsets_.find(str) != coff_offsets_.end();
    }

    void StringTableBuilder::build_coff(BinaryBuffer& out) const {
        out.emit_u32(static_cast<uint32_t>(coff_total_size_));
        for (const auto& s : coff_strings_) {
            out.emit_cstring(s);
        }
    }

    void StringTableBuilder::build_elf(BinaryBuffer& out) const {
        out.emit_u8(0);
        for (const auto& s : elf_strings_) {
            out.emit_cstring(s);
        }
    }

    size_t StringTableBuilder::total_bytes_coff() const noexcept {
        return coff_total_size_;
    }

    size_t StringTableBuilder::total_bytes_elf() const noexcept {
        return elf_total_size_;
    }

    void StringTableBuilder::clear() noexcept {
        strings_.clear();
        coff_strings_.clear();
        elf_strings_.clear();
        coff_offsets_.clear();
        elf_offsets_.clear();
        coff_total_size_ = 4;
        elf_total_size_ = 1;
    }

    size_t StringTableBuilder::count() const noexcept {
        return strings_.size();
    }

    const std::vector<std::string>& StringTableBuilder::get_strings() const noexcept {
        return strings_;
    }

    // =========================================================================
    // 3. ABISpec Implementation
    // =========================================================================

    ABISpec ABISpec::for_abi(TargetABI abi) {
        ABISpec spec;
        spec.abi_ = abi;

        switch (abi) {
            case TargetABI::Win64:
                spec.arch_ = TargetArch::X86_64;
                spec.int_arg_regs_ = { Reg::RCX, Reg::RDX, Reg::R8, Reg::R9 };
                spec.float_arg_regs_ = { Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3 };
                spec.callee_saved_ = {
                    Reg::RBX, Reg::RBP, Reg::RDI, Reg::RSI,
                    Reg::R12, Reg::R13, Reg::R14, Reg::R15,
                    Reg::XMM6, Reg::XMM7, Reg::XMM8, Reg::XMM9,
                    Reg::XMM10, Reg::XMM11, Reg::XMM12, Reg::XMM13,
                    Reg::XMM14, Reg::XMM15
                };
                spec.caller_saved_ = {
                    Reg::RAX, Reg::RCX, Reg::RDX, Reg::R8,
                    Reg::R9, Reg::R10, Reg::R11,
                    Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3,
                    Reg::XMM4, Reg::XMM5
                };
                spec.ret_reg_ = Reg::RAX;
                spec.float_ret_reg_ = Reg::XMM0;
                spec.shadow_space_ = 32;
                spec.red_zone_ = 0;
                spec.stack_align_ = 16;
                break;

            case TargetABI::SysV64:
                spec.arch_ = TargetArch::X86_64;
                spec.int_arg_regs_ = { Reg::RDI, Reg::RSI, Reg::RDX, Reg::RCX, Reg::R8, Reg::R9 };
                spec.float_arg_regs_ = {
                    Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3,
                    Reg::XMM4, Reg::XMM5, Reg::XMM6, Reg::XMM7
                };
                spec.callee_saved_ = {
                    Reg::RBX, Reg::RBP, Reg::R12, Reg::R13, Reg::R14, Reg::R15
                };
                spec.caller_saved_ = {
                    Reg::RAX, Reg::RCX, Reg::RDX, Reg::RSI, Reg::RDI,
                    Reg::R8, Reg::R9, Reg::R10, Reg::R11,
                    Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3,
                    Reg::XMM4, Reg::XMM5, Reg::XMM6, Reg::XMM7,
                    Reg::XMM8, Reg::XMM9, Reg::XMM10, Reg::XMM11,
                    Reg::XMM12, Reg::XMM13, Reg::XMM14, Reg::XMM15
                };
                spec.ret_reg_ = Reg::RAX;
                spec.float_ret_reg_ = Reg::XMM0;
                spec.shadow_space_ = 0;
                spec.red_zone_ = 128;
                spec.stack_align_ = 16;
                break;

            case TargetABI::Cdecl32:
                spec.arch_ = TargetArch::X86_32;
                spec.int_arg_regs_ = {};
                spec.float_arg_regs_ = {};
                spec.callee_saved_ = { Reg::EBX, Reg::ESI, Reg::EDI, Reg::EBP };
                spec.caller_saved_ = { Reg::EAX, Reg::ECX, Reg::EDX };
                spec.ret_reg_ = Reg::EAX;
                spec.float_ret_reg_ = Reg::None;
                spec.shadow_space_ = 0;
                spec.red_zone_ = 0;
                spec.stack_align_ = 16;
                break;

            case TargetABI::Baremetal64:
                spec.arch_ = TargetArch::X86_64;
                spec.int_arg_regs_ = { Reg::RDI, Reg::RSI, Reg::RDX, Reg::RCX, Reg::R8, Reg::R9 };
                spec.float_arg_regs_ = {
                    Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3,
                    Reg::XMM4, Reg::XMM5, Reg::XMM6, Reg::XMM7
                };
                spec.callee_saved_ = {
                    Reg::RBX, Reg::RBP, Reg::R12, Reg::R13, Reg::R14, Reg::R15
                };
                spec.caller_saved_ = {
                    Reg::RAX, Reg::RCX, Reg::RDX, Reg::RSI, Reg::RDI,
                    Reg::R8, Reg::R9, Reg::R10, Reg::R11,
                    Reg::XMM0, Reg::XMM1, Reg::XMM2, Reg::XMM3,
                    Reg::XMM4, Reg::XMM5, Reg::XMM6, Reg::XMM7,
                    Reg::XMM8, Reg::XMM9, Reg::XMM10, Reg::XMM11,
                    Reg::XMM12, Reg::XMM13, Reg::XMM14, Reg::XMM15
                };
                spec.ret_reg_ = Reg::RAX;
                spec.float_ret_reg_ = Reg::XMM0;
                spec.shadow_space_ = 0;
                spec.red_zone_ = 0;
                spec.stack_align_ = 16;
                break;

            case TargetABI::Baremetal32:
                spec.arch_ = TargetArch::X86_32;
                spec.int_arg_regs_ = {};
                spec.float_arg_regs_ = {};
                spec.callee_saved_ = { Reg::EBX, Reg::ESI, Reg::EDI, Reg::EBP };
                spec.caller_saved_ = { Reg::EAX, Reg::ECX, Reg::EDX };
                spec.ret_reg_ = Reg::EAX;
                spec.float_ret_reg_ = Reg::None;
                spec.shadow_space_ = 0;
                spec.red_zone_ = 0;
                spec.stack_align_ = 16;
                break;
        }

        return spec;
    }

    TargetABI ABISpec::get_abi() const noexcept {
        return abi_;
    }

    TargetArch ABISpec::get_arch() const noexcept {
        return arch_;
    }

    const std::vector<Reg>& ABISpec::get_integer_arg_regs() const noexcept {
        return int_arg_regs_;
    }

    const std::vector<Reg>& ABISpec::get_float_arg_regs() const noexcept {
        return float_arg_regs_;
    }

    const std::vector<Reg>& ABISpec::get_callee_saved_regs() const noexcept {
        return callee_saved_;
    }

    const std::vector<Reg>& ABISpec::get_caller_saved_regs() const noexcept {
        return caller_saved_;
    }

    Reg ABISpec::get_return_reg() const noexcept {
        return ret_reg_;
    }

    Reg ABISpec::get_float_return_reg() const noexcept {
        return float_ret_reg_;
    }

    uint32_t ABISpec::get_shadow_space_size() const noexcept {
        return shadow_space_;
    }

    uint32_t ABISpec::get_red_zone_size() const noexcept {
        return red_zone_;
    }

    uint32_t ABISpec::get_stack_alignment() const noexcept {
        return stack_align_;
    }

    bool ABISpec::is_callee_saved(Reg reg) const {
        for (auto r : callee_saved_) {
            if (r == reg) return true;
        }
        return false;
    }

    bool ABISpec::is_caller_saved(Reg reg) const {
        for (auto r : caller_saved_) {
            if (r == reg) return true;
        }
        return false;
    }

    const char* ABISpec::reg_name(Reg reg) {
        switch (reg) {
            case Reg::RAX: return "rax";
            case Reg::RCX: return "rcx";
            case Reg::RDX: return "rdx";
            case Reg::RBX: return "rbx";
            case Reg::RSP: return "rsp";
            case Reg::RBP: return "rbp";
            case Reg::RSI: return "rsi";
            case Reg::RDI: return "rdi";
            case Reg::R8:  return "r8";
            case Reg::R9:  return "r9";
            case Reg::R10: return "r10";
            case Reg::R11: return "r11";
            case Reg::R12: return "r12";
            case Reg::R13: return "r13";
            case Reg::R14: return "r14";
            case Reg::R15: return "r15";
            case Reg::XMM0: return "xmm0";
            case Reg::XMM1: return "xmm1";
            case Reg::XMM2: return "xmm2";
            case Reg::XMM3: return "xmm3";
            case Reg::XMM4: return "xmm4";
            case Reg::XMM5: return "xmm5";
            case Reg::XMM6: return "xmm6";
            case Reg::XMM7: return "xmm7";
            case Reg::XMM8: return "xmm8";
            case Reg::XMM9: return "xmm9";
            case Reg::XMM10: return "xmm10";
            case Reg::XMM11: return "xmm11";
            case Reg::XMM12: return "xmm12";
            case Reg::XMM13: return "xmm13";
            case Reg::XMM14: return "xmm14";
            case Reg::XMM15: return "xmm15";
            default: return "unknown";
        }
    }

    StackFrameInfo ABISpec::compute_stack_frame(const StackFrameRequest& req) const {
        StackFrameInfo info;
        info.abi = abi_;
        info.arch = arch_;
        info.use_frame_pointer = req.use_frame_pointer;
        info.local_vars_size = req.local_vars_size;

        std::vector<Reg> filtered_callee;
        for (Reg r : req.callee_saved_regs) {
            if (req.use_frame_pointer && (r == Reg::RBP || r == Reg::EBP)) {
                continue;
            }
            filtered_callee.push_back(r);
        }
        info.callee_saved_regs = filtered_callee;

        uint32_t slot_size = (arch_ == TargetArch::X86_64) ? 8 : 4;
        uint32_t ret_addr_size = slot_size;

        if (abi_ == TargetABI::Win64 && req.has_calls) {
            info.shadow_space_size = 32;
        } else {
            info.shadow_space_size = 0;
        }

        info.outgoing_args_size = req.max_outgoing_args_size;

        uint32_t pushed_count = (req.use_frame_pointer ? 1 : 0) + static_cast<uint32_t>(info.callee_saved_regs.size());
        info.pushed_regs_size = pushed_count * slot_size;

        uint32_t needed = info.local_vars_size + info.outgoing_args_size + info.shadow_space_size;
        uint32_t total_pushed = ret_addr_size + info.pushed_regs_size;

        uint32_t unaligned_total = total_pushed + needed;
        uint32_t aligned_total = (unaligned_total + (stack_align_ - 1)) / stack_align_ * stack_align_;

        info.total_frame_size = aligned_total - total_pushed;
        info.alignment_padding = info.total_frame_size - needed;

        return info;
    }

    void ABISpec::emit_prologue_bytes(const StackFrameInfo& frame, BinaryBuffer& out) const {
        bool is_64 = (frame.arch == TargetArch::X86_64);

        if (frame.use_frame_pointer) {
            out.emit_u8(0x55); // push rbp / push ebp
            if (is_64) {
                out.emit_u8(0x48);
                out.emit_u8(0x89);
                out.emit_u8(0xE5); // mov rbp, rsp
            } else {
                out.emit_u8(0x89);
                out.emit_u8(0xE5); // mov ebp, esp
            }
        }

        for (Reg reg : frame.callee_saved_regs) {
            if (frame.use_frame_pointer && (reg == Reg::RBP || reg == Reg::EBP)) {
                continue;
            }
            uint8_t r = reg_raw_index(reg);
            if (is_64) {
                if (r <= 7) {
                    out.emit_u8(0x50 + r);
                } else if (r >= 8 && r <= 15) {
                    out.emit_u8(0x41);
                    out.emit_u8(0x50 + (r - 8));
                }
            } else {
                if (r <= 7) {
                    out.emit_u8(0x50 + r);
                }
            }
        }

        if (frame.total_frame_size > 0) {
            if (is_64) {
                if (frame.total_frame_size <= 127) {
                    out.emit_u8(0x48);
                    out.emit_u8(0x83);
                    out.emit_u8(0xEC);
                    out.emit_u8(static_cast<uint8_t>(frame.total_frame_size));
                } else {
                    out.emit_u8(0x48);
                    out.emit_u8(0x81);
                    out.emit_u8(0xEC);
                    out.emit_u32(frame.total_frame_size);
                }
            } else {
                if (frame.total_frame_size <= 127) {
                    out.emit_u8(0x83);
                    out.emit_u8(0xEC);
                    out.emit_u8(static_cast<uint8_t>(frame.total_frame_size));
                } else {
                    out.emit_u8(0x81);
                    out.emit_u8(0xEC);
                    out.emit_u32(frame.total_frame_size);
                }
            }
        }
    }

    void ABISpec::emit_epilogue_bytes(const StackFrameInfo& frame, BinaryBuffer& out) const {
        bool is_64 = (frame.arch == TargetArch::X86_64);

        if (frame.total_frame_size > 0) {
            if (is_64) {
                if (frame.total_frame_size <= 127) {
                    out.emit_u8(0x48);
                    out.emit_u8(0x83);
                    out.emit_u8(0xC4);
                    out.emit_u8(static_cast<uint8_t>(frame.total_frame_size));
                } else {
                    out.emit_u8(0x48);
                    out.emit_u8(0x81);
                    out.emit_u8(0xC4);
                    out.emit_u32(frame.total_frame_size);
                }
            } else {
                if (frame.total_frame_size <= 127) {
                    out.emit_u8(0x83);
                    out.emit_u8(0xC4);
                    out.emit_u8(static_cast<uint8_t>(frame.total_frame_size));
                } else {
                    out.emit_u8(0x81);
                    out.emit_u8(0xC4);
                    out.emit_u32(frame.total_frame_size);
                }
            }
        }

        for (auto it = frame.callee_saved_regs.rbegin(); it != frame.callee_saved_regs.rend(); ++it) {
            if (frame.use_frame_pointer && (*it == Reg::RBP || *it == Reg::EBP)) {
                continue;
            }
            uint8_t r = reg_raw_index(*it);
            if (is_64) {
                if (r <= 7) {
                    out.emit_u8(0x58 + r);
                } else if (r >= 8 && r <= 15) {
                    out.emit_u8(0x41);
                    out.emit_u8(0x58 + (r - 8));
                }
            } else {
                if (r <= 7) {
                    out.emit_u8(0x58 + r);
                }
            }
        }

        if (frame.use_frame_pointer) {
            out.emit_u8(0x5D); // pop rbp / pop ebp
        }

        out.emit_u8(0xC3); // ret
    }

    // =========================================================================
    // 4. LinkMap Implementation
    // =========================================================================

    void LinkMap::set_base_address(uint64_t addr) noexcept {
        base_address_ = addr;
    }

    uint64_t LinkMap::get_base_address() const noexcept {
        return base_address_;
    }

    void LinkMap::add_section_entry(const std::string& name, uint64_t start_addr, uint64_t size, uint32_t align) {
        SectionMapEntry entry;
        entry.name = name;
        entry.start_address = start_addr;
        entry.size = size;
        entry.end_address = start_addr + size;
        entry.alignment = align;
        sections_.push_back(entry);
    }

    void LinkMap::add_symbol_entry(const std::string& name, uint64_t addr, uint64_t size, SectionKind sec, bool is_global) {
        SymbolMapEntry entry;
        entry.name = name;
        entry.address = addr;
        entry.size = size;
        entry.section = sec;
        entry.is_global = is_global;
        symbols_.push_back(entry);
    }

    void LinkMap::add_reloc_entry(SectionKind sec, uint64_t offset, uint64_t site_addr, const std::string& sym, RelocType type, int64_t addend, int64_t computed_value) {
        RelocMapEntry entry;
        entry.section = sec;
        entry.section_offset = offset;
        entry.site_address = site_addr;
        entry.target_symbol = sym;
        entry.type = type;
        entry.addend = addend;
        entry.computed_value = computed_value;
        relocations_.push_back(entry);
    }

    const std::vector<SectionMapEntry>& LinkMap::get_sections() const noexcept {
        return sections_;
    }

    const std::vector<SymbolMapEntry>& LinkMap::get_symbols() const noexcept {
        return symbols_;
    }

    const std::vector<RelocMapEntry>& LinkMap::get_relocations() const noexcept {
        return relocations_;
    }

    std::string LinkMap::generate_report() const {
        std::ostringstream ss;
        ss << "================================================================================\n";
        ss << "                      RQWos Flat Binary Link Map\n";
        ss << "================================================================================\n";
        ss << "Base Load Address: 0x" << std::hex << std::uppercase << std::setfill('0') << std::right << std::setw(16) << base_address_ << std::dec << "\n\n";

        ss << "--- Section Allocation ---\n";
        ss << std::left << std::setfill(' ') << std::setw(12) << "Name"
           << std::setw(20) << "Start Address"
           << std::setw(20) << "End Address"
           << std::setw(14) << "Size (hex)"
           << std::setw(12) << "Size (dec)"
           << "Align\n";
        ss << std::string(80, '-') << "\n";
        for (const auto& sec : sections_) {
            ss << std::left << std::setfill(' ') << std::setw(12) << sec.name
               << "0x" << std::hex << std::uppercase << std::setfill('0') << std::right << std::setw(16) << sec.start_address << "  "
               << "0x" << std::setw(16) << sec.end_address << "  "
               << "0x" << std::setw(8) << sec.size << "  "
               << std::dec << std::setfill(' ') << std::setw(10) << sec.size << "  "
               << std::setw(6) << sec.alignment << "\n";
        }

        ss << "\n--- Symbol Table ---\n";
        ss << std::left << std::setfill(' ') << std::setw(32) << "Symbol Name"
           << std::setw(20) << "Virtual Address"
           << std::setw(12) << "Section"
           << std::setw(10) << "Size"
           << "Binding\n";
        ss << std::string(80, '-') << "\n";
        for (const auto& sym : symbols_) {
            const char* sec_str = "Unknown";
            if (sym.section == SectionKind::Text) sec_str = ".text";
            else if (sym.section == SectionKind::RoData) sec_str = ".rodata";
            else if (sym.section == SectionKind::Data) sec_str = ".data";
            else if (sym.section == SectionKind::Bss) sec_str = ".bss";

            ss << std::left << std::setfill(' ') << std::setw(32) << sym.name
               << "0x" << std::hex << std::uppercase << std::setfill('0') << std::right << std::setw(16) << sym.address << "  "
               << std::left << std::setfill(' ') << std::setw(12) << sec_str
               << std::dec << std::setw(10) << sym.size
               << (sym.is_global ? "GLOBAL" : "LOCAL") << "\n";
        }

        ss << "\n--- Relocation Resolution Record ---\n";
        ss << std::left << std::setfill(' ') << std::setw(10) << "Section"
           << std::setw(10) << "Offset"
           << std::setw(20) << "Site Address"
           << std::setw(24) << "Target Symbol"
           << std::setw(10) << "Type"
           << std::setw(10) << "Addend"
           << "Value Resolved\n";
        ss << std::string(80, '-') << "\n";
        for (const auto& rel : relocations_) {
            const char* sec_str = (rel.section == SectionKind::Text ? ".text" : ".data");
            const char* type_str = "Unknown";
            if (rel.type == RelocType::Rel32) type_str = "Rel32";
            else if (rel.type == RelocType::Abs64) type_str = "Abs64";
            else if (rel.type == RelocType::Abs32) type_str = "Abs32";

            ss << std::left << std::setfill(' ') << std::setw(10) << sec_str
               << "0x" << std::hex << std::setfill('0') << std::right << std::setw(6) << rel.section_offset << "  "
               << "0x" << std::setw(16) << rel.site_address << "  "
               << std::left << std::setfill(' ') << std::setw(24) << rel.target_symbol
               << std::setw(10) << type_str
               << std::dec << std::setw(10) << rel.addend
               << "0x" << std::hex << std::uppercase << rel.computed_value << std::dec << "\n";
        }
        ss << "================================================================================\n";
        return ss.str();
    }

    std::string LinkMap::generate_link_map() const {
        return generate_report();
    }

    void LinkMap::clear() noexcept {
        base_address_ = 0;
        sections_.clear();
        symbols_.clear();
        relocations_.clear();
    }

    bool LinkMap::write_to_file(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;
        out << generate_report();
        return out.good();
    }

    // =========================================================================
    // 5. GrassObjWriter Implementation
    // =========================================================================

    uint16_t GrassObjWriter::coff_machine_for_arch(TargetArch arch) {
        return (arch == TargetArch::X86_64) ? COFF_MACHINE_AMD64 : COFF_MACHINE_I386;
    }

    uint16_t GrassObjWriter::coff_reloc_type(TargetArch arch, RelocType type) {
        if (arch == TargetArch::X86_64) {
            switch (type) {
                case RelocType::Rel32: return COFF_REL_AMD64_REL32;
                case RelocType::Abs64: return COFF_REL_AMD64_ADDR64;
                case RelocType::Abs32: return COFF_REL_AMD64_ADDR32;
            }
        } else {
            switch (type) {
                case RelocType::Rel32: return COFF_REL_I386_REL32;
                case RelocType::Abs32: return COFF_REL_I386_DIR32;
                case RelocType::Abs64: return COFF_REL_I386_DIR32;
            }
        }
        return 0;
    }

    uint16_t GrassObjWriter::elf_machine_for_arch(TargetArch arch) {
        return (arch == TargetArch::X86_64) ? ELF_EM_X86_64 : ELF_EM_386;
    }

    uint32_t GrassObjWriter::elf_reloc_type(TargetArch arch, RelocType type) {
        if (arch == TargetArch::X86_64) {
            switch (type) {
                case RelocType::Rel32: return ELF_R_X86_64_PC32;
                case RelocType::Abs64: return ELF_R_X86_64_64;
                case RelocType::Abs32: return ELF_R_X86_64_32;
            }
        } else {
            switch (type) {
                case RelocType::Rel32: return ELF_R_386_PC32;
                case RelocType::Abs32: return ELF_R_386_32;
                case RelocType::Abs64: return ELF_R_386_32;
            }
        }
        return 0;
    }

    bool GrassObjWriter::validate_compiled_unit(const CompiledUnit& unit, std::string* out_error) {
        for (const auto& sym : unit.symbols) {
            if (!sym.is_external) {
                if (sym.offset + sym.size < sym.offset) {
                    if (out_error) *out_error = "Symbol " + sym.name + " offset + size overflow";
                    return false;
                }
                if (sym.section == SectionKind::Text && sym.offset + sym.size > unit.text.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds text section bounds";
                    return false;
                }
                if (sym.section == SectionKind::RoData && sym.offset + sym.size > unit.rodata.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds rodata section bounds";
                    return false;
                }
                if (sym.section == SectionKind::Data && sym.offset + sym.size > unit.data.size()) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds data section bounds";
                    return false;
                }
                if (sym.section == SectionKind::Bss && sym.offset + sym.size > unit.bss_size) {
                    if (out_error) *out_error = "Symbol " + sym.name + " exceeds bss section bounds";
                    return false;
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Bss) {
                if (out_error) *out_error = "Relocations cannot be placed in BSS section";
                return false;
            }
            uint64_t reloc_width = (rel.type == RelocType::Abs64) ? 8 : 4;
            if (rel.offset + reloc_width < rel.offset) {
                if (out_error) *out_error = "Relocation offset overflow";
                return false;
            }
            if (rel.section == SectionKind::Text && rel.offset + reloc_width > unit.text.size()) {
                if (out_error) *out_error = "Relocation offset exceeds text section bounds";
                return false;
            }
            if (rel.section == SectionKind::RoData && rel.offset + reloc_width > unit.rodata.size()) {
                if (out_error) *out_error = "Relocation offset exceeds rodata section bounds";
                return false;
            }
            if (rel.section == SectionKind::Data && rel.offset + reloc_width > unit.data.size()) {
                if (out_error) *out_error = "Relocation offset exceeds data section bounds";
                return false;
            }
            if (unit.arch == TargetArch::X86_32 && rel.type == RelocType::Abs64) {
                if (out_error) *out_error = "Abs64 relocation requested for 32-bit architecture";
                return false;
            }
        }

        return true;
    }

    bool GrassObjWriter::write_coff_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error, bool include_directives) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct SectionData {
            SectionKind kind;
            std::string name;
            uint32_t characteristics = 0;
            std::vector<uint8_t> raw_bytes;
            std::vector<RelocEntry> relocs;
            int16_t section_number = 0;
            uint32_t ptr_raw_data = 0;
            uint32_t ptr_relocs = 0;
        };

        std::vector<SectionData> sections;
        int16_t next_sec_num = 1;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            SectionData sec;
            sec.kind = SectionKind::Text;
            sec.name = ".text";
            sec.characteristics = COFF_SCN_CNT_CODE | COFF_SCN_MEM_EXECUTE | COFF_SCN_MEM_READ | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.text;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (!unit.rodata.empty()) {
            SectionData sec;
            sec.kind = SectionKind::RoData;
            sec.name = ".rdata";
            sec.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.rodata;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (!unit.data.empty()) {
            SectionData sec;
            sec.kind = SectionKind::Data;
            sec.name = ".data";
            sec.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes = unit.data;
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        if (unit.bss_size > 0) {
            SectionData sec;
            sec.kind = SectionKind::Bss;
            sec.name = ".bss";
            sec.characteristics = COFF_SCN_CNT_UNINITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE | COFF_SCN_ALIGN_16BYTES;
            sec.raw_bytes.clear();
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        // Add .drectve section to automatically pull in system libraries when linking
        if (include_directives) {
            SectionData sec;
            sec.kind = SectionKind::RoData;
            sec.name = ".drectve";
            sec.characteristics = 0x00100A00; // IMAGE_SCN_LNK_INFO | IMAGE_SCN_LNK_REMOVE | ALIGN_1BYTE
            std::string drectve_str = " -defaultlib:kernel32.lib -defaultlib:ws2_32.lib -defaultlib:user32.lib ";
            sec.raw_bytes.assign(drectve_str.begin(), drectve_str.end());
            sec.section_number = next_sec_num++;
            sections.push_back(sec);
        }

        std::unordered_map<SectionKind, int16_t> sec_kind_to_num;
        for (const auto& sec : sections) {
            sec_kind_to_num[sec.kind] = sec.section_number;
        }

        for (const auto& rel : unit.relocations) {
            for (auto& sec : sections) {
                if (sec.kind == rel.section) {
                    sec.relocs.push_back(rel);
                    break;
                }
            }
        }

        for (auto& sec : sections) {
            for (const auto& rel : sec.relocs) {
                size_t off = static_cast<size_t>(rel.offset);
                if (rel.type == RelocType::Rel32) {
                    int32_t inline_addend = static_cast<int32_t>(rel.addend + 4);
                    if (inline_addend != 0 && off + 4 <= sec.raw_bytes.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 4);
                        curr += inline_addend;
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 4);
                    }
                } else if (rel.type == RelocType::Abs32) {
                    if (rel.addend != 0 && off + 4 <= sec.raw_bytes.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 4);
                    }
                } else if (rel.type == RelocType::Abs64) {
                    if (rel.addend != 0 && off + 8 <= sec.raw_bytes.size()) {
                        int64_t curr = 0;
                        std::memcpy(&curr, sec.raw_bytes.data() + off, 8);
                        curr += rel.addend;
                        std::memcpy(sec.raw_bytes.data() + off, &curr, 8);
                    }
                }
            }
        }

        StringTableBuilder coff_strtab;
        std::vector<COFF_SymbolRecord> symbols;
        std::unordered_map<std::string, uint32_t> sym_to_index;

        for (const auto& sec : sections) {
            COFF_SymbolRecord sym{};
            size_t copy_len = std::min<size_t>(sec.name.size(), 8);
            std::memcpy(sym.name.short_name, sec.name.data(), copy_len);
            sym.value = 0;
            sym.section_number = sec.section_number;
            sym.type = COFF_SYM_TYPE_NULL;
            sym.storage_class = COFF_SYM_CLASS_STATIC;
            sym.number_of_aux_symbols = 0;
            sym_to_index[sec.name] = static_cast<uint32_t>(symbols.size());
            symbols.push_back(sym);
        }

        for (const auto& s : unit.symbols) {
            COFF_SymbolRecord sym{};

            if (s.name.size() <= 8) {
                std::memcpy(sym.name.short_name, s.name.data(), s.name.size());
            } else {
                uint32_t str_off = coff_strtab.add_coff(s.name);
                sym.name.long_name.zeroes = 0;
                sym.name.long_name.offset = str_off;
            }

            if (s.is_external) {
                sym.value = 0;
                sym.section_number = 0;
                sym.storage_class = COFF_SYM_CLASS_EXTERNAL;
                sym.type = (s.section == SectionKind::Text) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
            } else {
                sym.value = static_cast<uint32_t>(s.offset);
                auto it = sec_kind_to_num.find(s.section);
                sym.section_number = (it != sec_kind_to_num.end()) ? it->second : 0;
                sym.storage_class = s.is_global ? COFF_SYM_CLASS_EXTERNAL : COFF_SYM_CLASS_STATIC;
                sym.type = (s.section == SectionKind::Text) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
            }

            sym.number_of_aux_symbols = 0;
            sym_to_index[s.name] = static_cast<uint32_t>(symbols.size());
            symbols.push_back(sym);
        }

        for (const auto& rel : unit.relocations) {
            if (sym_to_index.find(rel.symbol_name) == sym_to_index.end()) {
                COFF_SymbolRecord sym{};

                if (rel.symbol_name.size() <= 8) {
                    std::memcpy(sym.name.short_name, rel.symbol_name.data(), rel.symbol_name.size());
                } else {
                    uint32_t str_off = coff_strtab.add_coff(rel.symbol_name);
                    sym.name.long_name.zeroes = 0;
                    sym.name.long_name.offset = str_off;
                }

                sym.value = 0;
                sym.section_number = 0;
                sym.storage_class = COFF_SYM_CLASS_EXTERNAL;
                sym.type = (rel.type == RelocType::Rel32) ? COFF_SYM_TYPE_FUNC : COFF_SYM_TYPE_NULL;
                sym.number_of_aux_symbols = 0;
                sym_to_index[rel.symbol_name] = static_cast<uint32_t>(symbols.size());
                symbols.push_back(sym);
            }
        }

        uint32_t file_offset = static_cast<uint32_t>(sizeof(COFF_FileHeader) + sections.size() * sizeof(COFF_SectionHeader));

        for (auto& sec : sections) {
            if (!sec.raw_bytes.empty()) {
                uint32_t rem = file_offset % 4;
                if (rem != 0) file_offset += (4 - rem);
                sec.ptr_raw_data = file_offset;
                file_offset += static_cast<uint32_t>(sec.raw_bytes.size());
            } else {
                sec.ptr_raw_data = 0;
            }
        }

        for (auto& sec : sections) {
            if (!sec.relocs.empty()) {
                uint32_t rem = file_offset % 2;
                if (rem != 0) file_offset += (2 - rem);
                sec.ptr_relocs = file_offset;
                file_offset += static_cast<uint32_t>(sec.relocs.size() * sizeof(COFF_Relocation));
            } else {
                sec.ptr_relocs = 0;
            }
        }

        uint32_t rem_sym = file_offset % 4;
        if (rem_sym != 0) file_offset += (4 - rem_sym);
        uint32_t ptr_symbol_table = file_offset;

        out.clear();

        COFF_FileHeader file_hdr{};
        file_hdr.machine = coff_machine_for_arch(unit.arch);
        file_hdr.number_of_sections = static_cast<uint16_t>(sections.size());
        file_hdr.time_date_stamp = 0;
        file_hdr.pointer_to_symbol_table = ptr_symbol_table;
        file_hdr.number_of_symbols = static_cast<uint32_t>(symbols.size());
        file_hdr.size_of_optional_header = 0;
        file_hdr.characteristics = 0;
        out.emit_bytes(&file_hdr, sizeof(file_hdr));

        for (const auto& sec : sections) {
            COFF_SectionHeader shdr{};
            size_t copy_len = std::min<size_t>(sec.name.size(), 8);
            std::memcpy(shdr.name, sec.name.data(), copy_len);
            shdr.virtual_size = 0;
            shdr.virtual_address = 0;
            shdr.size_of_raw_data = static_cast<uint32_t>(sec.raw_bytes.size());
            shdr.pointer_to_raw_data = sec.ptr_raw_data;
            shdr.pointer_to_relocations = sec.ptr_relocs;
            shdr.pointer_to_linenumbers = 0;
            shdr.number_of_relocations = static_cast<uint16_t>(sec.relocs.size());
            shdr.number_of_linenumbers = 0;
            shdr.characteristics = sec.characteristics;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        for (const auto& sec : sections) {
            if (sec.ptr_raw_data > 0) {
                out.align_to(4, 0);
                out.emit_bytes(sec.raw_bytes.data(), sec.raw_bytes.size());
            }
        }

        for (const auto& sec : sections) {
            if (sec.ptr_relocs > 0) {
                out.align_to(2, 0);
                for (const auto& rel : sec.relocs) {
                    COFF_Relocation crel{};
                    crel.virtual_address = static_cast<uint32_t>(rel.offset);
                    crel.symbol_table_index = sym_to_index[rel.symbol_name];
                    crel.type = coff_reloc_type(unit.arch, rel.type);
                    out.emit_bytes(&crel, sizeof(crel));
                }
            }
        }

        out.align_to(4, 0);
        for (const auto& sym : symbols) {
            out.emit_bytes(&sym, sizeof(sym));
        }

        coff_strtab.build_coff(out);
        return true;
    }

    bool GrassObjWriter::write_coff_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error, bool include_directives) {
        BinaryBuffer buf;
        if (!write_coff_obj(unit, buf, out_error, include_directives)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write COFF object file to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_elf64_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct ElfSecDesc {
            std::string name;
            uint32_t type = 0;
            uint64_t flags = 0;
            uint64_t addr = 0;
            uint64_t offset = 0;
            uint64_t size = 0;
            uint32_t link = 0;
            uint32_t info = 0;
            uint64_t addralign = 1;
            uint64_t entsize = 0;
            std::vector<uint8_t> data;
            SectionKind sec_kind = SectionKind::Text;
            bool is_sec_kind = false;
        };

        std::vector<ElfSecDesc> sections;

        ElfSecDesc null_sec;
        null_sec.name = "";
        null_sec.type = ELF_SHT_NULL;
        sections.push_back(null_sec);

        uint32_t text_idx = 0;
        uint32_t rodata_idx = 0;
        uint32_t data_idx = 0;
        uint32_t bss_idx = 0;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            ElfSecDesc s;
            s.name = ".text";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_EXECINSTR;
            s.addralign = 16;
            s.data = unit.text;
            s.size = unit.text.size();
            s.sec_kind = SectionKind::Text;
            s.is_sec_kind = true;
            text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.rodata.empty()) {
            ElfSecDesc s;
            s.name = ".rodata";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC;
            s.addralign = 16;
            s.data = unit.rodata;
            s.size = unit.rodata.size();
            s.sec_kind = SectionKind::RoData;
            s.is_sec_kind = true;
            rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.data.empty()) {
            ElfSecDesc s;
            s.name = ".data";
            s.type = ELF_SHT_PROGBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_WRITE;
            s.addralign = 16;
            s.data = unit.data;
            s.size = unit.data.size();
            s.sec_kind = SectionKind::Data;
            s.is_sec_kind = true;
            data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (unit.bss_size > 0) {
            ElfSecDesc s;
            s.name = ".bss";
            s.type = ELF_SHT_NOBITS;
            s.flags = ELF_SHF_ALLOC | ELF_SHF_WRITE;
            s.addralign = 16;
            s.size = unit.bss_size;
            s.sec_kind = SectionKind::Bss;
            s.is_sec_kind = true;
            bss_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        std::vector<RelocEntry> text_relocs;
        std::vector<RelocEntry> rodata_relocs;
        std::vector<RelocEntry> data_relocs;
        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Text) {
                text_relocs.push_back(rel);
            } else if (rel.section == SectionKind::RoData) {
                rodata_relocs.push_back(rel);
            } else if (rel.section == SectionKind::Data) {
                data_relocs.push_back(rel);
            }
        }

        uint32_t rela_text_idx = 0;
        if (!text_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.text";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = text_idx;
            rela_text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rela_rodata_idx = 0;
        if (!rodata_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.rodata";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = rodata_idx;
            rela_rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rela_data_idx = 0;
        if (!data_relocs.empty()) {
            ElfSecDesc s;
            s.name = ".rela.data";
            s.type = ELF_SHT_RELA;
            s.flags = ELF_SHF_INFO_LINK;
            s.addralign = 8;
            s.entsize = sizeof(Elf64_Rela);
            s.info = data_idx;
            rela_data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        ElfSecDesc symtab_sec;
        symtab_sec.name = ".symtab";
        symtab_sec.type = ELF_SHT_SYMTAB;
        symtab_sec.flags = 0;
        symtab_sec.addralign = 8;
        symtab_sec.entsize = sizeof(Elf64_Sym);
        uint32_t symtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(symtab_sec);

        ElfSecDesc strtab_sec;
        strtab_sec.name = ".strtab";
        strtab_sec.type = ELF_SHT_STRTAB;
        strtab_sec.flags = 0;
        strtab_sec.addralign = 1;
        uint32_t strtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(strtab_sec);

        ElfSecDesc shstrtab_sec;
        shstrtab_sec.name = ".shstrtab";
        shstrtab_sec.type = ELF_SHT_STRTAB;
        shstrtab_sec.flags = 0;
        shstrtab_sec.addralign = 1;
        uint32_t shstrtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(shstrtab_sec);

        sections[symtab_idx].link = strtab_idx;
        if (rela_text_idx > 0) sections[rela_text_idx].link = symtab_idx;
        if (rela_rodata_idx > 0) sections[rela_rodata_idx].link = symtab_idx;
        if (rela_data_idx > 0) sections[rela_data_idx].link = symtab_idx;

        StringTableBuilder strtab;
        std::vector<Elf64_Sym> local_symbols;
        std::vector<Elf64_Sym> global_symbols;
        std::unordered_map<std::string, uint32_t> sym_index_map;

        Elf64_Sym sym0{};
        local_symbols.push_back(sym0);

        Elf64_Sym sym_file{};
        sym_file.st_name = strtab.add_elf("grass_source");
        sym_file.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_FILE;
        sym_file.st_shndx = ELF_SHN_ABS;
        local_symbols.push_back(sym_file);

        if (text_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(text_idx);
            sym_index_map[".text"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (rodata_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(rodata_idx);
            sym_index_map[".rodata"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (data_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(data_idx);
            sym_index_map[".data"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (bss_idx > 0) {
            Elf64_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(bss_idx);
            sym_index_map[".bss"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }

        auto get_section_shndx = [&](SectionKind k) -> uint16_t {
            if (k == SectionKind::Text) return static_cast<uint16_t>(text_idx);
            if (k == SectionKind::RoData) return static_cast<uint16_t>(rodata_idx);
            if (k == SectionKind::Data) return static_cast<uint16_t>(data_idx);
            if (k == SectionKind::Bss) return static_cast<uint16_t>(bss_idx);
            return 0;
        };

        for (const auto& s : unit.symbols) {
            if (!s.is_global && !s.is_external) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_info = (ELF_STB_LOCAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym.st_value = s.offset;
                sym.st_size = s.size;
                sym_index_map[s.name] = static_cast<uint32_t>(local_symbols.size());
                local_symbols.push_back(sym);
            }
        }

        uint32_t first_global_idx = static_cast<uint32_t>(local_symbols.size());
        sections[symtab_idx].info = first_global_idx;

        for (const auto& s : unit.symbols) {
            if (s.is_global && !s.is_external) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym.st_value = s.offset;
                sym.st_size = s.size;
                sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        for (const auto& s : unit.symbols) {
            if (s.is_external) {
                if (sym_index_map.find(s.name) == sym_index_map.end()) {
                    Elf64_Sym sym{};
                    sym.st_name = strtab.add_elf(s.name);
                    sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                    sym.st_shndx = 0;
                    sym.st_value = 0;
                    sym.st_size = 0;
                    sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                    global_symbols.push_back(sym);
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (sym_index_map.find(rel.symbol_name) == sym_index_map.end()) {
                Elf64_Sym sym{};
                sym.st_name = strtab.add_elf(rel.symbol_name);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((rel.type == RelocType::Rel32) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                sym.st_shndx = 0;
                sym.st_value = 0;
                sym.st_size = 0;
                sym_index_map[rel.symbol_name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        std::vector<Elf64_Sym> all_symbols = local_symbols;
        all_symbols.insert(all_symbols.end(), global_symbols.begin(), global_symbols.end());

        BinaryBuffer symtab_buf;
        for (const auto& s : all_symbols) {
            symtab_buf.emit_bytes(&s, sizeof(s));
        }
        sections[symtab_idx].data = symtab_buf.get_bytes();
        sections[symtab_idx].size = symtab_buf.size();

        BinaryBuffer strtab_buf;
        strtab.build_elf(strtab_buf);
        sections[strtab_idx].data = strtab_buf.get_bytes();
        sections[strtab_idx].size = strtab_buf.size();

        if (rela_text_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : text_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_text_idx].data = rela_buf.get_bytes();
            sections[rela_text_idx].size = rela_buf.size();
        }

        if (rela_rodata_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : rodata_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_rodata_idx].data = rela_buf.get_bytes();
            sections[rela_rodata_idx].size = rela_buf.size();
        }

        if (rela_data_idx > 0) {
            BinaryBuffer rela_buf;
            for (const auto& rel : data_relocs) {
                Elf64_Rela r{};
                r.r_offset = rel.offset;
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_64, rel.type);
                r.r_info = (static_cast<uint64_t>(sym_id) << 32) | (static_cast<uint64_t>(r_type) & 0xFFFFFFFFULL);
                r.r_addend = rel.addend;
                rela_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rela_data_idx].data = rela_buf.get_bytes();
            sections[rela_data_idx].size = rela_buf.size();
        }

        StringTableBuilder shstrtab;
        for (const auto& sec : sections) {
            shstrtab.add_elf(sec.name);
        }
        BinaryBuffer shstr_buf;
        shstrtab.build_elf(shstr_buf);
        sections[shstrtab_idx].data = shstr_buf.get_bytes();
        sections[shstrtab_idx].size = shstr_buf.size();

        out.clear();

        Elf64_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELF_CLASS_64;
        ehdr.e_ident[5] = ELF_DATA_2LSB;
        ehdr.e_ident[6] = ELF_EV_CURRENT;
        ehdr.e_ident[7] = ELF_OSABI_SYSV;
        ehdr.e_type = ELF_ET_REL;
        ehdr.e_machine = ELF_EM_X86_64;
        ehdr.e_version = ELF_EV_CURRENT;
        ehdr.e_shoff = 0; // patched below
        ehdr.e_ehsize = sizeof(Elf64_Ehdr);
        ehdr.e_shentsize = sizeof(Elf64_Shdr);
        ehdr.e_shnum = static_cast<uint16_t>(sections.size());
        ehdr.e_shstrndx = static_cast<uint16_t>(shstrtab_idx);
        out.emit_bytes(&ehdr, sizeof(ehdr));

        for (size_t i = 1; i < sections.size(); ++i) {
            auto& sec = sections[i];
            if (sec.type == ELF_SHT_NOBITS) {
                sec.offset = out.size();
            } else if (sec.size > 0) {
                out.align_to(sec.addralign > 0 ? static_cast<size_t>(sec.addralign) : 1, 0);
                sec.offset = out.size();
                out.emit_bytes(sec.data.data(), sec.data.size());
            } else {
                sec.offset = out.size();
            }
        }

        out.align_to(8, 0);
        uint64_t shoff = out.size();
        out.patch_u64(40, shoff);

        for (const auto& sec : sections) {
            Elf64_Shdr shdr{};
            shdr.sh_name = shstrtab.get_offset(sec.name);
            shdr.sh_type = sec.type;
            shdr.sh_flags = sec.flags;
            shdr.sh_addr = sec.addr;
            shdr.sh_offset = sec.offset;
            shdr.sh_size = sec.size;
            shdr.sh_link = sec.link;
            shdr.sh_info = sec.info;
            shdr.sh_addralign = sec.addralign;
            shdr.sh_entsize = sec.entsize;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        return true;
    }

    bool GrassObjWriter::write_elf32_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        struct ElfSecDesc32 {
            std::string name;
            uint32_t type = 0;
            uint32_t flags = 0;
            uint32_t addr = 0;
            uint32_t offset = 0;
            uint32_t size = 0;
            uint32_t link = 0;
            uint32_t info = 0;
            uint32_t addralign = 1;
            uint32_t entsize = 0;
            std::vector<uint8_t> data;
            SectionKind sec_kind = SectionKind::Text;
            bool is_sec_kind = false;
        };

        std::vector<ElfSecDesc32> sections;

        ElfSecDesc32 null_sec;
        null_sec.name = "";
        null_sec.type = ELF_SHT_NULL;
        sections.push_back(null_sec);

        uint32_t text_idx = 0;
        uint32_t rodata_idx = 0;
        uint32_t data_idx = 0;
        uint32_t bss_idx = 0;

        if (!unit.text.empty() || (unit.rodata.empty() && unit.data.empty() && unit.bss_size == 0)) {
            ElfSecDesc32 s;
            s.name = ".text";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_EXECINSTR);
            s.addralign = 16;
            s.data = unit.text;
            s.size = static_cast<uint32_t>(unit.text.size());
            s.sec_kind = SectionKind::Text;
            s.is_sec_kind = true;
            text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.rodata.empty()) {
            ElfSecDesc32 s;
            s.name = ".rodata";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC);
            s.addralign = 16;
            s.data = unit.rodata;
            s.size = static_cast<uint32_t>(unit.rodata.size());
            s.sec_kind = SectionKind::RoData;
            s.is_sec_kind = true;
            rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (!unit.data.empty()) {
            ElfSecDesc32 s;
            s.name = ".data";
            s.type = ELF_SHT_PROGBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_WRITE);
            s.addralign = 16;
            s.data = unit.data;
            s.size = static_cast<uint32_t>(unit.data.size());
            s.sec_kind = SectionKind::Data;
            s.is_sec_kind = true;
            data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        if (unit.bss_size > 0) {
            ElfSecDesc32 s;
            s.name = ".bss";
            s.type = ELF_SHT_NOBITS;
            s.flags = static_cast<uint32_t>(ELF_SHF_ALLOC | ELF_SHF_WRITE);
            s.addralign = 16;
            s.size = static_cast<uint32_t>(unit.bss_size);
            s.sec_kind = SectionKind::Bss;
            s.is_sec_kind = true;
            bss_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        std::vector<RelocEntry> text_relocs;
        std::vector<RelocEntry> rodata_relocs;
        std::vector<RelocEntry> data_relocs;
        for (const auto& rel : unit.relocations) {
            if (rel.section == SectionKind::Text) {
                text_relocs.push_back(rel);
            } else if (rel.section == SectionKind::RoData) {
                rodata_relocs.push_back(rel);
            } else if (rel.section == SectionKind::Data) {
                data_relocs.push_back(rel);
            }
        }

        if (text_idx > 0) {
            for (const auto& rel : text_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[text_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[text_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[text_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }
        if (rodata_idx > 0) {
            for (const auto& rel : rodata_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[rodata_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[rodata_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[rodata_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }
        if (data_idx > 0) {
            for (const auto& rel : data_relocs) {
                if (rel.addend != 0) {
                    size_t off = static_cast<size_t>(rel.offset);
                    if (off + 4 <= sections[data_idx].data.size()) {
                        int32_t curr = 0;
                        std::memcpy(&curr, sections[data_idx].data.data() + off, 4);
                        curr += static_cast<int32_t>(rel.addend);
                        std::memcpy(sections[data_idx].data.data() + off, &curr, 4);
                    }
                }
            }
        }

        uint32_t rel_text_idx = 0;
        if (!text_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.text";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = text_idx;
            rel_text_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rel_rodata_idx = 0;
        if (!rodata_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.rodata";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = rodata_idx;
            rel_rodata_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        uint32_t rel_data_idx = 0;
        if (!data_relocs.empty()) {
            ElfSecDesc32 s;
            s.name = ".rel.data";
            s.type = ELF_SHT_REL;
            s.flags = static_cast<uint32_t>(ELF_SHF_INFO_LINK);
            s.addralign = 4;
            s.entsize = sizeof(Elf32_Rel);
            s.info = data_idx;
            rel_data_idx = static_cast<uint32_t>(sections.size());
            sections.push_back(s);
        }

        ElfSecDesc32 symtab_sec;
        symtab_sec.name = ".symtab";
        symtab_sec.type = ELF_SHT_SYMTAB;
        symtab_sec.flags = 0;
        symtab_sec.addralign = 4;
        symtab_sec.entsize = sizeof(Elf32_Sym);
        uint32_t symtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(symtab_sec);

        ElfSecDesc32 strtab_sec;
        strtab_sec.name = ".strtab";
        strtab_sec.type = ELF_SHT_STRTAB;
        strtab_sec.flags = 0;
        strtab_sec.addralign = 1;
        uint32_t strtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(strtab_sec);

        ElfSecDesc32 shstrtab_sec;
        shstrtab_sec.name = ".shstrtab";
        shstrtab_sec.type = ELF_SHT_STRTAB;
        shstrtab_sec.flags = 0;
        shstrtab_sec.addralign = 1;
        uint32_t shstrtab_idx = static_cast<uint32_t>(sections.size());
        sections.push_back(shstrtab_sec);

        sections[symtab_idx].link = strtab_idx;
        if (rel_text_idx > 0) sections[rel_text_idx].link = symtab_idx;
        if (rel_rodata_idx > 0) sections[rel_rodata_idx].link = symtab_idx;
        if (rel_data_idx > 0) sections[rel_data_idx].link = symtab_idx;

        StringTableBuilder strtab;
        std::vector<Elf32_Sym> local_symbols;
        std::vector<Elf32_Sym> global_symbols;
        std::unordered_map<std::string, uint32_t> sym_index_map;

        Elf32_Sym sym0{};
        local_symbols.push_back(sym0);

        Elf32_Sym sym_file{};
        sym_file.st_name = strtab.add_elf("grass_source");
        sym_file.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_FILE;
        sym_file.st_shndx = ELF_SHN_ABS;
        local_symbols.push_back(sym_file);

        if (text_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(text_idx);
            sym_index_map[".text"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (rodata_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(rodata_idx);
            sym_index_map[".rodata"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (data_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(data_idx);
            sym_index_map[".data"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }
        if (bss_idx > 0) {
            Elf32_Sym s{};
            s.st_info = (ELF_STB_LOCAL << 4) | ELF_STT_SECTION;
            s.st_shndx = static_cast<uint16_t>(bss_idx);
            sym_index_map[".bss"] = static_cast<uint32_t>(local_symbols.size());
            local_symbols.push_back(s);
        }

        auto get_section_shndx = [&](SectionKind k) -> uint16_t {
            if (k == SectionKind::Text) return static_cast<uint16_t>(text_idx);
            if (k == SectionKind::RoData) return static_cast<uint16_t>(rodata_idx);
            if (k == SectionKind::Data) return static_cast<uint16_t>(data_idx);
            if (k == SectionKind::Bss) return static_cast<uint16_t>(bss_idx);
            return 0;
        };

        for (const auto& s : unit.symbols) {
            if (!s.is_global && !s.is_external) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_value = static_cast<uint32_t>(s.offset);
                sym.st_size = static_cast<uint32_t>(s.size);
                sym.st_info = (ELF_STB_LOCAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym_index_map[s.name] = static_cast<uint32_t>(local_symbols.size());
                local_symbols.push_back(sym);
            }
        }

        uint32_t first_global_idx = static_cast<uint32_t>(local_symbols.size());
        sections[symtab_idx].info = first_global_idx;

        for (const auto& s : unit.symbols) {
            if (s.is_global && !s.is_external) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(s.name);
                sym.st_value = static_cast<uint32_t>(s.offset);
                sym.st_size = static_cast<uint32_t>(s.size);
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_OBJECT);
                sym.st_shndx = get_section_shndx(s.section);
                sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        for (const auto& s : unit.symbols) {
            if (s.is_external) {
                if (sym_index_map.find(s.name) == sym_index_map.end()) {
                    Elf32_Sym sym{};
                    sym.st_name = strtab.add_elf(s.name);
                    sym.st_value = 0;
                    sym.st_size = 0;
                    sym.st_info = (ELF_STB_GLOBAL << 4) | ((s.section == SectionKind::Text) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                    sym.st_shndx = 0;
                    sym_index_map[s.name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                    global_symbols.push_back(sym);
                }
            }
        }

        for (const auto& rel : unit.relocations) {
            if (sym_index_map.find(rel.symbol_name) == sym_index_map.end()) {
                Elf32_Sym sym{};
                sym.st_name = strtab.add_elf(rel.symbol_name);
                sym.st_value = 0;
                sym.st_size = 0;
                sym.st_info = (ELF_STB_GLOBAL << 4) | ((rel.type == RelocType::Rel32) ? ELF_STT_FUNC : ELF_STT_NOTYPE);
                sym.st_shndx = 0;
                sym_index_map[rel.symbol_name] = first_global_idx + static_cast<uint32_t>(global_symbols.size());
                global_symbols.push_back(sym);
            }
        }

        std::vector<Elf32_Sym> all_symbols = local_symbols;
        all_symbols.insert(all_symbols.end(), global_symbols.begin(), global_symbols.end());

        BinaryBuffer symtab_buf;
        for (const auto& s : all_symbols) {
            symtab_buf.emit_bytes(&s, sizeof(s));
        }
        sections[symtab_idx].data = symtab_buf.get_bytes();
        sections[symtab_idx].size = static_cast<uint32_t>(symtab_buf.size());

        BinaryBuffer strtab_buf;
        strtab.build_elf(strtab_buf);
        sections[strtab_idx].data = strtab_buf.get_bytes();
        sections[strtab_idx].size = static_cast<uint32_t>(strtab_buf.size());

        if (rel_text_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : text_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_text_idx].data = rel_buf.get_bytes();
            sections[rel_text_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        if (rel_rodata_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : rodata_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_rodata_idx].data = rel_buf.get_bytes();
            sections[rel_rodata_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        if (rel_data_idx > 0) {
            BinaryBuffer rel_buf;
            for (const auto& rel : data_relocs) {
                Elf32_Rel r{};
                r.r_offset = static_cast<uint32_t>(rel.offset);
                uint32_t sym_id = sym_index_map[rel.symbol_name];
                uint32_t r_type = elf_reloc_type(TargetArch::X86_32, rel.type);
                r.r_info = (sym_id << 8) | (r_type & 0xFF);
                rel_buf.emit_bytes(&r, sizeof(r));
            }
            sections[rel_data_idx].data = rel_buf.get_bytes();
            sections[rel_data_idx].size = static_cast<uint32_t>(rel_buf.size());
        }

        StringTableBuilder shstrtab;
        for (const auto& sec : sections) {
            shstrtab.add_elf(sec.name);
        }
        BinaryBuffer shstr_buf;
        shstrtab.build_elf(shstr_buf);
        sections[shstrtab_idx].data = shstr_buf.get_bytes();
        sections[shstrtab_idx].size = static_cast<uint32_t>(shstr_buf.size());

        out.clear();

        Elf32_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7F;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = ELF_CLASS_32;
        ehdr.e_ident[5] = ELF_DATA_2LSB;
        ehdr.e_ident[6] = ELF_EV_CURRENT;
        ehdr.e_ident[7] = ELF_OSABI_SYSV;
        ehdr.e_type = ELF_ET_REL;
        ehdr.e_machine = ELF_EM_386;
        ehdr.e_version = ELF_EV_CURRENT;
        ehdr.e_shoff = 0; // patched below
        ehdr.e_ehsize = sizeof(Elf32_Ehdr);
        ehdr.e_shentsize = sizeof(Elf32_Shdr);
        ehdr.e_shnum = static_cast<uint16_t>(sections.size());
        ehdr.e_shstrndx = static_cast<uint16_t>(shstrtab_idx);
        out.emit_bytes(&ehdr, sizeof(ehdr));

        for (size_t i = 1; i < sections.size(); ++i) {
            auto& sec = sections[i];
            if (sec.type == ELF_SHT_NOBITS) {
                sec.offset = static_cast<uint32_t>(out.size());
            } else if (sec.size > 0) {
                out.align_to(sec.addralign > 0 ? static_cast<size_t>(sec.addralign) : 1, 0);
                sec.offset = static_cast<uint32_t>(out.size());
                out.emit_bytes(sec.data.data(), sec.data.size());
            } else {
                sec.offset = static_cast<uint32_t>(out.size());
            }
        }

        out.align_to(4, 0);
        uint32_t shoff = static_cast<uint32_t>(out.size());
        out.patch_u32(32, shoff);

        for (const auto& sec : sections) {
            Elf32_Shdr shdr{};
            shdr.sh_name = shstrtab.get_offset(sec.name);
            shdr.sh_type = sec.type;
            shdr.sh_flags = sec.flags;
            shdr.sh_addr = sec.addr;
            shdr.sh_offset = sec.offset;
            shdr.sh_size = sec.size;
            shdr.sh_link = sec.link;
            shdr.sh_info = sec.info;
            shdr.sh_addralign = sec.addralign;
            shdr.sh_entsize = sec.entsize;
            out.emit_bytes(&shdr, sizeof(shdr));
        }

        return true;
    }

    bool GrassObjWriter::write_elf_obj(const CompiledUnit& unit, BinaryBuffer& out, std::string* out_error) {
        if (unit.arch == TargetArch::X86_64) {
            return write_elf64_obj(unit, out, out_error);
        } else {
            return write_elf32_obj(unit, out, out_error);
        }
    }

    bool GrassObjWriter::write_elf_obj_file(const CompiledUnit& unit, const std::string& filename, std::string* out_error) {
        BinaryBuffer buf;
        if (!write_elf_obj(unit, buf, out_error)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write ELF object file to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_flat_binary(const CompiledUnit& unit, uint64_t base_address, BinaryBuffer& out, LinkMap* out_map, std::string* out_error) {
        if (!validate_compiled_unit(unit, out_error)) {
            return false;
        }

        uint64_t text_addr = base_address;
        uint64_t text_size = unit.text.size();
        uint64_t text_end = text_addr + text_size;

        uint64_t rodata_addr = (text_size > 0) ? ((text_end + 15) / 16 * 16) : text_addr;
        uint64_t rodata_size = unit.rodata.size();
        uint64_t rodata_end = rodata_addr + rodata_size;

        uint64_t data_addr = (rodata_size > 0) ? ((rodata_end + 15) / 16 * 16) : rodata_end;
        uint64_t data_size = unit.data.size();
        uint64_t data_end = data_addr + data_size;

        uint64_t bss_addr = (data_size > 0) ? ((data_end + 15) / 16 * 16) : data_end;
        uint64_t bss_size = unit.bss_size;

        if (out_map) {
            out_map->clear();
            out_map->set_base_address(base_address);
            if (text_size > 0) out_map->add_section_entry(".text", text_addr, text_size, 16);
            if (rodata_size > 0) out_map->add_section_entry(".rodata", rodata_addr, rodata_size, 16);
            if (data_size > 0) out_map->add_section_entry(".data", data_addr, data_size, 16);
            if (bss_size > 0) out_map->add_section_entry(".bss", bss_addr, bss_size, 16);
        }

        std::unordered_map<std::string, uint64_t> sym_addrs;
        for (const auto& s : unit.symbols) {
            if (s.is_external) continue;
            uint64_t addr = 0;
            if (s.section == SectionKind::Text) addr = text_addr + s.offset;
            else if (s.section == SectionKind::RoData) addr = rodata_addr + s.offset;
            else if (s.section == SectionKind::Data) addr = data_addr + s.offset;
            else if (s.section == SectionKind::Bss) addr = bss_addr + s.offset;

            sym_addrs[s.name] = addr;
            if (out_map) {
                out_map->add_symbol_entry(s.name, addr, s.size, s.section, s.is_global);
            }
        }

        out.clear();
        out.emit_bytes(unit.text.data(), unit.text.size());

        if (rodata_size > 0) {
            size_t target_off = static_cast<size_t>(rodata_addr - base_address);
            if (out.size() < target_off) {
                out.emit_zeros(target_off - out.size());
            }
            out.emit_bytes(unit.rodata.data(), unit.rodata.size());
        }

        if (data_size > 0) {
            size_t target_off = static_cast<size_t>(data_addr - base_address);
            if (out.size() < target_off) {
                out.emit_zeros(target_off - out.size());
            }
            out.emit_bytes(unit.data.data(), unit.data.size());
        }

        for (const auto& rel : unit.relocations) {
            uint64_t sec_base = 0;
            size_t sec_buf_off = 0;
            if (rel.section == SectionKind::Text) {
                sec_base = text_addr;
                sec_buf_off = 0;
            } else if (rel.section == SectionKind::RoData) {
                sec_base = rodata_addr;
                sec_buf_off = static_cast<size_t>(rodata_addr - base_address);
            } else if (rel.section == SectionKind::Data) {
                sec_base = data_addr;
                sec_buf_off = static_cast<size_t>(data_addr - base_address);
            } else {
                if (out_error) *out_error = "Relocation in unsupported section for flat binary";
                return false;
            }

            uint64_t site_addr = sec_base + rel.offset;
            size_t patch_off = sec_buf_off + static_cast<size_t>(rel.offset);

            auto it = sym_addrs.find(rel.symbol_name);
            if (it == sym_addrs.end()) {
                if (out_error) *out_error = "Unresolved symbol in flat binary: " + rel.symbol_name;
                return false;
            }
            uint64_t sym_addr = it->second;
            int64_t computed = 0;

            if (rel.type == RelocType::Rel32) {
                int64_t disp = static_cast<int64_t>(sym_addr + rel.addend) - static_cast<int64_t>(site_addr);
                computed = disp;
                out.patch_i32(patch_off, static_cast<int32_t>(disp));
            } else if (rel.type == RelocType::Abs64) {
                uint64_t abs_val = sym_addr + rel.addend;
                computed = static_cast<int64_t>(abs_val);
                out.patch_u64(patch_off, abs_val);
            } else if (rel.type == RelocType::Abs32) {
                uint32_t abs_val = static_cast<uint32_t>(sym_addr + rel.addend);
                computed = static_cast<int64_t>(abs_val);
                out.patch_u32(patch_off, abs_val);
            }

            if (out_map) {
                out_map->add_reloc_entry(rel.section, rel.offset, site_addr, rel.symbol_name, rel.type, rel.addend, computed);
            }
        }

        return true;
    }

    bool GrassObjWriter::write_flat_binary_file(const CompiledUnit& unit, uint64_t base_address, const std::string& filename, const std::string& map_filename, std::string* out_error) {
        BinaryBuffer buf;
        LinkMap lmap;
        if (!write_flat_binary(unit, base_address, buf, &lmap, out_error)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write flat binary to " + filename;
            return false;
        }
        if (!map_filename.empty()) {
            if (!lmap.write_to_file(map_filename)) {
                if (out_error) *out_error = "Failed to write link map to " + map_filename;
                return false;
            }
        }
        return true;
    }

    std::string GrassObjWriter::generate_link_map(const LinkMap& map) {
        return map.generate_report();
    }

    std::vector<WinLibraryImport> GrassObjWriter::get_default_windows_imports() {
        return {
            {
                "KERNEL32.dll",
                {
                    "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
                    "HeapAlloc", "HeapFree", "GetProcessHeap",
                    "QueryPerformanceCounter", "QueryPerformanceFrequency",
                    "Sleep", "LoadLibraryA", "GetProcAddress",
                    "GetLocalTime", "VirtualAlloc", "VirtualFree",
                    "GetLastError", "CreateFileA", "CloseHandle"
                }
            },
            {
                "USER32.dll",
                {
                    "MessageBoxA"
                }
            },
            {
                "WS2_32.dll",
                {
                    "WSAStartup", "WSACleanup", "socket", "connect", "send", "recv", "closesocket"
                }
            }
        };
    }

    bool GrassObjWriter::generate_import_table(const std::vector<WinLibraryImport>& imports,
                                              uint32_t idata_rva,
                                              BinaryBuffer& out_idata,
                                              std::unordered_map<std::string, uint32_t>& out_iat_offsets,
                                              std::string* out_error,
                                              uint32_t* out_iat_rva,
                                              uint32_t* out_iat_size) {
        (void)out_error;
        if (imports.empty()) {
            out_idata.clear();
            out_iat_offsets.clear();
            if (out_iat_rva) *out_iat_rva = 0;
            if (out_iat_size) *out_iat_size = 0;
            return true;
        }

        out_idata.clear();
        out_iat_offsets.clear();

        size_t num_dlls = imports.size();
        size_t dir_table_size = (num_dlls + 1) * 20;
        size_t current_offset = dir_table_size;

        std::vector<size_t> ilt_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            ilt_offsets[i] = current_offset;
            current_offset += (imports[i].symbols.size() + 1) * 8;
        }

        std::vector<size_t> iat_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            iat_offsets[i] = current_offset;
            current_offset += (imports[i].symbols.size() + 1) * 8;
        }

        size_t iat_start_off = iat_offsets.empty() ? 0 : iat_offsets[0];
        size_t iat_total_bytes = current_offset - iat_start_off;

        std::vector<size_t> name_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            name_offsets[i] = current_offset;
            current_offset += imports[i].dll_name.size() + 1;
        }

        if (current_offset % 2 != 0) current_offset++;

        std::vector<std::vector<size_t>> hint_name_offsets(num_dlls);
        for (size_t i = 0; i < num_dlls; ++i) {
            hint_name_offsets[i].resize(imports[i].symbols.size());
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                hint_name_offsets[i][s] = current_offset;
                current_offset += 2 + imports[i].symbols[s].size() + 1;
                if (current_offset % 2 != 0) current_offset++;
            }
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            uint32_t ilt_rva = static_cast<uint32_t>(idata_rva + ilt_offsets[i]);
            uint32_t name_rva = static_cast<uint32_t>(idata_rva + name_offsets[i]);
            uint32_t iat_rva = static_cast<uint32_t>(idata_rva + iat_offsets[i]);

            out_idata.emit_u32(ilt_rva);
            out_idata.emit_u32(0);
            out_idata.emit_u32(0);
            out_idata.emit_u32(name_rva);
            out_idata.emit_u32(iat_rva);
        }
        out_idata.emit_zeros(20);

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                uint64_t hint_name_rva = idata_rva + hint_name_offsets[i][s];
                out_idata.emit_u64(hint_name_rva);
            }
            out_idata.emit_u64(0);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                size_t entry_offset = out_idata.size();
                out_iat_offsets[imports[i].symbols[s]] = static_cast<uint32_t>(idata_rva + entry_offset);
                out_iat_offsets["__imp_" + imports[i].symbols[s]] = static_cast<uint32_t>(idata_rva + entry_offset);
                uint64_t hint_name_rva = idata_rva + hint_name_offsets[i][s];
                out_idata.emit_u64(hint_name_rva);
            }
            out_idata.emit_u64(0);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            out_idata.emit_cstring(imports[i].dll_name);
        }

        for (size_t i = 0; i < num_dlls; ++i) {
            for (size_t s = 0; s < imports[i].symbols.size(); ++s) {
                if (out_idata.size() % 2 != 0) out_idata.emit_u8(0);
                out_idata.emit_u16(static_cast<uint16_t>(s));
                out_idata.emit_cstring(imports[i].symbols[s]);
            }
        }

        if (out_iat_rva) *out_iat_rva = static_cast<uint32_t>(idata_rva + iat_start_off);
        if (out_iat_size) *out_iat_size = static_cast<uint32_t>(iat_total_bytes);

        return true;
    }

    static std::string detect_dll_for_symbol(const std::string& raw_name) {
        std::string s = raw_name;
        if (s.rfind("__imp_", 0) == 0) {
            s = s.substr(6);
        }

        static const std::unordered_set<std::string> k32_syms = {
            "ExitProcess", "GetStdHandle", "WriteFile", "ReadFile",
            "HeapAlloc", "HeapFree", "GetProcessHeap", "HeapReAlloc",
            "QueryPerformanceCounter", "QueryPerformanceFrequency",
            "GetTickCount64", "Sleep", "LoadLibraryA", "GetProcAddress",
            "FreeLibrary", "GetLocalTime", "GetSystemTime",
            "VirtualAlloc", "VirtualFree",
            "GetLastError", "SetLastError", "CreateFileA", "CloseHandle",
            "GetCommandLineA", "GetModuleHandleA", "RtlZeroMemory",
            "GetEnvironmentVariableA", "SetEnvironmentVariableA",
            "GetComputerNameA", "CreateProcessA", "WinExec",
            "CreateDirectoryA", "GetFileAttributesA", "CopyFileA",
            "CreateThread", "WaitForSingleObject", "CreateMutexA", "ReleaseMutex",
            "TlsAlloc", "TlsGetValue", "TlsSetValue", "TlsFree",
            "GetProcessAffinityMask", "SetProcessAffinityMask",
            "AddVectoredExceptionHandler", "RemoveVectoredExceptionHandler",
            "RtlCaptureContext", "RtlLookupFunctionEntry", "RtlVirtualUnwind",
            "RaiseException", "lstrlenA", "lstrlenW", "lstrcpyA", "lstrcatA",
            "lstrcmpA", "lstrcmpiA", "FormatMessageA",
            "GetFileSizeEx", "DeleteFileA"
        };
        if (k32_syms.find(s) != k32_syms.end()) return "KERNEL32.dll";

        static const std::unordered_set<std::string> u32_syms = {
            "MessageBoxA", "MessageBoxW", "GetDesktopWindow", "GetSystemMetrics",
            "OpenClipboard", "CloseClipboard", "GetClipboardData", "SetClipboardData",
            "EmptyClipboard", "GetAsyncKeyState"
        };
        if (u32_syms.find(s) != u32_syms.end()) return "USER32.dll";

        static const std::unordered_set<std::string> ws2_syms = {
            "WSAStartup", "WSACleanup", "socket", "connect", "send", "recv", "closesocket",
            "bind", "listen", "accept", "htons", "ntohs", "inet_addr", "gethostbyname",
            "WSAGetLastError"
        };
        if (ws2_syms.find(s) != ws2_syms.end()) return "WS2_32.dll";

        static const std::unordered_set<std::string> advapi_syms = {
            "GetUserNameA", "RegOpenKeyExA", "RegCloseKey", "RegQueryValueExA"
        };
        if (advapi_syms.find(s) != advapi_syms.end()) return "ADVAPI32.dll";

        static const std::unordered_set<std::string> crt_syms = {
            "printf", "puts", "putchar", "scanf", "getchar",
            "malloc", "free", "calloc", "realloc",
            "exit", "_exit", "abort",
            "memset", "memcpy", "memmove", "memcmp",
            "strlen", "strcmp", "strncmp", "strcpy", "strncpy", "strcat",
            "sprintf", "snprintf", "fprintf", "fputs",
            "sin", "cos", "tan", "sqrt", "pow", "floor", "ceil", "fabs",
            "atoi", "atof", "atol", "strtoll", "strtoull"
        };
        if (crt_syms.find(s) != crt_syms.end()) return "msvcrt.dll";

        return "KERNEL32.dll";
    }

    bool GrassObjWriter::inject_missing_win64_runtime(CompiledUnit& unit) {
        for (int pass = 0; pass < 8; ++pass) {
            bool any_injected = false;
            std::unordered_set<std::string> defined_symbols;
            for (const auto& s : unit.symbols) {
                if (!s.is_external) {
                    defined_symbols.insert(s.name);
                }
            }

            std::unordered_set<std::string> needed_symbols;
            for (const auto& r : unit.relocations) {
                if (defined_symbols.find(r.symbol_name) == defined_symbols.end()) {
                    needed_symbols.insert(r.symbol_name);
                }
            }

            auto add_rodata_str = [&](const std::string& sym_name, const std::string& text) {
                for (const auto& s : unit.symbols) {
                    if (s.name == sym_name && !s.is_external) return;
                }
                while (unit.rodata.size() % 16 != 0) unit.rodata.push_back(0);
                uint64_t off = unit.rodata.size();
                unit.rodata.insert(unit.rodata.end(), text.begin(), text.end());
                unit.rodata.push_back(0);
                SymbolDef sym;
                sym.name = sym_name;
                sym.section = SectionKind::RoData;
                sym.offset = off;
                sym.size = text.size() + 1;
                sym.is_global = true;
                sym.is_external = false;
                unit.symbols.push_back(sym);
                defined_symbols.insert(sym_name);
                any_injected = true;
            };

            auto add_data_buffer = [&](const std::string& sym_name, size_t size_bytes) {
                for (const auto& s : unit.symbols) {
                    if (s.name == sym_name && !s.is_external) return;
                }
                while (unit.data.size() % 16 != 0) unit.data.push_back(0);
                uint64_t off = unit.data.size();
                unit.data.resize(off + size_bytes, 0);
                SymbolDef sym;
                sym.name = sym_name;
                sym.section = SectionKind::Data;
                sym.offset = off;
                sym.size = size_bytes;
                sym.is_global = true;
                sym.is_external = false;
                unit.symbols.push_back(sym);
                defined_symbols.insert(sym_name);
                any_injected = true;
            };

            auto append_function = [&](const std::string& name,
                                       const std::vector<uint8_t>& code,
                                       const std::vector<RelocEntry>& rels) {
                while (unit.text.size() % 16 != 0) {
                    unit.text.push_back(0x90);
                }
                uint64_t fn_offset = unit.text.size();
                unit.text.insert(unit.text.end(), code.begin(), code.end());

                unit.symbols.erase(
                    std::remove_if(unit.symbols.begin(), unit.symbols.end(),
                        [&](const SymbolDef& s) { return s.name == name && s.is_external; }),
                    unit.symbols.end());

                SymbolDef sym;
                sym.name = name;
                sym.section = SectionKind::Text;
                sym.offset = fn_offset;
                sym.size = code.size();
                sym.is_global = true;
                sym.is_external = false;
                unit.symbols.push_back(sym);

                for (auto r : rels) {
                    r.offset += fn_offset;
                    unit.relocations.push_back(r);
                }
                defined_symbols.insert(name);
                any_injected = true;
            };

        // 1. _start (Entry Point Wrapper): calls main and exits via ExitProcess(eax) with zero CRT dependency
        if (defined_symbols.count("main") > 0 && defined_symbols.count("_start") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call main
                0x89, 0xC1,                         // 9: mov ecx, eax
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 11: call ExitProcess
                0x48, 0x83, 0xC4, 0x28,             // 16: add rsp, 40
                0xC3                                // 20: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "main", RelocType::Rel32, -4 },
                { SectionKind::Text, 12, "ExitProcess", RelocType::Rel32, -4 }
            };
            append_function("_start", code, rels);
        }

        // 2. grass_alloc and alloc: Win32 HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size)
        if ((needed_symbols.count("grass_alloc") > 0 && defined_symbols.count("grass_alloc") == 0) ||
            (needed_symbols.count("alloc") > 0 && defined_symbols.count("alloc") == 0)) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0x49, 0x89, 0xC8,                   // 4: mov r8, rcx (size)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 7: call GetProcessHeap
                0x48, 0x89, 0xC1,                   // 12: mov rcx, rax (hHeap)
                0xBA, 0x08, 0x00, 0x00, 0x00,       // 15: mov edx, 8 (HEAP_ZERO_MEMORY)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 20: call HeapAlloc
                0x48, 0x83, 0xC4, 0x28,             // 25: add rsp, 40
                0xC3                                // 29: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 8, "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, 21, "HeapAlloc", RelocType::Rel32, -4 }
            };
            if (needed_symbols.count("grass_alloc") > 0) append_function("grass_alloc", code, rels);
            if (needed_symbols.count("alloc") > 0) append_function("alloc", code, rels);
        }

        // 3. grass_free and free: Win32 HeapFree(GetProcessHeap(), 0, ptr)
        if ((needed_symbols.count("grass_free") > 0 && defined_symbols.count("grass_free") == 0) ||
            (needed_symbols.count("free") > 0 && defined_symbols.count("free") == 0)) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x1A,                         // 3: jz +26 (.ret at 31)
                0x48, 0x83, 0xEC, 0x28,             // 5: sub rsp, 40
                0x49, 0x89, 0xC8,                   // 9: mov r8, rcx (ptr)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 12: call GetProcessHeap
                0x48, 0x89, 0xC1,                   // 17: mov rcx, rax (hHeap)
                0x31, 0xD2,                         // 20: xor edx, edx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 22: call HeapFree
                0x48, 0x83, 0xC4, 0x28,             // 27: add rsp, 40
                0xC3                                // 31: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 13, "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, 23, "HeapFree", RelocType::Rel32, -4 }
            };
            if (needed_symbols.count("grass_free") > 0) append_function("grass_free", code, rels);
            if (needed_symbols.count("free") > 0) append_function("free", code, rels);
        }

        // 4. grass_realloc and realloc: Win32 HeapReAlloc(GetProcessHeap(), 8, ptr, new_size)
        if ((needed_symbols.count("grass_realloc") > 0 && defined_symbols.count("grass_realloc") == 0) ||
            (needed_symbols.count("realloc") > 0 && defined_symbols.count("realloc") == 0)) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x75, 0x08,                         // 3: jnz +8 (.do_realloc at 13)
                0x48, 0x89, 0xD1,                   // 5: mov rcx, rdx
                0xE9, 0x00, 0x00, 0x00, 0x00,       // 8: jmp grass_alloc
                0x48, 0x83, 0xEC, 0x28,             // 13: sub rsp, 40 (.do_realloc)
                0x49, 0x89, 0xD1,                   // 17: mov r9, rdx (new_size)
                0x49, 0x89, 0xC8,                   // 20: mov r8, rcx (ptr)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 23: call GetProcessHeap
                0x48, 0x89, 0xC1,                   // 28: mov rcx, rax (hHeap)
                0xBA, 0x08, 0x00, 0x00, 0x00,       // 31: mov edx, 8 (HEAP_ZERO_MEMORY)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 36: call HeapReAlloc
                0x48, 0x83, 0xC4, 0x28,             // 41: add rsp, 40
                0xC3                                // 45: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 9, "grass_alloc", RelocType::Rel32, -4 },
                { SectionKind::Text, 24, "GetProcessHeap", RelocType::Rel32, -4 },
                { SectionKind::Text, 37, "HeapReAlloc", RelocType::Rel32, -4 }
            };
            if (needed_symbols.count("grass_realloc") > 0) append_function("grass_realloc", code, rels);
            if (needed_symbols.count("realloc") > 0) append_function("realloc", code, rels);
        }

        // 5. memcpy: pure hardware x86_64 copy loop
        if (needed_symbols.count("memcpy") > 0 && defined_symbols.count("memcpy") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x89, 0xC8,                   // 0: mov rax, rcx
                0x4D, 0x85, 0xC0,                   // 3: test r8, r8
                0x74, 0x11,                         // 6: jz +17 (.done at 25)
                0x44, 0x8A, 0x0A,                   // 8: mov r9b, [rdx]
                0x44, 0x88, 0x09,                   // 11: mov [rcx], r9b
                0x48, 0xFF, 0xC2,                   // 14: inc rdx
                0x48, 0xFF, 0xC1,                   // 17: inc rcx
                0x49, 0xFF, 0xC8,                   // 20: dec r8
                0x75, 0xEF,                         // 23: jnz -17 (to 8)
                0xC3                                // 25: ret
            };
            std::vector<RelocEntry> rels;
            append_function("memcpy", code, rels);
        }

        // 6. memset: pure hardware x86_64 set loop
        if (needed_symbols.count("memset") > 0 && defined_symbols.count("memset") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x89, 0xC8,                   // 0: mov rax, rcx
                0x4D, 0x85, 0xC0,                   // 3: test r8, r8
                0x74, 0x0A,                         // 6: jz +10 (.done at 18)
                0x88, 0x11,                         // 8: mov [rcx], dl
                0x48, 0xFF, 0xC1,                   // 10: inc rcx
                0x49, 0xFF, 0xC8,                   // 13: dec r8
                0x75, 0xF6,                         // 16: jnz -10 (to 8)
                0xC3                                // 18: ret
            };
            std::vector<RelocEntry> rels;
            append_function("memset", code, rels);
        }

        // 7. grass_print_newline: Win32 WriteFile(GetStdHandle(-11), "\r\n", 2)
        if (needed_symbols.count("grass_print_newline") > 0 && defined_symbols.count("grass_print_newline") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x38,             // 0: sub rsp, 56
                0x66, 0xC7, 0x44, 0x24, 0x28, 0x0D, 0x0A, // 4: mov word ptr [rsp + 40], 0x0A0D
                0xB9, 0xF5, 0xFF, 0xFF, 0xFF,       // 11: mov ecx, -11
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 16: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 21: mov rcx, rax
                0x48, 0x8D, 0x54, 0x24, 0x28,       // 24: lea rdx, [rsp + 40]
                0x41, 0xB8, 0x02, 0x00, 0x00, 0x00, // 29: mov r8d, 2
                0x4C, 0x8D, 0x4C, 0x24, 0x30,       // 35: lea r9, [rsp + 48]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 40: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 49: call WriteFile
                0x48, 0x83, 0xC4, 0x38,             // 54: add rsp, 56
                0xC3                                // 58: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 17, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 50, "WriteFile", RelocType::Rel32, -4 }
            };
            append_function("grass_print_newline", code, rels);
        }

        // 8. grass_print_str: Win32 WriteFile(GetStdHandle(-11), s, lstrlenA(s))
        if (needed_symbols.count("grass_print_str") > 0 && defined_symbols.count("grass_print_str") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x38,                         // 3: jz +56 (.ret at 61)
                0x53,                               // 5: push rbx
                0x48, 0x83, 0xEC, 0x30,             // 6: sub rsp, 48
                0x48, 0x89, 0xCB,                   // 10: mov rbx, rcx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 13: call lstrlenA
                0x41, 0x89, 0xC0,                   // 18: mov r8d, eax
                0xB9, 0xF5, 0xFF, 0xFF, 0xFF,       // 21: mov ecx, -11
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 26: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 31: mov rcx, rax
                0x48, 0x89, 0xDA,                   // 34: mov rdx, rbx
                0x4C, 0x8D, 0x4C, 0x24, 0x28,       // 37: lea r9, [rsp + 40]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 42: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 51: call WriteFile
                0x48, 0x83, 0xC4, 0x30,             // 56: add rsp, 48
                0x5B,                               // 60: pop rbx
                0xC3                                // 61: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 14, "lstrlenA", RelocType::Rel32, -4 },
                { SectionKind::Text, 27, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 52, "WriteFile", RelocType::Rel32, -4 }
            };
            append_function("grass_print_str", code, rels);
        }

        // 9. grass_print_int: Win32 format integer + WriteFile
        if (needed_symbols.count("grass_print_int") > 0 && defined_symbols.count("grass_print_int") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x56,                               // 1: push rsi
                0x57,                               // 2: push rdi
                0x48, 0x83, 0xEC, 0x40,             // 3: sub rsp, 64
                0x48, 0x89, 0xC8,                   // 7: mov rax, rcx
                0x48, 0x8D, 0x7C, 0x24, 0x3F,       // 10: lea rdi, [rsp + 63]
                0xC6, 0x07, 0x00,                   // 15: mov byte ptr [rdi], 0
                0x31, 0xDB,                         // 18: xor ebx, ebx
                0x48, 0x85, 0xC0,                   // 20: test rax, rax
                0x79, 0x08,                         // 23: jns +8 (.pos at 33)
                0xBB, 0x01, 0x00, 0x00, 0x00,       // 25: mov ebx, 1
                0x48, 0xF7, 0xD8,                   // 30: neg rax
                0x31, 0xD2,                         // 33: xor edx, edx (.pos / .loop)
                0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, // 35: mov rcx, 10
                0x48, 0xF7, 0xF1,                   // 42: div rcx
                0x80, 0xC2, 0x30,                   // 45: add dl, 48
                0x48, 0xFF, 0xCF,                   // 48: dec rdi
                0x88, 0x17,                         // 51: mov [rdi], dl
                0x48, 0x85, 0xC0,                   // 53: test rax, rax
                0x75, 0xE7,                         // 56: jnz -25 (to 33)
                0x85, 0xDB,                         // 58: test ebx, ebx
                0x74, 0x06,                         // 60: jz +6 (.write at 68)
                0x48, 0xFF, 0xCF,                   // 62: dec rdi
                0xC6, 0x07, 0x2D,                   // 65: mov byte ptr [rdi], '-' (45)
                0x48, 0x8D, 0x54, 0x24, 0x3F,       // 68: lea rdx, [rsp + 63] (.write)
                0x48, 0x29, 0xFA,                   // 73: sub rdx, rdi
                0x49, 0x89, 0xD0,                   // 76: mov r8, rdx
                0xB9, 0xF5, 0xFF, 0xFF, 0xFF,       // 79: mov ecx, -11
                0x48, 0x89, 0xFE,                   // 84: mov rsi, rdi
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 87: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 92: mov rcx, rax
                0x48, 0x89, 0xF2,                   // 95: mov rdx, rsi
                0x4C, 0x8D, 0x4C, 0x24, 0x28,       // 98: lea r9, [rsp + 40]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 103: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 112: call WriteFile
                0x48, 0x83, 0xC4, 0x40,             // 117: add rsp, 64
                0x5F,                               // 121: pop rdi
                0x5E,                               // 122: pop rsi
                0x5B,                               // 123: pop rbx
                0xC3                                // 124: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 88, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 113, "WriteFile", RelocType::Rel32, -4 }
            };
            append_function("grass_print_int", code, rels);
        }

        // 10. grass_print_bool: prints "true" or "false"
        if (needed_symbols.count("grass_print_bool") > 0 && defined_symbols.count("grass_print_bool") == 0) {
            add_rodata_str("__grass_str_true", "true");
            add_rodata_str("__grass_str_false", "false");
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x09,                         // 3: jz +9 (.false_br at 14)
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 5: lea rcx, [rip + __grass_str_true]
                0xEB, 0x07,                         // 12: jmp +7 (.call at 21)
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 14: lea rcx, [rip + __grass_str_false]
                0xE9, 0x00, 0x00, 0x00, 0x00        // 21: jmp grass_print_str
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 8, "__grass_str_true", RelocType::Rel32, -4 },
                { SectionKind::Text, 17, "__grass_str_false", RelocType::Rel32, -4 },
                { SectionKind::Text, 22, "grass_print_str", RelocType::Rel32, -4 }
            };
            append_function("grass_print_bool", code, rels);
        }

        // 11. grass_print_ptr: prints hex pointer "0x..."
        if (needed_symbols.count("grass_print_ptr") > 0 && defined_symbols.count("grass_print_ptr") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x56,                               // 1: push rsi
                0x57,                               // 2: push rdi
                0x48, 0x83, 0xEC, 0x40,             // 3: sub rsp, 64
                0x48, 0x89, 0xC8,                   // 7: mov rax, rcx
                0x48, 0x8D, 0x7C, 0x24, 0x3F,       // 10: lea rdi, [rsp + 63]
                0xC6, 0x07, 0x00,                   // 15: mov byte ptr [rdi], 0
                0x48, 0x85, 0xC0,                   // 18: test rax, rax
                0x75, 0x06,                         // 21: jnz +6 (.loop at 29)
                0x48, 0xFF, 0xCF,                   // 23: dec rdi
                0xC6, 0x07, 0x30,                   // 26: mov byte ptr [rdi], '0'
                0xEB, 0x1A,                         // 29: jmp .prefix (at 57)
                0x89, 0xC2,                         // 31: mov edx, eax (.loop)
                0x83, 0xE2, 0x0F,                   // 33: and edx, 0xF
                0x80, 0xFA, 0x0A,                   // 36: cmp dl, 10
                0x72, 0x05,                         // 39: jb +5
                0x80, 0xC2, 0x57,                   // 41: add dl, 'a' - 10 (87)
                0xEB, 0x03,                         // 44: jmp +3
                0x80, 0xC2, 0x30,                   // 46: add dl, '0' (48)
                0x48, 0xFF, 0xCF,                   // 49: dec rdi
                0x88, 0x17,                         // 52: mov [rdi], dl
                0x48, 0xC1, 0xE8, 0x04,             // 54: shr rax, 4
                0x75, 0xE3,                         // 58: jnz .loop (at 31)
                0x48, 0xFF, 0xCF,                   // 60: dec rdi (.prefix)
                0xC6, 0x07, 0x78,                   // 63: mov byte ptr [rdi], 'x'
                0x48, 0xFF, 0xCF,                   // 66: dec rdi
                0xC6, 0x07, 0x30,                   // 69: mov byte ptr [rdi], '0'
                0x48, 0x8D, 0x54, 0x24, 0x3F,       // 72: lea rdx, [rsp + 63]
                0x48, 0x29, 0xFA,                   // 77: sub rdx, rdi
                0x49, 0x89, 0xD0,                   // 80: mov r8, rdx
                0xB9, 0xF5, 0xFF, 0xFF, 0xFF,       // 83: mov ecx, -11
                0x48, 0x89, 0xFE,                   // 88: mov rsi, rdi
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 91: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 96: mov rcx, rax
                0x48, 0x89, 0xF2,                   // 99: mov rdx, rsi
                0x4C, 0x8D, 0x4C, 0x24, 0x28,       // 102: lea r9, [rsp + 40]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 107: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 116: call WriteFile
                0x48, 0x83, 0xC4, 0x40,             // 121: add rsp, 64
                0x5F,                               // 125: pop rdi
                0x5E,                               // 126: pop rsi
                0x5B,                               // 127: pop rbx
                0xC3                                // 128: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 92, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 117, "WriteFile", RelocType::Rel32, -4 }
            };
            append_function("grass_print_ptr", code, rels);
        }

        // 12. grass_print_double: prints double value to console
        if (needed_symbols.count("grass_print_double") > 0 && defined_symbols.count("grass_print_double") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x38,             // 0: sub rsp, 56
                0xF2, 0x0F, 0x11, 0x44, 0x24, 0x20, // 4: movsd [rsp + 32], xmm0
                0xF2, 0x48, 0x0F, 0x2C, 0xC8,       // 10: cvttsd2si rcx, xmm0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 15: call grass_print_int
                0x66, 0xC7, 0x44, 0x24, 0x28, 0x2E, 0x00, // 20: mov word ptr [rsp + 40], '.' (0x002E)
                0xB9, 0xF5, 0xFF, 0xFF, 0xFF,       // 27: mov ecx, -11
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 32: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 37: mov rcx, rax
                0x48, 0x8D, 0x54, 0x24, 0x28,       // 40: lea rdx, [rsp + 40]
                0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, // 45: mov r8d, 1
                0x4C, 0x8D, 0x4C, 0x24, 0x30,       // 51: lea r9, [rsp + 48]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 56: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 65: call WriteFile
                0xF2, 0x0F, 0x10, 0x44, 0x24, 0x20, // 70: movsd xmm0, [rsp + 32]
                0xF2, 0x48, 0x0F, 0x2C, 0xC0,       // 76: cvttsd2si rax, xmm0
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 81: cvtsi2sd xmm1, rax
                0xF2, 0x0F, 0x5C, 0xC1,             // 86: subsd xmm0, xmm1
                0x48, 0xC7, 0xC0, 0x10, 0x27, 0x00, 0x00, // 90: mov rax, 10000
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 97: cvtsi2sd xmm1, rax
                0xF2, 0x0F, 0x59, 0xC1,             // 102: mulsd xmm0, xmm1
                0xF2, 0x48, 0x0F, 0x2C, 0xC8,       // 106: cvttsd2si rcx, xmm0
                0x48, 0x85, 0xC9,                   // 111: test rcx, rcx
                0x79, 0x03,                         // 114: jns +3
                0x48, 0xF7, 0xD9,                   // 116: neg rcx
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 119: call grass_print_int
                0x48, 0x83, 0xC4, 0x38,             // 124: add rsp, 56
                0xC3                                // 128: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 16, "grass_print_int", RelocType::Rel32, -4 },
                { SectionKind::Text, 33, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 66, "WriteFile", RelocType::Rel32, -4 },
                { SectionKind::Text, 120, "grass_print_int", RelocType::Rel32, -4 }
            };
            append_function("grass_print_double", code, rels);
        }

        // 13. grass_read_int: Win32 ReadFile(GetStdHandle(-10)) and parse integer
        if (needed_symbols.count("grass_read_int") > 0 && defined_symbols.count("grass_read_int") == 0) {
            std::vector<uint8_t> code = {
                0x56,                               // 0: push rsi
                0x48, 0x83, 0xEC, 0x40,             // 1: sub rsp, 64
                0xB9, 0xF6, 0xFF, 0xFF, 0xFF,       // 5: mov ecx, -10
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 10: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 15: mov rcx, rax
                0x48, 0x8D, 0x54, 0x24, 0x20,       // 18: lea rdx, [rsp + 32]
                0x41, 0xB8, 0x1F, 0x00, 0x00, 0x00, // 23: mov r8d, 31
                0x4C, 0x8D, 0x4C, 0x24, 0x18,       // 29: lea r9, [rsp + 24]
                0x48, 0xC7, 0x44, 0x24, 0x14, 0x00, 0x00, 0x00, 0x00, // 35: mov qword ptr [rsp + 20], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 44: call ReadFile
                0x31, 0xC0,                         // 49: xor eax, eax
                0x45, 0x31, 0xC0,                   // 51: xor r8d, r8d
                0x31, 0xC9,                         // 54: xor ecx, ecx
                0x48, 0x8D, 0x74, 0x24, 0x20,       // 56: lea rsi, [rsp + 32]
                0x0F, 0xB6, 0x14, 0x0E,             // 60: movzbl (%rsi,%rcx,1),%edx
                0x48, 0xFF, 0xC1,                   // 64: inc rcx
                0x80, 0xFA, 0x20,                   // 67: cmp dl, 32
                0x74, 0xF4,                         // 70: je 60
                0x80, 0xFA, 0x09,                   // 72: cmp dl, 9
                0x74, 0xEF,                         // 75: je 60
                0x80, 0xFA, 0x2D,                   // 77: cmp dl, 45 ('-')
                0x75, 0x0D,                         // 80: jne +13
                0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, // 82: mov r8d, 1
                0x0F, 0xB6, 0x14, 0x0E,             // 88: movzbl (%rsi,%rcx,1),%edx
                0x48, 0xFF, 0xC1,                   // 92: inc rcx
                0x80, 0xFA, 0x30,                   // 95: cmp dl, '0'
                0x72, 0x1C,                         // 98: jb +28 (.done at 128)
                0x80, 0xFA, 0x39,                   // 100: cmp dl, '9'
                0x77, 0x17,                         // 103: ja +23 (.done at 128)
                0x80, 0xEA, 0x30,                   // 105: sub dl, '0'
                0x48, 0x0F, 0xB6, 0xD2,             // 108: movzbq rdx, dl
                0x48, 0x6B, 0xC0, 0x0A,             // 112: imul rax, 10
                0x48, 0x01, 0xD0,                   // 116: add rax, rdx
                0x0F, 0xB6, 0x14, 0x0E,             // 119: movzbl (%rsi,%rcx,1),%edx
                0x48, 0xFF, 0xC1,                   // 123: inc rcx
                0xEB, 0xDF,                         // 126: jmp 95
                0x45, 0x85, 0xC0,                   // 128: test r8d, r8d (.done)
                0x74, 0x03,                         // 131: jz +3
                0x48, 0xF7, 0xD8,                   // 133: neg rax
                0x48, 0x83, 0xC4, 0x40,             // 136: add rsp, 64
                0x5E,                               // 140: pop rsi
                0xC3                                // 141: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 11, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 45, "ReadFile", RelocType::Rel32, -4 }
            };
            append_function("grass_read_int", code, rels);
        }

        // 14. grass_array_create, grass_array_push, grass_array_get, grass_array_set, grass_array_len
        if (needed_symbols.count("grass_array_create") > 0 && defined_symbols.count("grass_array_create") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xB9, 0x20, 0x00, 0x00, 0x00,       // 4: mov ecx, 32
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 9: call grass_alloc
                0x48, 0x85, 0xC0,                   // 14: test rax, rax
                0x74, 0x38,                         // 17: jz +56 (.ret at 75)
                0x48, 0x89, 0x44, 0x24, 0x20,       // 19: mov [rsp + 32], rax
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 24: mov r10, 0x415252594752474E
                0x4C, 0x89, 0x10,                   // 34: mov [rax], r10
                0x48, 0xC7, 0x40, 0x10, 0x00, 0x00, 0x00, 0x00, // 37: mov qword ptr [rax + 16], 0
                0x48, 0xC7, 0x40, 0x18, 0x08, 0x00, 0x00, 0x00, // 45: mov qword ptr [rax + 24], 8
                0xB9, 0x40, 0x00, 0x00, 0x00,       // 53: mov ecx, 64
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 58: call grass_alloc
                0x48, 0x8B, 0x4C, 0x24, 0x20,       // 63: mov rcx, [rsp + 32]
                0x48, 0x89, 0x41, 0x08,             // 68: mov [rcx + 8], rax
                0x48, 0x89, 0xC8,                   // 72: mov rax, rcx
                0x48, 0x83, 0xC4, 0x28,             // 75: add rsp, 40 (.ret)
                0xC3                                // 79: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 10, "grass_alloc", RelocType::Rel32, -4 },
                { SectionKind::Text, 59, "grass_alloc", RelocType::Rel32, -4 }
            };
            append_function("grass_array_create", code, rels);
        }

        if (needed_symbols.count("grass_array_push") > 0 && defined_symbols.count("grass_array_push") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x64,                         // 3: jz +100 (.ret at 105)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 5: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 15: cmp [rcx], r10
                0x75, 0x55,                         // 18: jne +85 (.ret at 105)
                0x53,                               // 20: push rbx
                0x56,                               // 21: push rsi
                0x48, 0x83, 0xEC, 0x28,             // 22: sub rsp, 40
                0x48, 0x89, 0xCB,                   // 26: mov rbx, rcx
                0x48, 0x89, 0xD6,                   // 29: mov rsi, rdx
                0x48, 0x8B, 0x43, 0x10,             // 32: mov rax, [rbx + 16]
                0x48, 0x3B, 0x43, 0x18,             // 36: cmp rax, [rbx + 24]
                0x72, 0x29,                         // 40: jb +41 (.push_store at 83)
                0x48, 0x8B, 0x43, 0x18,             // 42: mov rax, [rbx + 24]
                0x48, 0xD1, 0xE0,                   // 46: shl rax, 1
                0x48, 0x85, 0xC0,                   // 49: test rax, rax
                0x75, 0x05,                         // 52: jnz +5 (.cap_ok at 59)
                0xB8, 0x08, 0x00, 0x00, 0x00,       // 54: mov eax, 8
                0x48, 0x89, 0x43, 0x18,             // 59: mov [rbx + 24], rax (.cap_ok)
                0x48, 0xC1, 0xE0, 0x03,             // 63: shl rax, 3
                0x48, 0x89, 0xC2,                   // 67: mov rdx, rax
                0x48, 0x8B, 0x4B, 0x08,             // 70: mov rcx, [rbx + 8]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 74: call grass_realloc
                0x48, 0x89, 0x43, 0x08,             // 79: mov [rbx + 8], rax
                0x48, 0x8B, 0x4B, 0x08,             // 83: mov rcx, [rbx + 8] (.push_store)
                0x48, 0x8B, 0x53, 0x10,             // 87: mov rdx, [rbx + 16]
                0x48, 0x89, 0x34, 0xD1,             // 91: mov [rcx + rdx * 8], rsi
                0x48, 0xFF, 0x43, 0x10,             // 95: inc qword ptr [rbx + 16]
                0x48, 0x83, 0xC4, 0x28,             // 99: add rsp, 40
                0x5E,                               // 103: pop rsi
                0x5B,                               // 104: pop rbx
                0xC3                                // 105: ret (.ret)
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 75, "grass_realloc", RelocType::Rel32, -4 }
            };
            append_function("grass_array_push", code, rels);
        }

        if (needed_symbols.count("grass_array_get") > 0 && defined_symbols.count("grass_array_get") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0,                   // 0: xor rax, rax
                0x48, 0x85, 0xC9,                   // 3: test rcx, rcx
                0x74, 0x27,                         // 6: jz +39 (.ret at 47)
                0x48, 0x85, 0xD2,                   // 8: test rdx, rdx
                0x78, 0x22,                         // 11: js +34 (.ret at 47)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 13: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 23: cmp [rcx], r10
                0x74, 0x05,                         // 26: je +5 (.is_dyn_array at 33)
                0x0F, 0xB6, 0x04, 0x11,             // 28: movzx rax, byte ptr [rcx + rdx]
                0xC3,                               // 32: ret
                0x48, 0x3B, 0x51, 0x10,             // 33: cmp rdx, [rcx + 16] (.is_dyn_array)
                0x73, 0x08,                         // 37: jae +8 (.ret at 47)
                0x48, 0x8B, 0x41, 0x08,             // 39: mov rax, [rcx + 8]
                0x48, 0x8B, 0x04, 0xD0,             // 43: mov rax, [rax + rdx * 8]
                0xC3                                // 47: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            append_function("grass_array_get", code, rels);
        }

        if (needed_symbols.count("grass_array_set") > 0 && defined_symbols.count("grass_array_set") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x35,                         // 3: jz +53 (.ret at 58)
                0x48, 0x85, 0xD2,                   // 5: test rdx, rdx
                0x78, 0x30,                         // 8: js +48 (.ret at 58)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 10: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 20: cmp [rcx], r10
                0x74, 0x05,                         // 23: je +5 (.is_dyn_array at 30)
                0x44, 0x88, 0x04, 0x11,             // 25: mov [rcx + rdx], r8b
                0xC3,                               // 29: ret
                0x48, 0x3B, 0x51, 0x18,             // 30: cmp rdx, [rcx + 24] (.is_dyn_array)
                0x73, 0x16,                         // 34: jae +22 (.ret at 58)
                0x48, 0x3B, 0x51, 0x10,             // 36: cmp rdx, [rcx + 16]
                0x72, 0x08,                         // 40: jb +8 (.store at 50)
                0x48, 0x8D, 0x42, 0x01,             // 42: lea rax, [rdx + 1]
                0x48, 0x89, 0x41, 0x10,             // 46: mov [rcx + 16], rax
                0x48, 0x8B, 0x41, 0x08,             // 50: mov rax, [rcx + 8] (.store)
                0x4C, 0x89, 0x04, 0xD0,             // 54: mov [rax + rdx * 8], r8
                0xC3                                // 58: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            append_function("grass_array_set", code, rels);
        }

        if (needed_symbols.count("grass_array_len") > 0 && defined_symbols.count("grass_array_len") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0,                   // 0: xor rax, rax
                0x48, 0x85, 0xC9,                   // 3: test rcx, rcx
                0x74, 0x1E,                         // 6: jz +30 (.ret at 38)
                0x49, 0xBA, 0x4E, 0x47, 0x52, 0x47, 0x59, 0x52, 0x52, 0x41, // 8: mov r10, 0x415252594752474E
                0x4C, 0x39, 0x11,                   // 18: cmp [rcx], r10
                0x74, 0x0B,                         // 21: je +11 (.is_dyn_array at 34)
                0x80, 0x3C, 0x01, 0x00,             // 23: cmp byte ptr [rcx + rax], 0 (.strlen_loop)
                0x74, 0x09,                         // 27: je +9 (.ret at 38)
                0x48, 0xFF, 0xC0,                   // 29: inc rax
                0xEB, 0xF5,                         // 32: jmp -11 (.strlen_loop at 23)
                0x48, 0x8B, 0x41, 0x10,             // 34: mov rax, [rcx + 16] (.is_dyn_array)
                0xC3                                // 38: ret (.ret)
            };
            std::vector<RelocEntry> rels;
            append_function("grass_array_len", code, rels);
        }

        if (needed_symbols.count("str_to_int") > 0 && defined_symbols.count("str_to_int") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x31, 0xC0, 0x48, 0x85, 0xC9, 0x74, 0x55, 0x41, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x0F, 0xB6,
                0x11, 0x80, 0xFA, 0x20, 0x74, 0x0F, 0x80, 0xFA, 0x09, 0x74, 0x0A, 0x80, 0xFA, 0x0A, 0x74, 0x05,
                0x80, 0xFA, 0x0D, 0x75, 0x05, 0x48, 0xFF, 0xC1, 0xEB, 0xE4, 0x80, 0xFA, 0x2D, 0x75, 0x0B, 0x41,
                0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0xFF, 0xC1, 0xEB, 0x08, 0x80, 0xFA, 0x2B, 0x75, 0x03, 0x48,
                0xFF, 0xC1, 0x0F, 0xB6, 0x11, 0x83, 0xEA, 0x30, 0x83, 0xFA, 0x09, 0x77, 0x0C, 0x48, 0x6B, 0xC0,
                0x0A, 0x48, 0x01, 0xD0, 0x48, 0xFF, 0xC1, 0xEB, 0xE9, 0x49, 0x0F, 0xAF, 0xC0, 0xC3
            };
            std::vector<RelocEntry> rels;
            append_function("str_to_int", code, rels);
        }

        if (needed_symbols.count("int_to_str") > 0 && defined_symbols.count("int_to_str") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0x4C, 0x24, 0x20, 0xB9, 0x20, 0x00, 0x00, 0x00, 0xE8, 0x00,
                0x00, 0x00, 0x00, 0x48, 0x85, 0xC0, 0x74, 0x60, 0x49, 0x89, 0xC1, 0x4C, 0x8B, 0x44, 0x24, 0x20,
                0x48, 0x89, 0x44, 0x24, 0x20, 0x4D, 0x85, 0xC0, 0x75, 0x08, 0x66, 0x41, 0xC7, 0x01, 0x30, 0x00,
                0xEB, 0x46, 0x79, 0x0A, 0x41, 0xC6, 0x01, 0x2D, 0x49, 0xFF, 0xC1, 0x49, 0xF7, 0xD8, 0x4C, 0x8D,
                0x54, 0x24, 0x46, 0x41, 0xC6, 0x02, 0x00, 0x49, 0xC7, 0xC3, 0x0A, 0x00, 0x00, 0x00, 0x4C, 0x89,
                0xC0, 0x48, 0x31, 0xD2, 0x49, 0xF7, 0xF3, 0x80, 0xC2, 0x30, 0x49, 0xFF, 0xCA, 0x41, 0x88, 0x12,
                0x49, 0x89, 0xC0, 0x4D, 0x85, 0xC0, 0x75, 0xE6, 0x41, 0x8A, 0x12, 0x41, 0x88, 0x11, 0x49, 0xFF,
                0xC2, 0x49, 0xFF, 0xC1, 0x84, 0xD2, 0x75, 0xF0, 0x48, 0x8B, 0x44, 0x24, 0x20, 0x48, 0x83, 0xC4,
                0x58, 0xC3
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 15, "grass_alloc", RelocType::Rel32, -4 }
            };
            append_function("int_to_str", code, rels);
        }

        // 15. Standard timer intrinsics: start_time, stop_time, conclus_time, now_ms, sleep_ms
        bool need_time = (needed_symbols.count("start_time") > 0 ||
                          needed_symbols.count("stop_time") > 0 ||
                          needed_symbols.count("conclus_time") > 0);
        if (need_time) {
            add_data_buffer("__grass_time_buf", 64);
        }

        if (needed_symbols.count("start_time") > 0 && defined_symbols.count("start_time") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0x48, 0x8D, 0x0D, 0x00, 0x00, 0x00, 0x00, // 4: lea rcx, [rip + __grass_time_buf]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 11: call QueryPerformanceFrequency
                0x48, 0x8D, 0x0D, 0x08, 0x00, 0x00, 0x00, // 16: lea rcx, [rip + __grass_time_buf + 8]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 23: call QueryPerformanceCounter
                0x48, 0x83, 0xC4, 0x28,             // 28: add rsp, 40
                0xC3                                // 32: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 7, "__grass_time_buf", RelocType::Rel32, -4 },
                { SectionKind::Text, 12, "QueryPerformanceFrequency", RelocType::Rel32, -4 },
                { SectionKind::Text, 19, "__grass_time_buf", RelocType::Rel32, 4 }, // +8
                { SectionKind::Text, 24, "QueryPerformanceCounter", RelocType::Rel32, -4 }
            };
            append_function("start_time", code, rels);
        }

        if (needed_symbols.count("stop_time") > 0 && defined_symbols.count("stop_time") == 0) {
            std::vector<uint8_t> code = {
                0x53,                               // 0: push rbx
                0x57,                               // 1: push rdi
                0x56,                               // 2: push rsi
                0x48, 0x83, 0xEC, 0x30,             // 3: sub rsp, 48
                0x48, 0x8D, 0x0D, 0x10, 0x00, 0x00, 0x00, // 7: lea rcx, [rip + __grass_time_buf + 16]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 14: call QueryPerformanceCounter
                0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00, // 19: mov rax, [rip + __grass_time_buf + 16]
                0x48, 0x2B, 0x05, 0x08, 0x00, 0x00, 0x00, // 26: sub rax, [rip + __grass_time_buf + 8]
                0x48, 0x8B, 0x1D, 0x00, 0x00, 0x00, 0x00, // 33: mov rbx, [rip + __grass_time_buf]
                0x48, 0x85, 0xDB,                   // 40: test rbx, rbx
                0x75, 0x07,                         // 43: jnz +7
                0x48, 0xC7, 0xC3, 0x01, 0x00, 0x00, 0x00, // 45: mov rbx, 1
                0xF2, 0x48, 0x0F, 0x2A, 0xC0,       // 52: cvtsi2sd xmm0, rax
                0x48, 0xC7, 0xC1, 0xE8, 0x03, 0x00, 0x00, // 57: mov rcx, 1000
                0xF2, 0x48, 0x0F, 0x2A, 0xC9,       // 64: cvtsi2sd xmm1, rcx
                0xF2, 0x0F, 0x59, 0xC1,             // 69: mulsd xmm0, xmm1
                0xF2, 0x48, 0x0F, 0x2A, 0xD3,       // 73: cvtsi2sd xmm2, rbx
                0xF2, 0x0F, 0x5E, 0xC2,             // 78: divsd xmm0, xmm2
                0xF2, 0x0F, 0x11, 0x05, 0x18, 0x00, 0x00, 0x00, // 82: movsd [rip + __grass_time_buf + 24], xmm0
                0xF2, 0x48, 0x0F, 0x2C, 0xF0,       // 90: cvttsd2si rsi, xmm0
                0x48, 0x8D, 0x3D, 0x20, 0x00, 0x00, 0x00, // 95: lea rdi, [rip + __grass_time_buf + 32]
                0x48, 0x89, 0xF0,                   // 102: mov rax, rsi
                0x48, 0x8D, 0x5C, 0x24, 0x1E,       // 105: lea rbx, [rsp + 30]
                0xC6, 0x03, 0x00,                   // 110: mov byte ptr [rbx], 0
                0x31, 0xD2,                         // 113: xor edx, edx
                0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, // 115: mov rcx, 10
                0x48, 0xF7, 0xF1,                   // 122: div rcx
                0x80, 0xC2, 0x30,                   // 125: add dl, 48
                0x48, 0xFF, 0xCB,                   // 128: dec rbx
                0x88, 0x13,                         // 131: mov [rbx], dl
                0x48, 0x85, 0xC0,                   // 133: test rax, rax
                0x75, 0xE7,                         // 136: jnz -25 (to 113)
                0x8A, 0x03,                         // 138: mov al, [rbx]
                0x48, 0xFF, 0xC3,                   // 140: inc rbx
                0x88, 0x07,                         // 143: mov [rdi], al
                0x48, 0xFF, 0xC7,                   // 145: inc rdi
                0x84, 0xC0,                         // 148: test al, al
                0x75, 0xF2,                         // 150: jnz -14 (to 138)
                0x48, 0xFF, 0xCF,                   // 152: dec rdi
                0xC6, 0x07, 0x2E,                   // 155: mov byte ptr [rdi], '.' (0x2E)
                0x48, 0xFF, 0xC7,                   // 158: inc rdi
                0x48, 0xC7, 0xC0, 0x64, 0x00, 0x00, 0x00, // 161: mov rax, 100
                0xF2, 0x48, 0x0F, 0x2A, 0xC8,       // 168: cvtsi2sd xmm1, rax
                0xF2, 0x48, 0x0F, 0x2A, 0xD6,       // 173: cvtsi2sd xmm2, rsi
                0xF2, 0x0F, 0x10, 0xD8,             // 178: movsd xmm3, xmm0
                0xF2, 0x0F, 0x5C, 0xDA,             // 182: subsd xmm3, xmm2
                0xF2, 0x0F, 0x59, 0xD9,             // 186: mulsd xmm3, xmm1
                0xF2, 0x48, 0x0F, 0x2C, 0xC3,       // 190: cvttsd2si rax, xmm3
                0x48, 0x85, 0xC0,                   // 195: test rax, rax
                0x79, 0x03,                         // 198: jns +3
                0x48, 0xF7, 0xD8,                   // 200: neg rax
                0x31, 0xD2,                         // 203: xor edx, edx
                0x48, 0xC7, 0xC1, 0x0A, 0x00, 0x00, 0x00, // 205: mov rcx, 10
                0x48, 0xF7, 0xF1,                   // 212: div rcx
                0x04, 0x30,                         // 215: add al, 48
                0x80, 0xC2, 0x30,                   // 217: add dl, 48
                0x88, 0x07,                         // 220: mov [rdi], al
                0x88, 0x57, 0x01,                   // 222: mov [rdi + 1], dl
                0xC6, 0x47, 0x02, 0x20,             // 225: mov byte ptr [rdi + 2], ' '
                0xC6, 0x47, 0x03, 0x6D,             // 229: mov byte ptr [rdi + 3], 'm'
                0xC6, 0x47, 0x04, 0x73,             // 233: mov byte ptr [rdi + 4], 's'
                0xC6, 0x47, 0x05, 0x00,             // 237: mov byte ptr [rdi + 5], 0
                0x48, 0x89, 0xF0,                   // 241: mov rax, rsi
                0x48, 0x83, 0xC4, 0x30,             // 244: add rsp, 48
                0x5E,                               // 248: pop rsi
                0x5F,                               // 249: pop rdi
                0x5B,                               // 250: pop rbx
                0xC3                                // 251: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 10, "__grass_time_buf", RelocType::Rel32, 12 }, // +16
                { SectionKind::Text, 15, "QueryPerformanceCounter", RelocType::Rel32, -4 },
                { SectionKind::Text, 22, "__grass_time_buf", RelocType::Rel32, 12 }, // +16
                { SectionKind::Text, 29, "__grass_time_buf", RelocType::Rel32, 4 },  // +8
                { SectionKind::Text, 36, "__grass_time_buf", RelocType::Rel32, -4 }, // +0
                { SectionKind::Text, 86, "__grass_time_buf", RelocType::Rel32, 20 }, // +24
                { SectionKind::Text, 98, "__grass_time_buf", RelocType::Rel32, 28 }  // +32
            };
            append_function("stop_time", code, rels);
        }

        if (needed_symbols.count("conclus_time") > 0 && defined_symbols.count("conclus_time") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x8D, 0x05, 0x20, 0x00, 0x00, 0x00, // 0: lea rax, [rip + __grass_time_buf + 32]
                0xC3                                      // 7: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 3, "__grass_time_buf", RelocType::Rel32, 28 } // +32
            };
            append_function("conclus_time", code, rels);
        }

        if (needed_symbols.count("now_ms") > 0 && defined_symbols.count("now_ms") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call GetTickCount64
                0x48, 0x83, 0xC4, 0x28,             // 9: add rsp, 40
                0xC3                                // 13: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "GetTickCount64", RelocType::Rel32, -4 }
            };
            append_function("now_ms", code, rels);
        }

        if (needed_symbols.count("sleep_ms") > 0 && defined_symbols.count("sleep_ms") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 4: call Sleep
                0x48, 0x83, 0xC4, 0x28,             // 9: add rsp, 40
                0xC3                                // 13: ret
            };
            std::vector<RelocEntry> rels = {
                { SectionKind::Text, 5, "Sleep", RelocType::Rel32, -4 }
            };
            append_function("sleep_ms", code, rels);
        }

        // 16. Math intrinsics: sqrt, abs/fabs, floor, ceil, sin, cos, pow
        if (needed_symbols.count("sqrt") > 0 && defined_symbols.count("sqrt") == 0) {
            std::vector<uint8_t> code = {
                0xF2, 0x0F, 0x51, 0xC0,             // sqrtsd xmm0, xmm0
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("sqrt", code, rels);
        }

        if ((needed_symbols.count("abs") > 0 && defined_symbols.count("abs") == 0) ||
            (needed_symbols.count("fabs") > 0 && defined_symbols.count("fabs") == 0)) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x76, 0xC9,             // pcmpeqd xmm1, xmm1
                0x66, 0x0F, 0x73, 0xD1, 0x01,       // psrlq xmm1, 1
                0x66, 0x0F, 0xDB, 0xC1,             // pand xmm0, xmm1
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            if (needed_symbols.count("abs") > 0) append_function("abs", code, rels);
            if (needed_symbols.count("fabs") > 0) append_function("fabs", code, rels);
        }

        if (needed_symbols.count("floor") > 0 && defined_symbols.count("floor") == 0) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x01, // roundsd xmm0, xmm0, 1 (round down)
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("floor", code, rels);
        }

        if (needed_symbols.count("ceil") > 0 && defined_symbols.count("ceil") == 0) {
            std::vector<uint8_t> code = {
                0x66, 0x0F, 0x3A, 0x0B, 0xC0, 0x02, // roundsd xmm0, xmm0, 2 (round up)
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("ceil", code, rels);
        }

        if (needed_symbols.count("sin") > 0 && defined_symbols.count("sin") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xFE,                         // fsin
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("sin", code, rels);
        }

        if (needed_symbols.count("cos") > 0 && defined_symbols.count("cos") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xFF,                         // fcos
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("cos", code, rels);
        }

        if (needed_symbols.count("pow") > 0 && defined_symbols.count("pow") == 0) {
            std::vector<uint8_t> code = {
                0x48, 0x83, 0xEC, 0x18,             // sub rsp, 24
                0xF2, 0x0F, 0x11, 0x04, 0x24,       // movsd [rsp], xmm0 (base)
                0xF2, 0x0F, 0x11, 0x4C, 0x24, 0x08, // movsd [rsp+8], xmm1 (exp)
                0xDD, 0x44, 0x24, 0x08,             // fld qword ptr [rsp+8]
                0xDD, 0x04, 0x24,                   // fld qword ptr [rsp]
                0xD9, 0xF1,                         // fyl2x
                0xD9, 0xC0,                         // fld st(0)
                0xD9, 0xFC,                         // frndint
                0xDC, 0xE9,                         // fsubr st(1), st(0)
                0xD9, 0xC9,                         // fxch st(1)
                0xD9, 0xF0,                         // f2xm1
                0xD9, 0xE8,                         // fld1
                0xDE, 0xC1,                         // faddp st(1), st
                0xD9, 0xFD,                         // fscale
                0xDD, 0xD9,                         // fstp st(1)
                0xDD, 0x1C, 0x24,                   // fstp qword ptr [rsp]
                0xF2, 0x0F, 0x10, 0x04, 0x24,       // movsd xmm0, [rsp]
                0x48, 0x83, 0xC4, 0x18,             // add rsp, 24
                0xC3                                // ret
            };
            std::vector<RelocEntry> rels;
            append_function("pow", code, rels);
        }

        // 17. grass_install_runtime_crash_handler & __grass_veh_handler
        if (needed_symbols.count("grass_install_runtime_crash_handler") > 0 &&
            defined_symbols.count("grass_install_runtime_crash_handler") == 0) {
            add_rodata_str("__grass_crash_msg",
                "\r\n================================================================================\r\n"
                "GRASS NATIVE RUNTIME CRASH DETECTED\r\n"
                "Exception Code: 0xC0000005\r\n"
                "================================================================================\r\n"
                "Grass Native Stack Trace:\r\n"
                "  #0  in <native code>\r\n"
                "================================================================================\r\n\r\n");

            std::vector<uint8_t> veh_code = {
                0x48, 0x85, 0xC9,                   // 0: test rcx, rcx
                0x74, 0x3A,                         // 3: jz .pass (at 63)
                0x48, 0x8B, 0x01,                   // 5: mov rax, [rcx]
                0x48, 0x85, 0xC0,                   // 8: test rax, rax
                0x74, 0x32,                         // 11: jz .pass (at 63)
                0x48, 0x83, 0xEC, 0x38,             // 13: sub rsp, 56
                0xB9, 0xF4, 0xFF, 0xFF, 0xFF,       // 17: mov ecx, -12 (STD_ERROR_HANDLE)
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 22: call GetStdHandle
                0x48, 0x89, 0xC1,                   // 27: mov rcx, rax
                0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00, // 30: lea rdx, [rip + __grass_crash_msg]
                0x41, 0xB8, 0xDA, 0x00, 0x00, 0x00, // 37: mov r8d, 218
                0x4C, 0x8D, 0x4C, 0x24, 0x28,       // 43: lea r9, [rsp + 40]
                0x48, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00, 0x00, // 48: mov qword ptr [rsp + 32], 0
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 57: call WriteFile
                0xB9, 0x01, 0x00, 0x00, 0x00,       // 62: mov ecx, 1
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 67: call ExitProcess
                0x31, 0xC0,                         // 72: xor eax, eax (.pass)
                0xC3                                // 74: ret
            };
            std::vector<RelocEntry> veh_rels = {
                { SectionKind::Text, 23, "GetStdHandle", RelocType::Rel32, -4 },
                { SectionKind::Text, 33, "__grass_crash_msg", RelocType::Rel32, -4 },
                { SectionKind::Text, 58, "WriteFile", RelocType::Rel32, -4 },
                { SectionKind::Text, 68, "ExitProcess", RelocType::Rel32, -4 }
            };
            append_function("__grass_veh_handler", veh_code, veh_rels);

            std::vector<uint8_t> inst_code = {
                0x48, 0x83, 0xEC, 0x28,             // 0: sub rsp, 40
                0xB9, 0x01, 0x00, 0x00, 0x00,       // 4: mov ecx, 1
                0x48, 0x8D, 0x15, 0x00, 0x00, 0x00, 0x00, // 9: lea rdx, [rip + __grass_veh_handler]
                0xE8, 0x00, 0x00, 0x00, 0x00,       // 16: call AddVectoredExceptionHandler
                0x48, 0x83, 0xC4, 0x28,             // 21: add rsp, 40
                0xC3                                // 25: ret
            };
            std::vector<RelocEntry> inst_rels = {
                { SectionKind::Text, 12, "__grass_veh_handler", RelocType::Rel32, -4 },
                { SectionKind::Text, 17, "AddVectoredExceptionHandler", RelocType::Rel32, -4 }
            };
            append_function("grass_install_runtime_crash_handler", inst_code, inst_rels);
        }

        if (!any_injected) {
            break;
        }
    }

    return true;
}

    static const uint8_t s_pe_dos_stub[64] = {
        0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD, 0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, 0x54, 0x68,
        0x69, 0x73, 0x20, 0x70, 0x72, 0x6F, 0x67, 0x72, 0x61, 0x6D, 0x20, 0x63, 0x61, 0x6E, 0x6E, 0x6F,
        0x74, 0x20, 0x62, 0x65, 0x20, 0x72, 0x75, 0x6E, 0x20, 0x69, 0x6E, 0x20, 0x44, 0x4F, 0x53, 0x20,
        0x6D, 0x6F, 0x64, 0x65, 0x2E, 0x0D, 0x0D, 0x0A, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };

    bool GrassObjWriter::write_pe_executable(const CompiledUnit& unit,
                                            BinaryBuffer& out,
                                            std::string* out_error,
                                            const std::string& entry_point_name) {
        std::vector<WinLibraryImport> empty_imports;
        return write_pe_executable(unit, empty_imports, out, out_error, entry_point_name);
    }

    bool GrassObjWriter::write_pe_executable(const CompiledUnit& unit,
                                            const std::vector<WinLibraryImport>& custom_imports,
                                            BinaryBuffer& out,
                                            std::string* out_error,
                                            const std::string& entry_point_name) {
        CompiledUnit local_unit = unit;
        inject_missing_win64_runtime(local_unit);

        if (!validate_compiled_unit(local_unit, out_error)) {
            return false;
        }
        if (local_unit.arch != TargetArch::X86_64) {
            if (out_error) *out_error = "PE32+ executable writer only supports X86_64 architecture";
            return false;
        }
        if (local_unit.text.empty()) {
            if (out_error) *out_error = "Cannot generate PE executable with empty .text section";
            return false;
        }

        // 1. Locate entry point offset in .text
        uint32_t entry_offset = 0;
        bool entry_found = false;
        std::string target_entry = entry_point_name;
        if (target_entry.empty() || target_entry == "main") {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text && s.name == "_start") {
                    target_entry = "_start";
                    break;
                }
            }
        }
        if (!target_entry.empty()) {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text && s.name == target_entry) {
                    entry_offset = static_cast<uint32_t>(s.offset);
                    entry_found = true;
                    break;
                }
            }
        }
        if (!entry_found) {
            const char* candidates[] = {"_start", "main", "start", "WinMain"};
            for (const char* cand : candidates) {
                for (const auto& s : local_unit.symbols) {
                    if (!s.is_external && s.section == SectionKind::Text && s.name == cand) {
                        entry_offset = static_cast<uint32_t>(s.offset);
                        entry_found = true;
                        break;
                    }
                }
                if (entry_found) break;
            }
        }
        if (!entry_found) {
            for (const auto& s : local_unit.symbols) {
                if (!s.is_external && s.section == SectionKind::Text) {
                    entry_offset = static_cast<uint32_t>(s.offset);
                    entry_found = true;
                    break;
                }
            }
        }

        std::unordered_set<std::string> internal_syms;
        for (const auto& s : local_unit.symbols) {
            if (!s.is_external) internal_syms.insert(s.name);
        }

        // 2. Determine import libraries and symbols
        std::vector<WinLibraryImport> final_imports;
        if (!custom_imports.empty()) {
            final_imports = custom_imports;
        } else {
            std::unordered_set<std::string> needed_externals;
            for (const auto& r : local_unit.relocations) {
                if (internal_syms.find(r.symbol_name) == internal_syms.end()) {
                    std::string n = r.symbol_name;
                    if (n.rfind("__imp_", 0) == 0) n = n.substr(6);
                    if (internal_syms.find(n) == internal_syms.end()) {
                        needed_externals.insert(n);
                    }
                }
            }
            if (needed_externals.empty()) {
                needed_externals.insert("ExitProcess");
            }

            std::unordered_map<std::string, std::vector<std::string>> groups;
            std::vector<std::string> dlls;
            for (const auto& sym : needed_externals) {
                std::string dll = detect_dll_for_symbol(sym);
                if (groups.find(dll) == groups.end()) {
                    dlls.push_back(dll);
                }
                groups[dll].push_back(sym);
            }
            std::sort(dlls.begin(), dlls.end(), [](const std::string& a, const std::string& b) {
                auto prio = [](const std::string& s) {
                    if (s == "KERNEL32.dll") return 0;
                    if (s == "USER32.dll") return 1;
                    if (s == "WS2_32.dll") return 2;
                    if (s == "ADVAPI32.dll") return 3;
                    if (s == "msvcrt.dll") return 4;
                    return 5;
                };
                int pa = prio(a), pb = prio(b);
                if (pa != pb) return pa < pb;
                return a < b;
            });
            for (const auto& dll : dlls) {
                WinLibraryImport imp;
                imp.dll_name = dll;
                imp.symbols = groups[dll];
                std::sort(imp.symbols.begin(), imp.symbols.end());
                final_imports.push_back(std::move(imp));
            }
        }

        // 3. Prepare section buffers
        std::vector<uint8_t> text_data = local_unit.text;
        std::unordered_map<std::string, uint32_t> thunk_offsets;
        for (const auto& imp : final_imports) {
            for (const auto& sym : imp.symbols) {
                uint32_t thunk_start = static_cast<uint32_t>(text_data.size());
                thunk_offsets[sym] = thunk_start;
                thunk_offsets["__imp_" + sym] = thunk_start;
                // Emit: FF 25 00 00 00 00 (jmp qword ptr [rip + disp32])
                text_data.push_back(0xFF);
                text_data.push_back(0x25);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
                text_data.push_back(0x00);
            }
        }

        const uint64_t image_base = PE_DEFAULT_IMAGE_BASE;
        const uint32_t sec_align = PE_DEFAULT_SECTION_ALIGNMENT;
        const uint32_t file_align = PE_DEFAULT_FILE_ALIGNMENT;

        auto align_up = [](uint64_t val, uint64_t align) -> uint64_t {
            return (val + align - 1) / align * align;
        };

        // Layout RVAs and file offsets
        uint32_t text_rva = 0x1000;
        uint32_t text_file_offset = 0x200;
        uint32_t text_vsize = static_cast<uint32_t>(text_data.size());
        uint32_t text_raw_size = static_cast<uint32_t>(align_up(text_vsize, file_align));

        uint32_t rdata_rva = static_cast<uint32_t>(align_up(text_rva + text_vsize, sec_align));
        uint32_t rdata_file_offset = text_file_offset + text_raw_size;

        size_t rodata_raw_len = local_unit.rodata.size();
        size_t idata_offset_in_rdata = align_up(rodata_raw_len, 16);
        uint32_t idata_rva = static_cast<uint32_t>(rdata_rva + idata_offset_in_rdata);

        BinaryBuffer idata_buf;
        std::unordered_map<std::string, uint32_t> iat_offsets;
        uint32_t iat_rva = 0;
        uint32_t iat_size = 0;
        if (!generate_import_table(final_imports, idata_rva, idata_buf, iat_offsets, out_error, &iat_rva, &iat_size)) {
            return false;
        }

        std::vector<uint8_t> rdata_data = local_unit.rodata;
        if (idata_offset_in_rdata > rdata_data.size()) {
            rdata_data.resize(idata_offset_in_rdata, 0);
        }
        rdata_data.insert(rdata_data.end(), idata_buf.data(), idata_buf.data() + idata_buf.size());
        uint32_t rdata_vsize = static_cast<uint32_t>(rdata_data.size());
        uint32_t rdata_raw_size = static_cast<uint32_t>(align_up(rdata_vsize, file_align));

        uint32_t data_rva = static_cast<uint32_t>(align_up(rdata_rva + rdata_vsize, sec_align));
        uint32_t data_file_offset = rdata_file_offset + rdata_raw_size;
        std::vector<uint8_t> data_data = local_unit.data;
        uint32_t data_content_size = static_cast<uint32_t>(data_data.size());
        uint32_t data_vsize = std::max<uint32_t>(16, static_cast<uint32_t>(data_content_size + local_unit.bss_size));
        uint32_t data_raw_size = (data_content_size > 0) ? static_cast<uint32_t>(align_up(data_content_size, file_align)) : file_align;

        uint32_t size_of_image = static_cast<uint32_t>(align_up(data_rva + data_vsize, sec_align));

        // 4. Patch thunk displacements to point to IAT slots
        for (const auto& imp : final_imports) {
            for (const auto& sym : imp.symbols) {
                uint32_t thunk_start = thunk_offsets[sym];
                uint32_t disp_off = thunk_start + 2;
                uint64_t disp_va = image_base + text_rva + disp_off;
                uint64_t next_rip = disp_va + 4;
                uint64_t slot_va = image_base + iat_offsets[sym];
                int32_t disp32 = static_cast<int32_t>(static_cast<int64_t>(slot_va) - static_cast<int64_t>(next_rip));
                text_data[disp_off]     = static_cast<uint8_t>(disp32 & 0xFF);
                text_data[disp_off + 1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                text_data[disp_off + 2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                text_data[disp_off + 3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
            }
        }

        // 5. Build symbol address map
        std::unordered_map<std::string, uint64_t> sym_vas;
        for (const auto& s : local_unit.symbols) {
            if (s.is_external) continue;
            uint64_t va = 0;
            if (s.section == SectionKind::Text) va = image_base + text_rva + s.offset;
            else if (s.section == SectionKind::RoData) va = image_base + rdata_rva + s.offset;
            else if (s.section == SectionKind::Data) va = image_base + data_rva + s.offset;
            else if (s.section == SectionKind::Bss) va = image_base + data_rva + data_content_size + s.offset;
            sym_vas[s.name] = va;
        }
        for (const auto& [name, rva] : iat_offsets) {
            sym_vas["__imp_" + name] = image_base + rva;
            if (internal_syms.find(name) == internal_syms.end()) {
                auto it_thunk = thunk_offsets.find(name);
                if (it_thunk != thunk_offsets.end()) {
                    sym_vas[name] = image_base + text_rva + it_thunk->second;
                } else {
                    sym_vas[name] = image_base + rva;
                }
            }
        }

        // 6. Relocation patching
        for (const auto& rel : local_unit.relocations) {
            uint64_t site_va = 0;
            uint8_t* patch_ptr = nullptr;

            if (rel.section == SectionKind::Text) {
                if (rel.offset + 4 > text_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .text: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + text_rva + rel.offset;
                patch_ptr = text_data.data() + rel.offset;
            } else if (rel.section == SectionKind::RoData) {
                if (rel.offset + 4 > rdata_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .rodata: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + rdata_rva + rel.offset;
                patch_ptr = rdata_data.data() + rel.offset;
            } else if (rel.section == SectionKind::Data) {
                if (rel.offset + 4 > data_data.size()) {
                    if (out_error) *out_error = "Relocation offset out of range in .data: " + std::to_string(rel.offset);
                    return false;
                }
                site_va = image_base + data_rva + rel.offset;
                patch_ptr = data_data.data() + rel.offset;
            } else {
                if (out_error) *out_error = "Unsupported relocation section";
                return false;
            }

            // Direct CALL [rip + disp32] (FF 15) or JMP [rip + disp32] (FF 25)
            if (rel.section == SectionKind::Text && rel.offset >= 2 && patch_ptr) {
                uint8_t op0 = text_data[rel.offset - 2];
                uint8_t op1 = text_data[rel.offset - 1];
                if (op0 == 0xFF && (op1 == 0x15 || op1 == 0x25)) {
                    std::string clean = rel.symbol_name;
                    if (clean.rfind("__imp_", 0) == 0) clean = clean.substr(6);
                    auto it_iat = iat_offsets.find(clean);
                    if (it_iat != iat_offsets.end()) {
                        uint64_t slot_va = image_base + it_iat->second;
                        uint64_t next_rip = site_va + 4;
                        int32_t disp32 = static_cast<int32_t>(static_cast<int64_t>(slot_va) - static_cast<int64_t>(next_rip));
                        patch_ptr[0] = static_cast<uint8_t>(disp32 & 0xFF);
                        patch_ptr[1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                        patch_ptr[2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                        patch_ptr[3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
                        continue;
                    }
                }
            }

            // Standard relocation target lookup
            auto it_sym = sym_vas.find(rel.symbol_name);
            if (it_sym == sym_vas.end()) {
                std::string clean = rel.symbol_name;
                if (clean.rfind("__imp_", 0) == 0) clean = clean.substr(6);
                it_sym = sym_vas.find(clean);
                if (it_sym == sym_vas.end()) {
                    if (out_error) *out_error = "Unresolved symbol in PE executable: " + rel.symbol_name;
                    return false;
                }
            }
            uint64_t sym_va = it_sym->second;

            if (rel.type == RelocType::Rel32) {
                int64_t addend = (rel.addend == 0) ? -4 : rel.addend;
                int64_t disp = static_cast<int64_t>(sym_va + addend) - static_cast<int64_t>(site_va);
                int32_t disp32 = static_cast<int32_t>(disp);
                patch_ptr[0] = static_cast<uint8_t>(disp32 & 0xFF);
                patch_ptr[1] = static_cast<uint8_t>((disp32 >> 8) & 0xFF);
                patch_ptr[2] = static_cast<uint8_t>((disp32 >> 16) & 0xFF);
                patch_ptr[3] = static_cast<uint8_t>((disp32 >> 24) & 0xFF);
            } else if (rel.type == RelocType::Abs64) {
                if (rel.offset + 8 > ((rel.section == SectionKind::Data) ? data_data.size() : text_data.size())) {
                    if (out_error) *out_error = "Abs64 relocation out of range";
                    return false;
                }
                uint64_t abs_val = sym_va + rel.addend;
                for (size_t i = 0; i < 8; ++i) {
                    patch_ptr[i] = static_cast<uint8_t>((abs_val >> (i * 8)) & 0xFF);
                }
            } else if (rel.type == RelocType::Abs32) {
                uint32_t abs_val = static_cast<uint32_t>(sym_va + rel.addend);
                patch_ptr[0] = static_cast<uint8_t>(abs_val & 0xFF);
                patch_ptr[1] = static_cast<uint8_t>((abs_val >> 8) & 0xFF);
                patch_ptr[2] = static_cast<uint8_t>((abs_val >> 16) & 0xFF);
                patch_ptr[3] = static_cast<uint8_t>((abs_val >> 24) & 0xFF);
            }
        }

        // 7. Assemble PE executable file
        PE_DosHeader dos_hdr{};
        dos_hdr.e_magic = PE_DOS_MAGIC;
        dos_hdr.e_cblp = 0x0090;
        dos_hdr.e_cp = 0x0003;
        dos_hdr.e_cparhdr = 0x0004;
        dos_hdr.e_maxalloc = 0xFFFF;
        dos_hdr.e_sp = 0x00B8;
        dos_hdr.e_lfarlc = 0x0040;
        dos_hdr.e_lfanew = 0x0080;

        COFF_FileHeader file_hdr{};
        file_hdr.machine = COFF_MACHINE_AMD64;
        file_hdr.number_of_sections = 3;
        file_hdr.time_date_stamp = 0;
        file_hdr.pointer_to_symbol_table = 0;
        file_hdr.number_of_symbols = 0;
        file_hdr.size_of_optional_header = sizeof(PE32Plus_OptionalHeader);
        file_hdr.characteristics = 0x0022; // EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE

        PE32Plus_OptionalHeader opt_hdr{};
        opt_hdr.magic = PE_OPTIONAL_HDR64_MAGIC;
        opt_hdr.major_linker_version = 14;
        opt_hdr.minor_linker_version = 0;
        opt_hdr.size_of_code = text_raw_size;
        opt_hdr.size_of_initialized_data = rdata_raw_size + data_raw_size;
        opt_hdr.size_of_uninitialized_data = static_cast<uint32_t>(local_unit.bss_size);
        opt_hdr.address_of_entry_point = text_rva + entry_offset;
        opt_hdr.base_of_code = text_rva;
        opt_hdr.image_base = image_base;
        opt_hdr.section_alignment = sec_align;
        opt_hdr.file_alignment = file_align;
        opt_hdr.major_operating_system_version = 6;
        opt_hdr.minor_operating_system_version = 0;
        opt_hdr.major_image_version = 0;
        opt_hdr.minor_image_version = 0;
        opt_hdr.major_subsystem_version = 6;
        opt_hdr.minor_subsystem_version = 0;
        opt_hdr.win32_version_value = 0;
        opt_hdr.size_of_image = size_of_image;
        opt_hdr.size_of_headers = 0x200;
        opt_hdr.check_sum = 0;
        opt_hdr.subsystem = PE_SUBSYSTEM_WINDOWS_CUI;
        opt_hdr.dll_characteristics = 0x8100;
        opt_hdr.size_of_stack_reserve = 0x100000;
        opt_hdr.size_of_stack_commit = 0x1000;
        opt_hdr.size_of_heap_reserve = 0x100000;
        opt_hdr.size_of_heap_commit = 0x1000;
        opt_hdr.loader_flags = 0;
        opt_hdr.number_of_rva_and_sizes = 16;

        opt_hdr.data_directories[1].virtual_address = idata_rva;
        opt_hdr.data_directories[1].size = static_cast<uint32_t>((final_imports.size() + 1) * 20);
        opt_hdr.data_directories[12].virtual_address = iat_rva;
        opt_hdr.data_directories[12].size = iat_size;

        COFF_SectionHeader sec_text{};
        std::memcpy(sec_text.name, ".text\0\0\0", 8);
        sec_text.virtual_size = text_vsize;
        sec_text.virtual_address = text_rva;
        sec_text.size_of_raw_data = text_raw_size;
        sec_text.pointer_to_raw_data = text_file_offset;
        sec_text.characteristics = COFF_SCN_CNT_CODE | COFF_SCN_MEM_EXECUTE | COFF_SCN_MEM_READ;

        COFF_SectionHeader sec_rdata{};
        std::memcpy(sec_rdata.name, ".rdata\0\0", 8);
        sec_rdata.virtual_size = rdata_vsize;
        sec_rdata.virtual_address = rdata_rva;
        sec_rdata.size_of_raw_data = rdata_raw_size;
        sec_rdata.pointer_to_raw_data = rdata_file_offset;
        sec_rdata.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ;

        COFF_SectionHeader sec_data{};
        std::memcpy(sec_data.name, ".data\0\0\0", 8);
        sec_data.virtual_size = data_vsize;
        sec_data.virtual_address = data_rva;
        sec_data.size_of_raw_data = data_raw_size;
        sec_data.pointer_to_raw_data = data_file_offset;
        sec_data.characteristics = COFF_SCN_CNT_INITIALIZED_DATA | COFF_SCN_MEM_READ | COFF_SCN_MEM_WRITE;

        out.clear();
        out.emit_bytes(&dos_hdr, sizeof(dos_hdr));
        out.emit_bytes(s_pe_dos_stub, sizeof(s_pe_dos_stub));
        out.emit_u32(PE_SIGNATURE);
        out.emit_bytes(&file_hdr, sizeof(file_hdr));
        out.emit_bytes(&opt_hdr, sizeof(opt_hdr));
        out.emit_bytes(&sec_text, sizeof(sec_text));
        out.emit_bytes(&sec_rdata, sizeof(sec_rdata));
        out.emit_bytes(&sec_data, sizeof(sec_data));
        out.align_to(file_align, 0);

        out.emit_bytes(text_data.data(), text_data.size());
        out.align_to(file_align, 0);

        out.emit_bytes(rdata_data.data(), rdata_data.size());
        out.align_to(file_align, 0);

        if (!data_data.empty()) {
            out.emit_bytes(data_data.data(), data_data.size());
        }
        if (out.size() < data_file_offset + data_raw_size) {
            out.emit_zeros((data_file_offset + data_raw_size) - out.size());
        }

        return true;
    }

    bool GrassObjWriter::write_pe_executable_file(const CompiledUnit& unit,
                                                 const std::string& filename,
                                                 std::string* out_error,
                                                 const std::string& entry_point_name) {
        BinaryBuffer buf;
        if (!write_pe_executable(unit, buf, out_error, entry_point_name)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write PE executable to " + filename;
            return false;
        }
        return true;
    }

    bool GrassObjWriter::write_pe_executable_file(const CompiledUnit& unit,
                                                 const std::vector<WinLibraryImport>& custom_imports,
                                                 const std::string& filename,
                                                 std::string* out_error,
                                                 const std::string& entry_point_name) {
        BinaryBuffer buf;
        if (!write_pe_executable(unit, custom_imports, buf, out_error, entry_point_name)) {
            return false;
        }
        if (!buf.write_to_file(filename)) {
            if (out_error) *out_error = "Failed to write PE executable to " + filename;
            return false;
        }
        return true;
    }

} // namespace grass::backend
