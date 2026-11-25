#include <iostream>
#include <cassert>
#include <string>
#include <vector>

#include "native/GrassIR.h"
#include "native/GrassNativeMemory.h"
#include "native/GrassNativeOOP.h"

using namespace grass;
using namespace grass::native;

void test_memory_system_mode() {
    std::cout << "[RUN] test_memory_system_mode..." << std::endl;

    ir::IRModule module("test_mem_sys");
    ir::IRFunction* fn = module.create_function("sys_mem_entry", module.context.get_void_type());
    ir::BasicBlock* bb = fn->create_block("entry");
    ir::IRBuilder builder(fn);
    builder.set_insert_point(bb);

    GrassNativeMemory mem;

    // 1. emit_sizeof
    ir::ValueId sz_i8 = mem.emit_sizeof(builder, module.context.get_i8_type());
    ir::ValueId sz_i32 = mem.emit_sizeof(builder, module.context.get_i32_type());
    ir::ValueId sz_i64 = mem.emit_sizeof(builder, module.context.get_i64_type());
    ir::ValueId sz_ptr = mem.emit_sizeof(builder, module.context.get_ptr_type());
    assert(sz_i8 != ir::kInvalidValueId);
    assert(sz_i32 != ir::kInvalidValueId);
    assert(sz_i64 != ir::kInvalidValueId);
    assert(sz_ptr != ir::kInvalidValueId);

    assert(mem.get_type_size(module.context.get_i8_type()) == 1);
    assert(mem.get_type_size(module.context.get_i16_type()) == 2);
    assert(mem.get_type_size(module.context.get_i32_type()) == 4);
    assert(mem.get_type_size(module.context.get_i64_type()) == 8);
    assert(mem.get_type_size(module.context.get_ptr_type()) == 8);

    // 2. emit_alloc_const, emit_realloc_const, emit_free
    ir::ValueId buf = mem.emit_alloc_const(builder, 128, "raw_buf");
    assert(buf != ir::kInvalidValueId);

    ir::ValueId realloc_buf = mem.emit_realloc_const(builder, buf, 256, "realloc_buf");
    assert(realloc_buf != ir::kInvalidValueId);

    mem.emit_free(builder, realloc_buf);

    // 3. Raw pointer arithmetic
    ir::ValueId base_ptr = mem.emit_alloc_const(builder, 64, "base_ptr");
    ir::ValueId off_ptr = mem.emit_ptr_add_const(builder, base_ptr, 16, "off_ptr");
    assert(off_ptr != ir::kInvalidValueId);

    ir::ValueId diff = mem.emit_ptr_diff(builder, off_ptr, base_ptr, "diff_bytes");
    assert(diff != ir::kInvalidValueId);

    // 4. Raw/physical memory read & write (bare-metal MMIO / VGA 0xB8000)
    uint64_t vga_phys = 0xB8000;
    ir::ValueId test_val = builder.create_const_i16(0x0F41); // 'A' with white on black
    mem.emit_write_phys(builder, vga_phys, test_val);

    ir::ValueId read_val = mem.emit_read_phys(builder, vga_phys, module.context.get_i16_type(), "vga_word");
    assert(read_val != ir::kInvalidValueId);

    // 5. Memory copy and set
    mem.emit_memset_const(builder, base_ptr, 0xAA, 32);
    mem.emit_memcpy_const(builder, off_ptr, base_ptr, 16);

    builder.create_ret_void();

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_memory_system_mode" << std::endl;
}

void test_memory_raii_mode() {
    std::cout << "[RUN] test_memory_raii_mode..." << std::endl;

    ir::IRModule module("test_mem_raii");
    ir::IRFunction* fn = module.create_function("raii_entry", module.context.get_i32_type());
    ir::BasicBlock* bb = fn->create_block("entry");
    ir::IRBuilder builder(fn);
    builder.set_insert_point(bb);

    GrassNativeMemory mem;

    // Scope 0 (function root)
    mem.enter_scope("fn_root");
    ir::ValueId buf0 = mem.emit_alloc_const(builder, 64, "buf0");
    mem.register_dynamic_buffer(buf0, "buf0_resource");
    assert(mem.scope_depth() == 1);
    assert(mem.active_resource_count() == 1);

    // Scope 1 (nested block)
    {
        mem.enter_scope("inner_block");
        assert(mem.scope_depth() == 2);

        ir::ValueId obj1 = mem.emit_alloc_const(builder, 32, "obj1");
        mem.register_raii_object(obj1, "Widget_dtor", "widget1");

        ir::ValueId buf1 = mem.emit_alloc_const(builder, 128, "buf1");
        mem.register_dynamic_buffer(buf1, "buf1_resource");

        assert(mem.active_resource_count() == 3);

        // Exit inner scope: must emit buf1 free first, then Widget_dtor(obj1)
        mem.exit_scope(builder);
        assert(mem.scope_depth() == 1);
        assert(mem.active_resource_count() == 1);
    }

    // Early return: emit_return cleans up all active scopes before Ret
    ir::ValueId ret_val = builder.create_const_i32(42);
    mem.emit_return(builder, ret_val);

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_memory_raii_mode" << std::endl;
}

void test_oop_struct_layout_and_alignment() {
    std::cout << "[RUN] test_oop_struct_layout_and_alignment..." << std::endl;

    ir::IRModule module("test_struct_layout");
    GrassNativeOOP oop(module);

    // Layout test: struct with mixed alignment requirements
    // i8 (1 byte, align 1)
    // i32 (4 bytes, align 4) -> should be placed at offset 4 (padding 3 bytes)
    // i8 (1 byte, align 1)   -> placed at offset 8
    // i64 (8 bytes, align 8) -> placed at offset 16 (padding 7 bytes)
    // Total size should be 24 bytes, alignment 8.
    oop.begin_struct("MixedData");
    oop.add_field("tag", module.context.get_i8_type());
    oop.add_field("count", module.context.get_i32_type());
    oop.add_field("flag", module.context.get_i8_type());
    oop.add_field("timestamp", module.context.get_i64_type());
    const OOPClassDef& def = oop.finalize_class();

    assert(def.name == "MixedData");
    assert(!def.is_class);
    assert(!def.has_vtable);
    assert(def.fields.size() == 4);

    const OOPField* f_tag = def.find_field("tag");
    const OOPField* f_count = def.find_field("count");
    const OOPField* f_flag = def.find_field("flag");
    const OOPField* f_ts = def.find_field("timestamp");

    assert(f_tag != nullptr && f_tag->offset == 0 && f_tag->size == 1 && f_tag->alignment == 1);
    assert(f_count != nullptr && f_count->offset == 4 && f_count->size == 4 && f_count->alignment == 4);
    assert(f_flag != nullptr && f_flag->offset == 8 && f_flag->size == 1 && f_flag->alignment == 1);
    assert(f_ts != nullptr && f_ts->offset == 16 && f_ts->size == 8 && f_ts->alignment == 8);

    assert(def.total_size == 24);
    assert(def.alignment == 8);
    assert(def.ir_struct_type != nullptr);
    assert(def.ir_struct_type->size() == 24);
    assert(def.ir_struct_type->alignment() == 8);

    // Generate field access via GetElementPtr, Load, Store
    ir::IRFunction* fn = module.create_function("struct_test_fn", module.context.get_void_type());
    ir::BasicBlock* bb = fn->create_block("entry");
    ir::IRBuilder builder(fn);
    builder.set_insert_point(bb);

    ir::ValueId inst_ptr = builder.create_alloca(def.ir_struct_type, 1, 8, "inst");

    ir::ValueId new_count = builder.create_const_i32(100);
    oop.emit_store_field(builder, inst_ptr, "MixedData", "count", new_count);

    ir::ValueId loaded_count = oop.emit_load_field(builder, inst_ptr, "MixedData", "count", "cnt");
    assert(loaded_count != ir::kInvalidValueId);

    builder.create_ret_void();

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_oop_struct_layout_and_alignment" << std::endl;
}

void test_oop_inheritance_and_vtable() {
    std::cout << "[RUN] test_oop_inheritance_and_vtable..." << std::endl;

    ir::IRModule module("test_oop_vtable");
    GrassNativeOOP oop(module);

    // Base class: Shape with virtual methods draw and area
    oop.begin_class("Shape");
    oop.add_field("id", module.context.get_i32_type());
    oop.add_method("draw", module.context.get_void_type(), {}, {}, /*is_virtual=*/true);
    oop.add_method("area", module.context.get_f64_type(), {}, {}, /*is_virtual=*/true);
    oop.add_constructor();
    oop.add_destructor();
    const OOPClassDef& shape_def = oop.finalize_class();

    assert(shape_def.has_vtable);
    assert(shape_def.find_field("__vptr") != nullptr);
    assert(shape_def.find_field("__vptr")->offset == 0);
    assert(shape_def.vtable_slots.size() == 2);
    assert(shape_def.vtable_slots[0] == "Shape_draw");
    assert(shape_def.vtable_slots[1] == "Shape_area");

    // Derived class: Circle inherits Shape, overrides draw, adds circumference
    oop.begin_class("Circle", "Shape");
    oop.add_field("radius", module.context.get_f64_type());
    oop.add_method("draw", module.context.get_void_type(), {}, {}, /*is_virtual=*/true, /*is_override=*/true);
    oop.add_method("circumference", module.context.get_f64_type(), {}, {}, /*is_virtual=*/true);
    oop.add_constructor({module.context.get_f64_type()}, {"r"});
    oop.add_destructor();
    const OOPClassDef& circle_def = oop.finalize_class();

    assert(circle_def.has_vtable);
    // VTable overrides slot 0 (draw -> Circle_draw), keeps slot 1 (Shape_area), adds slot 2 (Circle_circumference)
    assert(circle_def.vtable_slots.size() == 3);
    assert(circle_def.vtable_slots[0] == "Circle_draw");
    assert(circle_def.vtable_slots[1] == "Shape_area");
    assert(circle_def.vtable_slots[2] == "Circle_circumference");

    // Inherited field id + derived field radius
    assert(circle_def.find_field("id") != nullptr);
    assert(circle_def.find_field("id")->is_inherited);
    assert(circle_def.find_field("radius") != nullptr);
    assert(!circle_def.find_field("radius")->is_inherited);

    // Method creation with implicit 'this'
    ir::IRFunction* draw_fn = oop.create_method_function("Circle", "draw");
    assert(draw_fn != nullptr);
    assert(draw_fn->arguments.size() == 1);
    assert(draw_fn->arguments[0].name == "this");
    assert(draw_fn->arguments[0].type->is_pointer());
    ir::BasicBlock* draw_bb = draw_fn->create_block("entry");
    ir::IRBuilder draw_builder(draw_fn);
    draw_builder.set_insert_point(draw_bb);
    draw_builder.create_ret_void();

    // Call tests: direct method call and virtual method dispatch
    ir::IRFunction* main_fn = module.create_function("test_oop_main", module.context.get_void_type());
    ir::BasicBlock* main_bb = main_fn->create_block("entry");
    ir::IRBuilder main_builder(main_fn);
    main_builder.set_insert_point(main_bb);

    // Construct circle on stack with auto RAII
    ir::ValueId r_val = main_builder.create_const_f64(5.0);
    ir::ValueId circle_inst = oop.emit_stack_object(main_builder, "Circle", {r_val}, /*auto_raii=*/true, "my_circle");
    assert(circle_inst != ir::kInvalidValueId);

    // Virtual call: circle->draw() (void method)
    oop.emit_call_virtual(main_builder, circle_inst, "Circle", "draw");
    assert(main_builder.get_insert_block()->instructions.back()->is_indirect_call);
    assert(main_builder.get_insert_block()->instructions.back()->op == ir::IROp::Call);

    // Virtual call: circle->circumference() (f64 method)
    ir::ValueId circ_res = oop.emit_call_virtual(main_builder, circle_inst, "Circle", "circumference");
    assert(circ_res != ir::kInvalidValueId);

    main_builder.create_ret_void();

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_oop_inheritance_and_vtable" << std::endl;
}

void test_oop_dynamic_arrays() {
    std::cout << "[RUN] test_oop_dynamic_arrays..." << std::endl;

    ir::IRModule module("test_oop_arrays");
    GrassNativeOOP oop(module);

    ir::IRFunction* fn = module.create_function("array_ops_fn", module.context.get_void_type());
    ir::BasicBlock* bb = fn->create_block("entry");
    ir::IRBuilder builder(fn);
    builder.set_insert_point(bb);

    ir::IRType* i64_type = module.context.get_i64_type();

    // 1. Create dynamic array with initial capacity 4
    ir::ValueId arr = oop.emit_array_create_const(builder, i64_type, 4, "my_arr");
    assert(arr != ir::kInvalidValueId);

    // 2. Query initial length and capacity
    ir::ValueId len0 = oop.emit_array_len(builder, arr, "len0");
    ir::ValueId cap0 = oop.emit_array_capacity(builder, arr, "cap0");
    assert(len0 != ir::kInvalidValueId);
    assert(cap0 != ir::kInvalidValueId);

    // 3. Set and get element arr[0]
    ir::ValueId zero = builder.create_const_i64(0);
    ir::ValueId val42 = builder.create_const_i64(42);
    oop.emit_array_set(builder, arr, zero, val42, i64_type);
    ir::ValueId read42 = oop.emit_array_get(builder, arr, zero, i64_type, "read42");
    assert(read42 != ir::kInvalidValueId);

    // 4. Push element (with automatic branch and reallocation)
    ir::ValueId val99 = builder.create_const_i64(99);
    oop.emit_array_push(builder, arr, val99, i64_type);

    // 5. Pop element
    ir::ValueId popped = oop.emit_array_pop(builder, arr, i64_type, "popped");
    assert(popped != ir::kInvalidValueId);

    // 6. Free dynamic array
    oop.emit_array_free(builder, arr);

    builder.create_ret_void();

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_oop_dynamic_arrays" << std::endl;
}

void test_oop_strings() {
    std::cout << "[RUN] test_oop_strings..." << std::endl;

    ir::IRModule module("test_oop_strings");
    GrassNativeOOP oop(module);

    ir::IRFunction* fn = module.create_function("string_ops_fn", module.context.get_void_type());
    ir::BasicBlock* bb = fn->create_block("entry");
    ir::IRBuilder builder(fn);
    builder.set_insert_point(bb);

    // 1. String literal
    ir::ValueId str1 = oop.emit_string_literal(builder, "Hello, ", "str1");
    ir::ValueId str2 = oop.emit_string_literal(builder, "GrassGreen!", "str2");
    assert(str1 != ir::kInvalidValueId);
    assert(str2 != ir::kInvalidValueId);

    // 2. String length
    ir::ValueId len1 = oop.emit_string_len(builder, str1, "len1");
    assert(len1 != ir::kInvalidValueId);

    // 3. String char indexing
    ir::ValueId zero = builder.create_const_i64(0);
    ir::ValueId ch0 = oop.emit_string_char_at(builder, str1, zero, "ch0");
    assert(ch0 != ir::kInvalidValueId);

    // 4. String concatenation
    ir::ValueId concat_str = oop.emit_string_concat(builder, str1, str2, "concat");
    assert(concat_str != ir::kInvalidValueId);

    // 5. Free strings
    oop.emit_string_free(builder, str1);
    oop.emit_string_free(builder, str2);
    oop.emit_string_free(builder, concat_str);

    builder.create_ret_void();

    std::string err;
    bool ok = module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_oop_strings" << std::endl;
}

void test_oop_modules_import_and_merge() {
    std::cout << "[RUN] test_oop_modules_import_and_merge..." << std::endl;

    ir::IRModule target_module("main_app");
    GrassNativeOOP oop(target_module);

    // 1. Register MathModule
    ModuleExport math_mod;
    math_mod.module_name = "Math";
    math_mod.dependencies = {};
    OOPClassDef vec2_def;
    vec2_def.name = "Vec2";
    vec2_def.is_class = false;
    vec2_def.total_size = 16;
    vec2_def.alignment = 8;
    OOPField fx{"x", target_module.context.get_f64_type(), 0, 8, 8, false};
    OOPField fy{"y", target_module.context.get_f64_type(), 8, 8, 8, false};
    vec2_def.fields = {fx, fy};
    vec2_def.field_indices["x"] = 0;
    vec2_def.field_indices["y"] = 1;
    math_mod.classes["Vec2"] = vec2_def;

    ModuleExport::ExportedFunction dot_fn;
    dot_fn.name = "Math_dot";
    dot_fn.return_type = target_module.context.get_f64_type();
    dot_fn.param_types = {target_module.context.get_ptr_type(), target_module.context.get_ptr_type()};
    dot_fn.param_names = {"a", "b"};
    math_mod.exported_functions.push_back(dot_fn);

    oop.register_module(math_mod);

    // 2. Import MathModule into target_module
    std::string err;
    bool imported = oop.import_module("Math", err);
    assert(imported);
    assert(oop.has_class("Vec2"));
    assert(target_module.get_function("Math_dot") != nullptr);

    // 3. Define implementation of Math_dot in a separate module and merge it
    ir::IRModule math_impl_mod("math_lib");
    ir::IRFunction* dot_impl = math_impl_mod.create_function("Math_dot", math_impl_mod.context.get_f64_type());
    dot_impl->add_argument("a", math_impl_mod.context.get_ptr_type());
    dot_impl->add_argument("b", math_impl_mod.context.get_ptr_type());
    ir::BasicBlock* dot_bb = dot_impl->create_block("entry");
    ir::IRBuilder dot_builder(dot_impl);
    dot_builder.set_insert_point(dot_bb);
    ir::ValueId dot_res = dot_builder.create_const_f64(0.0);
    dot_builder.create_ret(dot_res);

    bool math_merged = oop.merge_module(math_impl_mod, "", err);
    assert(math_merged);

    // 4. Merge another compiled module (physics) into target_module
    ir::IRModule second_mod("physics_lib");
    ir::IRFunction* phys_fn = second_mod.create_function("Physics_step", second_mod.context.get_void_type());
    ir::BasicBlock* p_bb = phys_fn->create_block("entry");
    ir::IRBuilder p_builder(phys_fn);
    p_builder.set_insert_point(p_bb);
    p_builder.create_ret_void();

    bool merged = oop.merge_module(second_mod, "", err);
    assert(merged);
    assert(target_module.get_function("Physics_step") != nullptr);

    bool ok = target_module.verify(err);
    if (!ok) {
        std::cerr << "Verification failed: " << err << std::endl;
    }
    assert(ok);

    std::cout << "[PASS] test_oop_modules_import_and_merge" << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "Starting GrassNativeOOP & Memory Tests" << std::endl;
    std::cout << "========================================" << std::endl;

    test_memory_system_mode();
    test_memory_raii_mode();
    test_oop_struct_layout_and_alignment();
    test_oop_inheritance_and_vtable();
    test_oop_dynamic_arrays();
    test_oop_strings();
    test_oop_modules_import_and_merge();

    std::cout << "========================================" << std::endl;
    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
