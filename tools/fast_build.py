import os
import sys
import time
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor

SRCS = [
    "src/cli/grassc.cpp",
    "src/cli/GrassInstaller.cpp",
    "src/cli/GrassPacker.cpp",
    "src/cli/GrassDiag.cpp",
    "src/frontend/GrassLexer.cpp",
    "src/frontend/GrassSyntax.cpp",
    "src/frontend/GrassBytecodeExpr.cpp",
    "src/frontend/GrassBytecodeUI.cpp",
    "src/frontend/GrassBytecodeStmt.cpp",
    "src/frontend/GrassBytecodeControl.cpp",
    "src/frontend/GrassBytecodeDecl.cpp",
    "src/frontend/GrassParserExpr.cpp",
    "src/frontend/GrassParserStmt.cpp",
    "src/frontend/GrassParserDecl.cpp",
    "src/frontend/GrassSema.cpp",
    "src/frontend/sema/SemaBuiltinsCore.cpp",
    "src/frontend/sema/SemaBuiltinsMedia.cpp",
    "src/frontend/sema/SemaBuiltinsSys.cpp",
    "src/frontend/sema/SemaExpr.cpp",
    "src/frontend/sema/SemaStmt.cpp",
    "src/frontend/GrassImport.cpp",
    "src/frontend/GrassLangGen.cpp",
    "src/native/GrassIR.cpp",
    "src/native/GrassIRModule.cpp",
    "src/native/GrassIRBuilder.cpp",
    "src/native/GrassIRSSA.cpp",
    "src/native/GrassIROptPasses.cpp",
    "src/native/GrassNativeCodegen.cpp",
    "src/native/GrassASTLowererHelpers.cpp",
    "src/native/GrassASTLowererDecl.cpp",
    "src/native/GrassASTLowererStmt.cpp",
    "src/native/GrassASTLowererExpr.cpp",
    "src/native/GrassASTLowererOps.cpp",
    "src/native/GrassASTLowererPrimary.cpp",
    "src/native/GrassNativeX86Backend.cpp",
    "src/native/GrassNativeX86Insts.cpp",
    "src/native/GrassNativeX86Vec.cpp",
    "src/native/GrassNativeOOP.cpp",
    "src/native/GrassNativeMemory.cpp",
    "src/native/GrassNativeStdlib.cpp",
    "src/native/stdlib/NativeStdlibCore.cpp",
    "src/native/stdlib/NativeStdlibMedia.cpp",
    "src/native/stdlib/NativeStdlibSys.cpp",
    "src/native/GrassNativeDebug.cpp",
    "src/native/GrassAdvancedOpt.cpp",
    "src/native/GrassAdvancedOptLoops.cpp",
    "src/native/GrassAdvancedOptVec.cpp",
    "src/native/GrassX86Encoder.cpp",
    "src/native/GrassX86EncoderALU.cpp",
    "src/native/GrassX86EncoderFP.cpp",
    "src/native/GrassX86EncoderSIMD.cpp",
    "src/native/GrassObjBase.cpp",
    "src/native/GrassObjWriter.cpp",
    "src/native/GrassObjELF.cpp",
    "src/native/GrassObjPE.cpp",
    "src/native/GrassObjRuntimeIO.cpp",
    "src/native/GrassObjRuntimeSys.cpp",
    "src/native/GrassElfWriter.cpp",
    "src/vm/GrassVM.cpp",
    "src/vm/builtins/BuiltinCore.cpp",
    "src/vm/builtins/BuiltinMedia.cpp",
    "src/vm/builtins/BuiltinSys.cpp",
    "src/vm/builtins/BuiltinLowLevel.cpp",
    "src/vm/GrassOptimizer.cpp",
    "src/vm/GrassJIT.cpp",
    "src/vm/GrassMemory.cpp",
    "src/vm/GrassFFI.cpp",
    "src/baremetal/GrassBoot.cpp",
    "src/baremetal/GrassBaremetal.cpp",
    "src/baremetal/GrassHAL.cpp",
    "src/baremetal/GrassMMIO.cpp",
    "src/baremetal/GrassASM.cpp",
    "src/baremetal/GrassKeyboard.cpp",
    "src/baremetal/GSE.cpp",
    "src/runtime/GrassFsys.cpp",
    "src/runtime/GrassNet.cpp",
    "src/runtime/GrassWeb.cpp",
    "src/runtime/GrassThread.cpp",
    "src/runtime/GrassTime.cpp",
    "src/runtime/GrassProc.cpp",
    "src/runtime/GrassSys.cpp",
    "src/runtime/GrassRandom.cpp",
    "src/runtime/GrassJson.cpp",
    "src/runtime/GrassTg.cpp",
    "src/runtime/GrassHardware.cpp",
    "src/media/Grass2DGraphics.cpp",
    "src/media/GrassGfx3D.cpp",
    "src/media/gui_engine.cpp",
    "src/media/GrassAudio.cpp",
    "src/media/GrassPhysics.cpp",
    "src/media/CoreMath.cpp",
    "src/media/FastMath.cpp",
    "src/media/GrassMath3D.cpp"
]

LIBS = [
    "-lgdiplus", "-lgdi32", "-lcomctl32", "-lwinmm", "-lws2_32",
    "-lwininet", "-lurlmon", "-lpsapi", "-liphlpapi", "-ldxgi", "-lpdh"
]

OBJ_DIR = "obj"
BIN_DIR = "bin"

def compile_file(src):
    base_name = os.path.splitext(os.path.basename(src))[0]
    obj_path = os.path.join(OBJ_DIR, base_name + ".o")
    d_path = os.path.join(OBJ_DIR, base_name + ".d")

    needs_rebuild = False
    if not os.path.exists(obj_path):
        needs_rebuild = True
    else:
        obj_mtime = os.path.getmtime(obj_path)
        if os.path.getmtime(src) > obj_mtime:
            needs_rebuild = True
        elif os.path.exists(d_path):
            try:
                with open(d_path, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                # Parse dependencies in Makefile format
                deps = content.replace("\\\n", " ").split()
                for dep in deps[1:]: # skip target
                    if dep.endswith(":") or not dep:
                        continue
                    if os.path.exists(dep) and os.path.getmtime(dep) > obj_mtime:
                        needs_rebuild = True
                        break
            except Exception:
                pass

    if not needs_rebuild:
        return (src, True, "")

    cmd = [
        "g++", "-std=c++20", "-O3", "-Isrc", "-Iinclude", "-MMD", "-MF", d_path,
        "-c", src, "-o", obj_path
    ]
    res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        return (src, False, res.stderr + res.stdout)
    return (src, True, "")

def main():
    os.makedirs(OBJ_DIR, exist_ok=True)
    os.makedirs(BIN_DIR, exist_ok=True)

    print("[GrassCompiler] Compiling source files in parallel...")
    start_time = time.time()

    workers = min(os.cpu_count() or 4, 16)
    errors = []
    rebuilt_count = 0

    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(compile_file, src) for src in SRCS]
        for f in futures:
            src, ok, err_msg = f.result()
            if not ok:
                errors.append((src, err_msg))
            elif err_msg != "skipped":
                rebuilt_count += 1

    if errors:
        print("[GrassCompiler] Compilation FAILED with errors:")
        for src, err in errors:
            print(f"\n--- In {src} ---")
            print(err)
        return 1

    print(f"[GrassCompiler] Object files ready in {time.time() - start_time:.2f}s. Linking bin/grassc.exe...")

    obj_files = [f"obj/{os.path.splitext(os.path.basename(s))[0]}.o" for s in SRCS]
    link_cmd = [
        "g++", "-std=c++20", "-O3", "-s", "-static", "-static-libgcc", "-static-libstdc++"
    ] + obj_files + ["-o", "bin/grassc.exe"] + LIBS

    link_res = subprocess.run(link_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if link_res.returncode != 0:
        print("[GrassCompiler] Linking FAILED:")
        print(link_res.stderr + link_res.stdout)
        return link_res.returncode

    print(f"[GrassCompiler] Build SUCCESSFUL: bin/grassc.exe (total {time.time() - start_time:.2f}s)")

    # 1. Update bin/grg.exe
    grg_exe = os.path.join(BIN_DIR, "grg.exe")
    shutil.copy2(os.path.join(BIN_DIR, "grassc.exe"), grg_exe)
    print("[GrassCompiler] Updated bin/grg.exe")

    # 2. Build gpm.exe if needed or out of date
    gpm_src = "src/cli/gpm.cpp"
    gpm_exe = os.path.join(BIN_DIR, "gpm.exe")
    if os.path.exists(gpm_src) and (not os.path.exists(gpm_exe) or os.path.getmtime(gpm_src) > os.path.getmtime(gpm_exe)):
        print("[GrassCompiler] Building gpm package manager...")
        subprocess.run(["g++", "-std=c++17", "-O3", "-Isrc", "-Iinclude", gpm_src, "-o", gpm_exe], check=False)

    # 3. Build grassvm.dll if needed or out of date
    vm_srcs = [
        "src/vm/grassvm_capi.cpp",
        "src/media/Grass2DGraphics.cpp",
        "src/media/GrassGfx3D.cpp",
        "src/media/GrassPhysics.cpp",
        "src/media/GrassMath3D.cpp",
        "src/media/GrassAudio.cpp",
        "src/media/gui_engine.cpp",
        "src/runtime/GrassJson.cpp",
        "src/runtime/GrassSys.cpp",
        "src/runtime/GrassProc.cpp",
        "src/runtime/GrassHardware.cpp",
        "src/runtime/GrassTg.cpp",
        "src/runtime/GrassNet.cpp",
        "src/runtime/GrassTime.cpp",
        "src/runtime/GrassFsys.cpp",
        "src/runtime/GrassWeb.cpp",
        "src/vm/GrassMemory.cpp",
        "src/baremetal/GrassHAL.cpp",
        "src/baremetal/GrassMMIO.cpp",
        "src/baremetal/GrassBaremetal.cpp",
        "src/frontend/GrassLangGen.cpp"
    ]
    vm_dll = os.path.join(BIN_DIR, "grassvm.dll")
    vm_lib = os.path.join(BIN_DIR, "grassvm.lib")
    vm_needs_build = not os.path.exists(vm_dll) or any(
        os.path.exists(s) and os.path.getmtime(s) > os.path.getmtime(vm_dll) for s in vm_srcs
    )
    if os.path.exists(vm_srcs[0]) and vm_needs_build:
        print("[GrassCompiler] Building libgrassvm embedded library...")
        subprocess.run(["g++", "-std=c++17", "-O3", "-shared"] + vm_srcs + ["-Isrc", "-Iinclude", "-o", vm_dll, f"-Wl,--out-implib,{vm_lib}"] + LIBS + ["-luser32", "-ladvapi32"], check=False)

    # 4. Synchronize and deploy to C:\GrassGreen if directory exists
    install_root = r"C:\GrassGreen"
    if os.path.exists(install_root):
        install_bin = os.path.join(install_root, "bin")
        install_stdlib = os.path.join(install_root, "stdlib")
        os.makedirs(install_bin, exist_ok=True)
        os.makedirs(install_stdlib, exist_ok=True)

        # Copy all binaries (*.exe, *.dll, *.lib) to C:\GrassGreen\bin\
        for fname in os.listdir(BIN_DIR):
            if fname.lower().endswith((".exe", ".dll", ".lib")):
                src_path = os.path.join(BIN_DIR, fname)
                dst_path = os.path.join(install_bin, fname)
                try:
                    shutil.copy2(src_path, dst_path)
                except Exception as ex:
                    print(f"[GrassCompiler Warning] Could not copy {fname} to {install_bin}: {ex}")
        print(f"[GrassCompiler] Deployed binaries to {install_bin}")

        # Synchronize stdlib/ to C:\GrassGreen\stdlib\
        stdlib_src_dir = "stdlib"
        if os.path.exists(stdlib_src_dir):
            src_files = set(os.listdir(stdlib_src_dir))
            for fname in src_files:
                src_f = os.path.join(stdlib_src_dir, fname)
                dst_f = os.path.join(install_stdlib, fname)
                shutil.copy2(src_f, dst_f)
            # Remove deleted files from target stdlib
            for dst_fname in os.listdir(install_stdlib):
                if dst_fname not in src_files:
                    try:
                        os.remove(os.path.join(install_stdlib, dst_fname))
                    except Exception:
                        pass
            print(f"[GrassCompiler] Synchronized standard library to {install_stdlib}")

    return 0

if __name__ == "__main__":
    sys.exit(main())
