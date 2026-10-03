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
| 本地服务 | `weibo-server`，监听 `127.0.0.1:8010` |

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

1. 从 Release 下载 `weibo_plugin.zip`，或自行打包（见下）
2. 解压到词典笔：

```text
/userdisk/PenMods/plugins/weibo_plugin/
```

3. 目录中应包含：

```text
weibo_plugin/
├── libweibo_plugin.so   # Qt/C++ 插件
├── weibo-server         # 本地 Go API 服务
├── qml/                 # QML 界面
├── metadata.json        # 插件入口元数据
└── icon.png
```

4. 在 PenMods 插件管理中启用 **笔里微博**，进入插件 / 重启设备完成更新。

> 插件会在 `init_plugin()` 里同步拉起本地 Go 服务（`127.0.0.1:8010`）。
> 想查看服务日志：`pkill -f weibo-server` 后以 `DEBUG=true ./weibo-server` 手动启动，
> 此时会监听 `0.0.0.0:8010` 便于在电脑上调试。

### 字体（重要）

设备自带的 CJK 字体不一定能被 QML 引用。插件按以下顺序找字体：

```text
qml/fonts/weibo.ttf        ← 推荐：自己放一个中文字体进来
系统字体（Microsoft YaHei 等）
```

仓库**不包含**字体文件（体积与授权原因）。如果界面上中文显示成方块，
把一个中文 TTF（例如开源的 **LXGW WenKai**、**思源黑体**）重命名为
`weibo.ttf` 放到 `qml/fonts/` 下即可，`Theme.qml` 会自动使用它。

### 登录

微博没有可用的开放扫码登录，因此本插件走 **Cookie 导入**：

1. 电脑浏览器登录 `https://weibo.com`
2. F12 → Application → Cookies → `https://weibo.com`
3. 复制 `SUB` 与 `SUBP` 两个值
4. 插件内：我的 → 登录 / 导入 Cookie，粘贴成：

```text
SUB=xxxxx; SUBP=yyyyy
```

Cookie 由 Go sidecar 持久化到
`/userdisk/PenMods/plugins/weibo_plugin/cookies.json`（**已在 `.gitignore` 中排除**）。

---

## 架构概览

```text
  QML UI（qml/）
    ↓ 调用
WeiboController / modules（Qt/C++，src/）
    ↓ HTTP
本地 Go sidecar（go_server/main，127.0.0.1:8010）
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

## 在 GitHub 上编译

`.github/workflows/build.yml` 里四个 job，全部在 GitHub 托管 runner 上跑，
本地不需要任何交叉编译环境：

| Job | 产物 |
|-----|------|
| `verify` | `tools/verify.py` 静态契约校验（秒级，先跑） |
| `build-so` | `libweibo_plugin.so`（aarch64，已 strip） |
| `build-server` | `weibo-server`（linux/arm64，静态） |
| `package` | `weibo_plugin.zip`（可直接解压到设备） |

`build-so` 与 netease-music 的做法完全一致 —— 依次 clone 三个仓库作为交叉编译环境：

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

CI 里额外做了三件 netease 没做的事，用来提前暴露问题：

1. **校验导出符号**：`nm -D` 确认 `init_plugin` / `attach_engine` / `destroy_plugin`
   确实导出，缺一个就直接失败（否则插件装到设备上会静默不工作）。
2. **Go 依赖校验**：`go mod tidy` 后 `go.sum` 必须为空 —— 本项目只允许标准库。
3. **打包校验**：`unzip` 前用 Python 读 `metadata.json`，确认 `main_qml` 与
   `main_so` 指向的文件在包里真实存在。

### 触发方式

```text
push 到 main/master（改到 src/ qml/ go_server/ *.pro 等路径时）
手动：Actions → Build WeiboPocket Plugin → Run workflow
```

---

## 本地开发构建

> 交叉编译目标：`arm64-v8a` / `aarch64-linux-gnu`。需要 Linux 主机。

### 方式一：xmake（与 bili 相同）

```bash
xmake f -c \
  --qt="/path/to/aarch64/qt" \
  --arch=arm64-v8a \
  --toolchain=zigcc \
  --cross=aarch64-linux-gnu.2.27 \
  -m release -vD

xmake
# 产物：build/linux/arm64-v8a/release/libweibo_plugin.so
```

### 方式二：qmake（与 CI 相同）

```bash
# 先把三个环境仓库 clone 到仓库上一级目录，并把 bin 加进 PATH
../qt-5.15.2-for-aarch64-dictpen-linux/bin/qmake weibo_plugin.pro
make -j$(nproc)
```

### 编译 Go sidecar

```bash
cd go_server/main
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags="-s -w" -o ../weibo-server .
```

或直接用脚本：

```bash
./go_server/build.sh              # 默认 linux/arm64
GOARCH=amd64 ./go_server/build.sh # 本机调试
```

### 本地调试 Go 服务

```bash
cd go_server/main
PORT=8010 DEBUG=true go run .
# 另开一个终端
curl http://127.0.0.1:8010/server/ping
curl 'http://127.0.0.1:8010/feed/hot' | head -c 400
```

### 一键打包

```bash
./package.sh                     # Linux：xmake + go build + zip
```

```powershell
pwsh -File package.ps1 -RunXmake -RunGo   # Windows
pwsh -File package.ps1                    # 只用已有产物打包
```

产物：根目录 `weibo_plugin.zip`，解压后即为设备插件根目录。

---

## 目录结构

```text
weibo_plugin/
├── qml/                       # QML 界面
│   ├── main.qml               # 路由 / 返回栈 / 页面保活
│   ├── components/            # 可复用组件
│   │   ├── qmldir             # singleton Theme + 21 个组件
│   │   └── Theme.qml          # 主题单例（必须在 components/ 下）
│   ├── pages/                 # 业务页面
│   ├── js/                    # ImageUrl / RichText / TimeText
│   └── fonts/                 # （可选）weibo.ttf
├── src/                       # Qt/C++ 插件
│   ├── WeiboController.*      # QML 边界、sidecar bring-up、插件入口
│   ├── WeiboModels.*          # 列表模型与归一化解析
│   ├── WeiboNetwork.*         # 本地 API 客户端
│   ├── WeiboJsonUtils.*       # 容错取值 / HTML / 时间 / 计数
│   ├── WeiboImageProvider.*   # image://weibo 异步图片通道
│   └── modules/               # feed / status / comment / search / profile
│                              # login / publish / topic / media / viewer
├── go_server/
│   ├── build.sh
│   └── main/                  # 本地 API 服务（Go 标准库 only）
├── tools/
│   └── verify.py              # 结构 / 契约静态校验（CI 第一个 job）
├── docs/
│   ├── SPEC.md                # HTTP + C++ + 构建契约
│   └── QML-CONTRACT.md        # QML 组件 / 页面 / 角色名契约
├── .github/workflows/build.yml
├── metadata.json
├── icon.png
├── xmake.lua
├── weibo_plugin.pro
├── package.sh / package.ps1
└── README.md
```

---

## 静态校验

没有交叉编译工具链的机器上（以及 CI 的第一步）先跑：

```bash
python3 tools/verify.py          # 0 错误才算通过（CI 第一个 job）
python3 tools/verify.py --fix    # 顺带规范化 QML 模块布局 / 补齐 import
python3 tools/go_lint.py         # Go 专用兜底：未用 import / 未用局部变量 / 未定义调用
```

`verify.py` 检查 15 类问题：metadata ↔ 打包布局、`weibo_plugin.pro` ↔ 源文件、
`components/qmldir` ↔ 实际组件文件、QML 括号与根元素、每个页面有 `controller`、
每个用到 `Theme`/组件的页面有正确的 import、QML 相对路径（import / `source:`）
能解析到真实文件、C++ 头文件声明的函数在 `.cpp` 里都有定义、
头文件声明与 `.cpp` 定义的参数个数 / `const` 一致、重复定义（链接错误）、
每个 `Q_INVOKABLE` / `Q_PROPERTY READ` 都有定义、QML 里
`controller.<模块>.<方法>()` 调用 ↔ 头文件对账、Go 只用标准库、
`routes.go` ↔ `docs/SPEC.md` 路由对齐、workflow YAML 结构。

> 它们**不能**替代编译，但能挡住绝大多数"编译通过、上机才报错"的问题 —— 尤其是
> QML 调用 C++ 方法名写错（`Property 'xxx' is not a function`）、
> Theme 单例因模块布局不对而不可见、以及 C++ 声明/定义签名漂移。

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
