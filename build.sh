#!/usr/bin/env bash
# ==============================================================================
#  GrassGreen 4.2 — Linux Fast Build Script
# ==============================================================================

set -e

echo "[GrassCompiler] Building GrassGreen v4.2 on Linux..."
mkdir -p bin

g++ -std=c++20 -O3 -s \
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
  -o bin/grassc \
  -pthread

cp bin/grassc bin/grg
chmod +x bin/grassc bin/grg

echo "[GrassCompiler] Build SUCCESSFUL: bin/grassc & bin/grg"

if [ -w /usr/local/bin ]; then
    cp bin/grassc /usr/local/bin/grassc
    cp bin/grg /usr/local/bin/grg
    echo "[GrassCompiler] Updated global /usr/local/bin/grassc and /usr/local/bin/grg"
fi
