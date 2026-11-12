@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line C++ Analyzer - Clang/LLVM Build Script
echo ======================================================================

where clang++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "%ProgramFiles%\LLVM\bin\clang++.exe" (
        set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
    ) else if exist "C:\LLVM\bin\clang++.exe" (
        set "PATH=C:\LLVM\bin;%PATH%"
    )
)

where clang++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: clang++ compiler not found!
    echo Please install LLVM via: winget install LLVM.LLVM
    if "%1" neq "--nopause" pause
    exit /b 1
)

echo Using clang++ compiler:
clang++ --version | findstr /i "clang version"

if not exist "bin" mkdir "bin"

echo.
echo [1/2] Compiling Headless CLI executable (bin\astro_analyzer_cli.exe)...
clang++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib/kissfft -Ilib/stb ^
    src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp ^
    src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp ^
    lib/kissfft/kiss_fft.c ^
    src/main_cli.cpp ^
    -o bin/astro_analyzer_cli.exe ^
    -lole32 -lkernel32 -luser32
if %ERRORLEVEL% neq 0 (
    echo Error building CLI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo CLI build succeeded!

echo.
echo [2/2] Compiling GUI executable (bin\astro_analyzer_gui.exe)...
clang++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib/kissfft -Ilib/stb -Ilib/imgui ^
    src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp ^
    src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp ^
    lib/kissfft/kiss_fft.c ^
    lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_widgets.cpp lib/imgui/imgui_tables.cpp ^
    lib/imgui/imgui_impl_win32.cpp lib/imgui/imgui_impl_dx11.cpp ^
    src/app_gui.cpp src/main_gui.cpp ^
    -o bin/astro_analyzer_gui.exe ^
    -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lkernel32 -luser32 -lgdi32 -lshell32
if %ERRORLEVEL% neq 0 (
    echo Error building GUI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo GUI build succeeded!

echo.
echo ======================================================================
echo Build complete! Binaries located in cpp_analyzer\bin\
echo ======================================================================
if "%1" neq "--nopause" pause
