@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0\.."

echo ======================================================================
echo   Hydrogen Line Suite - Automated Test Suite Runner
echo ======================================================================

:: Compiler detection
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
    echo [ERROR] g++ compiler not found!
    echo Please install MinGW-w64 or WinLibs.
    exit /b 1
)

if not exist "bin" mkdir "bin"

if "%1"=="--skip-build" goto EXEC_PHASE
if "%1"=="--no-build" goto EXEC_PHASE

set CORE_SRCS=src/sdr_device.cpp src/virtual_sdr.cpp src/sdr_controller.cpp src/dsp_pipeline.cpp src/cs16_writer.cpp src/waterfall.cpp src/sidecar_reader.cpp src/streaming_processor.cpp src/dc_excision.cpp src/rfi_filter.cpp src/baseline_fitter.cpp src/astrophysics.cpp src/plot_renderer.cpp src/exporter.cpp lib/kissfft/kiss_fft.c

echo.
echo ======================================================================
echo   [BUILD PHASE] Compiling Test Executables...
echo ======================================================================

echo [1/5] Compiling test_suite_state.exe...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude ^
    tests/test_suite_state.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/test_suite_state.exe
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_suite_state.exe!
    exit /b 1
)

echo [2/5] Compiling test_end_to_end_bridge.exe...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb ^
    %CORE_SRCS% ^
    tests/test_end_to_end_bridge.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/test_end_to_end_bridge.exe ^
    -lole32 -lkernel32 -luser32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_end_to_end_bridge.exe!
    exit /b 1
)

echo [3/5] Compiling test_cli_commands.exe...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude ^
    tests/test_cli_commands.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/test_cli_commands.exe
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_cli_commands.exe!
    exit /b 1
)

echo [4/5] Compiling test_performance_stress.exe...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb ^
    %CORE_SRCS% ^
    tests/test_performance_stress.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/test_performance_stress.exe ^
    -lole32 -lkernel32 -luser32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_performance_stress.exe!
    exit /b 1
)

echo [5/5] Compiling test_dsp_optimizations.exe...
g++ -std=c++17 -O3 -mavx2 -mfma -msse4.1 ^
    -Iinclude -Ilib -Ilib/kissfft -Ilib/rtlsdr -Ilib/stb ^
    %CORE_SRCS% ^
    tests/test_dsp_optimizations.cpp ^
    -static -static-libgcc -static-libstdc++ ^
    -o bin/test_dsp_optimizations.exe ^
    -lole32 -lkernel32 -luser32 -lpsapi
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Failed to compile test_dsp_optimizations.exe!
    exit /b 1
)

:EXEC_PHASE
echo.
echo ======================================================================
echo   [EXECUTION PHASE] Running All Automated Tests and Benchmarks...
echo ======================================================================

set TESTS_FAILED=0

echo.
echo [RUN] bin\test_suite_state.exe
bin\test_suite_state.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] test_suite_state failed.
    set /a TESTS_FAILED+=1
) else (
    echo [PASS] test_suite_state passed successfully.
)

echo.
echo [RUN] bin\test_end_to_end_bridge.exe
bin\test_end_to_end_bridge.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] test_end_to_end_bridge failed.
    set /a TESTS_FAILED+=1
) else (
    echo [PASS] test_end_to_end_bridge passed successfully.
)

echo.
echo [RUN] bin\test_cli_commands.exe
bin\test_cli_commands.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] test_cli_commands failed.
    set /a TESTS_FAILED+=1
) else (
    echo [PASS] test_cli_commands passed successfully.
)

echo.
echo [RUN] bin\test_performance_stress.exe
bin\test_performance_stress.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] test_performance_stress failed.
    set /a TESTS_FAILED+=1
) else (
    echo [PASS] test_performance_stress passed successfully.
)

echo.
echo [RUN] bin\test_dsp_optimizations.exe
bin\test_dsp_optimizations.exe
if %ERRORLEVEL% neq 0 (
    echo [FAIL] test_dsp_optimizations failed.
    set /a TESTS_FAILED+=1
) else (
    echo [PASS] test_dsp_optimizations passed successfully.
)

echo.
echo ======================================================================
if %TESTS_FAILED% equ 0 (
    echo   ALL 5 AUTOMATED TEST SUITES PASSED [100%%]
    echo   Validation, Bridge, CLI, Performance Benchmarks, and DSP Unit Tests OK.
    echo ======================================================================
    exit /b 0
) else (
    echo   [ERROR] Tests failed: %TESTS_FAILED%
    echo ======================================================================
    exit /b 1
)
