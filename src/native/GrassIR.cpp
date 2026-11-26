#include "native/GrassIR.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace grass::ir {

    // =========================================================================
    // 1. IRType Implementation
    // =========================================================================

    bool IRType::is_integer() const {
        return kind == TypeKind::I1 || kind == TypeKind::I8 ||
               kind == TypeKind::I16 || kind == TypeKind::I32 ||
               kind == TypeKind::I64;
    }

    bool IRType::is_floating_point() const {
        return kind == TypeKind::F32 || kind == TypeKind::F64;
    }

    uint32_t IRType::bit_width() const {
        switch (kind) {
            case TypeKind::Void: return 0;
            case TypeKind::I1:   return 1;
            case TypeKind::I8:   return 8;
            case TypeKind::I16:  return 16;
            case TypeKind::I32:  return 32;
            case TypeKind::I64:  return 64;
            case TypeKind::F32:  return 32;
            case TypeKind::F64:  return 64;
            case TypeKind::Ptr:  return 64;
            case TypeKind::Array:
                return element_type ? static_cast<uint32_t>(element_type->bit_width() * array_len) : 0;
            case TypeKind::Struct:
                return static_cast<uint32_t>(cached_size * 8);
            default: return 0;
        }
    }

    int32_t IRType::get_field_index(const std::string& field_name) const {
        if (kind != TypeKind::Struct) return -1;
        for (size_t i = 0; i < struct_fields.size(); ++i) {
            if (struct_fields[i].name == field_name) {
                return static_cast<int32_t>(i);
            }
        }
        return -1;
    }

    uint64_t IRType::get_field_offset(uint32_t index) const {
        if (kind != TypeKind::Struct || index >= struct_fields.size()) return 0;
        return struct_fields[index].offset;
    }

    IRType* IRType::get_field_type(uint32_t index) const {
        if (kind != TypeKind::Struct || index >= struct_fields.size()) return nullptr;
        return struct_fields[index].type;
    }

    std::string IRType::to_string() const {
        switch (kind) {
            case TypeKind::Void: return "void";
            case TypeKind::I1:   return "i1";
            case TypeKind::I8:   return "i8";
            case TypeKind::I16:  return "i16";
            case TypeKind::I32:  return "i32";
            case TypeKind::I64:  return "i64";
            case TypeKind::F32:  return "f32";
            case TypeKind::F64:  return "f64";
            case TypeKind::Ptr:  return "ptr";
            case TypeKind::Array:
                return "[" + std::to_string(array_len) + " x " +
                       (element_type ? element_type->to_string() : "unknown") + "]";
            case TypeKind::Vec:
                return "<" + std::to_string(array_len) + " x " +
                       (element_type ? element_type->to_string() : "unknown") + ">";
            case TypeKind::Struct: {
                if (!struct_name.empty()) return "%" + struct_name;
                std::string res = "{ ";
                for (size_t i = 0; i < struct_fields.size(); ++i) {
                    if (i > 0) res += ", ";
                    res += struct_fields[i].type ? struct_fields[i].type->to_string() : "unknown";
                }
                res += " }";
                return res;
            }
            default: return "unknown";
        }
    }

    bool IRType::equals(const IRType* other) const {
        if (!other) return false;
        if (this == other) return true;
        if (kind != other->kind) return false;

        if (kind == TypeKind::Array || kind == TypeKind::Vec) {
            return array_len == other->array_len &&
                   ((!element_type && !other->element_type) ||
                    (element_type && element_type->equals(other->element_type)));
        }
        if (kind == TypeKind::Struct) {
            if (!struct_name.empty() && !other->struct_name.empty()) {
                return struct_name == other->struct_name;
            }
            if (struct_fields.size() != other->struct_fields.size()) return false;
            for (size_t i = 0; i < struct_fields.size(); ++i) {
                if (!struct_fields[i].type->equals(other->struct_fields[i].type)) {
                    return false;
                }
            }
            return true;
        }
        return true;
    }

    // =========================================================================
    // IRContext Implementation
    // =========================================================================

    IRContext::IRContext() {
        auto make_prim = [this](TypeKind k, uint64_t sz, uint64_t al) -> IRType* {
            auto t = std::make_unique<IRType>();
            t->kind = k;
            t->cached_size = sz;
            t->cached_align = al;
            IRType* raw = t.get();
            types_storage_.push_back(std::move(t));
            return raw;
        };

        type_void_ = make_prim(TypeKind::Void, 0, 1);
        type_i1_   = make_prim(TypeKind::I1, 1, 1);
        type_i8_   = make_prim(TypeKind::I8, 1, 1);
        type_i16_  = make_prim(TypeKind::I16, 2, 2);
        type_i32_  = make_prim(TypeKind::I32, 4, 4);
        type_i64_  = make_prim(TypeKind::I64, 8, 8);
        type_f32_  = make_prim(TypeKind::F32, 4, 4);
        type_f64_  = make_prim(TypeKind::F64, 8, 8);
        type_ptr_  = make_prim(TypeKind::Ptr, 8, 8);
    }

    IRType* IRContext::get_void_type() { return type_void_; }
    IRType* IRContext::get_i1_type()   { return type_i1_; }
    IRType* IRContext::get_i8_type()   { return type_i8_; }
    IRType* IRContext::get_i16_type()  { return type_i16_; }
    IRType* IRContext::get_i32_type()  { return type_i32_; }
    IRType* IRContext::get_i64_type()  { return type_i64_; }
    IRType* IRContext::get_f32_type()  { return type_f32_; }
    IRType* IRContext::get_f64_type()  { return type_f64_; }
    IRType* IRContext::get_ptr_type(IRType* pointee) {
        if (!pointee) return type_ptr_;
        for (const auto& t : types_storage_) {
            if (t->kind == TypeKind::Ptr && t->element_type == pointee) {
                return t.get();
            }
        }
        auto t = std::make_unique<IRType>();
        t->kind = TypeKind::Ptr;
        t->element_type = pointee;
        t->cached_size = 8;
        t->cached_align = 8;
        IRType* raw = t.get();
        types_storage_.push_back(std::move(t));
        return raw;
    }

    IRType* IRContext::get_array_type(IRType* elem, uint64_t length) {
        for (const auto& t : types_storage_) {
            if (t->kind == TypeKind::Array && t->element_type == elem && t->array_len == length) {
                return t.get();
            }
        }
        auto t = std::make_unique<IRType>();
        t->kind = TypeKind::Array;
        t->element_type = elem;
        t->array_len = length;
        t->cached_align = elem ? elem->alignment() : 1;
        t->cached_size = elem ? elem->size() * length : 0;
        IRType* raw = t.get();
        types_storage_.push_back(std::move(t));
        return raw;
    }

    IRType* IRContext::get_vec_type(IRType* elem, uint64_t lanes) {
        for (const auto& t : types_storage_) {
            if (t->kind == TypeKind::Vec && t->element_type == elem && t->array_len == lanes) {
                return t.get();
            }
        }
        auto t = std::make_unique<IRType>();
        t->kind = TypeKind::Vec;
        t->element_type = elem;
        t->array_len = lanes;
        t->cached_size = elem ? elem->size() * lanes : 0;
        t->cached_align = t->cached_size > 0 ? t->cached_size : 16;
        IRType* raw = t.get();
        types_storage_.push_back(std::move(t));
        return raw;
    }

    IRType* IRContext::get_struct_type(const std::string& name,
                                       const std::vector<IRType*>& field_types,
                                       const std::vector<std::string>& field_names) {
        auto t = std::make_unique<IRType>();
        t->kind = TypeKind::Struct;
        t->struct_name = name;

        uint64_t current_offset = 0;
        uint64_t max_align = 1;

        for (size_t i = 0; i < field_types.size(); ++i) {
            IRType* ft = field_types[i];
            uint64_t field_align = ft ? ft->alignment() : 1;
            if (field_align > max_align) max_align = field_align;

            if (field_align > 0 && (current_offset % field_align) != 0) {
                current_offset += field_align - (current_offset % field_align);
            }

            StructField field;
            field.name = (i < field_names.size()) ? field_names[i] : "field" + std::to_string(i);
            field.type = ft;
            field.offset = current_offset;
            t->struct_fields.push_back(field);

            current_offset += ft ? ft->size() : 0;
        }

        if (max_align > 0 && (current_offset % max_align) != 0) {
            current_offset += max_align - (current_offset % max_align);
        }

        t->cached_size = current_offset;
        t->cached_align = max_align;

        IRType* raw = t.get();
        types_storage_.push_back(std::move(t));
        return raw;
    }

    // =========================================================================
    // String Helpers for Enums
    // =========================================================================

    std::string to_string(IROp op) {
        switch (op) {
            case IROp::ConstInt:      return "const_int";
            case IROp::ConstFloat:    return "const_float";
            case IROp::Copy:          return "copy";
            case IROp::Phi:           return "phi";
            case IROp::Add:           return "add";
            case IROp::Sub:           return "sub";
            case IROp::Mul:           return "mul";
            case IROp::SDiv:          return "sdiv";
            case IROp::UDiv:          return "udiv";
            case IROp::SRem:          return "srem";
            case IROp::URem:          return "urem";
            case IROp::Neg:           return "neg";
            case IROp::FAdd:          return "fadd";
            case IROp::FSub:          return "fsub";
            case IROp::FMul:          return "fmul";
            case IROp::FDiv:          return "fdiv";
            case IROp::FNeg:          return "fneg";
            case IROp::And:           return "and";
            case IROp::Or:            return "or";
            case IROp::Xor:           return "xor";
            case IROp::Not:           return "not";
            case IROp::Shl:           return "shl";
            case IROp::LShr:          return "lshr";
            case IROp::AShr:          return "ashr";
            case IROp::ICmp:          return "icmp";
            case IROp::FCmp:          return "fcmp";
            case IROp::ZExt:          return "zext";
            case IROp::SExt:          return "sext";
            case IROp::Trunc:         return "trunc";
            case IROp::SIToFP:        return "sitofp";
            case IROp::FPToSI:        return "fptosi";
            case IROp::Bitcast:       return "bitcast";
            case IROp::PtrToInt:      return "ptrtoint";
            case IROp::IntToPtr:      return "inttoptr";
            case IROp::Alloca:        return "alloca";
            case IROp::Load:          return "load";
            case IROp::Store:         return "store";
            case IROp::GetElementPtr: return "getelementptr";
            case IROp::MemCpy:        return "memcpy";
            case IROp::MemSet:        return "memset";
            case IROp::Jump:          return "jmp";
            case IROp::Branch:        return "br";
            case IROp::Call:          return "call";
            case IROp::Ret:           return "ret";
            case IROp::VecZero:       return "vec_zero";
            case IROp::VecSplat:      return "vec_splat";
            case IROp::VecLoad:       return "vec_load";
            case IROp::VecStore:      return "vec_store";
            case IROp::VecAdd:        return "vec_add";
            case IROp::VecSub:        return "vec_sub";
            case IROp::VecMul:        return "vec_mul";
            case IROp::VecHAdd:       return "vec_hadd";
            default:                  return "unknown_op";
        }
    }

    std::string to_string(ICmpCond cond) {
        switch (cond) {
            case ICmpCond::EQ:  return "eq";
            case ICmpCond::NE:  return "ne";
            case ICmpCond::SLT: return "slt";
            case ICmpCond::SLE: return "sle";
            case ICmpCond::SGT: return "sgt";
            case ICmpCond::SGE: return "sge";
            case ICmpCond::ULT: return "ult";
            case ICmpCond::ULE: return "ule";
            case ICmpCond::UGT: return "ugt";
            case ICmpCond::UGE: return "uge";
            default:            return "unk_icmp";
        }
    }

    std::string to_string(FCmpCond cond) {
        switch (cond) {
            case FCmpCond::OEQ: return "oeq";
            case FCmpCond::ONE: return "one";
            case FCmpCond::OLT: return "olt";
            case FCmpCond::OLE: return "ole";
            case FCmpCond::OGT: return "ogt";
            case FCmpCond::OGE: return "oge";
            default:            return "unk_fcmp";
        }
    }

    std::string to_string(PhysReg reg) {
        switch (reg) {
            case PhysReg::RAX: return "rax";
            case PhysReg::RCX: return "rcx";
            case PhysReg::RDX: return "rdx";
            case PhysReg::RBX: return "rbx";
            case PhysReg::RSP: return "rsp";
            case PhysReg::RBP: return "rbp";
            case PhysReg::RSI: return "rsi";
            case PhysReg::RDI: return "rdi";
            case PhysReg::R8:  return "r8";
            case PhysReg::R9:  return "r9";
            case PhysReg::R10: return "r10";
            case PhysReg::R11: return "r11";
            case PhysReg::R12: return "r12";
            case PhysReg::R13: return "r13";
            case PhysReg::R14: return "r14";
            case PhysReg::R15: return "r15";
            case PhysReg::XMM0:  return "xmm0";
            case PhysReg::XMM1:  return "xmm1";
            case PhysReg::XMM2:  return "xmm2";
            case PhysReg::XMM3:  return "xmm3";
            case PhysReg::XMM4:  return "xmm4";
            case PhysReg::XMM5:  return "xmm5";
            case PhysReg::XMM6:  return "xmm6";
            case PhysReg::XMM7:  return "xmm7";
            case PhysReg::XMM8:  return "xmm8";
            case PhysReg::XMM9:  return "xmm9";
            case PhysReg::XMM10: return "xmm10";
            case PhysReg::XMM11: return "xmm11";
            case PhysReg::XMM12: return "xmm12";
            case PhysReg::XMM13: return "xmm13";
            case PhysReg::XMM14: return "xmm14";
            case PhysReg::XMM15: return "xmm15";
            default:             return "none";
        }
    }

    // =========================================================================
    // 2. IRInst Implementation
    // =========================================================================

    bool IRInst::is_terminator() const {
        return op == IROp::Jump || op == IROp::Branch || op == IROp::Ret;
    }

    bool IRInst::is_binary_arithmetic() const {
        return op == IROp::Add || op == IROp::Sub || op == IROp::Mul ||
               op == IROp::SDiv || op == IROp::UDiv || op == IROp::SRem ||
               op == IROp::URem || op == IROp::FAdd || op == IROp::FSub ||
               op == IROp::FMul || op == IROp::FDiv;
    }

    bool IRInst::is_bitwise() const {
        return op == IROp::And || op == IROp::Or || op == IROp::Xor ||
               op == IROp::Not || op == IROp::Shl || op == IROp::LShr ||
               op == IROp::AShr;
    }

    bool IRInst::is_comparison() const {
        return op == IROp::ICmp || op == IROp::FCmp;
    }

    bool IRInst::is_cast() const {
        return op == IROp::ZExt || op == IROp::SExt || op == IROp::Trunc ||
               op == IROp::SIToFP || op == IROp::FPToSI || op == IROp::Bitcast ||
               op == IROp::PtrToInt || op == IROp::IntToPtr;
    }

    bool IRInst::is_memory() const {
        return op == IROp::Alloca || op == IROp::Load || op == IROp::Store ||
               op == IROp::GetElementPtr || op == IROp::MemCpy || op == IROp::MemSet ||
               op == IROp::VecLoad || op == IROp::VecStore;
    }

    bool IRInst::has_side_effects() const {
        return op == IROp::Store || op == IROp::Call || op == IROp::Jump ||
               op == IROp::Branch || op == IROp::Ret || op == IROp::MemCpy ||
               op == IROp::MemSet || op == IROp::VecStore;
    }

    bool IRInst::is_commutative() const {
        if (op == IROp::Add || op == IROp::Mul || op == IROp::FAdd ||
            op == IROp::FMul || op == IROp::And || op == IROp::Or ||
            op == IROp::Xor || op == IROp::VecAdd || op == IROp::VecMul) {
            return true;
        }
        if (op == IROp::ICmp && (icmp_cond == ICmpCond::EQ || icmp_cond == ICmpCond::NE)) {
            return true;
        }
        if (op == IROp::FCmp && (fcmp_cond == FCmpCond::OEQ || fcmp_cond == FCmpCond::ONE)) {
            return true;
        }
        return false;
    }

    void IRInst::add_phi_incoming(BasicBlock* block, ValueId val) {
        for (auto& in : phi_incoming) {
            if (in.block == block) {
                in.value = val;
                return;
            }
        }
        phi_incoming.push_back({block, val});
    }

    void IRInst::remove_phi_incoming(BasicBlock* block) {
        phi_incoming.erase(
            std::remove_if(phi_incoming.begin(), phi_incoming.end(),
                           [block](const PhiIncoming& in) { return in.block == block; }),
            phi_incoming.end());
    }

    ValueId IRInst::get_phi_incoming_value(BasicBlock* block) const {
        for (const auto& in : phi_incoming) {
            if (in.block == block) return in.value;
        }
        return kInvalidValueId;
    }

    void IRInst::replace_operand(ValueId old_val, ValueId new_val) {
        for (auto& opnd : operands) {
            if (opnd == old_val) opnd = new_val;
        }
        for (auto& in : phi_incoming) {
            if (in.value == old_val) in.value = new_val;
        }
    }

    bool IRInst::uses_value(ValueId val) const {
        for (ValueId opnd : operands) {
            if (opnd == val) return true;
        }
        for (const auto& in : phi_incoming) {
            if (in.value == val) return true;
        }
        return false;
    }

    std::string IRInst::to_string() const {
        std::ostringstream ss;
        if (result_id != kInvalidValueId) {
            ss << "%" << result_id << " = ";
        }

        switch (op) {
            case IROp::ConstInt:
                ss << "const " << (type ? type->to_string() : "i64") << " " << int_imm;
                break;
            case IROp::ConstFloat:
                ss << "const " << (type ? type->to_string() : "f64") << " " << float_imm;
                break;
            case IROp::Copy:
                ss << "copy " << (type ? type->to_string() : "") << " %" << (operands.empty() ? 0 : operands[0]);
                break;
            case IROp::Phi: {
                ss << "phi " << (type ? type->to_string() : "") << " ";
                for (size_t i = 0; i < phi_incoming.size(); ++i) {
                    if (i > 0) ss << ", ";
                    ss << "[ %" << phi_incoming[i].value << ", %"
                       << (phi_incoming[i].block ? phi_incoming[i].block->name : "null") << " ]";
                }
                break;
            }
            case IROp::Add:
            case IROp::Sub:
            case IROp::Mul:
            case IROp::SDiv:
            case IROp::UDiv:
            case IROp::SRem:
            case IROp::URem:
            case IROp::FAdd:
            case IROp::FSub:
            case IROp::FMul:
            case IROp::FDiv:
            case IROp::And:
            case IROp::Or:
            case IROp::Xor:
            case IROp::Shl:
            case IROp::LShr:
            case IROp::AShr:
                ss << ir::to_string(op) << " " << (type ? type->to_string() : "") << " ";
                if (operands.size() >= 2) {
                    ss << "%" << operands[0] << ", %" << operands[1];
                }
                break;
            case IROp::Neg:
            case IROp::FNeg:
            case IROp::Not:
                ss << ir::to_string(op) << " " << (type ? type->to_string() : "") << " ";
                if (!operands.empty()) ss << "%" << operands[0];
                break;
            case IROp::ICmp:
                ss << "icmp " << ir::to_string(icmp_cond) << " ";
                if (operands.size() >= 2) ss << "%" << operands[0] << ", %" << operands[1];
                break;
            case IROp::FCmp:
                ss << "fcmp " << ir::to_string(fcmp_cond) << " ";
                if (operands.size() >= 2) ss << "%" << operands[0] << ", %" << operands[1];
                break;
            case IROp::ZExt:
            case IROp::SExt:
            case IROp::Trunc:
            case IROp::SIToFP:
            case IROp::FPToSI:
            case IROp::Bitcast:
            case IROp::PtrToInt:
            case IROp::IntToPtr:
                ss << ir::to_string(op) << " ";
                if (!operands.empty()) ss << "%" << operands[0];
                ss << " to " << (type ? type->to_string() : "");
                break;
            case IROp::Alloca:
                ss << "alloca " << (allocated_type ? allocated_type->to_string() : "");
                if (alloca_count > 1) ss << ", count " << alloca_count;
                if (alloca_align > 0) ss << ", align " << alloca_align;
                break;
            case IROp::Load:
                ss << "load " << (type ? type->to_string() : "");
                if (!operands.empty()) ss << ", ptr %" << operands[0];
                break;
            case IROp::Store:
                ss << "store ";
                if (operands.size() >= 2) {
                    ss << "%" << operands[0] << ", ptr %" << operands[1];
                }
                break;
            case IROp::GetElementPtr:
                ss << "gep ";
                if (!operands.empty()) ss << "ptr %" << operands[0];
                for (size_t i = 1; i < operands.size(); ++i) {
                    ss << ", %" << operands[i];
                }
                if (gep_field_index >= 0) {
                    ss << ", field " << gep_field_index;
                }
                break;
            case IROp::MemCpy:
                ss << "memcpy ";
                if (operands.size() >= 3) {
                    ss << "dst %" << operands[0] << ", src %" << operands[1] << ", size %" << operands[2];
                }
                break;
            case IROp::MemSet:
                ss << "memset ";
                if (operands.size() >= 3) {
                    ss << "dst %" << operands[0] << ", val %" << operands[1] << ", size %" << operands[2];
                }
                break;
            case IROp::Jump:
                ss << "jmp label %" << (true_target ? true_target->name : "null");
                break;
            case IROp::Branch:
                ss << "br i1 ";
                if (!operands.empty()) ss << "%" << operands[0];
                ss << ", label %" << (true_target ? true_target->name : "null")
                   << ", label %" << (false_target ? false_target->name : "null");
                break;
            case IROp::Call:
                if (is_indirect_call) {
                    ss << "call " << (type ? type->to_string() : "void") << " %"
                       << (operands.empty() ? 0 : operands[0]) << "(";
                    for (size_t i = 1; i < operands.size(); ++i) {
                        if (i > 1) ss << ", ";
                        ss << "%" << operands[i];
                    }
                    ss << ")";
                } else {
                    ss << "call " << (type ? type->to_string() : "void") << " @" << callee_name << "(";
                    for (size_t i = 0; i < operands.size(); ++i) {
                        if (i > 0) ss << ", ";
                        ss << "%" << operands[i];
                    }
                    ss << ")";
                }
                break;
            case IROp::Ret:
                ss << "ret";
                if (!operands.empty()) {
                    ss << " " << (type ? type->to_string() : "") << " %" << operands[0];
                } else {
                    ss << " void";
                }
                break;
            default:
                ss << ir::to_string(op);
                break;
        }

        return ss.str();
    }

    // =========================================================================
    // 3. BasicBlock Implementation
    // =========================================================================

    BasicBlock::BasicBlock(std::string name_in, IRFunction* parent_in)
        : name(std::move(name_in)), parent(parent_in) {}

    IRInst* BasicBlock::get_terminator() const {
        if (instructions.empty()) return nullptr;
        IRInst* last = instructions.back().get();
        return last->is_terminator() ? last : nullptr;
    }

    bool BasicBlock::has_terminator() const {
        return get_terminator() != nullptr;
    }

    void BasicBlock::push_back(std::unique_ptr<IRInst> inst) {
        inst->parent = this;
        instructions.push_back(std::move(inst));
    }

    void BasicBlock::push_front(std::unique_ptr<IRInst> inst) {
        inst->parent = this;
        instructions.insert(instructions.begin(), std::move(inst));
    }

    std::vector<std::unique_ptr<IRInst>>::iterator BasicBlock::insert(
        std::vector<std::unique_ptr<IRInst>>::iterator pos,
        std::unique_ptr<IRInst> inst) {
        inst->parent = this;
        return instructions.insert(pos, std::move(inst));
    }

    bool BasicBlock::erase(IRInst* inst) {
        for (auto it = instructions.begin(); it != instructions.end(); ++it) {
            if (it->get() == inst) {
                instructions.erase(it);
                return true;
            }
        }
        return false;
    }

    void BasicBlock::erase(size_t index) {
        if (index < instructions.size()) {
            instructions.erase(instructions.begin() + index);
        }
    }

    void BasicBlock::add_pred(BasicBlock* pred) {
        if (std::find(preds.begin(), preds.end(), pred) == preds.end()) {
            preds.push_back(pred);
        }
    }

    void BasicBlock::remove_pred(BasicBlock* pred) {
        preds.erase(std::remove(preds.begin(), preds.end(), pred), preds.end());
    }

    void BasicBlock::add_succ(BasicBlock* succ) {
        if (std::find(succs.begin(), succs.end(), succ) == succs.end()) {
            succs.push_back(succ);
        }
    }

    void BasicBlock::remove_succ(BasicBlock* succ) {
        succs.erase(std::remove(succs.begin(), succs.end(), succ), succs.end());
    }

    std::string BasicBlock::dump() const {
        std::ostringstream ss;
        ss << name << ":\n";
        for (const auto& inst : instructions) {
            ss << "    " << inst->to_string() << "\n";
        }
        return ss.str();
    }


} // namespace grass::ir
