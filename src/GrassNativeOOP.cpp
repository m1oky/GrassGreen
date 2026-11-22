#include "GrassNativeOOP.h"

#include <stdexcept>
#include <algorithm>

namespace grass::native {

    const OOPField* OOPClassDef::find_field(const std::string& field_name) const {
        auto it = field_indices.find(field_name);
        if (it != field_indices.end() && it->second < fields.size()) {
            return &fields[it->second];
        }
        return nullptr;
    }

    const OOPMethod* OOPClassDef::find_method(const std::string& method_name) const {
        auto it = method_indices.find(method_name);
        if (it != method_indices.end() && it->second < methods.size()) {
            return &methods[it->second];
        }
        return nullptr;
    }

    int32_t OOPClassDef::get_field_index(const std::string& field_name) const {
        auto it = field_indices.find(field_name);
        if (it != field_indices.end()) {
            return static_cast<int32_t>(it->second);
        }
        return -1;
    }

    GrassNativeOOP::GrassNativeOOP(ir::IRModule& module, GrassNativeMemory* memory)
        : module_(module), memory_(memory ? memory : &default_memory_) {
    }

    std::string GrassNativeOOP::mangle_method_name(const std::string& class_name, const std::string& method_name) const {
        return class_name + "_" + method_name;
    }

    void GrassNativeOOP::begin_struct(const std::string& name) {
        is_building_ = true;
        current_class_ = OOPClassDef{};
        current_class_.name = name;
        current_class_.is_class = false;
        current_class_.has_vtable = false;
    }

    void GrassNativeOOP::begin_class(const std::string& name, const std::string& base_class) {
        is_building_ = true;
        current_class_ = OOPClassDef{};
        current_class_.name = name;
        current_class_.base_name = base_class;
        current_class_.is_class = true;

        if (!base_class.empty()) {
            auto b_it = classes_.find(base_class);
            if (b_it != classes_.end()) {
                const OOPClassDef& base = b_it->second;
                current_class_.has_vtable = base.has_vtable;
                current_class_.vptr_offset = base.vptr_offset;
                current_class_.vtable_slots = base.vtable_slots;
                current_class_.vtable_method_names = base.vtable_method_names;

                // Inherit fields from base class
                for (const auto& bf : base.fields) {
                    OOPField inherited_f = bf;
                    inherited_f.is_inherited = true;
                    current_class_.field_indices[inherited_f.name] = current_class_.fields.size();
                    current_class_.fields.push_back(inherited_f);
                }
            }
        }
    }

    void GrassNativeOOP::add_field(const std::string& field_name, ir::IRType* field_type) {
        if (!is_building_) return;
        OOPField f;
        f.name = field_name;
        f.type = field_type;
        f.alignment = memory_->get_type_alignment(field_type);
        f.size = memory_->get_type_size(field_type);
        f.is_inherited = false;
        current_class_.field_indices[field_name] = current_class_.fields.size();
        current_class_.fields.push_back(f);
    }

    void GrassNativeOOP::add_method(const std::string& method_name,
                                    ir::IRType* return_type,
                                    const std::vector<ir::IRType*>& param_types,
                                    const std::vector<std::string>& param_names,
                                    bool is_virtual,
                                    bool is_override) {
        if (!is_building_) return;
        OOPMethod m;
        m.name = method_name;
        m.mangled_name = mangle_method_name(current_class_.name, method_name);
        m.return_type = return_type ? return_type : module_.context.get_void_type();
        m.param_types = param_types;
        m.param_names = param_names;
        m.is_virtual = is_virtual || is_override;
        m.is_override = is_override;

        if (m.is_virtual) {
            current_class_.has_vtable = true;
            int32_t slot_idx = -1;
            for (size_t i = 0; i < current_class_.vtable_method_names.size(); ++i) {
                if (current_class_.vtable_method_names[i] == method_name) {
                    slot_idx = static_cast<int32_t>(i);
                    break;
                }
            }
            if (slot_idx >= 0) {
                m.vtable_index = slot_idx;
                current_class_.vtable_slots[slot_idx] = m.mangled_name;
            } else {
                m.vtable_index = static_cast<int32_t>(current_class_.vtable_slots.size());
                current_class_.vtable_slots.push_back(m.mangled_name);
                current_class_.vtable_method_names.push_back(method_name);
            }
        }

        current_class_.method_indices[method_name] = current_class_.methods.size();
        current_class_.methods.push_back(m);
    }

    void GrassNativeOOP::add_constructor(const std::vector<ir::IRType*>& param_types,
                                         const std::vector<std::string>& param_names) {
        if (!is_building_) return;
        OOPMethod m;
        m.name = "ctor";
        m.mangled_name = mangle_method_name(current_class_.name, "ctor");
        m.return_type = module_.context.get_void_type();
        m.param_types = param_types;
        m.param_names = param_names;
        m.is_constructor = true;
        current_class_.method_indices[m.name] = current_class_.methods.size();
        current_class_.methods.push_back(m);
    }

    void GrassNativeOOP::add_destructor() {
        if (!is_building_) return;
        OOPMethod m;
        m.name = "dtor";
        m.mangled_name = mangle_method_name(current_class_.name, "dtor");
        m.return_type = module_.context.get_void_type();
        m.is_destructor = true;
        current_class_.method_indices[m.name] = current_class_.methods.size();
        current_class_.methods.push_back(m);
    }

    const OOPClassDef& GrassNativeOOP::finalize_class() {
        if (!is_building_) {
            static OOPClassDef empty_def;
            return empty_def;
        }

        // If class has virtual methods and does not have __vptr yet, inject __vptr at index 0
        if (current_class_.has_vtable && current_class_.field_indices.find("__vptr") == current_class_.field_indices.end()) {
            OOPField vptr_field;
            vptr_field.name = "__vptr";
            vptr_field.type = module_.context.get_ptr_type();
            vptr_field.size = 8;
            vptr_field.alignment = 8;
            vptr_field.is_inherited = false;

            current_class_.fields.insert(current_class_.fields.begin(), vptr_field);
            current_class_.field_indices.clear();
            for (size_t i = 0; i < current_class_.fields.size(); ++i) {
                current_class_.field_indices[current_class_.fields[i].name] = i;
            }
        }

        // Calculate layout offsets taking alignment into account
        uint64_t cur_offset = 0;
        uint64_t max_align = 1;

        for (auto& f : current_class_.fields) {
            uint64_t a = f.alignment > 0 ? f.alignment : memory_->get_type_alignment(f.type);
            uint64_t sz = f.size > 0 ? f.size : memory_->get_type_size(f.type);
            if (a > max_align) max_align = a;

            cur_offset = (cur_offset + a - 1) & ~(a - 1);
            f.offset = cur_offset;
            f.size = sz;
            f.alignment = a;
            cur_offset += sz;
        }

        uint64_t total = (cur_offset + max_align - 1) & ~(max_align - 1);
        if (total == 0) total = max_align;
        current_class_.total_size = total;
        current_class_.alignment = max_align;

        // Build IR struct type
        std::vector<ir::IRType*> f_types;
        std::vector<std::string> f_names;
        for (const auto& f : current_class_.fields) {
            f_types.push_back(f.type);
            f_names.push_back(f.name);
        }

        ir::IRType* st = module_.context.get_struct_type(current_class_.name, f_types, f_names);
        st->cached_size = total;
        st->cached_align = max_align;
        for (size_t i = 0; i < current_class_.fields.size(); ++i) {
            if (i < st->struct_fields.size()) {
                st->struct_fields[i].offset = current_class_.fields[i].offset;
            }
        }
        current_class_.ir_struct_type = st;

        classes_[current_class_.name] = current_class_;
        is_building_ = false;
        return classes_[current_class_.name];
    }

    const OOPClassDef* GrassNativeOOP::get_class(const std::string& name) const {
        auto it = classes_.find(name);
        if (it != classes_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    bool GrassNativeOOP::has_class(const std::string& name) const {
        return classes_.find(name) != classes_.end();
    }

    ir::ValueId GrassNativeOOP::emit_field_gep(ir::IRBuilder& builder,
                                               ir::ValueId instance_ptr,
                                               const std::string& class_name,
                                               const std::string& field_name,
                                               const std::string& name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) {
            return ir::kInvalidValueId;
        }
        int32_t idx = cd->get_field_index(field_name);
        if (idx < 0) {
            return ir::kInvalidValueId;
        }

        if (builder.get_function()) {
            ir::IRType* inst_type = builder.get_function()->get_value(instance_ptr).type;
            ir::IRType* expected_ptr_type = module_.context.get_ptr_type(cd->ir_struct_type);
            if (inst_type != expected_ptr_type) {
                instance_ptr = builder.create_bitcast(instance_ptr, expected_ptr_type, name + "_inst_cast");
            }
        }
        return builder.create_gep_struct(instance_ptr, static_cast<uint32_t>(idx), name);
    }

    ir::ValueId GrassNativeOOP::emit_load_field(ir::IRBuilder& builder,
                                                ir::ValueId instance_ptr,
                                                const std::string& class_name,
                                                const std::string& field_name,
                                                const std::string& name) {
        ir::ValueId field_ptr = emit_field_gep(builder, instance_ptr, class_name, field_name, name + "_gep");
        return builder.create_load(field_ptr, name);
    }

    ir::IRInst* GrassNativeOOP::emit_store_field(ir::IRBuilder& builder,
                                                 ir::ValueId instance_ptr,
                                                 const std::string& class_name,
                                                 const std::string& field_name,
                                                 ir::ValueId value_to_store) {
        ir::ValueId field_ptr = emit_field_gep(builder, instance_ptr, class_name, field_name, "store_gep");
        return builder.create_store(value_to_store, field_ptr);
    }

    ir::IRFunction* GrassNativeOOP::create_method_function(const std::string& class_name,
                                                           const std::string& method_name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return nullptr;
        const OOPMethod* md = cd->find_method(method_name);
        if (!md) return nullptr;

        ir::IRFunction* fn = module_.create_function(md->mangled_name, md->return_type);
        ir::IRType* this_type = module_.context.get_ptr_type(cd->ir_struct_type);
        fn->add_argument("this", this_type);

        for (size_t i = 0; i < md->param_types.size(); ++i) {
            std::string pname = (i < md->param_names.size() && !md->param_names[i].empty())
                                ? md->param_names[i]
                                : ("arg_" + std::to_string(i));
            fn->add_argument(pname, md->param_types[i]);
        }
        return fn;
    }

    ir::ValueId GrassNativeOOP::emit_call_method(ir::IRBuilder& builder,
                                                 ir::ValueId instance_ptr,
                                                 const std::string& class_name,
                                                 const std::string& method_name,
                                                 const std::vector<ir::ValueId>& user_args,
                                                 const std::string& name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return ir::kInvalidValueId;
        const OOPMethod* md = cd->find_method(method_name);
        if (!md) return ir::kInvalidValueId;

        if (md->is_virtual) {
            return emit_call_virtual(builder, instance_ptr, class_name, method_name, user_args, name);
        }

        std::vector<ir::ValueId> args;
        args.reserve(user_args.size() + 1);
        args.push_back(instance_ptr);
        args.insert(args.end(), user_args.begin(), user_args.end());
        return builder.create_call(md->mangled_name, md->return_type, args, name);
    }

    ir::ValueId GrassNativeOOP::emit_call_virtual(ir::IRBuilder& builder,
                                                  ir::ValueId instance_ptr,
                                                  const std::string& class_name,
                                                  const std::string& method_name,
                                                  const std::vector<ir::ValueId>& user_args,
                                                  const std::string& name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return ir::kInvalidValueId;
        const OOPMethod* md = cd->find_method(method_name);
        if (!md || md->vtable_index < 0) return ir::kInvalidValueId;

        ir::ValueId vptr_slot = emit_field_gep(builder, instance_ptr, class_name, "__vptr", "vptr_slot");
        ir::ValueId vptr = builder.create_load(vptr_slot, "vptr_val");

        ir::ValueId slot_off = builder.create_const_i64(static_cast<int64_t>(md->vtable_index * 8));
        ir::ValueId slot_ptr = memory_->emit_ptr_add(builder, vptr, slot_off, "slot_ptr");
        ir::ValueId fn_ptr = builder.create_load(slot_ptr, "vfn_ptr");

        std::vector<ir::ValueId> args;
        args.reserve(user_args.size() + 1);
        args.push_back(instance_ptr);
        args.insert(args.end(), user_args.begin(), user_args.end());
        return builder.create_indirect_call(fn_ptr, md->return_type, args, name);
    }

    ir::ValueId GrassNativeOOP::emit_get_function_pointer(ir::IRBuilder& builder,
                                                          const std::string& fn_name,
                                                          const std::string& name) {
        ir::IRType* ptr_type = module_.context.get_ptr_type();
        return builder.create_call("__get_fn_ptr_" + fn_name, ptr_type, {}, name);
    }

    void GrassNativeOOP::emit_vtable_init(ir::IRBuilder& builder,
                                          ir::ValueId instance_ptr,
                                          const std::string& class_name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd || !cd->has_vtable || cd->vtable_slots.empty()) {
            return;
        }

        std::string init_fn_name = "__get_vtable_" + class_name;
        if (!module_.get_function(init_fn_name)) {
            ir::IRFunction* vt_fn = module_.create_function(init_fn_name, module_.context.get_ptr_type());
            ir::BasicBlock* entry = vt_fn->create_block("entry");
            ir::IRBuilder vt_builder(vt_fn);
            vt_builder.set_insert_point(entry);

            uint64_t num_slots = cd->vtable_slots.size();
            ir::ValueId vt_buf = memory_->emit_alloc_const(vt_builder, num_slots * 8, "vtable_buffer");

            for (size_t i = 0; i < num_slots; ++i) {
                const std::string& target_fn = cd->vtable_slots[i];
                ir::ValueId fn_ptr = emit_get_function_pointer(vt_builder, target_fn, "slot_fn_" + std::to_string(i));
                ir::ValueId off = vt_builder.create_const_i64(static_cast<int64_t>(i * 8));
                ir::ValueId slot = memory_->emit_ptr_add(vt_builder, vt_buf, off, "slot_ptr");
                vt_builder.create_store(fn_ptr, slot);
            }
            vt_builder.create_ret(vt_buf);
        }

        ir::ValueId vt_ptr = builder.create_call(init_fn_name, module_.context.get_ptr_type(), {}, "vt_ptr");
        ir::ValueId vptr_field = emit_field_gep(builder, instance_ptr, class_name, "__vptr", "vptr_field");
        builder.create_store(vt_ptr, vptr_field);
    }

    ir::ValueId GrassNativeOOP::emit_new_object(ir::IRBuilder& builder,
                                                const std::string& class_name,
                                                const std::vector<ir::ValueId>& ctor_args,
                                                const std::string& name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return ir::kInvalidValueId;

        ir::ValueId raw_obj = memory_->emit_alloc_const(builder, cd->total_size, name + "_raw");
        ir::IRType* class_ptr_type = module_.context.get_ptr_type(cd->ir_struct_type);
        ir::ValueId obj_ptr = builder.create_bitcast(raw_obj, class_ptr_type, name);

        if (cd->has_vtable) {
            emit_vtable_init(builder, obj_ptr, class_name);
        }

        const OOPMethod* ctor = cd->find_method("ctor");
        if (ctor) {
            std::vector<ir::ValueId> args;
            args.reserve(ctor_args.size() + 1);
            args.push_back(obj_ptr);
            args.insert(args.end(), ctor_args.begin(), ctor_args.end());
            builder.create_call(ctor->mangled_name, module_.context.get_void_type(), args);
        }

        return obj_ptr;
    }

    void GrassNativeOOP::emit_delete_object(ir::IRBuilder& builder,
                                            ir::ValueId instance_ptr,
                                            const std::string& class_name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return;

        const OOPMethod* dtor = cd->find_method("dtor");
        if (dtor) {
            builder.create_call(dtor->mangled_name, module_.context.get_void_type(), {instance_ptr});
        }
        memory_->emit_free(builder, instance_ptr);
    }

    ir::ValueId GrassNativeOOP::emit_stack_object(ir::IRBuilder& builder,
                                                  const std::string& class_name,
                                                  const std::vector<ir::ValueId>& ctor_args,
                                                  bool auto_raii,
                                                  const std::string& name) {
        const OOPClassDef* cd = get_class(class_name);
        if (!cd) return ir::kInvalidValueId;

        ir::ValueId obj_ptr = builder.create_alloca(cd->ir_struct_type, 1, static_cast<uint32_t>(cd->alignment), name);

        if (cd->has_vtable) {
            emit_vtable_init(builder, obj_ptr, class_name);
        }

        const OOPMethod* ctor = cd->find_method("ctor");
        if (ctor) {
            std::vector<ir::ValueId> args;
            args.reserve(ctor_args.size() + 1);
            args.push_back(obj_ptr);
            args.insert(args.end(), ctor_args.begin(), ctor_args.end());
            builder.create_call(ctor->mangled_name, module_.context.get_void_type(), args);
        }

        if (auto_raii && memory_) {
            const OOPMethod* dtor = cd->find_method("dtor");
            if (dtor) {
                memory_->register_raii_object(obj_ptr, dtor->mangled_name, class_name + "_raii_inst");
            }
        }

        return obj_ptr;
    }

    ir::IRType* GrassNativeOOP::get_dynamic_array_type() {
        if (!dynamic_array_type_) {
            dynamic_array_type_ = module_.context.get_struct_type("GrassDynamicArray", {
                module_.context.get_i64_type(),
                module_.context.get_i64_type(),
                module_.context.get_ptr_type()
            }, {"capacity", "length", "data"});
            dynamic_array_type_->cached_size = 24;
            dynamic_array_type_->cached_align = 8;
        }
        return dynamic_array_type_;
    }

    ir::ValueId GrassNativeOOP::emit_array_create(ir::IRBuilder& builder,
                                                  ir::IRType* element_type,
                                                  ir::ValueId initial_capacity,
                                                  const std::string& name) {
        ir::IRContext& ctx = module_.context;
        ir::ValueId raw_hdr = memory_->emit_alloc_const(builder, static_cast<uint64_t>(24), name + "_hdr_raw");
        ir::IRType* arr_ptr_type = ctx.get_ptr_type(get_dynamic_array_type());
        ir::ValueId hdr = builder.create_bitcast(raw_hdr, arr_ptr_type, name + "_hdr");

        uint64_t elem_sz = memory_->get_type_size(element_type);
        ir::ValueId elem_sz_val = builder.create_const_i64(static_cast<int64_t>(elem_sz));
        ir::ValueId buf_bytes = builder.create_mul(initial_capacity, elem_sz_val, name + "_buf_bytes");
        ir::ValueId raw_buf = memory_->emit_alloc(builder, buf_bytes, name + "_raw_buf");
        ir::IRType* elem_ptr_type = ctx.get_ptr_type(element_type);
        ir::ValueId data_buf = builder.create_bitcast(raw_buf, elem_ptr_type, name + "_buf");

        ir::ValueId cap_field = builder.create_gep_struct(hdr, 0, name + "_cap_ptr");
        builder.create_store(initial_capacity, cap_field);

        ir::ValueId zero = builder.create_const_i64(0);
        ir::ValueId len_field = builder.create_gep_struct(hdr, 1, name + "_len_ptr");
        builder.create_store(zero, len_field);

        ir::ValueId data_field = builder.create_gep_struct(hdr, 2, name + "_data_ptr");
        builder.create_store(data_buf, data_field);

        return hdr;
    }

    ir::ValueId GrassNativeOOP::emit_array_create_const(ir::IRBuilder& builder,
                                                        ir::IRType* element_type,
                                                        uint64_t initial_capacity,
                                                        const std::string& name) {
        ir::ValueId cap_val = builder.create_const_i64(static_cast<int64_t>(initial_capacity));
        return emit_array_create(builder, element_type, cap_val, name);
    }

    ir::ValueId GrassNativeOOP::emit_array_len(ir::IRBuilder& builder, ir::ValueId arr_ptr, const std::string& name) {
        if (builder.get_function()) {
            ir::IRType* ptype = builder.get_function()->get_value(arr_ptr).type;
            ir::IRType* exp_ptr = module_.context.get_ptr_type(get_dynamic_array_type());
            if (ptype != exp_ptr) {
                arr_ptr = builder.create_bitcast(arr_ptr, exp_ptr, "arr_len_cast");
            }
        }
        ir::ValueId len_field = builder.create_gep_struct(arr_ptr, 1, name + "_gep");
        return builder.create_load(len_field, name);
    }

    ir::ValueId GrassNativeOOP::emit_array_capacity(ir::IRBuilder& builder, ir::ValueId arr_ptr, const std::string& name) {
        if (builder.get_function()) {
            ir::IRType* ptype = builder.get_function()->get_value(arr_ptr).type;
            ir::IRType* exp_ptr = module_.context.get_ptr_type(get_dynamic_array_type());
            if (ptype != exp_ptr) {
                arr_ptr = builder.create_bitcast(arr_ptr, exp_ptr, "arr_cap_cast");
            }
        }
        ir::ValueId cap_field = builder.create_gep_struct(arr_ptr, 0, name + "_gep");
        return builder.create_load(cap_field, name);
    }

    ir::ValueId GrassNativeOOP::emit_array_element_ptr(ir::IRBuilder& builder,
                                                       ir::ValueId arr_ptr,
                                                       ir::ValueId index_val,
                                                       ir::IRType* elem_type,
                                                       const std::string& name) {
        if (builder.get_function()) {
            ir::IRType* ptype = builder.get_function()->get_value(arr_ptr).type;
            ir::IRType* exp_ptr = module_.context.get_ptr_type(get_dynamic_array_type());
            if (ptype != exp_ptr) {
                arr_ptr = builder.create_bitcast(arr_ptr, exp_ptr, "arr_elem_cast");
            }
        }
        ir::ValueId data_field = builder.create_gep_struct(arr_ptr, 2, name + "_data_field");
        ir::ValueId data_buf = builder.create_load(data_field, name + "_data_buf");

        uint64_t elem_sz = memory_->get_type_size(elem_type);
        ir::ValueId elem_sz_val = builder.create_const_i64(static_cast<int64_t>(elem_sz));
        ir::ValueId byte_off = builder.create_mul(index_val, elem_sz_val, name + "_byte_off");
        ir::ValueId raw_slot = memory_->emit_ptr_add(builder, data_buf, byte_off, name + "_raw_slot");
        ir::IRType* elem_ptr_type = module_.context.get_ptr_type(elem_type);
        return builder.create_bitcast(raw_slot, elem_ptr_type, name);
    }

    ir::ValueId GrassNativeOOP::emit_array_get(ir::IRBuilder& builder,
                                               ir::ValueId arr_ptr,
                                               ir::ValueId index_val,
                                               ir::IRType* elem_type,
                                               const std::string& name) {
        ir::ValueId elem_ptr = emit_array_element_ptr(builder, arr_ptr, index_val, elem_type, name + "_ptr");
        return builder.create_load(elem_ptr, name);
    }

    void GrassNativeOOP::emit_array_set(ir::IRBuilder& builder,
                                        ir::ValueId arr_ptr,
                                        ir::ValueId index_val,
                                        ir::ValueId value_to_store,
                                        ir::IRType* elem_type) {
        ir::ValueId elem_ptr = emit_array_element_ptr(builder, arr_ptr, index_val, elem_type, "set_elem_ptr");
        builder.create_store(value_to_store, elem_ptr);
    }

    void GrassNativeOOP::emit_array_push(ir::IRBuilder& builder,
                                         ir::ValueId arr_ptr,
                                         ir::ValueId value_to_push,
                                         ir::IRType* elem_type) {
        ir::IRFunction* fn = builder.get_function();
        if (!fn) return;

        if (builder.get_function()) {
            ir::IRType* ptype = builder.get_function()->get_value(arr_ptr).type;
            ir::IRType* exp_ptr = module_.context.get_ptr_type(get_dynamic_array_type());
            if (ptype != exp_ptr) {
                arr_ptr = builder.create_bitcast(arr_ptr, exp_ptr, "arr_push_cast");
            }
        }

        ir::BasicBlock* grow_bb = fn->create_block("arr_grow");
        ir::BasicBlock* store_bb = fn->create_block("arr_store");

        ir::ValueId cap = emit_array_capacity(builder, arr_ptr, "push_cap");
        ir::ValueId len = emit_array_len(builder, arr_ptr, "push_len");

        ir::ValueId need_grow = builder.create_icmp_sge(len, cap, "need_grow");
        builder.create_branch(need_grow, grow_bb, store_bb);

        // In grow_bb:
        builder.set_insert_point(grow_bb);
        ir::ValueId two = builder.create_const_i64(2);
        ir::ValueId doubled = builder.create_mul(cap, two, "doubled");
        ir::ValueId eight = builder.create_const_i64(8);
        ir::ValueId new_cap = builder.create_add(doubled, eight, "new_cap");

        uint64_t elem_sz = memory_->get_type_size(elem_type);
        ir::ValueId elem_sz_val = builder.create_const_i64(static_cast<int64_t>(elem_sz));
        ir::ValueId new_bytes = builder.create_mul(new_cap, elem_sz_val, "new_bytes");

        ir::ValueId data_field = builder.create_gep_struct(arr_ptr, 2, "push_data_field");
        ir::ValueId old_data = builder.create_load(data_field, "old_data");
        ir::ValueId raw_new_data = memory_->emit_realloc(builder, old_data, new_bytes, "realloc_data");
        ir::IRType* elem_ptr_type = module_.context.get_ptr_type(elem_type);
        ir::ValueId new_data = builder.create_bitcast(raw_new_data, elem_ptr_type, "typed_realloc_data");

        ir::ValueId cap_field = builder.create_gep_struct(arr_ptr, 0, "push_cap_field");
        builder.create_store(new_cap, cap_field);
        builder.create_store(new_data, data_field);
        builder.create_jump(store_bb);

        // In store_bb:
        builder.set_insert_point(store_bb);
        ir::ValueId cur_len = emit_array_len(builder, arr_ptr, "cur_len");
        ir::ValueId cur_data = builder.create_load(builder.create_gep_struct(arr_ptr, 2), "cur_data");
        ir::ValueId off = builder.create_mul(cur_len, elem_sz_val, "slot_off");
        ir::ValueId raw_slot_ptr = memory_->emit_ptr_add(builder, cur_data, off, "slot_ptr");
        ir::ValueId typed_slot_ptr = builder.create_bitcast(raw_slot_ptr, elem_ptr_type, "typed_slot_ptr");
        builder.create_store(value_to_push, typed_slot_ptr);

        ir::ValueId one = builder.create_const_i64(1);
        ir::ValueId next_len = builder.create_add(cur_len, one, "next_len");
        builder.create_store(next_len, builder.create_gep_struct(arr_ptr, 1));
    }

    ir::ValueId GrassNativeOOP::emit_array_pop(ir::IRBuilder& builder,
                                               ir::ValueId arr_ptr,
                                               ir::IRType* elem_type,
                                               const std::string& name) {
        ir::ValueId cur_len = emit_array_len(builder, arr_ptr, "pop_len");
        ir::ValueId one = builder.create_const_i64(1);
        ir::ValueId last_idx = builder.create_sub(cur_len, one, "last_idx");
        builder.create_store(last_idx, builder.create_gep_struct(arr_ptr, 1));
        return emit_array_get(builder, arr_ptr, last_idx, elem_type, name);
    }

    void GrassNativeOOP::emit_array_free(ir::IRBuilder& builder, ir::ValueId arr_ptr) {
        if (builder.get_function()) {
            ir::IRType* ptype = builder.get_function()->get_value(arr_ptr).type;
            ir::IRType* exp_ptr = module_.context.get_ptr_type(get_dynamic_array_type());
            if (ptype != exp_ptr) {
                arr_ptr = builder.create_bitcast(arr_ptr, exp_ptr, "arr_free_cast");
            }
        }
        ir::ValueId data_field = builder.create_gep_struct(arr_ptr, 2, "free_data_field");
        ir::ValueId data_buf = builder.create_load(data_field, "free_data_buf");
        memory_->emit_free(builder, data_buf);
        memory_->emit_free(builder, arr_ptr);
    }

    ir::IRType* GrassNativeOOP::get_string_type() {
        if (!string_type_) {
            string_type_ = module_.context.get_struct_type("GrassString", {
                module_.context.get_i64_type(),
                module_.context.get_ptr_type()
            }, {"length", "data"});
            string_type_->cached_size = 16;
            string_type_->cached_align = 8;
        }
        return string_type_;
    }

    ir::ValueId GrassNativeOOP::emit_string_literal(ir::IRBuilder& builder,
                                                    const std::string& text,
                                                    const std::string& name) {
        ir::IRContext& ctx = module_.context;
        ir::ValueId raw_obj = memory_->emit_alloc_const(builder, static_cast<uint64_t>(16), name + "_obj_raw");
        ir::IRType* str_ptr_type = ctx.get_ptr_type(get_string_type());
        ir::ValueId str_obj = builder.create_bitcast(raw_obj, str_ptr_type, name + "_obj");

        uint64_t byte_count = text.size() + 1;
        ir::ValueId raw_buf = memory_->emit_alloc_const(builder, byte_count, name + "_buf_raw");
        ir::IRType* i8_ptr_type = ctx.get_ptr_type(ctx.get_i8_type());
        ir::ValueId buf = builder.create_bitcast(raw_buf, i8_ptr_type, name + "_buf");

        for (size_t i = 0; i < text.size(); ++i) {
            ir::ValueId char_val = builder.create_const_i8(static_cast<int8_t>(text[i]));
            ir::ValueId off = builder.create_const_i64(static_cast<int64_t>(i));
            ir::ValueId raw_slot = memory_->emit_ptr_add(builder, buf, off, "char_slot");
            ir::ValueId slot = builder.create_bitcast(raw_slot, i8_ptr_type, "char_slot_cast");
            builder.create_store(char_val, slot);
        }

        ir::ValueId null_val = builder.create_const_i8(0);
        ir::ValueId null_off = builder.create_const_i64(static_cast<int64_t>(text.size()));
        ir::ValueId null_raw_slot = memory_->emit_ptr_add(builder, buf, null_off, "null_slot");
        ir::ValueId null_slot = builder.create_bitcast(null_raw_slot, i8_ptr_type, "null_slot_cast");
        builder.create_store(null_val, null_slot);

        ir::ValueId len_val = builder.create_const_i64(static_cast<int64_t>(text.size()));
        builder.create_store(len_val, builder.create_gep_struct(str_obj, 0));
        builder.create_store(buf, builder.create_gep_struct(str_obj, 1));
        return str_obj;
    }

    ir::ValueId GrassNativeOOP::emit_string_concat(ir::IRBuilder& builder,
                                                   ir::ValueId str1_ptr,
                                                   ir::ValueId str2_ptr,
                                                   const std::string& name) {
        ir::IRContext& ctx = module_.context;
        ir::IRType* str_ptr_type = ctx.get_ptr_type(get_string_type());
        if (builder.get_function()) {
            if (builder.get_function()->get_value(str1_ptr).type != str_ptr_type) {
                str1_ptr = builder.create_bitcast(str1_ptr, str_ptr_type, "s1_cast");
            }
            if (builder.get_function()->get_value(str2_ptr).type != str_ptr_type) {
                str2_ptr = builder.create_bitcast(str2_ptr, str_ptr_type, "s2_cast");
            }
        }
        ir::ValueId len1 = emit_string_len(builder, str1_ptr, "len1");
        ir::ValueId len2 = emit_string_len(builder, str2_ptr, "len2");
        ir::ValueId data1 = builder.create_load(builder.create_gep_struct(str1_ptr, 1), "data1");
        ir::ValueId data2 = builder.create_load(builder.create_gep_struct(str2_ptr, 1), "data2");

        ir::ValueId total_len = builder.create_add(len1, len2, "tot_len");
        ir::ValueId one = builder.create_const_i64(1);
        ir::ValueId buf_sz = builder.create_add(total_len, one, "buf_sz");

        ir::ValueId raw_new_buf = memory_->emit_alloc(builder, buf_sz, name + "_buf");
        ir::IRType* i8_ptr_type = ctx.get_ptr_type(ctx.get_i8_type());
        ir::ValueId new_buf = builder.create_bitcast(raw_new_buf, i8_ptr_type, name + "_buf_cast");

        memory_->emit_memcpy(builder, new_buf, data1, len1);

        ir::ValueId dest2 = memory_->emit_ptr_add(builder, new_buf, len1, "dest2");
        memory_->emit_memcpy(builder, dest2, data2, len2);

        ir::ValueId null_slot = memory_->emit_ptr_add(builder, new_buf, total_len, "null_slot");
        ir::ValueId null_slot_cast = builder.create_bitcast(null_slot, i8_ptr_type, "null_slot_cast");
        ir::ValueId zero_b = builder.create_const_i8(0);
        builder.create_store(zero_b, null_slot_cast);

        ir::ValueId raw_res_obj = memory_->emit_alloc_const(builder, static_cast<uint64_t>(16), name + "_obj_raw");
        ir::ValueId res_obj = builder.create_bitcast(raw_res_obj, str_ptr_type, name + "_obj");
        builder.create_store(total_len, builder.create_gep_struct(res_obj, 0));
        builder.create_store(new_buf, builder.create_gep_struct(res_obj, 1));
        return res_obj;
    }

    ir::ValueId GrassNativeOOP::emit_string_len(ir::IRBuilder& builder, ir::ValueId str_ptr, const std::string& name) {
        ir::IRContext& ctx = module_.context;
        ir::IRType* str_ptr_type = ctx.get_ptr_type(get_string_type());
        if (builder.get_function() && builder.get_function()->get_value(str_ptr).type != str_ptr_type) {
            str_ptr = builder.create_bitcast(str_ptr, str_ptr_type, "str_len_cast");
        }
        ir::ValueId len_field = builder.create_gep_struct(str_ptr, 0, name + "_gep");
        return builder.create_load(len_field, name);
    }

    ir::ValueId GrassNativeOOP::emit_string_char_at(ir::IRBuilder& builder,
                                                    ir::ValueId str_ptr,
                                                    ir::ValueId index_val,
                                                    const std::string& name) {
        ir::IRContext& ctx = module_.context;
        ir::IRType* str_ptr_type = ctx.get_ptr_type(get_string_type());
        if (builder.get_function() && builder.get_function()->get_value(str_ptr).type != str_ptr_type) {
            str_ptr = builder.create_bitcast(str_ptr, str_ptr_type, "str_cast");
        }
        ir::ValueId data_field = builder.create_gep_struct(str_ptr, 1, name + "_data_field");
        ir::ValueId data_buf = builder.create_load(data_field, name + "_data_buf");
        ir::ValueId char_ptr = memory_->emit_ptr_add(builder, data_buf, index_val, name + "_char_ptr");
        ir::IRType* i8_ptr_type = ctx.get_ptr_type(ctx.get_i8_type());
        ir::ValueId typed_char_ptr = builder.create_bitcast(char_ptr, i8_ptr_type, name + "_typed_char_ptr");
        return builder.create_load(typed_char_ptr, name);
    }

    void GrassNativeOOP::emit_string_free(ir::IRBuilder& builder, ir::ValueId str_ptr) {
        ir::IRContext& ctx = module_.context;
        ir::IRType* str_ptr_type = ctx.get_ptr_type(get_string_type());
        if (builder.get_function() && builder.get_function()->get_value(str_ptr).type != str_ptr_type) {
            str_ptr = builder.create_bitcast(str_ptr, str_ptr_type, "str_free_cast");
        }
        ir::ValueId data_field = builder.create_gep_struct(str_ptr, 1, "free_str_data_field");
        ir::ValueId data_buf = builder.create_load(data_field, "free_str_data_buf");
        memory_->emit_free(builder, data_buf);
        memory_->emit_free(builder, str_ptr);
    }

    void GrassNativeOOP::register_module(const ModuleExport& mod) {
        registered_modules_[mod.module_name] = mod;
    }

    bool GrassNativeOOP::import_module(const std::string& module_name, std::string& error_out) {
        if (imported_modules_.count(module_name) > 0) {
            return true;
        }
        auto it = registered_modules_.find(module_name);
        if (it == registered_modules_.end()) {
            error_out = "Module not found: " + module_name;
            return false;
        }
        const ModuleExport& mod = it->second;

        // Process module dependencies recursively
        for (const auto& dep : mod.dependencies) {
            if (!import_module(dep, error_out)) {
                return false;
            }
        }

        // Register module classes
        for (const auto& kv : mod.classes) {
            classes_[kv.first] = kv.second;
        }

        // Register function prototypes in IRModule
        for (const auto& fn_export : mod.exported_functions) {
            if (!module_.get_function(fn_export.name)) {
                ir::IRFunction* fn = module_.create_function(fn_export.name, fn_export.return_type);
                for (size_t i = 0; i < fn_export.param_types.size(); ++i) {
                    std::string pname = (i < fn_export.param_names.size() && !fn_export.param_names[i].empty())
                                        ? fn_export.param_names[i]
                                        : ("arg_" + std::to_string(i));
                    fn->add_argument(pname, fn_export.param_types[i]);
                }
            }
        }

        imported_modules_.insert(module_name);
        return true;
    }

    bool GrassNativeOOP::merge_module(ir::IRModule& other_module, const std::string& prefix, std::string& error_out) {
        (void)error_out;
        for (auto& other_fn : other_module.functions) {
            std::string target_name = prefix.empty() ? other_fn->name : (prefix + "_" + other_fn->name);
            ir::IRFunction* existing = module_.get_function(target_name);
            if (!existing) {
                existing = module_.create_function(target_name, other_fn->return_type);
                for (const auto& arg : other_fn->arguments) {
                    existing->add_argument(arg.name, arg.type);
                }
            }

            if (existing->blocks.empty() && !other_fn->blocks.empty()) {
                existing->arguments = std::move(other_fn->arguments);
                existing->values = std::move(other_fn->values);
                existing->next_value_id = other_fn->next_value_id;
                for (auto& blk : other_fn->blocks) {
                    blk->parent = existing;
                    for (auto& inst : blk->instructions) {
                        inst->parent = blk.get();
                    }
                    existing->blocks.push_back(std::move(blk));
                }
            }
        }
        return true;
    }

} // namespace grass::native
