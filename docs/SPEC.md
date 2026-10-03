# WeiboPocket（笔里微博）插件规格 / 内部契约

> 本文件是本项目的**唯一契约来源**。所有 C++ / QML / Go 代码必须与本文件一致。
> 架构完全仿照 `BiliPocket`（bili_plugin），交叉编译流程参照
> `Tone-cloud/netease-music`。

---

## 1. 目标平台

| 项 | 值 |
|----|----|
| 设备 | 有道词典笔 2 代（YDP02x）+ PenMods |
| 屏幕 | 320 × 170 触摸屏，横屏，无物理键盘 |
| 插件根目录 | `/userdisk/PenMods/plugins/weibo_plugin/` |
| 插件 ID | `com.weibopocket.client` |
| 版本 | `1.0.0` |
| 作者 | WeiboPocket |
| 主 so | `libweibo_plugin.so` |
| 主 QML | `qml/main.qml` |
| QML 模块名 | `WeiboPlugin` |
| QML 命名空间 | `WeiboPlugin 1.0` |
| 图片来源 | `image://weibo/...` |
| 本地 Go 服务 | `server`，监听 `127.0.0.1:8010` |
| 目标架构 | `aarch64-linux-gnu` / arm64-v8a |

### PenMods 插件 ABI（与 bili 完全一致）

`.so` 必须导出三个 C 符号：

```cpp
extern "C" void init_plugin();                    // 宿主加载后立即调用
extern "C" void attach_engine(QQmlEngine *engine); // QML 引擎就绪后调用
extern "C" void destroy_plugin();                  // 插件卸载时调用
```

`init_plugin()` **可能运行在无事件循环的加载线程**，因此：

* 只能做 `qmlRegisterType` + 同步拉起 Go sidecar（`QProcess::waitForStarted` + TCP 探测）。
* **绝对不能**在该函数里首次创建 `WeiboNetwork` 单例（会把 QNAM 钉在错误线程）。
* `WeiboNetwork::instance()` 的首次调用必须在 `attach_engine()`（GUI 线程）。

---

## 2. 目录结构

```text
weibo_plugin/
├── metadata.json
├── icon.png
├── README.md
├── xmake.lua                 # 本地/CI 备选构建（仿 bili）
├── weibo_plugin.pro          # qmake 工程（GitHub Actions 主用，仿 netease）
├── package.sh                # Linux 打包
├── package.ps1               # Windows 打包
├── docs/SPEC.md
├── .github/workflows/build.yml
├── src/
│   ├── WeiboController.{h,cpp}
│   ├── WeiboModels.{h,cpp}
│   ├── WeiboNetwork.{h,cpp}
│   ├── WeiboImageProvider.{h,cpp}
│   ├── WeiboAsyncUtils.hpp
│   ├── WeiboJsonUtils.h
│   ├── WeiboListFetch.hpp
│   └── modules/
│       ├── feed/WeiboFeedModule.{h,cpp}
│       ├── status/WeiboStatusModule.{h,cpp}
│       ├── comment/WeiboCommentModule.{h,cpp}
│       ├── search/WeiboSearchModule.{h,cpp}
│       ├── profile/WeiboProfileModule.{h,cpp}
│       ├── login/WeiboLoginModule.{h,cpp}
│       ├── publish/WeiboPublishModule.{h,cpp}
│       ├── topic/WeiboTopicModule.{h,cpp}
│       ├── media/WeiboMediaModule.{h,cpp}
│       └── viewer/WeiboViewerModule.{h,cpp}
├── qml/
│   ├── main.qml
│   ├── components/             # qmldir + Theme.qml + 21 个组件
│   │   ├── qmldir              # singleton Theme 1.0 Theme.qml + 全部组件
│   │   └── Theme.qml           # 主题单例（必须在 components/ 下，见 §9）
│   ├── js/{ImageUrl.js,RichText.js,TimeText.js}
│   ├── pages/*.qml
│   └── fonts/                  # （可选）weibo.ttf
├── tools/verify.py             # 结构 / 契约静态校验（CI 第一个 job）
├── tools/go_lint.py            # Go 兜底静态检查
├── tools/lambda_this.py        # lambda 缺 this 侦测（被 verify.py 的 S 项复用）
├── tools/pen-push.ps1          # 电脑端：生成 cookies.json 并自动送入词典笔
├── build_server.ps1            # Windows 编译 server（同 cc\netease，产物→仓库根）
├── cookies.example.json        # Cookie 模板（无真实值，随包发布）
└── go_server/
    ├── build.sh                # Linux 编译 server（产物→仓库根 server）
    └── main/                 # go module: weibopocket/server
        ├── go.mod
        ├── main.go  config.go  log.go  http.go  routes.go
        ├── client.go  cookies.go  util.go
        ├── endpoints.go        # 全部上游 URL 常量，便于维护
        ├── parse.go            # m.weibo.cn 原始 JSON → 归一化结构
        ├── api_feed.go  api_status.go  api_comment.go  api_search.go
        ├── api_user.go  api_login.go   api_publish.go  api_topic.go
        ├── api_media.go  api_favorite.go
        └── handlers.go
```

---

## 3. HTTP 契约（Go sidecar ↔ C++）

* 监听 `127.0.0.1:8010`（`PORT` 环境变量可覆盖，`DEBUG=true` 时监听 `0.0.0.0`）。
* 所有响应为 `application/json; charset=utf-8`，统一信封：

```json
{ "code": 0, "message": "", "data": { } }
```

* `code == 0` 表示成功，`data` 为对象（**永远不是数组**，数组统一放在 `items` 字段）。
* `code != 0` 表示失败，`message` 为人类可读中文错误。
* C++ 侧 `WeiboNetwork` 只认这三个字段。
* 需要登录态的接口在未登录时返回 `code = -100, message = "未登录"`；
  Cookie 失效返回 `code = -101, message = "登录已过期"`。
  C++ 侧收到 `-100/-101` 必须调用 `WeiboController::clearLocalLoginState()`。

### 3.1 归一化的 `BlogItem`（微博）

```jsonc
{
  "id": "5001234567890123",     // mblog id，字符串（超过 int32）
  "bid": "NkLmOpQr",            // 短链 id，可用于 https://m.weibo.cn/detail/<bid>
  "text": "正文纯文本",          // HTML 已剥离，保留换行
  "text_html": "正文 HTML",      // 可选，含 <a href="..."> 与图片表情
  "created_at": "2024-01-01 12:00:00",
  "created_ts": 1704096000,
  "source": "iPhone 15 Pro",
  "region_name": "发布于 北京",
  "is_long_text": false,
  "author": { UserItem },
  "pics": [ { "url": "...", "large": "...", "width": 800, "height": 600 } ],
  "page_info": {                 // 无媒体时 type = "none"
    "type": "none|video|live|article|music",
    "title": "", "cover": "", "url": "", "media_url": "",
    "duration": 0, "live_status": 0
  },
  "retweeted": BlogItem|null,    // 转发原微博，结构同本对象（不再嵌套第三层）
  "reposts_count": 0,
  "comments_count": 0,
  "attitudes_count": 0,
  "attitudes_status": 0,         // 0 未赞 / 1 已赞
  "favorited": false,
  "can_delete": false,
  "topic_ids": ["100808..."]
}
```

### 3.2 归一化的 `UserItem`

```jsonc
{
  "id": 1234567890,              // 数字 uid
  "name": "昵称",
  "avatar": "https://...",       // 头像原图
  "cover": "https://...",        // 主页封面
  "verified": true,
  "verified_type": 0,            // 0 黄V 2 蓝V 7 蓝V企业 -1 无
  "verified_reason": "认证说明",
  "description": "简介",
  "followers": 1000,
  "following": 10,
  "statuses_count": 100,
  "gender": "m|f|n",
  "location": "北京",
  "following_me": false,
  "following": true,             // 我是否已关注 TA
  "is_me": false
}
```

> 注意：`UserItem` 里 `id` 是**数字**；`BlogItem.id` 是**字符串**。

### 3.3 归一化的 `CommentItem`

```jsonc
{
  "id": "5001234567890123",
  "text": "评论内容",
  "text_html": "",
  "created_at": "2024-01-01 12:00:00",
  "created_ts": 1704096000,
  "like_count": 0,
  "liked": false,
  "reply_count": 0,
  "user": { UserItem },
  "reply_to": "被回复者昵称",     // 可空
  "pics": [ { "url": "...", "large": "...", "width": 0, "height": 0 } ],
  "can_delete": false
}
```

### 3.4 其它归一化结构

`HotItem`（热搜）：
```jsonc
{ "rank": 1, "word": "关键词", "raw_hot": 1234567, "label": "热|新|沸|爆|",
  "url": "https://s.weibo.com/weibo?q=...", "category": "" }
```

`TopicItem`（话题/超话）：
```jsonc
{ "id": "100808abc", "container_id": "100808abc", "name": "#话题#",
  "desc": "", "cover": "", "read_count": 0, "discuss_count": 0,
  "is_super": true, "level": 0, "exp": 0, "rank": 0,
  "checked": false, "signed_days": 0, "fans_count": 0, "type": "super|topic" }
```

`PageInfo`（统一分页游标）：
```jsonc
{ "since_id": "", "max_id": 0, "max_id_type": 0, "page": 1, "has_more": true }
```

---

## 4. 路由清单（Go sidecar）

**约定**：`GET` 走 query 参数；写操作走 `POST` + JSON body。

| 方法 | 路径 | 参数 | data |
|------|------|------|------|
| GET | `/` | — | 接口索引（人类可读） |
| GET | `/server/ping` | — | `{ "ok":true, "version":"1.0.0", "logged_in":bool }` |
| GET | `/server/state` | — | 本地快照，**不请求上游**：`{ "logged_in":bool, "verified":bool, "uid":0, "screen_name":"", "avatar":"", "cookie_rev":"", "cookie_file":"", "plugin_dir":"", "version":"1.0.0" }` |
| GET | `/config` | — | `{ "logged_in":bool, "verified":bool, "uid":0, "screen_name":"", "avatar":"" }` |
| POST | `/config/import` | 见下 | `{ "ok":true, "saved":true, "logged_in":bool, "verified":bool, "uid":0, "screen_name":"", "cookie_file":"", "cookie_rev":"", "message":"" }` |
| GET | `/feed/home` | `since_id`,`fresh_type` | `{ "items":[BlogItem], "since_id":"", "has_more":bool }` |
| GET | `/feed/follow` | `since_id` | 同上（关注流） |
| GET | `/feed/group` | `gid`,`since_id` | 同上（分组流） |
| GET | `/feed/hot` | — | `{ "items":[HotItem] }` |
| GET | `/feed/hot/status` | `word` | `{ "items":[BlogItem], "since_id":"", "has_more":bool }` |
| GET | `/search/status` | `q`,`page` | `{ "items":[BlogItem], "page":1, "has_more":bool }` |
| GET | `/search/user` | `q`,`page` | `{ "items":[UserItem], "page":1, "has_more":bool }` |
| GET | `/search/topic` | `q`,`page` | `{ "items":[TopicItem], "page":1, "has_more":bool }` |
| GET | `/search/history` | — | `{ "items":["词1","词2"] }` |
| POST | `/search/history/clear` | — | `{ "ok":true }` |
| GET | `/status/detail` | `id` | `{ "status":BlogItem }` |
| GET | `/status/comments` | `id`,`page`,`max_id`,`max_id_type` | `{ "items":[CommentItem], "total":0, "max_id":0, "max_id_type":0, "has_more":bool }` |
| GET | `/status/comments/replies` | `id`,`cid`,`page` | `{ "items":[CommentItem], "total":0, "has_more":bool }` |
| GET | `/status/reposts` | `id`,`page` | `{ "items":[BlogItem], "page":1, "has_more":bool }` |
| GET | `/status/likers` | `id`,`page` | `{ "items":[UserItem], "has_more":bool }` |
| POST | `/status/repost` | `id`,`content` | `{ "ok":true, "id":"" }` |
| POST | `/status/comment` | `id`,`content`,`cid`,`also_repost` | `{ "ok":true, "id":"" }` |
| POST | `/status/comment/delete` | `cid` | `{ "ok":true }` |
| POST | `/status/comment/like` | `cid`,`liked` | `{ "ok":true, "liked":bool }` |
| POST | `/status/like` | `id`,`liked` | `{ "ok":true, "liked":bool }` |
| POST | `/status/favorite` | `id`,`fav` | `{ "ok":true, "favorited":bool }` |
| POST | `/status/delete` | `id` | `{ "ok":true }` |
| POST | `/status/publish` | `content`,`visible`,`pic_ids` | `{ "ok":true, "id":"", "bid":"" }` |
| POST | `/status/upload_pic` | `data`(base64),`filename` | `{ "ok":true, "pic_id":"" }` |
| GET | `/status/favorites` | `page` | `{ "items":[BlogItem], "page":1, "has_more":bool }` |
| GET | `/status/mentions` | `page` | `{ "items":[BlogItem], "page":1, "has_more":bool }` |
| GET | `/user/profile` | `uid` | `{ "user":UserItem }` |
| GET | `/user/me` | — | `{ "user":UserItem }` |
| GET | `/user/statuses` | `uid`,`page`,`feature` | `{ "items":[BlogItem], "page":1, "has_more":bool }` |
| GET | `/user/following` | `uid`,`page` | `{ "items":[UserItem], "page":1, "total":0, "has_more":bool }` |
| GET | `/user/followers` | `uid`,`page` | 同上 |
| GET | `/user/search` | `q`,`page` | `{ "items":[UserItem], "page":1, "has_more":bool }` |
| POST | `/user/follow` | `uid`,`follow` | `{ "ok":true, "following":bool }` |
| GET | `/user/groups` | — | `{ "items":[{"gid":"","title":""}] }` |
| GET | `/user/topics` | `page` | `{ "items":[TopicItem], "page":1, "has_more":bool }` |
| GET | `/topic/detail` | `container_id` | `{ "topic":TopicItem }` |
| GET | `/topic/statuses` | `container_id`,`since_id` | `{ "items":[BlogItem], "since_id":"", "has_more":bool }` |
| POST | `/topic/checkin` | `id`,`name` | `{ "ok":true, "message":"", "exp_add":0, "signed":bool }` |
| POST | `/topic/checkin/all` | — | `{ "ok":true, "signed":0, "failed":0, "details":[{"id":"","name":"","ok":true,"message":""}] }` |
| GET | `/topic/search` | `q`,`page` | `{ "items":[TopicItem], "page":1, "has_more":bool }` |
| GET | `/media/info` | `id` | `{ "type":"video|live", "title":"", "cover":"", "url":"", "qualities":[{"label":"","url":""}] }` |
| POST | `/login/import` | `cookie` | `{ "ok":true, "saved":true, "logged_in":bool, "verified":bool, "uid":0, "screen_name":"", "message":"" }` |
| GET | `/login/info` | — | `{ "logged_in":bool, "verified":bool, "uid":0, "screen_name":"", "avatar":"", "expires_at":0 }` |
| POST | `/logout` | — | `{ "ok":true }` |

> **`logged_in` 与 `verified` 的区别（重要）**
>
> * `logged_in` = 本地**持有登录票据**（存在 SUB/SUBP）。离线时也是 `true`，
>   这样界面能照常显示账号，只是请求会失败。
> * `verified` = 上游 `/api/config` **确认过**登录态。Cookie 过期、或当前访问不到
>   微博时为 `false`。
>
> 导入 Cookie 时服务端**先落盘再校验**：只要解析出可用票据就返回 `saved=true`，
> 离线也能导入成功，等联网后由文件监听 / 轮询自动生效。因此判断「是否真的可用」
> 要看 `verified`，不能看 `logged_in`。

### 3.5 `POST /config/import` 的请求体（四种写法都接受）

```jsonc
// 1) cookies.json 原文（推荐，也就是 tools/pen-push.ps1 生成的文件）
{"cookies":[{"name":"SUB","value":"...","domain":".weibo.com","path":"/"}]}

// 2) 裸数组
[{"name":"SUB","value":"..."}]

// 3) 直接给 Cookie 头
{"cookie":"SUB=...; SUBP=..."}

// 4) 纯文本（Content-Type 任意）
SUB=...; SUBP=...
```

开头带 UTF-8 BOM 也能识别（记事本 / PowerShell 5.1 存 UTF-8 会写 BOM，
`encoding/json` 本身遇到 BOM 会报错，服务端会先剥掉）。

---

## 5. 上游接口（m.weibo.cn / weibo.com）

全部集中在 `go_server/main/endpoints.go`。**注意：上游为私有 Web API，字段可能变化；
本项目按以下公开可查的接口实现，若上游调整只需改 `endpoints.go` 与 `parse.go`。**

| 用途 | 上游 |
|------|------|
| 首页推荐流 | `GET https://m.weibo.cn/api/container/getIndex?containerid=102803&since_id=` |
| 关注流 | `GET https://m.weibo.cn/api/container/getIndex?containerid=102803_ctg1_4288_-_ctg1_4288&since_id=` |
| 热搜榜 | `GET https://m.weibo.cn/api/container/getIndex?containerid=106003type%3D25%26t%3D3%26disable_hot%3D1%26filter_type%3Drealtimehot` |
| 热搜微博 | `GET https://m.weibo.cn/api/container/getIndex?containerid=100103type%3D1%26q%3D<word>` |
| 综合搜索 | `GET https://m.weibo.cn/api/container/getIndex?containerid=100103type%3D1%26q%3D<kw>&page_type=searchall` |
| 用户搜索 | `GET https://m.weibo.cn/api/container/getIndex?containerid=100103type%3D3%26q%3D<kw>` |
| 话题搜索 | `GET https://m.weibo.cn/api/container/getIndex?containerid=100103type%3D60%26q%3D<kw>` |
| 微博详情 | `GET https://m.weibo.cn/statuses/show?id=<id>` |
| 热门评论 | `GET https://m.weibo.cn/comments/hotflow?id=<id>&mid=<id>&max_id_type=0` |
| 子评论 | `GET https://m.weibo.cn/comments/hotFlowChild?cid=<cid>&max_id=0&max_id_type=0` |
| 转发列表 | `GET https://m.weibo.cn/api/statuses/repostTimeline?id=<id>&page=<n>` |
| 点赞列表 | `GET https://m.weibo.cn/api/attitudes/attitudesList?id=<id>&page=<n>` |
| 发微博 | `POST https://weibo.com/ajax/statuses/update` |
| 传图 | `POST https://picupload.weibo.com/interface/pic_upload.php?...&data=base64` |
| 转发 | `POST https://m.weibo.cn/api/statuses/repost` |
| 评论 | `POST https://m.weibo.cn/api/comments/create` |
| 回复评论 | `POST https://m.weibo.cn/api/comments/reply` |
| 删评论 | `POST https://m.weibo.cn/api/comments/destroy` |
| 点赞 | `POST https://m.weibo.cn/api/statuses/like` |
| 取消赞 | `POST https://m.weibo.cn/api/statuses/unlike` |
| 评论点赞 | `POST https://m.weibo.cn/api/comments/like` |
| 关注 | `POST https://m.weibo.cn/api/friendships/create` |
| 取关 | `POST https://m.weibo.cn/api/friendships/destroy` |
| 用户资料 | `GET https://weibo.com/ajax/profile/info?uid=<uid>` |
| 用户微博 | `GET https://m.weibo.cn/api/container/getIndex?containerid=107603<uid>&page=<n>` |
| 关注列表 | `GET https://m.weibo.cn/api/container/getIndex?containerid=231093_-_selffollowed&page=<n>` |
| 粉丝列表 | `GET https://m.weibo.cn/api/container/getIndex?containerid=231093_-_followers&page=<n>` |
| 登录态检查 | `GET https://m.weibo.cn/api/config` |
| 我的超话 | `GET https://weibo.com/ajax/profile/topicList?page=<n>` |
| 超话签到 | `GET https://weibo.com/aj/general/button?ajwvr=6&api=http%3A%2F%2Fi.huati.weibo.com%2Faj%2Fsuper%2Fcheckin&id=<topic_id>&status=0` |
| 超话/话题流 | 同 `containerid` 体系（超话 `100808<id>`） |

### 请求头（`client.go` 统一注入）

* `User-Agent`: 移动端 Safari UA（`m.weibo.cn`）
* `Referer`: `https://m.weibo.cn/`；发微博/签到用 `https://weibo.com/`
* `X-Requested-With: XMLHttpRequest`
* `X-XSRF-TOKEN` / `X-CSRF-Token`：从 Cookie 的 `XSRF-TOKEN` 解码得到（`weibo.com` 写操作必需）
* Cookie：来自 `cookies.json` 的持久化 CookieJar

### Cookie 持久化

* 路径：`/userdisk/PenMods/plugins/weibo_plugin/cookies.json`，
  可用环境变量 `WEIBO_COOKIE_FILE` 覆盖；CI/桌面调试回落到 `./cookies.json`。
* 结构：`{ "cookies": [{"name":"SUB","value":"...","domain":".weibo.com"}], "updated_at": 0 }`
* 启动时加载；`/login/import` 导入后立即落盘；`/logout` 清空并落盘。

---

## 6. C++ 层契约

### 6.1 QML 可见类型（`init_plugin()` 中注册）

| 类型 | QML 名 |
|------|--------|
| `WeiboController` | `WeiboController` |
| `BlogListModel` | `BlogListModel` |
| `CommentListModel` | `CommentListModel` |
| `CommentReplyListModel` | `CommentReplyListModel` |
| `HotSearchModel` | `HotSearchModel` |
| `UserListModel` | `UserListModel` |
| `TopicListModel` | `TopicListModel` |
| `PictureListModel` | `PictureListModel` |
| `SearchHistoryModel` | `SearchHistoryModel` |

### 6.2 `WeiboController` 属性

全局：`loggedIn`, `userName`, `userAvatar`, `userId`, `userFollowers`,
`userFollowing`, `userDescription`, `globalError`, `isLoading`

详情快照：`detailId`, `detailText`, `detailTextHtml`, `detailAuthorName`,
`detailAuthorId`, `detailAuthorAvatar`, `detailCreatedAt`, `detailSource`,
`detailPics`(QVariantList), `detailRepostText`, `detailRepostAuthor`,
`detailRepostPics`(QVariantList), `detailRepostsCount`, `detailCommentsCount`,
`detailAttitudesCount`, `detailAttitudesStatus`, `detailFavorited`,
`detailCanDelete`, `detailPageType`, `detailPageTitle`, `detailPageCover`,
`detailPageUrl`, `detailRetweetedId`

媒体：`mediaType`, `mediaTitle`, `mediaCover`, `mediaUrl`, `mediaQualities`

发布：`publishUploading`, `publishProgress`, `publishStatus`

超话：`checkinRunning`, `checkinDone`, `checkinSuccess`, `checkinFailed`

### 6.3 `WeiboController` 子模块（`Q_PROPERTY(QObject*)`）

`feed`, `status`, `comments`, `search`, `profile`, `login`, `publish`, `topic`,
`media`, `viewer`

### 6.35 `WeiboController` 模型访问器（全部 `Q_INVOKABLE`）

QML 通过 `controller.<name>()` 拿列表模型，所以这 22 个 getter **必须**是
`Q_INVOKABLE`（否则运行期报 `Property 'xxxModel' is not a function`，而编译期完全看不出来）：

```
homeModel followModel hotStatusModel groupModel searchStatusModel
userStatusModel topicStatusModel myStatusModel favoriteModel mentionModel
repostModel commentModel commentReplyModel hotSearchModel userSearchModel
followingModel followerModel likerModel topicSearchModel myTopicModel
pictureModel searchHistoryModel
```

返回具体类型（`BlogListModel *` 等），因为它们在 `init_plugin()` 里都用
`qmlRegisterType` 注册过，metatype 对 QML 引擎是已知的。

`tools/verify.py` 的 L 项会扫描 QML 里所有 `controller.<module>.<method>()`
调用并与头文件对账，专门防这一类"编译通过、上机报错"的问题。

### 6.4 模块公开 API（Q_INVOKABLE）

**feed** (`WeiboFeedModule`)
```
fetchHome(sinceId="", freshType=0)   fetchMoreHome()
fetchFollow(sinceId="")              fetchMoreFollow()
fetchHot()                            // 热搜榜
fetchHotStatus(word, sinceId="")      fetchMoreHotStatus()
fetchGroup(gid, sinceId="")           fetchMoreGroup()
popularModel() rankingModel() dynamicModel() upDynamicModel() hotSearchModel()
```
> 为保持与 bili 的 QML 调用习惯一致，模型访问别名保留：
> `popularModel()` → home 流, `rankingModel()` → 热搜微博流,
> `dynamicModel()` → 关注流, `hotSearchModel()` → 热搜榜。

**status** (`WeiboStatusModule`)
```
fetchDetail(id)  restoreCachedDetail(id)  dropCachedDetail(id)
captureCurrentDetail()
fetchReposts(id, page=1) fetchLikers(id, page=1)
like(id, liked)  favorite(id, fav)  repost(id, content)  remove(id)
setActiveId(id)
repostModel() likerModel()
```

**comments** (`WeiboCommentModule`)
```
fetchComments(id, page=1, maxId=0, maxIdType=0)
fetchMoreComments()
fetchReplies(id, cid, page=1)
likeComment(cid, liked)  postComment(id, content, cid="", alsoRepost=false)
deleteComment(cid)  replyCid() replyName() setReplyTo(cid, name) clearReplyTo()
commentModel() replyModel()
```
> `setReplyTo` 的参数顺序是 **(cid, name)**。QML 统一按这个顺序调用。

**search** (`WeiboSearchModule`)
```
search(q, kind="status")   // status | user | topic
searchMore()                       // 按当前 kind 分发
searchUsersMore() searchTopicsMore()   // 各 tab 专用，语义化封装
searchStatus(q, page=1) searchUsers(q, page=1) searchTopics(q, page=1)
fetchHotSearch()
saveHistory(q) clearHistory()
searchModel() userModel() topicModel() hotSearchModel() historyModel()
```

**profile** (`WeiboProfileModule`)
```
fetchMe() fetchProfile(uid) fetchStatuses(uid, page=1, feature=0)
fetchMoreStatuses() fetchFollowing(uid, page=1) fetchFollowers(uid, page=1)
fetchMoreUsers() follow(uid, follow) myStatuses(page=1) myFavorites(page=1)
myMentions(page=1) fetchUserSearch(q, page=1) fetchGroups()
activeUid() setActiveUid(uid) followingActive()
activeUser()   // 见下方说明
```
> **`activeUser()` 返回 `QVariantMap`，不是 `WeiboUser`。**
> `WeiboUser` 没有 `Q_GADGET`，QML 拿不到任何字段，所以这里转成 camelCase 键的 map：
> `uid name avatar cover verified verifiedType verifiedReason description
> followersCount followingCount statusesCount followersText gender location
> followingMe isFollowing isMe`，并额外附 `followers` / `following` / `statuses`
> 三个短别名。页面写 `u.following` 或 `u.followingCount` 都能取到。
> 注意 `fetchGroups()` 只有请求，`groupsModel()` 返回 `nullptr`
> （controller 没有分组模型，本版本不做分组流）。

**login** (`WeiboLoginModule`)
```
importCookie(cookie) checkLogin() logout() fetchConfig()
loggedIn() uid()
startAutoRefresh(intervalMs=4000) stopAutoRefresh() refreshNow() autoRefreshRunning()
```
> **自动导入（电脑端 → 词典笔）**
> 构造函数里就启动了一个 4 秒周期的定时器，轮询 `GET /server/state`
> （纯本地快照，**不请求上游**，所以不会因为高频轮询触发风控）。
> 电脑端通过 adb push 或 `POST /config/import` 把 `cookies.json` 送达后：
> ① sidecar 的文件监听 2 秒内自动加载；② 本模块在下一次轮询感知到状态变化，
> 自动调用 `setLoginUser()` 并发出 `loginAutoRefreshed(screenName)`，
> 界面上**不需要任何操作**。
> 状态机：`!logged_in` → 清登录态；`logged_in && !verified` → 什么都不做
> （有票据但没校验过，不能用空昵称覆盖界面）；`logged_in && verified` → 登录。
> 首次同步不弹提示，只有「真的从未登录变成已登录」才 toast。

**publish** (`WeiboPublishModule`)
```
publish(content, visible=0, picIds=[]) uploadPicture(base64, filename) clearDraft()
saveDraft(content) loadDraft()
```

**topic** (`WeiboTopicModule`)
```
fetchMyTopics(page=1) fetchMoreMyTopics() fetchTopicDetail(containerId)
fetchTopicStatuses(containerId, sinceId="") fetchMoreTopicStatuses()
searchTopics(q, page=1)
checkin(id, name)   checkinAll()   refreshCheckinState()
topicModel() myTopicModel() statusModel()
```

**media** (`WeiboMediaModule`)
```
prepare(id)  // 解析视频/直播直链
```

**viewer** (`WeiboViewerModule`)
```
open(localPath)  // 交给宿主播放器 / 图片查看
```

### 6.5 图片 Provider

`image://weibo/<urlencoded-url>` → 原图，带内存缓存 + 320×170 内缩放。
`image://weibo/original/<urlencoded-url>` → 原图不缩放。
`image://weibo/avatar/<urlencoded-url>` → 圆形裁剪 + `@120w_120h` 后缀。
`image://weibo/size/<w>x<h>/<urlencoded-url>` → 指定尺寸缩放。
下载失败返回占位图（灰底 + WeiboPocket 文字），不返回空。

---

## 7. QML 页面路由（main.qml）

页面名 → 文件：

| currentPage | 文件 | 主要 props |
|-------------|------|-----------|
| `home` | `pages/HomePage.qml` | `controller`, `rootRef` |
| `detail` | `pages/StatusDetailPage.qml` | `controller`, `statusId`, `rootRef` |
| `comments` | `pages/CommentsPage.qml` | `controller`, `statusId`, `contextTitle` |
| `search` | `pages/SearchPage.qml` | `controller`, `rootRef` |
| `hot` | `pages/HotPage.qml` | `controller`, `rootRef` |
| `user` | `pages/UserPage.qml` | `controller`, `uid`, `rootRef` |
| `profile` | `pages/ProfilePage.qml` | `controller`, `rootRef` |
| `publish` | `pages/PublishPage.qml` | `controller` |
| `topic` | `pages/TopicPage.qml` | `controller`, `rootRef` |
| `topicDetail` | `pages/TopicDetailPage.qml` | `controller`, `containerId`, `topicName` |
| `media` | `pages/MediaPage.qml` | `controller`, `statusId` |
| `settings` | `pages/SettingsPage.qml` | `controller` |
| `viewer` | `pages/ImageViewerPage.qml` | `controller`, `pics`, `index` |

导航 API（`main.qml` 暴露给子页面）：
```js
root.navigateTo(page, props)   // 入栈
root.goBack()                  // 出栈；首页时触发 backButtonClicked()
root.rootController            // WeiboController 实例
```

页面通用信号：`onBackClicked`、`onStatusSelected(id)`、`onUserSelected(uid)`。
子页面**不得**直接引用其它页面，跨页跳转一律走 `rootRef.navigateTo(...)`。

---

## 8. 构建

### 8.1 qmake（GitHub Actions 主用，参照 netease-music）

```
QT += core network
CONFIG += shared c++17
TEMPLATE = lib
TARGET = weibo_plugin
DESTDIR = $$PWD/build
```
交叉编译环境由 CI 克隆：`qt-5.15.2-for-aarch64-dictpen-linux`、
`aarch64-dictpen-linux-gnu-gcc-toolchain`、`dictpen-libs`（与 netease 完全一致）。
需要链接：`-lQt5Qml -lQt5Quick -lQt5Gui -lQt5Network -lQt5Core -lGLESv2 -lEGL -lmali`。

> `DESTDIR` 是 `$$PWD/build`（不是 netease 的 `$$PWD/../build`）：本 `.pro` 在仓库
> 根目录，而 netease 的在 `plugin/` 子目录里，CI 也是在仓库根目录执行 qmake。

### 8.2 xmake（本地，参照 bili）

```bash
xmake f -c --qt=/path/to/qt --arch=arm64-v8a --toolchain=zigcc \
        --cross=aarch64-linux-gnu.2.27 -m release
xmake
# 产物 build/linux/arm64-v8a/release/libweibo_plugin.so
```

### 8.3 Go sidecar（产物固定为仓库根目录 `server`）

与 `cc\netease` 的做法一致，本机编译：

```powershell
# Windows（PowerShell 默认 Restricted，必须显式 Bypass）
powershell -NoProfile -ExecutionPolicy Bypass -File build_server.ps1          # linux/arm64
powershell -NoProfile -ExecutionPolicy Bypass -File build_server.ps1 -Local   # 本机调试
```

```bash
# Linux / macOS
./go_server/build.sh                # → 仓库根目录 server（linux/arm64）
GOARCH=amd64 ./go_server/build.sh   # 本机平台
```

等价的裸命令（CI 用这条）：

```bash
cd go_server/main
CGO_ENABLED=0 GOOS=linux GOARCH=arm64 \
  go build -trimpath -ldflags="-s -w" -o ../../server .
```

### 8.4 打包产物（平铺布局，与 netease 的 package.ps1 一致）

zip 根目录直接就是插件根目录：

```text
com.weibopocket.client.zip
├── libweibo_plugin.so
├── server
├── metadata.json
├── icon.png
├── README.md
├── cookies.example.json
└── qml/
```

解压到 `/userdisk/PenMods/plugins/weibo_plugin/`。

> 两个必须遵守的打包约束（都已在 `package.ps1` / `package.sh` 内自检）：
> 1. ZIP 条目名只能用 `/`。Windows PowerShell 5.1 的 `Compress-Archive` 打包
>    **目录**时会写成反斜杠（`qml\main.qml`），Linux 解压后会得到一个反斜杠
>    文件名的文件而不是目录，插件必然加载失败。
> 2. `.ps1` 含中文时必须带 UTF-8 BOM，否则 PowerShell 5.1 按 GBK 解析会报语法错误；
>    `.sh` 则绝不能带 BOM（会破坏 shebang）。`tools/verify.py` 的 Q 项检查这两点。

CI 产出三个 artifact：`libweibo_plugin.so`、`server`、`com.weibopocket.client.zip`。

---

## 9. 编码约定

* C++：`c++17`，4 空格缩进**不强制**，但同一文件内必须一致；类名 `WeiboXxx`；
  头文件用 `#pragma once`；成员变量 `m_` 前缀；信号 `xxxChanged()`。
* QML：`import QtQuick 2.12` + `import WeiboPlugin 1.0`（C++ 类型，由
  `qmlRegisterType` 注册，不需要 qmldir 文件）+ 未限定的 `import "../components"`
  （Theme 单例与 21 个组件）。
* **QML 模块布局**：`qml/` 根目录故意**没有** `qmldir`；`Theme.qml` 放在
  `qml/components/` 下，由 `qml/components/qmldir` 用 `singleton` 声明。
  单例只有在「同目录隐式导入」或「被未限定目录 import 引入」时才可见，
  放根目录会要求每个文件都写 `import ".."`，漏一个就是运行期报错。
* QML 只用 `Loader` + `Component` 做页面保活（与 bili 一致，不用 StackView）。
* QML 中禁止硬编码颜色，一律走 `Theme.*`。
* 所有 `Text` 必须显式设置 `font.pixelSize: Theme.fontXxx` 与
  `font.family: Theme.fontFamily`，否则 320×170 上会溢出。
* 禁止使用 `QtQuick.Controls` / `QtQuick.Layouts` / `QtGraphicalEffects`，
  也不要依赖图标字体或图片资源（设备上都没有）。图标一律用 Unicode 字形或 `Canvas`。
* Go：标准库 only（**不允许第三方依赖**，CI 里 `go mod tidy` 必须无下载）。
* 所有面向用户的字符串用简体中文。
* 改完代码先跑 `python3 tools/verify.py`（CI 的第一个 job 也是它）：
  它校验 metadata ↔ 打包布局、qmldir ↔ 实际文件、`.pro` ↔ 源文件、
  `Q_INVOKABLE`/`Q_PROPERTY` 定义、QML 的 controller 调用 ↔ 头文件、
  `model.<角色>` ↔ `roleNames()`、most vexing parse、lambda 缺 this、
  `.ps1` 的 UTF-8 BOM、Go 只用标准库、路由 ↔ 本 SPEC 对齐、workflow YAML 结构。
  另有 `tools/go_lint.py`（未用 import / 未用局部变量 / 未定义调用）与
  `tools/lambda_this.py`（单独排查 lambda 捕获）。

---

## 10. 电脑端导入 Cookie（自动生效）

词典笔只有 320×170 触摸屏，在笔上粘贴 `SUB`/`SUBP` 又慢又容易错。
目标：**在电脑上完成，笔上零操作**。

```text
电脑                                          词典笔
────                                          ──────
tools\pen-push.ps1
  ├─ 解析 Cookie / cookies.json
  ├─ 生成 cookies.json（UTF-8 无 BOM）
  └─ 送达 ──┬─ adb push ──────────────► /userdisk/PenMods/plugins/weibo_plugin/cookies.json
            │                                   │
            │                          sidecar 文件监听（每 2s stat 一次）
            │                                   ↓ 自动 loadCookieStore + refreshLoginState
            ├─ POST /config/import ────► 同一个文件（服务端自己写）
            │                                   ↓
            └─ 盘符复制 ─────────────────►     C++ WeiboLoginModule 每 4s 轮询 GET /server/state
                                                ↓ 发现 logged_in && verified
                                             setLoginUser() + loginAutoRefreshed 信号
                                                ↓
                                             界面自动显示昵称（无需任何触控）
```

### 10.1 电脑端：`tools/pen-push.ps1`

```powershell
# 1) 最省事：从浏览器复制 Cookie 头
powershell -NoProfile -ExecutionPolicy Bypass -File tools\pen-push.ps1 `
    -Cookie "SUB=xxxxx; SUBP=yyyyy"

# 2) 从文本文件读；3) 已有 cookies.json；4) 走网络；5) 只看计划；6) 反向拉取
... -CookieFile .\cookie.txt
... -Json .\cookies.json
... -Cookie "SUB=..." -Method http -Pen 192.168.1.23
... -Cookie "SUB=..." -DryRun
... -Pull .\cookies-from-pen.json
```

送达方式 `-Method auto|usb|adb|http`，`auto` 按 **盘符 → adb → http** 顺序尝试：

| 方式 | 说明 |
|------|------|
| `usb` | `/userdisk` 以盘符方式挂载时直接复制文件 |
| `adb` | **推荐**。PenManager 用的就是 adb；多设备时会用 `ls /userdisk/PenMods/plugins` 把手机/模拟器排除掉；设备上没有 PenMods 时拒绝推送（除非 `-Force`） |
| `http` | 笔上 sidecar 以 `WEIBO_BIND=0.0.0.0` 启动时 POST `/config/import`（同一局域网） |

脚本会：校验 SUB 存在 / 长度合理 / 不是 `cookies.example.json` 的占位文案 →
生成 `cookies.json`（**UTF-8 无 BOM**：Go 的 `encoding/json` 遇 BOM 会报
`invalid character 'ï'`）→ 送达 → 用 `/server/state` 或响应里的 `verified` 报告是否真的生效。

### 10.2 词典笔端：让导入自动生效

| 机制 | 位置 | 作用 |
|------|------|------|
| 文件监听 | `go_server/main/watch.go` `startCookieWatcher` | 每 2 秒 stat 一次 `cookies.json`，mtime/size 变了就自动重新加载；服务端自己写文件时会同步指纹，避免重复处理 |
| 本地快照 | `GET /server/state` | **只读缓存、不请求上游**，供 C++ 高频轮询（用 `/config` 会每 4 秒打一次 m.weibo.cn，容易风控） |
| 状态轮询 | `WeiboLoginModule::pollLocalState` | 每 4 秒读 `/server/state`；`logged_in && verified` 时自动 `setLoginUser()` 并发 `loginAutoRefreshed(name)` |
| 网络开关 | 环境变量 `WEIBO_BIND` | 默认只监听 `127.0.0.1`；设成 `0.0.0.0` 时启动横幅会打印局域网地址，供电脑端 POST |

> 想让电脑端走 HTTP，需要在设备上以 `WEIBO_BIND=0.0.0.0` 启动 sidecar。
> 插件默认拉起的是 `127.0.0.1`（更安全）；用 adb push 则完全不需要改监听地址。
