#include "native/GrassNativeX86Ctx.h"
#include <cstring>

namespace grass::codegen {

    bool X86FuncCodegen::emit_arith_mem(const ir::BasicBlock* bb, const ir::IRInst* inst) {
        (void)bb;
        const auto& options_ = options;
        switch (inst->op) {
            case ir::IROp::ConstInt: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                if (dst != backend::Reg::None && backend::reg_is_gpr(dst)) {
                    encoder.mov_ri64(dst, inst->int_imm);
                } else {
                    encoder.mov_ri64(backend::Reg::R11, inst->int_imm);
                    store_val(inst->result_id, backend::Reg::R11);
                }
                break;
            }
            case ir::IROp::ConstFloat: {
                uint64_t raw_bits;
                std::memcpy(&raw_bits, &inst->float_imm, 8);
                backend::Reg dst = get_loc_reg(inst->result_id);
                bool in_reg = (dst != backend::Reg::None && backend::reg_is_xmm(dst));
                int32_t off = in_reg ? phi_scratch_base : get_spill_offset(inst->result_id);
                encoder.mov_ri64(backend::Reg::R11, (int64_t)raw_bits);
                encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), backend::Reg::R11);
                if (in_reg) {
                    encoder.movsd_xm(dst, backend::mem(backend::Reg::RBP, -off, 8));
                }
                break;
            }
            case ir::IROp::Copy: {
                ir::IRType* ct = fn->get_value(inst->operands[0]).type;
                if (ct && ct->is_floating_point()) {
                    backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                    store_val(inst->result_id, src);
                } else {
                    backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                    store_val(inst->result_id, src);
                }
                break;
            }
            case ir::IROp::Add: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.add_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.add_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Sub: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.sub_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.sub_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Mul: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.imul_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.imul_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::SDiv: {
                backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                if (rhs == backend::Reg::RAX || rhs == backend::Reg::RDX) {
                    encoder.mov_rr(backend::Reg::R10, rhs);
                    rhs = backend::Reg::R10;
                }
                backend::LabelId non_zero_lbl = encoder.create_label();
                backend::LabelId done_lbl = encoder.create_label();
                encoder.test_rr(rhs, rhs);
                encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                encoder.xor_rr(backend::Reg::RAX, backend::Reg::RAX);
                encoder.jmp_label(done_lbl);
                encoder.bind_label(non_zero_lbl);
                if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                backend::LabelId do_div_lbl = encoder.create_label();
                encoder.cmp_ri(rhs, -1);
                encoder.jcc_label(backend::CondCode::NE, do_div_lbl);
                encoder.mov_ri64(backend::Reg::R11, static_cast<int64_t>(0x8000000000000000ULL));
                encoder.cmp_rr(backend::Reg::RAX, backend::Reg::R11);
                encoder.jcc_label(backend::CondCode::NE, do_div_lbl);
                encoder.jmp_label(done_lbl);
                encoder.bind_label(do_div_lbl);
                encoder.cqo();
                encoder.idiv_r(rhs);
                encoder.bind_label(done_lbl);
                store_val(inst->result_id, backend::Reg::RAX);
                break;
            }
            case ir::IROp::SRem: {
                backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                backend::Reg rhs = load_val(inst->operands[1], backend::Reg::R10);
                if (rhs == backend::Reg::RAX || rhs == backend::Reg::RDX) {
                    encoder.mov_rr(backend::Reg::R10, rhs);
                    rhs = backend::Reg::R10;
                }
                backend::LabelId non_zero_lbl = encoder.create_label();
                backend::LabelId done_lbl = encoder.create_label();
                encoder.test_rr(rhs, rhs);
                encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                encoder.xor_rr(backend::Reg::RDX, backend::Reg::RDX);
                encoder.jmp_label(done_lbl);
                encoder.bind_label(non_zero_lbl);
                if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                backend::LabelId do_rem_lbl = encoder.create_label();
                encoder.cmp_ri(rhs, -1);
                encoder.jcc_label(backend::CondCode::NE, do_rem_lbl);
                encoder.mov_ri64(backend::Reg::R11, static_cast<int64_t>(0x8000000000000000ULL));
                encoder.cmp_rr(backend::Reg::RAX, backend::Reg::R11);
                encoder.jcc_label(backend::CondCode::NE, do_rem_lbl);
                encoder.xor_rr(backend::Reg::RDX, backend::Reg::RDX);
                encoder.jmp_label(done_lbl);
                encoder.bind_label(do_rem_lbl);
                encoder.cqo();
                encoder.idiv_r(rhs);
                encoder.bind_label(done_lbl);
                store_val(inst->result_id, backend::Reg::RDX);
                break;
            }
            case ir::IROp::And: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.and_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.and_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Or: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.or_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.or_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Xor: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                if (out == rhs) {
                    encoder.mov_rr(backend::Reg::R9, lhs);
                    encoder.xor_rr(backend::Reg::R9, rhs);
                    store_val(inst->result_id, backend::Reg::R9);
                } else {
                    if (lhs != out) encoder.mov_rr(out, lhs);
                    encoder.xor_rr(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Neg: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                backend::Reg src = load_val(inst->operands[0], out);
                if (src != out) encoder.mov_rr(out, src);
                encoder.neg_r(out);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::Not: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                backend::Reg src = load_val(inst->operands[0], out);
                if (src != out) encoder.mov_rr(out, src);
                encoder.not_r(out);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::Shl: {
                backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                backend::Reg rhs = load_val(inst->operands[1], backend::Reg::RCX);
                if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                if (rhs != backend::Reg::RCX) encoder.mov_rr(backend::Reg::RCX, rhs);
                encoder.shl_rcl(backend::Reg::RAX);
                store_val(inst->result_id, backend::Reg::RAX);
                break;
            }
            case ir::IROp::AShr: {
                backend::Reg lhs = load_val(inst->operands[0], backend::Reg::RAX);
                backend::Reg rhs = load_val(inst->operands[1], backend::Reg::RCX);
                if (lhs != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, lhs);
                if (rhs != backend::Reg::RCX) encoder.mov_rr(backend::Reg::RCX, rhs);
                encoder.sar_rcl(backend::Reg::RAX);
                store_val(inst->result_id, backend::Reg::RAX);
                break;
            }
            case ir::IROp::ICmp: {
                backend::Reg lhs, rhs;
                load_binary_gpr(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                encoder.cmp_rr(lhs, rhs);
                backend::CondCode cc = icmp_to_cond_code(inst->icmp_cond);
                encoder.setcc_r(cc, backend::Reg::R11B);
                encoder.movzx_rr8(out, backend::Reg::R11B);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::Alloca: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                int32_t off = alloca_offsets.count(inst->result_id) ? alloca_offsets.at(inst->result_id) : get_spill_offset(inst->result_id);
                encoder.lea_rm(out, backend::mem(backend::Reg::RBP, -off, 8));
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::Load: {
                ir::IRType* rt = fn->get_value(inst->result_id).type;
                backend::Reg ptr = load_val(inst->operands[0], backend::Reg::R10);
                if (rt && rt->is_floating_point()) {
                    backend::Reg dst = get_loc_reg(inst->result_id);
                    backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                    encoder.movsd_xm(out, backend::mem(ptr, 0, 8));
                    store_val(inst->result_id, out);
                } else {
                    backend::Reg dst = get_loc_reg(inst->result_id);
                    backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                    uint8_t ld_size = 8;
                    if (rt) {
                        ld_size = static_cast<uint8_t>(rt->size());
                        if (ld_size == 0) ld_size = 8;
                    }
                    if (ld_size == 1) {
                        encoder.movzx_rm8(out, backend::mem(ptr, 0, 1));
                    } else if (ld_size == 2) {
                        encoder.movzx_rm16(out, backend::mem(ptr, 0, 2));
                    } else if (ld_size == 4) {
                        encoder.mov_rm(backend::reg_with_size(out, 4), backend::mem(ptr, 0, 4));
                    } else {
                        encoder.mov_rm(out, backend::mem(ptr, 0, 8));
                    }
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::Store: {
                backend::Reg ptr = load_val(inst->operands[1], backend::Reg::R10);
                ir::IRType* val_t = fn->get_value(inst->operands[0]).type;
                if (val_t && val_t->is_floating_point()) {
                    backend::Reg val = load_val(inst->operands[0], backend::Reg::XMM0);
                    encoder.movsd_mx(backend::mem(ptr, 0, 8), val);
                } else {
                    backend::Reg val = load_val(inst->operands[0], backend::Reg::R11);
                    uint8_t st_size = 8;
                    ir::IRType* ptr_t = fn->get_value(inst->operands[1]).type;
                    if (ptr_t && ptr_t->element_type) {
                        st_size = static_cast<uint8_t>(ptr_t->element_type->size());
                        if (st_size == 0) st_size = 8;
                    }
                    backend::Reg val_sized = backend::reg_with_size(val, st_size);
                    encoder.mov_mr(backend::mem(ptr, 0, st_size), val_sized);
                }
                break;
            }
            case ir::IROp::GetElementPtr: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                backend::Reg base = load_val(inst->operands[0], backend::Reg::RAX);
                encoder.mov_rr(out, base);
                if (inst->gep_field_index >= 0) {
                    int64_t byte_off = static_cast<int64_t>(inst->gep_field_index) * 8;
                    ir::IRType* base_t = fn->get_value(inst->operands[0]).type;
                    if (base_t && base_t->element_type && base_t->element_type->is_struct()) {
                        byte_off = static_cast<int64_t>(base_t->element_type->get_field_offset(static_cast<uint32_t>(inst->gep_field_index)));
                    }
                    if (byte_off != 0) {
                        encoder.add_ri(out, static_cast<int32_t>(byte_off));
                    }
                } else if (inst->operands.size() > 1) {
                    int64_t elem_sz = 8;
                    ir::IRType* base_t = fn->get_value(inst->operands[0]).type;
                    if (base_t && base_t->element_type) {
                        elem_sz = static_cast<int64_t>(base_t->element_type->size());
                        if (elem_sz <= 0) elem_sz = 1;
                    }
                    backend::Reg idx = load_val(inst->operands[1], backend::Reg::R10);
                    if (elem_sz != 1) {
                        encoder.imul_rri(backend::Reg::R10, idx, static_cast<int32_t>(elem_sz));
                        encoder.add_rr(out, backend::Reg::R10);
                    } else {
                        encoder.add_rr(out, idx);
                    }
                }
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::Trunc:
            case ir::IROp::SExt:
            case ir::IROp::ZExt:
            case ir::IROp::PtrToInt:
            case ir::IROp::IntToPtr:
            case ir::IROp::Bitcast: {
                ir::IRType* src_t = fn->get_value(inst->operands[0]).type;
                ir::IRType* dst_t = inst->type;
                bool src_fp = src_t && src_t->is_floating_point();
                bool dst_fp = dst_t && dst_t->is_floating_point();
                if (src_fp && !dst_fp) {
                    backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                    backend::Reg dst = get_loc_reg(inst->result_id);
                    backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::RAX;
                    encoder.movq_rx(out, src);
                    store_val(inst->result_id, out);
                } else if (!src_fp && dst_fp) {
                    backend::Reg src = load_val(inst->operands[0], backend::Reg::RAX);
                    backend::Reg dst = get_loc_reg(inst->result_id);
                    backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                    encoder.movq_xr(out, src);
                    store_val(inst->result_id, out);
                } else {
                    backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                    store_val(inst->result_id, src);
                }
                break;
            }
            case ir::IROp::Phi: {
                // Handled at predecessor jump edges
                break;
            }
            default:
                return false;
        }
        return true;
    }

} // namespace grass::codegen
