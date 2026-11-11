@echo off
title Hydrogen Line SDR Live Receiver & CS16 Recorder
cd /d "%~dp0"

echo ======================================================================
echo   HYDROGEN LINE SDR RECEIVER & CS16 RECORDER
echo ======================================================================
echo   Starting desktop application...
echo ----------------------------------------------------------------------

if exist "C:\Code\Python\python.exe" (
    start "" "C:\Code\Python\python.exe" "app.py"
) else (
    start "" python "app.py"
)

exit
