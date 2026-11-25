#pragma once
#ifndef GRASS_NATIVE_OOP_H
#define GRASS_NATIVE_OOP_H

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>

#include "GrassBackendTypes.h"
#include "GrassIR.h"
#include "GrassNativeMemory.h"

namespace grass::native {

    // Represents a single field in a struct or class
    struct OOPField {
        std::string name;
        ir::IRType* type = nullptr;
        uint64_t offset = 0;
        uint64_t size = 0;
        uint64_t alignment = 1;
        bool is_inherited = false;
    };

    // Represents a method in a class
    struct OOPMethod {
        std::string name;
        std::string mangled_name;
        ir::IRType* return_type = nullptr;
        std::vector<ir::IRType*> param_types; // Parameters without implicit 'this'
        std::vector<std::string> param_names;
        bool is_virtual = false;
        bool is_override = false;
        int32_t vtable_index = -1;
        bool is_constructor = false;
        bool is_destructor = false;
    };

    // Represents the complete compiled layout and metadata of a struct or class
    struct OOPClassDef {
        std::string name;
        std::string base_name;
        bool is_class = true;
        bool has_vtable = false;
        bool has_destructor = false;
        bool has_explicit_destructor = false;
        uint64_t vptr_offset = 0;
        uint64_t total_size = 0;
        uint64_t alignment = 1;

        std::vector<OOPField> fields;
        std::unordered_map<std::string, size_t> field_indices;

        std::vector<OOPMethod> methods;
        std::unordered_map<std::string, size_t> method_indices;

        std::vector<std::string> vtable_slots; // Function names placed in vtable order
        std::vector<std::string> vtable_method_names; // Base method names matching vtable slots
        ir::IRType* ir_struct_type = nullptr;

        const OOPField* find_field(const std::string& field_name) const;
        const OOPMethod* find_method(const std::string& method_name) const;
        int32_t get_field_index(const std::string& field_name) const;
    };

    // Multi-file module export descriptor
    struct ModuleExport {
        std::string module_name;
        std::vector<std::string> dependencies;
        std::unordered_map<std::string, OOPClassDef> classes;
        struct ExportedFunction {
            std::string name;
            ir::IRType* return_type = nullptr;
            std::vector<ir::IRType*> param_types;
            std::vector<std::string> param_names;
        };
        std::vector<ExportedFunction> exported_functions;
    };

    class GrassNativeOOP {
    public:
        explicit GrassNativeOOP(ir::IRModule& module, GrassNativeMemory* memory = nullptr);
        ~GrassNativeOOP() = default;

        ir::IRModule& get_module() { return module_; }
        const ir::IRModule& get_module() const { return module_; }
        ir::IRContext& get_context() { return module_.context; }

        void set_memory_manager(GrassNativeMemory* mem) { memory_ = mem ? mem : &default_memory_; }
        GrassNativeMemory* get_memory_manager() { return memory_; }

        // =====================================================================
        // 1. Struct and Class Layout Calculation
        // =====================================================================

        // Begin definition of struct or class
        void begin_struct(const std::string& name);
        void begin_class(const std::string& name, const std::string& base_class = "");

        // Add field to current struct/class being declared
        void add_field(const std::string& field_name, ir::IRType* field_type);

        // Add method to current class
        void add_method(const std::string& method_name,
                        ir::IRType* return_type,
                        const std::vector<ir::IRType*>& param_types = {},
                        const std::vector<std::string>& param_names = {},
                        bool is_virtual = false,
                        bool is_override = false);

        // Add constructor / destructor
        void add_constructor(const std::vector<ir::IRType*>& param_types = {},
                             const std::vector<std::string>& param_names = {});
        void add_destructor();

        // Finalize layout calculation (computes alignment, padding, offsets, vtable slots, and registers IRType)
        const OOPClassDef& finalize_class();

        // Query registered class/struct definition
        const OOPClassDef* get_class(const std::string& name) const;
        bool has_class(const std::string& name) const;
        std::string get_destructor_name(const std::string& class_name) const {
            return class_name + "__dtor";
        }

        // Field access:
        // Compute GEP pointer to field: instance_ptr->field
        ir::ValueId emit_field_gep(ir::IRBuilder& builder,
                                   ir::ValueId instance_ptr,
                                   const std::string& class_name,
                                   const std::string& field_name,
                                   const std::string& name = "field_gep");

        // Load value from field: load(instance_ptr->field)
        ir::ValueId emit_load_field(ir::IRBuilder& builder,
                                    ir::ValueId instance_ptr,
                                    const std::string& class_name,
                                    const std::string& field_name,
                                    const std::string& name = "field_val");

        // Store value to field: instance_ptr->field = value
        ir::IRInst* emit_store_field(ir::IRBuilder& builder,
                                     ir::ValueId instance_ptr,
                                     const std::string& class_name,
                                     const std::string& field_name,
                                     ir::ValueId value_to_store);

        // =====================================================================
        // 2. Methods, Implicit 'this', VTable, Constructors & Destructors
        // =====================================================================

        // Creates an IRFunction for a method (with implicit first parameter 'this')
        ir::IRFunction* create_method_function(const std::string& class_name,
                                               const std::string& method_name);

        // Method invocation: automatically prepends 'instance_ptr' as implicit 'this'
        // If method is virtual, generates indirect call via vtable. Otherwise direct call.
        ir::ValueId emit_call_method(ir::IRBuilder& builder,
                                     ir::ValueId instance_ptr,
                                     const std::string& class_name,
                                     const std::string& method_name,
                                     const std::vector<ir::ValueId>& user_args = {},
                                     const std::string& name = "call_res");

        // Direct virtual method invocation through vtable
        ir::ValueId emit_call_virtual(ir::IRBuilder& builder,
                                      ir::ValueId instance_ptr,
                                      const std::string& class_name,
                                      const std::string& method_name,
                                      const std::vector<ir::ValueId>& user_args = {},
                                      const std::string& name = "vcall_res");

        // VTable initialization for an instance in its constructor
        void emit_vtable_init(ir::IRBuilder& builder,
                              ir::ValueId instance_ptr,
                              const std::string& class_name);

        // Object creation on heap (alloc + ctor)
        ir::ValueId emit_new_object(ir::IRBuilder& builder,
                                    const std::string& class_name,
                                    const std::vector<ir::ValueId>& ctor_args = {},
                                    const std::string& name = "new_obj");

        // Object destruction on heap (dtor + free)
        void emit_delete_object(ir::IRBuilder& builder,
                                ir::ValueId instance_ptr,
                                const std::string& class_name);

        // Stack-allocated object (alloca + ctor + optional RAII auto-destructor registration)
        ir::ValueId emit_stack_object(ir::IRBuilder& builder,
                                      const std::string& class_name,
                                      const std::vector<ir::ValueId>& ctor_args = {},
                                      bool auto_raii = true,
                                      const std::string& name = "stack_obj");

        // Helper to obtain function pointer in GrassIR
        ir::ValueId emit_get_function_pointer(ir::IRBuilder& builder,
                                              const std::string& fn_name,
                                              const std::string& name = "fn_ptr");

        // =====================================================================
        // 3. Dynamic Arrays
        // =====================================================================

        // Dynamic array type: struct GrassArray { i64 capacity, i64 length, ptr data }
        ir::IRType* get_dynamic_array_type();

        // Create dynamic array on heap
        ir::ValueId emit_array_create(ir::IRBuilder& builder,
                                      ir::IRType* element_type,
                                      ir::ValueId initial_capacity,
                                      const std::string& name = "arr_obj");
        ir::ValueId emit_array_create_const(ir::IRBuilder& builder,
                                            ir::IRType* element_type,
                                            uint64_t initial_capacity = 8,
                                            const std::string& name = "arr_obj");

        // Query array length and capacity
        ir::ValueId emit_array_len(ir::IRBuilder& builder, ir::ValueId arr_ptr, const std::string& name = "arr_len");
        ir::ValueId emit_array_capacity(ir::IRBuilder& builder, ir::ValueId arr_ptr, const std::string& name = "arr_cap");

        // Compute pointer to element arr[index]
        ir::ValueId emit_array_element_ptr(ir::IRBuilder& builder,
                                           ir::ValueId arr_ptr,
                                           ir::ValueId index_val,
                                           ir::IRType* elem_type,
                                           const std::string& name = "elem_ptr");

        // Read element: arr[index]
        ir::ValueId emit_array_get(ir::IRBuilder& builder,
                                   ir::ValueId arr_ptr,
                                   ir::ValueId index_val,
                                   ir::IRType* elem_type,
                                   const std::string& name = "elem_val");

        // Write element: arr[index] = value
        void emit_array_set(ir::IRBuilder& builder,
                            ir::ValueId arr_ptr,
                            ir::ValueId index_val,
                            ir::ValueId value_to_store,
                            ir::IRType* elem_type);

        // Append element to array (automatically expands capacity if needed)
        void emit_array_push(ir::IRBuilder& builder,
                             ir::ValueId arr_ptr,
                             ir::ValueId value_to_push,
                             ir::IRType* elem_type);

        // Pop last element
        ir::ValueId emit_array_pop(ir::IRBuilder& builder,
                                   ir::ValueId arr_ptr,
                                   ir::IRType* elem_type,
                                   const std::string& name = "popped_val");

        // Free dynamic array
        void emit_array_free(ir::IRBuilder& builder, ir::ValueId arr_ptr);

        // =====================================================================
        // 4. Strings (Literals, Concatenation, Indexing)
        // =====================================================================

        // String type: struct GrassString { i64 length, ptr data }
        ir::IRType* get_string_type();

        // Emit string literal object
        ir::ValueId emit_string_literal(ir::IRBuilder& builder,
                                        const std::string& text,
                                        const std::string& name = "str_lit");

        // Concatenate strings: str1 + str2 -> new_str
        ir::ValueId emit_string_concat(ir::IRBuilder& builder,
                                       ir::ValueId str1_ptr,
                                       ir::ValueId str2_ptr,
                                       const std::string& name = "concat_str");

        // String length: str.length
        ir::ValueId emit_string_len(ir::IRBuilder& builder, ir::ValueId str_ptr, const std::string& name = "str_len");

        // String char indexing: str[index] -> i8
        ir::ValueId emit_string_char_at(ir::IRBuilder& builder,
                                        ir::ValueId str_ptr,
                                        ir::ValueId index_val,
                                        const std::string& name = "char_val");

        // Free string object and buffer
        void emit_string_free(ir::IRBuilder& builder, ir::ValueId str_ptr);

        // =====================================================================
        // 5. Multi-file Modules & Module Import
        // =====================================================================

        // Register a module export definition
        void register_module(const ModuleExport& mod);

        // Import a registered module into the current IRModule
        bool import_module(const std::string& module_name, std::string& error_out);

        // Merge an external IRModule into current IRModule
        bool merge_module(ir::IRModule& other_module, const std::string& prefix = "", std::string& error_out = *(std::string*)nullptr);

    private:
        ir::IRModule& module_;
        GrassNativeMemory default_memory_;
        GrassNativeMemory* memory_ = nullptr;

        std::unordered_map<std::string, OOPClassDef> classes_;
        std::unordered_map<std::string, ModuleExport> registered_modules_;
        std::unordered_set<std::string> imported_modules_;

        // Active class definition state
        bool is_building_ = false;
        OOPClassDef current_class_;

        ir::IRType* dynamic_array_type_ = nullptr;
        ir::IRType* string_type_ = nullptr;

        std::string mangle_method_name(const std::string& class_name, const std::string& method_name) const;
    };

} // namespace grass::native

#endif // GRASS_NATIVE_OOP_H
