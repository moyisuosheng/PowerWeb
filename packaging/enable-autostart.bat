@echo off
setlocal
cd /d "%~dp0"

rem Optional: port number. Default 8000, keep it the same as start-exe.bat
set "PORT=8000"
if not "%~1"=="" set "PORT=%~1"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-autostart.ps1" -Port %PORT%
echo.
pause
