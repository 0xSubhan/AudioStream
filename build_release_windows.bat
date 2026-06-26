@echo off
:: =============================================================================
:: AudioStream — Windows Release Builder
:: Produces a self-contained  dist\AudioStream.exe
::
:: Prerequisites (install once):
::   1. Python 3.10+         https://python.org/downloads
::   2. CMake 3.20+          https://cmake.org/download
::   3. Visual Studio 2022 Build Tools (C++ workload)
::              https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
::   4. vcpkg  https://vcpkg.io  — then run:
::              vcpkg install opus:x64-windows
::              vcpkg integrate install
::
:: Usage:
::   build_release_windows.bat
:: =============================================================================

setlocal EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "VENV_DIR=%SCRIPT_DIR%venv"
set "PC_APP_DIR=%SCRIPT_DIR%pc_app"
set "BUILD_DIR=%SCRIPT_DIR%build"
set "DIST_DIR=%SCRIPT_DIR%dist"

echo.
echo [build] AudioStream Windows Release Builder
echo ==============================================

:: ── Step 1: Verify prerequisites ─────────────────────────────────────────────
echo.
echo [build] Step 1/5 — Checking prerequisites...

where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [error] cmake not found. Install CMake from https://cmake.org/download and re-run.
    exit /b 1
)

where python >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [error] python not found. Install Python 3.10+ from https://python.org and re-run.
    exit /b 1
)

echo [build] Prerequisites OK.

:: ── Step 2: Build C++ engine ─────────────────────────────────────────────────
echo.
echo [build] Step 2/5 — Building C++ capture engine (WASAPI)...

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Detect vcpkg toolchain if VCPKG_ROOT is set
set "VCPKG_TOOLCHAIN="
if defined VCPKG_ROOT (
    set "VCPKG_TOOLCHAIN=-DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows"
    echo [build] vcpkg detected at %VCPKG_ROOT%
) else (
    echo [warn]  VCPKG_ROOT not set. If Opus is not in your PATH, the build will fail.
    echo [warn]  Set VCPKG_ROOT to your vcpkg install dir and re-run, or install Opus manually.
)

cmake -B "%BUILD_DIR%" -S "%SCRIPT_DIR%" -DCMAKE_BUILD_TYPE=Release %VCPKG_TOOLCHAIN%
if %ERRORLEVEL% neq 0 (
    echo [error] CMake configure step failed. Check output above.
    exit /b 1
)

cmake --build "%BUILD_DIR%" --config Release
if %ERRORLEVEL% neq 0 (
    echo [error] CMake build step failed. Check output above.
    exit /b 1
)

:: Verify that the .pyd file was produced in pc_app/
set "PYD_FILE="
for %%F in ("%PC_APP_DIR%\audiostream_core*.pyd") do set "PYD_FILE=%%F"
if not defined PYD_FILE (
    echo [error] C++ build succeeded but audiostream_core*.pyd not found in pc_app\
    exit /b 1
)
echo [build] C++ engine built: %PYD_FILE%

:: ── Step 3: Convert icon to .ico ─────────────────────────────────────────────
echo.
echo [build] Step 3/5 — Preparing Windows icon...

set "ICO_FILE=%SCRIPT_DIR%assets\icon.ico"
if not exist "%ICO_FILE%" (
    set "PNG_FILE=%SCRIPT_DIR%android_app\assets\icon.png"
    if exist "!PNG_FILE!" (
        python -c "from PIL import Image; img=Image.open(r'!PNG_FILE!'); img.save(r'!ICO_FILE!', format='ICO', sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(16,16)])" 2>nul
        if %ERRORLEVEL% equ 0 (
            echo [build] icon.ico generated.
        ) else (
            echo [warn]  Could not convert icon.png to icon.ico (Pillow not installed). The .exe will have no custom icon.
        )
    ) else (
        echo [warn]  icon.png not found. The .exe will have no custom icon.
    )
) else (
    echo [build] icon.ico already exists.
)

:: ── Step 4: Python venv + dependencies ───────────────────────────────────────
echo.
echo [build] Step 4/5 — Setting up Python virtual environment...

if not exist "%VENV_DIR%" (
    python -m venv "%VENV_DIR%"
)

"%VENV_DIR%\Scripts\pip" install --quiet --upgrade pip
"%VENV_DIR%\Scripts\pip" install --quiet -r "%PC_APP_DIR%\requirements.txt"
"%VENV_DIR%\Scripts\pip" install --quiet pyinstaller pillow

:: ── Step 5: Bundle with PyInstaller ──────────────────────────────────────────
echo.
echo [build] Step 5/5 — Bundling into AudioStream.exe...

cd /d "%PC_APP_DIR%"
"%VENV_DIR%\Scripts\pyinstaller" AudioStream.spec ^
    --distpath "%DIST_DIR%" ^
    --workpath "%BUILD_DIR%\pyinstaller_work" ^
    --noconfirm

if %ERRORLEVEL% neq 0 (
    echo [error] PyInstaller failed. Check output above.
    exit /b 1
)

:: ── Done ──────────────────────────────────────────────────────────────────────
set "EXE=%DIST_DIR%\AudioStream.exe"
if not exist "%EXE%" (
    echo [error] AudioStream.exe not found after build.
    exit /b 1
)

for %%A in ("%EXE%") do set "SIZE=%%~zA"
set /a SIZE_MB=%SIZE% / 1048576

echo.
echo ======================================================
echo   Build complete!
echo   Binary : dist\AudioStream.exe  (~%SIZE_MB% MB)
echo   Run it : dist\AudioStream.exe
echo.
echo   Upload dist\AudioStream.exe to GitHub Releases so
echo   Windows users can download and run it directly.
echo ======================================================
echo.

endlocal
