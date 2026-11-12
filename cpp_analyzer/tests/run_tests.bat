@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line C++ Scientific DSP Pipeline - Automated Test Suite
echo ======================================================================

:: Locate g++
where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    if exist "%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\g++.exe" (
        set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
    ) else if exist "C:\mingw64\bin\g++.exe" (
        set "PATH=C:\mingw64\bin;%PATH%"
    ) else if exist "C:\msys64\ucrt64\bin\g++.exe" (
        set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    )
)

where g++ >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo Error: g++ compiler not found!
    if "%1" neq "--nopause" pause
    exit /b 1
)

echo Using compiler:
g++ --version | findstr /i "g++"
echo.

if not exist "bin" mkdir "bin"

set CXXFLAGS=-std=c++17 -O3 -mavx2 -mfma -msse4.1 -I../include -I../lib -I../lib/kissfft -I. -static -static-libgcc -static-libstdc++

echo [1/5] Compiling and running test_streaming_processor...
g++ %CXXFLAGS% test_streaming_processor.cpp ../src/streaming_processor.cpp ../lib/kissfft/kiss_fft.c -o bin/test_streaming_processor.exe
if %ERRORLEVEL% neq 0 (
    echo Error compiling test_streaming_processor!
    exit /b 1
)
bin\test_streaming_processor.exe
if %ERRORLEVEL% neq 0 (
    echo Test failed: test_streaming_processor!
    exit /b 1
)

echo [2/5] Compiling and running test_baseline_solvers...
g++ %CXXFLAGS% test_baseline_solvers.cpp ../src/baseline_fitter.cpp -o bin/test_baseline_solvers.exe
if %ERRORLEVEL% neq 0 (
    echo Error compiling test_baseline_solvers!
    exit /b 1
)
bin\test_baseline_solvers.exe
if %ERRORLEVEL% neq 0 (
    echo Test failed: test_baseline_solvers!
    exit /b 1
)

echo [3/5] Compiling and running test_astrophysics...
g++ %CXXFLAGS% test_astrophysics.cpp ../src/astrophysics.cpp -o bin/test_astrophysics.exe
if %ERRORLEVEL% neq 0 (
    echo Error compiling test_astrophysics!
    exit /b 1
)
bin\test_astrophysics.exe
if %ERRORLEVEL% neq 0 (
    echo Test failed: test_astrophysics!
    exit /b 1
)

echo [4/5] Compiling and running test_sidecar_reader...
g++ %CXXFLAGS% test_sidecar_reader.cpp ../src/sidecar_reader.cpp -o bin/test_sidecar_reader.exe
if %ERRORLEVEL% neq 0 (
    echo Error compiling test_sidecar_reader!
    exit /b 1
)
bin\test_sidecar_reader.exe
if %ERRORLEVEL% neq 0 (
    echo Test failed: test_sidecar_reader!
    exit /b 1
)

echo [5/5] Compiling and running test_edge_cases...
g++ %CXXFLAGS% test_edge_cases.cpp ../src/streaming_processor.cpp ../lib/kissfft/kiss_fft.c -o bin/test_edge_cases.exe
if %ERRORLEVEL% neq 0 (
    echo Error compiling test_edge_cases!
    exit /b 1
)
bin\test_edge_cases.exe
if %ERRORLEVEL% neq 0 (
    echo Test failed: test_edge_cases!
    exit /b 1
)

echo.
echo ======================================================================
echo   ALL AUTOMATED UNIT TESTS PASSED SUCCESSFULLY! (100%%)
echo ======================================================================
echo.

if "%1" neq "--nopause" pause
exit /b 0
