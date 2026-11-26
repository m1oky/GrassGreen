#include "native/GrassIR.h"

#include <iostream>
#include <iomanip>
#include <algorithm>
#include <cassert>

namespace grass::ir {

    // =========================================================================
    // 4. IRFunction Implementation
    // =========================================================================

    IRFunction::IRFunction(std::string name_in, IRType* ret_type_in, IRModule* parent_in)
        : name(std::move(name_in)), return_type(ret_type_in), parent(parent_in) {}

    ValueId IRFunction::allocate_value_id() {
        return next_value_id++;
    }

    ValueId IRFunction::create_value(IRType* type, const std::string& val_name, IRInst* def) {
        ValueId id = allocate_value_id();
        IRValue v;
        v.id = id;
        v.type = type;
        v.name = val_name.empty() ? ("val" + std::to_string(id)) : val_name;
        v.def_inst = def;
        values[id] = v;
        return id;
    }

    static IRValue kDummyInvalidValue{};

    IRValue& IRFunction::get_value(ValueId id) {
        auto it = values.find(id);
        if (it != values.end()) return it->second;
        return kDummyInvalidValue;
    }

    const IRValue& IRFunction::get_value(ValueId id) const {
        auto it = values.find(id);
        if (it != values.end()) return it->second;
        return kDummyInvalidValue;
    }

    bool IRFunction::has_value(ValueId id) const {
        return values.find(id) != values.end();
    }

    void IRFunction::add_argument(const std::string& arg_name, IRType* type) {
        ValueId id = create_value(type, arg_name, nullptr);
        IRValue& val = get_value(id);
        val.is_argument = true;
        val.arg_index = static_cast<uint32_t>(arguments.size());
        arguments.push_back(val);
    }

    BasicBlock* IRFunction::create_block(const std::string& blk_name) {
        std::string final_name = blk_name.empty() ? ("bb" + std::to_string(blocks.size())) : blk_name;
        auto bb = std::make_unique<BasicBlock>(final_name, this);
        BasicBlock* raw = bb.get();
        blocks.push_back(std::move(bb));
        return raw;
    }

    BasicBlock* IRFunction::get_entry_block() const {
        return blocks.empty() ? nullptr : blocks.front().get();
    }

    void IRFunction::replace_all_uses_with(ValueId old_val, ValueId new_val) {
        if (old_val == new_val) return;
        if (!has_value(old_val)) return;

        std::vector<IRInst*> old_users = values[old_val].users;
        for (IRInst* user : old_users) {
            user->replace_operand(old_val, new_val);
            if (has_value(new_val)) {
                auto& new_users = values[new_val].users;
                if (std::find(new_users.begin(), new_users.end(), user) == new_users.end()) {
                    new_users.push_back(user);
                }
            }
        }
        values[old_val].users.clear();
    }

    void IRFunction::erase_inst(IRInst* inst) {
        if (!inst) return;
        for (ValueId opnd : inst->operands) {
            if (has_value(opnd)) {
                auto& u = values[opnd].users;
                u.erase(std::remove(u.begin(), u.end(), inst), u.end());
            }
        }
        for (const auto& in : inst->phi_incoming) {
            if (has_value(in.value)) {
                auto& u = values[in.value].users;
                u.erase(std::remove(u.begin(), u.end(), inst), u.end());
            }
        }
        if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
            values[inst->result_id].def_inst = nullptr;
        }
        if (inst->parent) {
            inst->parent->erase(inst);
        }
    }

    void IRFunction::erase_block(BasicBlock* bb) {
        if (!bb) return;
        for (auto& inst : bb->instructions) {
            if (!inst) continue;
            for (ValueId opnd : inst->operands) {
                if (has_value(opnd)) {
                    auto& u = values[opnd].users;
                    u.erase(std::remove(u.begin(), u.end(), inst.get()), u.end());
                }
            }
            for (const auto& in : inst->phi_incoming) {
                if (has_value(in.value)) {
                    auto& u = values[in.value].users;
                    u.erase(std::remove(u.begin(), u.end(), inst.get()), u.end());
                }
            }
            if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
                values[inst->result_id].def_inst = nullptr;
            }
        }
        bb->instructions.clear();
        for (BasicBlock* s : bb->succs) {
            for (auto& inst : s->instructions) {
                if (inst->op != IROp::Phi) break;
                inst->remove_phi_incoming(bb);
            }
            s->remove_pred(bb);
        }
        for (BasicBlock* p : bb->preds) {
            p->remove_succ(bb);
        }
        for (auto it = blocks.begin(); it != blocks.end(); ++it) {
            if (it->get() == bb) {
                blocks.erase(it);
                break;
            }
        }
    }

    void IRFunction::rebuild_cfg() {
        for (auto& bb : blocks) {
            bb->preds.clear();
            bb->succs.clear();
        }

        for (auto& bb : blocks) {
            IRInst* term = bb->get_terminator();
            if (!term) continue;

            if (term->op == IROp::Jump && term->true_target) {
                bb->add_succ(term->true_target);
                term->true_target->add_pred(bb.get());
            } else if (term->op == IROp::Branch) {
                if (term->true_target) {
                    bb->add_succ(term->true_target);
                    term->true_target->add_pred(bb.get());
                }
                if (term->false_target) {
                    bb->add_succ(term->false_target);
                    term->false_target->add_pred(bb.get());
                }
            }
        }
    }

    void IRFunction::rebuild_def_use() {
        for (auto& [id, val] : values) {
            val.users.clear();
            val.def_inst = nullptr;
        }

        for (auto& bb : blocks) {
            for (auto& inst : bb->instructions) {
                if (inst->result_id != kInvalidValueId && has_value(inst->result_id)) {
                    values[inst->result_id].def_inst = inst.get();
                }
                for (ValueId opnd : inst->operands) {
                    if (has_value(opnd)) {
                        auto& u = values[opnd].users;
                        if (std::find(u.begin(), u.end(), inst.get()) == u.end()) {
                            u.push_back(inst.get());
                        }
                    }
                }
                for (const auto& in : inst->phi_incoming) {
                    if (has_value(in.value)) {
                        auto& u = values[in.value].users;
                        if (std::find(u.begin(), u.end(), inst.get()) == u.end()) {
                            u.push_back(inst.get());
                        }
                    }
                }
            }
        }
    }

    bool IRFunction::verify(std::string& error_out) const {
        if (is_declaration || blocks.empty()) {
            return true;
        }

        for (size_t b_idx = 0; b_idx < blocks.size(); ++b_idx) {
            const auto& bb = blocks[b_idx];
            if (bb->instructions.empty()) {
                error_out = "Block '" + bb->name + "' has no instructions.";
                return false;
            }

            for (size_t i = 0; i < bb->instructions.size(); ++i) {
                const auto& inst = bb->instructions[i];
                bool is_last = (i == bb->instructions.size() - 1);

                if (inst->is_terminator()) {
                    if (!is_last) {
                        error_out = "Terminator instruction in middle of block '" + bb->name + "'";
                        return false;
                    }
                } else if (is_last) {
                    error_out = "Block '" + bb->name + "' does not end with a terminator.";
                    return false;
                }

                if (inst->op == IROp::Phi) {
                    if (i > 0 && bb->instructions[i - 1]->op != IROp::Phi) {
                        error_out = "Phi instruction not at start of block '" + bb->name + "'";
                        return false;
                    }

                    if (inst->phi_incoming.size() != bb->preds.size()) {
                        error_out = "Phi in '" + bb->name + "' has " + std::to_string(inst->phi_incoming.size()) +
                                    " incoming branches, but block has " + std::to_string(bb->preds.size()) + " predecessors.";
                        return false;
                    }

                    std::unordered_set<BasicBlock*> seen_preds;
                    for (const auto& in : inst->phi_incoming) {
                        if (!in.block || std::find(bb->preds.begin(), bb->preds.end(), in.block) == bb->preds.end()) {
                            error_out = "Phi in '" + bb->name + "' has incoming block that is not a CFG predecessor.";
                            return false;
                        }
                        if (!seen_preds.insert(in.block).second) {
                            error_out = "Phi in '" + bb->name + "' has duplicate incoming entry for block '" + in.block->name + "'";
                            return false;
                        }
                        if (has_value(in.value)) {
                            const IRValue& in_val = get_value(in.value);
                            if (inst->type && in_val.type && !inst->type->equals(in_val.type)) {
                                error_out = "Phi type mismatch in block '" + bb->name + "': expected " +
                                            inst->type->to_string() + " but got " + in_val.type->to_string();
                                return false;
                            }
                        }
                    }
                }

                for (ValueId opnd : inst->operands) {
                    if (!has_value(opnd)) {
                        error_out = "Instruction " + ir::to_string(inst->op) + " uses undefined ValueId %" + std::to_string(opnd) +
                                    " in block '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Branch) {
                    if (inst->operands.empty() || !has_value(inst->operands[0])) {
                        error_out = "Branch without condition in '" + bb->name + "'";
                        return false;
                    }
                    if (!get_value(inst->operands[0]).type->is_bool()) {
                        error_out = "Branch condition is not i1 in '" + bb->name + "'";
                        return false;
                    }
                    if (!inst->true_target || !inst->false_target) {
                        error_out = "Branch missing target in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Jump && !inst->true_target) {
                    error_out = "Jump missing target in '" + bb->name + "'";
                    return false;
                }

                if (inst->is_binary_arithmetic() && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && !t1->equals(t2)) {
                        error_out = "Binary op operand type mismatch in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::ICmp && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && !t1->equals(t2)) {
                        error_out = "ICmp operand type mismatch in '" + bb->name + "' of '" + name + "': " + (t1 ? t1->to_string() : "null") + " vs " + (t2 ? t2->to_string() : "null");
                        return false;
                    }
                }

                if (inst->op == IROp::FCmp && inst->operands.size() >= 2) {
                    IRType* t1 = get_value(inst->operands[0]).type;
                    IRType* t2 = get_value(inst->operands[1]).type;
                    if (t1 && t2 && (!t1->equals(t2) || !t1->is_floating_point())) {
                        error_out = "FCmp operands must be matching floating-point types in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Load && !inst->operands.empty()) {
                    IRType* pt = get_value(inst->operands[0]).type;
                    if (pt && !pt->is_pointer()) {
                        error_out = "Load operand must be a pointer in '" + bb->name + "'";
                        return false;
                    }
                }

                if (inst->op == IROp::Store && inst->operands.size() >= 2) {
                    IRType* vt = get_value(inst->operands[0]).type;
                    IRType* pt = get_value(inst->operands[1]).type;
                    if (pt && !pt->is_pointer()) {
                        error_out = "Store pointer operand must be a pointer in '" + bb->name + "'";
                        return false;
                    }
                    if (pt && pt->element_type && vt && !pt->element_type->equals(vt)) {
                        bool both_int = pt->element_type->is_integer() && vt->is_integer();
                        bool ptr_int = (pt->element_type->is_pointer() && vt->is_integer()) ||
                                       (pt->element_type->is_integer() && vt->is_pointer()) ||
                                       (pt->element_type->is_pointer() && vt->is_pointer());
                        if (!both_int && !ptr_int) {
                            error_out = "Store value type does not match pointer element type in '" + bb->name + "'";
                            return false;
                        }
                    }
                }

                if (inst->op == IROp::Ret) {
                    if (inst->operands.empty()) {
                        if (return_type && !return_type->is_void()) {
                            error_out = "Return void in function '" + name + "' returning non-void type";
                            return false;
                        }
                    } else {
                        IRType* ret_val_t = get_value(inst->operands[0]).type;
                        if (return_type && ret_val_t && !return_type->equals(ret_val_t)) {
                            error_out = "Return value type mismatch in function '" + name + "': expected " +
                                        (return_type ? return_type->to_string() : "null") + ", got " +
                                        (ret_val_t ? ret_val_t->to_string() : "null");
                            return false;
                        }
                    }
                }
            }
        }

        return true;
    }

    std::string IRFunction::dump() const {
        std::ostringstream ss;
        ss << "define " << (return_type ? return_type->to_string() : "void")
           << " @" << name << "(";
        for (size_t i = 0; i < arguments.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << (arguments[i].type ? arguments[i].type->to_string() : "unknown")
               << " %" << arguments[i].id;
        }
        ss << ") {\n";

        for (const auto& bb : blocks) {
            ss << bb->dump();
        }
        ss << "}\n";
        return ss.str();
    }

    // =========================================================================
    // 5. IRModule Implementation
    // =========================================================================

    IRModule::IRModule(std::string name_in) : name(std::move(name_in)) {}

    IRFunction* IRModule::create_function(const std::string& fn_name, IRType* ret_type) {
        auto fn = std::make_unique<IRFunction>(fn_name, ret_type, this);
        IRFunction* raw = fn.get();
        functions.push_back(std::move(fn));
        return raw;
    }

    IRFunction* IRModule::get_function(const std::string& fn_name) const {
        for (const auto& fn : functions) {
            if (fn->name == fn_name) return fn.get();
        }
        return nullptr;
    }

    bool IRModule::verify(std::string& error_out) const {
        for (const auto& fn : functions) {
            if (!fn->verify(error_out)) return false;
        }
        return true;
    }

    std::string IRModule::dump() const {
        std::ostringstream ss;
        ss << "; GrassIR Module: " << name << "\n\n";
        for (const auto& fn : functions) {
            ss << fn->dump() << "\n";
        }
        return ss.str();
    }


} // namespace grass::ir
