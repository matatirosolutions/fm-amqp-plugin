@echo off
setlocal EnableDelayedExpansion

:: ============================================================================
:: build-openssl-win.bat
::
:: Builds a static OpenSSL for Windows (x64) and places the output in
:: third_party\openssl\win\x64\.
::
:: Prerequisites — run this from a VS 2022 "x64 Native Tools Command Prompt"
:: (or any prompt where cl.exe and nmake are on PATH):
::
::   1. Strawberry Perl   https://strawberryperl.com  (needed by OpenSSL Configure)
::   2. NASM (optional)   https://nasm.us              (enables assembly optimisations)
::
:: Both are available via winget:
::   winget install StrawberryPerl.StrawberryPerl
::   winget install NASM.NASM
:: ============================================================================

set OPENSSL_VERSION=3.3.2
set OPENSSL_DIR=openssl-%OPENSSL_VERSION%
set OPENSSL_TAR=%OPENSSL_DIR%.tar.gz
set OPENSSL_URL=https://www.openssl.org/source/%OPENSSL_TAR%

set SCRIPT_DIR=%~dp0..\
set OUT_DIR=%SCRIPT_DIR%third_party\openssl\win\x64
set BUILD_DIR=%TEMP%\openssl-amqp-build

:: ── Verify prerequisites ─────────────────────────────────────────────────────

where perl >nul 2>&1
if errorlevel 1 (
    echo ERROR: perl not found. Install Strawberry Perl and ensure it is on PATH.
    echo   winget install StrawberryPerl.StrawberryPerl
    exit /b 1
)

where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found.
    echo Run this script from a "x64 Native Tools Command Prompt for VS 2022".
    exit /b 1
)

:: ── Download ─────────────────────────────────────────────────────────────────

echo =^> Setting up build directory: %BUILD_DIR%
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

echo =^> Downloading OpenSSL %OPENSSL_VERSION%
curl -L -o "%OPENSSL_TAR%" "%OPENSSL_URL%"
if errorlevel 1 ( echo ERROR: Download failed & exit /b 1 )

echo =^> Extracting
tar xf "%OPENSSL_TAR%"
if errorlevel 1 ( echo ERROR: Extraction failed & exit /b 1 )

:: ── Configure ────────────────────────────────────────────────────────────────

cd "%OPENSSL_DIR%"

echo =^> Configuring OpenSSL for VC-WIN64A (static, no-shared)
perl Configure VC-WIN64A ^
    no-shared ^
    no-tests ^
    no-apps ^
    --prefix="%BUILD_DIR%\install" ^
    --openssldir="%BUILD_DIR%\install\ssl"
if errorlevel 1 ( echo ERROR: Configure failed & exit /b 1 )

:: ── Build ────────────────────────────────────────────────────────────────────

echo =^> Building (this takes a few minutes)
nmake /nologo
if errorlevel 1 ( echo ERROR: Build failed & exit /b 1 )

echo =^> Installing
nmake /nologo install_sw
if errorlevel 1 ( echo ERROR: Install failed & exit /b 1 )

:: ── Copy to third_party ──────────────────────────────────────────────────────

echo =^> Copying to %OUT_DIR%
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%\lib"
mkdir "%OUT_DIR%\include"

copy "%BUILD_DIR%\install\lib\libssl.lib"    "%OUT_DIR%\lib\"
copy "%BUILD_DIR%\install\lib\libcrypto.lib" "%OUT_DIR%\lib\"
xcopy /e /i /q "%BUILD_DIR%\install\include\openssl" "%OUT_DIR%\include\openssl\"

echo =^> Cleaning up
cd /d "%SCRIPT_DIR%"
rmdir /s /q "%BUILD_DIR%"

echo.
echo Done. Output:
dir /b "%OUT_DIR%\lib\"
