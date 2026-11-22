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
                    "GetLocalTime", "QueryPerformanceCounter", "QueryPerformanceFrequency",
                    "Sleep", "VirtualAlloc", "VirtualFree"
                }
            },
            {
                "WS2_32.dll",
                {
                    "WSAStartup", "WSACleanup", "socket", "connect", "send", "recv", "closesocket"
                }
            },
            {
                "USER32.dll",
                {
                    "MessageBoxA"
                }
            }
        };
    }

    bool GrassObjWriter::generate_import_table(const std::vector<WinLibraryImport>& imports,
                                              uint32_t idata_rva,
                                              BinaryBuffer& out_idata,
                                              std::unordered_map<std::string, uint32_t>& out_iat_offsets,
                                              std::string* out_error) {
        (void)out_error;
        if (imports.empty()) {
            out_idata.clear();
            out_iat_offsets.clear();
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

        return true;
    }

} // namespace grass::backend
