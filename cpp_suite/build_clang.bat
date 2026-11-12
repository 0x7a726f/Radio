@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line SDR ^& Analysis Suite - Clang/LLVM Build Script
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

set CORE_SRCS=src/sdr_device.cpp src/virtual_sdr.cpp src/sdr_controller.cpp src/dsp_pipeline.cpp src/cs16_writer.cpp src/waterfall.cpp src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp lib/kissfft/kiss_fft.c

echo.
echo [1/2] Compiling Headless CLI executable (radio_suite_cli.exe)...
clang++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb ^
    %CORE_SRCS% ^
    src/main_cli.cpp ^
    -o bin/radio_suite_cli.exe ^
    -lole32 -lkernel32 -luser32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo Error building CLI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo CLI build succeeded!

echo.
echo [2/2] Compiling GUI executable (radio_suite.exe)...
clang++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb -Ilib/imgui ^
    %CORE_SRCS% ^
    lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_widgets.cpp lib/imgui/imgui_tables.cpp ^
    lib/imgui/imgui_impl_win32.cpp lib/imgui/imgui_impl_dx11.cpp ^
    src/ui_record_panel.cpp src/ui_analyze_panel.cpp src/app_gui.cpp src/main_gui.cpp ^
    -mwindows ^
    -o bin/radio_suite.exe ^
    -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lkernel32 -luser32 -lgdi32 -lshell32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo Error building GUI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo GUI build succeeded!

echo.
echo ======================================================================
echo Build complete! Binaries located in cpp_suite\bin\
echo ======================================================================
if "%1" neq "--nopause" pause
