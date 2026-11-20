# 在「启动」文件夹里创建一个快捷方式，让 PowerWeb 每次登录时静默启动。
#
# 用法（一般由 enable-autostart.bat 调用）：
#   install-autostart.ps1                   端口和密钥从 start-exe.bat 里读
#   install-autostart.ps1 -Port 8123        指定端口
#   install-autostart.ps1 -Key 1234         指定密钥

param(
    [int]$Port = 0,
    [string]$Key = ''
)

$ErrorActionPreference = 'Stop'

$dir     = $PSScriptRoot
$exe     = Join-Path $dir 'PowerWeb.exe'
$page    = Join-Path $dir 'index.html'
$batFile = Join-Path $dir 'start-exe.bat'
$startup = [Environment]::GetFolderPath('Startup')
$lnkPath = Join-Path $startup 'PowerWeb.lnk'

# 从 start-exe.bat 里取 set "NAME=value"，让自启和手动启动的配置保持一致
function Get-BatVar([string]$name) {
    if (-not (Test-Path -LiteralPath $batFile)) { return '' }
    $m = Select-String -Path $batFile -Pattern ('^\s*set\s+"' + $name + '=([^"]*)"') |
         Select-Object -First 1
    if ($m) { return $m.Matches[0].Groups[1].Value }
    return ''
}

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

# 参数没给就从 start-exe.bat 继承，再没有就用默认值
$portValue = $Port
if ($portValue -le 0) {
    $fromBat = Get-BatVar 'PORT'
    if ($fromBat -match '^\d+$') { $portValue = [int]$fromBat } else { $portValue = 8000 }
}

$keyValue = $Key
if ($keyValue -eq '') { $keyValue = Get-BatVar 'ACCESS_KEY' }

# --hidden 让 exe 启动后自己把控制台窗口藏起来，开机时不会闪一个黑框
$argLine = '--hidden --port ' + $portValue
if ($keyValue -ne '') { $argLine += ' --key "' + $keyValue + '"' }

$shell = New-Object -ComObject WScript.Shell
$sc = $shell.CreateShortcut($lnkPath)
$sc.TargetPath       = $exe              # 直接指向 exe，不再经过 PowerShell
$sc.Arguments        = $argLine
$sc.WorkingDirectory = $dir              # exe 按自身目录找 index.html
$sc.WindowStyle      = 7                 # 最小化启动，配合 --hidden 完全不可见
$sc.Description      = 'PowerWeb - LAN power control service'
$sc.Save()

$keyShown = '未设置'
if ($keyValue -ne '') { $keyShown = '已设置（' + $keyValue.Length + ' 位）' }

Write-Host ""
Write-Host "已创建开机自启快捷方式：" -ForegroundColor Green
Write-Host "  $lnkPath"
Write-Host ""
Write-Host "  目标程序 : $exe"
Write-Host "  启动参数 : --hidden --port $portValue"
Write-Host "  访问密钥 : $keyShown"
Write-Host ""
Write-Host "下次登录时 PowerWeb 会静默启动（不会有窗口）。"
Write-Host "想现在就启动，双击 start-exe.bat。"
