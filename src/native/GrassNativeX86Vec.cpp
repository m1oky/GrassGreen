#include "native/GrassNativeX86Ctx.h"
#include <cstring>

namespace grass::codegen {

    bool X86FuncCodegen::emit_ctrl_fp_vec(const ir::BasicBlock* bb, const ir::IRInst* inst) {
        const auto& options_ = options;
        switch (inst->op) {
            case ir::IROp::Call: {
                // Check for bare-metal intrinsics first
                if (inst->callee_name == "port_in8" || inst->callee_name == "io_in8" || inst->callee_name == "inb") {
                    backend::Reg p = load_val(inst->operands[0], backend::Reg::RDX);
                    if (p != backend::Reg::RDX) encoder.mov_rr(backend::Reg::RDX, p);
                    encoder.in_al_dx();
                    encoder.movzx_rr8(backend::Reg::RAX, backend::Reg::AL);
                    if (inst->result_id != ir::kInvalidValueId) {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                    break;
                }
                if (inst->callee_name == "port_out8" || inst->callee_name == "io_out8" || inst->callee_name == "outb") {
                    backend::Reg p = load_val(inst->operands[0], backend::Reg::RDX);
                    backend::Reg v = load_val(inst->operands[1], backend::Reg::RAX);
                    if (v == backend::Reg::RDX && p != backend::Reg::RDX) {
                        encoder.xchg_rr(backend::Reg::RDX, p);
                        encoder.mov_rr(backend::Reg::RAX, p);
                    } else {
                        if (p != backend::Reg::RDX) encoder.mov_rr(backend::Reg::RDX, p);
                        if (v != backend::Reg::RAX) encoder.mov_rr(backend::Reg::RAX, v);
                    }
                    encoder.out_dx_al();
                    break;
                }
                if (inst->callee_name == "cpu_cli" || inst->callee_name == "cli") {
                    encoder.cli();
                    break;
                }
                if (inst->callee_name == "cpu_sti" || inst->callee_name == "sti") {
                    encoder.sti();
                    break;
                }
                if (inst->callee_name == "cpu_hlt" || inst->callee_name == "hlt") {
                    encoder.hlt();
                    break;
                }
                if (inst->callee_name == "memory_barrier" || inst->callee_name == "read_barrier" || inst->callee_name == "write_barrier") {
                    encoder.mfence();
                    break;
                }
                if (inst->callee_name == "mmio_read8") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    encoder.movzx_rm8(backend::Reg::RAX, backend::mem(addr, 0, 1));
                    if (inst->result_id != ir::kInvalidValueId) {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                    break;
                }
                if (inst->callee_name == "mmio_read16") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    encoder.movzx_rm16(backend::Reg::RAX, backend::mem(addr, 0, 2));
                    if (inst->result_id != ir::kInvalidValueId) {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                    break;
                }
                if (inst->callee_name == "mmio_read32") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    encoder.mov_rm(backend::Reg::EAX, backend::mem(addr, 0, 4));
                    if (inst->result_id != ir::kInvalidValueId) {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                    break;
                }
                if (inst->callee_name == "mmio_read64") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    encoder.mov_rm(backend::Reg::RAX, backend::mem(addr, 0, 8));
                    if (inst->result_id != ir::kInvalidValueId) {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                    break;
                }
                if (inst->callee_name == "mmio_write8") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                    backend::Reg val8 = backend::reg_with_size(val, 1);
                    encoder.mov_mr(backend::mem(addr, 0, 1), val8);
                    break;
                }
                if (inst->callee_name == "mmio_write16") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                    backend::Reg val16 = backend::reg_with_size(val, 2);
                    encoder.mov_mr(backend::mem(addr, 0, 2), val16);
                    break;
                }
                if (inst->callee_name == "mmio_write32") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                    backend::Reg val32 = backend::reg_with_size(val, 4);
                    encoder.mov_mr(backend::mem(addr, 0, 4), val32);
                    break;
                }
                if (inst->callee_name == "mmio_write64") {
                    backend::Reg addr = load_val(inst->operands[0], backend::Reg::R10);
                    backend::Reg val = load_val(inst->operands[1], backend::Reg::R11);
                    backend::Reg val64 = backend::reg_with_size(val, 8);
                    encoder.mov_mr(backend::mem(addr, 0, 8), val64);
                    break;
                }

                // Check if callee is a string/global symbol in rodata or writable data
                if (rodata_symbols.count(inst->callee_name) || module.data_globals.count(inst->callee_name)) {
                    encoder.lea_symbol(backend::Reg::RAX, inst->callee_name);
                    store_val(inst->result_id, backend::Reg::RAX);
                    break;
                }

                if ((inst->callee_name == "system" || inst->callee_name == "clear" ||
                     inst->callee_name == "cls" || inst->callee_name == "clear_all" ||
                     inst->callee_name == "cls_all" || inst->callee_name == "grass_bm_color_int" ||
                     inst->callee_name == "grass_bm_color_str" || inst->callee_name == "color" ||
                     inst->callee_name == "draw_box" || inst->callee_name == "progress_bar" ||
                     inst->callee_name == "SetConsoleTextAttribute") &&
                    !options_.freestanding &&
                    options_.arch == TargetArch::X86_64 && options_.abi == TargetABI::Win64) {
                    encoder.call_extern("gout_flush");
                }

                // Pass arguments according to ABI
                // Phase 1: spill all operands to argument scratch slots at [RSP + i*8] to avoid register clobbering
                for (size_t i = 0; i < inst->operands.size(); ++i) {
                    ir::IRType* arg_t = fn->get_value(inst->operands[i]).type;
                    bool is_fp = arg_t && arg_t->is_floating_point();
                    int32_t temp_off = static_cast<int32_t>(i * 8);
                    if (is_fp) {
                        backend::Reg src = load_val(inst->operands[i], backend::Reg::XMM0);
                        encoder.movsd_mx(backend::mem(backend::Reg::RSP, temp_off, 8), src);
                    } else {
                        backend::Reg src = load_val(inst->operands[i], backend::Reg::R10);
                        encoder.mov_mr(backend::mem(backend::Reg::RSP, temp_off, 8), src);
                    }
                }

                // Phase 2: load from scratch slots into ABI target registers and stack parameter slots
                for (size_t i = 0; i < inst->operands.size(); ++i) {
                    ir::IRType* arg_t = fn->get_value(inst->operands[i]).type;
                    bool is_fp = arg_t && arg_t->is_floating_point();
                    int32_t temp_off = static_cast<int32_t>(i * 8);
                    if (i < arg_regs.size()) {
                        if (is_fp) {
                            backend::Reg xmm_reg = static_cast<backend::Reg>(0x0050 + i);
                            encoder.movsd_xm(xmm_reg, backend::mem(backend::Reg::RSP, temp_off, 8));
                        } else {
                            backend::Reg gpr_reg = arg_regs[i];
                            encoder.mov_rm(gpr_reg, backend::mem(backend::Reg::RSP, temp_off, 8));
                        }
                    } else {
                        int32_t stack_off = (options_.abi == TargetABI::Win64)
                            ? (32 + (i - 4) * 8)
                            : ((i - arg_regs.size()) * 8);
                        if (stack_off != temp_off) {
                            if (is_fp) {
                                encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RSP, temp_off, 8));
                                encoder.movsd_mx(backend::mem(backend::Reg::RSP, stack_off, 8), backend::Reg::XMM0);
                            } else {
                                encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RSP, temp_off, 8));
                                encoder.mov_mr(backend::mem(backend::Reg::RSP, stack_off, 8), backend::Reg::R10);
                            }
                        }
                    }
                }

                encoder.call_extern(inst->callee_name);

                if (inst->result_id != ir::kInvalidValueId) {
                    ir::IRType* res_t = inst->type;
                    if (res_t && res_t->is_floating_point()) {
                        store_val(inst->result_id, backend::Reg::XMM0);
                    } else {
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                }
                break;
            }
            case ir::IROp::Jump: {
                emit_phi_copies(const_cast<ir::BasicBlock*>(bb), inst->true_target);
                encoder.jmp_label(bb_labels[inst->true_target]);
                break;
            }
            case ir::IROp::Branch: {
                backend::Reg cond = load_val(inst->operands[0], backend::Reg::R10);
                encoder.test_rr(cond, cond);
                backend::LabelId skip_true = encoder.create_label();
                encoder.jcc_label(backend::CondCode::Z, skip_true);
                emit_phi_copies(const_cast<ir::BasicBlock*>(bb), inst->true_target);
                encoder.jmp_label(bb_labels[inst->true_target]);
                encoder.bind_label(skip_true);
                emit_phi_copies(const_cast<ir::BasicBlock*>(bb), inst->false_target);
                encoder.jmp_label(bb_labels[inst->false_target]);
                break;
            }
            case ir::IROp::Ret: {
                if (!inst->operands.empty()) {
                    ir::IRType* ret_t = fn->get_value(inst->operands[0]).type;
                    if (ret_t && ret_t->is_floating_point()) {
                        backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM0);
                        if (src != backend::Reg::XMM0) {
                            if (backend::reg_is_xmm(src)) encoder.movsd_xx(backend::Reg::XMM0, src);
                            else encoder.cvtsi2sd_xr(backend::Reg::XMM0, src);
                        }
                    } else {
                        backend::Reg src = load_val(inst->operands[0], backend::Reg::RAX);
                        if (src != backend::Reg::RAX) {
                            if (backend::reg_is_gpr(src)) encoder.mov_rr(backend::Reg::RAX, src);
                            else encoder.cvttsd2si_rx(backend::Reg::RAX, src);
                        }
                    }
                }
                encoder.jmp_label(epilogue_label);
                break;
            }
            case ir::IROp::FAdd: {
                backend::Reg lhs, rhs;
                load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                if (out == rhs) {
                    encoder.movsd_xx(backend::Reg::XMM2, lhs);
                    encoder.addsd_xx(backend::Reg::XMM2, rhs);
                    store_val(inst->result_id, backend::Reg::XMM2);
                } else {
                    if (out != lhs) encoder.movsd_xx(out, lhs);
                    encoder.addsd_xx(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::FSub: {
                backend::Reg lhs, rhs;
                load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                if (out == rhs) {
                    encoder.movsd_xx(backend::Reg::XMM2, lhs);
                    encoder.subsd_xx(backend::Reg::XMM2, rhs);
                    store_val(inst->result_id, backend::Reg::XMM2);
                } else {
                    if (out != lhs) encoder.movsd_xx(out, lhs);
                    encoder.subsd_xx(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::FMul: {
                backend::Reg lhs, rhs;
                load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                if (out == rhs) {
                    encoder.movsd_xx(backend::Reg::XMM2, lhs);
                    encoder.mulsd_xx(backend::Reg::XMM2, rhs);
                    store_val(inst->result_id, backend::Reg::XMM2);
                } else {
                    if (out != lhs) encoder.movsd_xx(out, lhs);
                    encoder.mulsd_xx(out, rhs);
                    store_val(inst->result_id, out);
                }
                break;
            }
            case ir::IROp::FDiv: {
                backend::Reg lhs, rhs;
                load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                backend::Reg res_reg = (out == rhs) ? backend::Reg::XMM2 : out;
                backend::LabelId non_zero_lbl = encoder.create_label();
                backend::LabelId done_lbl = encoder.create_label();
                encoder.xorpd_xx(backend::Reg::XMM3, backend::Reg::XMM3);
                encoder.ucomisd_xx(rhs, backend::Reg::XMM3);
                encoder.jcc_label(backend::CondCode::NE, non_zero_lbl);
                encoder.jcc_label(backend::CondCode::P, non_zero_lbl);
                encoder.xorpd_xx(res_reg, res_reg);
                encoder.jmp_label(done_lbl);
                encoder.bind_label(non_zero_lbl);
                if (res_reg != lhs) encoder.movsd_xx(res_reg, lhs);
                encoder.divsd_xx(res_reg, rhs);
                encoder.bind_label(done_lbl);
                store_val(inst->result_id, res_reg);
                break;
            }
            case ir::IROp::FCmp: {
                backend::Reg lhs, rhs;
                load_binary_fp(inst->operands[0], inst->operands[1], lhs, rhs);
                encoder.ucomisd_xx(lhs, rhs);
                backend::CondCode cc = backend::CondCode::E;
                switch (inst->fcmp_cond) {
                    case ir::FCmpCond::OEQ: cc = backend::CondCode::E; break;
                    case ir::FCmpCond::ONE: cc = backend::CondCode::NE; break;
                    case ir::FCmpCond::OLT: cc = backend::CondCode::B; break;
                    case ir::FCmpCond::OLE: cc = backend::CondCode::BE; break;
                    case ir::FCmpCond::OGT: cc = backend::CondCode::A; break;
                    case ir::FCmpCond::OGE: cc = backend::CondCode::AE; break;
                }
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R11;
                encoder.setcc_r(cc, backend::Reg::R11B);
                encoder.movzx_rr8(out, backend::Reg::R11B);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::FNeg: {
                backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM1);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                encoder.xorpd_xx(backend::Reg::XMM2, backend::Reg::XMM2);
                encoder.subsd_xx(backend::Reg::XMM2, src);
                if (out != backend::Reg::XMM2) encoder.movsd_xx(out, backend::Reg::XMM2);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::SIToFP: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM2;
                backend::Reg src = load_val(inst->operands[0], backend::Reg::R10);
                encoder.cvtsi2sd_xr(out, src);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::FPToSI: {
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg out = (dst != backend::Reg::None && backend::reg_is_gpr(dst)) ? dst : backend::Reg::R10;
                backend::Reg src = load_val(inst->operands[0], backend::Reg::XMM2);
                encoder.cvttsd2si_rx(out, src);
                store_val(inst->result_id, out);
                break;
            }
            case ir::IROp::VecZero: {
                ir::IRType* vt = inst->type;
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                backend::Reg vout = to_vec_reg(xmm_out, vt);
                if (vt && vt->array_len == 4) {
                    encoder.vpxor_yyy(vout, vout, vout);
                } else {
                    encoder.pxor_xx(vout, vout);
                }
                store_val(inst->result_id, vout);
                break;
            }
            case ir::IROp::VecSplat: {
                ir::IRType* vt = inst->type;
                bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                backend::Reg vout = to_vec_reg(xmm_out, vt);
                if (is_fp) {
                    backend::Reg sreg = load_val(inst->operands[0], backend::Reg::XMM1);
                    if (sreg != xmm_out) encoder.movsd_xx(xmm_out, sreg);
                    if (vt && vt->array_len == 4) {
                        encoder.vbroadcastsd_yx(vout, xmm_out);
                    } else {
                        encoder.shufpd_xxi(xmm_out, xmm_out, 0);
                    }
                } else {
                    backend::Reg sreg = load_val(inst->operands[0], backend::Reg::RAX);
                    encoder.movq_xr(xmm_out, sreg);
                    if (vt && vt->array_len == 4) {
                        encoder.vpbroadcastq_yx(vout, xmm_out);
                    } else {
                        encoder.shufpd_xxi(xmm_out, xmm_out, 0);
                    }
                }
                store_val(inst->result_id, vout);
                break;
            }
            case ir::IROp::VecLoad: {
                ir::IRType* vt = inst->type;
                bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                backend::Reg ptr = load_val(inst->operands[0], backend::Reg::R10);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM0;
                backend::Reg vout = to_vec_reg(xmm_out, vt);
                if (vt && vt->array_len == 4) {
                    if (is_fp) encoder.vmovupd_ym(vout, backend::mem(ptr, 0, 32));
                    else encoder.vmovdqu_ym(vout, backend::mem(ptr, 0, 32));
                } else {
                    if (is_fp) encoder.movupd_xm(vout, backend::mem(ptr, 0, 16));
                    else encoder.movdqu_xm(vout, backend::mem(ptr, 0, 16));
                }
                store_val(inst->result_id, vout);
                break;
            }
            case ir::IROp::VecStore: {
                ir::IRType* vt = fn->get_value(inst->operands[0]).type;
                bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                backend::Reg vreg = load_val(inst->operands[0], backend::Reg::XMM0);
                backend::Reg ptr = load_val(inst->operands[1], backend::Reg::R10);
                if (vt && vt->array_len == 4) {
                    if (is_fp) encoder.vmovupd_my(backend::mem(ptr, 0, 32), vreg);
                    else encoder.vmovdqu_my(backend::mem(ptr, 0, 32), vreg);
                } else {
                    if (is_fp) encoder.movupd_mx(backend::mem(ptr, 0, 16), vreg);
                    else encoder.movdqu_mx(backend::mem(ptr, 0, 16), vreg);
                }
                break;
            }
            case ir::IROp::VecAdd:
            case ir::IROp::VecSub:
            case ir::IROp::VecMul: {
                ir::IRType* vt = inst->type;
                bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                uint64_t lanes = vt ? vt->array_len : 2;
                backend::Reg lhs = load_val(inst->operands[0], backend::Reg::XMM0);
                backend::Reg rhs = load_val(inst->operands[1], backend::Reg::XMM1);
                backend::Reg dst = get_loc_reg(inst->result_id);
                backend::Reg xmm_out = (dst != backend::Reg::None && backend::reg_is_xmm(dst)) ? dst : backend::Reg::XMM2;
                backend::Reg vout = to_vec_reg(xmm_out, vt);

                if (lanes == 4) {
                    if (inst->op == ir::IROp::VecAdd) {
                        if (is_fp) encoder.vaddpd_yyy(vout, lhs, rhs);
                        else encoder.vpaddq_yyy(vout, lhs, rhs);
                    } else if (inst->op == ir::IROp::VecSub) {
                        if (is_fp) encoder.vsubpd_yyy(vout, lhs, rhs);
                        else encoder.vpsubq_yyy(vout, lhs, rhs);
                    } else {
                        encoder.vmulpd_yyy(vout, lhs, rhs);
                    }
                    store_val(inst->result_id, vout);
                } else {
                    auto emit_sse_op = [&](backend::Reg d, backend::Reg s) {
                        if (inst->op == ir::IROp::VecAdd) {
                            if (is_fp) encoder.addpd_xx(d, s);
                            else encoder.paddq_xx(d, s);
                        } else if (inst->op == ir::IROp::VecSub) {
                            if (is_fp) encoder.subpd_xx(d, s);
                            else encoder.psubq_xx(d, s);
                        } else {
                            encoder.mulpd_xx(d, s);
                        }
                    };
                    if (vout == rhs && vout != lhs) {
                        backend::Reg tmp = backend::Reg::XMM2;
                        if (lhs != backend::Reg::XMM0 && rhs != backend::Reg::XMM0) tmp = backend::Reg::XMM0;
                        else if (lhs != backend::Reg::XMM1 && rhs != backend::Reg::XMM1) tmp = backend::Reg::XMM1;
                        encoder.movdqu_xx(tmp, lhs);
                        emit_sse_op(tmp, rhs);
                        store_val(inst->result_id, tmp);
                    } else {
                        if (vout != lhs) encoder.movdqu_xx(vout, lhs);
                        emit_sse_op(vout, rhs);
                        store_val(inst->result_id, vout);
                    }
                }
                break;
            }
            case ir::IROp::VecHAdd: {
                ir::IRType* vt = fn->get_value(inst->operands[0]).type;
                bool is_fp = vt && vt->element_type && vt->element_type->is_floating_point();
                uint64_t lanes = vt ? vt->array_len : 2;
                backend::Reg vsrc = load_val(inst->operands[0], backend::Reg::XMM0);
                if (lanes == 4) {
                    backend::Reg hi_tmp = (backend::reg_raw_index(vsrc) == 1) ? backend::Reg::XMM2 : backend::Reg::XMM1;
                    if (is_fp) {
                        encoder.vextractf128_xyi(hi_tmp, vsrc, 1);
                        if (backend::reg_raw_index(vsrc) != 0) encoder.vextractf128_xyi(backend::Reg::XMM0, vsrc, 0);
                        encoder.addpd_xx(backend::Reg::XMM0, hi_tmp);
                        encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                        encoder.addsd_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                        encoder.vzeroupper();
                        store_val(inst->result_id, backend::Reg::XMM0);
                    } else {
                        encoder.vextracti128_xyi(hi_tmp, vsrc, 1);
                        if (backend::reg_raw_index(vsrc) != 0) encoder.vextracti128_xyi(backend::Reg::XMM0, vsrc, 0);
                        encoder.paddq_xx(backend::Reg::XMM0, hi_tmp);
                        encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                        encoder.paddq_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                        encoder.vzeroupper();
                        encoder.movq_rx(backend::Reg::RAX, backend::Reg::XMM0);
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                } else {
                    if (is_fp) {
                        if (vsrc != backend::Reg::XMM0) encoder.movupd_xx(backend::Reg::XMM0, vsrc);
                        encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                        encoder.addsd_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                        store_val(inst->result_id, backend::Reg::XMM0);
                    } else {
                        if (vsrc != backend::Reg::XMM0) encoder.movdqu_xx(backend::Reg::XMM0, vsrc);
                        encoder.movhlps_xx(backend::Reg::XMM1, backend::Reg::XMM0);
                        encoder.paddq_xx(backend::Reg::XMM0, backend::Reg::XMM1);
                        encoder.movq_rx(backend::Reg::RAX, backend::Reg::XMM0);
                        store_val(inst->result_id, backend::Reg::RAX);
                    }
                }
                break;
            }
            default:
                return false;
        }
        return true;
    }

} // namespace grass::codegen
