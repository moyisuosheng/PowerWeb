# Creates a Startup shortcut so PowerWeb launches hidden at every logon.
$ErrorActionPreference = 'Stop'

$dir     = $PSScriptRoot
$psExe   = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$startup = [Environment]::GetFolderPath('Startup')
$lnkPath = Join-Path $startup 'PowerWeb.lnk'

# ---- port: change here if start-server.bat uses another port ----
$port = 8000

$server = Join-Path $dir 'server.ps1'
if (-not (Test-Path -LiteralPath $server)) {
    Write-Host "server.ps1 not found next to this script. Aborting." -ForegroundColor Red
    exit 1
}

$shell = New-Object -ComObject WScript.Shell
$sc = $shell.CreateShortcut($lnkPath)
$sc.TargetPath       = $psExe
$sc.Arguments        = '-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "' + $server + '" -Port ' + $port
$sc.WorkingDirectory = $dir
$sc.WindowStyle      = 7
$sc.Description      = 'PowerWeb - LAN power control service'
$sc.Save()

Write-Host ""
Write-Host "OK. Autostart shortcut created:" -ForegroundColor Green
Write-Host "  $lnkPath"
Write-Host ""
Write-Host "PowerWeb will start (without a visible window) at the next logon."
Write-Host "To start it right now, double-click start-server.bat."
