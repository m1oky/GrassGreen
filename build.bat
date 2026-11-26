@echo off
setlocal
echo [GrassCompiler] Building GrassGreen v4.3.0...
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
    cl /nologo /std:c++20 /O2 /MP /EHsc /utf-8 /Isrc /Iinclude ^
      src/cli/grassc.cpp ^
      src/frontend/GrassSyntax.cpp src/frontend/GrassSema.cpp src/frontend/GrassImport.cpp src/frontend/GrassLangGen.cpp ^
      src/native/GrassIR.cpp src/native/GrassNativeCodegen.cpp src/native/GrassNativeOOP.cpp src/native/GrassNativeMemory.cpp src/native/GrassNativeStdlib.cpp src/native/GrassNativeDebug.cpp src/native/GrassAdvancedOpt.cpp src/native/GrassX86Encoder.cpp src/native/GrassObjWriter.cpp src/native/GrassElfWriter.cpp ^
      src/vm/GrassOptimizer.cpp src/vm/GrassJIT.cpp src/vm/GrassMemory.cpp src/vm/GrassFFI.cpp ^
      src/baremetal/GrassBoot.cpp src/baremetal/GrassBaremetal.cpp src/baremetal/GrassHAL.cpp src/baremetal/GrassMMIO.cpp src/baremetal/GrassASM.cpp src/baremetal/GrassKeyboard.cpp src/baremetal/GSE.cpp ^
      src/runtime/GrassFsys.cpp src/runtime/GrassNet.cpp src/runtime/GrassWeb.cpp src/runtime/GrassThread.cpp src/runtime/GrassTime.cpp src/runtime/GrassProc.cpp src/runtime/GrassSys.cpp src/runtime/GrassRandom.cpp src/runtime/GrassJson.cpp src/runtime/GrassTg.cpp src/runtime/GrassHardware.cpp ^
      src/media/Grass2DGraphics.cpp src/media/GrassGfx3D.cpp src/media/gui_engine.cpp src/media/GrassAudio.cpp src/media/GrassPhysics.cpp src/media/CoreMath.cpp src/media/FastMath.cpp src/media/GrassMath3D.cpp ^
      /Fe:bin/grassc.exe /link gdiplus.lib gdi32.lib comctl32.lib winmm.lib ws2_32.lib wininet.lib urlmon.lib psapi.lib iphlpapi.lib dxgi.lib pdh.lib
    goto :post_build
)

rem Fallback to sequential g++
echo [GrassCompiler] Compiling with g++...
g++ -std=c++20 -O3 -s -static -static-libgcc -static-libstdc++ -Isrc -Iinclude ^
  src/cli/grassc.cpp ^
  src/frontend/GrassSyntax.cpp src/frontend/GrassSema.cpp src/frontend/GrassImport.cpp src/frontend/GrassLangGen.cpp ^
  src/native/GrassIR.cpp src/native/GrassNativeCodegen.cpp src/native/GrassNativeOOP.cpp src/native/GrassNativeMemory.cpp src/native/GrassNativeStdlib.cpp src/native/GrassNativeDebug.cpp src/native/GrassAdvancedOpt.cpp src/native/GrassX86Encoder.cpp src/native/GrassObjWriter.cpp src/native/GrassElfWriter.cpp ^
  src/vm/GrassOptimizer.cpp src/vm/GrassJIT.cpp src/vm/GrassMemory.cpp src/vm/GrassFFI.cpp ^
  src/baremetal/GrassBoot.cpp src/baremetal/GrassBaremetal.cpp src/baremetal/GrassHAL.cpp src/baremetal/GrassMMIO.cpp src/baremetal/GrassASM.cpp src/baremetal/GrassKeyboard.cpp src/baremetal/GSE.cpp ^
  src/runtime/GrassFsys.cpp src/runtime/GrassNet.cpp src/runtime/GrassWeb.cpp src/runtime/GrassThread.cpp src/runtime/GrassTime.cpp src/runtime/GrassProc.cpp src/runtime/GrassSys.cpp src/runtime/GrassRandom.cpp src/runtime/GrassJson.cpp src/runtime/GrassTg.cpp src/runtime/GrassHardware.cpp ^
  src/media/Grass2DGraphics.cpp src/media/GrassGfx3D.cpp src/media/gui_engine.cpp src/media/GrassAudio.cpp src/media/GrassPhysics.cpp src/media/CoreMath.cpp src/media/FastMath.cpp src/media/GrassMath3D.cpp ^
  -o bin/grassc.exe ^
  -lgdiplus -lgdi32 -lcomctl32 -lwinmm -lws2_32 -lwininet -lurlmon -lpsapi -liphlpapi -ldxgi -lpdh

:post_build
if %ERRORLEVEL% equ 0 (
    copy /Y bin\grassc.exe bin\grg.exe >nul
    echo [GrassCompiler] Build SUCCESSFUL: bin\grassc.exe ^& bin\grg.exe

    echo [GrassCompiler] Building gpm package manager...
    g++ -std=c++17 -O3 -Isrc -Iinclude src/cli/gpm.cpp -o bin/gpm.exe

    echo [GrassCompiler] Building libgrassvm embedded library...
    g++ -std=c++17 -O3 -shared src/vm/grassvm_capi.cpp src/media/GrassGfx3D.cpp src/media/GrassPhysics.cpp src/media/GrassMath3D.cpp -Isrc -Iinclude -o bin/grassvm.dll "-Wl,--out-implib,bin/grassvm.lib" -lgdi32 -luser32

    if exist "C:\GrassGreen" (
        if not exist "C:\GrassGreen\bin" mkdir "C:\GrassGreen\bin"
        if not exist "C:\GrassGreen\stdlib" mkdir "C:\GrassGreen\stdlib"
        copy /Y bin\*.exe "C:\GrassGreen\bin\" >nul
        if exist "bin\grassvm.dll" copy /Y bin\grassvm.dll "C:\GrassGreen\bin\" >nul
        if exist "bin\grassvm.lib" copy /Y bin\grassvm.lib "C:\GrassGreen\bin\" >nul
        xcopy /Y /E /I stdlib\* "C:\GrassGreen\stdlib\" >nul
        echo [GrassCompiler] Successfully updated global C:\GrassGreen\bin and synchronized C:\GrassGreen\stdlib
    )
) else (
    echo [GrassCompiler] Build FAILED with code %ERRORLEVEL%
)
exit /b %ERRORLEVEL%
