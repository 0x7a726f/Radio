@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo ======================================================================
echo   Hydrogen Line SDR ^& Analysis Suite - MSVC Build Script
echo ======================================================================

set "VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Community"
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional"
    if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VS_PATH=C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    if exist "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build\vcvars64.bat" set "VS_PATH=C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
)

if defined VS_PATH (
    echo Found Visual Studio at: %VS_PATH%
    call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    echo Visual Studio not found in default paths. Attempting build with current environment...
)

set "BUILD_DIR=build_msvc"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cd "%BUILD_DIR%"
cmake .. -G "Visual Studio 17 2022" -A x64 || cmake .. -G "NMake Makefiles"
cmake --build . --config Release

echo.
echo Copying dependencies to bin folder...
if not exist "..\bin" mkdir "..\bin"
if exist "Release\radio_suite.exe" copy /Y "Release\radio_suite.exe" "..\bin\"
if exist "Release\radio_suite_cli.exe" copy /Y "Release\radio_suite_cli.exe" "..\bin\"
if exist "radio_suite.exe" copy /Y "radio_suite.exe" "..\bin\"
if exist "radio_suite_cli.exe" copy /Y "radio_suite_cli.exe" "..\bin\"

cd ..
echo ======================================================================
echo Build complete! Binaries located in cpp_suite\bin\
echo ======================================================================
if "%1" neq "--nopause" pause
