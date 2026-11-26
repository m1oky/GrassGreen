#include "native/GrassASTLowerer.h"
#include "frontend/GrassImport.h"
#include "frontend/GrassSema.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace grass::codegen {

    std::string ASTLowerer::current_namespace() const {
        if (namespace_stack_.empty()) return "";
        std::string res;
        for (size_t i = 0; i < namespace_stack_.size(); ++i) {
            if (i > 0) res += "::";
            res += namespace_stack_[i];
        }
        return res;
    }

    std::string ASTLowerer::qualify_name(const std::string& name) const {
        if (name.find("::") != std::string::npos) return name;
        std::string ns = current_namespace();
        if (ns.empty()) return name;
        return ns + "::" + name;
    }

    std::string ASTLowerer::resolve_callee(const std::string& callee) const {
        if (callee.find("::") != std::string::npos) {
            std::string mangled = grass::sema::GrassSema::mangleName(callee);
            if (module_.get_function(mangled)) return mangled;
            if (module_.get_function(callee)) return callee;
            return mangled;
        }
        auto sit = using_symbols_.find(callee);
        if (sit != using_symbols_.end()) {
            std::string mangled = grass::sema::GrassSema::mangleName(sit->second);
            if (module_.get_function(mangled)) return mangled;
            if (module_.get_function(sit->second)) return sit->second;
        }
        if (!namespace_stack_.empty()) {
            std::string q = current_namespace() + "::" + callee;
            std::string mangled = grass::sema::GrassSema::mangleName(q);
            if (module_.get_function(mangled)) return mangled;
            if (module_.get_function(q)) return q;
        }
        for (const auto& uns : using_namespaces_) {
            std::string q = uns + "::" + callee;
            std::string mangled = grass::sema::GrassSema::mangleName(q);
            if (module_.get_function(mangled)) return mangled;
            if (module_.get_function(q)) return q;
        }
        return callee;
    }

    const GrassToken& ASTLowerer::peek(int offset) const {
        size_t idx = pos_ + offset;
        if (idx >= tokens_.size()) return tokens_.back();
        return tokens_[idx];
    }

    GrassToken ASTLowerer::advance() {
        if (pos_ < tokens_.size()) {
            const auto& tok = tokens_[pos_++];
            builder_.set_current_loc(ir::SourceLocation(source_path_, tok.line, tok.col));
            return tok;
        }
        return tokens_.back();
    }

    bool ASTLowerer::match(GrassTokenKind kind) {
        if (peek().kind == kind) {
            advance();
            return true;
        }
        return false;
    }

    bool ASTLowerer::match_ident(const std::string& name) {
        if (peek().kind == TOK_IDENT && peek().text == name) {
            advance();
            return true;
        }
        return false;
    }

    bool ASTLowerer::consume(GrassTokenKind kind, const std::string& err_msg, std::string& error) {
        if (match(kind)) return true;
        error = "Line " + std::to_string(peek().line) + ": " + err_msg;
        return false;
    }

    void ASTLowerer::push_scope() {
        scopes_.emplace_back();
    }

    void ASTLowerer::emit_var_dtor(const VarBinding& b) {
        if (!b.is_raii) return;
        ir::BasicBlock* cur_bb = builder_.get_insert_block();
        if (!cur_bb || cur_bb->has_terminator()) return;
        if (b.is_vec) {
            if (b.type && b.type->is_struct()) {
                emit_vec_dtor(b.alloca_id, b.type, b.vec_elem_type, b.vec_elem_struct);
            }
            return;
        }
        if (b.struct_type_name.empty() || !oop_mgr_) return;
        const auto* cd = oop_mgr_->get_class(b.struct_type_name);
        if (!cd || !cd->has_destructor) return;
        std::string dtor_name = oop_mgr_->get_destructor_name(cd->name);
        ir::IRType* struct_ptr_t = ctx_.get_ptr_type(cd->ir_struct_type);
        if (b.type && b.type->is_pointer()) {
            ir::ValueId obj_ptr = builder_.create_load(b.alloca_id, cd->name + "_dtor_ptr");
            ir::ValueId null_ptr = builder_.create_const_int(b.type, 0);
            ir::ValueId is_nn = builder_.create_icmp_ne(obj_ptr, null_ptr);
            ir::BasicBlock* bb_call = builder_.create_block("dtor_call");
            ir::BasicBlock* bb_skip = builder_.create_block("dtor_skip");
            builder_.create_branch(is_nn, bb_call, bb_skip);
            builder_.set_insert_point(bb_call);
            ir::ValueId cast_ptr = coerce_type(obj_ptr, struct_ptr_t);
            builder_.create_call(dtor_name, ctx_.get_void_type(), { cast_ptr });
            builder_.create_jump(bb_skip);
            builder_.set_insert_point(bb_skip);
        } else {
            ir::ValueId cast_ptr = coerce_type(b.alloca_id, struct_ptr_t);
            builder_.create_call(dtor_name, ctx_.get_void_type(), { cast_ptr });
        }
    }

    void ASTLowerer::emit_struct_field_dtors(ir::ValueId this_ptr, const std::string& struct_name) {
        if (!oop_mgr_ || struct_name.empty() || !this_ptr) return;
        ir::BasicBlock* cur_bb = builder_.get_insert_block();
        if (!cur_bb || cur_bb->has_terminator()) return;
        const auto* cd = oop_mgr_->get_class(struct_name);
        if (!cd) return;
        for (auto it = cd->fields.rbegin(); it != cd->fields.rend(); ++it) {
            if (it->type && it->type->is_struct()) {
                auto vit = vec_struct_meta_.find(it->type->struct_name);
                if (vit != vec_struct_meta_.end()) {
                    ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, this_ptr, struct_name, it->name, it->name + "_vdtor_gep");
                    if (fptr != ir::kInvalidValueId) {
                        emit_vec_dtor(fptr, it->type, vit->second.elem_type, vit->second.elem_struct);
                    }
                    continue;
                }
                const auto* fcd = oop_mgr_->get_class(it->type->struct_name);
                if (fcd && fcd->has_destructor) {
                    ir::ValueId fptr = oop_mgr_->emit_field_gep(builder_, this_ptr, struct_name, it->name, it->name + "_dtor_gep");
                    if (fptr != ir::kInvalidValueId) {
                        builder_.create_call(oop_mgr_->get_destructor_name(fcd->name), ctx_.get_void_type(), { fptr });
                    }
                }
            }
        }
    }

    void ASTLowerer::emit_scope_cleanups(size_t frame_idx) {
        if (frame_idx >= scopes_.size()) return;
        ir::BasicBlock* cur_bb = builder_.get_insert_block();
        if (!cur_bb || cur_bb->has_terminator()) return;
        const auto& frame = scopes_[frame_idx];
        for (auto it = frame.raii_vars.rbegin(); it != frame.raii_vars.rend(); ++it) {
            auto vit = frame.vars.find(*it);
            if (vit != frame.vars.end()) {
                emit_var_dtor(vit->second);
            }
        }
    }

    void ASTLowerer::emit_scopes_cleanup_to(size_t target_depth) {
        for (size_t idx = scopes_.size(); idx > target_depth; --idx) {
            emit_scope_cleanups(idx - 1);
        }
    }

    void ASTLowerer::pop_scope(bool emit_cleanups) {
        if (scopes_.empty()) return;
        if (emit_cleanups) {
            emit_scope_cleanups(scopes_.size() - 1);
        }
        scopes_.pop_back();
    }

    bool ASTLowerer::declare_var(const std::string& name, ir::ValueId alloca_id, ir::IRType* type,
                     const std::string& struct_name, bool is_raii,
                     bool is_vec, ir::IRType* vec_elem_t, const std::string& vec_elem_struct,
                     bool is_array, ir::IRType* arr_elem_t, const std::string& arr_elem_struct) {
        if (scopes_.empty()) push_scope();
        VarBinding vb;
        vb.alloca_id = alloca_id;
        vb.type = type;
        vb.struct_type_name = struct_name;
        vb.is_raii = is_raii;
        vb.is_vec = is_vec;
        vb.vec_elem_type = vec_elem_t;
        vb.vec_elem_struct = vec_elem_struct;
        vb.is_array = is_array;
        vb.array_elem_type = arr_elem_t;
        vb.array_elem_struct = arr_elem_struct;
        scopes_.back().vars[name] = vb;
        if (is_raii) {
            scopes_.back().raii_vars.push_back(name);
        }
        return true;
    }

    const ASTLowerer::VarBinding* ASTLowerer::lookup_var(const std::string& name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->vars.find(name);
            if (f != it->vars.end()) return &f->second;
        }
        auto gf = global_vars_.find(name);
        if (gf != global_vars_.end()) return &gf->second;
        return nullptr;
    }

    ASTLowerer::VarBinding* ASTLowerer::lookup_var_mut(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto f = it->vars.find(name);
            if (f != it->vars.end()) return &f->second;
        }
        auto gf = global_vars_.find(name);
        if (gf != global_vars_.end()) return &gf->second;
        return nullptr;
    }

    ir::ValueId ASTLowerer::var_addr(const VarBinding* var) {
        if (!var) return 0;
        if (var->is_global) {
            ir::IRType* pt = ctx_.get_ptr_type(var->type ? var->type : ctx_.get_i64_type());
            return builder_.create_call(var->global_sym, pt, {});
        }
        return var->alloca_id;
    }

    ir::IRType* ASTLowerer::predict_array_elem_from_tokens(const std::vector<GrassToken>& toks, size_t start, size_t end) {
        ir::IRType* deduced = nullptr;
        size_t i = start;
        if (i < end && toks[i].kind == TOK_LBRACKET) i++;
        int bdepth = 0, pdepth = 0;
        while (i < end) {
            const auto& tk = toks[i];
            if (tk.kind == TOK_LBRACKET) { bdepth++; i++; continue; }
            if (tk.kind == TOK_RBRACKET) {
                if (bdepth == 0) break;
                bdepth--; i++; continue;
            }
            if (tk.kind == TOK_LPAREN) { pdepth++; i++; continue; }
            if (tk.kind == TOK_RPAREN) { if (pdepth > 0) pdepth--; i++; continue; }
            if (bdepth == 0 && pdepth == 0) {
                ir::IRType* et = nullptr;
                if (tk.kind == TOK_STRING) {
                    et = ctx_.get_ptr_type(ctx_.get_i8_type());
                } else if (tk.kind == TOK_NUMBER) {
                    bool is_fp = (tk.text.find('.') != std::string::npos ||
                                  tk.text.find('e') != std::string::npos ||
                                  tk.text.find('E') != std::string::npos);
                    et = is_fp ? ctx_.get_f64_type() : ctx_.get_i64_type();
                } else if (tk.kind == TOK_IDENT && (tk.text == "true" || tk.text == "false")) {
                    et = ctx_.get_i1_type();
                } else if (tk.kind == TOK_IDENT) {
                    if (const VarBinding* vb = lookup_var(tk.text)) {
                        et = vb->type;
                    }
                }
                if (et) {
                    if (!deduced) deduced = et;
                    else if (!deduced->equals(et)) {
                        if (deduced->is_numeric() && et->is_numeric() &&
                            (deduced->is_floating_point() || et->is_floating_point())) {
                            deduced = ctx_.get_f64_type();
                        }
                    }
                }
            }
            i++;
        }
        return deduced ? deduced : ctx_.get_i64_type();
    }

    ir::ValueId ASTLowerer::encode_array_slot(ir::ValueId val, ir::IRType* target_elem_t) {
        if (!builder_.get_function() || !builder_.get_function()->has_value(val)) return val;
        ir::IRType* vt = builder_.get_function()->get_value(val).type;
        ir::IRType* use_t = target_elem_t ? target_elem_t : vt;
        if (use_t && use_t->is_floating_point()) {
            ir::ValueId fv = coerce_type(val, ctx_.get_f64_type());
            return builder_.create_bitcast(fv, ctx_.get_i64_type(), "arr_f2i");
        }
        if (vt && vt->is_pointer()) {
            return builder_.create_ptrtoint(val, ctx_.get_i64_type());
        }
        return coerce_type(val, ctx_.get_i64_type());
    }

    ir::ValueId ASTLowerer::decode_array_slot(ir::ValueId raw_i64, ir::IRType* elem_t, const std::string& elem_struct) {
        if (!elem_t) {
            last_struct_name_.clear();
            return raw_i64;
        }
        if (elem_t->is_floating_point()) {
            last_struct_name_.clear();
            return builder_.create_bitcast(raw_i64, ctx_.get_f64_type(), "arr_i2f");
        }
        if (elem_t->is_pointer()) {
            if (!elem_struct.empty()) {
                last_struct_name_ = elem_struct;
            } else if (elem_t->element_type && elem_t->element_type->is_struct()) {
                last_struct_name_ = elem_t->element_type->struct_name;
            } else {
                last_struct_name_.clear();
            }
            return builder_.create_inttoptr(raw_i64, elem_t, "arr_i2p");
        }
        if (elem_t->is_bool()) {
            last_struct_name_.clear();
            return builder_.create_icmp_ne(raw_i64, builder_.create_const_i64(0), "arr_i2b");
        }
        last_struct_name_.clear();
        return coerce_type(raw_i64, elem_t);
    }

    void ASTLowerer::emit_zero_struct(ir::ValueId struct_alloca, ir::IRType* st_type) {
        if (!struct_alloca || !st_type || !st_type->is_struct()) return;
        uint64_t total_sz = st_type->size();
        uint64_t qwords = (total_sz + 7) / 8;
        ir::IRType* i64_ptr_t = ctx_.get_ptr_type(ctx_.get_i64_type());
        ir::ValueId base_p = builder_.create_bitcast(struct_alloca, i64_ptr_t, "zinit_ptr");
        ir::ValueId zero64 = builder_.create_const_i64(0);
        for (uint64_t i = 0; i < qwords; ++i) {
            ir::ValueId slot_p = (i == 0) ? base_p : builder_.create_gep(base_p, { builder_.create_const_i64(static_cast<int64_t>(i)) }, ctx_.get_i64_type());
            builder_.create_store(zero64, slot_p);
        }
    }

    bool ASTLowerer::is_vec_type_start(int offset) const {
        return (peek(offset).text == "vec" || peek(offset).text == "vector") &&
               peek(offset + 1).kind == TOK_LT;
    }

    uint64_t ASTLowerer::get_ir_type_size_bytes(ir::IRType* t) {
        if (!t) return 8;
        uint64_t sz = t->size();
        return sz > 0 ? sz : 8;
    }

    std::string ASTLowerer::mangle_ir_type_key(ir::IRType* t, const std::string& elem_struct) {
        if (!t) return "any";
        if (t->is_pointer()) return "ptr_" + mangle_ir_type_key(t->element_type, elem_struct);
        if (t->is_struct()) return "st_" + (!elem_struct.empty() ? elem_struct : t->struct_name);
        if (t->is_floating_point()) return "f64";
        if (t->is_bool()) return "i1";
        if (t->is_i8()) return "i8";
        if (t->is_i16()) return "i16";
        if (t->is_i32()) return "i32";
        return "i64";
    }

    ir::IRType* ASTLowerer::get_or_create_vec_ir_type(ir::IRType* elem_t, const std::string& elem_struct, std::string& out_sname) {
        if (!elem_t || elem_t->is_void()) elem_t = ctx_.get_i64_type();
        out_sname = "__vec_" + mangle_ir_type_key(elem_t, elem_struct);
        auto it = vec_struct_meta_.find(out_sname);
        if (it != vec_struct_meta_.end()) {
            return it->second.vec_struct_type;
        }
        ir::IRType* data_ptr_t = ctx_.get_ptr_type(elem_t);
        ir::IRType* i64_t = ctx_.get_i64_type();
        ir::IRType* st = ctx_.get_struct_type(out_sname, { data_ptr_t, i64_t, i64_t }, { "data", "size", "capacity" });
        vec_struct_meta_[out_sname] = { st, elem_t, elem_struct };
        return st;
    }

    ir::IRType* ASTLowerer::parse_vec_ir_type(ir::IRType*& out_elem_t, std::string& out_elem_struct, std::string& out_vec_sname) {
        advance(); // vec / vector
        advance(); // <
        out_elem_struct.clear();
        if (is_vec_type_start()) {
            ir::IRType* sub_e = nullptr;
            std::string sub_es, sub_vs;
            out_elem_t = parse_vec_ir_type(sub_e, sub_es, sub_vs);
        } else {
            std::string tname = advance().text;
            while (match(TOK_COLON_COLON)) {
                if (peek().kind == TOK_IDENT) tname += "::" + advance().text;
            }
            if (oop_mgr_ && oop_mgr_->has_class(tname)) {
                out_elem_struct = tname;
                const auto* cd = oop_mgr_->get_class(tname);
                out_elem_t = (cd && cd->ir_struct_type) ? cd->ir_struct_type : ctx_.get_i64_type();
            } else {
                out_elem_t = parse_type_name(tname);
            }
        }
        while (match(TOK_STAR)) {
            out_elem_t = ctx_.get_ptr_type(out_elem_t);
            out_elem_struct.clear();
        }
        match(TOK_GT);
        return get_or_create_vec_ir_type(out_elem_t, out_elem_struct, out_vec_sname);
    }

    void ASTLowerer::emit_vec_elem_dtor(ir::ValueId elem_ptr, const std::string& elem_struct) {
        if (!oop_mgr_ || elem_struct.empty() || !elem_ptr) return;
        const auto* cd = oop_mgr_->get_class(elem_struct);
        if (!cd || !cd->has_destructor) return;
        ir::ValueId cast_p = coerce_type(elem_ptr, ctx_.get_ptr_type(cd->ir_struct_type));
        builder_.create_call(oop_mgr_->get_destructor_name(cd->name), ctx_.get_void_type(), { cast_p });
    }

    ir::ValueId ASTLowerer::emit_vec_size(ir::ValueId vec_ptr) {
        ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "vec_sz_ptr");
        return builder_.create_load(sz_p, "vec_sz");
    }

    ir::ValueId ASTLowerer::emit_vec_capacity(ir::ValueId vec_ptr) {
        ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "vec_cap_ptr");
        return builder_.create_load(cap_p, "vec_cap");
    }

    ir::ValueId ASTLowerer::emit_vec_empty(ir::ValueId vec_ptr) {
        ir::ValueId sz = emit_vec_size(vec_ptr);
        return builder_.create_icmp_eq(sz, builder_.create_const_i64(0), "vec_empty");
    }

    ir::ValueId ASTLowerer::emit_vec_data(ir::ValueId vec_ptr, ir::IRType* elem_t) {
        ir::ValueId dp = builder_.create_gep_struct(vec_ptr, 0, "vec_data_field");
        ir::ValueId raw = builder_.create_load(dp, "vec_data");
        return coerce_type(raw, ctx_.get_ptr_type(elem_t));
    }

    void ASTLowerer::emit_vec_reserve(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId new_cap) {
        ir::ValueId new_cap64 = coerce_type(new_cap, ctx_.get_i64_type());
        ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "vec_cap_field");
        ir::ValueId cur_cap = builder_.create_load(cap_p, "cur_cap");
        ir::ValueId need_grow = builder_.create_icmp(ir::ICmpCond::SGT, new_cap64, cur_cap, "need_grow");
        ir::BasicBlock* bb_grow = builder_.create_block("vec_res_grow");
        ir::BasicBlock* bb_done = builder_.create_block("vec_res_done");
        builder_.create_branch(need_grow, bb_grow, bb_done);

        builder_.set_insert_point(bb_grow);
        uint64_t elem_sz = get_ir_type_size_bytes(elem_t);
        ir::ValueId bytes = builder_.create_mul(new_cap64, builder_.create_const_i64(static_cast<int64_t>(elem_sz)), "vec_bytes");
        ir::ValueId data_f = builder_.create_gep_struct(vec_ptr, 0, "vec_data_f");
        ir::ValueId cur_data = coerce_type(builder_.create_load(data_f, "cur_data"), ctx_.get_ptr_type());
        ir::ValueId new_raw = builder_.create_call("grass_realloc", ctx_.get_ptr_type(), { cur_data, bytes });
        ir::ValueId new_typed = coerce_type(new_raw, ctx_.get_ptr_type(elem_t));
        builder_.create_store(new_typed, data_f);
        builder_.create_store(new_cap64, cap_p);
        builder_.create_jump(bb_done);

        builder_.set_insert_point(bb_done);
    }

    void ASTLowerer::emit_vec_push(ir::ValueId vec_ptr, ir::IRType* elem_t, ir::ValueId val) {
        ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "push_sz_f");
        ir::ValueId cap_p = builder_.create_gep_struct(vec_ptr, 2, "push_cap_f");
        ir::ValueId sz = builder_.create_load(sz_p, "push_sz");
        ir::ValueId cap = builder_.create_load(cap_p, "push_cap");
        ir::ValueId full = builder_.create_icmp(ir::ICmpCond::SGE, sz, cap, "vec_full");

        ir::BasicBlock* bb_grow = builder_.create_block("vec_push_grow");
        ir::BasicBlock* bb_zero = builder_.create_block("vec_push_init");
        ir::BasicBlock* bb_dbl = builder_.create_block("vec_push_dbl");
        ir::BasicBlock* bb_store = builder_.create_block("vec_push_store");
        builder_.create_branch(full, bb_grow, bb_store);

        builder_.set_insert_point(bb_grow);
        ir::ValueId is_zero = builder_.create_icmp_eq(cap, builder_.create_const_i64(0), "cap_is_0");
        builder_.create_branch(is_zero, bb_zero, bb_dbl);

        builder_.set_insert_point(bb_zero);
        emit_vec_reserve(vec_ptr, elem_t, builder_.create_const_i64(8));
        builder_.create_jump(bb_store);

        builder_.set_insert_point(bb_dbl);
        ir::ValueId cap2 = builder_.create_mul(cap, builder_.create_const_i64(2), "cap_2x");
        emit_vec_reserve(vec_ptr, elem_t, cap2);
        builder_.create_jump(bb_store);

        builder_.set_insert_point(bb_store);
        ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
        ir::ValueId cur_sz = builder_.create_load(sz_p, "push_cur_sz");
        ir::ValueId slot_ptr = builder_.create_gep(data_ptr, { cur_sz }, elem_t, "push_slot");
        ir::IRType* val_t = builder_.get_function()->has_value(val) ? builder_.get_function()->get_value(val).type : nullptr;
        if (elem_t && elem_t->is_struct() && val_t && val_t->is_pointer()) {
            emit_copy_struct(slot_ptr, val, elem_t);
        } else {
            ir::ValueId cval = coerce_type(val, elem_t);
            builder_.create_store(cval, slot_ptr);
        }
        ir::ValueId next_sz = builder_.create_add(cur_sz, builder_.create_const_i64(1), "next_sz");
        builder_.create_store(next_sz, sz_p);
    }

    void ASTLowerer::emit_vec_pop(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct) {
        ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "pop_sz_f");
        ir::ValueId sz = builder_.create_load(sz_p, "pop_sz");
        ir::ValueId non_empty = builder_.create_icmp(ir::ICmpCond::SGT, sz, builder_.create_const_i64(0), "pop_ne");
        ir::BasicBlock* bb_do = builder_.create_block("vec_pop_do");
        ir::BasicBlock* bb_done = builder_.create_block("vec_pop_done");
        builder_.create_branch(non_empty, bb_do, bb_done);

        builder_.set_insert_point(bb_do);
        ir::ValueId new_sz = builder_.create_sub(sz, builder_.create_const_i64(1), "new_sz");
        if (oop_mgr_ && !elem_struct.empty()) {
            const auto* cd = oop_mgr_->get_class(elem_struct);
            if (cd && cd->has_destructor) {
                ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
                ir::ValueId elem_p = builder_.create_gep(data_ptr, { new_sz }, elem_t, "pop_elem_p");
                emit_vec_elem_dtor(elem_p, elem_struct);
            }
        }
        builder_.create_store(new_sz, sz_p);
        builder_.create_jump(bb_done);

        builder_.set_insert_point(bb_done);
    }

    void ASTLowerer::emit_vec_clear(ir::ValueId vec_ptr, ir::IRType* elem_t, const std::string& elem_struct) {
        ir::ValueId sz_p = builder_.create_gep_struct(vec_ptr, 1, "clr_sz_f");
        if (oop_mgr_ && !elem_struct.empty()) {
            const auto* cd = oop_mgr_->get_class(elem_struct);
            if (cd && cd->has_destructor) {
                ir::ValueId sz = builder_.create_load(sz_p, "clr_sz");
                ir::ValueId idx_alloca = builder_.create_alloca(ctx_.get_i64_type(), 1, 0, "clr_i");
                builder_.create_store(builder_.create_sub(sz, builder_.create_const_i64(1)), idx_alloca);
                ir::BasicBlock* bb_cond = builder_.create_block("vec_clr_cond");
                ir::BasicBlock* bb_body = builder_.create_block("vec_clr_body");
                ir::BasicBlock* bb_end = builder_.create_block("vec_clr_end");
                builder_.create_jump(bb_cond);

                builder_.set_insert_point(bb_cond);
                ir::ValueId cur_i = builder_.create_load(idx_alloca, "clr_idx");
                ir::ValueId ge0 = builder_.create_icmp(ir::ICmpCond::SGE, cur_i, builder_.create_const_i64(0), "clr_ge0");
                builder_.create_branch(ge0, bb_body, bb_end);

                builder_.set_insert_point(bb_body);
                ir::ValueId data_ptr = emit_vec_data(vec_ptr, elem_t);
                ir::ValueId elem_p = builder_.create_gep(data_ptr, { cur_i }, elem_t, "clr_elem_p");
                emit_vec_elem_dtor(elem_p, elem_struct);
                builder_.create_store(builder_.create_sub(cur_i, builder_.create_const_i64(1)), idx_alloca);
                builder_.create_jump(bb_cond);

                builder_.set_insert_point(bb_end);
            }
        }
        builder_.create_store(builder_.create_const_i64(0), sz_p);
    }

    void ASTLowerer::emit_vec_dtor(ir::ValueId vec_ptr, ir::IRType* vec_st, ir::IRType* elem_t, const std::string& elem_struct) {
        if (!vec_ptr) return;
        emit_vec_clear(vec_ptr, elem_t, elem_struct);
        ir::ValueId data_f = builder_.create_gep_struct(vec_ptr, 0, "dtor_data_f");
        ir::ValueId raw_p = coerce_type(builder_.create_load(data_f, "dtor_data"), ctx_.get_ptr_type());
        ir::ValueId null_p = builder_.create_const_int(ctx_.get_ptr_type(), 0);
        ir::ValueId non_null = builder_.create_icmp_ne(raw_p, null_p);
        ir::BasicBlock* bb_free = builder_.create_block("vec_dtor_free");
        ir::BasicBlock* bb_done = builder_.create_block("vec_dtor_done");
        builder_.create_branch(non_null, bb_free, bb_done);

        builder_.set_insert_point(bb_free);
        builder_.create_call("grass_free", ctx_.get_void_type(), { raw_p });
        builder_.create_jump(bb_done);

        builder_.set_insert_point(bb_done);
        if (vec_st && vec_st->is_struct()) {
            emit_zero_struct(vec_ptr, vec_st);
        }
    }

    void ASTLowerer::emit_copy_struct(ir::ValueId dst_ptr, ir::ValueId src_ptr, ir::IRType* st_type) {
        if (!dst_ptr || !src_ptr || !st_type || !st_type->is_struct()) return;
        uint64_t total_sz = st_type->size();
        uint64_t qwords = (total_sz + 7) / 8;
        ir::IRType* i64_ptr_t = ctx_.get_ptr_type(ctx_.get_i64_type());
        ir::ValueId dst_p = builder_.create_bitcast(dst_ptr, i64_ptr_t, "copy_dst");
        ir::ValueId src_p = builder_.create_bitcast(src_ptr, i64_ptr_t, "copy_src");
        for (uint64_t i = 0; i < qwords; ++i) {
            ir::ValueId idx = builder_.create_const_i64(static_cast<int64_t>(i));
            ir::ValueId s_elem = (i == 0) ? src_p : builder_.create_gep(src_p, { idx }, ctx_.get_i64_type());
            ir::ValueId d_elem = (i == 0) ? dst_p : builder_.create_gep(dst_p, { idx }, ctx_.get_i64_type());
            ir::ValueId v = builder_.create_load(s_elem, "copy_qw");
            builder_.create_store(v, d_elem);
        }
    }

    std::string ASTLowerer::add_rodata_string(const std::string& str) {
        std::string sym_name = ".Lstr_" + std::to_string(string_counter_++);
        uint64_t offset = rodata_.size();
        rodata_.insert(rodata_.end(), str.begin(), str.end());
        rodata_.push_back('\0'); // null terminator
        rodata_symbols_[sym_name] = offset;
        return sym_name;
    }

    ir::ValueId ASTLowerer::coerce_type(ir::ValueId val, ir::IRType* target_type) {
        if (!val || !target_type) return val;
        if (!builder_.get_function()->has_value(val)) return val;
        ir::IRType* src_type = builder_.get_function()->get_value(val).type;
        if (!src_type || src_type->equals(target_type)) return val;

        if (target_type->is_bool()) {
            if (src_type->is_integer()) {
                ir::ValueId zero = builder_.create_const_int(src_type, 0);
                return builder_.create_icmp_ne(val, zero);
            }
            if (src_type->is_pointer()) {
                ir::ValueId null_ptr = builder_.create_const_int(src_type, 0);
                return builder_.create_icmp_ne(val, null_ptr);
            }
        }

        if (src_type->is_bool() && target_type->is_integer()) {
            return builder_.create_zext(val, target_type);
        }

        if (src_type->is_integer() && target_type->is_integer()) {
            uint32_t src_bits = src_type->bit_width();
            uint32_t tgt_bits = target_type->bit_width();
            if (src_bits > tgt_bits) {
                return builder_.create_trunc(val, target_type);
            } else if (src_bits < tgt_bits) {
                return builder_.create_sext(val, target_type);
            }
            return val;
        }

        if (src_type->is_pointer() && target_type->is_integer()) {
            return builder_.create_ptrtoint(val, target_type);
        }

        if (src_type->is_integer() && target_type->is_pointer()) {
            return builder_.create_inttoptr(val, target_type);
        }

        if (src_type->is_pointer() && target_type->is_pointer()) {
            return builder_.create_bitcast(val, target_type);
        }

        if (src_type->is_integer() && target_type->is_floating_point()) {
            return builder_.create_sitofp(val, target_type);
        }

        if (src_type->is_floating_point() && target_type->is_integer()) {
            return builder_.create_fptosi(val, target_type);
        }

        return val;
    }

    void ASTLowerer::match_types(ir::ValueId& lhs, ir::ValueId& rhs) {
        if (!lhs || !rhs) return;
        if (!builder_.get_function()->has_value(lhs) || !builder_.get_function()->has_value(rhs)) return;
        ir::IRType* t1 = builder_.get_function()->get_value(lhs).type;
        ir::IRType* t2 = builder_.get_function()->get_value(rhs).type;
        if (!t1 || !t2 || t1->equals(t2)) return;

        if (t1->is_floating_point() || t2->is_floating_point()) {
            ir::IRType* target = ctx_.get_f64_type();
            lhs = coerce_type(lhs, target);
            rhs = coerce_type(rhs, target);
        } else if (t1->is_integer() && t2->is_integer()) {
            uint32_t w1 = t1->bit_width();
            uint32_t w2 = t2->bit_width();
            if (w1 > w2) {
                rhs = coerce_type(rhs, t1);
            } else {
                lhs = coerce_type(lhs, t2);
            }
        } else if (t1->is_pointer() || t2->is_pointer()) {
            if (t1->is_pointer() && t2->is_pointer()) {
                rhs = coerce_type(rhs, t1);
            } else if (t1->is_pointer() && t2->is_integer()) {
                rhs = coerce_type(rhs, t1);
            } else if (t1->is_integer() && t2->is_pointer()) {
                lhs = coerce_type(lhs, t2);
            }
        }
    }

    ASTLowerer::ASTLowerer(const std::vector<GrassToken>& tokens,
               ir::IRModule& module,
               std::vector<uint8_t>& rodata,
               std::unordered_map<std::string, uint64_t>& rodata_symbols,
               const std::string& source_path,
               bool freestanding,
               INativeOOPDelegate* oop_del,
               INativeMemoryDelegate* mem_del,
               INativeStdlibDelegate* stdlib_del,
               native::GrassNativeOOP* oop_mgr,
               native::GrassNativeMemory* mem_mgr)
        : tokens_(tokens), module_(module), ctx_(module.context),
          rodata_(rodata), rodata_symbols_(rodata_symbols),
          source_path_(source_path), freestanding_(freestanding),
          oop_delegate_(oop_del), memory_delegate_(mem_del), stdlib_delegate_(stdlib_del),
          oop_mgr_(oop_mgr), mem_mgr_(mem_mgr) {}

} // namespace grass::codegen
