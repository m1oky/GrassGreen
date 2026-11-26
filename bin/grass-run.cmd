@echo off
chcp 65001 >nul
if "%~1"=="" goto :eof
cd /d "%~dp1"
title GrassGreen - %~nx1
if exist "%~dp0grassc.exe" (
    "%~dp0grassc.exe" -r "%~1"
) else (
    "%~dp0grg.exe" -r "%~1"
)
set "GG_EXIT=%ERRORLEVEL%"
echo.
echo ========================================================
echo [GrassGreen] Process exited with code %GG_EXIT%.
echo ========================================================
pause
exit /b %GG_EXIT%
