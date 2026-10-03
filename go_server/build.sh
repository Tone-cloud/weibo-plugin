#!/bin/bash
# 编译 WeiboPocket 的两个 Go sidecar。
#
# 目前只有一个 sidecar（weibo-server）。短信登录辅助不适用微博
# （微博走 Cookie / 扫码，见 WeiboLoginModule），因此这里只构建主服务。
#
# 用法：
#   ./build.sh            # 交叉编译 linux/arm64（设备用）
#   GOARCH=amd64 ./build.sh   # 本机调试用

set -euo pipefail

cd "$(dirname "$0")"

GOOS="${GOOS:-linux}"
GOARCH="${GOARCH:-arm64}"
OUT="${OUT:-weibo-server}"

echo "==> 编译 weibo-server (GOOS=$GOOS GOARCH=$GOARCH)"
cd main
CGO_ENABLED=0 GOOS="$GOOS" GOARCH="$GOARCH" \
    go build -trimpath -ldflags="-s -w" -o "../$OUT" .
cd ..

ls -lh "$OUT"
echo "==> 完成：go_server/$OUT"
