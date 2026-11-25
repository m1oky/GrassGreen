#include <iostream>
#include <cassert>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

#include "native/GrassIR.h"

using namespace grass::ir;

static int g_test_count = 0;
static int g_pass_count = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n" << std::flush; \
            std::exit(1); \
        } \
    } while (0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "...\n" << std::flush; \
        g_test_count++; \
        fn(); \
        g_pass_count++; \
        std::cout << "[PASSED]  " << #fn << "\n" << std::flush; \
    } while (0)

// -----------------------------------------------------------------------------
// Test 1: Type System & Struct Layout
// -----------------------------------------------------------------------------
void test_type_system() {
    IRContext ctx;

    IRType* t_void = ctx.get_void_type();
    IRType* t_i1   = ctx.get_i1_type();
    IRType* t_i8   = ctx.get_i8_type();
    IRType* t_i16  = ctx.get_i16_type();
    IRType* t_i32  = ctx.get_i32_type();
    IRType* t_i64  = ctx.get_i64_type();
    IRType* t_f32  = ctx.get_f32_type();
    IRType* t_f64  = ctx.get_f64_type();
    IRType* t_ptr  = ctx.get_ptr_type();

    TEST_ASSERT(t_void->is_void(), "Void check");
    TEST_ASSERT(t_i1->is_bool() && t_i1->bit_width() == 1, "I1 check");
    TEST_ASSERT(t_i8->is_integer() && t_i8->size() == 1, "I8 check");
    TEST_ASSERT(t_i16->is_integer() && t_i16->size() == 2, "I16 check");
    TEST_ASSERT(t_i32->is_integer() && t_i32->size() == 4, "I32 check");
    TEST_ASSERT(t_i64->is_integer() && t_i64->size() == 8, "I64 check");
    TEST_ASSERT(t_f32->is_floating_point() && t_f32->size() == 4, "F32 check");
    TEST_ASSERT(t_f64->is_floating_point() && t_f64->size() == 8, "F64 check");
    TEST_ASSERT(t_ptr->is_pointer() && t_ptr->size() == 8, "Ptr check");

    // Array type: [10 x i32]
    IRType* t_arr = ctx.get_array_type(t_i32, 10);
    TEST_ASSERT(t_arr->is_array(), "Array check");
    TEST_ASSERT(t_arr->size() == 40, "Array size check (10 * 4)");
    TEST_ASSERT(t_arr->alignment() == 4, "Array align check");
    TEST_ASSERT(t_arr->to_string() == "[10 x i32]", "Array to_string check");

    // Struct type: struct Person { i8 id; i32 age; i64 score; f64 weight; ptr next; }
    std::vector<IRType*> fields = {t_i8, t_i32, t_i64, t_f64, t_ptr};
    std::vector<std::string> names = {"id", "age", "score", "weight", "next"};
    IRType* t_struct = ctx.get_struct_type("Person", fields, names);

    TEST_ASSERT(t_struct->is_struct(), "Struct check");
    TEST_ASSERT(t_struct->alignment() == 8, "Struct align check (max is 8)");
    // Field 0: i8, offset 0
    TEST_ASSERT(t_struct->get_field_offset(0) == 0, "Field 0 offset");
    // Field 1: i32, aligned to 4 -> offset 4
    TEST_ASSERT(t_struct->get_field_offset(1) == 4, "Field 1 offset (padding 3)");
    // Field 2: i64, aligned to 8 -> offset 8
    TEST_ASSERT(t_struct->get_field_offset(2) == 8, "Field 2 offset");
    // Field 3: f64, aligned to 8 -> offset 16
    TEST_ASSERT(t_struct->get_field_offset(3) == 16, "Field 3 offset");
    // Field 4: ptr, aligned to 8 -> offset 24
    TEST_ASSERT(t_struct->get_field_offset(4) == 24, "Field 4 offset");
    // Total struct size: 24 + 8 = 32, aligned to 8 -> 32
    TEST_ASSERT(t_struct->size() == 32, "Struct size check (32 bytes)");

    TEST_ASSERT(t_struct->get_field_index("score") == 2, "Field index lookup");
    TEST_ASSERT(t_struct->get_field_type(1) == t_i32, "Field type lookup");
}

// -----------------------------------------------------------------------------
// Test 2: IRBuilder & Verifier Integrity
// -----------------------------------------------------------------------------
void test_builder_and_verifier() {
    IRModule module("test_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("simple_add", i32_t);
    fn->add_argument("a", i32_t);
    fn->add_argument("b", i32_t);

    IRBuilder builder(fn);
    BasicBlock* entry = builder.create_block("entry");
    builder.set_insert_point(entry);

    ValueId sum = builder.create_add(fn->arguments[0].id, fn->arguments[1].id, "sum");
    builder.create_ret(sum);

    std::string err;
    bool ok = module.verify(err);
    if (!ok) std::cerr << "Verification error: " << err << "\n" << std::flush;
    TEST_ASSERT(ok, "Module verify should succeed on valid function");

    // Test verifier failure: missing terminator
    BasicBlock* bad_bb = builder.create_block("bad_bb");
    builder.set_insert_point(bad_bb);
    builder.create_add(sum, sum); // No terminator!
    fn->rebuild_cfg();

    bool bad_ok = fn->verify(err);
    TEST_ASSERT(!bad_ok, "Verifier must detect missing terminator");
    TEST_ASSERT(!err.empty(), "Verifier must provide error description");

    // Clean up bad_bb
    fn->erase_block(bad_bb);
    fn->rebuild_cfg();
    TEST_ASSERT(fn->verify(err), "Function should be valid after cleanup");
}

// -----------------------------------------------------------------------------
// Test 3: Dominance Tree & Mem2Reg SSA Construction
// -----------------------------------------------------------------------------
void test_mem2reg() {
    IRModule module("mem2reg_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("test_if_else_alloca", i32_t);
    fn->add_argument("cond_val", i32_t);

    IRBuilder builder(fn);
    BasicBlock* b_entry = builder.create_block("entry");
    BasicBlock* b_true  = builder.create_block("then_b");
    BasicBlock* b_false = builder.create_block("else_b");
    BasicBlock* b_merge = builder.create_block("merge_b");

    // Entry: alloca x, cond = cond_val > 0, br cond, then_b, else_b
    builder.set_insert_point(b_entry);
    ValueId x_alloca = builder.create_alloca(i32_t, 1, 4, "x");
    ValueId zero = builder.create_const_i32(0);
    ValueId cond = builder.create_icmp_sgt(fn->arguments[0].id, zero);
    builder.create_branch(cond, b_true, b_false);

    // True branch: store 10 to x, jmp merge
    builder.set_insert_point(b_true);
    ValueId v10 = builder.create_const_i32(10);
    builder.create_store(v10, x_alloca);
    builder.create_jump(b_merge);

    // False branch: store 20 to x, jmp merge
    builder.set_insert_point(b_false);
    ValueId v20 = builder.create_const_i32(20);
    builder.create_store(v20, x_alloca);
    builder.create_jump(b_merge);

    // Merge: load x, ret loaded
    builder.set_insert_point(b_merge);
    ValueId loaded = builder.create_load(x_alloca, "res");
    builder.create_ret(loaded);

    std::string err;
    TEST_ASSERT(fn->verify(err), "Pre-mem2reg verification");

    Mem2RegPass mem2reg;
    bool promoted = mem2reg.run_on_function(*fn);
    TEST_ASSERT(promoted, "Mem2Reg should promote alloca");

    bool verified_after = fn->verify(err);
    if (!verified_after) std::cerr << "Mem2Reg verify error: " << err << "\n";
    TEST_ASSERT(verified_after, "Post-mem2reg verification");

    // Verify alloca, store, load are eliminated and replaced by Phi
    bool has_alloca = false;
    bool has_load = false;
    bool has_store = false;
    bool has_phi = false;

    for (const auto& bb : fn->blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Alloca) has_alloca = true;
            if (inst->op == IROp::Load)   has_load = true;
            if (inst->op == IROp::Store)  has_store = true;
            if (inst->op == IROp::Phi) {
                has_phi = true;
                TEST_ASSERT(inst->phi_incoming.size() == 2, "Phi must have 2 incoming branches");
            }
        }
    }

    TEST_ASSERT(!has_alloca, "Alloca must be removed by Mem2Reg");
    TEST_ASSERT(!has_load, "Load must be removed by Mem2Reg");
    TEST_ASSERT(!has_store, "Store must be removed by Mem2Reg");
    TEST_ASSERT(has_phi, "Phi must be inserted by Mem2Reg");
}

// -----------------------------------------------------------------------------
// Test 4: Optimizer Passes (ConstantFolding, AlgSimp, StrengthRed, CSE, DCE, CFGSimp)
// -----------------------------------------------------------------------------
void test_optimizer_passes() {
    IRModule module("opt_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("opt_fn", i32_t);
    fn->add_argument("arg", i32_t);

    IRBuilder builder(fn);
    BasicBlock* entry = builder.create_block("entry");
    BasicBlock* dead_bb = builder.create_block("unreachable_bb");
    BasicBlock* next_bb = builder.create_block("next_bb");

    builder.set_insert_point(entry);

    // Constant folding target: 15 + 25 -> 40
    ValueId c15 = builder.create_const_i32(15);
    ValueId c25 = builder.create_const_i32(25);
    ValueId folded_sum = builder.create_add(c15, c25, "sum_const");

    // Algebraic simplification: x + 0 -> x, x * 1 -> x, x ^ x -> 0
    ValueId c0 = builder.create_const_i32(0);
    ValueId alg1 = builder.create_add(fn->arguments[0].id, c0, "alg_add");
    ValueId c1 = builder.create_const_i32(1);
    ValueId alg2 = builder.create_mul(alg1, c1, "alg_mul");
    ValueId alg3 = builder.create_xor(alg2, alg2, "alg_xor_zero");

    // Strength reduction: x * 8 -> x << 3, x udiv 16 -> x >> 4, x urem 8 -> x & 7
    ValueId c8 = builder.create_const_i32(8);
    ValueId sr_mul = builder.create_mul(fn->arguments[0].id, c8, "sr_mul");
    ValueId c16 = builder.create_const_i32(16);
    ValueId sr_div = builder.create_udiv(fn->arguments[0].id, c16, "sr_div");
    ValueId sr_rem = builder.create_urem(fn->arguments[0].id, c8, "sr_rem");

    // CSE: repeated pure computation
    ValueId c2 = builder.create_const_i32(2);
    ValueId expr1 = builder.create_mul(fn->arguments[0].id, c2, "expr1");
    ValueId expr2 = builder.create_mul(fn->arguments[0].id, c2, "expr2"); // Duplicate of expr1!

    // Dead code: unused computation
    builder.create_add(expr1, c15, "dead_val");

    // Combine result
    ValueId r1 = builder.create_add(folded_sum, alg3);
    ValueId r2 = builder.create_add(r1, sr_mul);
    ValueId r3 = builder.create_add(r2, sr_div);
    ValueId r4 = builder.create_add(r3, sr_rem);
    ValueId r5 = builder.create_add(r4, expr1);
    ValueId r6 = builder.create_add(r5, expr2);

    builder.create_jump(next_bb);

    // Unreachable block (should be eliminated by DCE)
    builder.set_insert_point(dead_bb);
    builder.create_add(c15, c25);
    builder.create_ret(c0);

    // Next block
    builder.set_insert_point(next_bb);
    builder.create_ret(r6);

    std::string err;
    TEST_ASSERT(fn->verify(err), "Pre-optimization verification");

    PassManager pm;
    pm.add_standard_optimizations();
    pm.run(*fn);

    bool post_ok = fn->verify(err);
    if (!post_ok) std::cerr << "Verify error: " << err << "\n" << std::flush;
    TEST_ASSERT(post_ok, "Post-optimization verification");

    // Verify optimizations took effect
    bool found_unreachable = false;
    bool found_shl = false;
    bool found_lshr = false;
    bool found_and = false;
    for (const auto& bb : fn->blocks) {
        if (bb->name == "unreachable_bb") found_unreachable = true;
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Shl) found_shl = true;
            if (inst->op == IROp::LShr) found_lshr = true;
            if (inst->op == IROp::And) found_and = true;
        }
    }

    TEST_ASSERT(!found_unreachable, "DCE must eliminate unreachable blocks");
    TEST_ASSERT(found_shl, "StrengthReduction must replace mul by 8 with Shl 3");
    TEST_ASSERT(found_lshr, "StrengthReduction must replace udiv by 16 with LShr 4");
    TEST_ASSERT(found_and, "StrengthReduction must replace urem by 8 with And 7");
}

// -----------------------------------------------------------------------------
// Test 5: Linear Scan Register Allocator & Spilling (20+ live variables)
// -----------------------------------------------------------------------------
void test_linear_scan_and_spilling() {
    IRModule module("lscan_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("pressure_func", i32_t);
    fn->add_argument("init", i32_t);

    IRBuilder builder(fn);
    BasicBlock* entry = builder.create_block("entry");
    builder.set_insert_point(entry);

    // Create 26 simultaneously live variables
    // v0 = init + 1
    // v1 = v0 + 2 ...
    // and finally sum all 26 variables together to force them all to be live simultaneously!
    std::vector<ValueId> live_vars;
    ValueId prev = fn->arguments[0].id;
    for (int i = 0; i < 26; ++i) {
        ValueId c = builder.create_const_i32(i + 1);
        ValueId v = builder.create_add(prev, c, "live_" + std::to_string(i));
        live_vars.push_back(v);
        prev = v;
    }

    // Accumulate all 26 live variables so their live ranges overlap
    ValueId accum = live_vars[0];
    for (size_t i = 1; i < live_vars.size(); ++i) {
        accum = builder.create_add(accum, live_vars[i], "acc_" + std::to_string(i));
    }
    builder.create_ret(accum);

    std::string err;
    TEST_ASSERT(fn->verify(err), "Pressure func verify");

    // Test Win64 ABI allocation
    LinearScanAllocator win64_alloc(TargetABI::Win64);
    AllocationResult win_res = win64_alloc.allocate(*fn);

    TEST_ASSERT(win_res.spill_count > 0, "With 26 live variables, spilling MUST occur on Win64");
    TEST_ASSERT(win_res.stack_frame_size > 0, "Stack frame size must be non-zero");
    TEST_ASSERT(win_res.stack_frame_size % 16 == 0, "Stack frame MUST be 16-byte aligned");

    // Test Cdecl32 ABI allocation (only 6 allocatable GPRs, guaranteed heavy spilling)
    LinearScanAllocator cdecl_alloc(TargetABI::Cdecl32);
    AllocationResult cdecl_res = cdecl_alloc.allocate(*fn);
    TEST_ASSERT(cdecl_res.spill_count >= 20, "Cdecl32 should spill at least 20 variables");
    TEST_ASSERT(cdecl_res.stack_frame_size % 16 == 0, "Cdecl32 stack frame must be 16-byte aligned");

    // Check non-interference: No two intervals in the same register overlap in time!
    std::unordered_map<PhysReg, std::vector<LiveInterval>> reg_assignments;
    for (const auto& [vid, interval] : win_res.intervals) {
        (void)vid;
        if (!interval.is_spilled && interval.reg != PhysReg::None) {
            reg_assignments[interval.reg].push_back(interval);
        }
    }

    for (const auto& [reg, iv_list] : reg_assignments) {
        for (size_t i = 0; i < iv_list.size(); ++i) {
            for (size_t j = i + 1; j < iv_list.size(); ++j) {
                const auto& a = iv_list[i];
                const auto& b = iv_list[j];
                bool overlaps = (a.start <= b.end && b.start <= a.end);
                if (overlaps) {
                    std::cerr << "Collision on register " << grass::ir::to_string(reg)
                              << ": [%" << a.val << " (" << a.start << ".." << a.end << ")] overlaps with "
                              << "[%" << b.val << " (" << b.start << ".." << b.end << ")]\n";
                }
                TEST_ASSERT(!overlaps, "Live intervals in same physical register must NOT overlap");
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Test 6: Phi Resolution & Swap/Cycle Breaking
// -----------------------------------------------------------------------------
void test_phi_resolution() {
    IRModule module("phi_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("phi_cycle_fn", i32_t);
    fn->add_argument("cond", i32_t);

    IRBuilder builder(fn);
    BasicBlock* entry = builder.create_block("entry");
    BasicBlock* loop = builder.create_block("loop");
    BasicBlock* exit = builder.create_block("exit");

    builder.set_insert_point(entry);
    ValueId c1 = builder.create_const_i32(1);
    ValueId c2 = builder.create_const_i32(2);
    builder.create_jump(loop);

    // In loop:
    // phi_a = phi [c1, entry], [phi_b, loop]
    // phi_b = phi [c2, entry], [phi_a, loop]  <-- SWAP CYCLE!
    builder.set_insert_point(loop);
    ValueId phi_a = builder.create_phi(i32_t, "phi_a");
    ValueId phi_b = builder.create_phi(i32_t, "phi_b");

    builder.add_phi_incoming(phi_a, entry, c1);
    builder.add_phi_incoming(phi_a, loop, phi_b);

    builder.add_phi_incoming(phi_b, entry, c2);
    builder.add_phi_incoming(phi_b, loop, phi_a);

    ValueId zero = builder.create_const_i32(0);
    ValueId br_cond = builder.create_icmp_eq(fn->arguments[0].id, zero);
    builder.create_branch(br_cond, loop, exit);

    builder.set_insert_point(exit);
    ValueId res = builder.create_add(phi_a, phi_b);
    builder.create_ret(res);

    std::string err;
    TEST_ASSERT(fn->verify(err), "Phi cycle verify");

    LinearScanAllocator allocator(TargetABI::Win64);
    AllocationResult res_alloc = allocator.allocate(*fn);

    // Phi resolution must have generated parallel copy instructions
    TEST_ASSERT(!res_alloc.phi_copies.empty(), "Phi resolution must produce parallel copy moves");
}

// -----------------------------------------------------------------------------
// Test 7: Complex End-to-End Test (All Features Combined)
// -----------------------------------------------------------------------------
void test_complex_end_to_end() {
    // Requirements from task:
    // - if/else branching
    // - while loop
    // - local Alloca variables
    // - dead code
    // - constant expressions
    // - 20+ simultaneously live variables (to guarantee spilling)
    // - verify(), Mem2Reg, PassManager, dump(), LinearScanAllocator
    IRModule module("complex_mod");
    IRType* i32_t = module.context.get_i32_type();

    IRFunction* fn = module.create_function("industrial_pipeline", i32_t);
    fn->add_argument("n", i32_t);
    fn->add_argument("limit", i32_t);

    IRBuilder builder(fn);
    BasicBlock* b_entry = builder.create_block("entry");
    BasicBlock* b_loop_head = builder.create_block("loop_head");
    BasicBlock* b_loop_body = builder.create_block("loop_body");
    BasicBlock* b_if_true   = builder.create_block("if_true");
    BasicBlock* b_if_false  = builder.create_block("if_false");
    BasicBlock* b_if_merge  = builder.create_block("if_merge");
    BasicBlock* b_unreachable = builder.create_block("dead_block");
    BasicBlock* b_exit      = builder.create_block("exit");

    // 1. Entry: Allocas, Constants, and 22 simultaneously live variables
    builder.set_insert_point(b_entry);
    ValueId acc_alloca = builder.create_alloca(i32_t, 1, 4, "acc_alloca");
    ValueId iter_alloca = builder.create_alloca(i32_t, 1, 4, "iter_alloca");

    ValueId c0 = builder.create_const_i32(0);
    ValueId c100 = builder.create_const_i32(100);
    ValueId c200 = builder.create_const_i32(200);
    ValueId folded_init = builder.create_add(c100, c200); // 300 (constant folded)

    builder.create_store(folded_init, acc_alloca);
    builder.create_store(c0, iter_alloca);

    // Create 22 live variables that span across into the exit block
    std::vector<ValueId> live22;
    ValueId p = fn->arguments[0].id;
    for (int i = 0; i < 22; ++i) {
        ValueId imm = builder.create_const_i32(i * 3 + 1);
        ValueId lv = builder.create_add(p, imm, "live_v_" + std::to_string(i));
        live22.push_back(lv);
        p = lv;
    }

    // Dead code in entry
    ValueId dead_c = builder.create_const_i32(999);
    builder.create_mul(dead_c, dead_c, "dead_square"); // Dead instruction!

    builder.create_jump(b_loop_head);

    // 2. Loop head: check while (iter < limit)
    builder.set_insert_point(b_loop_head);
    ValueId cur_iter = builder.create_load(iter_alloca, "cur_iter");
    ValueId loop_cond = builder.create_icmp_slt(cur_iter, fn->arguments[1].id, "loop_cond");
    builder.create_branch(loop_cond, b_loop_body, b_exit);

    // 3. Loop body: if/else branch based on (cur_iter & 1) == 0
    builder.set_insert_point(b_loop_body);
    ValueId c1 = builder.create_const_i32(1);
    ValueId is_even_bit = builder.create_and(cur_iter, c1, "is_even_bit");
    ValueId is_even = builder.create_icmp_eq(is_even_bit, c0, "is_even");
    builder.create_branch(is_even, b_if_true, b_if_false);

    // If true: acc += cur_iter * 8 (strength reduction target!)
    builder.set_insert_point(b_if_true);
    ValueId cur_acc_t = builder.create_load(acc_alloca, "acc_t");
    ValueId c8 = builder.create_const_i32(8);
    ValueId mul8 = builder.create_mul(cur_iter, c8, "mul8"); // Target for Shl
    ValueId new_acc_t = builder.create_add(cur_acc_t, mul8);
    builder.create_store(new_acc_t, acc_alloca);
    builder.create_jump(b_if_merge);

    // If false: acc += cur_iter + 0 (algebraic simplification target!)
    builder.set_insert_point(b_if_false);
    ValueId cur_acc_f = builder.create_load(acc_alloca, "acc_f");
    ValueId alg_add0 = builder.create_add(cur_iter, c0, "alg_add0"); // Target for alg simp -> cur_iter
    ValueId new_acc_f = builder.create_add(cur_acc_f, alg_add0);
    builder.create_store(new_acc_f, acc_alloca);
    builder.create_jump(b_if_merge);

    // If merge: iter += 1, jmp loop_head
    builder.set_insert_point(b_if_merge);
    ValueId next_iter = builder.create_add(cur_iter, c1, "next_iter");
    builder.create_store(next_iter, iter_alloca);
    builder.create_jump(b_loop_head);

    // Dead block (completely unreachable)
    builder.set_insert_point(b_unreachable);
    builder.create_add(c100, c200);
    builder.create_ret(c0);

    // 4. Exit: consume all 22 live variables and the accumulator
    builder.set_insert_point(b_exit);
    ValueId final_acc = builder.create_load(acc_alloca, "final_acc");

    ValueId sum_live = final_acc;
    for (ValueId lv : live22) {
        sum_live = builder.create_add(sum_live, lv);
    }
    builder.create_ret(sum_live);

    // --- PIPELINE EXECUTION ---
    std::string err;
    std::cout << "[Step 1] Verifying initial IR...\n";
    TEST_ASSERT(fn->verify(err), "Initial verify failed: " + err);

    std::cout << "[Step 2] Running Mem2Reg SSA promotion...\n";
    Mem2RegPass mem2reg;
    bool promoted = mem2reg.run_on_function(*fn);
    TEST_ASSERT(promoted, "Mem2Reg must promote local allocas");
    TEST_ASSERT(fn->verify(err), "Post-Mem2Reg verify failed: " + err);

    std::cout << "[Step 3] Running full PassManager...\n";
    PassManager pm;
    pm.add_standard_optimizations();
    pm.run(*fn);
    TEST_ASSERT(fn->verify(err), "Post-PassManager verify failed: " + err);

    std::cout << "[Step 4] Checking IR dump for optimizations...\n";
    std::string dumped_ir = fn->dump();
    TEST_ASSERT(!dumped_ir.empty(), "Dump must not be empty");

    // Verify dead block was removed
    TEST_ASSERT(dumped_ir.find("dead_block") == std::string::npos, "Dead block must be eliminated by DCE");
    // Verify constant 100 + 200 was folded
    TEST_ASSERT(dumped_ir.find("const i32 300") != std::string::npos, "Const folding must compute 300");

    std::cout << "[Step 5] Running LinearScanAllocator with Spilling...\n";
    LinearScanAllocator allocator(TargetABI::Win64);
    AllocationResult alloc_res = allocator.allocate(*fn);

    std::cout << "  - Spilled variables count: " << alloc_res.spill_count << "\n";
    std::cout << "  - Stack frame size: " << alloc_res.stack_frame_size << " bytes\n";
    std::cout << "  - Callee-saved regs used: " << alloc_res.used_callee_saved.size() << "\n";
    std::cout << "  - Parallel phi copies generated: " << alloc_res.phi_copies.size() << "\n";

    TEST_ASSERT(alloc_res.spill_count > 0, "Spilling MUST occur due to 22+ live variables");
    TEST_ASSERT(alloc_res.stack_frame_size > 0, "Stack frame must be non-zero");
    TEST_ASSERT(alloc_res.stack_frame_size % 16 == 0, "Stack frame must be 16-byte aligned");

    // Non-interference check
    std::unordered_map<PhysReg, std::vector<LiveInterval>> reg_map;
    for (const auto& [vid, iv] : alloc_res.intervals) {
        (void)vid;
        if (!iv.is_spilled && iv.reg != PhysReg::None) {
            reg_map[iv.reg].push_back(iv);
        }
    }

    for (const auto& [reg, list] : reg_map) {
        for (size_t i = 0; i < list.size(); ++i) {
            for (size_t j = i + 1; j < list.size(); ++j) {
                const auto& a = list[i];
                const auto& b = list[j];
                bool conflict = (a.start <= b.end && b.start <= a.end);
                TEST_ASSERT(!conflict, "Physical register assignment conflict detected");
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Test 8: Comprehensive Edge Cases & Calling Conventions
// -----------------------------------------------------------------------------
void test_edge_cases_and_calls() {
    IRModule module("edge_cases_mod");
    IRType* i8_t  = module.context.get_i8_type();
    IRType* i32_t = module.context.get_i32_type();
    IRType* i64_t = module.context.get_i64_type();
    IRType* f32_t = module.context.get_f32_type();
    IRType* f64_t = module.context.get_f64_type();

    // 1. F32 alloca promotion in Mem2Reg (checks for type mismatch bug fix)
    {
        IRFunction* fn_f32 = module.create_function("fn_f32_alloca", f32_t);
        fn_f32->add_argument("cond", module.context.get_i1_type());

        IRBuilder b(fn_f32);
        BasicBlock* bb_entry = b.create_block("entry");
        BasicBlock* bb_true  = b.create_block("then_b");
        BasicBlock* bb_false = b.create_block("else_b");
        BasicBlock* bb_merge = b.create_block("merge_b");

        b.set_insert_point(bb_entry);
        ValueId alloca_f = b.create_alloca(f32_t, 1, 4, "f_var");
        b.create_branch(fn_f32->arguments[0].id, bb_true, bb_false);

        b.set_insert_point(bb_true);
        ValueId v_pi = b.create_const_f32(3.14159f);
        b.create_store(v_pi, alloca_f);
        b.create_jump(bb_merge);

        b.set_insert_point(bb_false);
        ValueId v_e = b.create_const_f32(2.71828f);
        b.create_store(v_e, alloca_f);
        b.create_jump(bb_merge);

        b.set_insert_point(bb_merge);
        ValueId res_f = b.create_load(alloca_f, "loaded_f");
        b.create_ret(res_f);

        std::string err;
        TEST_ASSERT(fn_f32->verify(err), "F32 alloca pre-mem2reg verify");

        Mem2RegPass m2r;
        bool prom = m2r.run_on_function(*fn_f32);
        TEST_ASSERT(prom, "F32 alloca must be promoted");
        TEST_ASSERT(fn_f32->verify(err), "F32 alloca post-mem2reg verify must pass with proper F32 type: " + err);
    }

    // 2. Constant folding edge cases (SExt sign bit, division overflow safety, unary negation)
    {
        IRFunction* fn_cf = module.create_function("fn_cf_edge", i64_t);
        IRBuilder b(fn_cf);
        BasicBlock* entry = b.create_block("entry");
        b.set_insert_point(entry);

        // SExt of i8 -128 (0x80) to i64
        ValueId c_neg128 = b.create_const_int(i8_t, static_cast<int64_t>(0x80));
        ValueId sext_val = b.create_sext(c_neg128, i64_t, "sext_neg128");

        // Neg of 42 -> -42
        ValueId c42 = b.create_const_i64(42);
        ValueId neg_val = b.create_neg(c42, "neg42");

        // FNeg of 3.5 -> -3.5
        ValueId f3_5 = b.create_const_f64(3.5);
        ValueId fneg_val = b.create_fneg(f3_5, "fneg3_5");

        // SDiv INT64_MIN / -1 (must not crash)
        ValueId min_val = b.create_const_i64(INT64_MIN);
        ValueId neg_one = b.create_const_i64(-1);
        ValueId sdiv_overflow = b.create_sdiv(min_val, neg_one, "sdiv_safe");

        // Combine
        ValueId add1 = b.create_add(sext_val, neg_val);
        ValueId f2i = b.create_fptosi(fneg_val, i64_t);
        ValueId add2 = b.create_add(add1, f2i);
        ValueId add3 = b.create_add(add2, sdiv_overflow);
        b.create_ret(add3);

        ConstantFoldingPass cfp;
        cfp.run_on_function(*fn_cf);

        // Verify sext was sign-extended to -128
        const auto& sext_irval = fn_cf->get_value(sext_val);
        TEST_ASSERT(sext_irval.def_inst && sext_irval.def_inst->op == IROp::ConstInt, "SExt must be folded to ConstInt");
        TEST_ASSERT(sext_irval.def_inst->int_imm == -128, "SExt of 0x80 (i8) must equal -128 (i64)");

        // Verify neg was folded
        const auto& neg_irval = fn_cf->get_value(neg_val);
        TEST_ASSERT(neg_irval.def_inst && neg_irval.def_inst->int_imm == -42, "Neg of 42 must be -42");

        // Verify fneg was folded
        const auto& fneg_irval = fn_cf->get_value(fneg_val);
        TEST_ASSERT(fneg_irval.def_inst && fneg_irval.def_inst->float_imm == -3.5, "FNeg of 3.5 must be -3.5");
    }

    // 3. CSE pass edge cases (fcmp olt vs ogt, distinct float constants)
    {
        IRFunction* fn_cse = module.create_function("fn_cse_edge", module.context.get_i1_type());
        fn_cse->add_argument("fa", f64_t);
        fn_cse->add_argument("fb", f64_t);

        IRBuilder b(fn_cse);
        BasicBlock* entry = b.create_block("entry");
        b.set_insert_point(entry);

        ValueId cmp_olt = b.create_fcmp(FCmpCond::OLT, fn_cse->arguments[0].id, fn_cse->arguments[1].id, "cmp_olt");
        ValueId cmp_ogt = b.create_fcmp(FCmpCond::OGT, fn_cse->arguments[0].id, fn_cse->arguments[1].id, "cmp_ogt");
        ValueId cse_res = b.create_and(cmp_olt, cmp_ogt);
        b.create_ret(cse_res);

        CommonSubexpressionEliminationPass cse;
        cse.run_on_function(*fn_cse);

        // cmp_olt and cmp_ogt MUST BOTH REMAIN (different conditions, not CSE candidates!)
        bool has_olt = false;
        bool has_ogt = false;
        for (const auto& inst : entry->instructions) {
            if (inst->op == IROp::FCmp) {
                if (inst->fcmp_cond == FCmpCond::OLT) has_olt = true;
                if (inst->fcmp_cond == FCmpCond::OGT) has_ogt = true;
            }
        }
        TEST_ASSERT(has_olt && has_ogt, "FCmp OLT and OGT with same operands must NOT be falsely merged by CSE");
    }

    // 4. GEP struct field typing and memory operations
    {
        IRType* struct_person = module.context.get_struct_type("PersonRecord", {i32_t, i64_t, f64_t}, {"id", "salary", "rating"});
        IRFunction* fn_gep = module.create_function("fn_gep_struct", i64_t);

        IRBuilder b(fn_gep);
        BasicBlock* entry = b.create_block("entry");
        b.set_insert_point(entry);

        ValueId alloca_struct = b.create_alloca(struct_person, 1, 8, "person");
        ValueId field1_ptr = b.create_gep_struct(alloca_struct, 1, "salary_ptr");

        // Verify that field1_ptr has pointer type to i64
        const IRValue& f1_val = fn_gep->get_value(field1_ptr);
        TEST_ASSERT(f1_val.type && f1_val.type->is_pointer(), "GEP struct must return a pointer");
        TEST_ASSERT(f1_val.type->element_type == i64_t, "GEP struct field 1 element_type must be i64");

        // Create load from field1_ptr -> loaded type must be i64!
        ValueId loaded_salary = b.create_load(field1_ptr, "loaded_salary");
        const IRValue& load_val = fn_gep->get_value(loaded_salary);
        TEST_ASSERT(load_val.type == i64_t, "Load from struct field 1 pointer must have i64 type");

        // Test MemSet and MemCpy
        ValueId zero_byte = b.create_const_i8(0);
        ValueId sz32 = b.create_const_i64(32);
        b.create_memset(alloca_struct, zero_byte, sz32);

        ValueId alloca_dst = b.create_alloca(struct_person, 1, 8, "person_dst");
        b.create_memcpy(alloca_dst, alloca_struct, sz32);

        b.create_ret(loaded_salary);

        std::string err;
        TEST_ASSERT(fn_gep->verify(err), "GEP struct and memcpy/memset IR verification failed: " + err);
    }

    // 5. Calls, indirect calls, and callee-saved preservation across calls
    {
        IRFunction* fn_call = module.create_function("fn_caller", i64_t);
        fn_call->add_argument("arg_val", i64_t);

        IRBuilder b(fn_call);
        BasicBlock* entry = b.create_block("entry");
        b.set_insert_point(entry);

        // Value that crosses a call
        ValueId c7 = b.create_const_i64(7);
        ValueId live_across = b.create_add(fn_call->arguments[0].id, c7, "cross_call_val");

        // Direct call
        ValueId c10 = b.create_const_i64(10);
        ValueId call_res = b.create_call("callee_func", i64_t, {c10}, "call_res");

        // Indirect call
        ValueId fn_ptr = b.create_const_int(module.context.get_ptr_type(), 0x12345678);
        ValueId ind_res = b.create_indirect_call(fn_ptr, i64_t, {call_res}, "ind_res");

        // Use live_across after the calls
        ValueId sum = b.create_add(live_across, ind_res, "final_sum");
        b.create_ret(sum);

        std::string err;
        TEST_ASSERT(fn_call->verify(err), "Call caller verify failed: " + err);

        // Verify dump format of indirect call
        std::string dumped = fn_call->dump();
        TEST_ASSERT(dumped.find("call i64 %") != std::string::npos, "Indirect call must be dumped as 'call i64 %...'");

        // Register allocation with Win64 ABI
        LinearScanAllocator alloc(TargetABI::Win64);
        AllocationResult res = alloc.allocate(*fn_call);

        // Verify that live_across interval crosses call
        const LiveInterval& cross_iv = res.intervals[live_across];
        TEST_ASSERT(cross_iv.crosses_call, "live_across interval must be marked as crossing call");

        // CRITICAL: A variable crossing a call MUST NOT be allocated to caller-saved registers
        std::set<PhysReg> win64_caller_saved = {
            PhysReg::RAX, PhysReg::RCX, PhysReg::RDX,
            PhysReg::R8,  PhysReg::R9,  PhysReg::R10, PhysReg::R11
        };
        if (!cross_iv.is_spilled) {
            TEST_ASSERT(win64_caller_saved.find(cross_iv.reg) == win64_caller_saved.end(),
                        "Variable crossing call MUST NOT be assigned to a caller-saved register");
        }

        // Return value of call (call_res) must NOT be marked as crossing call!
        const LiveInterval& call_res_iv = res.intervals[call_res];
        TEST_ASSERT(!call_res_iv.crosses_call, "Call return value does NOT cross the call that produced it");
    }

    // 6. Extreme XMM register pressure & vector spilling (18 active floats)
    {
        IRFunction* fn_xmm = module.create_function("fn_xmm_pressure", f64_t);
        fn_xmm->add_argument("init_f", f64_t);

        IRBuilder b(fn_xmm);
        BasicBlock* entry = b.create_block("entry");
        b.set_insert_point(entry);

        std::vector<ValueId> live_floats;
        ValueId prev_f = fn_xmm->arguments[0].id;
        for (int i = 0; i < 18; ++i) {
            ValueId imm = b.create_const_f64(static_cast<double>(i + 1) * 0.5);
            ValueId v = b.create_fadd(prev_f, imm, "flive_" + std::to_string(i));
            live_floats.push_back(v);
            prev_f = v;
        }

        ValueId f_accum = live_floats[0];
        for (size_t i = 1; i < live_floats.size(); ++i) {
            f_accum = b.create_fadd(f_accum, live_floats[i]);
        }
        b.create_ret(f_accum);

        std::string err;
        TEST_ASSERT(fn_xmm->verify(err), "XMM pressure verify");

        LinearScanAllocator alloc_win(TargetABI::Win64);
        AllocationResult win_res = alloc_win.allocate(*fn_xmm);
        TEST_ASSERT(win_res.spill_count > 0, "18 active floats must trigger XMM spilling");
        TEST_ASSERT(win_res.stack_frame_size % 16 == 0, "XMM spill stack frame must be 16-byte aligned");
    }

    // 7. CFGSimplify block merge updating successor Phi nodes
    {
        IRFunction* fn_merge = module.create_function("fn_phi_merge", i32_t);
        fn_merge->add_argument("c", module.context.get_i1_type());

        IRBuilder b(fn_merge);
        BasicBlock* bb_entry = b.create_block("entry");
        BasicBlock* bb_a     = b.create_block("bb_a");
        BasicBlock* bb_b     = b.create_block("bb_b");
        BasicBlock* bb_c     = b.create_block("bb_c");
        BasicBlock* bb_merge = b.create_block("bb_merge");

        // entry: br cond, bb_a, bb_c
        b.set_insert_point(bb_entry);
        b.create_branch(fn_merge->arguments[0].id, bb_a, bb_c);

        // bb_a -> bb_b (single succ, single pred: perfect merge candidate!)
        b.set_insert_point(bb_a);
        ValueId va = b.create_const_i32(111);
        b.create_jump(bb_b);

        // bb_b -> bb_merge
        b.set_insert_point(bb_b);
        ValueId vb = b.create_add(va, va, "vb");
        b.create_jump(bb_merge);

        // bb_c -> bb_merge
        b.set_insert_point(bb_c);
        ValueId vc = b.create_const_i32(222);
        b.create_jump(bb_merge);

        // bb_merge: phi [vb, bb_b], [vc, bb_c]
        b.set_insert_point(bb_merge);
        ValueId phi_m = b.create_phi(i32_t, "phi_m");
        b.add_phi_incoming(phi_m, bb_b, vb);
        b.add_phi_incoming(phi_m, bb_c, vc);
        b.create_ret(phi_m);

        std::string err;
        TEST_ASSERT(fn_merge->verify(err), "CFG merge pre-verify");

        CFGSimplifyPass cfg_simp;
        bool merged = cfg_simp.run_on_function(*fn_merge);
        TEST_ASSERT(merged, "CFGSimplify must merge bb_a and bb_b");

        // Verify that bb_merge's phi has bb_a as incoming block (NOT bb_b which was erased!)
        bool verify_post = fn_merge->verify(err);
        if (!verify_post) std::cerr << "CFGSimplify verify error: " << err << "\n";
        TEST_ASSERT(verify_post, "CFGSimplify must maintain Phi CFG predecessor integrity");
    }
}

// -----------------------------------------------------------------------------
// Main Test Runner
// -----------------------------------------------------------------------------
int main() {
    std::cout << "========================================================\n";
    std::cout << "Starting GrassIRSSA Compiler Infrastructure Test Suite\n";
    std::cout << "========================================================\n";

    RUN_TEST(test_type_system);
    RUN_TEST(test_builder_and_verifier);
    RUN_TEST(test_mem2reg);
    RUN_TEST(test_optimizer_passes);
    RUN_TEST(test_linear_scan_and_spilling);
    RUN_TEST(test_phi_resolution);
    RUN_TEST(test_complex_end_to_end);
    RUN_TEST(test_edge_cases_and_calls);

    std::cout << "========================================================\n";
    std::cout << "All " << g_pass_count << " / " << g_test_count << " test suites passed successfully (100%)!\n";
    std::cout << "========================================================\n";
    return 0;
}
