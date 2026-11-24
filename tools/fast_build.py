import os
import sys
import time
import subprocess
from concurrent.futures import ThreadPoolExecutor

SRCS = [
    "src/grassc.cpp",
    "src/GrassHAL.cpp",
    "src/GrassMMIO.cpp",
    "src/GrassASM.cpp",
    "src/GrassKeyboard.cpp",
    "src/GrassBoot.cpp",
    "src/GrassThread.cpp",
    "src/GrassJIT.cpp",
    "src/GrassImport.cpp",
    "src/GrassOptimizer.cpp",
    "src/GrassSyntax.cpp",
    "src/GrassProc.cpp",
    "src/GrassFsys.cpp",
    "src/GrassTime.cpp",
    "src/GrassNet.cpp",
    "src/GrassJson.cpp",
    "src/GrassSys.cpp",
    "src/gui_engine.cpp",
    "src/GrassWeb.cpp",
    "src/GSE.cpp",
    "src/FastMath.cpp",
    "src/CoreMath.cpp",
    "src/GrassMemory.cpp",
    "src/GrassRandom.cpp",
    "src/GrassAudio.cpp",
    "src/GrassTg.cpp",
    "src/Grass2DGraphics.cpp",
    "src/GrassHardware.cpp",
    "src/GrassBaremetal.cpp",
    "src/GrassFFI.cpp",
    "src/GrassLangGen.cpp",
    "src/GrassMath3D.cpp",
    "src/GrassGfx3D.cpp",
    "src/GrassPhysics.cpp",
    "src/GrassIR.cpp",
    "src/GrassX86Encoder.cpp",
    "src/GrassObjWriter.cpp",
    "src/GrassElfWriter.cpp",
    "src/GrassAdvancedOpt.cpp",
    "src/GrassNativeCodegen.cpp",
    "src/GrassNativeDebug.cpp",
    "src/GrassSema.cpp",
    "src/GrassNativeOOP.cpp",
    "src/GrassNativeMemory.cpp",
    "src/GrassNativeStdlib.cpp"
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
        "g++", "-std=c++20", "-O3", "-MMD", "-MF", d_path,
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
    return 0

if __name__ == "__main__":
    sys.exit(main())
