@echo off
setlocal
chcp 65001 >nul
cd /d "%~dp0"
title PowerWeb - native exe

rem ---- change PORT here if you need another port ----
set "PORT=8000"
if not "%~1"=="" set "PORT=%~1"

rem ---- optional access key: uncomment and set a value to protect the API ----
set "ACCESS_KEY="
rem set "ACCESS_KEY=1234"

if not exist "%~dp0PowerWeb.exe" (
    echo PowerWeb.exe not found in this folder.
    echo Put PowerWeb.exe and index.html together.
    pause
    exit /b 1
)

if not exist "%~dp0index.html" (
    echo WARNING: index.html not found. The exe will return 404 for the page.
    echo.
)

echo Starting PowerWeb.exe on port %PORT% ...
echo Keep this window open. Press Ctrl+C to stop the service.
echo.

if "%ACCESS_KEY%"=="" (
    "%~dp0PowerWeb.exe" --port %PORT%
) else (
    "%~dp0PowerWeb.exe" --port %PORT% --key %ACCESS_KEY%
)

echo.
echo Service stopped. Press any key to close this window.
pause >nul
