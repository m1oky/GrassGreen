@echo off
setlocal
echo [GrassCompiler] Building GrassGreen v4.2...
if not exist "bin" mkdir bin

g++ -std=c++20 -O3 -s ^
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
  -o bin/grassc.exe ^
  -lgdiplus -lgdi32 -lcomctl32 -lwinmm -lws2_32 -lwininet -lurlmon -lpsapi

if %ERRORLEVEL% equ 0 (
    copy /Y bin\grassc.exe bin\grg.exe >nul
    if exist "C:\GrassGreen\bin" (
        copy /Y bin\grassc.exe C:\GrassGreen\bin\grassc.exe >nul
        copy /Y bin\grassc.exe C:\GrassGreen\bin\grg.exe >nul
    )
    echo [GrassCompiler] Build SUCCESSFUL: bin\grassc.exe ^& bin\grg.exe
    if exist "C:\GrassGreen\bin" echo [GrassCompiler] Successfully updated global C:\GrassGreen\bin
) else (
    echo [GrassCompiler] Build FAILED with code %ERRORLEVEL%
)
exit /b %ERRORLEVEL%
