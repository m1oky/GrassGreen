#include "native/GrassAdvancedOpt.h"

#include <algorithm>
#include <cassert>
#include <queue>
#include <map>

namespace grass::opt {

    // =========================================================================
    // Helper Functions
    // =========================================================================

    static bool is_const_int_val(const ir::IRFunction& fn, ir::ValueId vid, int64_t& out_val) {
        if (!fn.has_value(vid)) return false;
        const ir::IRInst* def = fn.get_value(vid).def_inst;
        if (def && def->op == ir::IROp::ConstInt) {
            out_val = def->int_imm;
            return true;
        }
        return false;
    }

    static bool is_power_of_two_u64(uint64_t x, uint32_t& shift_out) {
        if (x == 0 || (x & (x - 1)) != 0) return false;
        shift_out = 0;
        while ((x & 1ULL) == 0) {
            x >>= 1ULL;
            shift_out++;
        }
        return true;
    }

    static void convert_inst_to_const_int(ir::IRFunction& fn, ir::IRInst* inst, int64_t val) {
        for (ir::ValueId opnd : inst->operands) {
            if (fn.has_value(opnd)) {
                auto& u = fn.get_value(opnd).users;
                u.erase(std::remove(u.begin(), u.end(), inst), u.end());
            }
        }
        inst->op = ir::IROp::ConstInt;
        inst->int_imm = val;
        inst->operands.clear();
    }

    // =========================================================================
    // =========================================================================
    // 3. Loop Unrolling
    // =========================================================================

    GrassAdvancedOpt::LoopInductionInfo GrassAdvancedOpt::analyze_induction_variable(
        const ir::IRFunction& fn, const NaturalLoop& loop, ir::BasicBlock* preheader) {
        LoopInductionInfo info;
        if (!preheader || loop.latches.empty()) return info;

        ir::BasicBlock* latch = loop.latches[0];

        for (const auto& inst : loop.header->instructions) {
            if (inst->op != ir::IROp::Phi) break;

            ir::ValueId pre_val = inst->get_phi_incoming_value(preheader);
            ir::ValueId latch_val = inst->get_phi_incoming_value(latch);

            if (pre_val == ir::kInvalidValueId || latch_val == ir::kInvalidValueId) continue;

            int64_t init_val = 0;
            if (!is_const_int_val(fn, pre_val, init_val)) continue;

            if (!fn.has_value(latch_val)) continue;
            ir::IRInst* step_inst = fn.get_value(latch_val).def_inst;
            if (!step_inst) continue;

            int64_t step_val = 0;
            bool is_step = false;

            if (step_inst->op == ir::IROp::Add) {
                if (step_inst->operands.size() >= 2) {
                    if (step_inst->operands[0] == inst->result_id &&
                        is_const_int_val(fn, step_inst->operands[1], step_val)) {
                        is_step = true;
                    } else if (step_inst->operands[1] == inst->result_id &&
                               is_const_int_val(fn, step_inst->operands[0], step_val)) {
                        is_step = true;
                    }
                }
            } else if (step_inst->op == ir::IROp::Sub) {
                if (step_inst->operands.size() >= 2 &&
                    step_inst->operands[0] == inst->result_id &&
                    is_const_int_val(fn, step_inst->operands[1], step_val)) {
                    step_val = -step_val;
                    is_step = true;
                }
            }

            if (!is_step || step_val == 0) continue;

            // Find comparison instruction
            for (const auto& bb_ptr : loop.blocks) {
                for (const auto& c_inst : bb_ptr->instructions) {
                    if (c_inst->op == ir::IROp::ICmp && c_inst->operands.size() >= 2) {
                        int64_t limit_val = 0;
                        ir::ICmpCond cond = c_inst->icmp_cond;

                        if (c_inst->operands[0] == inst->result_id &&
                            is_const_int_val(fn, c_inst->operands[1], limit_val)) {
                            // iv cond limit
                        } else if (c_inst->operands[1] == inst->result_id &&
                                   is_const_int_val(fn, c_inst->operands[0], limit_val)) {
                            // limit cond iv -> invert
                            switch (cond) {
                                case ir::ICmpCond::SLT: cond = ir::ICmpCond::SGT; break;
                                case ir::ICmpCond::SLE: cond = ir::ICmpCond::SGE; break;
                                case ir::ICmpCond::SGT: cond = ir::ICmpCond::SLT; break;
                                case ir::ICmpCond::SGE: cond = ir::ICmpCond::SLE; break;
                                case ir::ICmpCond::ULT: cond = ir::ICmpCond::UGT; break;
                                case ir::ICmpCond::ULE: cond = ir::ICmpCond::UGE; break;
                                case ir::ICmpCond::UGT: cond = ir::ICmpCond::ULT; break;
                                case ir::ICmpCond::UGE: cond = ir::ICmpCond::ULE; break;
                                default: break;
                            }
                        } else {
                            continue;
                        }

                        uint64_t trip_count = 0;
                        bool computable = false;

                        if (step_val > 0) {
                            if (cond == ir::ICmpCond::SLT || cond == ir::ICmpCond::ULT) {
                                if (limit_val >= init_val) {
                                    trip_count = static_cast<uint64_t>(limit_val - init_val + step_val - 1) / static_cast<uint64_t>(step_val);
                                    computable = true;
                                }
                            } else if (cond == ir::ICmpCond::SLE || cond == ir::ICmpCond::ULE) {
                                if (limit_val >= init_val) {
                                    trip_count = static_cast<uint64_t>(limit_val - init_val) / static_cast<uint64_t>(step_val) + 1;
                                    computable = true;
                                }
                            }
                        } else if (step_val < 0) {
                            int64_t pos_step = -step_val;
                            if (cond == ir::ICmpCond::SGT || cond == ir::ICmpCond::UGT) {
                                if (init_val >= limit_val) {
                                    trip_count = static_cast<uint64_t>(init_val - limit_val + pos_step - 1) / static_cast<uint64_t>(pos_step);
                                    computable = true;
                                }
                            } else if (cond == ir::ICmpCond::SGE || cond == ir::ICmpCond::UGE) {
                                if (init_val >= limit_val) {
                                    trip_count = static_cast<uint64_t>(init_val - limit_val) / static_cast<uint64_t>(pos_step) + 1;
                                    computable = true;
                                }
                            }
                        }

                        if (computable && trip_count >= 2) {
                            info.phi_inst = inst.get();
                            info.step_inst = step_inst;
                            info.cmp_inst = c_inst.get();
                            info.init_val = init_val;
                            info.step_val = step_val;
                            info.limit_val = limit_val;
                            info.trip_count = trip_count;
                            info.is_valid = true;
                            return info;
                        }
                    }
                }
            }
        }

        return info;
    }

    bool GrassAdvancedOpt::unroll_loop(ir::IRFunction& fn, const NaturalLoop& loop,
                                      const LoopInductionInfo& info, int factor) {
        if (!info.is_valid || factor <= 1 || info.trip_count < 2) return false;
        if (loop.blocks.size() > 2 || loop.latches.empty() || loop.latches.size() > 1) return false;
        for (ir::BasicBlock* b : loop.blocks) {
            for (const auto& inst : b->instructions) {
                if (inst->op == ir::IROp::VecZero || inst->op == ir::IROp::VecSplat ||
                    inst->op == ir::IROp::VecLoad || inst->op == ir::IROp::VecStore ||
                    inst->op == ir::IROp::VecAdd  || inst->op == ir::IROp::VecSub ||
                    inst->op == ir::IROp::VecMul  || inst->op == ir::IROp::VecHAdd) {
                    return false;
                }
            }
        }

        // Supported: Single-block or Two-block loops with forward branch
        ir::BasicBlock* header = loop.header;
        ir::BasicBlock* latch = loop.latches[0];

        // Identify exit block
        ir::BasicBlock* exit_bb = nullptr;
        for (ir::BasicBlock* s : header->succs) {
            if (loop.blocks.find(s) == loop.blocks.end()) {
                exit_bb = s;
                break;
            }
        }
        if (!exit_bb && latch != header) {
            for (ir::BasicBlock* s : latch->succs) {
                if (loop.blocks.find(s) == loop.blocks.end()) {
                    exit_bb = s;
                    break;
                }
            }
        }
        if (!exit_bb) return false;

        uint64_t trip_count = info.trip_count;
        int unroll_factor = factor;
        uint64_t main_iters = trip_count / unroll_factor;
        uint64_t remainder_iters = trip_count % unroll_factor;

        if (main_iters == 0) return false; // trip count < factor

        // Collect body instructions from latch (or header)
        ir::BasicBlock* body_bb = (latch != header) ? latch : header;
        std::vector<ir::IRInst*> body_insts;
        for (auto& inst : body_bb->instructions) {
            if (inst->op == ir::IROp::Phi) continue;
            if (inst.get() == info.cmp_inst) continue;
            if (inst->is_terminator()) continue;
            body_insts.push_back(inst.get());
        }

        if (body_insts.empty()) return false;

        // Record original latch incoming values for all header Phis
        std::unordered_map<ir::ValueId, ir::ValueId> orig_latch_incoming;
        for (const auto& inst : header->instructions) {
            if (inst->op != ir::IROp::Phi) break;
            orig_latch_incoming[inst->result_id] = inst->get_phi_incoming_value(latch);
        }

        // Duplicate body unroll_factor - 1 times in body_bb
        size_t insert_pos = body_bb->instructions.size();
        if (!body_bb->instructions.empty() && body_bb->instructions.back()->is_terminator()) {
            insert_pos--;
        }

        // Initial prev_iter_map maps Phi to the result of iteration 0 (original body)
        std::unordered_map<ir::ValueId, ir::ValueId> prev_iter_map;
        for (const auto& [phi_id, orig_next] : orig_latch_incoming) {
            prev_iter_map[phi_id] = orig_next;
        }

        for (int k = 1; k < unroll_factor; ++k) {
            std::unordered_map<ir::ValueId, ir::ValueId> iter_map;

            for (ir::IRInst* orig_inst : body_insts) {
                auto cloned = std::make_unique<ir::IRInst>();
                cloned->op = orig_inst->op;
                cloned->type = orig_inst->type;
                cloned->loc = orig_inst->loc;
                cloned->int_imm = orig_inst->int_imm;
                cloned->float_imm = orig_inst->float_imm;
                cloned->icmp_cond = orig_inst->icmp_cond;
                cloned->fcmp_cond = orig_inst->fcmp_cond;
                cloned->callee_name = orig_inst->callee_name;
                cloned->is_indirect_call = orig_inst->is_indirect_call;
                cloned->allocated_type = orig_inst->allocated_type;
                cloned->alloca_count = orig_inst->alloca_count;
                cloned->alloca_align = orig_inst->alloca_align;
                cloned->gep_field_index = orig_inst->gep_field_index;

                if (orig_inst->result_id != ir::kInvalidValueId) {
                    ir::ValueId new_vid = fn.create_value(orig_inst->type, "unr_v", cloned.get());
                    cloned->result_id = new_vid;
                    iter_map[orig_inst->result_id] = new_vid;
                }

                for (ir::ValueId opnd : orig_inst->operands) {
                    if (iter_map.count(opnd)) {
                        cloned->operands.push_back(iter_map[opnd]);
                    } else if (prev_iter_map.count(opnd)) {
                        cloned->operands.push_back(prev_iter_map[opnd]);
                    } else {
                        cloned->operands.push_back(opnd);
                    }
                }

                cloned->parent = body_bb;
                body_bb->instructions.insert(body_bb->instructions.begin() + insert_pos, std::move(cloned));
                insert_pos++;
            }

            // Update Phi tracking for next iteration
            for (const auto& [phi_id, orig_next] : orig_latch_incoming) {
                if (iter_map.count(orig_next)) {
                    prev_iter_map[phi_id] = iter_map[orig_next];
                }
            }
        }

        // Remap latch Phi incoming to final unrolled iteration's values
        for (auto& inst : header->instructions) {
            if (inst->op != ir::IROp::Phi) break;
            if (prev_iter_map.count(inst->result_id)) {
                inst->add_phi_incoming(latch, prev_iter_map[inst->result_id]);
            }
        }

        // Update comparison instruction limit for main unrolled loop
        int64_t main_limit = info.init_val + static_cast<int64_t>(main_iters * unroll_factor) * info.step_val;
        ir::IRBuilder builder(&fn);

        // Insert new limit constant right before cmp_inst to keep Phis at start of block
        ir::BasicBlock* cmp_bb = info.cmp_inst->parent;
        size_t cmp_idx = 0;
        for (size_t i = 0; i < cmp_bb->instructions.size(); ++i) {
            if (cmp_bb->instructions[i].get() == info.cmp_inst) {
                cmp_idx = i;
                break;
            }
        }
        builder.set_insert_point(cmp_bb, cmp_idx);
        ir::ValueId new_limit_val = builder.create_const_int(info.phi_inst->type, main_limit);

        if (info.cmp_inst->operands.size() >= 2) {
            if (info.cmp_inst->operands[0] == info.phi_inst->result_id) {
                info.cmp_inst->operands[1] = new_limit_val;
            } else {
                info.cmp_inst->operands[0] = new_limit_val;
            }
        }

        // Generate tail for remainder iterations if remainder_iters > 0
        if (remainder_iters > 0) {
            ir::BasicBlock* tail_bb = fn.create_block(header->name + "_tail");

            // Retarget header exit branch to tail_bb
            ir::IRInst* term = header->get_terminator();
            if (term && term->op == ir::IROp::Branch) {
                if (term->true_target == exit_bb) term->true_target = tail_bb;
                if (term->false_target == exit_bb) term->false_target = tail_bb;
            }

            std::unordered_map<ir::ValueId, ir::ValueId> tail_val_map;
            for (const auto& inst : header->instructions) {
                if (inst->op != ir::IROp::Phi) break;
                tail_val_map[inst->result_id] = inst->result_id;
            }

            // Emit remainder_iters copies straight-line in tail_bb
            for (uint64_t r = 0; r < remainder_iters; ++r) {
                std::unordered_map<ir::ValueId, ir::ValueId> step_map;
                for (ir::IRInst* orig_inst : body_insts) {
                    auto cloned = std::make_unique<ir::IRInst>();
                    cloned->op = orig_inst->op;
                    cloned->type = orig_inst->type;
                    cloned->loc = orig_inst->loc;
                    cloned->int_imm = orig_inst->int_imm;
                    cloned->float_imm = orig_inst->float_imm;
                    cloned->icmp_cond = orig_inst->icmp_cond;
                    cloned->fcmp_cond = orig_inst->fcmp_cond;
                    cloned->callee_name = orig_inst->callee_name;
                    cloned->is_indirect_call = orig_inst->is_indirect_call;
                    cloned->allocated_type = orig_inst->allocated_type;
                    cloned->alloca_count = orig_inst->alloca_count;
                    cloned->alloca_align = orig_inst->alloca_align;
                    cloned->gep_field_index = orig_inst->gep_field_index;

                    if (orig_inst->result_id != ir::kInvalidValueId) {
                        ir::ValueId new_vid = fn.create_value(orig_inst->type, "tail_v", cloned.get());
                        cloned->result_id = new_vid;
                        step_map[orig_inst->result_id] = new_vid;
                    }

                    for (ir::ValueId opnd : orig_inst->operands) {
                        if (step_map.count(opnd)) {
                            cloned->operands.push_back(step_map[opnd]);
                        } else if (tail_val_map.count(opnd)) {
                            cloned->operands.push_back(tail_val_map[opnd]);
                        } else {
                            cloned->operands.push_back(opnd);
                        }
                    }

                    cloned->parent = tail_bb;
                    tail_bb->push_back(std::move(cloned));
                }

                for (const auto& [phi_id, orig_next] : orig_latch_incoming) {
                    if (step_map.count(orig_next)) {
                        tail_val_map[phi_id] = step_map[orig_next];
                    }
                }
            }

            // Tail jumps to exit_bb
            auto tail_jmp = std::make_unique<ir::IRInst>();
            tail_jmp->op = ir::IROp::Jump;
            tail_jmp->type = fn.parent ? fn.parent->context.get_void_type() : nullptr;
            tail_jmp->true_target = exit_bb;
            tail_bb->push_back(std::move(tail_jmp));

            // Update exit_bb Phis and operands: incoming from header now comes from tail_bb
            for (auto& inst : exit_bb->instructions) {
                if (inst->op == ir::IROp::Phi) {
                    for (auto& in : inst->phi_incoming) {
                        if (in.block == header) {
                            in.block = tail_bb;
                            if (tail_val_map.count(in.value)) {
                                in.value = tail_val_map[in.value];
                            }
                        }
                    }
                } else {
                    for (auto& opnd : inst->operands) {
                        if (tail_val_map.count(opnd)) {
                            opnd = tail_val_map[opnd];
                        }
                    }
                }
            }
        }

        fn.rebuild_cfg();
        fn.rebuild_def_use();
        return true;
    }

    bool GrassAdvancedOpt::run_loop_unrolling(ir::IRFunction& fn, int factor) {
        if (fn.is_declaration || fn.blocks.empty() || factor <= 1) return false;

        auto loops = detect_natural_loops(fn);
        if (loops.empty()) return false;

        bool changed = false;

        for (const auto& loop : loops) {
            // Verify inner loop (no other loop header is inside this loop)
            bool is_inner = true;
            for (const auto& other : loops) {
                if (other.header != loop.header && loop.blocks.count(other.header)) {
                    is_inner = false;
                    break;
                }
            }
            if (!is_inner) continue;

            ir::BasicBlock* preheader = get_or_create_preheader(fn, loop);
            if (!preheader) continue;

            auto info = analyze_induction_variable(fn, loop, preheader);
            if (info.is_valid && info.trip_count >= static_cast<uint64_t>(factor)) {
                if (unroll_loop(fn, loop, info, factor)) {
                    changed = true;
                }
            }
        }

        return changed;
    }

    bool GrassAdvancedOpt::run_loop_unrolling(ir::IRModule& module, int factor) {
        bool changed = false;
        for (auto& fn : module.functions) {
            if (run_loop_unrolling(*fn, factor)) changed = true;
        }
        return changed;
    }

    // =========================================================================
    // 4. Strength Reduction & Algebraic Simplification
    // =========================================================================

    bool GrassAdvancedOpt::run_strength_reduction(ir::IRFunction& fn) {
        if (fn.is_declaration || fn.blocks.empty()) return false;

        bool changed = false;
        bool progress = true;
        int iter = 0;
        constexpr int kMaxIters = 8;

        while (progress && iter < kMaxIters) {
            progress = false;
            iter++;
            fn.rebuild_def_use();

            ir::IRBuilder builder(&fn);

            for (auto& bb : fn.blocks) {
                for (size_t i = 0; i < bb->instructions.size(); ++i) {
                    ir::IRInst* inst = bb->instructions[i].get();
                    if (inst->result_id == ir::kInvalidValueId) continue;

                    ir::ValueId repl = ir::kInvalidValueId;
                    int64_t c0 = 0, c1 = 0;
                    uint32_t shift = 0;

                    // Algebraic identities
                    if (inst->op == ir::IROp::Add && inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            repl = inst->operands[0];
                        } else if (is_const_int_val(fn, inst->operands[0], c0) && c0 == 0) {
                            repl = inst->operands[1];
                        }
                    } else if (inst->op == ir::IROp::Sub && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) {
                            convert_inst_to_const_int(fn, inst, 0);
                            progress = true;
                            changed = true;
                            continue;
                        } else if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            repl = inst->operands[0];
                        }
                    } else if (inst->op == ir::IROp::Mul && inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1)) {
                            if (c1 == 0) {
                                convert_inst_to_const_int(fn, inst, 0);
                                progress = true;
                                changed = true;
                                continue;
                            } else if (c1 == 1) {
                                repl = inst->operands[0];
                            } else if (is_power_of_two_u64(static_cast<uint64_t>(c1), shift)) {
                                builder.set_insert_point(bb.get(), i);
                                ir::ValueId shift_val = builder.create_const_int(inst->type, shift);
                                inst->op = ir::IROp::Shl;
                                inst->operands = {inst->operands[0], shift_val};
                                progress = true;
                                changed = true;
                                continue;
                            }
                        } else if (is_const_int_val(fn, inst->operands[0], c0)) {
                            if (c0 == 0) {
                                convert_inst_to_const_int(fn, inst, 0);
                                progress = true;
                                changed = true;
                                continue;
                            } else if (c0 == 1) {
                                repl = inst->operands[1];
                            } else if (is_power_of_two_u64(static_cast<uint64_t>(c0), shift)) {
                                builder.set_insert_point(bb.get(), i);
                                ir::ValueId shift_val = builder.create_const_int(inst->type, shift);
                                inst->op = ir::IROp::Shl;
                                inst->operands = {inst->operands[1], shift_val};
                                progress = true;
                                changed = true;
                                continue;
                            }
                        }
                    } else if (inst->op == ir::IROp::UDiv && inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1)) {
                            if (c1 == 1) {
                                repl = inst->operands[0];
                            } else if (c1 > 0 && is_power_of_two_u64(static_cast<uint64_t>(c1), shift)) {
                                builder.set_insert_point(bb.get(), i);
                                ir::ValueId shift_val = builder.create_const_int(inst->type, shift);
                                inst->op = ir::IROp::LShr;
                                inst->operands = {inst->operands[0], shift_val};
                                progress = true;
                                changed = true;
                                continue;
                            }
                        }
                    } else if (inst->op == ir::IROp::SDiv && inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 1) {
                            repl = inst->operands[0];
                        }
                    } else if (inst->op == ir::IROp::URem && inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1)) {
                            if (c1 == 1) {
                                convert_inst_to_const_int(fn, inst, 0);
                                progress = true;
                                changed = true;
                                continue;
                            } else if (c1 > 0 && is_power_of_two_u64(static_cast<uint64_t>(c1), shift)) {
                                builder.set_insert_point(bb.get(), i);
                                ir::ValueId mask_val = builder.create_const_int(inst->type, c1 - 1);
                                inst->op = ir::IROp::And;
                                inst->operands = {inst->operands[0], mask_val};
                                progress = true;
                                changed = true;
                                continue;
                            }
                        }
                    } else if (inst->op == ir::IROp::Xor && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) {
                            convert_inst_to_const_int(fn, inst, 0);
                            progress = true;
                            changed = true;
                            continue;
                        } else if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            repl = inst->operands[0];
                        } else if (is_const_int_val(fn, inst->operands[0], c0) && c0 == 0) {
                            repl = inst->operands[1];
                        }
                    } else if (inst->op == ir::IROp::And && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) {
                            repl = inst->operands[0];
                        } else if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            convert_inst_to_const_int(fn, inst, 0);
                            progress = true;
                            changed = true;
                            continue;
                        } else if (is_const_int_val(fn, inst->operands[0], c0) && c0 == 0) {
                            convert_inst_to_const_int(fn, inst, 0);
                            progress = true;
                            changed = true;
                            continue;
                        }
                    } else if (inst->op == ir::IROp::Or && inst->operands.size() >= 2) {
                        if (inst->operands[0] == inst->operands[1]) {
                            repl = inst->operands[0];
                        } else if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            repl = inst->operands[0];
                        } else if (is_const_int_val(fn, inst->operands[0], c0) && c0 == 0) {
                            repl = inst->operands[1];
                        }
                    } else if ((inst->op == ir::IROp::Shl || inst->op == ir::IROp::LShr || inst->op == ir::IROp::AShr) &&
                               inst->operands.size() >= 2) {
                        if (is_const_int_val(fn, inst->operands[1], c1) && c1 == 0) {
                            repl = inst->operands[0];
                        }
                    }

                    if (repl != ir::kInvalidValueId) {
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

    bool GrassAdvancedOpt::run_strength_reduction(ir::IRModule& module) {
        bool changed = false;
        for (auto& fn : module.functions) {
            if (run_strength_reduction(*fn)) changed = true;
        }
        return changed;
    }


} // namespace grass::opt
