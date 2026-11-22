#pragma once
#ifndef GRASS_NATIVE_MEMORY_H
#define GRASS_NATIVE_MEMORY_H

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <unordered_map>

#include "GrassBackendTypes.h"
#include "GrassIR.h"

namespace grass::native {

    // Dual-mode memory model:
    // 1. Direct System Mode: low-level baremetal/kernel memory management (alloc, free, realloc, sizeof, raw ptrs, MMIO)
    // 2. Automatic Application Mode (RAII): deterministic scope-based resource tracking without garbage collection
    enum class MemoryMode : uint8_t {
        DirectSystem,
        AutomaticRAII
    };

    // Configuration for runtime memory symbols and bare-metal targets (e.g. RQWos kernel or user-space libc)
    struct MemoryRuntimeConfig {
        std::string alloc_fn = "grass_alloc";
        std::string free_fn = "grass_free";
        std::string realloc_fn = "grass_realloc";
        std::string memcpy_fn = "memcpy";
        std::string memset_fn = "memset";
        bool baremetal_mode = false;
        uint64_t default_alignment = 8;
    };

    // Action executed when a scope ends
    struct CleanupAction {
        enum class Kind : uint8_t {
            CustomDestructor,
            FreeDynamicBuffer,
            CustomCallback
        };

        Kind kind = Kind::FreeDynamicBuffer;
        ir::ValueId resource_ptr = ir::kInvalidValueId;
        std::string dtor_function;
        std::function<void(ir::IRBuilder&)> custom_callback = nullptr;
        std::string debug_name;
    };

    // Lexical scope frame holding RAII cleanups in chronological order
    struct ScopeFrame {
        std::string name;
        std::vector<CleanupAction> actions;
    };

    class GrassNativeMemory {
    public:
        explicit GrassNativeMemory(const MemoryRuntimeConfig& config = {});
        ~GrassNativeMemory() = default;

        const MemoryRuntimeConfig& config() const { return config_; }
        MemoryRuntimeConfig& config() { return config_; }

        // =====================================================================
        // 1. Direct System Mode (Low-Level / Bare-Metal / RQWos)
        // =====================================================================

        // Emit dynamic allocation call: alloc_fn(size_bytes) -> Ptr
        ir::ValueId emit_alloc(ir::IRBuilder& builder, ir::ValueId size_bytes, const std::string& name = "dyn_alloc");
        ir::ValueId emit_alloc_const(ir::IRBuilder& builder, uint64_t size_bytes, const std::string& name = "dyn_alloc");

        // Emit deallocation call: free_fn(ptr)
        void emit_free(ir::IRBuilder& builder, ir::ValueId ptr);

        // Emit reallocation call: realloc_fn(ptr, new_size) -> Ptr
        ir::ValueId emit_realloc(ir::IRBuilder& builder, ir::ValueId ptr, ir::ValueId new_size_bytes, const std::string& name = "dyn_realloc");
        ir::ValueId emit_realloc_const(ir::IRBuilder& builder, ir::ValueId ptr, uint64_t new_size_bytes, const std::string& name = "dyn_realloc");

        // Sizeof calculation: returns compile-time constant integer ValueId
        ir::ValueId emit_sizeof(ir::IRBuilder& builder, ir::IRType* type);
        uint64_t get_type_size(ir::IRType* type) const;
        uint64_t get_type_alignment(ir::IRType* type) const;

        // Raw pointer arithmetic
        // (ptr + byte_offset) -> Ptr
        ir::ValueId emit_ptr_add(ir::IRBuilder& builder, ir::ValueId ptr, ir::ValueId byte_offset, const std::string& name = "ptr_add");
        ir::ValueId emit_ptr_add_const(ir::IRBuilder& builder, ir::ValueId ptr, int64_t byte_offset, const std::string& name = "ptr_add");

        // Byte distance: (ptr1 - ptr2) -> I64
        ir::ValueId emit_ptr_diff(ir::IRBuilder& builder, ir::ValueId ptr1, ir::ValueId ptr2, const std::string& name = "ptr_diff");

        // Raw memory read/write at runtime address (address can be Ptr or I64)
        ir::ValueId emit_read_memory(ir::IRBuilder& builder, ir::ValueId address_val, ir::IRType* val_type, const std::string& name = "mem_val");
        void emit_write_memory(ir::IRBuilder& builder, ir::ValueId address_val, ir::ValueId value_to_write);

        // Physical memory read/write for bare-metal / RQWos (e.g. MMIO, VGA buffer at 0xB8000)
        ir::ValueId emit_read_phys(ir::IRBuilder& builder, uint64_t phys_addr, ir::IRType* val_type, const std::string& name = "phys_val");
        void emit_write_phys(ir::IRBuilder& builder, uint64_t phys_addr, ir::ValueId value_to_write);

        // Raw memory block operations
        void emit_memcpy(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId src_ptr, ir::ValueId size_bytes);
        void emit_memcpy_const(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId src_ptr, uint64_t size_bytes);
        void emit_memset(ir::IRBuilder& builder, ir::ValueId dest_ptr, ir::ValueId byte_val, ir::ValueId size_bytes);
        void emit_memset_const(ir::IRBuilder& builder, ir::ValueId dest_ptr, uint8_t byte_val, uint64_t size_bytes);

        // =====================================================================
        // 2. Automatic Application Mode (RAII Scope Management)
        // =====================================================================

        // Push a new lexical block / scope frame
        void enter_scope(const std::string& scope_name = "");

        // Pop current scope and emit cleanups in reverse order (LIFO)
        void exit_scope(ir::IRBuilder& builder);

        // Register local object with a destructor in the current scope
        void register_raii_object(ir::ValueId obj_ptr, const std::string& dtor_fn_name, const std::string& debug_name = "");

        // Register dynamic heap buffer to be automatically freed when current scope exits
        void register_dynamic_buffer(ir::ValueId buf_ptr, const std::string& debug_name = "");

        // Register custom cleanup function
        void register_custom_cleanup(std::function<void(ir::IRBuilder&)> cleanup_fn, const std::string& debug_name = "");

        // Emit cleanups for all active scopes in function, then emit Ret / RetVoid
        ir::IRInst* emit_return(ir::IRBuilder& builder, std::optional<ir::ValueId> ret_val = std::nullopt);

        // Emit all active cleanups up to function root without returning
        void emit_all_cleanups(ir::IRBuilder& builder);

        // Query active scope stack
        size_t scope_depth() const { return scope_stack_.size(); }
        size_t active_resource_count() const;
        void clear_scopes();

    private:
        MemoryRuntimeConfig config_;
        std::vector<ScopeFrame> scope_stack_;

        void execute_cleanup_action(ir::IRBuilder& builder, const CleanupAction& action);
    };

    // Scoped RAII C++ helper for compilers
    class RAIIScopeGuard {
    public:
        RAIIScopeGuard(GrassNativeMemory& mem, ir::IRBuilder& builder, const std::string& name = "")
            : mem_(mem), builder_(builder), active_(true) {
            mem_.enter_scope(name);
        }
        ~RAIIScopeGuard() {
            if (active_) {
                mem_.exit_scope(builder_);
            }
        }
        void dismiss() { active_ = false; }

    private:
        GrassNativeMemory& mem_;
        ir::IRBuilder& builder_;
        bool active_;
    };

} // namespace grass::native

#endif // GRASS_NATIVE_MEMORY_H
