#include "native/GrassNativeX86Ctx.h"
#include <algorithm>
#include <cstring>

namespace grass::codegen {

    backend::Reg phys_reg_to_backend(ir::PhysReg pr, TargetArch arch) {
        if (pr == ir::PhysReg::None) return backend::Reg::None;
        int idx = static_cast<int>(pr);
        if (idx >= static_cast<int>(ir::PhysReg::RAX) && idx <= static_cast<int>(ir::PhysReg::R15)) {
            int gpr = idx - static_cast<int>(ir::PhysReg::RAX);
            return (arch == TargetArch::X86_64) ? static_cast<backend::Reg>(0x0030 + gpr)
                                                : static_cast<backend::Reg>(0x0020 + gpr);
        }
        if (idx >= static_cast<int>(ir::PhysReg::XMM0) && idx <= static_cast<int>(ir::PhysReg::XMM15)) {
            int xmm = idx - static_cast<int>(ir::PhysReg::XMM0);
            return static_cast<backend::Reg>(0x0050 + xmm);
        }
        return backend::Reg::None;
    }

    backend::CondCode icmp_to_cond_code(ir::ICmpCond cond) {
        switch (cond) {
            case ir::ICmpCond::EQ:  return backend::CondCode::E;
            case ir::ICmpCond::NE:  return backend::CondCode::NE;
            case ir::ICmpCond::SLT: return backend::CondCode::L;
            case ir::ICmpCond::SLE: return backend::CondCode::LE;
            case ir::ICmpCond::SGT: return backend::CondCode::G;
            case ir::ICmpCond::SGE: return backend::CondCode::GE;
            case ir::ICmpCond::ULT: return backend::CondCode::B;
            case ir::ICmpCond::ULE: return backend::CondCode::BE;
            case ir::ICmpCond::UGT: return backend::CondCode::A;
            case ir::ICmpCond::UGE: return backend::CondCode::AE;
        }
        return backend::CondCode::E;
    }

    backend::Reg X86FuncCodegen::get_loc_reg(ir::ValueId val) const {

        auto it = alloc_res.intervals.find(val);
        if (it != alloc_res.intervals.end() && !it->second.is_spilled) {
            return phys_reg_to_backend(it->second.reg, options.arch);
        }
        return backend::Reg::None;
    }

    int32_t X86FuncCodegen::get_spill_offset(ir::ValueId val) const {

        auto it = alloc_res.intervals.find(val);
        if (it != alloc_res.intervals.end() && it->second.is_spilled) {
            return spill_base_offset + it->second.stack_slot_offset;
        }
        return spill_base_offset + static_cast<int32_t>(val * 8);
    }

    backend::Reg X86FuncCodegen::to_vec_reg(backend::Reg xmm_r, ir::IRType* vtype) const {

        if (vtype && vtype->is_vector() && vtype->array_len == 4 && backend::reg_is_xmm(xmm_r)) {
            return static_cast<backend::Reg>(0x0070 | backend::reg_raw_index(xmm_r));
        }
        return xmm_r;
    }

    backend::Reg X86FuncCodegen::to_xmm_reg(backend::Reg r) const {

        if (backend::reg_is_ymm(r)) {
            return static_cast<backend::Reg>(0x0050 | backend::reg_raw_index(r));
        }
        return r;
    }

    backend::Reg X86FuncCodegen::load_val(ir::ValueId val, backend::Reg temp_reg) {

        backend::Reg r = get_loc_reg(val);
        ir::IRType* vtype = fn->get_value(val).type;
        if (r != backend::Reg::None) {
            return (vtype && vtype->is_vector()) ? to_vec_reg(r, vtype) : r;
        }
        int32_t off = get_spill_offset(val);
        if (vtype && vtype->is_vector()) {
            backend::Reg base_xmm = backend::reg_is_xmm(temp_reg) ? temp_reg : backend::Reg::XMM0;
            backend::Reg vtmp = to_vec_reg(base_xmm, vtype);
            if (vtype->array_len == 4) {
                encoder.vmovdqu_ym(vtmp, backend::mem(backend::Reg::RBP, -off, 32));
            } else {
                encoder.movdqu_xm(vtmp, backend::mem(backend::Reg::RBP, -off, 16));
            }
            return vtmp;
        }
        if (vtype && vtype->is_floating_point()) {
            if (backend::reg_is_xmm(temp_reg)) {
                encoder.movsd_xm(temp_reg, backend::mem(backend::Reg::RBP, -off, 8));
            } else {
                encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 8));
                return backend::Reg::XMM0;
            }
        } else {
            encoder.mov_rm(temp_reg, backend::mem(backend::Reg::RBP, -off, 8));
        }
        return temp_reg;
    }

    void X86FuncCodegen::store_val(ir::ValueId val, backend::Reg src_reg) {

        backend::Reg r = get_loc_reg(val);
        ir::IRType* vtype = fn->get_value(val).type;

        if (vtype && vtype->is_vector()) {
            if (r != backend::Reg::None) {
                backend::Reg vr = to_vec_reg(r, vtype);
                if (vr != src_reg) {
                    if (vtype->array_len == 4) encoder.vmovdqu_yy(vr, src_reg);
                    else encoder.movdqu_xx(vr, src_reg);
                }
            } else {
                int32_t off = get_spill_offset(val);
                if (vtype->array_len == 4) {
                    encoder.vmovdqu_my(backend::mem(backend::Reg::RBP, -off, 32), src_reg);
                } else {
                    encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), src_reg);
                }
            }
            return;
        }

        if (r != backend::Reg::None) {
            if (r != src_reg) {
                if (backend::reg_is_xmm(r) && backend::reg_is_xmm(src_reg)) {
                    encoder.movsd_xx(r, src_reg);
                } else if (backend::reg_is_gpr(r) && backend::reg_is_gpr(src_reg)) {
                    encoder.mov_rr(r, src_reg);
                } else if (backend::reg_is_gpr(r) && backend::reg_is_xmm(src_reg)) {
                    encoder.cvttsd2si_rx(r, src_reg);
                } else if (backend::reg_is_xmm(r) && backend::reg_is_gpr(src_reg)) {
                    encoder.cvtsi2sd_xr(r, src_reg);
                }
            }
        } else {
            int32_t off = get_spill_offset(val);
            if (backend::reg_is_xmm(src_reg)) {
                encoder.movsd_mx(backend::mem(backend::Reg::RBP, -off, 8), src_reg);
            } else {
                encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), src_reg);
            }
        }
    }

    void X86FuncCodegen::load_binary_fp(ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs) {

        backend::Reg r0 = get_loc_reg(v0);
        backend::Reg r1 = get_loc_reg(v1);

        if (r0 != backend::Reg::None && r1 != backend::Reg::None) {
            out_lhs = r0;
            out_rhs = r1;
            return;
        }
        if (r0 != backend::Reg::None && r1 == backend::Reg::None) {
            out_lhs = r0;
            backend::Reg tmp = (r0 == backend::Reg::XMM1) ? backend::Reg::XMM2 : backend::Reg::XMM1;
            int32_t off = get_spill_offset(v1);
            encoder.movsd_xm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
            out_rhs = tmp;
            return;
        }
        if (r0 == backend::Reg::None && r1 != backend::Reg::None) {
            out_rhs = r1;
            backend::Reg tmp = (r1 == backend::Reg::XMM0) ? backend::Reg::XMM2 : backend::Reg::XMM0;
            int32_t off = get_spill_offset(v0);
            encoder.movsd_xm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
            out_lhs = tmp;
            return;
        }
        int32_t off0 = get_spill_offset(v0);
        int32_t off1 = get_spill_offset(v1);
        encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off0, 8));
        encoder.movsd_xm(backend::Reg::XMM1, backend::mem(backend::Reg::RBP, -off1, 8));
        out_lhs = backend::Reg::XMM0;
        out_rhs = backend::Reg::XMM1;
    }

    void X86FuncCodegen::load_binary_gpr(ir::ValueId v0, ir::ValueId v1, backend::Reg& out_lhs, backend::Reg& out_rhs) {

        backend::Reg r0 = get_loc_reg(v0);
        backend::Reg r1 = get_loc_reg(v1);

        if (r0 != backend::Reg::None && r1 != backend::Reg::None) {
            out_lhs = r0;
            out_rhs = r1;
            return;
        }
        if (r0 != backend::Reg::None && r1 == backend::Reg::None) {
            out_lhs = r0;
            backend::Reg tmp = (r0 == backend::Reg::R10) ? backend::Reg::R11 : backend::Reg::R10;
            int32_t off = get_spill_offset(v1);
            encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
            out_rhs = tmp;
            return;
        }
        if (r0 == backend::Reg::None && r1 != backend::Reg::None) {
            out_rhs = r1;
            backend::Reg tmp = (r1 == backend::Reg::R11) ? backend::Reg::R10 : backend::Reg::R11;
            int32_t off = get_spill_offset(v0);
            encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, -off, 8));
            out_lhs = tmp;
            return;
        }
        int32_t off0 = get_spill_offset(v0);
        int32_t off1 = get_spill_offset(v1);
        encoder.mov_rm(backend::Reg::R11, backend::mem(backend::Reg::RBP, -off0, 8));
        encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RBP, -off1, 8));
        out_lhs = backend::Reg::R11;
        out_rhs = backend::Reg::R10;
    }

    void X86FuncCodegen::emit_phi_copies(ir::BasicBlock* from_bb, ir::BasicBlock* to_bb) {

        if (!to_bb) return;
        struct PhiMove {
            ir::ValueId src_val;
            ir::ValueId dst_val;
            bool is_fp;
            uint8_t vec_lanes;
        };
        std::vector<PhiMove> moves;
        for (const auto& phi_inst : to_bb->instructions) {
            if (phi_inst->op != ir::IROp::Phi) break;
            ir::ValueId in_val = phi_inst->get_phi_incoming_value(from_bb);
            if (in_val != ir::kInvalidValueId) {
                ir::IRType* vt = phi_inst->type;
                bool fp = vt && vt->is_floating_point();
                uint8_t vlanes = (vt && vt->is_vector()) ? static_cast<uint8_t>(vt->array_len) : 0;
                moves.push_back({ in_val, phi_inst->result_id, fp, vlanes });
            }
        }
        if (moves.empty()) return;

        // Pass 1: save all source values into stack scratch slots
        int32_t scratch_base = phi_scratch_base;
        for (size_t i = 0; i < moves.size(); ++i) {
            int32_t off = scratch_base + static_cast<int32_t>(i * 32);
            if (moves[i].vec_lanes == 4) {
                backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                encoder.vmovdqu_my(backend::mem(backend::Reg::RBP, -off, 32), r);
            } else if (moves[i].vec_lanes == 2) {
                backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), r);
            } else if (moves[i].is_fp) {
                backend::Reg r = load_val(moves[i].src_val, backend::Reg::XMM0);
                encoder.movsd_mx(backend::mem(backend::Reg::RBP, -off, 8), r);
            } else {
                backend::Reg r = load_val(moves[i].src_val, backend::Reg::R10);
                encoder.mov_mr(backend::mem(backend::Reg::RBP, -off, 8), r);
            }
        }

        // Pass 2: load from scratch slots into destination values
        for (size_t i = 0; i < moves.size(); ++i) {
            int32_t off = scratch_base + static_cast<int32_t>(i * 32);
            if (moves[i].vec_lanes == 4) {
                encoder.vmovdqu_ym(backend::Reg::YMM0, backend::mem(backend::Reg::RBP, -off, 32));
                store_val(moves[i].dst_val, backend::Reg::YMM0);
            } else if (moves[i].vec_lanes == 2) {
                encoder.movdqu_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 16));
                store_val(moves[i].dst_val, backend::Reg::XMM0);
            } else if (moves[i].is_fp) {
                encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, -off, 8));
                store_val(moves[i].dst_val, backend::Reg::XMM0);
            } else {
                encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RBP, -off, 8));
                store_val(moves[i].dst_val, backend::Reg::R10);
            }
        }
    }

    bool NativeCompiler::codegen_module(ir::IRModule& module,
                                        const std::vector<uint8_t>& rodata,
                                        const std::unordered_map<std::string, uint64_t>& rodata_symbols,
                                        CompiledUnit& unit,
                                        std::string& error) {
        (void)error;
        unit.arch = options_.arch;
        unit.abi = options_.abi;
        unit.text.clear();
        unit.rodata = rodata;
        unit.data.assign(static_cast<size_t>(module.data_globals_size), 0);
        unit.bss_size = 0;
        unit.symbols.clear();
        unit.relocations.clear();

        // Register rodata symbols
        for (const auto& [name, off] : rodata_symbols) {
            backend::SymbolDef sym;
            sym.name = name;
            sym.section = backend::SectionKind::RoData;
            sym.offset = off;
            sym.size = 0;
            sym.is_global = false;
            sym.is_external = false;
            unit.symbols.push_back(sym);
        }

        // Register writable global data symbols
        for (const auto& [name, off] : module.data_globals) {
            backend::SymbolDef sym;
            sym.name = name;
            sym.section = backend::SectionKind::Data;
            sym.offset = off;
            sym.size = 8;
            sym.is_global = false;
            sym.is_external = false;
            unit.symbols.push_back(sym);
        }

        backend::ABISpec abi_spec = backend::ABISpec::for_abi(options_.abi);
        ir::LinearScanAllocator allocator(options_.abi);

        backend::GrassX86Encoder encoder(options_.arch);
        grass::debug::GrassDebugTableBuilder debug_builder;

        for (auto& fn : module.functions) {
            if (fn->is_declaration || fn->blocks.empty()) {
                backend::SymbolDef ext_sym;
                ext_sym.name = fn->name;
                ext_sym.section = backend::SectionKind::Text;
                ext_sym.offset = 0;
                ext_sym.size = 0;
                ext_sym.is_global = true;
                ext_sym.is_external = true;
                unit.symbols.push_back(ext_sym);
                continue;
            }

            ir::AllocationResult alloc_res = allocator.allocate(*fn);

            // Function symbol in text section
            backend::SymbolDef fn_sym;
            fn_sym.name = fn->name;
            fn_sym.section = backend::SectionKind::Text;
            fn_sym.offset = encoder.code_size();
            fn_sym.is_global = true;
            fn_sym.is_external = false;

            int32_t callee_saved_size = static_cast<int32_t>(alloc_res.used_callee_saved.size() * 8);
            int32_t spill_base_offset = callee_saved_size + 16;
            int32_t alloca_stack_cursor = spill_base_offset + alloc_res.stack_frame_size + 32;
            std::unordered_map<ir::ValueId, int32_t> alloca_offsets;
            for (const auto& bb : fn->blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Alloca) {
                        uint32_t bytes = 8;
                        if (inst->type && inst->type->element_type) {
                            if (inst->type->element_type->is_struct()) {
                                bytes = static_cast<uint32_t>(inst->type->element_type->size());
                                if (bytes == 0) bytes = 8;
                            }
                        }
                        alloca_stack_cursor = (alloca_stack_cursor + 15) & ~15;
                        alloca_offsets[inst->result_id] = alloca_stack_cursor;
                        alloca_stack_cursor += bytes;
                    }
                }
            }

            std::vector<backend::Reg> used_callee_xmm;
            if (options_.abi == TargetABI::Win64 && options_.arch == TargetArch::X86_64) {
                for (ir::PhysReg pr : alloc_res.used_callee_saved) {
                    backend::Reg r = phys_reg_to_backend(pr, options_.arch);
                    if (r != backend::Reg::None && backend::reg_is_xmm(r)) {
                        used_callee_xmm.push_back(r);
                    }
                }
            }

            int32_t xmm_save_base = alloca_stack_cursor + 16;
            std::unordered_map<backend::Reg, int32_t> xmm_save_offsets;
            for (backend::Reg xr : used_callee_xmm) {
                xmm_save_base = (xmm_save_base + 15) & ~15;
                xmm_save_offsets[xr] = xmm_save_base;
                xmm_save_base += 16;
            }

            size_t max_call_args = 0;
            for (const auto& bb : fn->blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Call) {
                        max_call_args = std::max(max_call_args, inst->operands.size());
                    }
                }
            }

            int32_t phi_scratch_base = xmm_save_base + 16;

            // Compute stack frame
            backend::StackFrameRequest req;
            req.local_vars_size = phi_scratch_base + 512;
            req.has_calls = true;
            if (options_.abi == TargetABI::Win64) {
                uint32_t out_args = (max_call_args > 4) ? static_cast<uint32_t>(32 + (max_call_args - 4) * 8) : 32;
                req.max_outgoing_args_size = (out_args + 15) & ~15;
            } else {
                uint32_t out_args = (max_call_args > 6) ? static_cast<uint32_t>((max_call_args - 6) * 8) : 0;
                req.max_outgoing_args_size = (out_args + 15) & ~15;
            }
            req.use_frame_pointer = true;
            for (ir::PhysReg pr : alloc_res.used_callee_saved) {
                backend::Reg r = phys_reg_to_backend(pr, options_.arch);
                if (r != backend::Reg::None && backend::reg_is_gpr(r)) {
                    req.callee_saved_regs.push_back(r);
                }
            }
            backend::StackFrameInfo frame_info = abi_spec.compute_stack_frame(req);

            // Block labels
            std::unordered_map<ir::BasicBlock*, backend::LabelId> bb_labels;
            for (const auto& bb : fn->blocks) {
                bb_labels[bb.get()] = encoder.create_label();
            }

            backend::LabelId epilogue_label = encoder.create_label();

            // Emit function prologue
            if (options_.arch == TargetArch::X86_64) {
                encoder.push_r(backend::Reg::RBP);
                encoder.mov_rr(backend::Reg::RBP, backend::Reg::RSP);
                for (backend::Reg r : frame_info.callee_saved_regs) encoder.push_r(r);
                if (frame_info.total_frame_size > 0) encoder.sub_ri(backend::Reg::RSP, frame_info.total_frame_size);
                for (backend::Reg xr : used_callee_xmm) {
                    int32_t off = xmm_save_offsets[xr];
                    encoder.movdqu_mx(backend::mem(backend::Reg::RBP, -off, 16), xr);
                }
                if (fn->name == "main" && !options_.freestanding) {
                    encoder.call_extern("grass_install_runtime_crash_handler");
                }
            } else {
                encoder.push_r(backend::Reg::EBP);
                encoder.mov_rr(backend::Reg::EBP, backend::Reg::ESP);
                for (backend::Reg r : frame_info.callee_saved_regs) encoder.push_r(r);
                if (frame_info.total_frame_size > 0) encoder.sub_ri(backend::Reg::ESP, frame_info.total_frame_size);
            }


            const auto& arg_regs = abi_spec.get_integer_arg_regs();
            X86FuncCodegen fcg{
                options_, module, rodata_symbols, abi_spec, arg_regs, encoder,
                fn.get(), alloc_res, alloca_offsets, spill_base_offset, phi_scratch_base,
                bb_labels, epilogue_label
            };
            for (size_t i = 0; i < fn->arguments.size(); ++i) {
                ir::IRType* arg_t = fn->arguments[i].type;
                bool is_fp = arg_t && arg_t->is_floating_point();
                if (i < arg_regs.size()) {
                    if (is_fp) {
                        fcg.store_val(fn->arguments[i].id, static_cast<backend::Reg>(0x0050 + i));
                    } else {
                        fcg.store_val(fn->arguments[i].id, arg_regs[i]);
                    }
                } else {
                    int32_t shadow_disp = (options_.abi == TargetABI::Win64) ? 32 : 0;
                    int32_t stack_off = 16 + shadow_disp + static_cast<int32_t>(i - arg_regs.size()) * 8;
                    if (is_fp) {
                        encoder.movsd_xm(backend::Reg::XMM0, backend::mem(backend::Reg::RBP, stack_off, 8));
                        fcg.store_val(fn->arguments[i].id, backend::Reg::XMM0);
                    } else {
                        encoder.mov_rm(backend::Reg::R10, backend::mem(backend::Reg::RBP, stack_off, 8));
                        fcg.store_val(fn->arguments[i].id, backend::Reg::R10);
                    }
                }
            }

            // Translate blocks
            for (const auto& bb : fn->blocks) {
                encoder.bind_label(bb_labels[bb.get()]);
                for (const auto& inst : bb->instructions) {
                    size_t inst_code_start = encoder.code_size();
                    try {
                        if (!fcg.emit_arith_mem(bb.get(), inst.get())) {
                            fcg.emit_ctrl_fp_vec(bb.get(), inst.get());
                        }
                        size_t inst_code_end = encoder.code_size();
                        if (inst_code_end > inst_code_start && inst->loc.line > 0) {
                            debug_builder.add_entry(static_cast<uint32_t>(inst_code_start),
                                                    static_cast<uint32_t>(inst_code_end),
                                                    inst->loc.file, fn->name, inst->loc.line, inst->loc.column);
                        }
                    } catch (const std::exception& ex) {
                        error = std::string("In block '") + bb->name + "' inst op=" + std::to_string(static_cast<int>(inst->op)) + ": " + ex.what();
                        return false;
                    }
                }
            }

            // Function epilogue
            encoder.bind_label(epilogue_label);
            if (fn->name == "main" && !options_.freestanding &&
                options_.arch == TargetArch::X86_64 && options_.abi == TargetABI::Win64) {
                encoder.push_r(backend::Reg::RAX);
                encoder.sub_ri(backend::Reg::RSP, 40);
                encoder.call_extern("gout_flush");
                encoder.add_ri(backend::Reg::RSP, 40);
                encoder.pop_r(backend::Reg::RAX);
            }
            for (backend::Reg xr : used_callee_xmm) {
                int32_t off = xmm_save_offsets[xr];
                encoder.movdqu_xm(xr, backend::mem(backend::Reg::RBP, -off, 16));
            }
            if (frame_info.total_frame_size > 0) {
                encoder.add_ri((options_.arch == TargetArch::X86_64) ? backend::Reg::RSP : backend::Reg::ESP,
                               frame_info.total_frame_size);
            }
            for (auto it = frame_info.callee_saved_regs.rbegin(); it != frame_info.callee_saved_regs.rend(); ++it) {
                if (backend::reg_is_gpr(*it)) encoder.pop_r(*it);
            }
            encoder.pop_r((options_.arch == TargetArch::X86_64) ? backend::Reg::RBP : backend::Reg::EBP);
            encoder.ret();

            fn_sym.size = encoder.code_size() - fn_sym.offset;
            unit.symbols.push_back(fn_sym);
        }

        unit.text = encoder.code();
        unit.relocations = encoder.relocations();

        // Attach Grass debug table into .rodata
        debug_builder.set_text_bounds(0x1000, encoder.code_size());
        std::vector<uint8_t> debug_bytes = debug_builder.serialize();
        if (!debug_bytes.empty()) {
            uint64_t debug_table_offset = unit.rodata.size();
            while (debug_table_offset % 16 != 0) {
                unit.rodata.push_back(0);
                debug_table_offset++;
            }
            unit.rodata.insert(unit.rodata.end(), debug_bytes.begin(), debug_bytes.end());

            backend::SymbolDef debug_sym;
            debug_sym.name = "__grass_debug_table";
            debug_sym.section = backend::SectionKind::RoData;
            debug_sym.offset = debug_table_offset;
            debug_sym.size = debug_bytes.size();
            debug_sym.is_global = true;
            debug_sym.is_external = false;
            unit.symbols.push_back(debug_sym);
        }
        return true;
    }
} // namespace grass::codegen
