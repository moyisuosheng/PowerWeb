# 在「启动」文件夹里创建一个快捷方式，让 PowerWeb 每次登录时静默启动。
#
# 用法（一般由 enable-autostart.bat 调用）：
#   install-autostart.ps1             使用默认端口 8000
#   install-autostart.ps1 -Port 8123  换端口
#   install-autostart.ps1 -Key 1234   同时在快捷方式里带上访问密钥

param(
    [int]$Port = 8000,
    [string]$Key = ''
)

$ErrorActionPreference = 'Stop'

$dir     = $PSScriptRoot
$exe     = Join-Path $dir 'PowerWeb.exe'
$page    = Join-Path $dir 'index.html'
$startup = [Environment]::GetFolderPath('Startup')
$lnkPath = Join-Path $startup 'PowerWeb.lnk'

if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host ""
    Write-Host "PowerWeb.exe not found next to this script. Aborting." -ForegroundColor Red
    Write-Host "Put PowerWeb.exe, index.html and this script in the same folder." -ForegroundColor DarkGray
    Write-Host ""
    exit 1
}

if (-not (Test-Path -LiteralPath $page)) {
    Write-Host "WARNING: index.html not found next to PowerWeb.exe; the page will 404." -ForegroundColor Yellow
}

# --hidden 让 exe 启动后自己把控制台窗口藏起来，开机时不会闪一个黑框
$argLine = '--hidden --port ' + $Port
if ($Key -ne '') { $argLine += ' --key "' + $Key + '"' }

$shell = New-Object -ComObject WScript.Shell
$sc = $shell.CreateShortcut($lnkPath)
$sc.TargetPath       = $exe              # 直接指向 exe，不再经过 PowerShell
$sc.Arguments        = $argLine
$sc.WorkingDirectory = $dir              # exe 按自身目录找 index.html
$sc.WindowStyle      = 7                 # 最小化启动，配合 --hidden 完全不可见
$sc.Description      = 'PowerWeb - LAN power control service'
$sc.Save()

Write-Host ""
Write-Host "OK. Autostart shortcut created:" -ForegroundColor Green
Write-Host "  $lnkPath"
Write-Host ""
Write-Host "  target : $exe"
Write-Host "  args   : $argLine"
Write-Host ""
Write-Host "PowerWeb will start silently at the next logon."
Write-Host "To start it right now, double-click start-exe.bat."
