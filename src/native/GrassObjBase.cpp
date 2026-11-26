#include "native/GrassObjWriter.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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


} // namespace grass::backend
