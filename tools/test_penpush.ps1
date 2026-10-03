# tools\pen-push.ps1 的集成测试。
#
# 不需要真实词典笔：在本机跑一个 host 版 sidecar 当"设备"，验证电脑端工具的
# 完整链路（生成 JSON → HTTP 送达 → 设备端落盘 → 文件监听生效）与全部分支。
#
# 用法（PowerShell 默认策略 Restricted，需要显式 Bypass）：
#   cd go_server/main; ; $env:CGO_ENABLED=0; go build -o ..\..\server_host.exe .
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_penpush.ps1
#
# ⚠ 含中文，必须以 UTF-8 **带 BOM** 保存（tools\verify.py 的 Q 项会检查）。
# 注意：adb 通路只测「安全护栏」（用不存在的序列号），不会碰你真实连接的设备。

[CmdletBinding()]
param(
    [string]$Exe,
    [int]$Port = 18013
)

$ErrorActionPreference = "Continue"
$root = Split-Path $PSScriptRoot -Parent
$tool = Join-Path $PSScriptRoot "pen-push.ps1"
$tmp = Join-Path $root ".testtmp"

if (-not $Exe) {
    foreach ($n in @("server_host.exe", "server_host", "server_test.exe", "server_test")) {
        $p = Join-Path $root $n
        if (Test-Path $p) { $Exe = $p; break }
    }
}
if (-not $Exe -or -not (Test-Path $Exe)) {
    Write-Host "找不到 host 版 server。先执行：" -ForegroundColor Red
    Write-Host "  cd go_server/main; `$env:CGO_ENABLED=0; go build -o ..\..\server_host.exe ."
    exit 2
}

$script:ok = $true
function Check($label, $cond, $extra = "") {
    $text = if ($extra -is [array]) { $extra -join ' ' } else { [string]$extra }
    $mark = if ($cond) { "  OK   " } else { "  FAIL " }
    $line = $mark + $label + $(if ($text) { "  $text" } else { "" })
    if ($cond) { Write-Host $line -ForegroundColor Green } else { Write-Host $line -ForegroundColor Red }
    if (-not $cond) { $script:ok = $false }
}
function Run-Tool([string[]]$toolArgs) {
    $ps = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"
    if (-not (Test-Path $ps)) { $ps = "pwsh" }
    $all = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $tool) + $toolArgs
    $out = & $ps @all 2>&1
    return @{ Exit = $LASTEXITCODE; Text = ($out | Out-String) }
}

New-Item -ItemType Directory -Path $tmp -Force | Out-Null
$devCookie = Join-Path $tmp "device-cookies.json"
$log = Join-Path $tmp "dev-server.log"
Remove-Item $devCookie, $log -Force -ErrorAction SilentlyContinue

$env:PORT = "$Port"
$env:WEIBO_BIND = "127.0.0.1"
$env:WEIBO_COOKIE_FILE = $devCookie
$env:WEIBO_PLUGIN_DIR = $tmp
Remove-Item Env:DEBUG -ErrorAction SilentlyContinue

$proc = $null
# -WindowStyle 是 Windows 专有参数，Linux 上会报错，所以按平台拼参数
$spArgs = @{
    FilePath               = $Exe
    PassThru               = $true
    RedirectStandardOutput = $log
    RedirectStandardError  = "$log.err"
}
if ($IsWindows -or $env:OS -eq 'Windows_NT') { $spArgs['WindowStyle'] = 'Hidden' }
try {
    $proc = Start-Process @spArgs
} catch {
    Write-Host "启动 $Exe 失败：$($_.Exception.Message)" -ForegroundColor Red
    exit 2
}
$ready = $false
for ($i = 0; $i -lt 60; $i++) {
    try {
        $r = Invoke-WebRequest "http://127.0.0.1:$Port/server/ping" -UseBasicParsing -TimeoutSec 3
        if ($r.StatusCode -eq 200) { $ready = $true; break }
    } catch { }
    Start-Sleep -Milliseconds 250
}
Check "假设备（host sidecar）已就绪" $ready $Exe

try {
    Write-Host "`n== 1. -Cookie + http 通路（真实 POST 到 sidecar）=="
    $localJson = Join-Path $tmp "pushed.json"
    $r = Run-Tool @("-Cookie", "SUB=FAKEPUSH1234567890; SUBP=FAKEPUSHSUBP0987654321",
                    "-Method", "http", "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", $localJson)
    Check "退出码 0" ($r.Exit -eq 0)
    Check "报告 saved=True" ($r.Text -match 'saved=True')
    Check "报告 verified=False（假 Cookie）" ($r.Text -match 'verified=False')
    Check "本地 JSON 已生成" (Test-Path $localJson)
    Check "设备端 cookies.json 已写入" (Test-Path $devCookie)

    Write-Host "`n== 2. 生成的 JSON 结构正确且无 BOM =="
    $bytes = [System.IO.File]::ReadAllBytes($localJson)
    Check "无 BOM（Go 的 encoding/json 遇 BOM 会报错）" `
        (-not ($bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF))
    $j = [System.IO.File]::ReadAllText($localJson, [System.Text.Encoding]::UTF8) | ConvertFrom-Json
    Check "有 cookies 数组" ($null -ne $j.cookies)
    Check "两条 Cookie" ($j.cookies.Count -eq 2) "count=$($j.cookies.Count)"
    Check "SUB 排在最前" ($j.cookies[0].name -eq 'SUB')
    Check "带 domain/path" ($j.cookies[0].domain -eq '.weibo.com' -and $j.cookies[0].path -eq '/')
    Check "updated_at 已写" ($j.updated_at -gt 0)

    Write-Host "`n== 3. 直接改设备文件 → 文件监听自动生效（adb push 路径）=="
    $before = (Invoke-WebRequest "http://127.0.0.1:$Port/server/state" -UseBasicParsing).Content | ConvertFrom-Json
    $pushed = '{"cookies":[{"name":"SUB","value":"FAKE_ADB_PUSHED_9999","domain":".weibo.com"}],"updated_at":0}'
    [System.IO.File]::WriteAllText($devCookie, $pushed, (New-Object System.Text.UTF8Encoding($false)))
    Start-Sleep -Seconds 4
    $after = (Invoke-WebRequest "http://127.0.0.1:$Port/server/state" -UseBasicParsing).Content | ConvertFrom-Json
    Check "cookie_rev 变化" ($before.data.cookie_rev -ne $after.data.cookie_rev)
    # 日志是 UTF-8；PS 5.1 的 Get-Content 默认按 ANSI 读会乱码，必须显式指定
    $dlog = Get-Content $log -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
    Check "日志出现『检测到 cookies.json 被外部修改』" ($dlog -match '检测到 cookies.json 被外部修改')

    Write-Host "`n== 4. 裸 SUB 值也能用 =="
    $r = Run-Tool @("-Cookie", "FAKEBAREVALUE1234567890", "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", (Join-Path $tmp "bare.json"))
    Check "识别为裸 SUB 并成功" ($r.Exit -eq 0 -and $r.Text -match '按 SUB 处理')

    Write-Host "`n== 5. 校验分支 =="
    $r = Run-Tool @("-Cookie", "SUBP=onlysubp1234567890", "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", (Join-Path $tmp "x.json"))
    Check "缺 SUB → 报错" ($r.Exit -ne 0 -and $r.Text -match '缺少 SUB')

    $r = Run-Tool @("-Cookie", "SUB=short", "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", (Join-Path $tmp "x.json"))
    Check "SUB 太短 → 报错" ($r.Exit -ne 0 -and $r.Text -match '太短')

    $r = Run-Tool @("-Cookie", "SUB=把这里替换成你浏览器里的值; SUBP=aaaaaa", "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", (Join-Path $tmp "x.json"))
    Check "占位文案 → 报错" ($r.Exit -ne 0 -and $r.Text -match '占位')

    $r = Run-Tool @("-Cookie", "SUB=FAKEVALID1234567890", "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "19999", "-Out", (Join-Path $tmp "x.json"))
    Check "连不上 → 报错并给排查方向" ($r.Exit -ne 0 -and $r.Text -match '同一局域网|WEIBO_BIND')

    Write-Host "`n== 6. adb 通路的安全护栏（不存在的序列号，不碰真实设备）=="
    $r = Run-Tool @("-Cookie", "SUB=FAKEADB1234567890", "-Serial", "NOSUCHSERIAL",
                    "-Method", "adb", "-DryRun", "-Out", (Join-Path $tmp "adb.json"))
    Check "不认识 adb 设备 → 拒绝推送" ($r.Exit -ne 0 -and $r.Text -match '不是词典笔|PenMods')

    Write-Host "`n== 7. -CookieFile 通路 =="
    $cf = Join-Path $tmp "cookie.txt"
    [System.IO.File]::WriteAllText($cf, "SUB=FAKEFILE1234567890; SUBP=FAKEFILESUBP1234567890",
        (New-Object System.Text.UTF8Encoding($false)))
    $r = Run-Tool @("-CookieFile", $cf, "-Method", "http",
                    "-Pen", "127.0.0.1", "-Port", "$Port", "-Out", (Join-Path $tmp "file.json"))
    Check "从文件读并推送成功" ($r.Exit -eq 0 -and $r.Text -match '解析出 2 条')
}
finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host $(if ($script:ok) { "结果: 全部通过" } else { "结果: 有失败项" }) `
    -ForegroundColor $(if ($script:ok) { "Green" } else { "Red" })
exit $(if ($script:ok) { 0 } else { 1 })
