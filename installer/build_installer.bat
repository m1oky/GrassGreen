@echo off
setlocal
echo [GrassInstaller] Building GrassGreen v4.2.0 Sovereign Setup...

if not exist "bin" mkdir bin

echo [GrassInstaller] 1. Compiling and running C++ payload builder...
g++ -std=c++20 -O2 installer\make_payload.cpp -o installer\make_payload.exe
if %ERRORLEVEL% neq 0 (
    echo [GrassInstaller] ERROR: Failed to compile make_payload.cpp!
    exit /b %ERRORLEVEL%
)

installer\make_payload.exe
del installer\make_payload.exe

if not exist "installer\payload_data.h" (
    echo [GrassInstaller] ERROR: payload_data.h was not created!
    exit /b 1
)

echo [GrassInstaller] 2. Compiling installer\setup.cpp into setup.exe...
g++ -std=c++20 -O2 -s -mwindows ^
    installer\setup.cpp ^
    -o setup.exe ^
    -lcomctl32 -lshlwapi -lole32 -lshell32 -ladvapi32 -lgdi32

if %ERRORLEVEL% equ 0 (
    copy /Y setup.exe bin\setup.exe >nul
    echo [GrassInstaller] ========================================
    echo [GrassInstaller] Setup successfully built: setup.exe
    echo [GrassInstaller] Dual copy: bin\setup.exe
    echo [GrassInstaller] ========================================
) else (
    echo [GrassInstaller] ERROR: Failed to compile setup.exe (Code %ERRORLEVEL%)
)

exit /b %ERRORLEVEL%
