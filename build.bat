@echo off
setlocal
echo [GrassCompiler] Building GrassGreen v4.3...
if not exist "bin" mkdir bin

g++ -std=c++20 -O3 -s -static -static-libgcc -static-libstdc++ ^
  src/grassc.cpp ^
  src/GrassHAL.cpp ^
  src/GrassMMIO.cpp ^
  src/GrassASM.cpp ^
  src/GrassKeyboard.cpp ^
  src/GrassBoot.cpp ^
  src/GrassThread.cpp ^
  src/GrassJIT.cpp ^
  src/GrassImport.cpp ^
  src/GrassOptimizer.cpp ^
  src/GrassSyntax.cpp ^
  src/GrassProc.cpp ^
  src/GrassFsys.cpp ^
  src/GrassTime.cpp ^
  src/GrassNet.cpp ^
  src/GrassJson.cpp ^
  src/GrassSys.cpp ^
  src/gui_engine.cpp ^
  src/GrassWeb.cpp ^
  src/GSE.cpp ^
  src/FastMath.cpp ^
  src/CoreMath.cpp ^
  src/GrassMemory.cpp ^
  src/GrassRandom.cpp ^
  src/GrassAudio.cpp ^
  src/GrassTg.cpp ^
  src/Grass2DGraphics.cpp ^
  src/GrassHardware.cpp ^
  src/GrassBaremetal.cpp ^
  src/GrassFFI.cpp ^
  src/GrassLangGen.cpp ^
  src/GrassMath3D.cpp ^
  src/GrassGfx3D.cpp ^
  src/GrassPhysics.cpp ^
  -o bin/grassc.exe ^
  -lgdiplus -lgdi32 -lcomctl32 -lwinmm -lws2_32 -lwininet -lurlmon -lpsapi -liphlpapi -ldxgi -lpdh

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
