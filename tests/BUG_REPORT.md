# GrassGreen v4.2.0 Compiler Audit & Vulnerability Report

**Date:** 2026-11-22  
**Target:** GrassGreen Compiler (`bin/grassc.exe` v4.2.0)  
**Target Architectures:** x86_64 Win64 ABI, GrassVM Bytecode Interpreter  
**Auditor:** DeepMind Antigravity Audit Agent  
**Status:** Completed (Strict 0-Emoji Policy Enforced)  

---

## 1. Executive Summary

A comprehensive, adversarial audit of the GrassGreen v4.2.0 compiler was conducted. The investigation spanned the full compilation pipeline:
1. Syntax and lexical analysis (`src/GrassSyntax.cpp`, `src/GrassSyntax.h`).
2. Semantic analysis and type checking (`src/GrassSema.cpp`, `src/GrassSema.h`).
3. SSA Intermediate Representation and optimization passes (`src/GrassIR.cpp`, `src/GrassIR.h`).
4. Native machine code generator and register allocation (`src/GrassNativeCodegen.cpp`, `src/GrassX86Encoder.cpp`, `src/GrassObjWriter.cpp`).
5. OOP and memory management runtime (`src/GrassNativeOOP.cpp`, `src/GrassNativeMemory.cpp`, `bin/grass_native_runtime.c`).
6. Bytecode VM runtime (`src/grassc.cpp`).

The audit uncovered **17 distinct vulnerabilities and compiler flaws**, including 6 CRITICAL defects, 6 HIGH-severity flaws, and 5 MEDIUM-severity issues. The critical defects range from Win64 ABI parameter displacement (causing arguments 5+ to read garbage/aliased stack values) and silent register clobbering in nested loops (causing infinite execution hangs), to unhandled hardware exception traps, stack overflow compiler crashes, and silent memory leaks due to omitted RAII destructors.

All defects were verified using standalone reproductions in `tests/audit/repro_*.grass` and automated via the differential test runner `tests/audit/run_audit.py`.

---

## 2. Differential Testing Matrix & Fuzzing Results

The automated test suite `tests/audit/run_audit.py` executed 34 differential test programs comparing bytecode execution (`-r`) against the standalone native x86_64 executable (`--native`), along with 8 crash/fuzzing negative test programs.

### Summary Statistics

| Metric | Count | Percentage |
| :--- | :---: | :---: |
| **Total Differential Tests** | **34** | 100.0% |
| - Exact Output & Exit Code Matches | 6 | 17.6% |
| - Value / Stdout Mismatches | 13 | 38.2% |
| - Native Codegen / Link Failures | 10 | 29.4% |
| - Bytecode VM Failures | 1 | 2.9% |
| - Both Backends Failed | 2 | 5.9% |
| - Process Timeouts / Infinite Hangs | 2 | 5.9% |
| **Fuzzing Negative Tests** | **8** | 100.0% |
| - Cleanly Handled (Error Diagnostic) | 7 | 87.5% |
| - Compiler Crash / Stack Overflow (Hang) | 1 | 12.5% |
| **Standalone Reproduction Files** | **16** | 100.0% |

---

## 3. Critical Vulnerabilities & Crashes (CRITICAL)

---

### BUG-01: Win64 ABI Stack Parameter Displacement (Caller Shadow Space Read)

- **Severity:** CRITICAL
- **Component:** Native Code Generator (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:2018-2022`
- **Minimal Reproduction File:** `tests/audit/repro_win64_args.grass`

#### Vulnerability Description
Under the Microsoft x64 calling convention (Win64 ABI), the first 4 integer/pointer arguments are passed in registers (`%rcx`, `%rdx`, `%r8`, `%r9`). The caller must allocate a 32-byte shadow store (home space) on the stack immediately preceding any additional stack arguments. Consequently:
- Return address is at `[RSP + 0]` on callee entry.
- After `push %rbp; mov %rsp, %rbp`, the caller's shadow space occupies `[RBP + 16]` through `[RBP + 40]`.
- Stack parameter 5 resides at `[RBP + 48]` (`16 + 32`).
- Stack parameter 6 resides at `[RBP + 56]`.

In `GrassNativeCodegen.cpp:2019`, the callee's parameter placement loop is implemented as:
```cpp
int32_t stack_off = 16 + (i - arg_regs.size()) * 8;
encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, stack_off, 8));
```
For argument 5 (`i = 4`, `arg_regs.size() = 4`), `stack_off = 16 + 0 = 16`.
For argument 6 (`i = 5`), `stack_off = 16 + 8 = 24`.
For argument 7 (`i = 6`), `stack_off = 16 + 16 = 32`.
For argument 8 (`i = 7`), `stack_off = 16 + 24 = 40`.

The callee reads from `[RBP + 16]` to `[RBP + 40]`, which is the caller's 32-byte shadow space where the caller spilled arguments 1, 2, 3, and 4.
As a result, arguments 5, 6, 7, and 8 receive duplicate values of arguments 1, 2, 3, and 4 instead of the actual arguments passed! In methods, where argument 1 is `this`, argument 5 receives the `this` pointer instead of user data.

#### Observed vs Expected Output
- **GrassVM Output:** `10 20 30 40 50 60 70 80`, `result = 360`
- **Native Output:** `10 20 30 40 10 20 30 40`, `result = 200`

#### Concrete Fix
In `src/GrassNativeCodegen.cpp`, lines 2018-2022, adjust the stack offset to account for the 32-byte shadow space when targeting Win64:
```cpp
// src/GrassNativeCodegen.cpp:2018
} else {
    int32_t shadow_disp = (options_.abi == TargetABI::Win64) ? 32 : 0;
    int32_t stack_off = 16 + shadow_disp + static_cast<int32_t>(i - arg_regs.size()) * 8;
    backend::Reg tmp = backend::Reg::R10;
    encoder.mov_rm(tmp, backend::mem(backend::Reg::RBP, stack_off, 8));
    store_val(fn->arguments[i].id, tmp);
}
```

---

### BUG-02: Callee RAX Register Clobbering in Comparisons (Infinite Loops)

- **Severity:** CRITICAL
- **Component:** Native Code Generator & Register Allocator (`src/GrassNativeCodegen.cpp`, `src/GrassIR.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:2271, 2533`, `src/GrassIR.cpp:2986`
- **Minimal Reproduction File:** `tests/audit/repro_rax_clobber.grass`

#### Vulnerability Description
In `src/GrassNativeCodegen.cpp:2271` and `2533`, integer and floating-point comparison instructions (`IROp::Icmp`, `IROp::Fcmp`) lower conditional set instructions by hardcoding the destination register to `backend::Reg::AL`:
```cpp
encoder.setcc_r(cc, backend::Reg::AL);
encoder.movzx_rr8(dst_r, backend::Reg::AL);
```
However, in `src/GrassIR.cpp:2986`, `backend::Reg::RAX` is listed in `gpr_caller_saved` as an allocatable register for user variables:
```cpp
const std::vector<PhysReg> gpr_caller_saved = {
    PhysReg::RAX, PhysReg::RCX, PhysReg::RDX, PhysReg::R8, PhysReg::R9, PhysReg::R10, PhysReg::R11
};
```
When linear scan register allocation assigns `PhysReg::RAX` to a variable (e.g. an inner loop counter `j`), any comparison (`j < 5` or `j == 3`) executes `setcc_r(..., Reg::AL)`, which immediately overwrites the lowest byte of `%rax` with 0 or 1. The loop counter is corrupted on every iteration, resulting in an infinite execution hang.

#### Observed vs Expected Output
- **GrassVM Output:** `count = 10` (executes in 0.01s)
- **Native Output:** Hangs indefinitely (timed out after 3.0s)

#### Concrete Fix
1. Either exclude `PhysReg::RAX` from allocatable general-purpose registers in `src/GrassIR.cpp`:
```cpp
// src/GrassIR.cpp:2986
// Exclude RAX from general allocation because it is reserved for call returns, idiv, and setcc
const std::vector<PhysReg> gpr_caller_saved = {
    PhysReg::RCX, PhysReg::RDX, PhysReg::R8, PhysReg::R9, PhysReg::R10, PhysReg::R11
};
```
2. Or in `src/GrassNativeCodegen.cpp:2271`, emit `setcc_r` directly into the byte sub-register of `dst_r` (e.g. `dst_r` as low-8 register) if `dst_r` is a legacy byte register, or spill/restore `RAX` around `setcc_r`.

---

### BUG-03: Floating-Point Arithmetic Lowering Emits Integer GPR Instructions (Codegen Crash)

- **Severity:** CRITICAL
- **Component:** AST Lowerer & X86 Machine Encoder (`src/GrassNativeCodegen.cpp`, `src/GrassX86Encoder.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1348-1350, 1364-1365`
- **Minimal Reproduction File:** `tests/audit/repro_float_arith.grass`

#### Vulnerability Description
In `ASTLowerer::parse_additive` and `ASTLowerer::parse_multiplicative`, binary operations (`+`, `-`, `*`, `/`) unconditionally generate integer IR operations:
```cpp
// src/GrassNativeCodegen.cpp:1348
if (op == TOK_PLUS) lhs = builder_.create_add(lhs, rhs);
else if (op == TOK_MINUS) lhs = builder_.create_sub(lhs, rhs);
```
Even if `lhs` and `rhs` are `f64` or `f32`, integer opcodes `IROp::Add`, `IROp::Sub`, `IROp::Mul`, `IROp::Sdiv` are emitted into the SSA IR.
During native instruction selection (`GrassNativeCodegen.cpp:2240`), integer ops invoke `get_reg(lhs)` and `get_reg(rhs)`. The register allocator assigns XMM registers (`XMM0`-`XMM5`) to floating-point values. When the integer instruction tries to validate operands with `validate_arch_gpr`, an unhandled C++ exception is thrown:
```
std::invalid_argument: "Expected general purpose register"
```
The compiler aborts build with `Build error: Codegen Error: In block 'entry' inst op=4: Expected general purpose register`.

#### Observed vs Expected Output
- **GrassVM Output:** `c = 4`
- **Native Output:** `Build error: Codegen Error: In block 'entry' inst op=4: Expected general purpose register`

#### Concrete Fix
In `src/GrassNativeCodegen.cpp`, check operand types in `parse_additive` and `parse_multiplicative`:
```cpp
// src/GrassNativeCodegen.cpp:1348
bool is_fp = (builder_.get_function()->get_value(lhs).type->is_floating_point() ||
              builder_.get_function()->get_value(rhs).type->is_floating_point());
if (is_fp) {
    if (op == TOK_PLUS) lhs = builder_.create_fadd(lhs, rhs);
    else if (op == TOK_MINUS) lhs = builder_.create_fsub(lhs, rhs);
} else {
    if (op == TOK_PLUS) lhs = builder_.create_add(lhs, rhs);
    else if (op == TOK_MINUS) lhs = builder_.create_sub(lhs, rhs);
}
```

---

### BUG-04: Sema Stack Overflow Crash on Cyclic Inheritance Hierarchy

- **Severity:** CRITICAL
- **Component:** Semantic Analyzer (`src/GrassSema.cpp`)
- **Exact Location:** `src/GrassSema.cpp:409-417, 967-986`
- **Minimal Reproduction File:** `tests/audit/repro_cyclic_class.grass`

#### Vulnerability Description
In `GrassSema::checkClassDecl`, class declarations verify that `base_class` is declared, but perform no cycle detection:
```cpp
// src/GrassSema.cpp:415
if (!cls->base_class.empty() && !structs_.count(cls->base_class)) {
    reportError(cls->loc, "Base class '" + cls->base_class + "' not found...");
}
```
If two classes inherit cyclically (`class A : B {} class B : A {}`), the symbol table accepts both.
Later, when a member expression `a.unknown` is checked in `checkMemberExpr`:
```cpp
// src/GrassSema.cpp:967
auto find_field = [this](const StructSymbol* s, const std::string& field_name, auto& self) -> const SemaType* {
    auto fit = s->fields.find(field_name);
    if (fit != s->fields.end()) return &fit->second;
    if (!s->base_class.empty()) {
        const StructSymbol* base = findStruct(s->base_class);
        if (base) return self(base, field_name, self);
    }
    return nullptr;
};
```
If `field_name` does not exist, `find_field` endlessly calls `self(base, ...)` between `A` and `B`. This infinite recursion exhausts the process stack, raising `EXCEPTION_STACK_OVERFLOW` (`0xC00000FD`) and crashing the compiler executable.

#### Observed vs Expected Output
- **Expected:** Diagnostic error: `Cyclic inheritance detected between 'A' and 'B'`
- **Observed:** Compiler hangs and crashes with unhandled stack overflow exception.

#### Concrete Fix
Add cycle detection during semantic analysis and add a visited set in `find_field` / `find_method`:
```cpp
// src/GrassSema.cpp:967
auto find_field = [this](const StructSymbol* s, const std::string& field_name,
                         std::unordered_set<std::string>& visited, auto& self) -> const SemaType* {
    if (!s || visited.count(s->name)) return nullptr;
    visited.insert(s->name);
    auto fit = s->fields.find(field_name);
    if (fit != s->fields.end()) return &fit->second;
    if (!s->base_class.empty()) {
        const StructSymbol* base = findStruct(s->base_class);
        if (base) return self(base, field_name, visited, self);
    }
    return nullptr;
};
```

---

### BUG-05: Hardware Exception Trap #DE / 0xC0000094 on Division/Modulo by Zero

- **Severity:** CRITICAL
- **Component:** Native Code Generator (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:2248-2260`
- **Minimal Reproduction File:** `tests/audit/repro_div_zero_crash.grass`

#### Vulnerability Description
Integer division (`IROp::Sdiv`) and remainder (`IROp::Srem`) are lowered directly to x86 `cqo` followed by `idiv`:
```cpp
encoder.mov_rr(backend::Reg::RAX, lhs_r);
encoder.cqo();
encoder.idiv_r(rhs_r);
```
No zero check or `INT64_MIN / -1` overflow check is generated.
When `rhs_r` is `0`, the x86 CPU raises hardware fault `#DE` (`EXCEPTION_INT_DIVIDE_BY_ZERO`, `0xC0000094`).
When `lhs_r` is `-9223372036854775808` (`INT64_MIN`) and `rhs_r` is `-1`, `idiv` cannot represent `+9223372036854775808` in 64 bits and raises `EXCEPTION_INT_OVERFLOW` (`0xC0000095`).
In GrassVM, division by zero is safely trapped and returns `0.0`.

#### Observed vs Expected Output
- **GrassVM Output:** `c = 0` (clean return)
- **Native Output:** Process terminated abruptly by OS with unhandled exception `0xC0000094`.

#### Concrete Fix
In `GrassNativeCodegen.cpp`, emit a conditional test and branch before `idiv`, or route division through a safe runtime helper:
```cpp
// src/GrassNativeCodegen.cpp:2248
// Test if divisor is zero
backend::LabelId non_zero_label = encoder.create_label();
backend::LabelId end_div_label = encoder.create_label();
encoder.test_rr(rhs_r, rhs_r);
encoder.jcc(backend::ConditionCode::NotZero, non_zero_label);
// If zero, return 0 (matching VM behavior)
encoder.xor_rr(dst_r, dst_r);
encoder.jmp(end_div_label);
encoder.bind_label(non_zero_label);
encoder.mov_rr(backend::Reg::RAX, lhs_r);
encoder.cqo();
encoder.idiv_r(rhs_r);
encoder.mov_rr(dst_r, backend::Reg::RAX);
encoder.bind_label(end_div_label);
```

---

### BUG-06: RAII Destructor and Scope Cleanups Omitted on Early Control Flow

- **Severity:** CRITICAL
- **Component:** Native Code Generator & Memory Manager (`src/GrassNativeCodegen.cpp`, `src/GrassNativeMemory.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1095-1130`
- **Minimal Reproduction File:** `tests/audit/repro_raii_return_leak.grass`

#### Vulnerability Description
`GrassNativeMemory` tracks scope frames and registered RAII cleanup functions (`register_raii_cleanup`). When exiting a scope normally via `}`, `ASTLowerer::parse_block` calls `pop_scope()`:
```cpp
// src/GrassNativeCodegen.cpp:1090
pop_scope();
```
However, in `ASTLowerer::parse_statement`:
1. `break`: emits `builder_.create_jump(loop_stack_.back().b_exit)` directly.
2. `continue`: emits `builder_.create_jump(loop_stack_.back().b_step)` directly.
3. `return`: emits `builder_.create_ret(...)` directly.

None of these control flow statements invoke `mem_mgr_->exit_scope()` or walk active scopes to emit cleanups. Any dynamically allocated resources (heap strings, arrays, custom struct destructors) allocated inside enclosing blocks are permanently leaked upon early exit.

#### Concrete Fix
Before emitting `create_ret`, `create_jump` (for break/continue), walk down the active scope stack and emit pending cleanups:
```cpp
// src/GrassNativeCodegen.cpp:1120
if (mem_mgr_) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        mem_mgr_->emit_scope_cleanups(builder_, *it);
    }
}
builder_.create_ret(ret_val);
```

---

## 4. High-Severity Flaws & Discrepancies (HIGH)

---

### BUG-07: Missing `OP_BIT_SHL` and `OP_BIT_SHR` in GrassVM Dispatch Table

- **Severity:** HIGH
- **Component:** Bytecode VM Interpreter (`src/grassc.cpp`)
- **Exact Location:** `src/grassc.cpp:317-385`
- **Minimal Reproduction File:** `tests/audit/repro_shl_vm.grass`

#### Vulnerability Description
`BytecodeCompiler` emits `OP_BIT_SHL` (`0x1A`) and `OP_BIT_SHR` (`0x1B`) for bitwise shift operators `<<` and `>>`.
However, in `GrassVM::execute`, the computed-goto dispatch table does NOT register jump targets for `0x1A` and `0x1B`. When executed, both opcodes jump to `do_unknown:`:
```cpp
do_unknown:
    DISPATCH_STEP();
```
`do_unknown` simply skips to the next instruction without modifying the stack. Because `<<` pushed LHS and then RHS, skipping the operation leaves the RHS operand at the top of the stack and discards LHS.
As a result, `1 << 4` evaluates to `4` in GrassVM, whereas Native correctly computes `16`.

#### Observed vs Expected Output
- **GrassVM Output:** `shl = 4 shr = 2`
- **Native Output:** `shl = 16 shr = 8`

#### Concrete Fix
In `src/grassc.cpp:330`, add dispatch table entries and implementations for `OP_BIT_SHL` and `OP_BIT_SHR`:
```cpp
TARGET(OP_BIT_SHL) {
    Value b = stack_pop();
    Value a = stack_pop();
    Value res;
    res.type = VAL_NUMBER;
    res.val = static_cast<double>(static_cast<int64_t>(a.val) << static_cast<int64_t>(b.val));
    stack_push(res);
    DISPATCH_STEP();
}
TARGET(OP_BIT_SHR) {
    Value b = stack_pop();
    Value a = stack_pop();
    Value res;
    res.type = VAL_NUMBER;
    res.val = static_cast<double>(static_cast<int64_t>(a.val) >> static_cast<int64_t>(b.val));
    stack_push(res);
    DISPATCH_STEP();
}
```

---

### BUG-08: Eager Bitwise Logic Lowering for `&&` and `||` in Native Backend

- **Severity:** HIGH
- **Component:** Native AST Lowerer (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1237-1250`
- **Minimal Reproduction File:** `tests/audit/repro_logic_bitwise.grass`

#### Vulnerability Description
In `ASTLowerer::parse_logical_and` and `parse_logical_or`:
```cpp
ir::ValueId ASTLowerer::parse_logical_and() {
    auto lhs = parse_equality();
    while (match(TOK_AND_AND)) {
        auto rhs = parse_equality();
        lhs = builder_.create_and(lhs, rhs);
    }
    return lhs;
}
```
1. `create_and` and `create_or` are bitwise integer operations (`IROp::And`, `IROp::Or`).
2. Operating directly on integers yields invalid truth values: `2 && 1` computes `2 & 1 = 0` (falsy) instead of truthy `1`. `2 || 0` computes `2 | 0 = 2` instead of boolean `1`.
3. Expressions are evaluated eagerly without short-circuiting: in `0 && side_effect()`, `side_effect()` is unconditionally executed.

#### Observed vs Expected Output
- **GrassVM Output:** `1 1`
- **Native Output:** `0 2`

#### Concrete Fix
Lower `&&` and `||` into control flow with short-circuit basic blocks, or at minimum cast operands to `i1` (`icmp ne 0`):
```cpp
// Convert operands to boolean before logical operations
ir::ValueId zero = builder_.create_const_int(ctx_.get_i64_type(), 0);
ir::ValueId lhs_bool = builder_.create_icmp_ne(lhs, zero);
ir::ValueId rhs_bool = builder_.create_icmp_ne(rhs, zero);
lhs = builder_.create_and(lhs_bool, rhs_bool);
```

---

### BUG-09: Struct GEP Stride Hardcoded to 8 Bytes (Field Padding & Alignment Divergence)

- **Severity:** HIGH
- **Component:** Native Code Generator (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:2280`
- **Minimal Reproduction File:** `tests/audit/repro_struct_padding.grass`

#### Vulnerability Description
In `GrassNativeCodegen.cpp:2280`:
```cpp
case IROp::GetElementPtr: {
    backend::Reg base_r = get_reg(inst->operands[0]);
    int64_t byte_off = static_cast<int64_t>(inst->gep_field_index) * 8;
    encoder.lea_m(dst_r, backend::MemOperand(base_r, static_cast<int32_t>(byte_off)));
    break;
}
```
`IROp::GetElementPtr` computes field offset strictly as `gep_field_index * 8`.
Although `GrassNativeOOP::finalize_class()` calculates exact byte offsets taking C ABI alignment and packing into account (e.g. `bool` = 1 byte, `int32` = 4 bytes, `int64` = 8 bytes), `GrassNativeCodegen` ignores these computed offsets and forces an 8-byte stride for every field. When structs contain mixed data types (such as `bool`, `int32`, `ptr`), the compiled code reads and writes from incorrect memory locations.

#### Concrete Fix
In `IROp::GetElementPtr`, retrieve the true field byte offset from `OOPClassDef` or `IRType::struct_fields`:
```cpp
case IROp::GetElementPtr: {
    backend::Reg base_r = get_reg(inst->operands[0]);
    int64_t byte_off = 0;
    if (inst->gep_struct_type && inst->gep_field_index < inst->gep_struct_type->struct_fields.size()) {
        byte_off = inst->gep_struct_type->struct_fields[inst->gep_field_index].offset;
    } else {
        byte_off = static_cast<int64_t>(inst->gep_field_index) * 8;
    }
    encoder.lea_m(dst_r, backend::MemOperand(base_r, static_cast<int32_t>(byte_off)));
    break;
}
```

---

### BUG-10: Stack Red-Zone Violation in `emit_phi_copies` (Windows x64 ABI Non-Compliance)

- **Severity:** HIGH
- **Component:** Native Code Generator (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:2046-2055`
- **Minimal Reproduction File:** `tests/audit/repro_phi_redzone.grass`

#### Vulnerability Description
In `GrassNativeCodegen.cpp:2046`:
```cpp
int32_t scratch_base = frame_info.total_frame_size + 64;
// writes to: [RBP - (scratch_base + i * 8)]
```
`%rsp` is decremented in the prologue by `frame_info.total_frame_size`.
Any offset larger than `total_frame_size` points to stack memory BELOW `%rsp`.
While the SysV AMD64 ABI (Linux/macOS) permits a 128-byte red zone below `%rsp`, the Microsoft x64 ABI strictly forbids writing below `%rsp`. Asynchronous kernel events (thread context switches, APCs, hardware interrupts, debugger hooks, SEH exception handlers) overwrite memory below `%rsp` without warning.

#### Concrete Fix
Include the scratch area inside `req.local_vars_size` in `GrassNativeCodegen.cpp:1846` so that `%rsp` is allocated below all scratch slots during the function prologue.

---

### BUG-11: Struct Metadata Loss with Idiomatic `take` Declaration in Native Backend

- **Severity:** HIGH
- **Component:** Native AST Lowerer (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:894-905`
- **Minimal Reproduction File:** `tests/audit/repro_take_struct.grass`

#### Vulnerability Description
In `ASTLowerer::parse_statement`:
```cpp
bool is_struct_decl = (oop_mgr_ && oop_mgr_->has_class(peek().text));
if ((is_var_keyword(peek().text) || is_struct_decl) && peek(1).kind == TOK_IDENT) {
    ...
    if (is_struct_decl) {
        struct_name = type_name;
    }
```
When declaring an instance with the language's primary variable keyword `take`:
```grass
take c = Calculator();
```
`is_struct_decl` is `false`. The lowerer treats `c` as an anonymous `i64` variable with `struct_type_name = ""`.
When `c.compute()` is subsequently invoked, `last_struct_name_` is empty. The method call resolution is skipped completely, generating invalid IR with uninitialized `ValueId %0`, which causes an `Optimization Error: Instruction uses undefined ValueId %0`.

#### Concrete Fix
When variable type is `take` (inferred), inspect the initializer expression on the RHS. If the RHS is a struct constructor call `StructName()`, propagate the struct type name to the newly declared variable binding.

---

### BUG-12: Array Index Assignment `arr[idx] = val` Unimplemented in Native Lowerer

- **Severity:** HIGH
- **Component:** Native AST Lowerer (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1150-1234`
- **Minimal Reproduction File:** `tests/audit/repro_array_assign.grass`

#### Vulnerability Description
`ASTLowerer::parse_assignment` only handles:
1. `obj.field = rhs`
2. `var = rhs`
It contains no case for `arr[idx] = rhs`.
When `arr[0] = 42;` is encountered, the parser fails to recognize the assignment, falling through to `parse_logical_or`, which stops at `=` and fails with:
`Build error: Lowering Error: Unexpected token in expression: =`.
The runtime function `grass_array_set` is implemented in `bin/grass_native_runtime.c`, but the compiler never emits calls to it.

#### Concrete Fix
In `ASTLowerer::parse_assignment`, add a branch recognizing `TOK_IDENT [ expr ] = expr` and emit `create_call("grass_array_set", ...)`:
```cpp
// Check for array element assignment: arr[idx] = expr
if (peek().kind == TOK_IDENT && peek(1).kind == TOK_LBRACKET) {
    std::string arr_name = advance().text;
    advance(); // consume '['
    ir::ValueId idx = parse_expression(error);
    consume(TOK_RBRACKET, "Expected ']'", error);
    consume(TOK_ASSIGN, "Expected '='", error);
    ir::ValueId val = parse_assignment(error);
    const VarBinding* var = lookup_var(arr_name);
    ir::ValueId arr_ptr = builder_.create_load(var->alloca_id, arr_name + "_ptr");
    builder_.create_call("grass_array_set", ctx_.get_void_type(), { arr_ptr, idx, val });
    return val;
}
```

---

## 5. Medium-Severity Issues (MEDIUM)

---

### BUG-13: Integer Literal Overflow to `f64` for Constants >= 1e15

- **Severity:** MEDIUM
- **Component:** Native AST Lowerer (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1515-1518`
- **Minimal Reproduction File:** `tests/audit/repro_int_literal_1e15.grass`

#### Vulnerability Description
In `ASTLowerer::parse_primary`:
```cpp
double val = advance().num_val;
if (std::floor(val) == val && std::fabs(val) < 1e15) {
    return builder_.create_const_i64(static_cast<int64_t>(val));
}
return builder_.create_const_f64(val);
```
Any integer literal >= `1e15` (1,000,000,000,000,000) is converted into `f64`. A 64-bit integer can hold up to `9.22e18` (`INT64_MAX = 9223372036854775807`). Converting these integers to `f64` loses the bottom 11 bits of precision (as double mantissa is only 53 bits) and causes codegen crashes when used in integer expressions.

#### Concrete Fix
Store literal values as 64-bit integers in `GrassToken` using `std::stoll` when no decimal point is present in the token text.

---

### BUG-14: Win64 Non-Volatile XMM Registers (XMM6-XMM15) Not Restored in Epilogue

- **Severity:** MEDIUM
- **Component:** Native Code Generator (`src/GrassNativeCodegen.cpp`)
- **Exact Location:** `src/GrassNativeCodegen.cpp:1850-1855, 2599-2602`
- **Minimal Reproduction File:** `tests/audit/repro_xmm_callee_saved.grass`

#### Vulnerability Description
Win64 ABI requires `XMM6`-`XMM15` to be preserved across calls by the callee. `GrassIR.cpp:2992` marks them as callee-saved. While they are saved in the prologue via `movsd_mx`, the epilogue loops through `frame_info.callee_saved_regs` with:
```cpp
if (backend::reg_is_gpr(*it)) encoder.pop_r(*it);
```
Any XMM registers in `callee_saved_regs` are ignored, violating Win64 ABI contract and corrupting the caller's floating-point register state.

---

### BUG-15: Division Semantics Divergence: Floating-Point in VM vs Integer Truncation in Native

- **Severity:** MEDIUM
- **Component:** Bytecode VM vs Native Backend (`src/grassc.cpp:445`, `src/GrassNativeCodegen.cpp:2248`)
- **Exact Location:** `src/grassc.cpp:445`
- **Minimal Reproduction File:** `tests/audit/repro_div_semantics.grass`

#### Vulnerability Description
In GrassVM, all numeric values are stored as IEEE-754 `double`. Evaluating `-7 / 3` produces `-2.33333`.
In the native backend, integer variables are typed as `i64` and compiled to x86 `idiv`, which truncates towards zero, producing `-2`. This creates incompatible behavior between script prototyping and production native binaries.

---

### BUG-16: `array.push()` Method Call Rejected by Sema

- **Severity:** MEDIUM
- **Component:** Semantic Analyzer (`src/GrassSema.cpp`)
- **Exact Location:** `src/GrassSema.cpp:945-947`
- **Minimal Reproduction File:** `tests/audit/repro_array_push_sema.grass`

#### Vulnerability Description
`GrassSema.cpp:945` rejects method invocations on array types with:
`Member access 'push' on non-struct type '[]'`.
Valid GrassGreen code (including `examples/test_array_struct.grass`) fails compilation unless the `--no-sema` flag is manually supplied.

---

### BUG-17: `class` Keyword Rejected by VM Bytecode Compiler and Ignored by Native Lowerer

- **Severity:** MEDIUM
- **Component:** Bytecode Compiler & Native AST Lowerer (`src/GrassSyntax.cpp:2007`, `src/GrassNativeCodegen.cpp:548`)
- **Exact Location:** `src/GrassSyntax.cpp:2007`
- **Minimal Reproduction File:** `tests/audit/repro_class_unsupported.grass`

#### Vulnerability Description
`BytecodeCompiler` explicitly errors out on `class`:
`Class definition outside function is not supported in current VM bytecode compiler!`.
`ASTLowerer` only checks for token `struct`, completely ignoring `class`. Consequently, class syntax cannot be used in either execution mode.

---

## 6. Recommendations & Action Plan for GrassGreen v4.2.1

1. **Fix Callee Stack Offset (BUG-01):** Immediate priority. Change line 2019 of `src/GrassNativeCodegen.cpp` to add 32 bytes for Win64.
2. **Exclude RAX from General Allocation (BUG-02):** Remove `PhysReg::RAX` from `gpr_caller_saved` in `src/GrassIR.cpp:2986` to prevent register clobbering in loop comparisons.
3. **Add Floating-Point Arithmetic Lowering (BUG-03):** Route float `+`, `-`, `*`, `/` to `create_fadd`, `create_fsub`, `create_fmul`, `create_fdiv` in `ASTLowerer`.
4. **Implement Cycle Detection in Sema (BUG-04):** Guard inheritance resolution in `src/GrassSema.cpp` with a `visited` set to eliminate stack overflows.
5. **Handle Integer Division Traps (BUG-05):** Emit zero-checks before x86 `idiv` in `src/GrassNativeCodegen.cpp`.
6. **Implement Scope Unwinding for Early Returns (BUG-06):** Ensure `break`, `continue`, and `return` execute RAII destructors.
7. **Add Missing Shift Opcodes to GrassVM (BUG-07):** Register `OP_BIT_SHL` and `OP_BIT_SHR` in `src/grassc.cpp`.
8. **Implement Array Assignment (BUG-12):** Add `arr[idx] = val` parsing to `ASTLowerer::parse_assignment`.

---
*Report generated automatically by Antigravity Compiler Audit Suite.*
