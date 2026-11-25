#!/usr/bin/env bash
# ==============================================================================
#  GrassGreen 4.3.0 — Linux Fast Build Script
# ==============================================================================

set -e

echo "[GrassCompiler] Building GrassGreen v4.3.0 on Linux..."
mkdir -p bin

g++ -std=c++20 -O3 -s -static -static-libgcc -static-libstdc++ -Isrc -Iinclude \
  src/cli/grassc.cpp \
  src/frontend/GrassSyntax.cpp \
  src/frontend/GrassSema.cpp \
  src/frontend/GrassImport.cpp \
  src/frontend/GrassLangGen.cpp \
  src/native/GrassIR.cpp \
  src/native/GrassNativeCodegen.cpp \
  src/native/GrassNativeOOP.cpp \
  src/native/GrassNativeMemory.cpp \
  src/native/GrassNativeStdlib.cpp \
  src/native/GrassNativeDebug.cpp \
  src/native/GrassAdvancedOpt.cpp \
  src/native/GrassX86Encoder.cpp \
  src/native/GrassObjWriter.cpp \
  src/native/GrassElfWriter.cpp \
  src/vm/GrassOptimizer.cpp \
  src/vm/GrassJIT.cpp \
  src/vm/GrassMemory.cpp \
  src/vm/GrassFFI.cpp \
  src/baremetal/GrassBoot.cpp \
  src/baremetal/GrassBaremetal.cpp \
  src/baremetal/GrassHAL.cpp \
  src/baremetal/GrassMMIO.cpp \
  src/baremetal/GrassASM.cpp \
  src/baremetal/GrassKeyboard.cpp \
  src/baremetal/GSE.cpp \
  src/runtime/GrassFsys.cpp \
  src/runtime/GrassNet.cpp \
  src/runtime/GrassWeb.cpp \
  src/runtime/GrassThread.cpp \
  src/runtime/GrassTime.cpp \
  src/runtime/GrassProc.cpp \
  src/runtime/GrassSys.cpp \
  src/runtime/GrassRandom.cpp \
  src/runtime/GrassJson.cpp \
  src/runtime/GrassTg.cpp \
  src/runtime/GrassHardware.cpp \
  src/media/Grass2DGraphics.cpp \
  src/media/GrassGfx3D.cpp \
  src/media/gui_engine.cpp \
  src/media/GrassAudio.cpp \
  src/media/GrassPhysics.cpp \
  src/media/CoreMath.cpp \
  src/media/FastMath.cpp \
  src/media/GrassMath3D.cpp \
  -o bin/grassc \
  -pthread -ldl

cp bin/grassc bin/grg
chmod +x bin/grassc bin/grg

echo "[GrassCompiler] Building gpm package manager..."
g++ -std=c++17 -O3 -Isrc -Iinclude src/cli/gpm.cpp -o bin/gpm
chmod +x bin/gpm

echo "[GrassCompiler] Building libgrassvm shared library..."
g++ -std=c++17 -O3 -shared -fPIC src/vm/grassvm_capi.cpp -Isrc -Iinclude -o bin/libgrassvm.so

echo "[GrassCompiler] Build SUCCESSFUL: bin/grassc, bin/grg, bin/gpm, bin/libgrassvm.so"

if [ -w /usr/local/bin ]; then
    cp bin/grassc /usr/local/bin/grassc
    cp bin/grg /usr/local/bin/grg
    cp bin/gpm /usr/local/bin/gpm
    echo "[GrassCompiler] Updated global /usr/local/bin/grassc, /usr/local/bin/grg, /usr/local/bin/gpm"
fi
