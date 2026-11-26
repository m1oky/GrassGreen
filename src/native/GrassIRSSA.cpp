#include "native/GrassIR.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace grass::ir {

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
                        if (current->type && current->type->is_vector()) {
                            current_stack_slot = (current_stack_slot + 31) & ~31;
                            current->stack_slot_offset = current_stack_slot;
                            current_stack_slot += 32;
                        } else {
                            current->stack_slot_offset = current_stack_slot;
                            current_stack_slot += 8;
                        }
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
            if (interval.type && (interval.type->is_floating_point() || interval.type->is_vector())) {
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
