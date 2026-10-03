# 笔里微博（WeiboPocket）

适用于 **有道词典笔 2 代（YDP02x）** + PenMods 的微博客户端插件。

面向 `320 × 170` 触摸屏设计，以插件形式运行在 PenMods 插件系统中。
架构完全仿照 [BiliPocket](https://github.com/Lyrecoul) 的 `bili_plugin`
（QML 界面 + Qt/C++ 插件 + 本地 Go sidecar），交叉编译流程参照
[netease-music](https://github.com/Tone-cloud/netease-music)。

| 项目 | 说明 |
|------|------|
| 插件 ID | `com.weibopocket.client` |
| 当前版本 | `1.0.0` |
| 作者 | WeiboPocket |
| 安装路径 | `/userdisk/PenMods/plugins/weibo_plugin/` |
| 主 so | `libweibo_plugin.so` |
| 本地服务 | `server`，监听 `127.0.0.1:8010` |

---

## 功能

- **首页**：推荐信息流 / 关注流，下拉式加载更多、一键刷新
- **微博详情**：正文富文本（@提及 / #话题# / 链接可点）、九宫格图片、转发原微博、媒体块
- **图片查看**：横向滑动大图，走异步图片通道（带内存缓存与圆形头像裁剪）
- **评论**：热门评论、二级评论面板、发表评论 / 回复 @某人、删评论、评论点赞
- **搜索**：微博 / 用户 / 话题 三类搜索 + 本地搜索历史 + 热搜榜
- **热搜榜**：左侧榜单 + 右侧该话题微博流
- **用户主页**：资料、微博列表、关注 / 粉丝列表、关注 / 取关
- **我的**：我的微博、我的收藏、@我的、我的超话、发布微博、退出登录
- **超话签到**：我的超话列表、单个签到、**一键签到**（串行 + 250ms 间隔防风控）
- **发布微博**：文字 + 可见性，草稿本地持久化
- **视频 / 直播**：解析 `page_info` 直链，支持清晰度选择，交给宿主播放器
- **登录**：Cookie 导入（`SUB` / `SUBP`），登录态持久化到插件目录

---

## 安装 / 更新

1. 从 Release 下载 `com.weibopocket.client.zip`，或自行打包（见下）
2. 解压到词典笔：

```text
/userdisk/PenMods/plugins/weibo_plugin/
```

3. 目录中应包含：

```text
weibo_plugin/
├── libweibo_plugin.so   # Qt/C++ 插件
├── server         # 本地 Go API 服务
├── qml/                 # QML 界面
├── metadata.json        # 插件入口元数据
└── icon.png
```

4. 在 PenMods 插件管理中启用 **笔里微博**，进入插件 / 重启设备完成更新。

> 插件会在 `init_plugin()` 里同步拉起本地 Go 服务（`127.0.0.1:8010`）。
> 想查看服务日志：`pkill -f server` 后以 `DEBUG=true ./server` 手动启动，
> 此时会监听 `0.0.0.0:8010` 便于在电脑上调试。

### 字体

设备自带的字体没有中文字形，所以插件**自带**一份中文字体：

```text
qml/fonts/msyh.ttf         ← 已随仓库提供（Microsoft YaHei，14.3 MB）
系统字体（Microsoft YaHei 等）  ← FontLoader 异步加载完成前的回退
```

这份文件与 [`cc/bili_plugin/qml/msyh.ttf`] 是同一份 —— bili 插件在同一台词典笔上
用的就是它，字形覆盖和加载性能都验证过。**别删**：缺了它插件照样能启动，
但界面上中文全是方块。

想换成开源字体（体积/授权考虑）：保持文件名不变直接替换即可，
要求与注意事项见 [`qml/fonts/README.md`](qml/fonts/README.md)。
`tools/verify.py` 的 U 项会校验它确实是真 TrueType（能挡住 LFS 指针、
被当文本转换、误用 `.ttc` 这几种「静默失败」），CI 也会断言它在安装包里。

### 登录

微博没有可用的开放扫码登录，因此本插件走 **Cookie 导入**。

**推荐做法：在电脑浏览器里粘贴，词典笔上零操作。**

和 [bili 插件](https://github.com/Lyrecoul) 的 `bili-sms:8666`、
[netease-music](https://github.com/Tone-cloud/netease-music) 的登录服务 `:8667`
同一思路：sidecar 单独监听一个端口给局域网，**主 API 仍然只在 `127.0.0.1`**，
暴露出去的只有一张「粘贴 Cookie」的页面。

1. 电脑浏览器登录 `https://weibo.com`
2. F12 → Application → Cookies → `https://weibo.com`，复制 `SUB` 与 `SUBP` 的值
3. **在词典笔上**：设置 → 电脑端导入 → 屏幕上会显示一条链接，例如

```text
http://192.168.1.167:8011/5a51f58a
```

4. **在电脑浏览器**打开这条链接，把 `SUB=…; SUBP=…` 粘进去（或直接选一个
   `cookies.json` 文件），点「导入到词典笔」

就这两步。页面会立刻告诉你结果（`已登录：昵称` 或
`已保存但未通过校验`），而词典笔上的界面会在 **4 秒内自动登录**，
不需要在笔上点任何东西。

页面还能「下载词典笔上的 cookies.json」，用来备份或换设备。

> **地址怎么来的**：sidecar 会挑一个电脑最可能连上的私网地址
> （优先 `192.168.x` → `10.x` → `172.16-31.x`，跳过 `169.254.x` 这类链路本地地址
> 和虚拟网卡），并在启动日志里打印备用地址。链接里带 8 位一次性 token，
> 同网段的陌生设备猜不到入口、进不了导入页；不带 token 的 `/` 只会看到一张
> 提示页。关闭这个页面：`WEIBO_LOGIN_PORT=0`；换端口：`WEIBO_LOGIN_PORT=8012`。

为什么导入后笔上不用点任何东西：

| 机制 | 说明 |
|------|------|
| 导入页落盘 | 页面提交后服务端立刻把 Cookie 写进 `cookies.json` |
| sidecar 文件监听 | 每 2 秒 stat 一次 `cookies.json`，一变就自动重新加载并校验 |
| `GET /server/state` | 纯本地快照、**不请求微博**，所以插件可以放心高频轮询 |
| 插件 4 秒轮询 | 发现「已登录且校验通过」就自动更新界面并弹一条提示 |

**其它导入方式**（都走同一套自动生效机制）：

```bash
# 1) 直接把 cookies.json 丢进插件目录（PenManager / 挂载 / 文件管理器都行）
#    /userdisk/PenMods/plugins/weibo_plugin/cookies.json
#    格式见 cookies.example.json，开头带不带 UTF-8 BOM 都能识别

# 2) 自动化 / 脚本：POST 到主 API（需要先以 WEIBO_BIND=0.0.0.0 启动）
#    接受 cookies.json 原文 / 裸数组 / {"cookie":"SUB=..."} / 裸文本 四种写法
curl -X POST http://<笔IP>:8010/config/import \
     -H 'Content-Type: application/json' \
     -d '{"cookies":[{"name":"SUB","value":"..."},{"name":"SUBP","value":"..."}]}'
```

**在笔上手动粘贴**（不推荐，屏幕太小容易输错）：
设置 → 账号 → 导入 Cookie，粘贴 `SUB=…; SUBP=…`。

> 判断是否真的登录成功：`verified` 才代表微博确认过。Cookie 过期时页面会提示
> 「Cookie 已保存，但未通过登录校验」——文件已经存进去了，等你在电脑上换一份
> 新的再导入一次即可，不需要在笔上删旧文件。

Cookie 由 Go sidecar 持久化到
`/userdisk/PenMods/plugins/weibo_plugin/cookies.json`（**已在 `.gitignore` 中排除**）。

---

## 架构概览

```text
  QML UI（qml/）
    ↓ 调用
WeiboController / modules（Qt/C++，src/）
    ↓ HTTP
本地 Go sidecar（go_server/main）
  ├─ 主 API   127.0.0.1:8010      ← 只有插件自己访问
  └─ 导入页   0.0.0.0:8011        ← 只有「粘贴 Cookie」这一张页面给局域网
    ↓
上游 m.weibo.cn / weibo.com
```

| 层级 | 路径 | 职责 |
|------|------|------|
| UI | `qml/` | 页面路由、交互、绑定展示 |
| 插件运行时 | `src/` | QML 类型注册、网络、模型、业务模块 |
| 本地 API | `go_server/main` | Cookie / 登录态、上游接口代理与字段归一化 |

入口契约见 `metadata.json`：`main_qml` = `qml/main.qml`，`main_so` = `libweibo_plugin.so`。
接口契约见 [`docs/SPEC.md`](docs/SPEC.md)，QML 契约见 [`docs/QML-CONTRACT.md`](docs/QML-CONTRACT.md)。

### PenMods 插件 ABI

`.so` 导出三个 C 符号，与 `bili_plugin` / `netease_player` 一致：

```cpp
extern "C" void init_plugin();                     // 宿主加载后立即调用
extern "C" void attach_engine(QQmlEngine *engine);  // QML 引擎就绪后调用
extern "C" void destroy_plugin();
```

`init_plugin()` 可能运行在**没有事件循环的加载线程**，所以它只做
`qmlRegisterType` + 同步拉起 sidecar；`WeiboNetwork` 单例的首次创建放在
`attach_engine()`（GUI 线程），否则 `QNetworkAccessManager` 会被钉在错误线程上，
表现是「请求发得出去但永远收不到 `finished`」。

---

## 在 GitHub 上编译（.so 走这里）

`.github/workflows/build.yml` 里四个 job，全部在 GitHub 托管 runner 上跑，
本机不需要任何交叉编译环境：

| Job | 产物 |
|-----|------|
| `verify` | `tools/verify.py` + `tools/go_lint.py` 静态契约校验（秒级，先跑） |
| `build-so` | `libweibo_plugin.so`（aarch64，已 strip）← **这就是要下回来的那个** |
| `build-server` | `server`（linux/arm64，静态；可选，本机也能编，见下） |
| `package` | `com.weibopocket.client.zip`（可直接解压到设备） |

`build-so` 与 netease-music 的工作流做法完全一致 —— 依次 clone 三个仓库作为交叉编译环境：

```text
https://github.com/Lyrecoul/qt-5.15.2-for-aarch64-dictpen-linux.git
https://github.com/Lyrecoul/aarch64-dictpen-linux-gnu-gcc-toolchain.git
https://github.com/Lyrecoul/dictpen-libs.git
```

然后：

```bash
./qt-5.15.2-for-aarch64-dictpen-linux/bin/qmake weibo_plugin.pro
make -j$(nproc)
aarch64-dictpen-linux-gnu-strip --strip-unneeded build/libweibo_plugin.so
```

CI 里额外做了四件 netease 没做的事，用来提前暴露问题：

1. **校验导出符号**：`nm -D` 确认 `init_plugin` / `attach_engine` / `destroy_plugin`
   确实导出，缺一个就直接失败（否则插件装到设备上会静默不工作）。
2. **Go 依赖校验**：`go mod tidy` 后 `go.sum` 必须为空 —— 本项目只允许标准库；
   并跑 `go vet` 与一次真实的 `/server/ping` 冒烟测试。
3. **打包校验**：读 `metadata.json`，确认 `main_qml` (`qml/main.qml`) 与
   `main_so` (`libweibo_plugin.so`) 在 zip 里真实存在，并断言 zip 里**没有**
   反斜杠条目名。
4. **打包前校验布局**：`server` / `qml/main.qml` / `icon.png` / `cookies.example.json`
   一个都不能少。

### 触发方式

```text
push 到 main/master（改到 src/ qml/ go_server/ *.pro 等路径时）
手动：Actions → Build WeiboPocket Plugin → Run workflow
```

### 拿产物

```text
Actions → 某次运行 → Artifacts
  ├── libweibo_plugin            → 解压得到 libweibo_plugin.so
  ├── server                     → 解压得到 server（可选）
  └── com.weibopocket.client-zip → 直接可装的完整包
```

---

## 本地开发构建

### 编译 Go sidecar（本机，与 cc\netease 的做法一致）

netease 用 `build_server.ps1` 在 Windows 上交叉编译，本项目同样是这个流程：

```powershell
# Windows PowerShell 默认是 Restricted，脚本要显式 Bypass
powershell -NoProfile -ExecutionPolicy Bypass -File build_server.ps1

# 产物：仓库根目录 server（linux/arm64，纯静态）
# 本机调试版本：
powershell -NoProfile -ExecutionPolicy Bypass -File build_server.ps1 -Local
#   -> server_host.exe
```

脚本会自动找 Go：优先 `C:\Users\aresi\go-sdk\go\bin\go.exe`（netease 的脚本用的
就是这个路径），其次 `%USERPROFILE%\go-sdk`、`Program Files\Go`，最后回落 PATH。
编译前会先跑 `go vet`。

Linux / macOS / CI：

```bash
./go_server/build.sh                # → 仓库根目录 server（linux/arm64）
GOARCH=amd64 ./go_server/build.sh   # 本机平台
```

等价的裸命令（与 CI 相同）：

```bash
cd go_server/main
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags="-s -w" -o ../../server .
```

### 本地调试 Go 服务

```bash
cd go_server/main
PORT=8010 DEBUG=true go run .
# 另开一个终端
curl http://127.0.0.1:8010/server/ping
curl 'http://127.0.0.1:8010/feed/hot' | head -c 400
```

### 编译 .so（本机，可选）

`.so` 推荐直接用 GitHub Actions 的产物。本机要自己编的话：

```bash
# 方式一：xmake（与 bili 相同）
xmake f -c --qt="/path/to/aarch64/qt" --arch=arm64-v8a \
        --toolchain=zigcc --cross=aarch64-linux-gnu.2.27 -m release -vD
xmake
# 产物：build/linux/arm64-v8a/release/libweibo_plugin.so

# 方式二：qmake（与 CI 相同，三个环境仓库 clone 到仓库根目录旁）
./qt-5.15.2-for-aarch64-dictpen-linux/bin/qmake weibo_plugin.pro
make -j$(nproc)
```

### 一键打包

打包与 netease 的 `package.ps1` 一致：**平铺布局**（`metadata.json` / `server` /
`icon.png` / `qml/` 直接在 zip 根目录），解压到
`/userdisk/PenMods/plugins/weibo_plugin/` 即为插件根目录。

```powershell
# Windows：.so 先放到 build\ 下（从 Actions artifact 解压）
powershell -NoProfile -ExecutionPolicy Bypass -File package.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File package.ps1 -RunGo   # 顺便编 server
# 产物：仓库上一级目录 com.weibopocket.client.zip
```

```bash
# Linux / macOS
./package.sh          # 编译 server + 打包
./package.sh -x       # 顺便跑 xmake 编 .so
```

打包脚本会自检：zip 里不能有反斜杠条目名、`metadata.json` 声明的 `main_qml` /
`main_so` 必须在包里、`server` 与 `qml/components/Theme.qml` 必须存在。

> ⚠ **不要用 `Compress-Archive` 打包目录**。Windows PowerShell 5.1 的
> `Compress-Archive` 会把目录分隔符写成反斜杠（zip 里出现 `qml\main.qml`）。
> ZIP 规范要求 `/`，Linux/Android 解压时反斜杠只是普通文件名字符 ——
> 结果是多出一个叫 `qml\main.qml` 的文件、却没有 `qml/` 目录，插件直接加载失败。
> `package.ps1` 因此改用 `ZipArchive` 手工写条目名。

> ⚠ **`.ps1` 必须带 UTF-8 BOM**。Windows PowerShell 5.1 读没有 BOM 的 `.ps1`
> 会按系统 ANSI 代码页（简中机器上是 GBK）解码，中文注释变乱码后可能凑出引号/
> 花括号，直接报 `Unexpected token` 语法错误。`tools/verify.py` 的 Q 项会检查
> 这一点，`--fix` 自动补 BOM。反过来 `.sh` **绝不能**有 BOM（会破坏 shebang）。


---

## 目录结构

```text
weibo_plugin/
├── qml/                       # QML 界面
│   ├── main.qml               # 路由 / 返回栈 / 页面保活
│   ├── components/            # 可复用组件
│   │   ├── qmldir             # singleton Theme + 21 个组件
│   │   └── Theme.qml          # 主题单例（必须在 components/ 下）
│   ├── pages/                 # 业务页面（13 个）
│   ├── js/                    # ImageUrl / RichText / TimeText
│   └── fonts/                 # msyh.ttf（中文字体，必须随包）
├── src/                       # Qt/C++ 插件
│   ├── WeiboController.*      # QML 边界、sidecar bring-up、插件入口
│   ├── WeiboModels.*          # 列表模型与归一化解析
│   ├── WeiboNetwork.*         # 本地 API 客户端
│   ├── WeiboJsonUtils.*       # 容错取值 / HTML / 时间 / 计数
│   ├── WeiboImageProvider.*   # image://weibo 异步图片通道
│   └── modules/               # feed / status / comment / search / profile
│                              # login / publish / topic / media / viewer
├── go_server/
│   ├── build.sh               # Linux 编译 server（→ 仓库根目录 server）
│   └── main/                  # 本地 API 服务（Go 标准库 only，21 个 .go）
├── tools/
│   ├── verify.py              # 结构 / 契约静态校验（20 项，CI 第一个 job）
│   ├── go_lint.py             # Go 兜底：未用 import / 未用局部变量 / 未定义调用
│   ├── lambda_this.py         # lambda 缺 this 侦测
│   └── test_autoimport.py     # 集成测试（导入页 + 监听 + BOM，不需要真机）
├── docs/
│   ├── SPEC.md                # HTTP + C++ + 构建契约
│   └── QML-CONTRACT.md        # QML 组件 / 页面 / 角色名契约
├── .github/workflows/build.yml
├── build_server.ps1           # Windows 编译 server（同 cc\netease）
├── package.sh / package.ps1   # 打包（平铺 zip，同 cc\netease）
├── xmake.lua                  # 本地 .so 构建（同 bili）
├── weibo_plugin.pro           # CI 用的 qmake 工程
├── metadata.json
├── icon.png
├── cookies.example.json       # Cookie 模板（无真实值，可提交）
├── .gitattributes / .gitignore
└── README.md
```

---

## 静态校验

没有交叉编译工具链的机器上（以及 CI 的第一步）先跑：

```bash
python3 tools/verify.py          # 0 错误才算通过（CI 第一个 job）
python3 tools/verify.py --fix    # 顺带规范化 QML 模块布局 / 补齐 import / 修 BOM
python3 tools/go_lint.py         # Go 专用兜底：未用 import / 未用局部变量 / 未定义调用
```

### 集成测试（不需要真实词典笔）

在本机跑一个 host 版 sidecar 当「假设备」，验证导入的完整链路：

```bash
cd go_server/main && CGO_ENABLED=0 go build -o ../../server_host . && cd ../..

# 覆盖：导入页（token/表单/导出）、/config/import 四种请求体、文件监听、
#       UTF-8 BOM 容忍、全部错误分支
python3 tools/test_autoimport.py
```

`verify.py` 检查 20 类问题：metadata ↔ 打包布局、`weibo_plugin.pro` ↔ 源文件、
`components/qmldir` ↔ 实际组件文件、QML 括号与根元素、每个页面有 `controller`、
每个用到 `Theme`/组件的页面有正确的 import、QML 相对路径（import / `source:`）
能解析到真实文件、C++ 头文件声明的函数在 `.cpp` 里都有定义、
头文件声明与 `.cpp` 定义的参数个数 / `const` 一致、重复定义（链接错误）、
每个 `Q_INVOKABLE` / `Q_PROPERTY READ` 都有定义、QML 里
`controller.<模块>.<方法>()` 调用 ↔ 头文件对账、`model.<角色>` ↔ `roleNames()`、
most vexing parse（CI 真实踩过的编译错误）、lambda 缺 `this`（同上）、
`.ps1` 的 UTF-8 BOM 策略、Go 只用标准库、
`routes.go` ↔ `docs/SPEC.md` 路由对齐、workflow YAML 结构。

> 它们**不能**替代编译，但能挡住绝大多数"编译通过、上机才报错"的问题 —— 尤其是
> QML 调用 C++ 方法名写错（`Property 'xxx' is not a function`）、
> Theme 单例因模块布局不对而不可见、C++ 声明/定义签名漂移，
> 以及 most vexing parse / lambda 漏捕获 `this` 这两类**只有编译器才会发现**的错误。

---

## 已知限制 / 声明

- 本项目依赖 PenMods 插件机制，**不是**独立桌面客户端。
- 上游使用的是微博 **Web / H5 私有接口**（`m.weibo.cn`、`weibo.com/ajax`），
  并非公开开放平台 API。接口与字段可能随时变化；所有上游 URL 集中在
  `go_server/main/endpoints.go`，字段解析集中在 `go_server/main/parse.go`，
  上游调整时只需改这两个文件。
- 部分写操作（发微博、超话签到、点赞）在风控严格时会失败并返回中文错误提示。
- 视频 / 直播播放依赖宿主提供的播放器（插件目录下的 `playvideo`），
  不可用时回落到系统 URL 打开方式。
- 登录 Cookie 仅保存在设备本地（`cookies.json`）；请妥善保管设备与账号。
- 使用第三方客户端访问微博接口可能违反平台规则，风险自负。
- 仅用于学习和测试，请于下载后 24 小时内删除。所有 API 均从官方网站收集，
  不提供任何破解内容。

---

## 致谢

- [Lyrecoul](https://github.com/Lyrecoul) — Qt aarch64 交叉编译环境、PenMods 插件框架
- [BiliPocket](https://github.com/Lyrecoul) — 本项目的架构与 UI 直接仿照对象
- [Tone-cloud/netease-music](https://github.com/Tone-cloud/netease-music) — GitHub Actions 交叉编译流程
- [LXGW WenKai](https://github.com/lxgw/LxgwWenKai) — 推荐的中文字体
