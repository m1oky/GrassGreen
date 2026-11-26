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
    // 4b. SIMD Loop Vectorization (SSE2 / AVX2)
    // =========================================================================

    bool GrassAdvancedOpt::run_loop_vectorization(ir::IRFunction& fn, int vector_width) {
        if (vector_width != 2 && vector_width != 4) return false;
        if (fn.is_declaration || fn.blocks.empty() || !fn.parent) return false;

        bool changed = false;
        std::unordered_set<ir::BasicBlock*> vectorized_blocks;
        std::vector<NaturalLoop> loops = detect_natural_loops(fn);
        ir::IRContext& ctx = fn.parent->context;
        ir::IRType* i64_t = ctx.get_i64_type();

        for (const auto& loop : loops) {
            if (loop.latches.size() != 1 || loop.blocks.size() > 2) continue;
            bool already_done = false;
            for (ir::BasicBlock* b : loop.blocks) {
                if (vectorized_blocks.count(b)) { already_done = true; break; }
            }
            if (already_done) continue;

            ir::BasicBlock* header = loop.header;
            ir::BasicBlock* latch = loop.latches[0];
            ir::IRInst* hdr_term = header->get_terminator();
            if (!hdr_term || hdr_term->op != ir::IROp::Branch || hdr_term->operands.empty()) continue;

            ir::BasicBlock* body_bb = hdr_term->true_target;
            ir::BasicBlock* exit_bb = hdr_term->false_target;
            if (!body_bb || !exit_bb) continue;
            if (loop.blocks.find(body_bb) == loop.blocks.end() || loop.blocks.find(exit_bb) != loop.blocks.end()) continue;
            if (loop.blocks.size() == 2 && body_bb != latch) continue;

            ir::BasicBlock* preheader = get_or_create_preheader(fn, loop);
            if (!preheader) continue;

            // 1. Identify induction variable: iv_phi = Phi(iv_init, iv + 1)
            ir::IRInst* iv_phi = nullptr;
            ir::IRInst* iv_step = nullptr;
            ir::ValueId iv_init = ir::kInvalidValueId;
            ir::ValueId limit_val = ir::kInvalidValueId;
            ir::IRInst* cmp_inst = fn.has_value(hdr_term->operands[0]) ? fn.get_value(hdr_term->operands[0]).def_inst : nullptr;
            if (!cmp_inst || cmp_inst->op != ir::IROp::ICmp || cmp_inst->operands.size() < 2) continue;
            if (cmp_inst->icmp_cond != ir::ICmpCond::SLT && cmp_inst->icmp_cond != ir::ICmpCond::ULT) continue;

            // Helper: unwrap SExt/ZExt
            auto unwrap_ext = [&](ir::ValueId v) -> ir::ValueId {
                if (fn.has_value(v)) {
                    ir::IRInst* d = fn.get_value(v).def_inst;
                    if (d && (d->op == ir::IROp::SExt || d->op == ir::IROp::ZExt) && !d->operands.empty()) {
                        return d->operands[0];
                    }
                }
                return v;
            };
            ir::ValueId cmp_lhs_base = unwrap_ext(cmp_inst->operands[0]);

            for (const auto& inst : header->instructions) {
                if (inst->op != ir::IROp::Phi) break;
                ir::ValueId pre_v = inst->get_phi_incoming_value(preheader);
                ir::ValueId lat_v = inst->get_phi_incoming_value(latch);
                if (pre_v == ir::kInvalidValueId || lat_v == ir::kInvalidValueId) continue;
                if (cmp_inst->operands[0] != inst->result_id && cmp_lhs_base != inst->result_id) continue;
                if (!fn.has_value(lat_v)) continue;
                ir::IRInst* s_inst = fn.get_value(lat_v).def_inst;
                if (!s_inst || s_inst->op != ir::IROp::Add || s_inst->operands.size() < 2) continue;
                int64_t step_c = 0;
                bool step_ok = false;
                if (s_inst->operands[0] == inst->result_id && is_const_int_val(fn, s_inst->operands[1], step_c) && step_c == 1) step_ok = true;
                else if (s_inst->operands[1] == inst->result_id && is_const_int_val(fn, s_inst->operands[0], step_c) && step_c == 1) step_ok = true;
                if (!step_ok) continue;

                ir::ValueId cand_lim = cmp_inst->operands[1];
                if (!fn.has_value(cand_lim)) continue;
                ir::IRInst* lim_def = fn.get_value(cand_lim).def_inst;
                if (lim_def && loop.blocks.count(lim_def->parent)) continue;

                iv_phi = inst.get();
                iv_step = s_inst;
                iv_init = pre_v;
                limit_val = cand_lim;
                break;
            }
            if (!iv_phi) continue;

            // 2. Identify reduction Phis in header
            struct RedInfo {
                ir::IRInst* phi = nullptr;
                ir::IRInst* acc_inst = nullptr;
                ir::ValueId init_val = ir::kInvalidValueId;
                ir::ValueId expr_val = ir::kInvalidValueId;
                ir::IRType* elem_type = nullptr;
                bool is_fp = false;
            };
            std::vector<RedInfo> reductions;
            bool bad_phi = false;
            for (const auto& inst : header->instructions) {
                if (inst->op != ir::IROp::Phi) break;
                if (inst.get() == iv_phi) continue;

                ir::ValueId pre_v = inst->get_phi_incoming_value(preheader);
                ir::ValueId lat_v = inst->get_phi_incoming_value(latch);
                if (pre_v == ir::kInvalidValueId || lat_v == ir::kInvalidValueId || !fn.has_value(lat_v)) {
                    bad_phi = true; break;
                }
                ir::IRInst* acc_inst = fn.get_value(lat_v).def_inst;
                if (!acc_inst || (acc_inst->op != ir::IROp::Add && acc_inst->op != ir::IROp::FAdd) || acc_inst->operands.size() < 2) {
                    bad_phi = true; break;
                }
                ir::ValueId expr_v = ir::kInvalidValueId;
                if (acc_inst->operands[0] == inst->result_id) expr_v = acc_inst->operands[1];
                else if (acc_inst->operands[1] == inst->result_id) expr_v = acc_inst->operands[0];
                else { bad_phi = true; break; }

                // acc_phi must only be used by acc_inst inside the loop
                for (ir::IRInst* u : fn.get_value(inst->result_id).users) {
                    if (u != acc_inst && loop.blocks.count(u->parent)) {
                        bad_phi = true; break;
                    }
                }
                if (bad_phi) break;

                ir::IRType* et = inst->type;
                if (!et || (!et->is_i64() && !et->is_f64())) { bad_phi = true; break; }
                reductions.push_back({ inst.get(), acc_inst, pre_v, expr_v, et, et->is_f64() });
            }
            if (bad_phi) continue;

            // 3. Validate all body instructions
            std::unordered_set<ir::IRInst*> skip_insts = { iv_step, cmp_inst };
            for (const auto& r : reductions) skip_insts.insert(r.acc_inst);

            std::vector<ir::IRInst*> body_insts;
            std::unordered_map<ir::ValueId, ir::ValueId> gep_to_base;
            std::vector<ir::ValueId> load_bases;
            std::vector<ir::ValueId> store_bases;
            bool valid_body = true;

            // Preserve block order: header (after phis) then body_bb (if 2-block loop)
            std::vector<ir::BasicBlock*> ordered_blocks = { header };
            if (body_bb != header) ordered_blocks.push_back(body_bb);

            std::unordered_set<ir::ValueId> iv_aliases = { iv_phi->result_id };
            for (ir::BasicBlock* b : ordered_blocks) {
                for (const auto& inst_ptr : b->instructions) {
                    ir::IRInst* inst = inst_ptr.get();
                    if (inst->op == ir::IROp::Phi || inst->is_terminator() || skip_insts.count(inst)) continue;

                    if ((inst->op == ir::IROp::SExt || inst->op == ir::IROp::ZExt) &&
                        !inst->operands.empty() && iv_aliases.count(inst->operands[0])) {
                        iv_aliases.insert(inst->result_id);
                        continue;
                    }
                    if (inst->op == ir::IROp::GetElementPtr) {
                        if (inst->operands.size() != 2 || !iv_aliases.count(inst->operands[1])) {
                            valid_body = false; break;
                        }
                        ir::ValueId base = inst->operands[0];
                        ir::IRInst* base_def = fn.has_value(base) ? fn.get_value(base).def_inst : nullptr;
                        if (base_def && loop.blocks.count(base_def->parent)) { valid_body = false; break; }
                        ir::IRType* ptr_t = fn.get_value(inst->result_id).type;
                        ir::IRType* elem_t = (ptr_t && ptr_t->element_type) ? ptr_t->element_type : nullptr;
                        if (!elem_t || (!elem_t->is_i64() && !elem_t->is_f64())) { valid_body = false; break; }
                        gep_to_base[inst->result_id] = base;
                        body_insts.push_back(inst);
                    } else if (inst->op == ir::IROp::Load) {
                        if (inst->operands.empty() || !gep_to_base.count(inst->operands[0])) {
                            valid_body = false; break;
                        }
                        if (!inst->type || (!inst->type->is_i64() && !inst->type->is_f64())) {
                            valid_body = false; break;
                        }
                        load_bases.push_back(gep_to_base[inst->operands[0]]);
                        body_insts.push_back(inst);
                    } else if (inst->op == ir::IROp::Store) {
                        if (inst->operands.size() < 2 || !gep_to_base.count(inst->operands[1])) {
                            valid_body = false; break;
                        }
                        if (iv_aliases.count(inst->operands[0])) {
                            valid_body = false; break;
                        }
                        ir::IRType* vt = fn.get_value(inst->operands[0]).type;
                        if (!vt || (!vt->is_i64() && !vt->is_f64())) {
                            valid_body = false; break;
                        }
                        store_bases.push_back(gep_to_base[inst->operands[1]]);
                        body_insts.push_back(inst);
                    } else if (inst->op == ir::IROp::Add || inst->op == ir::IROp::Sub ||
                               inst->op == ir::IROp::FAdd || inst->op == ir::IROp::FSub ||
                               inst->op == ir::IROp::FMul) {
                        if (!inst->type || (!inst->type->is_i64() && !inst->type->is_f64())) {
                            valid_body = false; break;
                        }
                        if ((inst->operands.size() > 0 && iv_aliases.count(inst->operands[0])) ||
                            (inst->operands.size() > 1 && iv_aliases.count(inst->operands[1]))) {
                            valid_body = false; break;
                        }
                        body_insts.push_back(inst);
                    } else {
                        valid_body = false; break;
                    }
                }
                if (!valid_body) break;
            }
            if (!valid_body) continue;
            if (store_bases.empty() && reductions.empty()) continue;

            // 4. Collect alias check pairs (store_base vs load_base where distinct & not distinct allocas)
            std::vector<std::pair<ir::ValueId, ir::ValueId>> alias_pairs;
            for (ir::ValueId sb : store_bases) {
                for (ir::ValueId lb : load_bases) {
                    if (sb == lb) continue;
                    ir::IRInst* sd = fn.has_value(sb) ? fn.get_value(sb).def_inst : nullptr;
                    ir::IRInst* ld = fn.has_value(lb) ? fn.get_value(lb).def_inst : nullptr;
                    if (sd && ld && sd->op == ir::IROp::Alloca && ld->op == ir::IROp::Alloca) continue;
                    bool dup = false;
                    for (const auto& p : alias_pairs) {
                        if ((p.first == sb && p.second == lb) || (p.first == lb && p.second == sb)) { dup = true; break; }
                    }
                    if (!dup) alias_pairs.push_back({ sb, lb });
                }
            }

            // 5. Build vectorized loop + scalar tail preheader
            ir::BasicBlock* vec_check = fn.create_block(header->name + "_vec_chk");
            ir::BasicBlock* vec_alias = alias_pairs.empty() ? nullptr : fn.create_block(header->name + "_vec_alias");
            ir::BasicBlock* vec_pre = fn.create_block(header->name + "_vec_pre");
            ir::BasicBlock* vec_header = fn.create_block(header->name + "_vec_hdr");
            ir::BasicBlock* vec_body = fn.create_block(header->name + "_vec_body");
            ir::BasicBlock* vec_exit = fn.create_block(header->name + "_vec_exit");
            ir::BasicBlock* tail_pre = fn.create_block(header->name + "_tail_pre");

            ir::IRBuilder builder(&fn);

            // Redirect preheader -> vec_check
            ir::IRInst* pre_term = preheader->get_terminator();
            if (!pre_term || pre_term->op != ir::IROp::Jump) continue;
            pre_term->true_target = vec_check;

            // vec_check: count = limit64 - iv_init64; if count >= vector_width -> (vec_alias or vec_pre) else tail_pre
            builder.set_insert_point(vec_check);
            ir::ValueId iv_init_64 = (fn.get_value(iv_init).type && fn.get_value(iv_init).type->is_i64())
                ? iv_init : builder.create_sext(iv_init, i64_t, "iv_init64");
            ir::ValueId limit_64 = (fn.get_value(limit_val).type && fn.get_value(limit_val).type->is_i64())
                ? limit_val : builder.create_sext(limit_val, i64_t, "limit64");
            ir::ValueId count_val = builder.create_sub(limit_64, iv_init_64, "vec_cnt");
            ir::ValueId width_c = builder.create_const_i64(vector_width);
            ir::ValueId has_vec = builder.create_icmp(ir::ICmpCond::SGE, count_val, width_c, "has_vec");
            builder.create_branch(has_vec, vec_alias ? vec_alias : vec_pre, tail_pre);

            // vec_alias: runtime overlap check
            if (vec_alias) {
                builder.set_insert_point(vec_alias);
                ir::ValueId eight = builder.create_const_i64(8);
                ir::ValueId bytes = builder.create_mul(count_val, eight, "vec_bytes");
                ir::ValueId all_ok = ir::kInvalidValueId;
                for (const auto& p : alias_pairs) {
                    ir::ValueId p1 = builder.create_ptrtoint(p.first, i64_t, "a_p1");
                    ir::ValueId p2 = builder.create_ptrtoint(p.second, i64_t, "a_p2");
                    ir::ValueId p1_end = builder.create_add(p1, bytes, "a_e1");
                    ir::ValueId p2_end = builder.create_add(p2, bytes, "a_e2");
                    ir::ValueId c1 = builder.create_icmp(ir::ICmpCond::ULE, p1_end, p2, "no_ov1");
                    ir::ValueId c2 = builder.create_icmp(ir::ICmpCond::ULE, p2_end, p1, "no_ov2");
                    ir::ValueId ok = builder.create_or(c1, c2, "pair_ok");
                    all_ok = (all_ok == ir::kInvalidValueId) ? ok : builder.create_and(all_ok, ok, "all_ok");
                }
                builder.create_branch(all_ok, vec_pre, tail_pre);
            }

            // vec_pre: vec_limit = iv_init_64 + (count & ~(vector_width - 1))
            builder.set_insert_point(vec_pre);
            ir::ValueId mask_c = builder.create_const_i64(vector_width - 1);
            ir::ValueId rem_val = builder.create_and(count_val, mask_c, "vec_rem");
            ir::ValueId vec_cnt = builder.create_sub(count_val, rem_val, "vec_main_cnt");
            ir::ValueId vec_limit = builder.create_add(iv_init_64, vec_cnt, "vec_limit");
            std::vector<ir::ValueId> vec_acc_zeros;
            for (const auto& r : reductions) {
                ir::IRType* vt = ctx.get_vec_type(r.elem_type, static_cast<uint64_t>(vector_width));
                vec_acc_zeros.push_back(builder.create_vec_zero(vt, "vec_zero"));
            }
            builder.create_jump(vec_header);

            // vec_header: Phis + branch
            builder.set_insert_point(vec_header);
            ir::ValueId vec_iv = builder.create_phi(i64_t, "vec_iv");
            std::vector<ir::ValueId> vec_acc_phis;
            for (const auto& r : reductions) {
                ir::IRType* vt = ctx.get_vec_type(r.elem_type, static_cast<uint64_t>(vector_width));
                vec_acc_phis.push_back(builder.create_phi(vt, "vec_acc"));
            }
            ir::ValueId vcond = builder.create_icmp(ir::ICmpCond::SLT, vec_iv, vec_limit, "vcond");
            builder.create_branch(vcond, vec_body, vec_exit);

            // vec_body: vectorize DAG
            builder.set_insert_point(vec_body);
            std::unordered_map<ir::ValueId, ir::ValueId> val_map;
            for (ir::ValueId av : iv_aliases) {
                val_map[av] = vec_iv;
            }

            auto get_vec_val = [&](ir::ValueId scalar_or_mapped, ir::IRType* elem_t) -> ir::ValueId {
                auto it = val_map.find(scalar_or_mapped);
                if (it != val_map.end()) return it->second;
                ir::IRType* vt = ctx.get_vec_type(elem_t, static_cast<uint64_t>(vector_width));
                ir::ValueId splat = builder.create_vec_splat(vt, scalar_or_mapped, "vsplat");
                val_map[scalar_or_mapped] = splat;
                return splat;
            };

            for (ir::IRInst* inst : body_insts) {
                if (inst->op == ir::IROp::GetElementPtr) {
                    ir::IRType* ptr_t = fn.get_value(inst->result_id).type;
                    ir::IRType* elem_t = ptr_t->element_type;
                    val_map[inst->result_id] = builder.create_gep(inst->operands[0], { vec_iv }, elem_t, "vgep");
                } else if (inst->op == ir::IROp::Load) {
                    ir::IRType* vt = ctx.get_vec_type(inst->type, static_cast<uint64_t>(vector_width));
                    val_map[inst->result_id] = builder.create_vec_load(vt, val_map[inst->operands[0]], "vload");
                } else if (inst->op == ir::IROp::Store) {
                    ir::IRType* elem_t = fn.get_value(inst->operands[0]).type;
                    ir::ValueId vsrc = get_vec_val(inst->operands[0], elem_t);
                    builder.create_vec_store(vsrc, val_map[inst->operands[1]]);
                } else if (inst->op == ir::IROp::Add || inst->op == ir::IROp::FAdd) {
                    ir::ValueId vl = get_vec_val(inst->operands[0], inst->type);
                    ir::ValueId vr = get_vec_val(inst->operands[1], inst->type);
                    val_map[inst->result_id] = builder.create_vec_add(vl, vr, "vadd");
                } else if (inst->op == ir::IROp::Sub || inst->op == ir::IROp::FSub) {
                    ir::ValueId vl = get_vec_val(inst->operands[0], inst->type);
                    ir::ValueId vr = get_vec_val(inst->operands[1], inst->type);
                    val_map[inst->result_id] = builder.create_vec_sub(vl, vr, "vsub");
                } else if (inst->op == ir::IROp::FMul) {
                    ir::ValueId vl = get_vec_val(inst->operands[0], inst->type);
                    ir::ValueId vr = get_vec_val(inst->operands[1], inst->type);
                    val_map[inst->result_id] = builder.create_vec_mul(vl, vr, "vmul");
                }
            }

            std::vector<ir::ValueId> vec_acc_nexts;
            for (size_t ri = 0; ri < reductions.size(); ++ri) {
                ir::ValueId vexpr = get_vec_val(reductions[ri].expr_val, reductions[ri].elem_type);
                vec_acc_nexts.push_back(builder.create_vec_add(vec_acc_phis[ri], vexpr, "vacc_next"));
            }
            ir::ValueId vec_iv_next = builder.create_add(vec_iv, width_c, "vec_iv_next");
            builder.create_jump(vec_header);

            // Wire vec_header Phis
            fn.get_value(vec_iv).def_inst->add_phi_incoming(vec_pre, iv_init_64);
            fn.get_value(vec_iv).def_inst->add_phi_incoming(vec_body, vec_iv_next);
            for (size_t ri = 0; ri < reductions.size(); ++ri) {
                ir::IRInst* vp = fn.get_value(vec_acc_phis[ri]).def_inst;
                vp->add_phi_incoming(vec_pre, vec_acc_zeros[ri]);
                vp->add_phi_incoming(vec_body, vec_acc_nexts[ri]);
            }

            // vec_exit: horizontal reduction + jump to tail header
            builder.set_insert_point(vec_exit);
            std::vector<ir::ValueId> acc_merged_vals;
            for (size_t ri = 0; ri < reductions.size(); ++ri) {
                ir::ValueId hsum = builder.create_vec_hadd(vec_acc_phis[ri], reductions[ri].elem_type, "vhsum");
                ir::ValueId merged = reductions[ri].is_fp
                    ? builder.create_fadd(reductions[ri].init_val, hsum, "acc_merged")
                    : builder.create_add(reductions[ri].init_val, hsum, "acc_merged");
                acc_merged_vals.push_back(merged);
            }
            ir::ValueId tail_iv_start = (iv_phi->type && iv_phi->type->is_i64())
                ? vec_limit : builder.create_trunc(vec_limit, iv_phi->type, "tail_iv_start");
            builder.create_jump(header);

            // tail_pre -> header
            builder.set_insert_point(tail_pre);
            builder.create_jump(header);

            // Update original header Phis: preheader -> tail_pre, plus vec_exit incoming
            for (const auto& inst : header->instructions) {
                if (inst->op != ir::IROp::Phi) break;
                for (auto& inc : inst->phi_incoming) {
                    if (inc.block == preheader) {
                        inc.block = tail_pre;
                    }
                }
                if (inst.get() == iv_phi) {
                    inst->add_phi_incoming(vec_exit, tail_iv_start);
                } else {
                    for (size_t ri = 0; ri < reductions.size(); ++ri) {
                        if (inst.get() == reductions[ri].phi) {
                            inst->add_phi_incoming(vec_exit, acc_merged_vals[ri]);
                            break;
                        }
                    }
                }
            }

            vectorized_blocks.insert(vec_header);
            vectorized_blocks.insert(vec_body);
            for (ir::BasicBlock* b : loop.blocks) vectorized_blocks.insert(b);
            changed = true;
        }

        if (changed) {
            fn.rebuild_cfg();
            fn.rebuild_def_use();
        }
        return changed;
    }

    bool GrassAdvancedOpt::run_loop_vectorization(ir::IRModule& module, int vector_width) {
        bool changed = false;
        for (auto& fn : module.functions) {
            if (run_loop_vectorization(*fn, vector_width)) changed = true;
        }
        return changed;
    }


} // namespace grass::opt
