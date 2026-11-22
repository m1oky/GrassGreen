#include "GrassNativeMemory.h"

#include <stdexcept>
#include <algorithm>

namespace grass::native {

    GrassNativeMemory::GrassNativeMemory(const MemoryRuntimeConfig& config)
        : config_(config) {
    }

    ir::ValueId GrassNativeMemory::emit_alloc(ir::IRBuilder& builder, ir::ValueId size_bytes, const std::string& name) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return ir::kInvalidValueId;
        }
        ir::IRContext& ctx = builder.get_function()->parent->context;
        ir::IRType* ptr_type = ctx.get_ptr_type();
        return builder.create_call(config_.alloc_fn, ptr_type, {size_bytes}, name);
    }

    ir::ValueId GrassNativeMemory::emit_alloc_const(ir::IRBuilder& builder, uint64_t size_bytes, const std::string& name) {
        ir::ValueId size_val = builder.create_const_i64(static_cast<int64_t>(size_bytes));
        return emit_alloc(builder, size_val, name);
    }

    void GrassNativeMemory::emit_free(ir::IRBuilder& builder, ir::ValueId ptr) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return;
        }
        ir::IRContext& ctx = builder.get_function()->parent->context;
        ir::IRType* void_type = ctx.get_void_type();
        builder.create_call(config_.free_fn, void_type, {ptr});
    }

    ir::ValueId GrassNativeMemory::emit_realloc(ir::IRBuilder& builder, ir::ValueId ptr, ir::ValueId new_size_bytes, const std::string& name) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return ir::kInvalidValueId;
        }
        ir::IRContext& ctx = builder.get_function()->parent->context;
        ir::IRType* ptr_type = ctx.get_ptr_type();
        return builder.create_call(config_.realloc_fn, ptr_type, {ptr, new_size_bytes}, name);
    }

    ir::ValueId GrassNativeMemory::emit_realloc_const(ir::IRBuilder& builder, ir::ValueId ptr, uint64_t new_size_bytes, const std::string& name) {
        ir::ValueId size_val = builder.create_const_i64(static_cast<int64_t>(new_size_bytes));
        return emit_realloc(builder, ptr, size_val, name);
    }

    uint64_t GrassNativeMemory::get_type_size(ir::IRType* type) const {
        if (!type) return 0;
        if (type->cached_size > 0) return type->cached_size;

        switch (type->kind) {
            case ir::TypeKind::Void: return 0;
            case ir::TypeKind::I1:
            case ir::TypeKind::I8: return 1;
            case ir::TypeKind::I16: return 2;
            case ir::TypeKind::I32:
            case ir::TypeKind::F32: return 4;
            case ir::TypeKind::I64:
            case ir::TypeKind::F64:
            case ir::TypeKind::Ptr: return 8;
            case ir::TypeKind::Array:
                return type->array_len * get_type_size(type->element_type);
            case ir::TypeKind::Struct: {
                if (type->struct_fields.empty()) return 1;
                uint64_t cur_offset = 0;
                uint64_t max_align = 1;
                for (const auto& f : type->struct_fields) {
                    uint64_t f_align = get_type_alignment(f.type);
                    uint64_t f_size = get_type_size(f.type);
                    if (f_align > max_align) max_align = f_align;
                    cur_offset = (cur_offset + f_align - 1) & ~(f_align - 1);
                    cur_offset += f_size;
                }
                uint64_t total = (cur_offset + max_align - 1) & ~(max_align - 1);
                return total == 0 ? 1 : total;
            }
        }
        return 8;
    }

    uint64_t GrassNativeMemory::get_type_alignment(ir::IRType* type) const {
        if (!type) return 1;
        if (type->cached_align > 0) return type->cached_align;

        switch (type->kind) {
            case ir::TypeKind::Void: return 1;
            case ir::TypeKind::I1:
            case ir::TypeKind::I8: return 1;
            case ir::TypeKind::I16: return 2;
            case ir::TypeKind::I32:
            case ir::TypeKind::F32: return 4;
            case ir::TypeKind::I64:
            case ir::TypeKind::F64:
            case ir::TypeKind::Ptr: return 8;
            case ir::TypeKind::Array:
                return get_type_alignment(type->element_type);
            case ir::TypeKind::Struct: {
                uint64_t max_align = 1;
                for (const auto& f : type->struct_fields) {
                    uint64_t a = get_type_alignment(f.type);
                    if (a > max_align) max_align = a;
                }
                return max_align;
            }
        }
        return 1;
    }

    ir::ValueId GrassNativeMemory::emit_sizeof(ir::IRBuilder& builder, ir::IRType* type) {
        uint64_t sz = get_type_size(type);
        return builder.create_const_i64(static_cast<int64_t>(sz));
    }

    ir::ValueId GrassNativeMemory::emit_ptr_add(ir::IRBuilder& builder, ir::ValueId ptr, ir::ValueId byte_offset, const std::string& name) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return ir::kInvalidValueId;
        }
        ir::IRContext& ctx = builder.get_function()->parent->context;
        ir::IRType* i64_type = ctx.get_i64_type();
        ir::IRType* ptr_type = ctx.get_ptr_type();

        ir::ValueId int_addr = builder.create_ptrtoint(ptr, i64_type, name + "_as_int");
        ir::ValueId new_int_addr = builder.create_add(int_addr, byte_offset, name + "_sum");
        return builder.create_inttoptr(new_int_addr, ptr_type, name);
    }

    ir::ValueId GrassNativeMemory::emit_ptr_add_const(ir::IRBuilder& builder, ir::ValueId ptr, int64_t byte_offset, const std::string& name) {
        ir::ValueId off_val = builder.create_const_i64(byte_offset);
        return emit_ptr_add(builder, ptr, off_val, name);
    }

    ir::ValueId GrassNativeMemory::emit_ptr_diff(ir::IRBuilder& builder, ir::ValueId ptr1, ir::ValueId ptr2, const std::string& name) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return ir::kInvalidValueId;
        }
        ir::IRContext& ctx = builder.get_function()->parent->context;
        ir::IRType* i64_type = ctx.get_i64_type();

        ir::ValueId addr1 = builder.create_ptrtoint(ptr1, i64_type, name + "_addr1");
        ir::ValueId addr2 = builder.create_ptrtoint(ptr2, i64_type, name + "_addr2");
        return builder.create_sub(addr1, addr2, name);
    }

    ir::ValueId GrassNativeMemory::emit_read_memory(ir::IRBuilder& builder, ir::ValueId address_val, ir::IRType* val_type, const std::string& name) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return ir::kInvalidValueId;
        }
        ir::IRType* addr_type = builder.get_function()->get_value(address_val).type;
        ir::ValueId ptr_val = address_val;
        if (!addr_type || !addr_type->is_pointer()) {
            ir::IRContext& ctx = builder.get_function()->parent->context;
            ir::IRType* ptr_type = ctx.get_ptr_type(val_type);
            ptr_val = builder.create_inttoptr(address_val, ptr_type, name + "_ptr");
        }
        return builder.create_load(ptr_val, name);
    }

    void GrassNativeMemory::emit_write_memory(ir::IRBuilder& builder, ir::ValueId address_val, ir::ValueId value_to_write) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return;
        }
        ir::IRType* addr_type = builder.get_function()->get_value(address_val).type;
        ir::ValueId ptr_val = address_val;
        if (!addr_type || !addr_type->is_pointer()) {
            ir::IRContext& ctx = builder.get_function()->parent->context;
            ir::IRType* val_type = builder.get_function()->get_value(value_to_write).type;
            ir::IRType* ptr_type = ctx.get_ptr_type(val_type);
            ptr_val = builder.create_inttoptr(address_val, ptr_type, "write_ptr");
        }
        builder.create_store(value_to_write, ptr_val);
    }

    ir::ValueId GrassNativeMemory::emit_read_phys(ir::IRBuilder& builder, uint64_t phys_addr, ir::IRType* val_type, const std::string& name) {
        ir::ValueId addr_val = builder.create_const_i64(static_cast<int64_t>(phys_addr));
        return emit_read_memory(builder, addr_val, val_type, name);
    }

    void GrassNativeMemory::emit_write_phys(ir::IRBuilder& builder, uint64_t phys_addr, ir::ValueId value_to_write) {
        ir::ValueId addr_val = builder.create_const_i64(static_cast<int64_t>(phys_addr));
        emit_write_memory(builder, addr_val, value_to_write);
    }

    void GrassNativeMemory::emit_memcpy(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId src_ptr, ir::ValueId size_bytes) {
        builder.create_memcpy(dest_ptr, src_ptr, size_bytes);
    }

    void GrassNativeMemory::emit_memcpy_const(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId src_ptr, uint64_t size_bytes) {
        ir::ValueId sz = builder.create_const_i64(static_cast<int64_t>(size_bytes));
        emit_memcpy(builder, dest_ptr, src_ptr, sz);
    }

    void GrassNativeMemory::emit_memset(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId byte_val, ir::ValueId size_bytes) {
        builder.create_memset(dest_ptr, byte_val, size_bytes);
    }

    void GrassNativeMemory::emit_memset_const(ir::IRBuilder& builder, ir::ValueId dest_ptr, uint8_t byte_val, uint64_t size_bytes) {
        ir::ValueId b = builder.create_const_i8(static_cast<int8_t>(byte_val));
        ir::ValueId sz = builder.create_const_i64(static_cast<int64_t>(size_bytes));
        emit_memset(builder, dest_ptr, b, sz);
    }

    void GrassNativeMemory::enter_scope(const std::string& scope_name) {
        scope_stack_.push_back(ScopeFrame{scope_name, {}});
    }

    void GrassNativeMemory::exit_scope(ir::IRBuilder& builder) {
        if (scope_stack_.empty()) {
            return;
        }
        ScopeFrame frame = std::move(scope_stack_.back());
        scope_stack_.pop_back();

        // Destruct/free in reverse order (LIFO)
        for (auto it = frame.actions.rbegin(); it != frame.actions.rend(); ++it) {
            execute_cleanup_action(builder, *it);
        }
    }

    void GrassNativeMemory::register_raii_object(ir::ValueId obj_ptr, const std::string& dtor_fn_name, const std::string& debug_name) {
        if (scope_stack_.empty()) {
            enter_scope("root_scope");
        }
        CleanupAction act;
        act.kind = CleanupAction::Kind::CustomDestructor;
        act.resource_ptr = obj_ptr;
        act.dtor_function = dtor_fn_name;
        act.debug_name = debug_name;
        scope_stack_.back().actions.push_back(act);
    }

    void GrassNativeMemory::register_dynamic_buffer(ir::ValueId buf_ptr, const std::string& debug_name) {
        if (scope_stack_.empty()) {
            enter_scope("root_scope");
        }
        CleanupAction act;
        act.kind = CleanupAction::Kind::FreeDynamicBuffer;
        act.resource_ptr = buf_ptr;
        act.debug_name = debug_name;
        scope_stack_.back().actions.push_back(act);
    }

    void GrassNativeMemory::register_custom_cleanup(std::function<void(ir::IRBuilder&)> cleanup_fn, const std::string& debug_name) {
        if (scope_stack_.empty()) {
            enter_scope("root_scope");
        }
        CleanupAction act;
        act.kind = CleanupAction::Kind::CustomCallback;
        act.custom_callback = cleanup_fn;
        act.debug_name = debug_name;
        scope_stack_.back().actions.push_back(act);
    }

    ir::IRInst* GrassNativeMemory::emit_return(ir::IRBuilder& builder, std::optional<ir::ValueId> ret_val) {
        emit_all_cleanups(builder);
        if (ret_val.has_value()) {
            return builder.create_ret(*ret_val);
        } else {
            return builder.create_ret_void();
        }
    }

    void GrassNativeMemory::emit_all_cleanups(ir::IRBuilder& builder) {
        // Walk from innermost scope to outermost scope
        for (auto s_it = scope_stack_.rbegin(); s_it != scope_stack_.rend(); ++s_it) {
            // Inside each scope, execute in reverse order (LIFO)
            for (auto a_it = s_it->actions.rbegin(); a_it != s_it->actions.rend(); ++a_it) {
                execute_cleanup_action(builder, *a_it);
            }
        }
    }

    size_t GrassNativeMemory::active_resource_count() const {
        size_t count = 0;
        for (const auto& s : scope_stack_) {
            count += s.actions.size();
        }
        return count;
    }

    void GrassNativeMemory::clear_scopes() {
        scope_stack_.clear();
    }

    void GrassNativeMemory::execute_cleanup_action(ir::IRBuilder& builder, const CleanupAction& action) {
        if (!builder.get_function() || !builder.get_function()->parent) {
            return;
        }
        switch (action.kind) {
            case CleanupAction::Kind::CustomDestructor: {
                if (!action.dtor_function.empty() && action.resource_ptr != ir::kInvalidValueId) {
                    ir::IRContext& ctx = builder.get_function()->parent->context;
                    ir::IRType* void_type = ctx.get_void_type();
                    builder.create_call(action.dtor_function, void_type, {action.resource_ptr});
                }
                break;
            }
            case CleanupAction::Kind::FreeDynamicBuffer: {
                if (action.resource_ptr != ir::kInvalidValueId) {
                    emit_free(builder, action.resource_ptr);
                }
                break;
            }
            case CleanupAction::Kind::CustomCallback: {
                if (action.custom_callback) {
                    action.custom_callback(builder);
                }
                break;
            }
        }
    }

} // namespace grass::native
