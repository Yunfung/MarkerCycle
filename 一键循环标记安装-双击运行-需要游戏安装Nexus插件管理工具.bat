@echo off
rem ===================================================================================================
rem  MarkerCycle installer launcher (works only when Nexus is installed in the game)
rem  Double-click to run. Passes all arguments to the PowerShell installer next to this file.
rem ===================================================================================================
setlocal
title MarkerCycle - yi jian xun huan biao ji
cd /d "%~dp0"

set "PS1="
for %%f in ("%~dp0*.ps1") do set "PS1=%%f"

if not defined PS1 (
    echo [ERROR] No .ps1 installer found next to this .bat file.
    echo         Please keep all files of this package in the same folder.
    pause
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" %*
set "RC=%errorlevel%"

echo.
pause
exit /b %RC%
