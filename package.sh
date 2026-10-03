#!/bin/bash
# 一键打包（Linux / macOS / CI）—— 生成可直接解压到设备的插件 zip
#
# 与 cc\netease\package.ps1 的流程一致：
#   zip 内容是**平铺**的（metadata.json / server / icon.png / qml/ ...），
#   解压到 /userdisk/PenMods/plugins/weibo_plugin/ 即为插件根目录。
#
# 前置：
#   * Go 1.22+  —— 用来编译 server（脚本会自己调 go_server/build.sh）
#   * libweibo_plugin.so —— 由 GitHub Actions 编译，下载后放到 build/
#     （本机有 xmake + aarch64 Qt 环境时也可以用 -x 让脚本自己编）
#
# 用法：
#   ./package.sh                 # 编译 server + 打包
#   ./package.sh -x              # 先跑 xmake 编 .so，再打包
#   SO=path/to/libweibo_plugin.so ./package.sh
#   OUT=/tmp/out.zip ./package.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

RUN_XMAKE=0
for arg in "$@"; do
    [ "$arg" = "-x" ] && RUN_XMAKE=1
done

OUT="${OUT:-$SCRIPT_DIR/../com.weibopocket.client.zip}"

if [ "$RUN_XMAKE" = "1" ]; then
    echo '==> 编译插件 (xmake)'
    xmake
fi

echo '==> 编译 Go sidecar'
./go_server/build.sh
echo '----------------'

# ---- 找 .so：优先 -SO / $SO，其次 build/ 下各处 ----
if [ -z "${SO:-}" ]; then
    for cand in \
        "build/libweibo_plugin.so" \
        "build/linux/arm64-v8a/release/libweibo_plugin.so" \
        "libweibo_plugin.so"
    do
        [ -f "$cand" ] && SO="$cand" && break
    done
fi

if [ -z "${SO:-}" ] || [ ! -f "$SO" ]; then
    echo "警告: 找不到 libweibo_plugin.so" >&2
    echo "      .so 由 GitHub Actions 编译（.github/workflows/build.yml 的 build-so job），" >&2
    echo "      下载 artifact 后放到 build/ 再打包；本机有 xmake 环境时用 ./package.sh -x" >&2
    SO=""
fi

if [ ! -f server ]; then
    echo "找不到 server —— 先执行 ./go_server/build.sh" >&2
    exit 1
fi

# ---- 平铺打包 ----
rm -f "$OUT"
FILES=(metadata.json server icon.png README.md cookies.example.json)
[ -n "$SO" ] && FILES+=(libweibo_plugin.so) && cp -f "$SO" ./libweibo_plugin.so

# -j 去掉路径前缀 → 文件位于 zip 根目录
zip -j -q "$OUT" "${FILES[@]}"
# qml 目录保留 qml/ 前缀
zip -r -q "$OUT" qml

# 清理临时拷贝
[ -n "$SO" ] && rm -f ./libweibo_plugin.so

# ---- 结果自检 ----
echo '----------------'
unzip -l "$OUT" | head -20
# metadata.json 的入口必须真的在包里
python3 - "$OUT" "$SCRIPT_DIR/metadata.json" <<'PY'
import json, sys, zipfile
out, meta_path = sys.argv[1], sys.argv[2]
meta = json.load(open(meta_path, encoding="utf-8"))
names = set(zipfile.ZipFile(out).namelist())
fail = False
for key in ("main_qml", "main_so"):
    want = meta[key]
    if want in names:
        print(f"OK   {key} -> {want}")
    elif key == "main_so":
        print(f"警告 main_so -> {want} 不在包里（.so 未提供）")
    else:
        print(f"FATAL metadata.json 的 {key} -> {want} 不在包里")
        fail = True
# 中文字体：缺了插件能起来，但中文全是方块，属于必须拦住的问题
if "qml/fonts/msyh.ttf" in names:
    print("OK   中文字体 qml/fonts/msyh.ttf")
else:
    print("FATAL zip 里没有中文字体 qml/fonts/msyh.ttf（设备上中文会显示成方块）")
    fail = True
# 条目名必须用正斜杠，反斜杠在 Linux 下会解成文件名字符
bad = [n for n in names if "\\" in n]
if bad:
    print(f"FATAL zip 有 {len(bad)} 个反斜杠条目名，例如 {bad[0]!r}")
    fail = True
sys.exit(1 if fail else 0)
PY

ls -lh "$OUT"
echo "打包完成: $OUT"
echo "解压到设备: /userdisk/PenMods/plugins/weibo_plugin/"
