#include "GrassAdvancedOpt.h"

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
    // 1. Function Inlining
    // =========================================================================

    size_t GrassAdvancedOpt::count_instructions(const ir::IRFunction& fn) {
        size_t count = 0;
        for (const auto& bb : fn.blocks) {
            count += bb->instructions.size();
        }
        return count;
    }

    std::unordered_set<std::string> GrassAdvancedOpt::find_recursive_functions(const ir::IRModule& module) {
        std::unordered_map<std::string, std::unordered_set<std::string>> call_graph;
        for (const auto& fn : module.functions) {
            for (const auto& bb : fn->blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Call && !inst->is_indirect_call && !inst->callee_name.empty()) {
                        call_graph[fn->name].insert(inst->callee_name);
                    }
                }
            }
        }

        std::unordered_set<std::string> recursive_funcs;
        for (const auto& [func_name, _] : call_graph) {
            std::unordered_set<std::string> visited;
            std::vector<std::string> stack;

            for (const auto& callee : call_graph[func_name]) {
                stack.push_back(callee);
            }

            while (!stack.empty()) {
                std::string curr = stack.back();
                stack.pop_back();

                if (curr == func_name) {
                    recursive_funcs.insert(func_name);
                    break;
                }

                if (visited.insert(curr).second) {
                    auto it = call_graph.find(curr);
                    if (it != call_graph.end()) {
                        for (const auto& nxt : it->second) {
                            stack.push_back(nxt);
                        }
                    }
                }
            }
        }

        return recursive_funcs;
    }

    bool GrassAdvancedOpt::can_inline_function(const ir::IRFunction& callee, int threshold,
                                              const std::unordered_set<std::string>& recursive_funcs) {
        if (callee.is_declaration) return false;
        if (callee.blocks.empty()) return false;
        if (callee.name == "main") return false;
        if (recursive_funcs.find(callee.name) != recursive_funcs.end()) return false;

        size_t inst_count = count_instructions(callee);
        return inst_count <= static_cast<size_t>(threshold);
    }

    bool GrassAdvancedOpt::inline_call_site(ir::IRFunction& caller, ir::BasicBlock* call_bb,
                                           size_t call_idx, ir::IRFunction& callee) {
        ir::IRInst* call_inst = call_bb->instructions[call_idx].get();
        if (!call_inst || call_inst->op != ir::IROp::Call || call_inst->is_indirect_call) {
            return false;
        }

        ir::SourceLocation call_loc = call_inst->loc;
        ir::ValueId call_res_id = call_inst->result_id;
        ir::IRType* call_ret_type = call_inst->type;

        // Split call_bb after call_inst into resume_bb
        ir::BasicBlock* resume_bb = caller.create_block(call_bb->name + "_cont");

        for (size_t i = call_idx + 1; i < call_bb->instructions.size(); ++i) {
            call_bb->instructions[i]->parent = resume_bb;
            resume_bb->instructions.push_back(std::move(call_bb->instructions[i]));
        }

        std::unique_ptr<ir::IRInst> call_owner = std::move(call_bb->instructions[call_idx]);
        call_bb->instructions.resize(call_idx);

        // Update phis in other blocks: incoming from call_bb now comes from resume_bb
        for (auto& bb : caller.blocks) {
            if (bb.get() == call_bb || bb.get() == resume_bb) continue;
            for (auto& inst : bb->instructions) {
                if (inst->op != ir::IROp::Phi) break;
                for (auto& in : inst->phi_incoming) {
                    if (in.block == call_bb) {
                        in.block = resume_bb;
                    }
                }
            }
        }

        std::unordered_map<ir::ValueId, ir::ValueId> val_map;
        std::unordered_map<ir::BasicBlock*, ir::BasicBlock*> bb_map;

        // Map formal arguments to actual arguments
        for (size_t a = 0; a < callee.arguments.size() && a < call_owner->operands.size(); ++a) {
            val_map[callee.arguments[a].id] = call_owner->operands[a];
        }

        // Clone basic blocks
        for (const auto& c_bb : callee.blocks) {
            ir::BasicBlock* cloned_bb = caller.create_block(call_bb->name + "_inl_" + c_bb->name);
            bb_map[c_bb.get()] = cloned_bb;
        }

        // Map SSA values defined in callee
        for (const auto& c_bb : callee.blocks) {
            for (const auto& c_inst : c_bb->instructions) {
                if (c_inst->result_id != ir::kInvalidValueId) {
                    ir::ValueId new_vid = caller.create_value(c_inst->type, "inl_v", nullptr);
                    val_map[c_inst->result_id] = new_vid;
                }
            }
        }

        // Clone instructions
        std::vector<std::pair<ir::BasicBlock*, ir::ValueId>> ret_points;

        for (const auto& c_bb : callee.blocks) {
            ir::BasicBlock* cloned_bb = bb_map[c_bb.get()];
            for (const auto& c_inst : c_bb->instructions) {
                if (c_inst->op == ir::IROp::Ret) {
                    ir::ValueId ret_val = ir::kInvalidValueId;
                    if (!c_inst->operands.empty()) {
                        ir::ValueId old_vid = c_inst->operands[0];
                        ret_val = val_map.count(old_vid) ? val_map[old_vid] : old_vid;
                    }
                    ret_points.push_back({cloned_bb, ret_val});

                    auto jmp = std::make_unique<ir::IRInst>();
                    jmp->op = ir::IROp::Jump;
                    jmp->type = caller.parent ? caller.parent->context.get_void_type() : nullptr;
                    jmp->true_target = resume_bb;
                    jmp->loc = c_inst->loc.is_valid() ? c_inst->loc : call_loc;
                    cloned_bb->push_back(std::move(jmp));
                } else {
                    auto cloned = std::make_unique<ir::IRInst>();
                    cloned->op = c_inst->op;
                    cloned->type = c_inst->type;
                    cloned->loc = c_inst->loc.is_valid() ? c_inst->loc : call_loc;

                    if (c_inst->result_id != ir::kInvalidValueId) {
                        cloned->result_id = val_map[c_inst->result_id];
                        caller.get_value(cloned->result_id).def_inst = cloned.get();
                    }

                    for (ir::ValueId opnd : c_inst->operands) {
                        cloned->operands.push_back(val_map.count(opnd) ? val_map[opnd] : opnd);
                    }

                    cloned->int_imm = c_inst->int_imm;
                    cloned->float_imm = c_inst->float_imm;
                    cloned->icmp_cond = c_inst->icmp_cond;
                    cloned->fcmp_cond = c_inst->fcmp_cond;
                    cloned->callee_name = c_inst->callee_name;
                    cloned->is_indirect_call = c_inst->is_indirect_call;
                    cloned->allocated_type = c_inst->allocated_type;
                    cloned->alloca_count = c_inst->alloca_count;
                    cloned->alloca_align = c_inst->alloca_align;
                    cloned->gep_field_index = c_inst->gep_field_index;

                    if (c_inst->true_target) cloned->true_target = bb_map[c_inst->true_target];
                    if (c_inst->false_target) cloned->false_target = bb_map[c_inst->false_target];

                    for (const auto& in : c_inst->phi_incoming) {
                        ir::BasicBlock* mapped_b = bb_map[in.block];
                        ir::ValueId mapped_v = val_map.count(in.value) ? val_map[in.value] : in.value;
                        cloned->add_phi_incoming(mapped_b, mapped_v);
                    }

                    cloned_bb->push_back(std::move(cloned));
                }
            }
        }

        // Connect call_bb to entry block of inlined function
        ir::BasicBlock* callee_entry = callee.get_entry_block();
        ir::BasicBlock* cloned_entry = bb_map[callee_entry];
        auto jmp_entry = std::make_unique<ir::IRInst>();
        jmp_entry->op = ir::IROp::Jump;
        jmp_entry->type = caller.parent ? caller.parent->context.get_void_type() : nullptr;
        jmp_entry->true_target = cloned_entry;
        jmp_entry->loc = call_loc;
        call_bb->push_back(std::move(jmp_entry));

        // Connect return values to call_res_id
        if (call_res_id != ir::kInvalidValueId) {
            if (ret_points.size() == 1) {
                caller.replace_all_uses_with(call_res_id, ret_points[0].second);
            } else if (ret_points.size() > 1) {
                auto phi = std::make_unique<ir::IRInst>();
                phi->op = ir::IROp::Phi;
                phi->type = call_ret_type;
                ir::ValueId phi_vid = caller.create_value(call_ret_type, "inl_ret_phi", phi.get());
                phi->result_id = phi_vid;
                phi->loc = call_loc;
                for (const auto& rp : ret_points) {
                    phi->add_phi_incoming(rp.first, rp.second);
                }
                resume_bb->push_front(std::move(phi));
                caller.replace_all_uses_with(call_res_id, phi_vid);
            }
        }

        // Hoist any Alloca from cloned blocks to caller entry block
        ir::BasicBlock* caller_entry = caller.get_entry_block();
        if (caller_entry) {
            for (const auto& c_bb : callee.blocks) {
                ir::BasicBlock* cloned_bb = bb_map[c_bb.get()];
                for (size_t i = 0; i < cloned_bb->instructions.size();) {
                    if (cloned_bb->instructions[i]->op == ir::IROp::Alloca) {
                        auto alloca_inst = std::move(cloned_bb->instructions[i]);
                        cloned_bb->instructions.erase(cloned_bb->instructions.begin() + i);
                        alloca_inst->parent = caller_entry;
                        caller_entry->push_front(std::move(alloca_inst));
                    } else {
                        ++i;
                    }
                }
            }
        }

        caller.rebuild_cfg();
        caller.rebuild_def_use();
        return true;
    }

    bool GrassAdvancedOpt::run_inlining(ir::IRModule& module, int threshold) {
        auto recursive_funcs = find_recursive_functions(module);

        std::unordered_map<std::string, ir::IRFunction*> func_map;
        for (const auto& fn : module.functions) {
            func_map[fn->name] = fn.get();
        }

        bool overall_changed = false;
        bool changed_iteration = true;
        int iter = 0;
        constexpr int kMaxInliningIterations = 4;

        while (changed_iteration && iter < kMaxInliningIterations) {
            changed_iteration = false;
            iter++;

            for (auto& caller : module.functions) {
                if (caller->is_declaration || caller->blocks.empty()) continue;

                bool inlined_in_caller = false;
                for (size_t b_idx = 0; b_idx < caller->blocks.size(); ++b_idx) {
                    ir::BasicBlock* bb = caller->blocks[b_idx].get();
                    for (size_t i = 0; i < bb->instructions.size(); ++i) {
                        ir::IRInst* inst = bb->instructions[i].get();
                        if (inst->op == ir::IROp::Call && !inst->is_indirect_call && !inst->callee_name.empty()) {
                            auto it = func_map.find(inst->callee_name);
                            if (it != func_map.end() && it->second != caller.get()) {
                                ir::IRFunction* callee = it->second;
                                if (can_inline_function(*callee, threshold, recursive_funcs)) {
                                    if (inline_call_site(*caller, bb, i, *callee)) {
                                        inlined_in_caller = true;
                                        changed_iteration = true;
                                        overall_changed = true;
                                        break; // restart block scan for caller
                                    }
                                }
                            }
                        }
                    }
                    if (inlined_in_caller) break;
                }
            }
        }

        return overall_changed;
    }

    // =========================================================================
    // 2. Loop-Invariant Code Motion (LICM)
    // =========================================================================

    std::vector<GrassAdvancedOpt::NaturalLoop> GrassAdvancedOpt::detect_natural_loops(ir::IRFunction& fn) {
        fn.rebuild_cfg();
        ir::DominatorTree dom;
        dom.build(fn);

        std::map<ir::BasicBlock*, NaturalLoop> loop_map;

        for (const auto& bb : fn.blocks) {
            for (ir::BasicBlock* succ : bb->succs) {
                if (dom.dominates(succ, bb.get())) {
                    NaturalLoop& loop = loop_map[succ];
                    loop.header = succ;
                    loop.latches.push_back(bb.get());
                }
            }
        }

        std::vector<NaturalLoop> loops;
        for (auto& [header, loop] : loop_map) {
            loop.blocks.insert(header);
            std::vector<ir::BasicBlock*> worklist;

            for (ir::BasicBlock* latch : loop.latches) {
                if (loop.blocks.insert(latch).second) {
                    worklist.push_back(latch);
                }
            }

            while (!worklist.empty()) {
                ir::BasicBlock* curr = worklist.back();
                worklist.pop_back();

                for (ir::BasicBlock* p : curr->preds) {
                    if (loop.blocks.insert(p).second) {
                        worklist.push_back(p);
                    }
                }
            }

            loops.push_back(std::move(loop));
        }

        // Sort loops by block count ascending (innermost loops first)
        std::sort(loops.begin(), loops.end(), [](const NaturalLoop& a, const NaturalLoop& b) {
            return a.blocks.size() < b.blocks.size();
        });

        return loops;
    }

    ir::BasicBlock* GrassAdvancedOpt::get_or_create_preheader(ir::IRFunction& fn, const NaturalLoop& loop) {
        std::vector<ir::BasicBlock*> outside_preds;
        for (ir::BasicBlock* p : loop.header->preds) {
            if (loop.blocks.find(p) == loop.blocks.end()) {
                outside_preds.push_back(p);
            }
        }

        if (outside_preds.size() == 1 && outside_preds[0]->succs.size() == 1 && outside_preds[0] != loop.header) {
            return outside_preds[0];
        }

        ir::BasicBlock* preheader = fn.create_block(loop.header->name + "_preheader");

        auto jmp = std::make_unique<ir::IRInst>();
        jmp->op = ir::IROp::Jump;
        jmp->type = fn.parent ? fn.parent->context.get_void_type() : nullptr;
        jmp->true_target = loop.header;
        preheader->push_back(std::move(jmp));

        if (loop.header == fn.get_entry_block()) {
            for (size_t i = 0; i < fn.blocks.size(); ++i) {
                if (fn.blocks[i].get() == preheader) {
                    auto pre_ptr = std::move(fn.blocks[i]);
                    fn.blocks.erase(fn.blocks.begin() + i);
                    fn.blocks.insert(fn.blocks.begin(), std::move(pre_ptr));
                    break;
                }
            }
        }

        for (ir::BasicBlock* p : outside_preds) {
            ir::IRInst* term = p->get_terminator();
            if (term) {
                if (term->true_target == loop.header) term->true_target = preheader;
                if (term->false_target == loop.header) term->false_target = preheader;
            }
        }

        for (auto& inst : loop.header->instructions) {
            if (inst->op != ir::IROp::Phi) break;
            if (outside_preds.size() == 1) {
                for (auto& in : inst->phi_incoming) {
                    if (in.block == outside_preds[0]) {
                        in.block = preheader;
                    }
                }
            } else if (outside_preds.size() > 1) {
                auto pre_phi = std::make_unique<ir::IRInst>();
                pre_phi->op = ir::IROp::Phi;
                pre_phi->type = inst->type;
                ir::ValueId pre_phi_vid = fn.create_value(inst->type, "pre_phi", pre_phi.get());
                pre_phi->result_id = pre_phi_vid;

                for (ir::BasicBlock* op_bb : outside_preds) {
                    ir::ValueId inc_val = inst->get_phi_incoming_value(op_bb);
                    if (inc_val != ir::kInvalidValueId) {
                        pre_phi->add_phi_incoming(op_bb, inc_val);
                    }
                }

                preheader->push_front(std::move(pre_phi));

                for (ir::BasicBlock* op_bb : outside_preds) {
                    inst->remove_phi_incoming(op_bb);
                }
                inst->add_phi_incoming(preheader, pre_phi_vid);
            }
        }

        fn.rebuild_cfg();
        return preheader;
    }

    bool GrassAdvancedOpt::is_hoistable_instruction(const ir::IRInst* inst) {
        if (!inst || inst->result_id == ir::kInvalidValueId) return false;
        if (inst->has_side_effects()) return false;
        if (inst->is_terminator()) return false;
        if (inst->op == ir::IROp::Phi || inst->op == ir::IROp::Alloca) return false;
        if (inst->op == ir::IROp::Load || inst->op == ir::IROp::Store ||
            inst->op == ir::IROp::MemCpy || inst->op == ir::IROp::MemSet) {
            return false;
        }

        if (inst->is_binary_arithmetic() || inst->is_bitwise() ||
            inst->is_comparison() || inst->is_cast() ||
            inst->op == ir::IROp::GetElementPtr ||
            inst->op == ir::IROp::ConstInt || inst->op == ir::IROp::ConstFloat ||
            inst->op == ir::IROp::Copy) {
            return true;
        }

        return false;
    }

    bool GrassAdvancedOpt::run_licm(ir::IRFunction& fn) {
        if (fn.is_declaration || fn.blocks.empty()) return false;

        auto loops = detect_natural_loops(fn);
        if (loops.empty()) return false;

        bool changed = false;

        for (const auto& loop : loops) {
            ir::BasicBlock* preheader = get_or_create_preheader(fn, loop);
            if (!preheader) continue;

            fn.rebuild_def_use();

            std::unordered_set<ir::IRInst*> invariant_set;
            std::vector<ir::IRInst*> ordered_invariants;

            auto is_val_invariant = [&](ir::ValueId vid) -> bool {
                if (!fn.has_value(vid)) return true;
                const auto& v = fn.get_value(vid);
                if (v.is_argument || !v.def_inst) return true;
                if (loop.blocks.find(v.def_inst->parent) == loop.blocks.end()) return true;
                return invariant_set.count(v.def_inst) > 0;
            };

            bool progress = true;
            while (progress) {
                progress = false;
                for (ir::BasicBlock* bb : loop.blocks) {
                    for (auto& inst : bb->instructions) {
                        if (invariant_set.count(inst.get())) continue;
                        if (!is_hoistable_instruction(inst.get())) continue;

                        bool all_ops_inv = true;
                        for (ir::ValueId opnd : inst->operands) {
                            if (!is_val_invariant(opnd)) {
                                all_ops_inv = false;
                                break;
                            }
                        }

                        if (all_ops_inv) {
                            // Check non-zero divisor for division / modulo
                            if (inst->op == ir::IROp::SDiv || inst->op == ir::IROp::UDiv ||
                                inst->op == ir::IROp::SRem || inst->op == ir::IROp::URem) {
                                int64_t div_c = 0;
                                if (!is_const_int_val(fn, inst->operands[1], div_c) || div_c == 0) {
                                    continue;
                                }
                            }

                            invariant_set.insert(inst.get());
                            ordered_invariants.push_back(inst.get());
                            progress = true;
                        }
                    }
                }
            }

            if (!ordered_invariants.empty()) {
                for (ir::IRInst* inst : ordered_invariants) {
                    ir::BasicBlock* old_bb = inst->parent;
                    if (!old_bb) continue;

                    std::unique_ptr<ir::IRInst> owned_inst;
                    for (auto it = old_bb->instructions.begin(); it != old_bb->instructions.end(); ++it) {
                        if (it->get() == inst) {
                            owned_inst = std::move(*it);
                            old_bb->instructions.erase(it);
                            break;
                        }
                    }

                    if (owned_inst) {
                        owned_inst->parent = preheader;
                        if (!preheader->instructions.empty() && preheader->instructions.back()->is_terminator()) {
                            preheader->instructions.insert(preheader->instructions.end() - 1, std::move(owned_inst));
                        } else {
                            preheader->instructions.push_back(std::move(owned_inst));
                        }
                        changed = true;
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

    bool GrassAdvancedOpt::run_licm(ir::IRModule& module) {
        bool changed = false;
        for (auto& fn : module.functions) {
            if (run_licm(*fn)) changed = true;
        }
        return changed;
    }

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

    // =========================================================================
    // 5. Unified Entry Point
    // =========================================================================

    bool GrassAdvancedOpt::optimize(ir::IRModule& module, int opt_level) {
        if (opt_level <= 0) return false;

        Options opts;
        if (opt_level == 1) {
            opts.enable_inlining = false;
            opts.enable_licm = false;
            opts.enable_unrolling = false;
            opts.enable_strength_reduction = true;
        } else if (opt_level == 2) {
            opts.inline_threshold = 30;
            opts.unroll_factor = 4;
            opts.enable_inlining = true;
            opts.enable_licm = true;
            opts.enable_unrolling = true;
            opts.enable_strength_reduction = true;
        } else {
            opts.inline_threshold = 40;
            opts.unroll_factor = 4;
            opts.enable_inlining = true;
            opts.enable_licm = true;
            opts.enable_unrolling = true;
            opts.enable_strength_reduction = true;
        }

        return optimize(module, opts);
    }

    bool GrassAdvancedOpt::optimize(ir::IRModule& module, const Options& opts) {
        bool changed = false;

        // Cleanups and initial strength reduction
        for (auto& fn : module.functions) {
            if (fn->is_declaration || fn->blocks.empty()) continue;
            ir::ConstantFoldingPass cf;
            cf.run_on_function(*fn);
            if (opts.enable_strength_reduction) {
                run_strength_reduction(*fn);
            }
            ir::CFGSimplifyPass cfg;
            cfg.run_on_function(*fn);
            ir::DeadCodeEliminationPass dce;
            dce.run_on_function(*fn);
        }

        // Function inlining
        if (opts.enable_inlining) {
            if (run_inlining(module, opts.inline_threshold)) {
                changed = true;
                for (auto& fn : module.functions) {
                    if (fn->is_declaration || fn->blocks.empty()) continue;
                    ir::CFGSimplifyPass cfg;
                    cfg.run_on_function(*fn);
                    ir::DeadCodeEliminationPass dce;
                    dce.run_on_function(*fn);
                }
            }
        }

        // LICM
        if (opts.enable_licm) {
            if (run_licm(module)) {
                changed = true;
            }
        }

        // Loop unrolling
        if (opts.enable_unrolling) {
            if (run_loop_unrolling(module, opts.unroll_factor)) {
                changed = true;
            }
        }

        // Final cleanup passes
        for (auto& fn : module.functions) {
            if (fn->is_declaration || fn->blocks.empty()) continue;
            if (opts.enable_strength_reduction) {
                if (run_strength_reduction(*fn)) changed = true;
            }
            ir::ConstantFoldingPass cf;
            cf.run_on_function(*fn);
            ir::CommonSubexpressionEliminationPass cse;
            cse.run_on_function(*fn);
            ir::DeadCodeEliminationPass dce;
            dce.run_on_function(*fn);
            ir::CFGSimplifyPass cfg;
            cfg.run_on_function(*fn);
        }

        return changed;
    }

} // namespace grass::opt
