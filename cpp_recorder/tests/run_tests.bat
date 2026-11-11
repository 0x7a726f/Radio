@echo off
setlocal enabledelayedexpansion

echo ======================================================================
echo   Hydrogen Line SDR C++ Recorder - Automated Test Suite Runner
echo ======================================================================

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
    exit /b 1
)

cd /d "%~dp0.."
if not exist "bin\tests\obj" mkdir "bin\tests\obj"

set "CFLAGS=-std=c++17 -O3 -mavx2 -mfma -msse4.1 -Iinclude -Ilib/kissfft -Ilib/rtlsdr -Itests"
set "LDFLAGS=-static -static-libgcc -static-libstdc++ -lole32 -lkernel32 -luser32 -lpsapi"
set "COMMON_OBJS=bin/tests/obj/sdr_device.o bin/tests/obj/virtual_sdr.o bin/tests/obj/sdr_controller.o bin/tests/obj/dsp_pipeline.o bin/tests/obj/cs16_writer.o bin/tests/obj/waterfall.o bin/tests/obj/kiss_fft.o"

echo.
echo [*] Compiling shared core object files (cached for rapid testing)...
g++ %CFLAGS% -c src/sdr_device.cpp -o bin/tests/obj/sdr_device.o
g++ %CFLAGS% -c src/virtual_sdr.cpp -o bin/tests/obj/virtual_sdr.o
g++ %CFLAGS% -c src/sdr_controller.cpp -o bin/tests/obj/sdr_controller.o
g++ %CFLAGS% -c src/dsp_pipeline.cpp -o bin/tests/obj/dsp_pipeline.o
g++ %CFLAGS% -c src/cs16_writer.cpp -o bin/tests/obj/cs16_writer.o
g++ %CFLAGS% -c src/waterfall.cpp -o bin/tests/obj/waterfall.o
gcc -O3 -Ilib/kissfft -c lib/kissfft/kiss_fft.c -o bin/tests/obj/kiss_fft.o

echo.
echo [1/6] Linking test_tuning_coalescing.exe...
g++ %CFLAGS% tests/test_tuning_coalescing.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/test_tuning_coalescing.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile test_tuning_coalescing!
    exit /b 1
)

echo [2/6] Linking test_dsp_pipeline.exe...
g++ %CFLAGS% tests/test_dsp_pipeline.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/test_dsp_pipeline.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile test_dsp_pipeline!
    exit /b 1
)

echo [3/6] Linking test_cs16_writer.exe...
g++ %CFLAGS% tests/test_cs16_writer.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/test_cs16_writer.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile test_cs16_writer!
    exit /b 1
)

echo [4/6] Linking test_virtual_sdr.exe...
g++ %CFLAGS% tests/test_virtual_sdr.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/test_virtual_sdr.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile test_virtual_sdr!
    exit /b 1
)

echo [5/6] Linking benchmarks_stress.exe...
g++ %CFLAGS% tests/benchmarks_stress.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/benchmarks_stress.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile benchmarks_stress!
    exit /b 1
)

echo [6/6] Linking test_simd_lockfree.exe...
g++ %CFLAGS% tests/test_simd_lockfree.cpp %COMMON_OBJS% %LDFLAGS% -o bin/tests/test_simd_lockfree.exe
if %ERRORLEVEL% neq 0 (
    echo FAILED to compile test_simd_lockfree!
    exit /b 1
)

echo.
echo ======================================================================
echo   EXECUTING TEST SUITES AND BENCHMARKS
echo ======================================================================

set "ALL_PASS=1"

echo.
echo --- Running: test_tuning_coalescing.exe ---
bin\tests\test_tuning_coalescing.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
echo --- Running: test_dsp_pipeline.exe ---
bin\tests\test_dsp_pipeline.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
echo --- Running: test_cs16_writer.exe ---
bin\tests\test_cs16_writer.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
echo --- Running: test_virtual_sdr.exe ---
bin\tests\test_virtual_sdr.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
echo --- Running: benchmarks_stress.exe ---
bin\tests\benchmarks_stress.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
echo --- Running: test_simd_lockfree.exe ---
bin\tests\test_simd_lockfree.exe
if %ERRORLEVEL% neq 0 set "ALL_PASS=0"

echo.
if "%ALL_PASS%"=="1" (
    echo ======================================================================
    echo   OVERALL STATUS: ALL TEST SUITES AND BENCHMARKS PASSED (100%%)
    echo ======================================================================
    exit /b 0
) else (
    echo ======================================================================
    echo   OVERALL STATUS: SOME TESTS FAILED!
    echo ======================================================================
    exit /b 1
)
