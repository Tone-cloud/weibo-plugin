# 在电脑上准备好微博 Cookie / JSON，并自动导入词典笔。
#
# 为什么需要它：词典笔只有 320x170 触摸屏，在笔上粘贴 SUB/SUBP 又慢又容易错。
# 这个脚本让你在电脑上完成「填 Cookie → 生成 cookies.json → 送到笔上」，
# 笔那边由 sidecar 自动加载，**不需要在笔上做任何操作**。
#
# 三种送达方式（-Method auto 会按顺序自动挑）：
#   1. usb   如果 /userdisk 以 U 盘/盘符方式挂载，直接复制文件；
#   2. adb   推荐。PenManager 用的就是 adb，把文件 push 进插件目录，
#            sidecar 监听文件变化，2 秒内自动生效；
#   3. http  笔上 sidecar 以 WEIBO_BIND=0.0.0.0 启动时，
#            POST /config/import（同样是落盘，效果一致）。
#
# ⚠ 本文件含中文，必须以 UTF-8 **带 BOM** 保存，否则 Windows PowerShell 5.1
#   会按 GBK 解析并报语法错误。
#
# 用法（PowerShell 默认策略 Restricted，需要显式 Bypass）：
#   # 1) 最省事：从浏览器复制 Cookie 头，粘到命令行
#   powershell -NoProfile -ExecutionPolicy Bypass -File pen-push.ps1 `
#       -Cookie "SUB=xxxxx; SUBP=yyyyy"
#
#   # 2) 从文件读（cookie.txt 里放一行 Cookie 头）
#   ... -File pen-push.ps1 -CookieFile .\cookie.txt
#
#   # 3) 已经有 cookies.json，直接推送
#   ... -File pen-push.ps1 -Json .\cookies.json
#
#   # 4) 走网络（笔上先以 WEIBO_BIND=0.0.0.0 启动 sidecar）
#   ... -File pen-push.ps1 -Cookie "SUB=..." -Method http -Pen 192.168.1.23
#
#   # 5) 只看会做什么，不推送到设备（本地 cookies.json 仍会生成，方便检查）
#   ... -File pen-push.ps1 -Cookie "SUB=..." -DryRun
#
#   # 6) 把笔上现有的 cookies.json 拉回来编辑
#   ... -File pen-push.ps1 -Pull .\cookies-from-pen.json

[CmdletBinding(DefaultParameterSetName = "CookieText")]
param(
    # Cookie 头（"SUB=...; SUBP=..."）或裸 SUB 值
    [Parameter(ParameterSetName = "CookieText", Mandatory = $true)]
    [string]$Cookie,

    # 从文本文件读 Cookie 头
    [Parameter(ParameterSetName = "CookieFile", Mandatory = $true)]
    [string]$CookieFile,

    # 已有的 cookies.json
    [Parameter(ParameterSetName = "Json", Mandatory = $true)]
    [string]$Json,

    # 把笔上的 cookies.json 拉回到本地（可与其它参数组合，单独用也行）
    [Parameter(ParameterSetName = "Pull", Mandatory = $true)]
    [string]$Pull,

    # 本地生成的 cookies.json 落在哪里
    [string]$Out = (Join-Path $PSScriptRoot "cookies.json"),

    # auto / usb / adb / http
    [ValidateSet("auto", "usb", "adb", "http")]
    [string]$Method = "auto",

    # http 方式用：词典笔的局域网 IP
    [string]$Pen,

    [int]$Port = 8010,

    # 设备上插件目录名（bili / netease 等其它插件也能用这个脚本）
    [string]$PluginName = "weibo_plugin",

    # 指定 adb 序列号（接了多台设备时用）
    [string]$Serial,

    # adb.exe 路径（默认自动找）
    [string]$Adb,

    # 设备上没有 PenMods 目录时也强行推送
    [switch]$Force,

    # 只打印计划，不实际写文件/推送
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

# 设备上的插件目录（PenMods 约定）
$DevicePluginDir = "/userdisk/PenMods/plugins/$PluginName"
$DeviceCookiePath = "$DevicePluginDir/cookies.json"

# 需要保留的 Cookie 名（顺序按重要性，便于阅读生成结果）
$KnownCookieOrder = @(
    'SUB', 'SUBP', 'SSOLoginState', 'ALF', 'XSRF-TOKEN', 'WBPSESS',
    'SUHB', 'SCF', 'ULV', 'SRT', 'tid', 'login_sid_t', 'cross_origin_proto'
)

function Write-Step($text) { Write-Host "==> $text" -ForegroundColor Cyan }
function Write-Ok($text) { Write-Host "    $text" -ForegroundColor Green }
function Write-Warn2($text) { Write-Host "    $text" -ForegroundColor Yellow }
function Write-Info($text) { Write-Host "    $text" }

# ---------------------------------------------------------------- Cookie 解析

# Parse-CookieHeader 把 "SUB=a; SUBP=b" 解析成有序的 @{name,value}
function Parse-CookieHeader([string]$text) {
    $pairs = New-Object System.Collections.ArrayList
    foreach ($chunk in ($text -split ';')) {
        $part = $chunk.Trim()
        if ($part -eq '') { continue }
        $eq = $part.IndexOf('=')
        if ($eq -le 0) { continue }
        $name = $part.Substring(0, $eq).Trim()
        $value = $part.Substring($eq + 1).Trim()
        if ($name -eq '' -or $value -eq '') { continue }
        [void]$pairs.Add([pscustomobject]@{ name = $name; value = $value })
    }
    return $pairs
}

# Build-CookieList 汇总所有输入来源，返回 @{name,value} 数组
function Build-CookieList() {
    if ($PSCmdlet.ParameterSetName -eq 'Json') {
        if (-not (Test-Path $Json)) { throw "找不到 $Json" }
        Write-Step "从 $Json 读取"
        $raw = [System.IO.File]::ReadAllText((Resolve-Path $Json).Path, [System.Text.Encoding]::UTF8)
        $raw = $raw.TrimStart([char]0xFEFF)   # 容忍 BOM
        $obj = $raw | ConvertFrom-Json
        $list = New-Object System.Collections.ArrayList
        $items = if ($obj.PSObject.Properties.Name -contains 'cookies') { $obj.cookies } else { $obj }
        foreach ($c in $items) {
            if ($c.name -and $c.value) {
                [void]$list.Add([pscustomobject]@{ name = [string]$c.name; value = [string]$c.value })
            }
        }
        Write-Ok "读到 $($list.Count) 条 Cookie"
        return $list
    }

    $text = if ($PSCmdlet.ParameterSetName -eq 'CookieFile') {
        if (-not (Test-Path $CookieFile)) { throw "找不到 $CookieFile" }
        Write-Step "从 $CookieFile 读取"
        ([System.IO.File]::ReadAllText((Resolve-Path $CookieFile).Path, [System.Text.Encoding]::UTF8)).Trim()
    } else {
        Write-Step "使用命令行传入的 Cookie"
        $Cookie.Trim()
    }

    if ($text -eq '') { throw "Cookie 内容为空" }

    # 只填了 SUB 的裸值
    if ($text -notmatch '=') {
        Write-Info "只检测到裸值，按 SUB 处理"
        $text = "SUB=$text"
    }

    $pairs = Parse-CookieHeader $text
    if ($pairs.Count -eq 0) { throw "没能从内容里解析出任何 Cookie，请检查格式（应为 SUB=...; SUBP=...）" }
    Write-Ok "解析出 $($pairs.Count) 条 Cookie：$(($pairs | ForEach-Object { $_.name }) -join ', ')"
    return $pairs
}

# 校验：SUB 必须有，且不能还是占位文案
function Assert-CookiesUsable($list) {
    $sub = $list | Where-Object { $_.name -eq 'SUB' }
    if (-not $sub) {
        $hasSubp = $list | Where-Object { $_.name -eq 'SUBP' }
        if ($hasSubp) {
            throw "只找到 SUBP，缺少 SUB —— 微博登录必须带 SUB"
        }
        throw "缺少 SUB Cookie —— 请在浏览器登录 weibo.com 后从 Cookie 里复制 SUB 与 SUBP"
    }
    $value = [string]$sub.value
    if ($value.Length -lt 10) {
        throw "SUB 的值太短（$($value.Length) 字符），看起来不是真的 Cookie"
    }
    if ($value -match '替换|示例|example|xxxx|your') {
        throw "SUB 的值看起来还是 cookies.example.json 里的占位文案，请替换成真实值"
    }
    $names = $list | ForEach-Object { $_.name }
    if ($names -notcontains 'SUBP') {
        Write-Warn2 "没有 SUBP —— 部分接口可能失败（建议一起复制）"
    }
    return $true
}

# ---------------------------------------------------------------- 写本地 JSON

# Write-CookieJson 写 cookies.json。
# 用 UTF8Encoding($false) 明确**不写 BOM**：Go 的 encoding/json 遇到 BOM 会报
# invalid character 'ï'。Go 侧虽然也做了剥离，这里仍然不写更干净。
function Write-CookieJson($list, [string]$path) {
    $ordered = New-Object System.Collections.ArrayList
    foreach ($name in $KnownCookieOrder) {
        foreach ($c in $list) {
            if ($c.name -eq $name) { [void]$ordered.Add($c) }
        }
    }
    foreach ($c in $list) {
        if ($KnownCookieOrder -notcontains $c.name) { [void]$ordered.Add($c) }
    }

    $payload = [ordered]@{
        cookies    = @($ordered | ForEach-Object {
                [ordered]@{ name = $_.name; value = $_.value; domain = '.weibo.com'; path = '/' }
            })
        updated_at = [int][double]::Parse((Get-Date -UFormat %s))
    }
    $jsonText = $payload | ConvertTo-Json -Depth 6

    $dir = Split-Path $path -Parent
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($path, $jsonText, $utf8NoBom)
    Write-Ok "已写出 $path（$($ordered.Count) 条，无 BOM）"
    return $path
}

# ---------------------------------------------------------------- adb

function Find-Adb() {
    $cands = @()
    if ($Adb) { $cands += $Adb }
    $cands += @(
        "$env:USERPROFILE\Desktop\platform-tools\adb.exe",
        "$env:LOCALAPPDATA\PenManager\adb.exe",
        "$env:ProgramFiles\platform-tools\adb.exe"
    )
    foreach ($c in $cands) { if ($c -and (Test-Path $c)) { return (Resolve-Path $c).Path } }
    $cmd = Get-Command adb -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

# Get-DeviceCandidates 列出 adb 里处于 device 状态的序列号
function Get-DeviceCandidates([string]$adbPath) {
    $out = & $adbPath devices 2>&1
    $serials = @()
    foreach ($line in $out) {
        if ($line -match '^\s*(\S+)\s+device\b') { $serials += $Matches[1] }
    }
    return $serials
}

# Test-IsDictPen 判断设备上有没有 PenMods（用来把手机/模拟器排除掉）
function Test-IsDictPen([string]$adbPath, [string]$serial) {
    $probe = & $adbPath -s $serial shell "ls /userdisk/PenMods/plugins 2>/dev/null" 2>&1
    if ($LASTEXITCODE -ne 0) { return $false }
    return ($probe -join "`n") -match $PluginName -or ($probe -join "`n") -match '\w'
}

function Invoke-AdbPush([string]$path) {
    $adbPath = Find-Adb
    if (-not $adbPath) {
        throw "找不到 adb.exe。装了 PenManager 的话通常在 %LOCALAPPDATA%\PenManager\adb.exe；也可以用 -Adb 指定路径"
    }
    Write-Step "adb: $adbPath"

    $candidates = Get-DeviceCandidates $adbPath
    if (-not $candidates -or $candidates.Count -eq 0) {
        throw "adb 没看到设备。请确认词典笔已用 USB 连接、并且 PenManager 能识别到它"
    }
    Write-Info "adb 设备: $($candidates -join ', ')"

    $serial = $Serial
    if (-not $serial) {
        if ($candidates.Count -eq 1) {
            $serial = $candidates[0]
        } else {
            # 多台设备：挑有 PenMods 的那台（手机会被排除）
            foreach ($s in $candidates) {
                if (Test-IsDictPen $adbPath $s) { $serial = $s; break }
            }
            if (-not $serial) {
                throw "adb 上有 $($candidates.Count) 台设备，但没找到带 PenMods 的词典笔。用 -Serial 指定其中一个：$($candidates -join ', ')"
            }
        }
    }
    Write-Ok "使用设备: $serial"

    if (-not (Test-IsDictPen $adbPath $serial)) {
        if (-not $Force) {
            throw "设备 $serial 上没有 /userdisk/PenMods（看起来不是词典笔）。确认无误可加 -Force"
        }
        Write-Warn2 "设备上没有 PenMods，按 -Force 继续"
    }

    if ($DryRun) {
        Write-Info "[DryRun] $adbPath -s $serial shell mkdir -p $DevicePluginDir"
        Write-Info "[DryRun] $adbPath -s $serial push `"$path`" $DeviceCookiePath"
        return @{ Method = 'adb'; Serial = $serial; Path = $DeviceCookiePath }
    }

    Write-Step "确保目录存在"
    & $adbPath -s $serial shell "mkdir -p $DevicePluginDir" | Out-Null
    & $adbPath -s $serial shell "chmod 755 $DevicePluginDir" | Out-Null

    Write-Step "推送到 $DeviceCookiePath"
    $push = & $adbPath -s $serial push "$path" $DeviceCookiePath 2>&1
    $push | ForEach-Object { Write-Info $_ }
    if ($LASTEXITCODE -ne 0) { throw "adb push 失败（exit $LASTEXITCODE）" }

    # 644：sidecar 只需要读；避免权限过宽
    & $adbPath -s $serial shell "chmod 644 $DeviceCookiePath" | Out-Null

    Write-Step "设备端确认"
    $remote = & $adbPath -s $serial shell "wc -c < $DeviceCookiePath; ls -l $DeviceCookiePath" 2>&1
    $remote | ForEach-Object { Write-Info $_ }

    return @{ Method = 'adb'; Serial = $serial; Path = $DeviceCookiePath }
}

function Invoke-AdbPull([string]$target) {
    $adbPath = Find-Adb
    if (-not $adbPath) { throw "找不到 adb.exe，无法拉取" }
    $serial = $Serial
    if (-not $serial) {
        $candidates = Get-DeviceCandidates $adbPath
        foreach ($s in $candidates) { if (Test-IsDictPen $adbPath $s) { $serial = $s; break } }
        if (-not $serial -and $candidates.Count -eq 1) { $serial = $candidates[0] }
        if (-not $serial) { throw "没能确定设备序列号，请用 -Serial 指定" }
    }
    Write-Step "从 $serial 拉取 $DeviceCookiePath"
    if ($DryRun) {
        Write-Info "[DryRun] $adbPath -s $serial pull $DeviceCookiePath `"$target`""
        return
    }
    & $adbPath -s $serial pull $DeviceCookiePath "$target" 2>&1 | ForEach-Object { Write-Info $_ }
    if (-not (Test-Path $target)) { throw "拉取失败：设备上可能还没有 $DeviceCookiePath" }
    Write-Ok "已保存到 $target"
}

# ---------------------------------------------------------------- USB 盘符

# Find-MountedPluginDir 找以盘符方式挂载的插件目录
function Find-MountedPluginDir() {
    foreach ($drive in (Get-PSDrive -PSProvider FileSystem)) {
        $root = $drive.Root
        foreach ($cand in @(
                (Join-Path $root "PenMods\plugins\$PluginName"),
                (Join-Path $root "userdisk\PenMods\plugins\$PluginName")
            )) {
            if (Test-Path $cand) { return (Resolve-Path $cand).Path }
        }
    }
    return $null
}

function Invoke-UsbCopy([string]$path) {
    $dir = Find-MountedPluginDir
    if (-not $dir) { throw "没有以盘符方式挂载的词典笔目录" }
    $target = Join-Path $dir "cookies.json"
    Write-Step "USB 盘符复制 → $target"
    if ($DryRun) { Write-Info "[DryRun] copy `"$path`" `"$target`""; return @{ Method = 'usb'; Path = $target } }
    Copy-Item -LiteralPath $path -Destination $target -Force
    Write-Ok "已复制"
    return @{ Method = 'usb'; Path = $target }
}

# ---------------------------------------------------------------- HTTP

function Invoke-HttpImport([string]$path) {
    if (-not $Pen) { throw "http 方式需要 -Pen <词典笔IP>；也可以先在笔上以 WEIBO_BIND=0.0.0.0 启动 sidecar" }
    $url = "http://$Pen`:$Port/config/import"
    Write-Step "POST $url"
    $body = [System.IO.File]::ReadAllBytes($path)
    if ($DryRun) { Write-Info "[DryRun] POST $url（$($body.Length) 字节）"; return @{ Method = 'http'; Path = $url } }
    try {
        $resp = Invoke-WebRequest -Uri $url -Method POST -Body $body `
            -ContentType 'application/json; charset=utf-8' -UseBasicParsing -TimeoutSec 20
    } catch {
        throw "请求失败：$($_.Exception.Message)`n  （确认笔和电脑在同一局域网，且 sidecar 以 WEIBO_BIND=0.0.0.0 启动）"
    }
    $json = $resp.Content | ConvertFrom-Json
    if ($json.code -ne 0) { throw "设备返回错误：$($json.message)" }
    $d = $json.data
    Write-Ok "设备已接受：saved=$($d.saved) verified=$($d.verified)"
    if ($d.verified) {
        Write-Ok "已登录：$($d.screen_name)（uid=$($d.uid)）"
    } else {
        Write-Warn2 $d.message
    }
    return @{ Method = 'http'; Path = $url; Data = $d }
}

# ---------------------------------------------------------------- 主流程

Write-Host ""
Write-Host "笔里微博 · 电脑端导入 Cookie" -ForegroundColor White
Write-Host "  设备目录: $DevicePluginDir"
Write-Host "  本地 JSON: $Out"
Write-Host "  方式: $Method$(if ($DryRun) { '  [DryRun]' })"
Write-Host ""

# 只拉取的情况
if ($PSCmdlet.ParameterSetName -eq 'Pull') {
    Invoke-AdbPull $Pull
    Write-Host ""
    Write-Host "完成。改完这个文件再用 -Json 推回去即可。" -ForegroundColor Green
    exit 0
}

# 1) 收集 + 校验
$cookies = Build-CookieList
Assert-CookiesUsable $cookies | Out-Null

# 2) 写本地 JSON
$built = Write-CookieJson $cookies $Out

# 3) 送达
$result = $null
if ($Method -eq 'usb') {
    $result = Invoke-UsbCopy $built
} elseif ($Method -eq 'adb') {
    $result = Invoke-AdbPush $built
} elseif ($Method -eq 'http') {
    $result = Invoke-HttpImport $built
} else {
    # auto：盘符 → adb → http
    try { $result = Invoke-UsbCopy $built }
    catch {
        try { $result = Invoke-AdbPush $built }
        catch {
            if ($Pen) { $result = Invoke-HttpImport $built }
            else { throw "自动导入失败：$($_.Exception.Message)" }
        }
    }
}

Write-Host ""
if ($result.Method -eq 'adb') {
    Write-Host "完成。cookies.json 已推到设备：" -ForegroundColor Green
    Write-Host "  $($result.Path)"
    Write-Host "  设备侧 sidecar 每 2 秒检查一次文件变化，会自动加载；" -ForegroundColor Green
    Write-Host "  插件界面上不需要任何操作，几秒后「我的」页就会出现昵称。" -ForegroundColor Green
} elseif ($result.Method -eq 'usb') {
    Write-Host "完成。已复制到 $($result.Path)" -ForegroundColor Green
    Write-Host "  拔掉 USB 后（或 already 挂载中）sidecar 会自动加载。" -ForegroundColor Green
} elseif ($result.Method -eq 'http') {
    Write-Host "完成。设备已通过 HTTP 导入：$($result.Path)" -ForegroundColor Green
} else {
    Write-Host "完成。" -ForegroundColor Green
}
