#include <iostream>
#include <cassert>
#include <string>

#include "native/GrassIR.h"
#include "native/GrassAdvancedOpt.h"

using namespace grass::ir;
using namespace grass::opt;

static void test_function_inlining() {
    std::cout << "Running Test 1: Function Inlining..." << std::endl;

    IRModule module("inline_module");
    IRType* i64_t = module.context.get_i64_type();

    // Small function: add_small(a, b) -> a + b (2 instructions)
    IRFunction* add_small = module.create_function("add_small", i64_t);
    add_small->add_argument("a", i64_t);
    add_small->add_argument("b", i64_t);
    BasicBlock* add_entry = add_small->create_block("entry");
    IRBuilder b_add(add_small);
    b_add.set_insert_point(add_entry);
    ValueId sum = b_add.create_add(add_small->arguments[0].id, add_small->arguments[1].id, "sum");
    b_add.create_ret(sum);

    // Caller function: caller_func(x, y) -> add_small(x, y) * x
    IRFunction* caller = module.create_function("caller_func", i64_t);
    caller->add_argument("x", i64_t);
    caller->add_argument("y", i64_t);
    BasicBlock* caller_entry = caller->create_block("entry");
    IRBuilder b_caller(caller);
    b_caller.set_insert_point(caller_entry);
    ValueId call_val = b_caller.create_call("add_small", i64_t, {caller->arguments[0].id, caller->arguments[1].id}, "call_add");
    ValueId prod = b_caller.create_mul(call_val, caller->arguments[0].id, "prod");
    b_caller.create_ret(prod);

    // Recursive function: should NOT be inlined
    IRFunction* rec_fn = module.create_function("rec_fn", i64_t);
    rec_fn->add_argument("n", i64_t);
    BasicBlock* rec_entry = rec_fn->create_block("entry");
    BasicBlock* rec_base = rec_fn->create_block("base");
    BasicBlock* rec_step = rec_fn->create_block("step");
    IRBuilder b_rec(rec_fn);
    b_rec.set_insert_point(rec_entry);
    ValueId one = b_rec.create_const_i64(1);
    ValueId is_base = b_rec.create_icmp_sle(rec_fn->arguments[0].id, one, "cmp");
    b_rec.create_branch(is_base, rec_base, rec_step);

    b_rec.set_insert_point(rec_base);
    b_rec.create_ret(one);

    b_rec.set_insert_point(rec_step);
    ValueId sub_one = b_rec.create_sub(rec_fn->arguments[0].id, one, "n_minus_1");
    ValueId rec_call = b_rec.create_call("rec_fn", i64_t, {sub_one}, "rec_call");
    ValueId rec_prod = b_rec.create_mul(rec_fn->arguments[0].id, rec_call, "rec_prod");
    b_rec.create_ret(rec_prod);

    // Caller of recursive function
    IRFunction* caller_rec = module.create_function("caller_rec", i64_t);
    caller_rec->add_argument("z", i64_t);
    BasicBlock* cr_entry = caller_rec->create_block("entry");
    IRBuilder b_cr(caller_rec);
    b_cr.set_insert_point(cr_entry);
    ValueId r_val = b_cr.create_call("rec_fn", i64_t, {caller_rec->arguments[0].id}, "rcall");
    b_cr.create_ret(r_val);

    // Large function (> 30 instructions): should NOT be inlined
    IRFunction* large_fn = module.create_function("large_fn", i64_t);
    large_fn->add_argument("p", i64_t);
    BasicBlock* large_entry = large_fn->create_block("entry");
    IRBuilder b_large(large_fn);
    b_large.set_insert_point(large_entry);
    ValueId cur = large_fn->arguments[0].id;
    for (int i = 0; i < 35; ++i) {
        ValueId c_i = b_large.create_const_i64(1);
        cur = b_large.create_add(cur, c_i, "add_" + std::to_string(i));
    }
    b_large.create_ret(cur);

    // Caller of large function
    IRFunction* caller_large = module.create_function("caller_large", i64_t);
    caller_large->add_argument("q", i64_t);
    BasicBlock* cl_entry = caller_large->create_block("entry");
    IRBuilder b_cl(caller_large);
    b_cl.set_insert_point(cl_entry);
    ValueId l_val = b_cl.create_call("large_fn", i64_t, {caller_large->arguments[0].id}, "lcall");
    b_cl.create_ret(l_val);

    // Run Inlining Pass
    bool inlined = GrassAdvancedOpt::run_inlining(module, 30);
    assert(inlined && "Expected inlining to perform changes");

    // 1. Verify caller_func: call to add_small has been inlined
    bool has_add_small_call = false;
    bool has_add_inst = false;
    for (const auto& bb : caller->blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Call && inst->callee_name == "add_small") {
                has_add_small_call = true;
            }
            if (inst->op == IROp::Add) {
                has_add_inst = true;
            }
        }
    }
    assert(!has_add_small_call && "call to add_small must be inlined");
    assert(has_add_inst && "Add instruction must now be in caller_func");

    // 2. Verify recursive function call was NOT inlined
    bool has_rec_call = false;
    for (const auto& bb : caller_rec->blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Call && inst->callee_name == "rec_fn") {
                has_rec_call = true;
            }
        }
    }
    assert(has_rec_call && "recursive function call must NOT be inlined");

    // 3. Verify large function call was NOT inlined
    bool has_large_call = false;
    for (const auto& bb : caller_large->blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Call && inst->callee_name == "large_fn") {
                has_large_call = true;
            }
        }
    }
    assert(has_large_call && "large function (> 30 insts) call must NOT be inlined");

    std::string err;
    assert(module.verify(err) && "Module verification failed after inlining");
    std::cout << "[PASS] Test 1: Function Inlining passed." << std::endl;
}

static void test_licm() {
    std::cout << "Running Test 2: Loop-Invariant Code Motion (LICM)..." << std::endl;

    IRModule module("licm_module");
    IRType* i64_t = module.context.get_i64_type();

    // calc(a, b)
    IRFunction* fn = module.create_function("calc", i64_t);
    fn->add_argument("a", i64_t);
    fn->add_argument("b", i64_t);

    BasicBlock* entry = fn->create_block("entry");
    BasicBlock* header = fn->create_block("header");
    BasicBlock* body = fn->create_block("body");
    BasicBlock* exit = fn->create_block("exit");

    IRBuilder b(fn);

    // Entry
    b.set_insert_point(entry);
    ValueId init_i = b.create_const_i64(0);
    ValueId init_acc = b.create_const_i64(0);
    b.create_jump(header);

    // Header
    b.set_insert_point(header);
    ValueId phi_i = b.create_phi(i64_t, "i");
    ValueId phi_acc = b.create_phi(i64_t, "acc");
    ValueId limit = b.create_const_i64(100);
    ValueId cmp = b.create_icmp_slt(phi_i, limit, "cmp");
    b.create_branch(cmp, body, exit);

    // Body
    b.set_insert_point(body);
    // Invariant computations
    ValueId inv1 = b.create_mul(fn->arguments[0].id, fn->arguments[1].id, "inv_mul");
    ValueId c42 = b.create_const_i64(42);
    ValueId inv2 = b.create_add(inv1, c42, "inv_add");
    ValueId c7 = b.create_const_i64(7);
    ValueId inv3 = b.create_xor(inv2, c7, "inv_xor");

    // Variant computations
    ValueId v1 = b.create_add(phi_acc, inv3, "acc_v1");
    ValueId next_acc = b.create_add(v1, phi_i, "next_acc");
    ValueId c1 = b.create_const_i64(1);
    ValueId next_i = b.create_add(phi_i, c1, "next_i");
    b.create_jump(header);

    // Exit
    b.set_insert_point(exit);
    b.create_ret(phi_acc);

    // Fill incoming for Phis
    b.add_phi_incoming(phi_i, entry, init_i);
    b.add_phi_incoming(phi_i, body, next_i);

    b.add_phi_incoming(phi_acc, entry, init_acc);
    b.add_phi_incoming(phi_acc, body, next_acc);

    fn->rebuild_cfg();
    fn->rebuild_def_use();

    size_t body_inst_count_before = body->instructions.size();
    assert(body_inst_count_before >= 7 && "Body must have at least 7 instructions before LICM");

    // Run LICM
    bool changed = GrassAdvancedOpt::run_licm(*fn);
    assert(changed && "LICM must perform changes");

    size_t body_inst_count_after = body->instructions.size();
    assert(body_inst_count_after < body_inst_count_before &&
           "LICM must decrease the number of instructions in loop body");

    // Check that invariant calculations are no longer in body
    for (const auto& inst : body->instructions) {
        assert(inst->result_id != inv1 && "inv1 must have been hoisted out of loop body");
        assert(inst->result_id != inv2 && "inv2 must have been hoisted out of loop body");
        assert(inst->result_id != inv3 && "inv3 must have been hoisted out of loop body");
    }

    std::string err;
    assert(fn->verify(err) && "Function verification failed after LICM");
    std::cout << "[PASS] Test 2: Loop-Invariant Code Motion (LICM) passed (body instructions reduced from "
              << body_inst_count_before << " to " << body_inst_count_after << ")." << std::endl;
}

static void test_loop_unrolling() {
    std::cout << "Running Test 3: Loop Unrolling..." << std::endl;

    IRModule module("unroll_module");
    IRType* i64_t = module.context.get_i64_type();

    IRFunction* fn = module.create_function("loop_fn", i64_t);
    fn->add_argument("mult", i64_t);

    BasicBlock* entry = fn->create_block("entry");
    BasicBlock* header = fn->create_block("header");
    BasicBlock* body = fn->create_block("body");
    BasicBlock* exit = fn->create_block("exit");

    IRBuilder b(fn);

    // Entry
    b.set_insert_point(entry);
    ValueId init_i = b.create_const_i64(0);
    ValueId init_sum = b.create_const_i64(0);
    b.create_jump(header);

    // Header
    b.set_insert_point(header);
    ValueId phi_i = b.create_phi(i64_t, "i");
    ValueId phi_sum = b.create_phi(i64_t, "sum");
    ValueId limit = b.create_const_i64(10); // trip count = 10 (4 * 2 + 2)
    ValueId cmp = b.create_icmp_slt(phi_i, limit, "cmp");
    b.create_branch(cmp, body, exit);

    // Body
    b.set_insert_point(body);
    ValueId term = b.create_mul(phi_i, fn->arguments[0].id, "term");
    ValueId next_sum = b.create_add(phi_sum, term, "next_sum");
    ValueId c1 = b.create_const_i64(1);
    ValueId next_i = b.create_add(phi_i, c1, "next_i");
    b.create_jump(header);

    // Exit
    b.set_insert_point(exit);
    b.create_ret(phi_sum);

    b.add_phi_incoming(phi_i, entry, init_i);
    b.add_phi_incoming(phi_i, body, next_i);
    b.add_phi_incoming(phi_sum, entry, init_sum);
    b.add_phi_incoming(phi_sum, body, next_sum);

    fn->rebuild_cfg();
    fn->rebuild_def_use();

    size_t body_inst_count_before = body->instructions.size();

    // Run Loop Unrolling with factor 4
    bool unrolled = GrassAdvancedOpt::run_loop_unrolling(*fn, 4);
    assert(unrolled && "Loop unrolling must succeed on trip count 10");

    size_t body_inst_count_after = body->instructions.size();
    // Body should now contain 4 unrolled copies (approximately 4x instructions)
    assert(body_inst_count_after > body_inst_count_before &&
           "Unrolled body should contain instructions for 4 iterations");

    // Check if remainder tail block was created
    bool found_tail = false;
    for (const auto& bb : fn->blocks) {
        if (bb->name.find("tail") != std::string::npos) {
            found_tail = true;
            break;
        }
    }
    assert(found_tail && "Tail block for remainder iterations (10 % 4 = 2) must be present");

    std::string err;
    if (!fn->verify(err)) {
        std::cerr << "Verification error in Loop Unrolling: " << err << std::endl;
        std::cerr << "Function dump:\n" << fn->dump() << std::endl;
    }
    assert(fn->verify(err) && "Function verification failed after Loop Unrolling");
    std::cout << "[PASS] Test 3: Loop Unrolling passed (body unrolled by 4x, remainder tail generated)." << std::endl;
}

static bool is_const_int_val(const IRFunction& fn, ValueId vid, int64_t& out_val) {
    if (!fn.has_value(vid)) return false;
    const IRInst* def = fn.get_value(vid).def_inst;
    if (def && def->op == IROp::ConstInt) {
        out_val = def->int_imm;
        return true;
    }
    return false;
}

static void test_strength_reduction() {
    std::cout << "Running Test 4: Strength Reduction and Algebraic Identities..." << std::endl;

    IRModule module("strength_module");
    IRType* i64_t = module.context.get_i64_type();

    IRFunction* fn = module.create_function("opt_math", i64_t);
    fn->add_argument("x", i64_t);
    fn->add_argument("y", i64_t);

    BasicBlock* entry = fn->create_block("entry");
    IRBuilder b(fn);
    b.set_insert_point(entry);

    ValueId x = fn->arguments[0].id;
    ValueId y = fn->arguments[1].id;

    // 1. mul x, 8 -> shl x, 3
    ValueId c8 = b.create_const_i64(8);
    ValueId m1 = b.create_mul(x, c8, "m1");

    // 2. mul 16, y -> shl y, 4
    ValueId c16 = b.create_const_i64(16);
    ValueId m2 = b.create_mul(c16, y, "m2");

    // 3. udiv x, 4 -> lshr x, 2
    ValueId c4 = b.create_const_i64(4);
    ValueId d1 = b.create_udiv(x, c4, "d1");

    // 4. urem x, 8 -> and x, 7
    ValueId r1 = b.create_urem(x, c8, "r1");

    // 5. x + 0 -> x
    ValueId c0 = b.create_const_i64(0);
    ValueId a1 = b.create_add(x, c0, "a1");

    // 6. x * 1 -> x
    ValueId c1 = b.create_const_i64(1);
    ValueId m3 = b.create_mul(x, c1, "m3");

    // 7. x * 0 -> 0
    ValueId m4 = b.create_mul(x, c0, "m4");

    // 8. x - x -> 0
    ValueId s1 = b.create_sub(x, x, "s1");

    // 9. x ^ x -> 0
    ValueId x1 = b.create_xor(x, x, "x1");

    // Accumulate results
    ValueId sum1 = b.create_add(m1, m2, "sum1");
    ValueId sum2 = b.create_add(sum1, d1, "sum2");
    ValueId sum3 = b.create_add(sum2, r1, "sum3");
    ValueId sum4 = b.create_add(sum3, a1, "sum4");
    ValueId sum5 = b.create_add(sum4, m3, "sum5");
    ValueId sum6 = b.create_add(sum5, m4, "sum6");
    ValueId sum7 = b.create_add(sum6, s1, "sum7");
    ValueId sum8 = b.create_add(sum7, x1, "sum8");
    b.create_ret(sum8);

    fn->rebuild_cfg();
    fn->rebuild_def_use();

    bool changed = GrassAdvancedOpt::run_strength_reduction(*fn);
    assert(changed && "Strength reduction must perform optimizations");

    // Verify instructions
    bool found_shl_3 = false;
    bool found_shl_4 = false;
    bool found_lshr_2 = false;
    bool found_and_7 = false;

    for (const auto& inst : entry->instructions) {
        if (inst->result_id == m1 && inst->op == IROp::Shl) {
            int64_t sh = 0;
            if (is_const_int_val(*fn, inst->operands[1], sh) && sh == 3) {
                found_shl_3 = true;
            }
        }
        if (inst->result_id == m2 && inst->op == IROp::Shl) {
            int64_t sh = 0;
            if (is_const_int_val(*fn, inst->operands[1], sh) && sh == 4) {
                found_shl_4 = true;
            }
        }
        if (inst->result_id == d1 && inst->op == IROp::LShr) {
            int64_t sh = 0;
            if (is_const_int_val(*fn, inst->operands[1], sh) && sh == 2) {
                found_lshr_2 = true;
            }
        }
        if (inst->result_id == r1 && inst->op == IROp::And) {
            int64_t msk = 0;
            if (is_const_int_val(*fn, inst->operands[1], msk) && msk == 7) {
                found_and_7 = true;
            }
        }
    }

    assert(found_shl_3 && "mul x, 8 must be converted to shl x, 3");
    assert(found_shl_4 && "mul 16, y must be converted to shl y, 4");
    assert(found_lshr_2 && "udiv x, 4 must be converted to lshr x, 2");
    assert(found_and_7 && "urem x, 8 must be converted to and x, 7");

    std::string err;
    assert(fn->verify(err) && "Function verification failed after Strength Reduction");
    std::cout << "[PASS] Test 4: Strength Reduction and Algebraic Identities passed." << std::endl;
}

static void test_full_optimization_pipeline() {
    std::cout << "Running Test 5: Full Optimization Pipeline (GrassAdvancedOpt::optimize)..." << std::endl;

    IRModule module("full_test_module");
    IRType* i64_t = module.context.get_i64_type();

    // 1. Small leaf function: step_func(val) -> val * 8 + 0
    IRFunction* step_fn = module.create_function("step_func", i64_t);
    step_fn->add_argument("val", i64_t);
    BasicBlock* s_entry = step_fn->create_block("entry");
    IRBuilder b_s(step_fn);
    b_s.set_insert_point(s_entry);
    ValueId c8 = b_s.create_const_i64(8);
    ValueId m8 = b_s.create_mul(step_fn->arguments[0].id, c8, "m8");
    ValueId c0 = b_s.create_const_i64(0);
    ValueId add0 = b_s.create_add(m8, c0, "add0");
    b_s.create_ret(add0);

    // 2. Main compute function with loop and invariant math:
    IRFunction* main_fn = module.create_function("main_compute", i64_t);
    main_fn->add_argument("scale", i64_t);
    main_fn->add_argument("offset", i64_t);

    BasicBlock* m_entry = main_fn->create_block("entry");
    BasicBlock* m_header = main_fn->create_block("header");
    BasicBlock* m_body = main_fn->create_block("body");
    BasicBlock* m_exit = main_fn->create_block("exit");

    IRBuilder b_m(main_fn);

    // Entry
    b_m.set_insert_point(m_entry);
    ValueId init_i = b_m.create_const_i64(0);
    ValueId init_acc = b_m.create_const_i64(0);
    b_m.create_jump(m_header);

    // Header
    b_m.set_insert_point(m_header);
    ValueId phi_i = b_m.create_phi(i64_t, "i");
    ValueId phi_acc = b_m.create_phi(i64_t, "acc");
    ValueId limit = b_m.create_const_i64(16); // 16 iterations (divisible by 4)
    ValueId cmp = b_m.create_icmp_slt(phi_i, limit, "cmp");
    b_m.create_branch(cmp, m_body, m_exit);

    // Body
    b_m.set_insert_point(m_body);
    // Invariant expression:
    ValueId inv_term = b_m.create_mul(main_fn->arguments[0].id, main_fn->arguments[1].id, "inv_prod");
    // Call to small function step_func:
    ValueId call_res = b_m.create_call("step_func", i64_t, {phi_i}, "call_step");
    ValueId partial = b_m.create_add(phi_acc, call_res, "partial");
    ValueId next_acc = b_m.create_add(partial, inv_term, "next_acc");
    ValueId c1 = b_m.create_const_i64(1);
    ValueId next_i = b_m.create_add(phi_i, c1, "next_i");
    b_m.create_jump(m_header);

    // Exit
    b_m.set_insert_point(m_exit);
    b_m.create_ret(phi_acc);

    b_m.add_phi_incoming(phi_i, m_entry, init_i);
    b_m.add_phi_incoming(phi_i, m_body, next_i);
    b_m.add_phi_incoming(phi_acc, m_entry, init_acc);
    b_m.add_phi_incoming(phi_acc, m_body, next_acc);

    main_fn->rebuild_cfg();
    main_fn->rebuild_def_use();

    // Run Full Optimization Pipeline
    bool optimized = GrassAdvancedOpt::optimize(module, 2);
    assert(optimized && "optimize(module, 2) must return true for optimizable code");

    // Verify that call to step_func was inlined
    bool has_step_call = false;
    for (const auto& bb : main_fn->blocks) {
        for (const auto& inst : bb->instructions) {
            if (inst->op == IROp::Call && inst->callee_name == "step_func") {
                has_step_call = true;
            }
        }
    }
    assert(!has_step_call && "step_func must be inlined into main_compute");

    std::string err;
    assert(module.verify(err) && "Module verification failed after GrassAdvancedOpt::optimize");
    std::cout << "[PASS] Test 5: Full Optimization Pipeline (GrassAdvancedOpt::optimize) passed." << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "GrassAdvancedOpt SSA IR Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    test_function_inlining();
    test_licm();
    test_loop_unrolling();
    test_strength_reduction();
    test_full_optimization_pipeline();

    std::cout << "========================================" << std::endl;
    std::cout << "All 5 tests passed successfully!" << std::endl;
    std::cout << "========================================" << std::endl;
    return 0;
}
