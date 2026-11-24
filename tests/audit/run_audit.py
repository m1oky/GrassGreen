#!/usr/bin/env python3
"""
GrassGreen Compiler Differential Tester & Audit Fuzzer
Tests GrassVM (-r) vs Native Backend (--native) on GrassGreen v4.2.0.
Verifies compiler stability, Win64 ABI compliance, memory management, and semantics.
Strictly 0 emojis in script and output.
"""

import os
import sys
import glob
import time
import subprocess
from typing import Dict, Any, Tuple, Optional

GRASSC_BIN = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "bin", "grassc.exe"))
AUDIT_DIR = os.path.abspath(os.path.dirname(__file__))

def clean_output(text: str) -> str:
    """Strip VM banners and normalize line endings."""
    if not text:
        return ""
    lines = []
    for line in text.replace("\r\n", "\n").split("\n"):
        line_s = line.strip()
        if line_s == "[GrassVM]: Executing bytecode...":
            continue
        if line_s.startswith("[GrassNativeCompiler]:"):
            continue
        lines.append(line)
    return "\n".join(lines).strip()

def run_vm(src_path: str, timeout: float = 3.0) -> Tuple[int, str, str, float]:
    """Run program in bytecode VM mode (-r)."""
    t0 = time.time()
    try:
        proc = subprocess.run(
            [GRASSC_BIN, "-r", src_path],
            capture_output=True,
            text=True,
            timeout=timeout
        )
        elapsed = time.time() - t0
        return proc.returncode, clean_output(proc.stdout), proc.stderr.strip(), elapsed
    except subprocess.TimeoutExpired:
        return -999, "", "TIMEOUT", time.time() - t0
    except Exception as e:
        return -998, "", str(e), time.time() - t0

def run_native(src_path: str, exe_path: str, timeout: float = 3.0) -> Tuple[int, str, str, float]:
    """Compile and run program in native mode (--native)."""
    t0 = time.time()
    cmd_build = [GRASSC_BIN, src_path, "--native", "-o", exe_path]
    try:
        p_build = subprocess.run(
            cmd_build,
            capture_output=True,
            text=True,
            timeout=4.0
        )
        if p_build.returncode != 0:
            err = p_build.stderr.strip() or p_build.stdout.strip()
            return -888, "", f"BUILD_FAIL(code {p_build.returncode}): {err}", time.time() - t0

        if not os.path.exists(exe_path):
            return -887, "", "BUILD_FAIL: exe not found", time.time() - t0

        p_run = subprocess.run(
            [exe_path],
            capture_output=True,
            text=True,
            timeout=timeout
        )
        elapsed = time.time() - t0
        return p_run.returncode, clean_output(p_run.stdout), p_run.stderr.strip(), elapsed
    except subprocess.TimeoutExpired:
        return -999, "", "TIMEOUT", time.time() - t0
    except Exception as e:
        return -998, "", str(e), time.time() - t0

# Differential Test Suite Definitions (34 tests)
DIFFERENTIAL_TESTS = [
    # 1. Arithmetic Edge Cases
    (
        "arith_precedence",
        """!GrassGreen!
int main() {
    take a = 2 + 3 * 4 - 5 * 2;
    take b = (2 + 3) * (4 - 2);
    gout << a << " " << b << endl;
    return 0;
}
"""
    ),
    (
        "arith_negative_div_mod",
        """!GrassGreen!
int main() {
    take a = -7 / 3;
    take b = -7 % 3;
    take c = 7 / -3;
    take d = 7 % -3;
    gout << a << " " << b << " " << c << " " << d << endl;
    return 0;
}
"""
    ),
    (
        "arith_overflow_int32",
        """!GrassGreen!
int main() {
    take a = 2147483647;
    take b = a + 10;
    gout << b << endl;
    return 0;
}
"""
    ),
    (
        "arith_large_int_literal",
        """!GrassGreen!
int main() {
    take a = 9223372036854775807;
    take b = a + 1;
    gout << b << endl;
    return 0;
}
"""
    ),
    (
        "arith_div_by_zero",
        """!GrassGreen!
int main() {
    take a = 10;
    take b = 0;
    take c = a / b;
    gout << c << endl;
    return 0;
}
"""
    ),
    (
        "arith_int64_min_overflow",
        """!GrassGreen!
int main() {
    take a = -9223372036854775808;
    take b = -1;
    take c = a / b;
    gout << c << endl;
    return 0;
}
"""
    ),

    # 2. Float and Type Conversions
    (
        "float_simple_arith",
        """!GrassGreen!
int main() {
    take a = 1.5;
    take b = 2.5;
    take c = a + b;
    gout << c << endl;
    return 0;
}
"""
    ),
    (
        "float_mul_div",
        """!GrassGreen!
int main() {
    take a = 5.0;
    take b = 2.0;
    take c = a * b;
    take d = a / b;
    gout << c << " " << d << endl;
    return 0;
}
"""
    ),
    (
        "float_comparison",
        """!GrassGreen!
int main() {
    take a = 1.5;
    take b = 2.5;
    take lt = a < b;
    take gt = a > b;
    take eq = a == 1.5;
    gout << lt << " " << gt << " " << eq << endl;
    return 0;
}
"""
    ),

    # 3. Bitwise Operations
    (
        "bit_and_or_xor",
        """!GrassGreen!
int main() {
    take a = 12 & 10;
    take b = 12 | 10;
    take c = 12 ^ 10;
    gout << a << " " << b << " " << c << endl;
    return 0;
}
"""
    ),
    (
        "bit_shift_left",
        """!GrassGreen!
int main() {
    take a = 1 << 4;
    take b = 3 << 3;
    gout << a << " " << b << endl;
    return 0;
}
"""
    ),
    (
        "bit_shift_right",
        """!GrassGreen!
int main() {
    take a = 32 >> 2;
    take b = 100 >> 1;
    gout << a << " " << b << endl;
    return 0;
}
"""
    ),

    # 4. Logical Operators & Short Circuiting
    (
        "logic_and_integers",
        """!GrassGreen!
int main() {
    take a = 2 && 1;
    take b = 0 && 5;
    gout << a << " " << b << endl;
    return 0;
}
"""
    ),
    (
        "logic_or_integers",
        """!GrassGreen!
int main() {
    take a = 2 || 0;
    take b = 0 || 0;
    gout << a << " " << b << endl;
    return 0;
}
"""
    ),
    (
        "logic_side_effects_and",
        """!GrassGreen!
int side(int x) {
    gout << "eval_" << x << " ";
    return x;
}
int main() {
    take a = 0 && side(1);
    gout << "res=" << a << endl;
    return 0;
}
"""
    ),
    (
        "logic_side_effects_or",
        """!GrassGreen!
int side(int x) {
    gout << "eval_" << x << " ";
    return x;
}
int main() {
    take a = 1 || side(2);
    gout << "res=" << a << endl;
    return 0;
}
"""
    ),

    # 5. Loops, Break, Continue, RAX Clobbering
    (
        "loop_nested_while_break",
        """!GrassGreen!
int main() {
    take i = 0;
    take count = 0;
    while (i < 5) {
        take j = 0;
        while (j < 5) {
            j = j + 1;
            if (j == 3) {
                break;
            }
            count = count + 1;
        }
        i = i + 1;
    }
    gout << "count = " << count << endl;
    return 0;
}
"""
    ),
    (
        "loop_while_continue",
        """!GrassGreen!
int main() {
    take i = 0;
    take sum = 0;
    while (i < 10) {
        i = i + 1;
        if (i % 2 == 0) {
            continue;
        }
        sum = sum + i;
    }
    gout << "sum = " << sum << endl;
    return 0;
}
"""
    ),
    (
        "loop_deep_nested_3",
        """!GrassGreen!
int main() {
    take c = 0;
    take i = 0;
    while (i < 3) {
        take j = 0;
        while (j < 3) {
            take k = 0;
            while (k < 3) {
                c = c + 1;
                k = k + 1;
            }
            j = j + 1;
        }
        i = i + 1;
    }
    gout << "c = " << c << endl;
    return 0;
}
"""
    ),

    # 6. Functions, Arguments (>6 args, Win64 ABI)
    (
        "func_args_4",
        """!GrassGreen!
int f4(int a, int b, int c, int d) {
    return a + b + c + d;
}
int main() {
    take r = f4(1, 2, 3, 4);
    gout << "r = " << r << endl;
    return 0;
}
"""
    ),
    (
        "func_args_8",
        """!GrassGreen!
int f8(int a, int b, int c, int d, int e, int f, int g, int h) {
    gout << a << " " << b << " " << c << " " << d << " " << e << " " << f << " " << g << " " << h << endl;
    return a + b + c + d + e + f + g + h;
}
int main() {
    take r = f8(10, 20, 30, 40, 50, 60, 70, 80);
    gout << "r = " << r << endl;
    return 0;
}
"""
    ),
    (
        "func_args_10",
        """!GrassGreen!
int f10(int a, int b, int c, int d, int e, int f, int g, int h, int i, int j) {
    return a + b + c + d + e + f + g + h + i + j;
}
int main() {
    take r = f10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);
    gout << "r = " << r << endl;
    return 0;
}
"""
    ),
    (
        "func_deep_recursion_100",
        """!GrassGreen!
int rec(int n) {
    if (n <= 0) return 0;
    return 1 + rec(n - 1);
}
int main() {
    gout << rec(100) << endl;
    return 0;
}
"""
    ),
    (
        "func_early_return_scopes",
        """!GrassGreen!
int test_scopes(int x) {
    if (x > 5) {
        take a = 10;
        if (x > 8) {
            take b = 20;
            return a + b;
        }
        return a;
    }
    return 0;
}
int main() {
    gout << test_scopes(10) << " " << test_scopes(6) << " " << test_scopes(2) << endl;
    return 0;
}
"""
    ),

    # 7. Structs, OOP, and Methods
    (
        "struct_fields_basic",
        """!GrassGreen!
struct Point {
    int x;
    int y;
}
int main() {
    Point p;
    p.x = 15;
    p.y = 25;
    gout << p.x << " " << p.y << endl;
    return 0;
}
"""
    ),
    (
        "struct_method_basic",
        """!GrassGreen!
struct Box {
    int w;
    int h;
    int area() {
        return 42;
    }
}
int main() {
    Box b;
    take a = b.area();
    gout << "area = " << a << endl;
    return 0;
}
"""
    ),
    (
        "struct_method_args5",
        """!GrassGreen!
struct MathHelper {
    int base;
    int sum4(int a, int b, int c, int d) {
        gout << a << " " << b << " " << c << " " << d << endl;
        return a + b + c + d;
    }
}
int main() {
    MathHelper m;
    take r = m.sum4(10, 20, 30, 40);
    gout << "r = " << r << endl;
    return 0;
}
"""
    ),
    (
        "struct_decl_with_take",
        """!GrassGreen!
struct Widget {
    int id;
    int get_val() {
        return 99;
    }
}
int main() {
    take w = Widget();
    take res = w.get_val();
    gout << "res = " << res << endl;
    return 0;
}
"""
    ),
    (
        "struct_mixed_alignment",
        """!GrassGreen!
struct Mixed {
    bool flag1;
    bool flag2;
    int val;
}
int main() {
    Mixed m;
    m.flag1 = true;
    m.flag2 = false;
    m.val = 777;
    gout << m.val << endl;
    return 0;
}
"""
    ),

    # 8. Dynamic Arrays
    (
        "array_create_empty_len",
        """!GrassGreen!
int main() {
    take arr = [];
    gout << len(arr) << endl;
    return 0;
}
"""
    ),
    (
        "array_push_sema_check",
        """!GrassGreen!
int main() {
    take arr = [];
    arr.push(100);
    gout << len(arr) << endl;
    return 0;
}
"""
    ),
    (
        "array_element_assign",
        """!GrassGreen!
int main() {
    take arr = [];
    arr[0] = 55;
    gout << arr[0] << endl;
    return 0;
}
"""
    ),
    (
        "array_literal_populated",
        """!GrassGreen!
int main() {
    take arr = [10, 20, 30];
    gout << len(arr) << endl;
    return 0;
}
"""
    ),
    (
        "class_keyword_support",
        """!GrassGreen!
class Entity {
    int id;
}
int main() {
    Entity e;
    e.id = 1;
    gout << e.id << endl;
    return 0;
}
"""
    ),
]

# Crash / Fuzzing Negative Tests
FUZZ_TESTS = [
    ("fuzz_empty_file", ""),
    ("fuzz_unclosed_string", '!GrassGreen!\nint main() { take s = "hello; return 0; }'),
    ("fuzz_unclosed_comment", '!GrassGreen!\n/* unclosed comment\nint main() { return 0; }'),
    ("fuzz_500_parens", '!GrassGreen!\nint main() { take x = ' + ('(' * 500) + '1' + (')' * 500) + '; return x; }'),
    ("fuzz_cyclic_inheritance", '!GrassGreen!\nclass A : B { int x; }\nclass B : A { int y; }\nint main() { A a; a.unknown = 10; return 0; }'),
    ("fuzz_bad_method", '!GrassGreen!\nstruct Foo { int x; }\nint main() { Foo f; f.non_existent(); return 0; }'),
    ("fuzz_type_mismatch", '!GrassGreen!\nint main() { int a = "string"; return 0; }'),
    ("fuzz_free_int", '!GrassGreen!\nint main() { free(123); return 0; }'),
]

def main():
    print("=" * 80)
    print("GrassGreen v4.2.0 Compiler Audit & Differential Testing Suite")
    print(f"Target Binary: {GRASSC_BIN}")
    print("=" * 80)

    if not os.path.exists(GRASSC_BIN):
        print(f"Error: Compiler binary not found at {GRASSC_BIN}")
        sys.exit(1)

    work_dir = os.path.join(AUDIT_DIR, "gen_tests")
    os.makedirs(work_dir, exist_ok=True)

    results = {
        "MATCH": 0,
        "MISMATCH": 0,
        "VM_FAIL": 0,
        "NAT_FAIL": 0,
        "BOTH_FAIL": 0,
        "TIMEOUT": 0,
    }

    test_records = []

    print(f"\n--- Stage 1: Differential Testing (VM vs Native) [{len(DIFFERENTIAL_TESTS)} tests] ---")
    for name, code in DIFFERENTIAL_TESTS:
        src_file = os.path.join(work_dir, f"{name}.grass")
        exe_file = os.path.join(work_dir, f"{name}.exe")
        with open(src_file, "w", encoding="utf-8") as f:
            f.write(code)

        vm_ret, vm_out, vm_err, _ = run_vm(src_file)
        nat_ret, nat_out, nat_err, _ = run_native(src_file, exe_file)

        status = "UNKNOWN"
        notes = ""

        if vm_ret == -999 or nat_ret == -999:
            status = "TIMEOUT"
            results["TIMEOUT"] += 1
            notes = "Execution timed out (infinite loop or hang)"
        elif vm_ret != 0 and nat_ret != 0:
            status = "BOTH_FAIL"
            results["BOTH_FAIL"] += 1
            notes = f"Both failed (VM={vm_ret}, NAT={nat_ret})"
        elif vm_ret != 0:
            status = "VM_FAIL"
            results["VM_FAIL"] += 1
            notes = f"VM error: {vm_err[:50]}"
        elif nat_ret != 0:
            status = "NAT_FAIL"
            results["NAT_FAIL"] += 1
            notes = f"Native error: {nat_err[:50]}"
        else:
            if vm_out == nat_out and vm_ret == nat_ret:
                status = "MATCH"
                results["MATCH"] += 1
            else:
                status = "MISMATCH"
                results["MISMATCH"] += 1
                notes = f"Stdout mismatch: VM='{vm_out}' vs NAT='{nat_out}'"

        test_records.append({
            "name": name,
            "status": status,
            "vm_ret": vm_ret,
            "vm_out": vm_out,
            "nat_ret": nat_ret,
            "nat_out": nat_out,
            "notes": notes,
        })

        print(f"[{status:9}] {name:27} | VM_out={vm_out[:18]:18} | NAT_out={nat_out[:18]:18} | {notes[:40]}")

    print(f"\n--- Stage 2: Crash & Syntax Fuzzing [{len(FUZZ_TESTS)} tests] ---")
    fuzz_records = []
    for name, code in FUZZ_TESTS:
        src_file = os.path.join(work_dir, f"{name}.grass")
        exe_file = os.path.join(work_dir, f"{name}.exe")
        with open(src_file, "w", encoding="utf-8") as f:
            f.write(code)

        # Fuzz test compilation
        vm_ret, vm_out, vm_err, _ = run_vm(src_file, timeout=2.0)
        p_nat = subprocess.run([GRASSC_BIN, src_file, "--native", "-o", exe_file], capture_output=True, text=True, timeout=2.0)
        nat_ret = p_nat.returncode
        nat_err = p_nat.stderr.strip() or p_nat.stdout.strip()

        is_crash = False
        desc = ""

        if vm_ret == -999 or nat_ret == -999:
            is_crash = True
            desc = "TIMEOUT / HANG (Stack Overflow / Infinite Loop)"
        elif nat_ret == 1 and vm_ret == 1:
            desc = "Handled cleanly: compiler diagnostic reported"
        elif nat_ret < 0 or nat_ret > 1:
            is_crash = True
            desc = f"Uncaught exception / crash code {nat_ret}"
        else:
            desc = f"Handled: NAT_ret={nat_ret}, VM_ret={vm_ret}"

        fuzz_status = "CRASH" if is_crash else "HANDLED"
        print(f"[{fuzz_status:7}] {name:25} | VM_ret={vm_ret:4} | NAT_ret={nat_ret:4} | {desc}")

    print(f"\n--- Stage 3: Verification of Standalone Repro Files ---")
    repro_files = sorted(glob.glob(os.path.join(AUDIT_DIR, "repro_*.grass")))
    for rpath in repro_files:
        rname = os.path.basename(rpath)
        rexe = os.path.join(work_dir, f"{rname}.exe")
        vm_ret, vm_out, vm_err, _ = run_vm(rpath, timeout=2.0)
        nat_ret, nat_out, nat_err, _ = run_native(rpath, rexe, timeout=2.0)
        print(f"[REPRO] {rname:30} | VM={vm_ret} ('{vm_out[:15]}') | NAT={nat_ret} ('{nat_out[:15]}') | Err: {nat_err[:30]}")

    print("\n" + "=" * 80)
    print("AUDIT SUMMARY STATISTICS")
    print("=" * 80)
    total_diff = len(DIFFERENTIAL_TESTS)
    print(f"Differential Tests Run    : {total_diff}")
    print(f"  Exact Output Matches     : {results['MATCH']}")
    print(f"  Stdout / Value Mismatches: {results['MISMATCH']}")
    print(f"  Native Failures / Crashes: {results['NAT_FAIL']}")
    print(f"  VM Failures              : {results['VM_FAIL']}")
    print(f"  Both Rejected            : {results['BOTH_FAIL']}")
    print(f"  Timeouts / Infinite Loops: {results['TIMEOUT']}")
    print(f"Fuzzing Tests Run         : {len(FUZZ_TESTS)}")
    print(f"Standalone Repros Checked : {len(repro_files)}")
    print("=" * 80)

if __name__ == "__main__":
    main()
