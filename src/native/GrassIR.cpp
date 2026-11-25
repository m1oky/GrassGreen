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

        if (kind == TypeKind::Array) {
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
               op == IROp::GetElementPtr || op == IROp::MemCpy || op == IROp::MemSet;
    }

    bool IRInst::has_side_effects() const {
        return op == IROp::Store || op == IROp::Call || op == IROp::Jump ||
               op == IROp::Branch || op == IROp::Ret || op == IROp::MemCpy ||
               op == IROp::MemSet;
    }

    bool IRInst::is_commutative() const {
        if (op == IROp::Add || op == IROp::Mul || op == IROp::FAdd ||
            op == IROp::FMul || op == IROp::And || op == IROp::Or ||
            op == IROp::Xor) {
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

    // =========================================================================
    // 4. IRFunction Implementation
    // =========================================================================

    IRFunction::IRFunction(std::string name_in, IRType* ret_type_in, IRModule* parent_in)
        : name(std::move(name_in)), return_type(ret_type_in), parent(parent_in) {}

    ValueId IRFunction::allocate_value_id() {
        return next_value_id++;
    }

    ValueId IRFunction::create_value(IRType* type, const std::string& val_name, IRInst* def) {
        ValueId id = allocate_value_id();
        IRValue v;
        v.id = id;
        v.type = type;
        v.name = val_name.empty() ? ("val" + std::to_string(id)) : val_name;
        v.def_inst = def;
        values[id] = v;
        return id;
    }

    static IRValue kDummyInvalidValue{};

    IRValue& IRFunction::get_value(ValueId id) {
        auto it = values.find(id);
        if (it != values.end()) return it->second;
        return kDummyInvalidValue;
    }

    const IRValue& IRFunction::get_value(ValueId id) const {
        auto it = values.find(id);
        if (it != values.end()) return it->second;
        return kDummyInvalidValue;
    }

    bool IRFunction::has_value(ValueId id) const {
        return values.find(id) != values.end();
    }

    void IRFunction::add_argument(const std::string& arg_name, IRType* type) {
        ValueId id = create_value(type, arg_name, nullptr);
        IRValue& val = get_value(id);
        val.is_argument = true;
        val.arg_index = static_cast<uint32_t>(arguments.size());
        arguments.push_back(val);
    }

    BasicBlock* IRFunction::create_block(const std::string& blk_name) {
        std::string final_name = blk_name.empty() ? ("bb" + std::to_string(blocks.size())) : blk_name;
        auto bb = std::make_unique<BasicBlock>(final_name, this);
        BasicBlock* raw = bb.get();
        blocks.push_back(std::move(bb));
        return raw;
    }

    BasicBlock* IRFunction::get_entry_block() const {
        return blocks.empty() ? nullptr : blocks.front().get();
    }

    void IRFunction::replace_all_uses_with(ValueId old_val, ValueId new_val) {
        if (old_val == new_val) return;
        if (!has_value(old_val)) return;

        std::vector<IRInst*> old_users = values[old_val].users;
        for (IRInst* user : old_users) {
            user->replace_operand(old_val, new_val);
            if (has_value(new_val)) {
                auto& new_users = values[new_val].users;
                if (std::find(new_users.begin(), new_users.end(), user) == new_users.end()) {
                    new_users.push_back(user);
                }
            }
        }
        values[old_val].users.clear();
    }

    void IRFunction::erase_inst(IRInst* inst) {
        if (!inst) return;
        for (ValueId opnd : inst->operands) {
            if (has_value(opnd)) {
                auto& u = values[opnd].users;
                u.erase(std::remove(u.begin(), u.end(), inst), u.end());
            }
        }
        for (const auto& in : inst->phi_incoming) {
            if (has_value(in.value)) {
                auto& u = values[in.value].users;
                u.erase(std::remove(u.begin(), u.end(), inst), u.end());
            }
        }
        if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
            values[inst->result_id].def_inst = nullptr;
        }
        if (inst->parent) {
            inst->parent->erase(inst);
        }
    }

    void IRFunction::erase_block(BasicBlock* bb) {
        if (!bb) return;
        for (auto& inst : bb->instructions) {
            if (!inst) continue;
            for (ValueId opnd : inst->operands) {
                if (has_value(opnd)) {
                    auto& u = values[opnd].users;
                    u.erase(std::remove(u.begin(), u.end(), inst.get()), u.end());
                }
            }
            for (const auto& in : inst->phi_incoming) {
                if (has_value(in.value)) {
                    auto& u = values[in.value].users;
                    u.erase(std::remove(u.begin(), u.end(), inst.get()), u.end());
                }
            }
            if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
                values[inst->result_id].def_inst = nullptr;
            }
        }
        bb->instructions.clear();
        for (BasicBlock* s : bb->succs) {
            for (auto& inst : s->instructions) {
                if (inst->op != IROp::Phi) break;
                inst->remove_phi_incoming(bb);
            }
            s->remove_pred(bb);
        }
        for (BasicBlock* p : bb->preds) {
            p->remove_succ(bb);
        }
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                blocks.erase(it);
                break;
            }
        }
    }

    void IRFunction::rebuild_cfg() {
        for (auto& bb : blocks) {
            bb->preds.clear();
            bb->succs.clear();
        }

        for (auto& bb : blocks) {
            IRInst* term = bb->get_terminator();
            if (!term) continue;

            if (term->op == IROp::Jump && term->true_target) {
                bb->add_succ(term->true_target);
                term->true_target->add_pred(bb.get());
            } else if (term->op == IROp::Branch) {
                if (term->true_target) {
                    bb->add_succ(term->true_target);
                    term->true_target->add_pred(bb.get());
                }
                if (term->false_target) {
                    bb->add_succ(term->false_target);
                    term->false_target->add_pred(bb.get());
                }
            }
        }
    }

    void IRFunction::rebuild_def_use() {
        for (auto& [id, val] : values) {
            val.users.clear();
            val.def_inst = nullptr;
        }

        for (auto& bb : blocks) {
            for (auto& inst : bb->instructions) {
                if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
                    values[inst->result_id].def_inst = inst.get();
                }
                for (ValueId opnd : inst->operands) {
                    if (has_value(opnd)) {
                        auto& u = values[opnd].users;
                        if (std::find(u.begin(), u.end(), inst.get()) == u.end()) {
                            u.push_back(inst.get());
                        }
                    }
                }
                for (const auto& in : inst->phi_incoming) {
                    if (has_value(in.value)) {
                        auto& u = values[in.value].users;
                        if (std::find(u.begin(), u.end(), inst.get()) == u.end()) {
                            u.push_back(inst.get());
                        }
                    }
                }
            }
        }
    }

    bool IRFunction::verify(std::string& error_out) const {
        if (is_declaration || blocks.empty()) {
            return true;
        }

        for (size_t b_idx = 0; b_idx < blocks.size(); ++b_idx) {
            const auto& bb = blocks[b_idx];
            if (bb->instructions.empty()) {
                error_out = "Block '" + bb->name + "' has no instructions.";
                return false;
            }

            for (size_t i = 0; i < bb->instructions.size(); ++i) {
                const auto& inst = bb->instructions[i];
                bool is_last = (i == bb->instructions.size() - 1);

                if (inst->is_terminator()) {
                    if (!is_last) {
                        error_out = "Terminator instruction in middle of block '" + bb->name + "'";
                        return false;
                    }
                } else if (is_last) {
                    error_out = "Block '" + bb->name + "' does not end with a terminator.";
                    return false;
                }

                if (inst->op == IROp::Phi) {
                    if (i > 0 && bb->instructions[i - 1]->op != IROp::Phi) {
                        error_out = "Phi instruction not at start of block '" + bb->name + "'";
                        return false;
                    }

                    if (inst->phi_incoming.size() != bb->preds.size()) {
                        error_out = "Phi in '" + bb->name + "' has " + std::to_string(inst->phi_incoming.size()) +
                                    " incoming branches, but block has " + std::to_string(bb->preds.size()) + " predecessors.";
                        return false;
                    }

                    std::unordered_set<BasicBlock*> seen_preds;
                    for (const auto& in : inst->phi_incoming) {
                        if (!in.block || std::find(bb->preds.begin(), bb->preds.end(), in.block) == bb->preds.end()) {
                            error_out = "Phi in '" + bb->name + "' has incoming block that is not a CFG predecessor.";
                            return false;
                        }
                        if (!seen_preds.insert(in.block).second) {
                            error_out = "Phi in '" + bb->name + "' has duplicate incoming entry for block '" + in.block->name + "'";
                            return false;
                        }
                        if (has_value(in.value)) {
                            const IRValue& in_val = get_value(in.value);
                            if (inst->type && in_val.type && !inst->type->equals(in_val.type)) {
                                error_out = "Phi type mismatch in block '" + bb->name + "': expected " +
                                            inst->type->to_string() + " but got " + in_val.type->to_string();
                                return false;
                            }
                        }
                    }
                }

                for (ValueId opnd : inst->operands) {
                    if (!has_value(opnd)) {
                        error_out = "Instruction " + ir::to_string(inst->op) + " uses undefined ValueId %" + std::to_string(opnd) +
                                    " in block '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Branch) {
                    if (inst->operands.empty() || !has_value(inst->operands[0])) {
                        error_out = "Branch without condition in '" + bb->name + "'";
                        return false;
                    }
                    if (!get_value(inst->operands[0]).type->is_bool()) {
                        error_out = "Branch condition is not i1 in '" + bb->name + "'";
                        return false;
                    }
                    if (!inst->true_target || !inst->false_target) {
                        error_out = "Branch missing target in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Jump && !inst->true_target) {
                    error_out = "Jump missing target in '" + bb->name + "'";
                    return false;
                }

                if (inst->is_binary_arithmetic() && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && !t1->equals(t2)) {
                        error_out = "Binary op operand type mismatch in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::ICmp && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && !t1->equals(t2)) {
                        error_out = "ICmp operand type mismatch in '" + bb->name + "' of '" + name + "': " + (t1 ? t1->to_string() : "null") + " vs " + (t2 ? t2->to_string() : "null");
                        return false;
                    }
                }

                if (inst->op == IROp::FCmp && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && (!t1->equals(t2) || !t1->is_floating_point())) {
                        error_out = "FCmp operands must be matching floating-point types in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Load && !inst->operands.empty()) {
                    IRType* pt = get_value(inst->operands[0]).type;
                    if (pt && !pt->is_pointer()) {
                        error_out = "Load operand must be a pointer in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Store && inst->operands.size() >= 2) {
                    IRType* vt = get_value(inst->operands[0]).type;
                    IRType* pt = get_value(inst->operands[1]).type;
                    if (pt && !pt->is_pointer()) {
                        error_out = "Store pointer operand must be a pointer in '" + bb->name + "'";
                        return false;
                    }
                    if (pt && pt->element_type && vt && !pt->element_type->equals(vt)) {
                        bool both_int = pt->element_type->is_integer() && vt->is_integer();
                        bool ptr_int = (pt->element_type->is_pointer() && vt->is_integer()) ||
                                       (pt->element_type->is_integer() && vt->is_pointer()) ||
                                       (pt->element_type->is_pointer() && vt->is_pointer());
                        if (!both_int && !ptr_int) {
                            error_out = "Store value type does not match pointer element type in '" + bb->name + "'";
                            return false;
                        }
                    }
                }

                if (inst->op == IROp::Ret) {
                    if (inst->operands.empty()) {
                        if (return_type && !return_type->is_void()) {
                            error_out = "Return void in function '" + name + "' returning non-void type";
                            return false;
                        }
                    } else {
                        IRType* ret_val_t = get_value(inst->operands[0]).type;
                        if (return_type && ret_val_t && !return_type->equals(ret_val_t)) {
                            error_out = "Return value type mismatch in function '" + name + "': expected " +
                                        (return_type ? return_type->to_string() : "null") + ", got " +
                                        (ret_val_t ? ret_val_t->to_string() : "null");
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    std::string IRFunction::dump() const {
        std::ostringstream ss;
        ss << "define " << (return_type ? return_type->to_string() : "void")
           << " @" << name << "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << (arguments[i].type ? arguments[i].type->to_string() : "unknown")
               << " %" << arguments[i].id;
        }
        ss << ") {\n";

        for (const auto& bb : blocks) {
            ss << bb->dump();
        }
        ss << "}\n";
        return ss.str();
    }

    // =========================================================================
    // 5. IRModule Implementation
    // =========================================================================

    IRModule::IRModule(std::string name_in) : name(std::move(name_in)) {}

    IRFunction* IRModule::create_function(const std::string& fn_name, IRType* ret_type) {
        auto fn = std::make_unique<IRFunction>(fn_name, ret_type, this);
        IRFunction* raw = fn.get();
        functions.push_back(std::move(fn));
        return raw;
    }

    IRFunction* IRModule::get_function(const std::string& fn_name) const {
        for (const auto& fn : functions) {
            if (fn->name == fn_name) return fn.get();
        }
        return nullptr;
    }

    bool IRModule::verify(std::string& error_out) const {
        for (const auto& fn : functions) {
            if (!fn->verify(error_out)) return false;
        }
        return true;
    }

    std::string IRModule::dump() const {
        std::ostringstream ss;
        ss << "; GrassIR Module: " << name << "\n\n";
        for (const auto& fn : functions) {
            ss << fn->dump() << "\n";
        }
        return ss.str();
    }

    // =========================================================================
    // 6. IRBuilder Implementation
    // =========================================================================

    IRBuilder::IRBuilder(IRFunction* fn) : function_(fn) {}

    void IRBuilder::set_function(IRFunction* fn) {
        function_ = fn;
        insert_block_ = nullptr;
        at_end_ = true;
    }

    void IRBuilder::set_insert_point(BasicBlock* bb) {
        insert_block_ = bb;
        at_end_ = true;
    }

    void IRBuilder::set_insert_point(BasicBlock* bb, size_t index) {
        insert_block_ = bb;
        insert_index_ = index;
        at_end_ = false;
    }

    BasicBlock* IRBuilder::create_block(const std::string& name) {
        assert(function_ && "No function set for IRBuilder");
        return function_->create_block(name);
    }

    IRInst* IRBuilder::insert_instruction(std::unique_ptr<IRInst> inst) {
        assert(insert_block_ && "No insert block set for IRBuilder");
        IRInst* raw = inst.get();
        raw->parent = insert_block_;
        if (!raw->loc.is_valid() && current_loc_.is_valid()) {
            raw->loc = current_loc_;
        }

        if (at_end_ || insert_index_ >= insert_block_->instructions.size()) {
            insert_block_->instructions.push_back(std::move(inst));
            insert_index_ = insert_block_->instructions.size();
        } else {
            insert_block_->instructions.insert(
                insert_block_->instructions.begin() + insert_index_,
                std::move(inst));
            insert_index_++;
        }

        if (raw->result_id != kInvalidValueId && function_->has_value(raw->result_id)) {
            function_->get_value(raw->result_id).def_inst = raw;
        }

        for (ValueId opnd : raw->operands) {
            if (function_->has_value(opnd)) {
                auto& u = function_->get_value(opnd).users;
                if (std::find(u.begin(), u.end(), raw) == u.end()) {
                    u.push_back(raw);
                }
            }
        }

        for (const auto& in : raw->phi_incoming) {
            if (function_->has_value(in.value)) {
                auto& u = function_->get_value(in.value).users;
                if (std::find(u.begin(), u.end(), raw) == u.end()) {
                    u.push_back(raw);
                }
            }
        }

        return raw;
    }

    ValueId IRBuilder::create_const_int(IRType* type, int64_t val) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::ConstInt;
        inst->type = type;
        inst->int_imm = val;
        inst->result_id = function_->create_value(type, "", inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_const_i1(bool val) {
        IRType* t = function_->parent ? function_->parent->context.get_i1_type() : nullptr;
        return create_const_int(t, val ? 1 : 0);
    }

    ValueId IRBuilder::create_const_i8(int8_t val) {
        IRType* t = function_->parent ? function_->parent->context.get_i8_type() : nullptr;
        return create_const_int(t, val);
    }

    ValueId IRBuilder::create_const_i16(int16_t val) {
        IRType* t = function_->parent ? function_->parent->context.get_i16_type() : nullptr;
        return create_const_int(t, val);
    }

    ValueId IRBuilder::create_const_i32(int32_t val) {
        IRType* t = function_->parent ? function_->parent->context.get_i32_type() : nullptr;
        return create_const_int(t, val);
    }

    ValueId IRBuilder::create_const_i64(int64_t val) {
        IRType* t = function_->parent ? function_->parent->context.get_i64_type() : nullptr;
        return create_const_int(t, val);
    }

    ValueId IRBuilder::create_const_f32(float val) {
        IRType* t = function_->parent ? function_->parent->context.get_f32_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::ConstFloat;
        inst->type = t;
        inst->float_imm = static_cast<double>(val);
        inst->result_id = function_->create_value(t, "", inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_const_f64(double val) {
        IRType* t = function_->parent ? function_->parent->context.get_f64_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::ConstFloat;
        inst->type = t;
        inst->float_imm = val;
        inst->result_id = function_->create_value(t, "", inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_copy(ValueId src, const std::string& name) {
        IRType* t = function_->get_value(src).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Copy;
        inst->type = t;
        inst->operands.push_back(src);
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_phi(IRType* type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Phi;
        inst->type = type;
        inst->result_id = function_->create_value(type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    void IRBuilder::add_phi_incoming(ValueId phi_val, BasicBlock* block, ValueId val) {
        IRValue& v = function_->get_value(phi_val);
        if (v.def_inst && v.def_inst->op == IROp::Phi) {
            v.def_inst->add_phi_incoming(block, val);
            if (function_->has_value(val)) {
                auto& u = function_->get_value(val).users;
                if (std::find(u.begin(), u.end(), v.def_inst) == u.end()) {
                    u.push_back(v.def_inst);
                }
            }
        }
    }

    ValueId IRBuilder::create_add(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Add;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_sub(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Sub;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_mul(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Mul;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_sdiv(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::SDiv;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_udiv(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::UDiv;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_srem(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::SRem;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_urem(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::URem;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_neg(ValueId operand, const std::string& name) {
        IRType* t = function_->get_value(operand).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Neg;
        inst->type = t;
        inst->operands = {operand};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fadd(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FAdd;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fsub(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FSub;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fmul(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FMul;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fdiv(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FDiv;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fneg(ValueId operand, const std::string& name) {
        IRType* t = function_->get_value(operand).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FNeg;
        inst->type = t;
        inst->operands = {operand};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_and(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::And;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_or(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Or;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_xor(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Xor;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_not(ValueId operand, const std::string& name) {
        IRType* t = function_->get_value(operand).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Not;
        inst->type = t;
        inst->operands = {operand};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_shl(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Shl;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_lshr(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::LShr;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_ashr(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::AShr;
        inst->type = t;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_icmp(ICmpCond cond, ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->parent ? function_->parent->context.get_i1_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::ICmp;
        inst->type = t;
        inst->icmp_cond = cond;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_icmp_eq(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::EQ, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_ne(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::NE, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_slt(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::SLT, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_sle(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::SLE, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_sgt(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::SGT, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_sge(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::SGE, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_ult(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::ULT, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_ule(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::ULE, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_ugt(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::UGT, lhs, rhs, name);
    }
    ValueId IRBuilder::create_icmp_uge(ValueId lhs, ValueId rhs, const std::string& name) {
        return create_icmp(ICmpCond::UGE, lhs, rhs, name);
    }

    ValueId IRBuilder::create_fcmp(FCmpCond cond, ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* t = function_->parent ? function_->parent->context.get_i1_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FCmp;
        inst->type = t;
        inst->fcmp_cond = cond;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_zext(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::ZExt;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_sext(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::SExt;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_trunc(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Trunc;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_sitofp(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::SIToFP;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_fptosi(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::FPToSI;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_bitcast(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Bitcast;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_ptrtoint(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::PtrToInt;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_inttoptr(ValueId val, IRType* dest_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::IntToPtr;
        inst->type = dest_type;
        inst->operands = {val};
        inst->result_id = function_->create_value(dest_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_alloca(IRType* elem_type, uint32_t count, uint32_t align, const std::string& name) {
        IRType* ptr_t = function_->parent ? function_->parent->context.get_ptr_type(elem_type) : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Alloca;
        inst->type = ptr_t;
        inst->allocated_type = elem_type;
        inst->alloca_count = count;
        inst->alloca_align = align ? align : static_cast<uint32_t>(elem_type ? elem_type->alignment() : 1);
        inst->result_id = function_->create_value(ptr_t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_load(ValueId ptr, const std::string& name) {
        IRType* loaded_type = nullptr;
        const IRValue& ptr_val = function_->get_value(ptr);
        if (ptr_val.type && ptr_val.type->element_type) {
            loaded_type = ptr_val.type->element_type;
        } else if (ptr_val.def_inst && ptr_val.def_inst->op == IROp::Alloca) {
            loaded_type = ptr_val.def_inst->allocated_type;
        } else {
            loaded_type = function_->parent ? function_->parent->context.get_i32_type() : nullptr;
        }

        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Load;
        inst->type = loaded_type;
        inst->operands = {ptr};
        inst->result_id = function_->create_value(loaded_type, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    IRInst* IRBuilder::create_store(ValueId val, ValueId ptr) {
        if (function_->has_value(val) && function_->has_value(ptr)) {
            IRType* vt = function_->get_value(val).type;
            IRType* pt = function_->get_value(ptr).type;
            if (pt && pt->element_type && vt) {
                if (pt->element_type->is_integer() && vt->is_integer()) {
                    if (vt->bit_width() > pt->element_type->bit_width()) {
                        val = create_trunc(val, pt->element_type);
                    } else if (vt->bit_width() < pt->element_type->bit_width()) {
                        val = create_sext(val, pt->element_type);
                    }
                } else if (pt->element_type->is_integer() && vt->is_pointer()) {
                    val = create_ptrtoint(val, pt->element_type);
                } else if (pt->element_type->is_pointer() && vt->is_integer()) {
                    val = create_inttoptr(val, pt->element_type);
                }
            }
        }
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Store;
        inst->type = void_t;
        inst->operands = {val, ptr};
        return insert_instruction(std::move(inst));
    }

    ValueId IRBuilder::create_gep(ValueId base_ptr, const std::vector<ValueId>& indices, IRType* result_elem_type, const std::string& name) {
        IRType* ptr_t = function_->parent ? function_->parent->context.get_ptr_type(result_elem_type) : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::GetElementPtr;
        inst->type = ptr_t;
        inst->operands.push_back(base_ptr);
        for (ValueId idx : indices) inst->operands.push_back(idx);
        inst->result_id = function_->create_value(ptr_t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    ValueId IRBuilder::create_gep_struct(ValueId base_ptr, uint32_t field_index, const std::string& name) {
        IRType* struct_type = nullptr;
        if (function_->has_value(base_ptr)) {
            IRType* pt = function_->get_value(base_ptr).type;
            if (pt && pt->element_type && pt->element_type->is_struct()) {
                struct_type = pt->element_type;
            }
        }
        IRType* field_type = struct_type ? struct_type->get_field_type(field_index) : nullptr;
        IRType* ptr_t = function_->parent ? function_->parent->context.get_ptr_type(field_type) : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::GetElementPtr;
        inst->type = ptr_t;
        inst->operands.push_back(base_ptr);
        inst->gep_field_index = static_cast<int32_t>(field_index);
        inst->result_id = function_->create_value(ptr_t, name, inst.get());
        return insert_instruction(std::move(inst))->result_id;
    }

    IRInst* IRBuilder::create_memcpy(ValueId dst, ValueId src, ValueId size_bytes) {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::MemCpy;
        inst->type = void_t;
        inst->operands = {dst, src, size_bytes};
        return insert_instruction(std::move(inst));
    }

    IRInst* IRBuilder::create_memset(ValueId dst, ValueId val_byte, ValueId size_bytes) {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::MemSet;
        inst->type = void_t;
        inst->operands = {dst, val_byte, size_bytes};
        return insert_instruction(std::move(inst));
    }

    IRInst* IRBuilder::create_jump(BasicBlock* target) {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Jump;
        inst->type = void_t;
        inst->true_target = target;
        IRInst* raw = insert_instruction(std::move(inst));
        function_->rebuild_cfg();
        return raw;
    }

    IRInst* IRBuilder::create_branch(ValueId cond, BasicBlock* true_target, BasicBlock* false_target) {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Branch;
        inst->type = void_t;
        inst->operands = {cond};
        inst->true_target = true_target;
        inst->false_target = false_target;
        IRInst* raw = insert_instruction(std::move(inst));
        function_->rebuild_cfg();
        return raw;
    }

    ValueId IRBuilder::create_call(const std::string& func_name, IRType* ret_type, const std::vector<ValueId>& args, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Call;
        inst->type = ret_type;
        inst->callee_name = func_name;
        inst->operands = args;
        ValueId res_id = kInvalidValueId;
        if (ret_type && !ret_type->is_void()) {
            inst->result_id = function_->create_value(ret_type, name, inst.get());
            res_id = inst->result_id;
        }
        insert_instruction(std::move(inst));
        return res_id;
    }

    ValueId IRBuilder::create_indirect_call(ValueId func_ptr, IRType* ret_type, const std::vector<ValueId>& args, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Call;
        inst->type = ret_type;
        inst->is_indirect_call = true;
        inst->operands.push_back(func_ptr);
        for (ValueId a : args) inst->operands.push_back(a);
        ValueId res_id = kInvalidValueId;
        if (ret_type && !ret_type->is_void()) {
            inst->result_id = function_->create_value(ret_type, name, inst.get());
            res_id = inst->result_id;
        }
        insert_instruction(std::move(inst));
        return res_id;
    }

    IRInst* IRBuilder::create_ret(ValueId val) {
        IRType* t = function_->get_value(val).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Ret;
        inst->type = t;
        inst->operands = {val};
        return insert_instruction(std::move(inst));
    }

    IRInst* IRBuilder::create_ret_void() {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::Ret;
        inst->type = void_t;
        return insert_instruction(std::move(inst));
    }

    // =========================================================================
    // 7. Dominator Tree (Cooper-Harvey-Kennedy)
    // =========================================================================

    void DominatorTree::build(IRFunction& fn) {
        function = &fn;
        idom.clear();
        dom_children.clear();
        dominance_frontiers.clear();
        rpo_number.clear();
        rpo_blocks.clear();

        fn.rebuild_cfg();
        BasicBlock* entry = fn.get_entry_block();
        if (!entry) return;

        // Step 1: DFS post-order then reverse to obtain RPO
        std::unordered_set<BasicBlock*> visited;
        std::vector<BasicBlock*> post_order;

        std::function<void(BasicBlock*)> dfs = [&](BasicBlock* b) {
            visited.insert(b);
            for (BasicBlock* s : b->succs) {
                if (visited.find(s) == visited.end()) {
                    dfs(s);
                }
            }
            post_order.push_back(b);
        };
        dfs(entry);

        rpo_blocks.assign(post_order.rbegin(), post_order.rend());
        for (size_t i = 0; i < rpo_blocks.size(); ++i) {
            rpo_number[rpo_blocks[i]] = i;
        }

        // Step 2: Cooper-Harvey-Kennedy iteration
        idom[entry] = entry;

        auto intersect = [&](BasicBlock* b1, BasicBlock* b2) -> BasicBlock* {
            BasicBlock* finger1 = b1;
            BasicBlock* finger2 = b2;
            while (finger1 != finger2) {
                while (rpo_number[finger1] > rpo_number[finger2]) {
                    finger1 = idom[finger1];
                }
                while (rpo_number[finger2] > rpo_number[finger1]) {
                    finger2 = idom[finger2];
                }
            }
            return finger1;
        };

        bool changed = true;
        while (changed) {
            changed = false;
            for (size_t i = 1; i < rpo_blocks.size(); ++i) {
                BasicBlock* b = rpo_blocks[i];

                BasicBlock* new_idom = nullptr;
                for (BasicBlock* p : b->preds) {
                    if (idom.find(p) != idom.end()) {
                        new_idom = p;
                        break;
                    }
                }
                if (!new_idom) continue;

                for (BasicBlock* p : b->preds) {
                    if (p != new_idom && idom.find(p) != idom.end()) {
                        new_idom = intersect(p, new_idom);
                    }
                }

                if (idom.find(b) == idom.end() || idom[b] != new_idom) {
                    idom[b] = new_idom;
                    changed = true;
                }
            }
        }

        idom[entry] = nullptr;

        // Step 3: Dominator tree children
        for (BasicBlock* b : rpo_blocks) {
            if (b != entry && idom[b]) {
                dom_children[idom[b]].push_back(b);
            }
        }

        // Step 4: Dominance Frontiers
        for (BasicBlock* b : rpo_blocks) {
            if (b->preds.size() >= 2) {
                for (BasicBlock* p : b->preds) {
                    if (visited.find(p) == visited.end()) continue;
                    BasicBlock* runner = p;
                    while (runner != nullptr && runner != idom[b]) {
                        dominance_frontiers[runner].insert(b);
                        runner = idom[runner];
                    }
                }
            }
        }
    }

    bool DominatorTree::dominates(BasicBlock* a, BasicBlock* b) const {
        if (!a || !b) return false;
        if (a == b) return true;
        BasicBlock* curr = b;
        while (curr != nullptr) {
            if (curr == a) return true;
            auto it = idom.find(curr);
            if (it == idom.end()) break;
            curr = it->second;
        }
        return false;
    }

    const std::set<BasicBlock*>& DominatorTree::get_frontier(BasicBlock* b) const {
        static const std::set<BasicBlock*> empty_set;
        auto it = dominance_frontiers.find(b);
        return (it != dominance_frontiers.end()) ? it->second : empty_set;
    }

    BasicBlock* DominatorTree::get_idom(BasicBlock* b) const {
        auto it = idom.find(b);
        return (it != idom.end()) ? it->second : nullptr;
    }

    const std::vector<BasicBlock*>& DominatorTree::get_children(BasicBlock* b) const {
        static const std::vector<BasicBlock*> empty_vec;
        auto it = dom_children.find(b);
        return (it != dom_children.end()) ? it->second : empty_vec;
    }

    // =========================================================================
    // 8. Mem2Reg SSA Construction
    // =========================================================================

    bool Mem2RegPass::run_on_function(IRFunction& fn) {
        if (fn.is_declaration || fn.blocks.empty()) return false;
        fn.rebuild_cfg();

        // Remove unreachable blocks before dominator tree & SSA construction
        std::unordered_set<BasicBlock*> reachable;
        std::vector<BasicBlock*> worklist;
        BasicBlock* entry = fn.get_entry_block();
        if (entry) {
            reachable.insert(entry);
            worklist.push_back(entry);
            while (!worklist.empty()) {
                BasicBlock* curr = worklist.back();
                worklist.pop_back();
                for (BasicBlock* succ : curr->succs) {
                    if (reachable.insert(succ).second) {
                        worklist.push_back(succ);
                    }
                }
            }
        }
        std::vector<BasicBlock*> unreachable_blocks;
        for (auto& bb : fn.blocks) {
            if (reachable.find(bb.get()) == reachable.end()) {
                unreachable_blocks.push_back(bb.get());
            }
        }
        for (BasicBlock* dead_bb : unreachable_blocks) {
            fn.erase_block(dead_bb);
        }

        // Hoist all allocas to entry block so they dominate all uses
        if (entry) {
            for (auto& bb : fn.blocks) {
                if (bb.get() == entry) continue;
                for (size_t i = 0; i < bb->instructions.size();) {
                    if (bb->instructions[i]->op == IROp::Alloca) {
                        auto alloca_inst = std::move(bb->instructions[i]);
                        bb->instructions.erase(bb->instructions.begin() + i);
                        alloca_inst->parent = entry;
                        entry->push_front(std::move(alloca_inst));
                    } else {
                        ++i;
                    }
                }
            }
        }

        fn.rebuild_cfg();
        fn.rebuild_def_use();

        DominatorTree dom;
        dom.build(fn);

        std::vector<IRInst*> promotable_allocas;
        for (auto& bb : fn.blocks) {
            for (auto& inst : bb->instructions) {
                if (inst->op != IROp::Alloca) continue;
                if (!inst->allocated_type ||
                    (!inst->allocated_type->is_numeric() &&
                     !inst->allocated_type->is_pointer() &&
                     !inst->allocated_type->is_bool())) {
                    continue;
                }
                if (inst->alloca_count != 1) continue;

                bool promotable = true;
                const auto& users = fn.get_value(inst->result_id).users;
                for (IRInst* u : users) {
                    if (u->op == IROp::Load) {
                        if (u->operands.empty() || u->operands[0] != inst->result_id) {
                            promotable = false;
                            break;
                        }
                    } else if (u->op == IROp::Store) {
                        if (u->operands.size() < 2 || u->operands[1] != inst->result_id || u->operands[0] == inst->result_id) {
                            promotable = false;
                            break;
                        }
                    } else {
                        promotable = false;
                        break;
                    }
                }

                if (promotable) {
                    promotable_allocas.push_back(inst.get());
                }
            }
        }

        if (promotable_allocas.empty()) return false;

        std::unordered_map<ValueId, std::set<BasicBlock*>> alloca_def_blocks;
        for (IRInst* alloca_inst : promotable_allocas) {
            ValueId aid = alloca_inst->result_id;
            for (IRInst* u : fn.get_value(aid).users) {
                if (u->op == IROp::Store && u->parent) {
                    alloca_def_blocks[aid].insert(u->parent);
                }
            }
        }

        std::unordered_map<IRInst*, ValueId> phi_to_alloca;
        for (IRInst* alloca_inst : promotable_allocas) {
            ValueId aid = alloca_inst->result_id;
            const auto& def_blocks = alloca_def_blocks[aid];

            std::set<BasicBlock*> idf;
            std::vector<BasicBlock*> worklist(def_blocks.begin(), def_blocks.end());
            std::set<BasicBlock*> in_worklist(def_blocks.begin(), def_blocks.end());

            while (!worklist.empty()) {
                BasicBlock* x = worklist.back();
                worklist.pop_back();

                for (BasicBlock* y : dom.get_frontier(x)) {
                    if (idf.find(y) == idf.end()) {
                        idf.insert(y);
                        if (in_worklist.find(y) == in_worklist.end()) {
                            in_worklist.insert(y);
                            worklist.push_back(y);
                        }
                    }
                }
            }

            for (BasicBlock* y : idf) {
                auto phi = std::make_unique<IRInst>();
                phi->op = IROp::Phi;
                phi->type = alloca_inst->allocated_type;
                phi->result_id = fn.create_value(phi->type, "", phi.get());
                phi->parent = y;
                phi_to_alloca[phi.get()] = aid;
                y->push_front(std::move(phi));
            }
        }

        std::unordered_map<ValueId, ValueId> zero_vals;
        IRBuilder builder(&fn);
        if (fn.get_entry_block()) {
            builder.set_insert_point(fn.get_entry_block(), 0);
            for (IRInst* alloca_inst : promotable_allocas) {
                ValueId aid = alloca_inst->result_id;
                IRType* t = alloca_inst->allocated_type;
                if (t->kind == TypeKind::F32) {
                    zero_vals[aid] = builder.create_const_f32(0.0f);
                } else if (t->kind == TypeKind::F64) {
                    zero_vals[aid] = builder.create_const_f64(0.0);
                } else {
                    zero_vals[aid] = builder.create_const_int(t, 0);
                }
            }
        }

        std::unordered_map<ValueId, std::vector<ValueId>> var_stacks;
        std::unordered_set<IRInst*> to_erase;

        std::function<void(BasicBlock*)> rename_block = [&](BasicBlock* bb) {
            std::unordered_map<ValueId, size_t> push_counts;

            for (auto& inst : bb->instructions) {
                if (inst->op != IROp::Phi) break;
                auto it = phi_to_alloca.find(inst.get());
                if (it != phi_to_alloca.end()) {
                    ValueId aid = it->second;
                    var_stacks[aid].push_back(inst->result_id);
                    push_counts[aid]++;
                }
            }

            for (auto& inst : bb->instructions) {
                if (inst->op == IROp::Load) {
                    ValueId ptr = inst->operands[0];
                    if (zero_vals.find(ptr) != zero_vals.end()) {
                        ValueId curr_val = var_stacks[ptr].empty() ? zero_vals[ptr] : var_stacks[ptr].back();
                        fn.replace_all_uses_with(inst->result_id, curr_val);
                        to_erase.insert(inst.get());
                    }
                } else if (inst->op == IROp::Store) {
                    ValueId ptr = inst->operands[1];
                    ValueId val = inst->operands[0];
                    if (zero_vals.find(ptr) != zero_vals.end()) {
                        var_stacks[ptr].push_back(val);
                        push_counts[ptr]++;
                        to_erase.insert(inst.get());
                    }
                }
            }

            for (BasicBlock* succ : bb->succs) {
                for (auto& inst : succ->instructions) {
                    if (inst->op != IROp::Phi) break;
                    auto it = phi_to_alloca.find(inst.get());
                    if (it != phi_to_alloca.end()) {
                        ValueId aid = it->second;
                        ValueId curr_val = var_stacks[aid].empty() ? zero_vals[aid] : var_stacks[aid].back();
                        inst->add_phi_incoming(bb, curr_val);
                    }
                }
            }

            for (BasicBlock* child : dom.get_children(bb)) {
                rename_block(child);
            }

            for (auto& [aid, count] : push_counts) {
                for (size_t i = 0; i < count; ++i) {
                    var_stacks[aid].pop_back();
                }
            }
        };

        if (fn.get_entry_block()) {
            rename_block(fn.get_entry_block());
        }

        for (IRInst* inst : to_erase) {
            fn.erase_inst(inst);
        }
        for (IRInst* alloca_inst : promotable_allocas) {
            fn.erase_inst(alloca_inst);
        }

        fn.rebuild_cfg();
        fn.rebuild_def_use();
        return true;
    }

    // =========================================================================
    // 9. Optimization Passes
    // =========================================================================

    // ConstantFoldingPass
    bool ConstantFoldingPass::run_on_function(IRFunction& fn) {
        bool changed = false;
        bool progress = true;

        auto is_const_int = [&](ValueId vid, int64_t& out) -> bool {
            if (!fn.has_value(vid)) return false;
            IRInst* def = fn.get_value(vid).def_inst;
            if (def && def->op == IROp::ConstInt) {
                out = def->int_imm;
                return true;
            }
            return false;
        };

        auto is_const_float = [&](ValueId vid, double& out) -> bool {
            if (!fn.has_value(vid)) return false;
            IRInst* def = fn.get_value(vid).def_inst;
            if (def && def->op == IROp::ConstFloat) {
                out = def->float_imm;
                return true;
            }
            return false;
        };

        while (progress) {
            progress = false;
            fn.rebuild_def_use();

            for (auto& bb : fn.blocks) {
                for (size_t i = 0; i < bb->instructions.size(); ++i) {
                    IRInst* inst = bb->instructions[i].get();

                    if (inst->op == IROp::Branch && !inst->operands.empty()) {
                        int64_t cond_val = 0;
                        if (is_const_int(inst->operands[0], cond_val)) {
                            BasicBlock* target = (cond_val != 0) ? inst->true_target : inst->false_target;
                            BasicBlock* dead_target = (cond_val != 0) ? inst->false_target : inst->true_target;

                            if (dead_target && dead_target != target) {
                                for (auto& dead_inst : dead_target->instructions) {
                                    if (dead_inst->op != IROp::Phi) break;
                                    dead_inst->remove_phi_incoming(bb.get());
                                }
                            }

                            inst->op = IROp::Jump;
                            inst->operands.clear();
                            inst->true_target = target;
                            inst->false_target = nullptr;

                            fn.rebuild_cfg();
                            progress = true;
                            changed = true;
                            break;
                        }
                    }

                    if (inst->is_binary_arithmetic() && inst->operands.size() >= 2) {
                        int64_t c1 = 0, c2 = 0;
                        double f1 = 0.0, f2 = 0.0;

                        if (is_const_int(inst->operands[0], c1) && is_const_int(inst->operands[1], c2)) {
                            int64_t res = 0;
                            bool ok = true;
                            switch (inst->op) {
                                case IROp::Add:  res = c1 + c2; break;
                                case IROp::Sub:  res = c1 - c2; break;
                                case IROp::Mul:  res = c1 * c2; break;
                                case IROp::SDiv:
                                    if (c2 != 0 && !(c1 == INT64_MIN && c2 == -1)) res = c1 / c2;
                                    else ok = false;
                                    break;
                                case IROp::UDiv:
                                    if (c2 != 0) res = static_cast<int64_t>(static_cast<uint64_t>(c1) / static_cast<uint64_t>(c2));
                                    else ok = false;
                                    break;
                                case IROp::SRem:
                                    if (c2 != 0 && !(c1 == INT64_MIN && c2 == -1)) res = c1 % c2;
                                    else ok = false;
                                    break;
                                case IROp::URem:
                                    if (c2 != 0) res = static_cast<int64_t>(static_cast<uint64_t>(c1) % static_cast<uint64_t>(c2));
                                    else ok = false;
                                    break;
                                default: ok = false; break;
                            }
                            if (ok) {
                                uint32_t w = inst->type ? inst->type->bit_width() : 64;
                                if (w < 64) {
                                    uint64_t mask = (1ULL << w) - 1ULL;
                                    res = static_cast<int64_t>(static_cast<uint64_t>(res) & mask);
                                }
                                inst->op = IROp::ConstInt;
                                inst->int_imm = res;
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            }
                        } else if (is_const_float(inst->operands[0], f1) && is_const_float(inst->operands[1], f2)) {
                            double res = 0.0;
                            bool ok = true;
                            switch (inst->op) {
                                case IROp::FAdd: res = f1 + f2; break;
                                case IROp::FSub: res = f1 - f2; break;
                                case IROp::FMul: res = f1 * f2; break;
                                case IROp::FDiv: if (f2 != 0.0) res = f1 / f2; else ok = false; break;
                                default: ok = false; break;
                            }
                            if (ok) {
                                inst->op = IROp::ConstFloat;
                                inst->float_imm = res;
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            }
                        }
                    }

                    if ((inst->op == IROp::Neg || inst->op == IROp::Not) && !inst->operands.empty()) {
                        int64_t c1 = 0;
                        if (is_const_int(inst->operands[0], c1)) {
                            int64_t res = 0;
                            if (inst->op == IROp::Neg) {
                                res = (c1 == INT64_MIN) ? INT64_MIN : -c1;
                            } else {
                                res = ~c1;
                            }
                            uint32_t w = inst->type ? inst->type->bit_width() : 64;
                            if (w < 64) {
                                uint64_t mask = (1ULL << w) - 1ULL;
                                res = static_cast<int64_t>(static_cast<uint64_t>(res) & mask);
                            }
                            inst->op = IROp::ConstInt;
                            inst->int_imm = res;
                            inst->operands.clear();
                            progress = true;
                            changed = true;
                            continue;
                        }
                    }

                    if (inst->op == IROp::FNeg && !inst->operands.empty()) {
                        double f1 = 0.0;
                        if (is_const_float(inst->operands[0], f1)) {
                            inst->op = IROp::ConstFloat;
                            inst->float_imm = -f1;
                            inst->operands.clear();
                            progress = true;
                            changed = true;
                            continue;
                        }
                    }

                    if (inst->is_bitwise() && inst->operands.size() >= 2) {
                        int64_t c1 = 0, c2 = 0;
                        if (is_const_int(inst->operands[0], c1) && is_const_int(inst->operands[1], c2)) {
                            int64_t res = 0;
                            switch (inst->op) {
                                case IROp::And:  res = c1 & c2; break;
                                case IROp::Or:   res = c1 | c2; break;
                                case IROp::Xor:  res = c1 ^ c2; break;
                                case IROp::Shl:  res = c1 << (c2 & 63); break;
                                case IROp::LShr: res = static_cast<int64_t>(static_cast<uint64_t>(c1) >> (c2 & 63)); break;
                                case IROp::AShr: res = c1 >> (c2 & 63); break;
                                default: break;
                            }
                            uint32_t w = inst->type ? inst->type->bit_width() : 64;
                            if (w < 64) {
                                uint64_t mask = (1ULL << w) - 1ULL;
                                res = static_cast<int64_t>(static_cast<uint64_t>(res) & mask);
                            }
                            inst->op = IROp::ConstInt;
                            inst->int_imm = res;
                            inst->operands.clear();
                            progress = true;
                            changed = true;
                            continue;
                        }
                    }

                    if (inst->op == IROp::ICmp && inst->operands.size() >= 2) {
                        int64_t c1 = 0, c2 = 0;
                        if (is_const_int(inst->operands[0], c1) && is_const_int(inst->operands[1], c2)) {
                            bool res = false;
                            switch (inst->icmp_cond) {
                                case ICmpCond::EQ:  res = (c1 == c2); break;
                                case ICmpCond::NE:  res = (c1 != c2); break;
                                case ICmpCond::SLT: res = (c1 < c2); break;
                                case ICmpCond::SLE: res = (c1 <= c2); break;
                                case ICmpCond::SGT: res = (c1 > c2); break;
                                case ICmpCond::SGE: res = (c1 >= c2); break;
                                case ICmpCond::ULT: res = (static_cast<uint64_t>(c1) < static_cast<uint64_t>(c2)); break;
                                case ICmpCond::ULE: res = (static_cast<uint64_t>(c1) <= static_cast<uint64_t>(c2)); break;
                                case ICmpCond::UGT: res = (static_cast<uint64_t>(c1) > static_cast<uint64_t>(c2)); break;
                                case ICmpCond::UGE: res = (static_cast<uint64_t>(c1) >= static_cast<uint64_t>(c2)); break;
                            }
                            inst->op = IROp::ConstInt;
                            inst->int_imm = res ? 1 : 0;
                            inst->operands.clear();
                            progress = true;
                            changed = true;
                            continue;
                        }
                    }

                    if (inst->op == IROp::FCmp && inst->operands.size() >= 2) {
                        double f1 = 0.0, f2 = 0.0;
                        if (is_const_float(inst->operands[0], f1) && is_const_float(inst->operands[1], f2)) {
                            bool res = false;
                            switch (inst->fcmp_cond) {
                                case FCmpCond::OEQ: res = (f1 == f2); break;
                                case FCmpCond::ONE: res = (f1 != f2); break;
                                case FCmpCond::OLT: res = (f1 < f2); break;
                                case FCmpCond::OLE: res = (f1 <= f2); break;
                                case FCmpCond::OGT: res = (f1 > f2); break;
                                case FCmpCond::OGE: res = (f1 >= f2); break;
                            }
                            inst->op = IROp::ConstInt;
                            inst->int_imm = res ? 1 : 0;
                            inst->operands.clear();
                            progress = true;
                            changed = true;
                            continue;
                        }
                    }

                    if (inst->is_cast() && !inst->operands.empty()) {
                        int64_t c1 = 0;
                        double f1 = 0.0;
                        if (is_const_int(inst->operands[0], c1)) {
                            if (inst->op == IROp::SIToFP) {
                                inst->op = IROp::ConstFloat;
                                inst->float_imm = static_cast<double>(c1);
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            } else if (inst->op == IROp::PtrToInt || (inst->op == IROp::Bitcast && inst->type && inst->type->is_integer())) {
                                inst->op = IROp::ConstInt;
                                inst->int_imm = c1;
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            } else if (inst->op == IROp::ZExt || inst->op == IROp::SExt || inst->op == IROp::Trunc) {
                                IRType* src_type = fn.get_value(inst->operands[0]).type;
                                uint32_t src_w = src_type ? src_type->bit_width() : 64;
                                uint32_t dst_w = inst->type ? inst->type->bit_width() : 64;
                                uint64_t dst_mask = (dst_w >= 64) ? ~0ULL : ((1ULL << dst_w) - 1ULL);
                                uint64_t src_mask = (src_w >= 64) ? ~0ULL : ((1ULL << src_w) - 1ULL);

                                int64_t res = 0;
                                if (inst->op == IROp::SExt) {
                                    res = static_cast<int64_t>(static_cast<uint64_t>(c1) & src_mask);
                                    if (src_w < 64 && (res & (1ULL << (src_w - 1)))) {
                                        res |= static_cast<int64_t>(~src_mask);
                                    }
                                    res = static_cast<int64_t>(static_cast<uint64_t>(res) & dst_mask);
                                } else {
                                    res = static_cast<int64_t>((static_cast<uint64_t>(c1) & src_mask) & dst_mask);
                                }
                                inst->op = IROp::ConstInt;
                                inst->int_imm = res;
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            }
                        } else if (is_const_float(inst->operands[0], f1)) {
                            if (inst->op == IROp::FPToSI) {
                                inst->op = IROp::ConstInt;
                                inst->int_imm = static_cast<int64_t>(f1);
                                inst->operands.clear();
                                progress = true;
                                changed = true;
                                continue;
                            }
                        }
                    }
                }
            }
        }

        return changed;
    }

    // AlgebraicSimplificationPass
    bool AlgebraicSimplificationPass::run_on_function(IRFunction& fn) {
        bool changed = false;
        bool progress = true;

        auto is_const_int_val = [&](ValueId vid, int64_t val) -> bool {
            if (!fn.has_value(vid)) return false;
            IRInst* def = fn.get_value(vid).def_inst;
            return def && def->op == IROp::ConstInt && def->int_imm == val;
        };

        auto convert_to_const_int = [&](IRInst* target_inst, int64_t val) {
            for (ValueId opnd : target_inst->operands) {
                if (fn.has_value(opnd)) {
                    auto& u = fn.get_value(opnd).users;
                    u.erase(std::remove(u.begin(), u.end(), target_inst), u.end());
                }
            }
            target_inst->op = IROp::ConstInt;
            target_inst->int_imm = val;
            target_inst->operands.clear();
            progress = true;
            changed = true;
        };

        while (progress) {
            progress = false;
            fn.rebuild_def_use();

            for (auto& bb : fn.blocks) {
                for (size_t i = 0; i < bb->instructions.size(); ++i) {
                    IRInst* inst = bb->instructions[i].get();
                    if (inst->result_id == kInvalidValueId) continue;

                    ValueId repl = kInvalidValueId;

                    if (inst->op == IROp::Add && inst->operands.size() >= 2) {
                        if (is_const_int_val(inst->operands[1], 0)) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[0], 0)) repl = inst->operands[1];
                    } else if (inst->op == IROp::Sub && inst->operands.size() >= 2) {
                        if (is_const_int_val(inst->operands[1], 0)) repl = inst->operands[0];
                        else if (inst->operands[0] == inst->operands[1]) {
                            convert_to_const_int(inst, 0);
                            continue;
                        }
                    } else if (inst->op == IROp::Mul && inst->operands.size() >= 2) {
                        if (is_const_int_val(inst->operands[1], 1)) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[0], 1)) repl = inst->operands[1];
                        else if (is_const_int_val(inst->operands[1], 0) || is_const_int_val(inst->operands[0], 0)) {
                            convert_to_const_int(inst, 0);
                            continue;
                        }
                    } else if (inst->op == IROp::SDiv || inst->op == IROp::UDiv) {
                        if (inst->operands.size() >= 2 && is_const_int_val(inst->operands[1], 1)) {
                            repl = inst->operands[0];
                        }
                    } else if (inst->op == IROp::Xor && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) {
                            convert_to_const_int(inst, 0);
                            continue;
                        }
                        if (is_const_int_val(inst->operands[1], 0)) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[0], 0)) repl = inst->operands[1];
                    } else if (inst->op == IROp::And && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[1], 0) || is_const_int_val(inst->operands[0], 0)) {
                            convert_to_const_int(inst, 0);
                            continue;
                        }
                    } else if (inst->op == IROp::Or && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[1], 0)) repl = inst->operands[0];
                        else if (is_const_int_val(inst->operands[0], 0)) repl = inst->operands[1];
                    } else if (inst->op == IROp::Shl || inst->op == IROp::LShr || inst->op == IROp::AShr) {
                        if (inst->operands.size() >= 2 && is_const_int_val(inst->operands[1], 0)) {
                            repl = inst->operands[0];
                        }
                    }

                    if (repl != kInvalidValueId) {
                        fn.replace_all_uses_with(inst->result_id, repl);
                        fn.erase_inst(inst);
                        progress = true;
                        changed = true;
                        break;
                    }
                }
            }
        }

        if (changed) {
            fn.rebuild_def_use();
        }
        return changed;
    }

    // StrengthReductionPass
    bool StrengthReductionPass::run_on_function(IRFunction& fn) {
        bool changed = false;

        auto is_power_of_two = [](uint64_t x, uint32_t& shift_out) -> bool {
            if (x == 0 || (x & (x - 1)) != 0) return false;
            shift_out = 0;
            while ((x & 1ULL) == 0) {
                x >>= 1ULL;
                shift_out++;
            }
            return true;
        };

        auto is_const_int = [&](ValueId vid, int64_t& out) -> bool {
            if (!fn.has_value(vid)) return false;
            IRInst* def = fn.get_value(vid).def_inst;
            if (def && def->op == IROp::ConstInt) {
                out = def->int_imm;
                return true;
            }
            return false;
        };

        IRBuilder builder(&fn);

        for (auto& bb : fn.blocks) {
            for (size_t i = 0; i < bb->instructions.size(); ++i) {
                IRInst* inst = bb->instructions[i].get();
                if (inst->operands.size() < 2) continue;

                int64_t c2 = 0;
                uint32_t shift = 0;

                if (inst->op == IROp::Mul) {
                    ValueId x = inst->operands[0];
                    if (is_const_int(inst->operands[1], c2) && c2 > 0 && is_power_of_two(static_cast<uint64_t>(c2), shift)) {
                        builder.set_insert_point(bb.get(), i);
                        ValueId shift_val = builder.create_const_int(inst->type, shift);
                        inst->op = IROp::Shl;
                        inst->operands = {x, shift_val};
                        changed = true;
                    } else if (is_const_int(inst->operands[0], c2) && c2 > 0 && is_power_of_two(static_cast<uint64_t>(c2), shift)) {
                        ValueId y = inst->operands[1];
                        builder.set_insert_point(bb.get(), i);
                        ValueId shift_val = builder.create_const_int(inst->type, shift);
                        inst->op = IROp::Shl;
                        inst->operands = {y, shift_val};
                        changed = true;
                    }
                } else if (inst->op == IROp::UDiv) {
                    ValueId x = inst->operands[0];
                    if (is_const_int(inst->operands[1], c2) && c2 > 0 && is_power_of_two(static_cast<uint64_t>(c2), shift)) {
                        builder.set_insert_point(bb.get(), i);
                        ValueId shift_val = builder.create_const_int(inst->type, shift);
                        inst->op = IROp::LShr;
                        inst->operands = {x, shift_val};
                        changed = true;
                    }
                } else if (inst->op == IROp::URem) {
                    ValueId x = inst->operands[0];
                    if (is_const_int(inst->operands[1], c2) && c2 > 0 && is_power_of_two(static_cast<uint64_t>(c2), shift)) {
                        builder.set_insert_point(bb.get(), i);
                        ValueId mask_val = builder.create_const_int(inst->type, c2 - 1);
                        inst->op = IROp::And;
                        inst->operands = {x, mask_val};
                        changed = true;
                    }
                }
            }
        }

        if (changed) {
            fn.rebuild_def_use();
        }
        return changed;
    }

    // CommonSubexpressionEliminationPass
    bool CommonSubexpressionEliminationPass::run_on_function(IRFunction& fn) {
        fn.rebuild_cfg();
        fn.rebuild_def_use();
        DominatorTree dom;
        dom.build(fn);

        struct ExprKey {
            IROp op;
            IRType* type;
            ValueId op1;
            ValueId op2;
            int64_t int_imm;
            double float_imm;
            ICmpCond icmp_cond;
            FCmpCond fcmp_cond;

            bool operator<(const ExprKey& o) const {
                if (op != o.op) return op < o.op;
                if (type != o.type) return type < o.type;
                if (op1 != o.op1) return op1 < o.op1;
                if (op2 != o.op2) return op2 < o.op2;
                if (int_imm != o.int_imm) return int_imm < o.int_imm;
                if (float_imm != o.float_imm) return float_imm < o.float_imm;
                if (icmp_cond != o.icmp_cond) return icmp_cond < o.icmp_cond;
                return fcmp_cond < o.fcmp_cond;
            }
        };

        std::map<ExprKey, ValueId> avail_exprs;
        std::vector<IRInst*> to_erase;
        bool changed = false;

        std::function<void(BasicBlock*)> walk_dom = [&](BasicBlock* bb) {
            auto saved_avail = avail_exprs;

            for (auto& inst : bb->instructions) {
                if (inst->result_id == kInvalidValueId || inst->has_side_effects() || inst->is_memory() || inst->op == IROp::Phi) {
                    continue;
                }

                ExprKey key;
                key.op = inst->op;
                key.type = inst->type;
                key.op1 = inst->operands.size() > 0 ? inst->operands[0] : kInvalidValueId;
                key.op2 = inst->operands.size() > 1 ? inst->operands[1] : kInvalidValueId;
                key.int_imm = inst->int_imm;
                key.float_imm = inst->float_imm;
                key.icmp_cond = inst->icmp_cond;
                key.fcmp_cond = inst->fcmp_cond;

                if (inst->is_commutative() && key.op1 > key.op2) {
                    std::swap(key.op1, key.op2);
                }

                auto it = avail_exprs.find(key);
                if (it != avail_exprs.end()) {
                    fn.replace_all_uses_with(inst->result_id, it->second);
                    to_erase.push_back(inst.get());
                    changed = true;
                } else {
                    avail_exprs[key] = inst->result_id;
                }
            }

            for (BasicBlock* child : dom.get_children(bb)) {
                walk_dom(child);
            }

            avail_exprs = saved_avail;
        };

        if (fn.get_entry_block()) {
            walk_dom(fn.get_entry_block());
        }

        for (IRInst* inst : to_erase) {
            fn.erase_inst(inst);
        }

        if (changed) {
            fn.rebuild_def_use();
        }
        return changed;
    }

    // DeadCodeEliminationPass
    bool DeadCodeEliminationPass::run_on_function(IRFunction& fn) {
        bool changed = false;
        fn.rebuild_cfg();

        // 1. Remove unreachable blocks
        std::unordered_set<BasicBlock*> reachable;
        std::vector<BasicBlock*> worklist;
        BasicBlock* entry = fn.get_entry_block();
        if (entry) {
            reachable.insert(entry);
            worklist.push_back(entry);
            while (!worklist.empty()) {
                BasicBlock* curr = worklist.back();
                worklist.pop_back();
                for (BasicBlock* succ : curr->succs) {
                    if (reachable.insert(succ).second) {
                        worklist.push_back(succ);
                    }
                }
            }
        }

        std::vector<BasicBlock*> unreachable_blocks;
        for (auto& bb : fn.blocks) {
            if (reachable.find(bb.get()) == reachable.end()) {
                unreachable_blocks.push_back(bb.get());
            }
        }

        for (BasicBlock* dead_bb : unreachable_blocks) {
            fn.erase_block(dead_bb);
            changed = true;
        }

        // 2. Remove dead instructions
        bool progress = true;
        while (progress) {
            progress = false;
            fn.rebuild_def_use();

            for (auto& bb : fn.blocks) {
                for (size_t i = 0; i < bb->instructions.size(); ++i) {
                    IRInst* inst = bb->instructions[i].get();
                    if (inst->result_id != kInvalidValueId &&
                        !inst->has_side_effects() &&
                        inst->op != IROp::Alloca) {
                        if (fn.get_value(inst->result_id).users.empty()) {
                            fn.erase_inst(inst);
                            progress = true;
                            changed = true;
                            break;
                        }
                    }
                }
            }
        }

        if (changed) {
            fn.rebuild_cfg();
            fn.rebuild_def_use();
        }
        return changed;
    }

    // CFGSimplifyPass
    bool CFGSimplifyPass::run_on_function(IRFunction& fn) {
        bool changed = false;
        bool progress = true;

        while (progress) {
            progress = false;
            fn.rebuild_cfg();

            // 1. Merge blocks: A -> B where A has only succ B, and B has only pred A
            for (size_t i = 0; i < fn.blocks.size(); ++i) {
                BasicBlock* a = fn.blocks[i].get();
                if (a->succs.size() == 1) {
                    BasicBlock* b = a->succs[0];
                    if (b != a && b != fn.get_entry_block() && b->preds.size() == 1 && b->preds[0] == a) {
                        // Remove terminator from A
                        if (!a->instructions.empty() && a->instructions.back()->is_terminator()) {
                            a->instructions.pop_back();
                        }

                        // Eliminate phis in B (single incoming)
                        for (auto& inst : b->instructions) {
                            if (inst->op == IROp::Phi) {
                                ValueId val = inst->get_phi_incoming_value(a);
                                fn.replace_all_uses_with(inst->result_id, val);
                            } else {
                                inst->parent = a;
                                a->instructions.push_back(std::move(inst));
                            }
                        }

                        // For all successors of B, redirect incoming block B to A in their Phi nodes
                        for (BasicBlock* s : b->succs) {
                            for (auto& s_inst : s->instructions) {
                                if (s_inst->op != IROp::Phi) break;
                                for (auto& in : s_inst->phi_incoming) {
                                    if (in.block == b) {
                                        in.block = a;
                                    }
                                }
                            }
                        }

                        b->instructions.clear();
                        fn.erase_block(b);
                        progress = true;
                        changed = true;
                        break;
                    }
                }
            }
            if (progress) continue;

            // 2. Trampoline block removal: block with only unconditional jump
            for (size_t i = 0; i < fn.blocks.size(); ++i) {
                BasicBlock* b = fn.blocks[i].get();
                if (b == fn.get_entry_block()) continue;
                if (b->instructions.size() == 1 && b->instructions[0]->op == IROp::Jump) {
                    BasicBlock* c = b->instructions[0]->true_target;
                    if (c && c != b) {
                        for (BasicBlock* p : b->preds) {
                            IRInst* p_term = p->get_terminator();
                            if (p_term) {
                                if (p_term->true_target == b) p_term->true_target = c;
                                if (p_term->false_target == b) p_term->false_target = c;
                            }
                        }

                        for (auto& c_inst : c->instructions) {
                            if (c_inst->op != IROp::Phi) break;
                            ValueId incoming_val = c_inst->get_phi_incoming_value(b);
                            c_inst->remove_phi_incoming(b);
                            if (incoming_val != kInvalidValueId) {
                                for (BasicBlock* p : b->preds) {
                                    c_inst->add_phi_incoming(p, incoming_val);
                                }
                            }
                        }

                        b->instructions.clear();
                        fn.erase_block(b);
                        progress = true;
                        changed = true;
                        break;
                    }
                }
            }
        }

        if (changed) {
            fn.rebuild_cfg();
            fn.rebuild_def_use();
        }
        return changed;
    }

    // PassManager
    void PassManager::add_pass(std::unique_ptr<IRPass> pass) {
        passes.push_back(std::move(pass));
    }

    void PassManager::add_standard_optimizations() {
        add<ConstantFoldingPass>();
        add<AlgebraicSimplificationPass>();
        add<StrengthReductionPass>();
        add<CommonSubexpressionEliminationPass>();
        add<DeadCodeEliminationPass>();
        add<CFGSimplifyPass>();
    }

    bool PassManager::run(IRFunction& fn) {
        if (fn.is_declaration || fn.blocks.empty()) return false;
        bool changed = false;
        bool iter_changed = true;
        int iters = 0;
        constexpr int kMaxIters = 8;
        while (iter_changed && iters < kMaxIters) {
            iter_changed = false;
            for (auto& pass : passes) {
                if (pass->run_on_function(fn)) {
                    iter_changed = true;
                    changed = true;
                }
            }
            iters++;
        }
        return changed;
    }

    bool PassManager::run(IRModule& module) {
        bool changed = false;
        for (auto& fn : module.functions) {
            changed |= run(*fn);
        }
        return changed;
    }

    // =========================================================================
    // 10. Liveness Analysis & Linear Scan Allocator
    // =========================================================================

    void LivenessAnalysis::run(IRFunction& fn) {
        function = &fn;
        live_in.clear();
        live_out.clear();
        def_set.clear();
        use_set.clear();
        intervals.clear();
        inst_indices.clear();
        linear_blocks.clear();

        fn.rebuild_cfg();
        fn.rebuild_def_use();

        BasicBlock* entry = fn.get_entry_block();
        if (!entry) return;

        // Linearize blocks via DFS RPO
        std::unordered_set<BasicBlock*> visited;
        std::vector<BasicBlock*> post_order;
        std::function<void(BasicBlock*)> dfs = [&](BasicBlock* b) {
            visited.insert(b);
            for (BasicBlock* s : b->succs) {
                if (visited.find(s) == visited.end()) dfs(s);
            }
            post_order.push_back(b);
        };
        dfs(entry);
        linear_blocks.assign(post_order.rbegin(), post_order.rend());

        // Number instructions evenly
        int current_idx = 0;
        for (BasicBlock* b : linear_blocks) {
            for (auto& inst : b->instructions) {
                inst_indices[inst.get()] = current_idx;
                current_idx += 2;
            }
        }

        // Def and Use sets per block
        for (BasicBlock* b : linear_blocks) {
            for (auto& inst : b->instructions) {
                if (inst->op != IROp::Phi) {
                    for (ValueId opnd : inst->operands) {
                        if (def_set[b].find(opnd) == def_set[b].end()) {
                            use_set[b].insert(opnd);
                        }
                    }
                }
                if (inst->result_id != kInvalidValueId) {
                    def_set[b].insert(inst->result_id);
                }
            }
        }

        // Fixed-point backward dataflow iteration
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto it = linear_blocks.rbegin(); it != linear_blocks.rend(); ++it) {
                BasicBlock* b = *it;
                std::set<ValueId> new_live_out;

                for (BasicBlock* s : b->succs) {
                    for (ValueId v : live_in[s]) {
                        if (fn.has_value(v)) {
                            IRInst* def = fn.get_value(v).def_inst;
                            if (def && def->parent == s && def->op == IROp::Phi) {
                                continue;
                            }
                        }
                        new_live_out.insert(v);
                    }

                    for (auto& inst : s->instructions) {
                        if (inst->op != IROp::Phi) break;
                        ValueId in_val = inst->get_phi_incoming_value(b);
                        if (in_val != kInvalidValueId) {
                            new_live_out.insert(in_val);
                        }
                    }
                }

                std::set<ValueId> new_live_in = use_set[b];
                for (ValueId v : new_live_out) {
                    if (def_set[b].find(v) == def_set[b].end()) {
                        new_live_in.insert(v);
                    }
                }

                if (new_live_in != live_in[b] || new_live_out != live_out[b]) {
                    live_in[b] = std::move(new_live_in);
                    live_out[b] = std::move(new_live_out);
                    changed = true;
                }
            }
        }

        // Build intervals for all values
        for (const auto& [vid, val] : fn.values) {
            LiveInterval li;
            li.val = vid;
            li.type = val.type;
            li.start = val.def_inst ? inst_indices[val.def_inst] : 0;
            li.end = li.start;

            for (IRInst* u : val.users) {
                if (u->op != IROp::Phi) {
                    auto it = inst_indices.find(u);
                    if (it != inst_indices.end()) {
                        li.end = std::max(li.end, it->second);
                    }
                } else {
                    for (const auto& in : u->phi_incoming) {
                        if (in.value == vid && in.block && !in.block->instructions.empty()) {
                            auto it = inst_indices.find(in.block->instructions.back().get());
                            if (it != inst_indices.end()) {
                                li.end = std::max(li.end, it->second + 1);
                            }
                        }
                    }
                }
            }

            for (BasicBlock* b : linear_blocks) {
                if (live_out[b].count(vid) && !b->instructions.empty()) {
                    auto it = inst_indices.find(b->instructions.back().get());
                    if (it != inst_indices.end()) {
                        li.end = std::max(li.end, it->second + 1);
                    }
                }
            }

            for (BasicBlock* b : linear_blocks) {
                for (auto& inst : b->instructions) {
                    if (inst->op == IROp::Call ||
                        inst->op == IROp::SDiv || inst->op == IROp::SRem ||
                        inst->op == IROp::UDiv || inst->op == IROp::URem ||
                        inst->op == IROp::Shl  || inst->op == IROp::AShr ||
                        inst->op == IROp::LShr) {
                        int call_idx = inst_indices[inst.get()];
                        if (call_idx > li.start && call_idx < li.end) {
                            li.crosses_call = true;
                        }
                    }
                }
            }

            intervals[vid] = li;
        }
    }

    AllocationResult LinearScanAllocator::allocate(IRFunction& fn) {
        AllocationResult result;
        result.abi = abi;

        LivenessAnalysis liveness;
        liveness.run(fn);

        // Partition physical registers by ABI
        std::vector<PhysReg> gpr_caller_saved;
        std::vector<PhysReg> gpr_callee_saved;
        std::vector<PhysReg> xmm_caller_saved;
        std::vector<PhysReg> xmm_callee_saved;

        if (abi == TargetABI::Win64) {
            // Exclude RAX and codegen scratch registers (R9, R10, R11, XMM0..XMM3) from general allocation
            gpr_caller_saved = {PhysReg::RCX, PhysReg::RDX, PhysReg::R8};
            gpr_callee_saved = {PhysReg::RBX, PhysReg::RDI, PhysReg::RSI,
                                PhysReg::R12, PhysReg::R13, PhysReg::R14, PhysReg::R15};
            xmm_caller_saved = {PhysReg::XMM4, PhysReg::XMM5};
            xmm_callee_saved = {PhysReg::XMM6, PhysReg::XMM7, PhysReg::XMM8,
                                PhysReg::XMM9, PhysReg::XMM10, PhysReg::XMM11,
                                PhysReg::XMM12, PhysReg::XMM13, PhysReg::XMM14, PhysReg::XMM15};
        } else if (abi == TargetABI::SysV64) {
            // Exclude RAX and codegen scratch registers (R9, R10, R11, XMM0..XMM3)
            gpr_caller_saved = {PhysReg::RCX, PhysReg::RDX,
                                PhysReg::RSI, PhysReg::RDI, PhysReg::R8};
            gpr_callee_saved = {PhysReg::RBX, PhysReg::R12,
                                PhysReg::R13, PhysReg::R14, PhysReg::R15};
            xmm_caller_saved = {PhysReg::XMM4, PhysReg::XMM5, PhysReg::XMM6, PhysReg::XMM7,
                                PhysReg::XMM8, PhysReg::XMM9, PhysReg::XMM10, PhysReg::XMM11,
                                PhysReg::XMM12, PhysReg::XMM13, PhysReg::XMM14, PhysReg::XMM15};
        } else {
            // Cdecl32
            gpr_caller_saved = {PhysReg::RAX, PhysReg::RCX, PhysReg::RDX};
            gpr_callee_saved = {PhysReg::RBX, PhysReg::RSI, PhysReg::RDI};
            xmm_caller_saved = {PhysReg::XMM0, PhysReg::XMM1, PhysReg::XMM2, PhysReg::XMM3,
                                PhysReg::XMM4, PhysReg::XMM5, PhysReg::XMM6, PhysReg::XMM7};
        }

        std::unordered_set<PhysReg> all_callee_saved(gpr_callee_saved.begin(), gpr_callee_saved.end());
        all_callee_saved.insert(xmm_callee_saved.begin(), xmm_callee_saved.end());

        int32_t current_stack_slot = 0;

        auto run_linear_scan = [&](const std::vector<LiveInterval*>& intervals_to_alloc,
                                   const std::vector<PhysReg>& caller_regs,
                                   const std::vector<PhysReg>& callee_regs) {
            std::vector<PhysReg> all_regs = caller_regs;
            all_regs.insert(all_regs.end(), callee_regs.begin(), callee_regs.end());

            std::vector<LiveInterval*> sorted = intervals_to_alloc;
            std::sort(sorted.begin(), sorted.end(), [](const LiveInterval* a, const LiveInterval* b) {
                return a->start < b->start;
            });

            std::vector<LiveInterval*> active;
            std::set<PhysReg> free_regs(all_regs.begin(), all_regs.end());

            for (LiveInterval* current : sorted) {
                auto it = active.begin();
                while (it != active.end()) {
                    LiveInterval* act = *it;
                    if (act->end < current->start) {
                        free_regs.insert(act->reg);
                        it = active.erase(it);
                    } else {
                        ++it;
                    }
                }

                PhysReg chosen = PhysReg::None;
                if (current->crosses_call) {
                    for (PhysReg r : callee_regs) {
                        if (free_regs.count(r)) {
                            chosen = r;
                            break;
                        }
                    }
                    if (chosen == PhysReg::None) {
                        // Check if an active interval using a callee-saved register can be evicted
                        LiveInterval* best_act = nullptr;
                        for (auto rit = active.rbegin(); rit != active.rend(); ++rit) {
                            LiveInterval* act = *rit;
                            if (all_callee_saved.count(act->reg) && act->end > current->end) {
                                best_act = act;
                                break;
                            }
                        }
                        if (best_act) {
                            chosen = best_act->reg;
                            best_act->is_spilled = true;
                            best_act->reg = PhysReg::None;
                            best_act->stack_slot_offset = current_stack_slot;
                            current_stack_slot += 8;
                            result.spill_count++;

                            active.erase(std::find(active.begin(), active.end(), best_act));
                        }
                    }
                } else {
                    for (PhysReg r : caller_regs) {
                        if (free_regs.count(r)) {
                            chosen = r;
                            break;
                        }
                    }
                    if (chosen == PhysReg::None) {
                        for (PhysReg r : callee_regs) {
                            if (free_regs.count(r)) {
                                chosen = r;
                                break;
                            }
                        }
                    }
                }

                if (chosen != PhysReg::None) {
                    free_regs.erase(chosen);
                    current->reg = chosen;
                    current->is_spilled = false;

                    if (all_callee_saved.count(chosen)) {
                        result.used_callee_saved.insert(chosen);
                    }

                    active.push_back(current);
                    std::sort(active.begin(), active.end(), [](const LiveInterval* a, const LiveInterval* b) {
                        return a->end < b->end;
                    });
                } else {
                    // Must spill
                    if (!active.empty() && active.back()->end > current->end && !current->crosses_call) {
                        LiveInterval* spill_cand = active.back();
                        current->reg = spill_cand->reg;
                        current->is_spilled = false;
                        if (all_callee_saved.count(current->reg)) {
                            result.used_callee_saved.insert(current->reg);
                        }

                        spill_cand->is_spilled = true;
                        spill_cand->reg = PhysReg::None;
                        spill_cand->stack_slot_offset = current_stack_slot;
                        current_stack_slot += 8;
                        result.spill_count++;

                        active.pop_back();
                        active.push_back(current);
                        std::sort(active.begin(), active.end(), [](const LiveInterval* a, const LiveInterval* b) {
                            return a->end < b->end;
                        });
                    } else {
                        current->is_spilled = true;
                        current->reg = PhysReg::None;
                        current->stack_slot_offset = current_stack_slot;
                        current_stack_slot += 8;
                        result.spill_count++;
                    }
                }
            }
        };

        result.intervals = liveness.intervals;

        std::vector<LiveInterval*> gpr_intervals;
        std::vector<LiveInterval*> xmm_intervals;

        for (auto& [vid, interval] : result.intervals) {
            (void)vid;
            if (interval.type && interval.type->is_floating_point()) {
                xmm_intervals.push_back(&interval);
            } else {
                gpr_intervals.push_back(&interval);
            }
        }

        run_linear_scan(gpr_intervals, gpr_caller_saved, gpr_callee_saved);
        run_linear_scan(xmm_intervals, xmm_caller_saved, xmm_callee_saved);

        bool has_calls = false;
        for (const auto& bb : fn.blocks) {
            for (const auto& inst : bb->instructions) {
                if (inst->op == IROp::Call) {
                    has_calls = true;
                    break;
                }
            }
            if (has_calls) break;
        }

        // Phi Resolution (Sequentializing parallel copies with cycle breaking)
        auto get_location = [&](ValueId vid) -> ParallelCopy::Location {
            ParallelCopy::Location loc;
            const auto& iv = result.intervals[vid];
            loc.type = iv.type;
            if (iv.is_spilled) {
                loc.kind = ParallelCopy::LocKind::Stack;
                loc.stack_offset = iv.stack_slot_offset;
            } else {
                loc.kind = ParallelCopy::LocKind::Reg;
                loc.reg = iv.reg;
            }
            return loc;
        };

        for (const auto& bb : fn.blocks) {
            for (BasicBlock* s : bb->succs) {
                std::vector<ParallelCopy> pending_copies;

                for (const auto& inst : s->instructions) {
                    if (inst->op != IROp::Phi) break;
                    ValueId in_val = inst->get_phi_incoming_value(bb.get());
                    if (in_val != kInvalidValueId) {
                        ParallelCopy::Location src_loc = get_location(in_val);
                        ParallelCopy::Location dst_loc = get_location(inst->result_id);
                        if (!(src_loc == dst_loc)) {
                            pending_copies.push_back({src_loc, dst_loc});
                        }
                    }
                }

                while (!pending_copies.empty()) {
                    bool emitted = false;
                    for (auto it = pending_copies.begin(); it != pending_copies.end(); ++it) {
                        bool dst_used_as_src = false;
                        for (const auto& other : pending_copies) {
                            if (&other != &(*it) && other.src == it->dst) {
                                dst_used_as_src = true;
                                break;
                            }
                        }
                        if (!dst_used_as_src) {
                            result.phi_copies.push_back(*it);
                            pending_copies.erase(it);
                            emitted = true;
                            break;
                        }
                    }

                    if (!emitted) {
                        // Cycle detected: break using scratch stack temporary
                        auto cycle_move = pending_copies.front();
                        ParallelCopy::Location temp_loc;
                        temp_loc.kind = ParallelCopy::LocKind::Stack;
                        temp_loc.stack_offset = current_stack_slot;
                        current_stack_slot += 8;

                        result.phi_copies.push_back({cycle_move.src, temp_loc});

                        for (auto& other : pending_copies) {
                            if (other.src == cycle_move.src) {
                                other.src = temp_loc;
                            }
                        }
                    }
                }
            }
        }

        uint32_t total_stack = static_cast<uint32_t>(current_stack_slot);
        if (abi == TargetABI::Win64 && has_calls) {
            total_stack += 32; // Shadow store
        }
        result.stack_frame_size = (total_stack + 15) & ~15;

        return result;
    }

} // namespace grass::ir
