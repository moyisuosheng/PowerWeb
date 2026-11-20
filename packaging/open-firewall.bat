@echo off
setlocal
cd /d "%~dp0"

set "PORT=8000"
if not "%~1"=="" set "PORT=%~1"

net session >nul 2>&1
if errorlevel 1 (
    echo Need administrator privileges, requesting elevation ...
    powershell.exe -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%PORT%' -Verb RunAs"
    exit /b
)

echo Adding inbound firewall rule for TCP port %PORT% ...
netsh advfirewall firewall delete rule name="PowerWeb TCP %PORT%" >nul 2>&1
netsh advfirewall firewall add rule name="PowerWeb TCP %PORT%" dir=in action=allow protocol=TCP localport=%PORT% profile=private,domain
if errorlevel 1 (
    echo.
    echo FAILED to add the firewall rule.
) else (
    echo.
    echo OK. Inbound TCP %PORT% is now allowed on private/domain networks.
)
echo.
pause
