#!/usr/bin/env bash
# ==============================================================================
#  GrassGreen 4.2 — Linux Fast Build Script
# ==============================================================================

set -e

echo "[GrassCompiler] Building GrassGreen v4.2 on Linux..."
mkdir -p bin

g++ -std=c++20 -O3 -s -static-libgcc -static-libstdc++ \
  src/grassc.cpp \
  src/GrassHAL.cpp \
  src/GrassMMIO.cpp \
  src/GrassASM.cpp \
  src/GrassKeyboard.cpp \
  src/GrassBoot.cpp \
  src/GrassThread.cpp \
  src/GrassJIT.cpp \
  src/GrassImport.cpp \
  src/GrassOptimizer.cpp \
  src/GrassSyntax.cpp \
  src/GrassProc.cpp \
  src/GrassFsys.cpp \
  src/GrassTime.cpp \
  src/GrassNet.cpp \
  src/GrassJson.cpp \
  src/GrassSys.cpp \
  src/gui_engine.cpp \
  src/GrassWeb.cpp \
  src/GSE.cpp \
  src/FastMath.cpp \
  src/CoreMath.cpp \
  src/GrassMemory.cpp \
  src/GrassRandom.cpp \
  src/GrassAudio.cpp \
  src/GrassTg.cpp \
  src/Grass2DGraphics.cpp \
  src/GrassHardware.cpp \
  src/GrassBaremetal.cpp \
  src/GrassFFI.cpp \
  src/GrassLangGen.cpp \
  src/GrassMath3D.cpp \
  src/GrassGfx3D.cpp \
  src/GrassPhysics.cpp \
  -o bin/grassc \
  -pthread -ldl

cp bin/grassc bin/grg
chmod +x bin/grassc bin/grg

echo "[GrassCompiler] Building gpm package manager..."
g++ -std=c++17 -O3 src/gpm.cpp -o bin/gpm
chmod +x bin/gpm

echo "[GrassCompiler] Building libgrassvm shared library..."
g++ -std=c++17 -O3 -shared -fPIC src/grassvm_capi.cpp -Iinclude -o bin/libgrassvm.so

echo "[GrassCompiler] Build SUCCESSFUL: bin/grassc, bin/grg, bin/gpm, bin/libgrassvm.so"

if [ -w /usr/local/bin ]; then
    cp bin/grassc /usr/local/bin/grassc
    cp bin/grg /usr/local/bin/grg
    cp bin/gpm /usr/local/bin/gpm
    echo "[GrassCompiler] Updated global /usr/local/bin/grassc, /usr/local/bin/grg, /usr/local/bin/gpm"
fi
