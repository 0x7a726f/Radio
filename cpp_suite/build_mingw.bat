@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line SDR ^& Analysis Suite - MinGW-w64 Build Script
echo ======================================================================

:: Try to detect MinGW / g++ in PATH or standard winget / msys2 paths
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe" (
        set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
    ) else if exist "C:\mingw64\bin\g++.exe" (
        set "PATH=C:\mingw64\bin;%PATH%"
    ) else if exist "C:\msys64\ucrt64\bin\g++.exe" (
        set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    ) else if exist "C:\msys64\mingw64\bin\g++.exe" (
        set "PATH=C:\msys64\mingw64\bin;%PATH%"
    )
)

where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: g++ compiler not found!
    echo Please install MinGW or WinLibs via: winget install BrechtSanders.WinLibs.POSIX.UCRT
    if "%1" neq "--nopause" pause
    exit /b 1
)

echo Using compiler:
g++ --version | findstr /i "g++"

if not exist "bin" mkdir "bin"

set CORE_SRCS=src/sdr_device.cpp src/virtual_sdr.cpp src/sdr_controller.cpp src/dsp_pipeline.cpp src/cs16_writer.cpp src/waterfall.cpp src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp lib/kissfft/kiss_fft.c

echo.
echo [1/2] Compiling Headless CLI executable (bin\radio_suite_cli.exe)...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb ^
    %CORE_SRCS% ^
    src/main_cli.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/radio_suite_cli.exe ^
    -lole32 -lkernel32 -luser32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo Error building CLI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo CLI build succeeded: bin\radio_suite_cli.exe

echo.
echo [2/2] Compiling Desktop GUI executable (bin\radio_suite.exe)...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb -Ilib/imgui ^
    %CORE_SRCS% ^
    lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_widgets.cpp lib/imgui/imgui_tables.cpp lib/imgui/imgui_impl_win32.cpp lib/imgui/imgui_impl_dx11.cpp ^
    src/ui_record_panel.cpp src/ui_analyze_panel.cpp src/app_gui.cpp src/main_gui.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -mwindows ^
    -o bin/radio_suite.exe ^
    -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -lole32 -lkernel32 -luser32 -lgdi32 -lshell32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo Error building GUI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo GUI build succeeded: bin\radio_suite.exe

echo.
echo ======================================================================
echo   Build Complete!
echo   CLI: bin\radio_suite_cli.exe
echo   GUI: bin\radio_suite.exe
echo ======================================================================

if "%1" neq "--nopause" pause
