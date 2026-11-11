@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"
echo ========================================================
echo    Building C++ Demodulator and Native Desktop App
echo ========================================================
echo.

:: 1. Check if cl.exe is already available in PATH
where cl.exe >nul 2>&1
if %errorlevel% equ 0 (
    echo [*] Found cl.exe in PATH.
    goto :compile
)

:: 2. Search for Visual Studio using vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_PATH=%%i"
    )
    if defined VS_PATH (
        if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
            echo [*] Initializing Visual Studio from !VS_PATH!...
            call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat"
            goto :compile
        )
    )
)

:: 3. Check known installation directories
for %%d in (
    "C:\Program Files\Microsoft Visual Studio\2022\Community"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Professional"
    "D:\Program Files\Microsoft Visual Studio\2022\Community"
    "D:\Visual Studio"
    "E:\Visual Studio"
) do (
    if exist "%%~d\VC\Auxiliary\Build\vcvars64.bat" (
        echo [*] Initializing Visual Studio from %%~d...
        call "%%~d\VC\Auxiliary\Build\vcvars64.bat"
        goto :compile
    )
)

echo [!] Could not locate Visual Studio C++ compiler.
echo     Please run this script from a Visual Studio Developer Command Prompt.
pause
exit /b 1

:compile
echo [*] Compiling tv_demod.dll (SIMD AVX2 Optimized, Standalone CRT)...
cl /nologo /EHsc /O2 /arch:AVX2 /fp:fast /MT /LD /Fe:"..\tv_demod.dll" tv_demod.cpp /link winmm.lib
if %errorlevel% neq 0 (
    echo [!] Error compiling tv_demod.dll!
    pause
    exit /b %errorlevel%
)

echo [*] Compiling tv_app.exe (Native Windows Desktop App, Standalone CRT)...
cl /nologo /EHsc /O2 /MT /Fe:"..\tv_app.exe" tv_app.cpp /I. /link /LIBPATH:.. tv_demod.lib user32.lib gdi32.lib comctl32.lib shell32.lib advapi32.lib winmm.lib /SUBSYSTEM:WINDOWS
if %errorlevel% neq 0 (
    echo [!] Error compiling tv_app.exe!
    pause
    exit /b %errorlevel%
)

del /q *.obj >nul 2>&1
echo.
echo [+] BUILD SUCCESSFUL!
echo     - TV\tv_demod.dll
echo     - TV\tv_app.exe
pause
