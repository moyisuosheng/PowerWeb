@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"

rem Create a Startup shortcut so PowerWeb starts silently at every logon.
rem Port and access key are inherited from start-exe.bat; override with args:
rem   enable-autostart.bat -Port 8123 -Key 1234

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install-autostart.ps1" %*
echo.
pause
