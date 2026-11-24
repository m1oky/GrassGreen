@echo off
setlocal
echo [GrassCompiler] Building GrassGreen v4.3...
if not exist "bin" mkdir bin
if not exist "obj" mkdir obj

rem Fast build check: Python with parallel compilation
where python >nul 2>&1
if %ERRORLEVEL% equ 0 (
    python tools/fast_build.py
    goto :post_build
)

rem Check if MSVC cl.exe is used
where cl.exe >nul 2>&1
if %ERRORLEVEL% equ 0 (
    echo [GrassCompiler] Compiling with MSVC /MP...
    cl /nologo /std:c++20 /O2 /MP /EHsc /utf-8 ^
      src/grassc.cpp src/GrassHAL.cpp src/GrassMMIO.cpp src/GrassASM.cpp ^
      src/GrassKeyboard.cpp src/GrassBoot.cpp src/GrassThread.cpp src/GrassJIT.cpp ^
      src/GrassImport.cpp src/GrassOptimizer.cpp src/GrassSyntax.cpp src/GrassProc.cpp ^
      src/GrassFsys.cpp src/GrassTime.cpp src/GrassNet.cpp src/GrassJson.cpp ^
      src/GrassSys.cpp src/gui_engine.cpp src/GrassWeb.cpp src/GSE.cpp ^
      src/FastMath.cpp src/CoreMath.cpp src/GrassMemory.cpp src/GrassRandom.cpp ^
      src/GrassAudio.cpp src/GrassTg.cpp src/Grass2DGraphics.cpp src/GrassHardware.cpp ^
      src/GrassBaremetal.cpp src/GrassFFI.cpp src/GrassLangGen.cpp src/GrassMath3D.cpp ^
      src/GrassGfx3D.cpp src/GrassPhysics.cpp src/GrassIR.cpp src/GrassX86Encoder.cpp ^
      src/GrassObjWriter.cpp src/GrassElfWriter.cpp src/GrassAdvancedOpt.cpp ^
      src/GrassNativeCodegen.cpp src/GrassNativeDebug.cpp src/GrassSema.cpp ^
      src/GrassNativeOOP.cpp src/GrassNativeMemory.cpp src/GrassNativeStdlib.cpp ^
      /Fe:bin/grassc.exe /link gdiplus.lib gdi32.lib comctl32.lib winmm.lib ws2_32.lib wininet.lib urlmon.lib psapi.lib iphlpapi.lib dxgi.lib pdh.lib
    goto :post_build
)

rem Fallback to sequential g++
echo [GrassCompiler] Compiling with g++...
g++ -std=c++20 -O3 -s -static -static-libgcc -static-libstdc++ ^
  src/grassc.cpp src/GrassHAL.cpp src/GrassMMIO.cpp src/GrassASM.cpp ^
  src/GrassKeyboard.cpp src/GrassBoot.cpp src/GrassThread.cpp src/GrassJIT.cpp ^
  src/GrassImport.cpp src/GrassOptimizer.cpp src/GrassSyntax.cpp src/GrassProc.cpp ^
  src/GrassFsys.cpp src/GrassTime.cpp src/GrassNet.cpp src/GrassJson.cpp ^
  src/GrassSys.cpp src/gui_engine.cpp src/GrassWeb.cpp src/GSE.cpp ^
  src/FastMath.cpp src/CoreMath.cpp src/GrassMemory.cpp src/GrassRandom.cpp ^
  src/GrassAudio.cpp src/GrassTg.cpp src/Grass2DGraphics.cpp src/GrassHardware.cpp ^
  src/GrassBaremetal.cpp src/GrassFFI.cpp src/GrassLangGen.cpp src/GrassMath3D.cpp ^
  src/GrassGfx3D.cpp src/GrassPhysics.cpp src/GrassIR.cpp src/GrassX86Encoder.cpp ^
  src/GrassObjWriter.cpp src/GrassElfWriter.cpp src/GrassAdvancedOpt.cpp ^
  src/GrassNativeCodegen.cpp src/GrassNativeDebug.cpp src/GrassSema.cpp ^
  src/GrassNativeOOP.cpp src/GrassNativeMemory.cpp src/GrassNativeStdlib.cpp ^
  -o bin/grassc.exe ^
  -lgdiplus -lgdi32 -lcomctl32 -lwinmm -lws2_32 -lwininet -lurlmon -lpsapi -liphlpapi -ldxgi -lpdh

:post_build
if %ERRORLEVEL% equ 0 (
    copy /Y bin\grassc.exe bin\grg.exe >nul
    if exist "C:\GrassGreen\bin" (
        copy /Y bin\grassc.exe C:\GrassGreen\bin\grassc.exe >nul
        copy /Y bin\grassc.exe C:\GrassGreen\bin\grg.exe >nul
    )
    echo [GrassCompiler] Build SUCCESSFUL: bin\grassc.exe ^& bin\grg.exe
    if exist "C:\GrassGreen\bin" echo [GrassCompiler] Successfully updated global C:\GrassGreen\bin

    echo [GrassCompiler] Building gpm package manager...
    g++ -std=c++17 -O3 src/gpm.cpp -o bin/gpm.exe
    if exist "C:\GrassGreen\bin" copy /Y bin\gpm.exe C:\GrassGreen\bin\gpm.exe >nul

    echo [GrassCompiler] Building libgrassvm embedded library...
    g++ -std=c++17 -O3 -shared src/grassvm_capi.cpp -Iinclude -o bin/grassvm.dll "-Wl,--out-implib,bin/grassvm.lib"
    if exist "C:\GrassGreen\bin" (
        copy /Y bin\grassvm.dll C:\GrassGreen\bin\grassvm.dll >nul
        copy /Y bin\grassvm.lib C:\GrassGreen\bin\grassvm.lib >nul
    )
) else (
    echo [GrassCompiler] Build FAILED with code %ERRORLEVEL%
)
exit /b %ERRORLEVEL%
