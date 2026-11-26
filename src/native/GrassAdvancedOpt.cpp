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
            ir::ValueId actual = call_owner->operands[a];
            ir::IRType* formal_t = callee.arguments[a].type;
            ir::IRType* actual_t = caller.has_value(actual) ? caller.get_value(actual).type : nullptr;
            if (formal_t && actual_t && !formal_t->equals(actual_t)) {
                if (actual_t->is_pointer() && formal_t->is_integer()) {
                    auto cast_inst = std::make_unique<ir::IRInst>();
                    cast_inst->op = ir::IROp::PtrToInt;
                    cast_inst->type = formal_t;
                    cast_inst->operands.push_back(actual);
                    ir::ValueId cast_vid = caller.create_value(formal_t, "inl_cast", cast_inst.get());
                    cast_inst->result_id = cast_vid;
                    cast_inst->loc = call_loc;
                    call_bb->push_back(std::move(cast_inst));
                    val_map[callee.arguments[a].id] = cast_vid;
                    continue;
                } else if (actual_t->is_integer() && formal_t->is_pointer()) {
                    auto cast_inst = std::make_unique<ir::IRInst>();
                    cast_inst->op = ir::IROp::IntToPtr;
                    cast_inst->type = formal_t;
                    cast_inst->operands.push_back(actual);
                    ir::ValueId cast_vid = caller.create_value(formal_t, "inl_cast", cast_inst.get());
                    cast_inst->result_id = cast_vid;
                    cast_inst->loc = call_loc;
                    call_bb->push_back(std::move(cast_inst));
                    val_map[callee.arguments[a].id] = cast_vid;
                    continue;
                } else if (actual_t->is_integer() && formal_t->is_integer()) {
                    auto cast_inst = std::make_unique<ir::IRInst>();
                    if (actual_t->bit_width() < formal_t->bit_width()) {
                        cast_inst->op = ir::IROp::SExt;
                    } else {
                        cast_inst->op = ir::IROp::Trunc;
                    }
                    cast_inst->type = formal_t;
                    cast_inst->operands.push_back(actual);
                    ir::ValueId cast_vid = caller.create_value(formal_t, "inl_cast", cast_inst.get());
                    cast_inst->result_id = cast_vid;
                    cast_inst->loc = call_loc;
                    call_bb->push_back(std::move(cast_inst));
                    val_map[callee.arguments[a].id] = cast_vid;
                    continue;
                }
            }
            val_map[callee.arguments[a].id] = actual;
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

            // Detect whether loop modifies any vec<T> header or calls functions, so pure vec<T>
            // field loads (data ptr & size) can be safely hoisted into the preheader for SIMD.
            bool loop_has_calls = false;
            std::unordered_set<ir::ValueId> written_vec_bases;
            for (ir::BasicBlock* bb : loop.blocks) {
                for (const auto& inst : bb->instructions) {
                    if (inst->op == ir::IROp::Call || inst->op == ir::IROp::MemCpy || inst->op == ir::IROp::MemSet) {
                        loop_has_calls = true;
                        break;
                    }
                    if (inst->op == ir::IROp::Store && inst->operands.size() >= 2) {
                        ir::ValueId ptr_v = inst->operands[1];
                        ir::IRInst* pdef = fn.has_value(ptr_v) ? fn.get_value(ptr_v).def_inst : nullptr;
                        if (pdef && pdef->op == ir::IROp::GetElementPtr && pdef->gep_field_index >= 0 && !pdef->operands.empty()) {
                            written_vec_bases.insert(pdef->operands[0]);
                        } else if (fn.has_value(ptr_v) && fn.get_value(ptr_v).type &&
                                   fn.get_value(ptr_v).type->is_pointer() &&
                                   fn.get_value(ptr_v).type->element_type &&
                                   fn.get_value(ptr_v).type->element_type->is_struct() &&
                                   fn.get_value(ptr_v).type->element_type->struct_name.rfind("__vec_", 0) == 0) {
                            written_vec_bases.insert(ptr_v);
                        }
                    }
                }
                if (loop_has_calls) break;
            }

            auto is_invariant_vec_header_load = [&](const ir::IRInst* inst) -> bool {
                if (loop_has_calls || !inst || inst->op != ir::IROp::Load || inst->operands.empty()) return false;
                if (inst->parent != loop.header && loop.blocks.size() > 2) return false;
                ir::IRInst* pdef = fn.has_value(inst->operands[0]) ? fn.get_value(inst->operands[0]).def_inst : nullptr;
                if (!pdef || pdef->op != ir::IROp::GetElementPtr || pdef->gep_field_index < 0 || pdef->operands.empty()) {
                    return false;
                }
                ir::ValueId base = pdef->operands[0];
                if (!fn.has_value(base)) return false;
                ir::IRType* bt = fn.get_value(base).type;
                if (!bt || !bt->is_pointer() || !bt->element_type || !bt->element_type->is_struct()) return false;
                if (bt->element_type->struct_name.rfind("__vec_", 0) != 0) return false;
                if (written_vec_bases.count(base)) return false;
                return true;
            };

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
                        if (!is_hoistable_instruction(inst.get()) && !is_invariant_vec_header_load(inst.get())) continue;

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

        // SIMD Loop Vectorization (SSE2 / AVX2)
        if (opts.enable_vectorization) {
            if (run_loop_vectorization(module, opts.vector_width)) {
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
