#include "native/GrassIR.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace grass::ir {

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


} // namespace grass::ir
