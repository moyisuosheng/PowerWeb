# PowerWeb 冒烟测试（Windows）
#
# 在 CI 里跑，也可以在本地跑：
#   powershell -ExecutionPolicy Bypass -File scripts\smoke.ps1 -Dir build\dist
#
# 全程使用 --dry-run，不会真的关机。

param(
    [string]$Dir = "build/dist",
    [int]$Port = 18000
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # 别让进度条刷乱 CI 日志

$exe = Join-Path $Dir "PowerWeb.exe"
if (-not (Test-Path $exe)) { throw "找不到 $exe，请先 make dist" }

$script:passed = 0
function Check($cond, $name) {
    if ($cond) {
        $script:passed++
        Write-Host ("  [ok]   " + $name)
    } else {
        throw ("  [FAIL] " + $name)
    }
}

function Wait-Server($BaseUrl, $Seconds = 15) {
    for ($i = 0; $i -lt ($Seconds * 4); $i++) {
        Start-Sleep -Milliseconds 250
        try {
            $r = Invoke-RestMethod -Uri "$BaseUrl/api/status" -TimeoutSec 2
            if ($r.ok) { return $r }
        } catch { }
    }
    throw "服务在 $Seconds 秒内没有响应 $BaseUrl"
}

function Stop-Server($proc) {
    if ($proc -and -not $proc.HasExited) {
        try { $proc.Kill(); $proc.WaitForExit(5000) | Out-Null } catch { }
    }
    Start-Sleep -Milliseconds 300
}

Write-Host ""
Write-Host "PowerWeb 冒烟测试：$exe"
Write-Host ""

# ---- 1. 版本号与帮助 ----
$ver = & $exe --version
Check ($LASTEXITCODE -eq 0) "--version 退出码为 0"
Check ($ver -match '^PowerWeb \d+\.\d+') "--version 输出：$ver"

$helpOut = & $exe --help
Check ($LASTEXITCODE -eq 0) "--help 退出码为 0"
Check (($helpOut -join "`n") -match '--port') "--help 包含参数说明"

# ---- 2. 无密钥模式 ----
$base = "http://127.0.0.1:$Port"
$p = Start-Process -FilePath $exe -ArgumentList "--port", "$Port", "--dry-run" -PassThru -WindowStyle Hidden
try {
    $st = Wait-Server $base
    Check ($st.app -eq "PowerWeb") "/api/status 返回 app=PowerWeb"
    Check ($st.dryRun -eq $true) "dryRun 已生效"
    Check ($null -eq $st.scheduled) "初始没有预约"

    $html = Invoke-WebRequest -Uri "$base/" -UseBasicParsing -TimeoutSec 5
    Check ($html.StatusCode -eq 200) "首页返回 200"
    Check ($html.Content -match 'PowerWeb') "首页内容包含 PowerWeb"

    $body = '{"action":"shutdown","seconds":600}'
    $r = Invoke-RestMethod -Method Post -Uri "$base/api/schedule" -ContentType "application/json" -Body $body -TimeoutSec 5
    Check ($r.ok -eq $true) "预约关机 10 分钟成功"
    Check ($r.action -eq "shutdown") "返回 action=shutdown"

    $st = Invoke-RestMethod -Uri "$base/api/status" -TimeoutSec 5
    Check ($st.scheduled.action -eq "shutdown") "status 里出现预约"
    Check ($st.scheduled.remaining -gt 500) "剩余时间约 600 秒（实际 $($st.scheduled.remaining)）"

    $r = Invoke-RestMethod -Method Post -Uri "$base/api/cancel" -ContentType "application/json" -Body '{}' -TimeoutSec 5
    Check ($r.ok -eq $true) "取消预约成功"

    $st = Invoke-RestMethod -Uri "$base/api/status" -TimeoutSec 5
    Check ($null -eq $st.scheduled) "取消后没有预约"

    # 非法参数应当 400
    $code = 0
    try {
        Invoke-RestMethod -Method Post -Uri "$base/api/schedule" -ContentType "application/json" -Body '{"action":"boom","seconds":10}' -TimeoutSec 5 | Out-Null
    } catch { $code = [int]$_.Exception.Response.StatusCode }
    Check ($code -eq 400) "非法 action 返回 400（实际 $code）"

    # 锁屏 / 睡眠在 dry-run 下只打印命令
    $r = Invoke-RestMethod -Method Post -Uri "$base/api/power" -ContentType "application/json" -Body '{"action":"lock"}' -TimeoutSec 5
    Check ($r.ok -eq $true) "lock 指令被接受"
} finally {
    Stop-Server $p
}

# ---- 3. 密钥模式 ----
$base2 = "http://127.0.0.1:$($Port + 1)"
$p2 = Start-Process -FilePath $exe -ArgumentList "--port", "$($Port + 1)", "--key", "s3cret", "--dry-run" -PassThru -WindowStyle Hidden
try {
    Start-Sleep -Seconds 1
    $code = 0
    try {
        Invoke-RestMethod -Uri "$base2/api/status" -TimeoutSec 5 | Out-Null
    } catch { $code = [int]$_.Exception.Response.StatusCode }
    Check ($code -eq 401) "没有密钥时返回 401（实际 $code）"

    $r = Invoke-RestMethod -Uri "$base2/api/status" -Headers @{ "X-Auth-Key" = "s3cret" } -TimeoutSec 5
    Check ($r.ok -eq $true -and $r.keyRequired -eq $true) "带正确密钥可访问，keyRequired=true"
} finally {
    Stop-Server $p2
}

Write-Host ""
Write-Host "全部通过：$script:passed 项"
Write-Host ""
