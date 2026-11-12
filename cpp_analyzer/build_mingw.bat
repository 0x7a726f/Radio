@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line C++ Analyzer - MinGW-w64 Build Script
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

echo.
echo [1/2] Compiling Headless CLI executable (bin\astro_analyzer_cli.exe)...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/stb ^
    src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp ^
    src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp ^
    lib/kissfft/kiss_fft.c ^
    src/main_cli.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/astro_analyzer_cli.exe ^
    -lole32 -lkernel32 -luser32
if %ERRORLEVEL% neq 0 (
    echo Error building CLI executable!
    if "%1" neq "--nopause" pause
    exit /b 1
)
echo CLI build succeeded!

echo.
echo [2/2] Compiling Desktop GUI executable (bin\astro_analyzer_gui.exe)...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/stb -Ilib/imgui ^
    src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp ^
    src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp ^
    lib/kissfft/kiss_fft.c ^
    lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_widgets.cpp lib/imgui/imgui_tables.cpp ^
    lib/imgui/imgui_impl_win32.cpp lib/imgui/imgui_impl_dx11.cpp ^
    src/app_gui.cpp src/main_gui.cpp ^
    -static -static-libgcc -static-libstdc++ ^
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
echo Build complete! Binaries are in: cpp_analyzer\bin\
echo   - bin\astro_analyzer_cli.exe (Headless CLI)
echo   - bin\astro_analyzer_gui.exe (Desktop DirectX 11 GUI)
echo ======================================================================
if "%1" neq "--nopause" pause
