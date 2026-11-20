@echo off
setlocal
cd /d "%~dp0"

set "LNK=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\PowerWeb.lnk"

if exist "%LNK%" (
    del /f /q "%LNK%"
    echo Autostart disabled. Shortcut removed:
    echo   %LNK%
) else (
    echo Autostart was not enabled, nothing to remove.
)

echo.
pause
