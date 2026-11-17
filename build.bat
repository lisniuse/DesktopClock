@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VSPATH="

if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\2022\Community"
if not defined VSPATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
if not defined VSPATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
if not defined VSPATH if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined VSPATH if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VSPATH=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"

:have_vs
if not defined VSPATH (
    echo [error] Visual Studio with the C++ workload was not found.
    exit /b 1
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (
    echo [error] Failed to initialize the MSVC environment.
    exit /b 1
)

if not exist build mkdir build

rc /nologo /fo build\app.res res\app.rc
if errorlevel 1 exit /b 1

cl /nologo /utf-8 /std:c++17 /EHsc /O2 /W4 ^
    /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX ^
    /I src /Fo:build\ /Fe:build\DesktopClock.exe ^
    src\main.cpp src\clock_window.cpp src\config.cpp build\app.res ^
    /link /SUBSYSTEM:WINDOWS

if errorlevel 1 exit /b 1
echo.
echo Built: %~dp0build\DesktopClock.exe
