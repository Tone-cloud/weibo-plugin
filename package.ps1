# 打包脚本 —— 生成可直接解压到设备的插件 zip
#
# 与 cc\netease\package.ps1 的流程一致：
#   * zip 内容是**平铺**的（metadata.json / server / icon.png / qml/ ...），
#     解压到 /userdisk/PenMods/plugins/weibo_plugin/ 即为插件根目录
#   * libweibo_plugin.so 来自 GitHub Actions 的 artifact，先放进 build\ 再打包
#
# 用法（Windows PowerShell 默认策略 Restricted，需要显式 Bypass）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File package.ps1
#   ... -File package.ps1 -RunGo           # 先跑 build_server.ps1 编译 server
#   ... -File package.ps1 -So path\to\libweibo_plugin.so
#
# 产物默认落在仓库的上一级目录：com.weibopocket.client.zip
#
# 注意：不要改回 Compress-Archive。Windows PowerShell 5.1 的 Compress-Archive
#   打包**目录**时会把分隔符写成反斜杠（zip 里出现 "qml\main.qml"）。ZIP 规范
#   要求用 "/"，Linux/Android 解压时反斜杠只是普通文件名字符 —— 结果会多出一个
#   名叫 "qml\main.qml" 的文件、却没有 qml/ 目录，插件直接加载失败。
#   因此这里用 ZipArchive 手工写条目名，强制正斜杠。

[CmdletBinding()]
param(
    [switch]$RunGo,
    [string]$So,
    [string]$Out
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot

if (-not $Out) {
    $Out = Join-Path (Split-Path $root -Parent) "com.weibopocket.client.zip"
}

if ($RunGo) {
    Write-Host "==> 先编译 Go sidecar" -ForegroundColor Cyan
    & (Join-Path $root "build_server.ps1")
}

if (Test-Path $Out) { Remove-Item $Out -Force }

# ---- libweibo_plugin.so：必须由 GitHub Actions 产出后放进来 ----
if (-not $So) {
    foreach ($cand in @("$root\build\libweibo_plugin.so", "$root\libweibo_plugin.so")) {
        if (Test-Path $cand) { $So = $cand; break }
    }
}
if (-not $So -or -not (Test-Path $So)) {
    Write-Host "警告: 找不到 libweibo_plugin.so" -ForegroundColor Yellow
    Write-Host "      .so 由 GitHub Actions 编译（.github/workflows/build.yml 的 build-so job），"
    Write-Host "      下载 artifact 后放到 $root\build\ 再重新打包。"
    Write-Host "      本次打包将不含 .so。" -ForegroundColor Yellow
}

if (-not (Test-Path "$root\server")) {
    throw "找不到 $root\server —— 先执行 build_server.ps1（或加 -RunGo）"
}

Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

# ---- 平铺文件（位于 zip 根目录） ----
$flat = @("$root\metadata.json", "$root\server", "$root\icon.png",
          "$root\README.md", "$root\cookies.example.json")
if ($So -and (Test-Path $So)) {
    $flat += (Resolve-Path $So).Path
    Write-Host "已包含 $([System.IO.Path]::GetFileName($So))" -ForegroundColor Green
}

function Write-ZipEntry {
    param(
        [System.IO.Compression.ZipArchive]$Archive,
        [string]$EntryName,
        [string]$SourcePath
    )
    $entry = $Archive.CreateEntry($EntryName,
        [System.IO.Compression.CompressionLevel]::Optimal)
    $entryStream = $entry.Open()
    $fileStream = [System.IO.File]::OpenRead($SourcePath)
    try { $fileStream.CopyTo($entryStream) }
    finally { $fileStream.Dispose(); $entryStream.Dispose() }
}

$zip = [System.IO.Compression.ZipFile]::Open(
    $Out, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in $flat) {
        # 只取文件名：与 netease 的平铺布局一致
        Write-ZipEntry -Archive $zip -EntryName ([System.IO.Path]::GetFileName($f)) -SourcePath $f
    }
    # qml 目录：保留 "qml/..." 前缀，条目名强制正斜杠
    $qmlBase = (Resolve-Path "$root\qml").Path
    Get-ChildItem -Path $qmlBase -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($qmlBase.Length + 1).Replace('\', '/')
        Write-ZipEntry -Archive $zip -EntryName "qml/$rel" -SourcePath $_.FullName
    }
} finally {
    $zip.Dispose()
}

# ---- 打包结果自检 ----
$archive = [System.IO.Compression.ZipFile]::OpenRead($Out)
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName })
} finally {
    $archive.Dispose()
}

# 1) 任何条目名带反斜杠都是错的（见文件头注释）
$bad = @($names | Where-Object { $_.Contains('\') })
if ($bad.Count -gt 0) {
    throw "zip 里有 $($bad.Count) 个条目用了反斜杠分隔符（例如 '$($bad[0])'），Linux 解压会失败"
}

# 2) metadata.json 声明的入口必须真的在包里
$meta = Get-Content "$root\metadata.json" -Raw | ConvertFrom-Json
foreach ($key in @("main_qml", "main_so")) {
    $want = $meta.$key
    if ($names -contains $want) {
        Write-Host "OK   $key -> $want"
    } elseif ($key -eq "main_so") {
        Write-Host "警告 main_so -> $want 不在包里（.so 未提供）" -ForegroundColor Yellow
    } else {
        throw "metadata.json 的 $key -> $want 不在包里！"
    }
}
if ($names -notcontains 'server') { throw "zip 里没有 server" }
if ($names -notcontains 'qml/components/Theme.qml') { throw "zip 里没有 qml/components/Theme.qml" }

$size = (Get-Item $Out).Length / 1MB
Write-Host ""
Write-Host ("打包完成: {0}" -f $Out) -ForegroundColor Green
Write-Host ("            {0} 个条目，{1:N2} MB" -f $names.Count, $size)
Write-Host "解压到设备: /userdisk/PenMods/plugins/weibo_plugin/"
