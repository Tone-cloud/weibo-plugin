# 编译 Go sidecar（交叉编译到设备的 linux/arm64）
#
# 与 cc\netease\build_server.ps1 的做法一致：
#   GOOS=linux / GOARCH=arm64 / CGO_ENABLED=0，纯静态，产物放到仓库根目录 server
#
# 用法（Windows PowerShell 默认策略是 Restricted，需要显式 Bypass）：
#   powershell -NoProfile -ExecutionPolicy Bypass -File build_server.ps1
#   ... -File build_server.ps1 -Local      # 编译本机版本，便于本地调试
#   ... -File build_server.ps1 -GoArch arm # 只改 GOARCH
#
# 注意：本文件含中文，必须带 UTF-8 BOM 保存，否则 Windows PowerShell 5.1
# 会按 GBK 解析并报语法错误。tools/verify.py 的 Q 项会检查这一点。

[CmdletBinding()]
param(
    # 编译当前平台（而不是交叉编译到设备），方便本地起服务调试。
    # 注意不能叫 -Host：$Host 是 PowerShell 的只读自动变量。
    [switch]$Local,
    [string]$GoArch = "arm64"
)

$ErrorActionPreference = "Stop"

# 优先使用本机 go-sdk（netease-music 的 build_server.ps1 用的就是这个路径），
# 找不到再回落到 PATH 上的 go。这样既能在你机器上直接跑，也能在别处复用。
$goCandidates = @(
    "C:\Users\aresi\go-sdk\go\bin\go.exe",
    "$env:USERPROFILE\go-sdk\go\bin\go.exe",
    "$env:ProgramFiles\Go\bin\go.exe"
)
$goExe = $goCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $goExe) {
    $cmd = Get-Command go -ErrorAction SilentlyContinue
    if ($cmd) { $goExe = $cmd.Source }
}
if (-not $goExe) {
    throw "找不到 go：请安装 Go 1.22+ 或把 go.exe 放到 C:\Users\aresi\go-sdk\go\bin\ 下"
}
Write-Host "使用 go: $goExe"

if ($Local) {
    # 清掉交叉编译变量，让 Go 用本机平台
    Remove-Item Env:GOOS   -ErrorAction SilentlyContinue
    Remove-Item Env:GOARCH -ErrorAction SilentlyContinue
    $target = "$PSScriptRoot\server_host.exe"
    Write-Host "==> 编译本机版本（本地调试用）"
} else {
    $env:GOOS = "linux"
    $env:GOARCH = $GoArch
    $target = "$PSScriptRoot\server"
    Write-Host "==> 交叉编译 GOOS=linux GOARCH=$GoArch"
}
$env:CGO_ENABLED = "0"
$env:GOTOOLCHAIN = "local"

# 默认 GOCACHE 在 %LOCALAPPDATA%\go-build，受限环境可能不可写；
# 显式指到仓库内的 .gocache（已在 .gitignore 里）更稳。
if (-not $env:GOCACHE) {
    $env:GOCACHE = "$PSScriptRoot\.gocache"
    New-Item -ItemType Directory -Path $env:GOCACHE -Force | Out-Null
}

Set-Location "$PSScriptRoot\go_server\main"

# 先 vet 一遍：静态检查没问题再编译，报错更好定位
& $goExe vet ./...
if ($LASTEXITCODE -ne 0) { throw "go vet 失败" }

& $goExe build -ldflags="-s -w" -trimpath -o $target .
Write-Host "Exit code: $LASTEXITCODE"
if ($LASTEXITCODE -ne 0) { throw "go build 失败" }

$artifact = Get-Item $target
Write-Host ("产物: {0}  ({1:N0} 字节)" -f $artifact.FullName, $artifact.Length)
if ($Local) {
    Write-Host '本机调试: $env:PORT=8010; $env:DEBUG=true; .\server_host.exe'
}
