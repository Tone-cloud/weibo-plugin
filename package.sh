#!/bin/bash
# 一键打包：编译 so + Go sidecar，组装 weibo_plugin/ 并压成 weibo_plugin.zip
#
# 前置：
#   - xmake 已配置好 aarch64 Qt 交叉编译环境（见 README）
#   - Go 1.22+
#
# 产物：根目录 weibo_plugin.zip，解压后即为设备插件根目录 weibo_plugin/

set -euo pipefail

pwd_dir="$(pwd)"
echo "当前目录：$pwd_dir"

echo '==> 编译插件 (xmake)'
xmake

echo '==> 编译 Go 服务器'
cd "$pwd_dir/go_server"
./build.sh
cd "$pwd_dir"
echo '----------------'

SO_PATH="build/linux/arm64-v8a/release/libweibo_plugin.so"
if [ ! -f "$SO_PATH" ]; then
    echo "找不到 $SO_PATH" >&2
    exit 1
fi

# 临时目录
rm -rf weibo_plugin
mkdir -p weibo_plugin
cp "$SO_PATH"          ./weibo_plugin/libweibo_plugin.so
cp go_server/weibo-server ./weibo_plugin/weibo-server
chmod +x ./weibo_plugin/weibo-server
cp -r ./qml             ./weibo_plugin
cp metadata.json        ./weibo_plugin
cp icon.png             ./weibo_plugin

# 打包
rm -f weibo_plugin.zip
zip -r weibo_plugin.zip weibo_plugin/*

# 清理
rm -rf ./weibo_plugin

echo '----------------'
ls -lh weibo_plugin.zip
echo '打包完成'
