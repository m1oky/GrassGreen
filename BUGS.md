# GrassGreen Compiler & Runtime Security and Pointer Audit Report

Date: 2026-11-25
Scope: Pointer operations (`--native` and GrassVM), compiler/parser/sema robustness, standard library (`!memory!`, `!string!`, `!fs!`, `!io!`, `!net!`), and Win64 PE32+ ABI compliance.

---

## BUG-001: VM ignores `&`, `*`, dereference assignments, and pointer arithmetic
- **Subsystem**: BytecodeCompiler / GrassVM (`src/GrassSyntax.h`, `src/GrassSyntax.cpp`, `src/GrassOptimizer.cpp`, `src/grassc.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  int main() {
      int x = 10;
      int* p = &x;
      *p = 25;
      gout << x << endl;
      return 0;
  }
  ```
- **Root Cause**: `BytecodeCompiler::parseUnary()` stripped unary `*` and `&` without emitting opcodes (`if (match(TOK_STAR)) { parseUnary(); return; }` and `if (match(TOK_AMP)) { parseUnary(); return; }`), while `*ptr = val;` emitted `OP_STORE_VAR ptr` (overwriting the pointer itself). `GrassValue` lacked a pointer representation (`TAG_PTR` / `GrassVMPtr`) for variable cells, struct instances/fields, array elements, and heap buffers.
- **Fix**: Added `TAG_PTR` and `GrassVMPtr` in `GrassSyntax.h`, opcodes `OP_ADDR_VAR`, `OP_DEREF`, `OP_STORE_DEREF`, `OP_ADDR_INDEX`, `OP_ADDR_FIELD`, full VM execution and pointer arithmetic (`ptr + i`, `ptr - i`, `p2 - p1`, `ptr[i]`, `p == 0`, `p == nullptr`, `p->field`, `p->method()`) in `grassc.cpp`, plus `BytecodeCompiler` lowering in `GrassSyntax.cpp` and optimizer decoding in `GrassOptimizer.cpp`.

---

## BUG-002: Native `ptr[i]` and `&arr[i]` mishandle typed pointers and dynamic array headers
- **Subsystem**: Native Codegen (`src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  int main() {
      int a = 10;
      int* p = &a;
      p[0] = 42;
      gout << p[0] << endl;
      return 0;
  }
  ```
- **Root Cause**: `ASTLowerer::parse_postfix()` lowered every `[idx]` to `grass_array_get(base, idx)` (which treats non-`GrassDynArray` pointers as raw byte arrays without scaling by `sizeof(T)`), and `parse_assignment()` lowered `ptr[idx] = val` to `grass_array_set`. Conversely, `&arr[i]` emitted `create_gep(arr_ptr, { idx })` on the 32-byte `GrassDynArray` header instead of dereferencing its `data` buffer pointer at offset 8.
- **Fix**: For typed pointers (`element_type && !element_type->is_void()`), `ptr[i]` and `ptr[i] = val` now emit scaled `GEP` + `Load` / `Store` (with type coercion). Dynamic arrays (`ptr(void)`) continue to use `grass_array_get` / `grass_array_set`, while `&arr[i]` loads the `data` buffer pointer before indexing.

---

## BUG-003: Native dereferenced assignment `*ptr = rhs` lacks type coercion and computed lvalue support
- **Subsystem**: Native Codegen (`src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  int main() {
      float f = 1.5;
      float* pf = &f;
      *pf = 3;
      *(pf + 0) = 4.5;
      return 0;
  }
  ```
- **Root Cause**: `ASTLowerer::parse_assignment()` only recognized `*...*ident = rhs` and called `builder_.create_store(rhs, target_addr)` without coercing `rhs` to the pointed element type (storing raw `int64(3)` bits into a `double` slot), and rejected computed pointer targets like `*(ptr + i) = val;`.
- **Fix**: Added `coerce_type(rhs, elem_type)` before `create_store`, plus support for `*(expr) = rhs` and compound `*ptr += rhs` assignments.

---

## BUG-004: Native unary `&` missing chained member paths and non-lvalue validation
- **Subsystem**: Native Codegen (`src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  struct Inner { int val; }
  struct Outer { Inner inner; }
  int main() {
      Outer o;
      int* pv = &o.inner.val;
      *pv = 99;
      return 0;
  }
  ```
- **Root Cause**: `ASTLowerer::parse_unary()` only handled single-dot `&obj.field`, failing on `&o.inner.val` or `&p->field`, and did not report an error when `&` was applied to non-lvalues (e.g. `&5`).
- **Fix**: Extended `&` lowering to traverse chained `.` and `->` member paths via `emit_field_gep` / `emit_load_field` and fail cleanly with diagnostics on invalid rvalue operands.

---

## BUG-005: C-style pointer casts with namespaces misparse in `ASTLowerer`
- **Subsystem**: Native Codegen (`src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  take p = (ns::Item*)raw_ptr;
  ```
- **Root Cause**: `ASTLowerer::parse_unary()` extracted the full qualified name `tname` during lookahead, then called `parse_type_name(advance().text)` using only the first identifier segment and consumed `::` without advancing the second identifier.
- **Fix**: Reused `tname` directly and skipped the already-scanned qualifier tokens cleanly before parsing `*` pointer depth.

---

## BUG-006: `take p = &s;` could misidentify borrowed stack struct address as owning RAII instance
- **Subsystem**: Native Codegen RAII (`src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  struct Guard { int id; ~Guard() { gout << this.id << endl; } }
  int main() {
      Guard g;
      g.id = 1;
      take p = &g;
      return 0;
  }
  ```
- **Root Cause**: If `&g` propagated struct ownership metadata into `take p = &g;`, `p` could register a duplicate destructor call at scope exit alongside `g`.
- **Fix**: Explicitly mark `&` address-of expressions as non-owning (`last_expr_is_borrowed_ptr_ = true`) while keeping `Struct*` typed pointer metadata for `p->field` and `p->method()`, preventing RAII registration for borrowed pointers.

---

## BUG-007: Parser null-AST propagation on malformed syntax and unbounded expression recursion
- **Subsystem**: Parser & Sema (`src/GrassSyntax.cpp`, `src/GrassSema.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  int main() { int x = 1 + ; return 0; }
  ```
- **Root Cause**: `GrassParser` binary/unary helpers could construct `BinaryExprAST`/`UnaryExprAST` with null children on syntax recovery; `parseStruct`/`parseClass` did not check `peek().kind == TOK_IDENT` after `fn`; `parsePrimary` lacked a nesting depth guard; `GrassSema` dereferenced `mem->object` / `if_stmt->condition` without defensive null checks.
- **Fix**: Added null checks in `GrassParser` and `GrassSema` AST traversal, validated identifiers after `fn`, added recursion depth cap (`MAX_PARSER_DEPTH = 512` / `MAX_RECURSION_DEPTH`), and ensured `Lexer` safely bounds string scans at EOF without buffer overread.

---

## BUG-008: Missing integer overflow and negative-size guards in `memory_alloc`, `grass_alloc`, `grass_realloc`, `GrassMemory`, and `ArenaAllocator`
- **Subsystem**: Native Win64 Runtime, VM Memory & `!memory!` (`src/GrassObjWriter.cpp`, `src/GrassNativeCodegen.cpp`, `src/GrassMemory.cpp`, `stdlib/memory.grass`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  !memory!
  int main() {
      take bad1 = memory_alloc(-16);
      take bad2 = memory_alloc(9223372036854775800);
      return 0;
  }
  ```
- **Root Cause**: `grass_alloc` / `alloc` passed negative or near-`INT64_MAX` signed 64-bit integers directly to `HeapAlloc` as unsigned `SIZE_T`; `memory_alloc` only guarded `<= 0` without checking upper overflow bounds; `grass_realloc` lacked size overflow validation; `GrassMemory::alloc_block` lacked bounds checks against huge VM allocations; `ArenaAllocator::allocate` computed `this.offset + aligned` which could overflow when `size_bytes` was extreme.
- **Fix**: Enforced `size <= 0 || size > 0x7FFFFFFF0000ULL` (returning `0` / `nullptr`) in `memory_alloc`, `grass_alloc`, `alloc`, and `grass_realloc` across Win64 stubs and `kEmbeddedRuntimeC`, added safe overflow check in `GrassMemory::alloc_block` and `GrassHAL::alloc_executable_memory`, and rewrote `ArenaAllocator::allocate` in `stdlib/memory.grass` with subtraction-based overflow protection.

---

## BUG-009: Out-of-bounds read/overflow in `stdlib/string.grass` (`str_substr`, `int_to_str`, `str_cat`)
- **Subsystem**: Standard Library `!string!` (`stdlib/string.grass`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  !string!
  int main() {
      take s = str_substr("hello", -5, 3);
      return 0;
  }
  ```
- **Root Cause**: `str_substr` did not reject `start_idx < 0` or check `sub_len > total_len - start_idx` safely against `start_idx + sub_len` overflow. `int_to_str` broke on `INT64_MIN` (`0 - val` overflows 64-bit signed int) and wrote into a fixed 32-byte buffer without bounds checks. `str_cat(s, s)` could corrupt memory on overlapping self-append via `lstrcatA`.
- **Fix**: Added `start_idx < 0` and overflow-safe length clamping in `str_substr`, `dst == src` guard in `str_cat`, and `INT64_MIN` + index bounds safety in `int_to_str`.

---

## BUG-010: Missing `INVALID_HANDLE_VALUE (-1)` checks and parameter validation in `!io!`, `!fs!`, and `!net!`
- **Subsystem**: Standard Library (`stdlib/io.grass`, `stdlib/fsys.grass`, `stdlib/net.grass`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  io_write_raw(-1, "test", 4);
  ```
- **Root Cause**: `io_write_raw` and `io_read_raw` only checked `handle == 0` (missing `handle == -1` `INVALID_HANDLE_VALUE`); `io_print`, `io_print_newline`, `io_print_int`, `io_print_ptr`, and `io_print_double` did not guard `GetStdHandle` failure (`0` or `-1`); `net_connect` / `net_bind` did not validate `addr_len <= 0`.
- **Fix**: Added explicit `handle == -1` checks across `stdlib/io.grass`, invalid-handle guards in `WriteFile` Win64 stubs, and `addr_len <= 0` / `backlog < 0` guards in `stdlib/net.grass`.

---

## BUG-011: `grass_read_int` Win64 ABI 5th-argument stack corruption & missing null termination
- **Subsystem**: Win64 PE Runtime Stubs (`src/GrassObjWriter.cpp`)
- **Status**: FIXED
- **Minimal Repro**: Calling `grass_read_int` in a `--native` Win64 PE executable.
- **Root Cause**: `grass_read_int` zeroed `qword ptr [rsp + 20]` (clobbering `[rsp + 24]`, where `r9` `lpNumberOfBytesRead` pointed!) instead of `[rsp + 32]` (the Win64 ABI 5th argument `lpOverlapped` for `ReadFile`), skipped `GetStdHandle` and `ReadFile` return value checks, and parsed digits without writing a trailing `'\0'` terminator after `bytesRead`.
- **Fix**: Corrected frame layout (`sub rsp, 80`, `[rsp+32] = 0`, `r9 = [rsp+40]`, buffer `[rsp+48..79]`), added `GetStdHandle` (`0` / `-1`) and `ReadFile` error checks, clamped `bytesRead <= 31`, and guaranteed `'\0'` termination.

---

## BUG-012: Win64 PE stubs `grass_print_int`, `grass_array_push`, `grass_print_str`, and `__grass_veh_handler` safety gaps
- **Subsystem**: Win64 PE Runtime Stubs (`src/GrassObjWriter.cpp`)
- **Status**: FIXED
- **Root Cause**:
  1. `grass_print_int` placed `lpNumberOfBytesWritten` at `[rsp+40]` adjacent to the downward-growing digit buffer ending at `[rsp+63]` (tight margin for 20-char `INT64_MIN`).
  2. `grass_array_push` doubled capacity (`shl rax, 1` -> `shl rax, 3`) without checking overflow or `grass_realloc` returning `NULL` (overwriting `arr->data` with `NULL`).
  3. `grass_print_str`, `grass_print_newline`, and `__grass_veh_handler` did not check `GetStdHandle` returning `-1` (`INVALID_HANDLE_VALUE`) or `0` before invoking `WriteFile`.
- **Fix**: Widened `grass_print_int` stack frame to 80 bytes with clean `[rsp+48..79]` digit buffer, added `grass_realloc` `NULL`/overflow check in `grass_array_push`, and added `GetStdHandle` (`0` / `-1`) guards in `grass_print_str`, `grass_print_newline`, and `__grass_veh_handler` while maintaining strict 16-byte `RSP` alignment and 32-byte shadow space across all Win64 calls.

---

## BUG-013: Unbounded executable memory allocation & lack of strict W^X protection in `GrassHAL`
- **Subsystem**: HAL / Executable Memory (`src/GrassHAL.h`, `src/GrassHAL.cpp`)
- **Status**: FIXED
- **Root Cause**: `GrassHAL::alloc_executable_memory(size)` accepted arbitrary `size == 0` or multi-GB requests without upper-bound validation, and lacked a helper to drop `WRITE` permissions (`W^X`) after emitting JIT bytes. (Note: PE32+ image sections in `GrassObjWriter.cpp` already enforce strict `.text = RX`, `.rdata = R`, `.data = RW` without `RWX`.)
- **Fix**: Added `size == 0 || size > (64ULL << 20)` guard and `protect_executable_memory(void*, size_t)` in `GrassHAL` to transition executable buffers to `PAGE_EXECUTE_READ` / `PROT_READ | PROT_EXEC`.

---

## BUG-014: Register allocator scratch clobbering, division liveness, and `ConstFloat` stack spill collision
- **Subsystem**: IR Register Allocator & Win64 Native Backend (`src/GrassIR.cpp`, `src/GrassNativeCodegen.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  !GrassGreen!
  int* choose_ptr(int* a, int* b, int use_a) {
      if (use_a != 0) { return a; }
      return b;
  }
  int main() {
      take arr = [10, 20, 30];
      take p0 = &arr[0];
      take p2 = p0 + 2;
      int diff = p2 - p0;
      bool is_null = (p0 == 0);
      return 0;
  }
  ```
- **Root Cause**: `LinearScanAllocator::allocate` allowed general allocation of codegen scratch registers (`R9`, `R10`, `R11`, `XMM0`–`XMM3`) and only marked `crosses_call` for `IROp::Call` (not `SDiv`/`SRem`/`Shl`/`AShr`, which clobber `RDX`, `RAX`, `RCX`). As a result, `setcc r11b` corrupted `a` inside `choose_ptr` and `idiv` wiped `RDX` holding a live `ptrtoint(p0)` before `p0 == 0`. `ConstFloat` also used `get_spill_offset` (`val * 8`) even for register-allocated values, and `SDiv`/`SRem` could clobber `rhs` in `RAX`/`RDX` on `cqo`.
- **Fix**: Excluded `R9`–`R11` and `XMM0`–`XMM3` from `LinearScanAllocator` register pools, marked intervals crossing `SDiv`/`SRem`/`UDiv`/`URem`/`Shl`/`AShr`/`LShr` as `crosses_call`, copied `rhs` to `R10` if in `RAX`/`RDX` in `SDiv`/`SRem`, and used `phi_scratch_base` for register-resident `ConstFloat` staging.

---

## BUG-015: Native `FDiv` division by zero, pointer IR typing mismatches, `Struct*` parameter parsing, and `*(expr)` lookahead offset
- **Subsystem**: Native Codegen & IR Optimizer (`src/GrassNativeCodegen.cpp`, `src/GrassIR.cpp`)
- **Status**: FIXED
- **Minimal Repro**:
  ```grass
  void mutate_guard_via_ptr(Guard* g, int delta) { g->bump(delta); }
  int main() {
      take arr = [10, 20, 30, 40];
      take p0 = &arr[0];
      *(p0 + 3) = 45;
      float sfdiv = 9.0 / 0.0;
      return 0;
  }
  ```
- **Root Cause**:
  1. `IROp::FDiv` executed `divsd` without checking for `0.0` / `-0.0` divisors.
  2. `parse_additive` emitted raw `add ptr, i64` and `sdiv ptr, i64`, while `ConstantFolding` converted `IntToPtr` to `ConstInt` with `ptr` type (`ICmp operand type mismatch`).
  3. `parse_function` only matched `TOK_IDENT TOK_IDENT` for parameters, rejecting `Guard* g`.
  4. `parse_assignment` indexed `tokens_[paren_close]` with relative offset `look` instead of `pos_ + look` for `*(expr) = val;`.
- **Fix**: Added zero-divisor guard returning `0.0` in `IROp::FDiv`, normalized pointer arithmetic and mixed `ptr`/`int` comparisons to `i64` via `ptrtoint`/`inttoptr`, prevented `IntToPtr` from folding to `ConstInt`, added `TOK_STAR` handling for `Struct*` function parameters, and fixed `paren_close = pos_ + look`.

---

## BUG-016: VM optimizer offset desync for `&` opcodes, folded `ADD_CONST` on pointers, and struct pointer field truncation
- **Subsystem**: VM Bytecode Optimizer & Execution Engine (`src/GrassOptimizer.cpp`, `src/grassc.cpp`, `src/GrassSyntax.cpp`)
- **Status**: FIXED
- **Root Cause**: Pass 5.1 of `GrassOptimizer` omitted `OP_ADDR_VAR`, `OP_ADDR_INDEX`, and `OP_ADDR_FIELD` when computing new instruction offsets, desynchronizing `OP_CALL_USER -> main`. Folded `OP_ADD_CONST`/`OP_SUB_CONST` converted `GrassVMPtr` operands to raw doubles instead of adjusting offsets, and `struct` pointer fields were registered as `GrassFieldType::U64`, stripping `TAG_PTR` bits.
- **Fix**: Added all three address-of opcodes to Pass 5.1 offset calculation in `GrassOptimizer.cpp`, implemented pointer offset adjustment in `do_add_const` / `do_sub_const` in `grassc.cpp`, and stored pointer struct fields as `GrassFieldType::VALUE` in `GrassSyntax.cpp`.
