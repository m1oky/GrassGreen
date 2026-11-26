#include "native/GrassIR.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace grass::ir {

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

    ValueId IRBuilder::create_vec_zero(IRType* vec_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecZero;
        inst->type = vec_type;
        inst->result_id = function_->create_value(vec_type, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    ValueId IRBuilder::create_vec_splat(IRType* vec_type, ValueId scalar, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecSplat;
        inst->type = vec_type;
        inst->operands = {scalar};
        inst->result_id = function_->create_value(vec_type, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    ValueId IRBuilder::create_vec_load(IRType* vec_type, ValueId ptr, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecLoad;
        inst->type = vec_type;
        inst->operands = {ptr};
        inst->result_id = function_->create_value(vec_type, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    IRInst* IRBuilder::create_vec_store(ValueId vec_val, ValueId ptr) {
        IRType* void_t = function_->parent ? function_->parent->context.get_void_type() : nullptr;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecStore;
        inst->type = void_t;
        inst->operands = {vec_val, ptr};
        return insert_instruction(std::move(inst));
    }

    ValueId IRBuilder::create_vec_add(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* vt = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecAdd;
        inst->type = vt;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(vt, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    ValueId IRBuilder::create_vec_sub(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* vt = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecSub;
        inst->type = vt;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(vt, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    ValueId IRBuilder::create_vec_mul(ValueId lhs, ValueId rhs, const std::string& name) {
        IRType* vt = function_->get_value(lhs).type;
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecMul;
        inst->type = vt;
        inst->operands = {lhs, rhs};
        inst->result_id = function_->create_value(vt, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
    }

    ValueId IRBuilder::create_vec_hadd(ValueId vec_val, IRType* scalar_type, const std::string& name) {
        auto inst = std::make_unique<IRInst>();
        inst->op = IROp::VecHAdd;
        inst->type = scalar_type;
        inst->operands = {vec_val};
        inst->result_id = function_->create_value(scalar_type, name, inst.get());
        ValueId id = inst->result_id;
        insert_instruction(std::move(inst));
        return id;
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


} // namespace grass::ir
