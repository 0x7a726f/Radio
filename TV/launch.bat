@echo off
setlocal
cd /d "%~dp0"
title Greek TV and Radio SDR Live Tuner

echo ========================================================
echo    Greek Television and Radio SDR Live Tuner
echo    C++ Demodulator Engine and Native Desktop App
echo ========================================================
echo.

if exist "%~dp0tv_app.exe" (
    echo [*] Launching Native C++ Desktop Application [tv_app.exe]...
    start "" "%~dp0tv_app.exe"
    exit /b
)

if exist "%~dp0tv_desktop_app.py" (
    echo [*] Launching Native Python Desktop App...
    python "%~dp0tv_desktop_app.py"
    pause
    exit /b
)

python "%~dp0run.py"
pause
