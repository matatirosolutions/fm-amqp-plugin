@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: build-win.bat
::
:: Builds AMQPFMPlugin.fmx64 for Windows x64.
::
:: Run from a VS 2022 "x64 Native Tools Command Prompt" (or any prompt where
:: cl.exe and cmake are on PATH).
::
:: Prerequisites:
::   - Visual Studio 2022 (or Build Tools) with C++ workload
::   - CMake 3.21+   https://cmake.org  (or: winget install Kitware.CMake)
::   - Git            https://git-scm.com (needed by FetchContent for rabbitmq-c)
::   - SDK files in sdk\  (FMWrapper.lib + headers)
::   - OpenSSL static libs in third_party\openssl\win\x64\
::     (run build-openssl-win.bat first if not present)
::
:: Output: build\win\Release\AMQPFMPlugin.fmx64
:: Install: copy to %APPDATA%\FileMaker\Extensions\
:: ============================================================================

set SCRIPT_DIR=%~dp0..\

:: ── Prerequisites check ──────────────────────────────────────────────────────

where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake not found. Install CMake and ensure it is on PATH.
    echo   winget install Kitware.CMake
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found.
    echo Run this script from a "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

if not exist "%SCRIPT_DIR%sdk\Libraries\Win\x64\FMWrapper.lib" (
    echo ERROR: sdk\Libraries\Win\x64\FMWrapper.lib not found.
    echo Download the FileMaker Plugin SDK and place it in sdk\
    exit /b 1
)

:: ── Configure ────────────────────────────────────────────────────────────────

echo =^> Configuring
cmake -S "%SCRIPT_DIR%." -B "%SCRIPT_DIR%build\win" ^
    -G "Visual Studio 17 2022" -A x64
if errorlevel 1 ( echo ERROR: CMake configure failed & exit /b 1 )

:: ── Build ────────────────────────────────────────────────────────────────────

echo =^> Building Release
cmake --build "%SCRIPT_DIR%build\win" --config Release
if errorlevel 1 ( echo ERROR: Build failed & exit /b 1 )

echo.
echo Done: %SCRIPT_DIR%build\win\Release\AMQPFMPlugin.fmx64
echo.
echo To install, copy to:
echo   %APPDATA%\FileMaker\Extensions\AMQPFMPlugin.fmx64
