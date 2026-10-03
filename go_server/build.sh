#!/bin/bash
# 编译 WeiboPocket 的 Go sidecar（Linux / macOS / CI 用）。
#
# 与 cc\netease 的做法一致：CGO_ENABLED=0 纯静态，产物放到**仓库根目录** server。
# Windows 上用同目录上一级的 build_server.ps1。
#
# 微博不需要短信登录辅助（走 Cookie，见 WeiboLoginModule），所以只有一个二进制。
#
# 用法：
#   ./build.sh                    # 交叉编译 linux/arm64（设备用）
#   GOARCH=amd64 ./build.sh       # 本机调试用
#   OUT=/tmp/srv ./build.sh       # 自定义输出路径

set -euo pipefail

cd "$(dirname "$0")"                 # 进入 go_server/
ROOT="$(cd .. && pwd)"               # 仓库根目录

GOOS="${GOOS:-linux}"
GOARCH="${GOARCH:-arm64}"
OUT="${OUT:-$ROOT/server}"

echo "==> go vet"
(cd main && CGO_ENABLED=0 GOOS="$GOOS" GOARCH="$GOARCH" GOTOOLCHAIN=local go vet ./...)

echo "==> 编译 server (GOOS=$GOOS GOARCH=$GOARCH)"
(cd main && CGO_ENABLED=0 GOOS="$GOOS" GOARCH="$GOARCH" GOTOOLCHAIN=local \
    go build -ldflags="-s -w" -trimpath -o "$OUT" .)

ls -lh "$OUT"
echo "==> 完成：$OUT"
