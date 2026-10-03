# WeiboPocket 打包脚本（Windows / PowerShell）
#
# 用法：
#   pwsh -File package.ps1                 # 用已编译好的产物打包
#   pwsh -File package.ps1 -RunXmake       # 先执行 xmake
#   pwsh -File package.ps1 -RunXmake -RunGo
#
# 需要：xmake（交叉编译 so）、Go 1.22+（编译 sidecar）、Compress-Archive

[CmdletBinding()]
param(
    [switch]$RunXmake,
    [switch]$RunGo,
    [string]$SoPath = "build\linux\arm64-v8a\release\libweibo_plugin.so",
    [string]$GoBin  = "go_server\weibo-server"
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Push-Location $root
try {
    if ($RunXmake) {
        Write-Host "==> xmake" -ForegroundColor Cyan
        & xmake
        if ($LASTEXITCODE -ne 0) { throw "xmake 失败" }
    }

    if ($RunGo) {
        Write-Host "==> go build (linux/arm64)" -ForegroundColor Cyan
        Push-Location (Join-Path $root "go_server\main")
        $env:CGO_ENABLED = "0"; $env:GOOS = "linux"; $env:GOARCH = "arm64"
        & go build -trimpath -ldflags="-s -w" -o "..\weibo-server" .
        if ($LASTEXITCODE -ne 0) { throw "go build 失败" }
        Pop-Location
    }

    if (-not (Test-Path $SoPath)) { throw "找不到 $SoPath，先执行 xmake 或用 -SoPath 指定" }
    if (-not (Test-Path $GoBin))  { throw "找不到 $GoBin，先执行 ./go_server/build.sh 或用 -RunGo" }

    $staging = Join-Path $root "weibo_plugin"
    if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
    New-Item -ItemType Directory -Path $staging | Out-Null

    Copy-Item $SoPath  (Join-Path $staging "libweibo_plugin.so")
    Copy-Item $GoBin   (Join-Path $staging "weibo-server")
    Copy-Item (Join-Path $root "metadata.json") $staging
    Copy-Item (Join-Path $root "icon.png")      $staging
    Copy-Item (Join-Path $root "qml")           $staging -Recurse

    $zip = Join-Path $root "weibo_plugin.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $staging "*") -DestinationPath $zip -Force

    Remove-Item $staging -Recurse -Force
    Write-Host "打包完成：$zip" -ForegroundColor Green
    Get-Item $zip | Select-Object FullName, Length
}
finally {
    Pop-Location
}
