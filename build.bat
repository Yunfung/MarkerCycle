@echo off
rem ===================================================================================================
rem  MarkerCycle ( yi jian xun huan biao ji ) build script
rem  Builds addons\MarkerCycle.dll with the MSVC x64 toolchain.
rem ===================================================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "SRC=%ROOT%src\MarkerCycle.cpp"
set "IMGUI=%ROOT%src\imgui\imgui.cpp %ROOT%src\imgui\imgui_draw.cpp %ROOT%src\imgui\imgui_tables.cpp %ROOT%src\imgui\imgui_widgets.cpp"
set "OUTDIR=%ROOT%addons"
set "OBJDIR=%ROOT%build"
set "DLL=%OUTDIR%\MarkerCycle.dll"
set "BUILDDLL=%OBJDIR%\MarkerCycle.dll"

set "VCVARS="
call :TryVcVars "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
call :TryVcVars "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS goto :novc

echo [1/3] Using MSVC: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 goto :fail

if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

echo [2/3] Compiling...
pushd "%OBJDIR%"
cl /nologo /W3 /O2 /MT /EHsc /std:c++17 /utf-8 /DNDEBUG /LD /I"%ROOT%src" /I"%ROOT%src\imgui" "%SRC%" %IMGUI% /Fe:"%BUILDDLL%"
set "BUILD_RESULT=%errorlevel%"
popd

if not "%BUILD_RESULT%"=="0" goto :fail
if not exist "%BUILDDLL%" goto :fail

copy /y "%BUILDDLL%" "%DLL%" >nul
if errorlevel 1 goto :fail

echo [3/3] OK -^> %DLL%
for %%F in ("%DLL%") do echo         size: %%~zF bytes   time: %%~tF
exit /b 0

:TryVcVars
if defined VCVARS goto :eof
if exist "%~1" set "VCVARS=%~1"
goto :eof

:novc
echo [ERROR] vcvars64.bat was not found.
echo         Install "Visual Studio 2022 Build Tools" with the C++ workload, then run this again.
goto :fail

:fail
echo.
echo Build failed.
exit /b 1
