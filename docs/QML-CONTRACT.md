# QML 内部契约（组件 API / 页面 props / model 角色名）

> 与 `docs/SPEC.md` 配套。**所有 QML 文件必须严格遵守本文件。**
> 屏幕 320 × 170，横屏，触摸。每处 `Text` 必须写 `font.pixelSize` 与 `font.family: Theme.fontFamily`。

---

## 0. 模块布局与 import 规则（先读这一节）

```text
qml/
├── main.qml            # 宿主按 metadata.json 加载
├── components/
│   ├── qmldir          # singleton Theme 1.0 Theme.qml + 全部组件
│   ├── Theme.qml       # 主题单例（必须在这里，不能在 qml/ 根目录）
│   └── *.qml
├── pages/              # 页面（目录隐式导入，无需 qmldir）
├── js/                 # .pragma library 工具库
└── fonts/              # msyh.ttf（中文字体，必须随包）
```

> `qml/` 根目录**故意没有 `qmldir`**。若在那里再声明一次 Theme 与 21 个组件，
> `import "components"` 与 `import WeiboPlugin 1.0` 会给同一个文件注册两个同名类型，
> 造成歧义。Theme 与组件的可见性**只由 `components/qmldir` 提供**。
> `tools/verify.py` 的 C 项会检查这一点，`--fix` 会自动删除误建的根 qmldir。

**为什么 `Theme.qml` 必须放在 `components/` 下**：QML 单例只有两种可见途径 ——
① 所在目录的 `qmldir` 里用 `singleton` 声明后，靠**同目录隐式导入**生效；
② 被其它目录用**未限定**的目录 import 引入时，读取该目录的 `qmldir`。
把 Theme 放在 `qml/` 根目录会要求每个文件都写 `import ".."`，漏一个就是运行期报错。
本布局与 netease-music 插件在真机上验证过的布局一致。

因此 import 规则是固定的：

| 文件位置 | 必须的 import |
|----------|---------------|
| `components/*.qml` | `import QtQuick 2.12` + `import WeiboPlugin 1.0`（C++ 类型）。**Theme 与同目录的兄弟组件不需要 import**（同目录隐式导入） |
| `pages/*.qml` | `import QtQuick 2.12` + `import WeiboPlugin 1.0` + **未限定**的 `import "../components"`（拿到 Theme 与全部组件名）+ 需要的 `import "../js/Xxx.js" as Xxx` |
| `main.qml` | `import QtQuick 2.12` + `import WeiboPlugin 1.0` + **未限定**的 `import "components"` + `import "pages" as Pages` |

* `import WeiboPlugin 1.0` 由 `qmlRegisterType` 注册，**不需要任何 qmldir 文件**即可解析，
  提供 `WeiboController` 与各列表模型。
* 可以额外再有 `import "../components" as Components`，此时两种写法都可用：
  `TitleBar { }` 与 `Components.TitleBar { }` 都成立。
* `tools/verify.py` 会检查这些 import，`python3 tools/verify.py --fix` 可自动补齐。

---

## 1. `Theme`（singleton，`qml/components/Theme.qml`）

```qml
pragma Singleton
import QtQuick 2.12
Item { /* ... */ }
```

### 颜色

| 属性 | 值 | 用途 |
|------|----|------|
| `primary` | `#FF8200` | 微博橙，主色 |
| `primaryLight` | `#FFA033` | |
| `primaryDark` | `#E06E00` | |
| `primaryGlow` | `#1AFF8200` | |
| `accent` | `#E6162D` | 微博红，强调/未读 |
| `accentDark` | `#C1121F` | |
| `verifiedYellow` | `#FFAC2D` | 黄 V |
| `verifiedBlue` | `#3C8DBC` | 蓝 V |
| `bgPrimary` | `#0D0D0D` | 根背景 |
| `bgSecondary` | `#161616` | |
| `bgTertiary` | `#242424` | |
| `bgCard` | `#1A1A1A` | 卡片 |
| `bgCardHover` | `#262626` | |
| `bgInput` | `#2C2C2C` | |
| `bgOverlay` | `#CC000000` | |
| `textPrimary` | `#F0F0F0` | |
| `textSecondary` | `#A0A0A0` | |
| `textTertiary` | `#666666` | |
| `textOnPrimary` | `#FFFFFF` | |
| `textLink` | `#FFA033` | 链接/@/话题 |
| `textTopic` | `#4A9EFF` | `#话题#` |
| `border` | `#2E2E2E` | |
| `borderLight` | `#3A3A3A` | |
| `divider` | `#1F1F1F` | |
| `error` | `#FF5252` | |
| `success` | `#4CAF50` | |
| `warning` | `#FFA726` | |

### 尺寸 / 字体 / 圆角 / 动画（与 bili 完全一致，QML 里禁止硬编码这些值）

```
fontTiny 7  fontSmall 8  fontBody 9  fontNormal 10  fontMedium 11
fontLarge 13  fontTitle 14  fontHuge 18
spacingTiny 2  spacingSmall 4  spacingNormal 6  spacingMedium 8
spacingLarge 12  spacingXL 16
radiusTiny 2  radiusSmall 4  radiusMedium 6  radiusLarge 10  radiusXL 14
radiusRound 999
cardWidth 105  listCacheBuffer 640  listDisplayMargin 160
touchMinSize 28  buttonHeight 24  buttonHeightLarge 30
titleBarHeight 28
animFast 120  animNormal 200  animSlow 350  animPage 300
fontFamily  // FontLoader 成功则用其 name，否则 "Microsoft YaHei"
```

### 工具函数

`withAlpha(color, a)`、`lighten(color, factor)`、`darken(color, factor)`、`countText(n)`（→"1.2万"）。

---

## 2. `qml/js/` 库（三份，均为 `.pragma library`）

### `ImageUrl.js`
```js
avatarSource(url)            // 圆形头像通道：image://weibo/avatar/<enc>
previewSource(url)           // 信息流缩略图（走 size 通道，按调用方传的 w/h）
sizedSource(url, w, h)       // image://weibo/size/<w>x<h>/<enc>
originalSource(url)          // image://weibo/original/<enc>
rawSource(url)               // image://weibo/<enc>
sinaSized(url, suffix)       // 微博 CDN 尺寸后缀：把已有 @xxx 换成 suffix，如 "@120w_120h"
stripSinaSize(url)           // 去掉 @xxx 后缀，保留 query
```
规则：`data:image/` 与 `image://` 开头的字符串**原样返回**，不得再包一层 provider。
微博图片域名形如 `wx1.sinaimg.cn/large/<id>.jpg` / `ww1.sinaimg.cn/orj360/...`，
尺寸后缀规则是路径末尾追加 `@120w_120h`（不是 b 站的 `@50w_50h`，但同样替换语义）。

### `RichText.js`
```js
DEFAULT_LINK_COLOR
escapeRichText(text)                    // & < > 转义
escapeHtmlAttribute(text)
linkify(text, linkColor)                // 转义 + @用户/#话题#/http 链接化 + 换行→<br>
richText(text, linkColor, topicColor)   // linkify 的增强版：#话题# 用 topicColor
```
链接化规则（顺序敏感，先长后短）：
1. `https?://\S+`
2. `@[\w\u4e00-\u9fa5\-]{1,30}`（@提及）
3. `#([^#\n]{1,40})#`（话题）
`Text { textFormat: Text.RichText; onLinkActivated: ... }` 由调用方处理。
链接的 `href` 约定：
* `@昵称` → `href="weibo://user?name=<enc>"`
* `#话题#` → `href="weibo://topic?name=<enc>"`
* 普通 URL → `href="<原 URL>"`

### `TimeText.js`
```js
relative(ts)     // 秒级时间戳 → "刚刚/5分钟前/3小时前/昨天 12:30/01-01/2024-01-01"
count(n)         // 数字 → "1.2万"
```

---

## 3. 列表模型的角色名（QML delegate 里直接用 `model.<role>`）

**BlogListModel**（信息流 / 搜索结果 / 用户微博 / 超话流 / 转发列表）
```
id bid text textHtml createdAt createdTs createdText source regionName isLongText
authorId authorName authorAvatar authorVerified authorVerifiedType authorVerifiedReason
pics picCount firstPic
pageType pageTitle pageCover pageUrl pageMediaUrl pageDuration pageLiveStatus hasMedia
hasRetweeted retweetedId retweetedAuthorId retweetedAuthorName retweetedAuthorAvatar
retweetedText retweetedTextHtml retweetedPics retweetedPicCount retweetedFirstPic
retweetedPageType retweetedPageTitle retweetedPageCover retweetedPageUrl retweetedPageMediaUrl
repostsCount commentsCount attitudesCount attitudesStatus favorited canDelete
repostsCountText commentsCountText attitudesCountText topicIds
```
`pics` / `retweetedPics` 是 `[{url, large, width, height, index}]`；`topicIds` 是字符串数组。

**CommentListModel / CommentReplyListModel**
```
id text textHtml createdAt createdTs createdText likeCount liked replyCount
userId userName userAvatar userVerified replyTo pics picCount firstPic canDelete
```

**HotSearchModel** `rank word rawHot hotText label url category`

**UserListModel**
```
uid name avatar cover verified verifiedType verifiedReason description
followersCount followingCount statusesCount followersText gender location
followingMe isFollowing isMe
```

**TopicListModel**
```
id containerId name desc cover readCount discussCount fansCount readText discussText
isSuper level exp rank checked signedDays type statusText
```

**PictureListModel** `url large width height index`

**SearchHistoryModel** `word`

---

## 4. 组件 API（`qml/components/`）

> 全部为普通组件（非 singleton）。未列出的属性不得依赖。

| 组件 | 属性 | 信号 / 方法 |
|------|------|-------------|
| `TitleBar` | `title`(string) `subtitle`(string) `showBack`(bool=true) `showAction`(bool=false) `actionText`(string) `actionEnabled`(bool=true) | `backClicked()` `actionClicked()` |
| `LoadingIndicator` | `running`(bool) `text`(string) `indicatorSize`(int=18) | — |
| `ErrorOverlay` | `errorMessage`(string) `visible`(bool) | `retryClicked()` `dismissed()` |
| `Toast` | — | `show(message, duration=2000)` |
| `IconButton` | `glyph`(string) `label`(string) `buttonSize`(int=Theme.touchMinSize) `enabled`(bool) `highlight`(bool) | `clicked()` |
| `Avatar` | `source`(string, 原始 URL) `avatarSize`(int=20) `verified`(bool) `verifiedType`(int=-1) `online`(bool=false) | `clicked()` |
| `TabBar` | `tabs`(var, `[{text, badge}]`) `currentIndex`(int) `tabHeight`(int=24) | `tabClicked(index)` |
| `BlogCard` | `blog`(var, 见下) `compact`(bool=false) `showActions`(bool=true) `selected`(bool=false) | `clicked()` `authorClicked(uid)` `likeClicked()` `commentClicked()` `repostClicked()` `favoriteClicked()` `mediaClicked()` `imageClicked(index)` `topicClicked(name)` `linkClicked(url)` `retweetClicked(id)` |
| `RichTextLabel` | `sourceText`(string) `color`(color) `fontSize`(int) `maximumLines`(int=0) `linkTextColor`(color) `topicTextColor`(color) | `userClicked(name)` `topicClicked(name)` `linkClicked(url)` |
| `UserRow` | `uid`(var) `name` `avatar` `verified`(bool) `verifiedType`(int) `description` `followersText` `isFollowing`(bool) `showFollow`(bool=true) | `clicked(uid)` `followClicked(uid, follow)` |
| `CommentRow` | `cid` `userName` `userAvatar` `userVerified`(bool) `text` `createdText` `likeCount`(int) `liked`(bool) `replyTo` `replyCount`(int) `pics`(var) `canDelete`(bool) `isReply`(bool=false) | `userClicked(uid)` `replyClicked(cid, name)` `likeClicked(cid, liked)` `deleteClicked(cid)` `imageClicked(index)` `repliesClicked(cid)` |
| `HotRow` | `rank`(int) `word` `rawHot`(var) `hotText` `label` `showRank`(bool=true) | `clicked(word)` |
| `TopicRow` | `topicId` `name` `desc` `cover` `readText` `discussText` `isSuper`(bool) `level`(int) `checked`(bool) `signedDays`(int) `statusText` | `clicked(topicId)` `checkinClicked(topicId, name)` |
| `LoadMoreListView` | `model`(var) `spacing`(int=0) `contentMargin`(int=0) `orientation`(enum: `ListView.Horizontal`/`Vertical`) `delegate`(Component) `hasMore`(bool) `loadingMore`(bool) `emptyText`(string) | `loadMore()` `atYBeginning`/`atYEnd`(bool, readonly) `contentYValue()`/`contentXValue()`(方法) `restoreContentX(v)` / `restoreContentY(v)` |
| `SkeletonPill` | `pillWidth`(int) `pillHeight`(int) `radius`(int) | — |
| `PopupStack` | `open`(bool) `items`(var, `[{text, danger}]`) `title`(string) | `picked(index)` `closed()` |
| `VirtualKeyboardInput` | `text`(string) `placeholder`(string) `cjkOnly`(bool=false) | `textEdited(text)` `accepted(text)` |
| `SearchInput` | `text`(string) `placeholder`(string) `showCancel`(bool=true) | `textEdited(text)` `accepted(text)` `cancelClicked()` |
| `TextAreaInput` | `text`(string) `placeholder`(string) `maxLength`(int=2000) `lineHeight`(int=14) `showCounter`(bool=true) | `textEdited(text)` `accepted(text)` |
| `EmptyState` | `text`(string) `hint`(string) `glyph`(string) | `actionClicked()` |
| `ConfirmPopup` | `visible`(bool) `title` `message` `confirmText` `cancelText` `danger`(bool) | `confirmed()` `cancelled()` |

### `BlogCard.blog` 对象形状

`blog` 是一个普通 JS 对象，键名就是上面的 **BlogListModel 角色名**。页面 delegate 里这样构造：

```qml
delegate: BlogCard {
    blog: ({
        id: model.id, bid: model.bid, text: model.text, textHtml: model.textHtml,
        createdAt: model.createdAt, createdText: model.createdText, source: model.source,
        regionName: model.regionName, authorId: model.authorId, authorName: model.authorName,
        authorAvatar: model.authorAvatar, authorVerified: model.authorVerified,
        authorVerifiedType: model.authorVerifiedType,
        pics: model.pics, picCount: model.picCount, firstPic: model.firstPic,
        pageType: model.pageType, pageTitle: model.pageTitle, pageCover: model.pageCover,
        pageUrl: model.pageUrl, pageMediaUrl: model.pageMediaUrl, hasMedia: model.hasMedia,
        hasRetweeted: model.hasRetweeted, retweetedAuthorName: model.retweetedAuthorName,
        retweetedText: model.retweetedText, retweetedPics: model.retweetedPics,
        retweetedPageType: model.retweetedPageType, retweetedPageTitle: model.retweetedPageTitle,
        retweetedPageCover: model.retweetedPageCover,
        repostsCount: model.repostsCount, commentsCount: model.commentsCount,
        attitudesCount: model.attitudesCount, attitudesStatus: model.attitudesStatus,
        favorited: model.favorited, canDelete: model.canDelete,
        repostsCountText: model.repostsCountText, commentsCountText: model.commentsCountText,
        attitudesCountText: model.attitudesCountText
    })
    onStatusClicked: ...
}
```

`BlogCard` 内部必须对每个键做存在性判断（`blog && blog.text ? blog.text : ""`），
不得因为缺键而抛异常。

### `BlogCard` 布局约定（320×170）

* `compact: false`（信息流列表项，纵向）：总高约 78–110，取决于有无图/转发。
  结构：`Avatar(20)` + 昵称（`fontBody`）+ 时间/来源（`fontTiny`, `textTertiary`）
  → 正文 `RichTextLabel`（最多 4 行）
  → 图片九宫格（1 张时 96×64 单图；2 张并排；3 张一行；≥4 张 2×2 + `+N` 角标；
     最多显示 4 张，超出在右下角画 `+N`）
  → 转发块（灰底，昵称 + 最多 2 行正文）
  → 媒体块（视频：封面 96×54 + 播放三角 + 时长；直播：红点 + "直播中"）
  → 操作行：转发 / 评论 / 赞 / 收藏（图标 + `countText`，`fontTiny`）
* `compact: true`（搜索/用户页列表项，横向）：固定高 62，左侧 88×52 缩略图，
  右侧 3 行文本。`showActions` 强制为 `false`。

### 图标

**不要依赖图标字体或图标文件**——设备上没有。所有图标用
`Text { text: "♥" }` 这类 Unicode 字符或 `Canvas` 手绘。约定：

| 语义 | 字符 |
|------|------|
| 返回 | `‹` |
| 转发 | `⇄` |
| 评论 | `💬` → 用 `Text` 显示不可靠，改用 `Canvas` 或字符 `☰`；推荐字符 `✉` |
| 点赞 | `♥` / 未赞 `♡` |
| 收藏 | `★` / 未收藏 `☆` |
| 搜索 | `⌕` |
| 设置 | `⚙` |
| 刷新 | `↻` |
| 更多 | `⋯` |
| 下拉/展开 | `⌄` |
| 图片 | `▣` |
| 视频 | `▶` |
| 直播 | `●` |
| 签到/完成 | `✓` |

---

## 5. 页面 props 与信号（`qml/pages/`）

每个页面根元素必须是 `Rectangle { anchors.fill: parent; color: Theme.bgPrimary }`，
并且必须提供 `controller` 属性（类型 `var`）。

| 页面 | 必填 props | 必须提供的信号 |
|------|-----------|----------------|
| `HomePage` | `controller`, `rootRef` | `backClicked()` `statusSelected(id)` `userSelected(uid)` `searchRequested()` `hotRequested()` `publishRequested()` `profileRequested()` `topicRequested()` |
| `StatusDetailPage` | `controller`, `statusId`(string), `rootRef` | `backClicked()` `commentsRequested(statusId)` `userSelected(uid)` `mediaRequested(statusId)` `topicSelected(containerId, name)` `imageRequested(pics, index)` |
| `CommentsPage` | `controller`, `statusId`(string), `contextTitle`(string) | `backClicked()` `userSelected(uid)` |
| `SearchPage` | `controller`, `rootRef` | `backClicked()` `statusSelected(id)` `userSelected(uid)` `topicSelected(containerId, name)` |
| `HotPage` | `controller`, `rootRef` | `backClicked()` `wordSelected(word)` |
| `UserPage` | `controller`, `uid`(var), `rootRef` | `backClicked()` `statusSelected(id)` `imageRequested(pics, index)` `mediaRequested(statusId)` |
| `ProfilePage` | `controller`, `rootRef` | `backClicked()` `statusSelected(id)` `userSelected(uid)` `topicRequested()` `loginRequested()` `settingsRequested()` `imageRequested(pics, index)` |
| `PublishPage` | `controller` | `backClicked()` `published(id)` |
| `TopicPage` | `controller`, `rootRef` | `backClicked()` `topicSelected(containerId, name)` `statusSelected(id)` `checkinAllRequested()` |
| `TopicDetailPage` | `controller`, `containerId`(string), `topicName`(string), `rootRef` | `backClicked()` `statusSelected(id)` `userSelected(uid)` `checkinRequested(topicId, name)` |
| `MediaPage` | `controller`, `statusId`(string) | `backClicked()` |
| `SettingsPage` | `controller` | `backClicked()` |
| `ImageViewerPage` | `controller`, `pics`(var), `initialIndex`(int) | `backClicked()` |

### 页面通用要求

* 顶部一律放 `TitleBar`（`title` 用页面名，`showBack: true`，`onBackClicked: root.backClicked()`）。
  内容区高度 = `parent.height - Theme.titleBarHeight`。
* **必须**处理 4 种状态：加载中（`LoadingIndicator`）、空（`EmptyState`）、
  出错（页内提示 + 重试）、有数据。
* 所有列表必须使用 `LoadMoreListView`，并绑定模型的
  `count` / `loading` / `hasMore` / `errorMessage`。
* 涉网操作失败必须给出反馈：`controller.toastMessage(...)` 会把消息送到
  `main.qml` 的全局 `Toast`，页面**不要**自己再创建一个 Toast（除非确实需要局部提示）。
* 页面**不得**引用其它页面类型；跨页跳转一律 `rootRef.navigateTo(page, props)`。
* `controller` 可能为 `null`（引擎尚未注入）：所有绑定与函数入口都要判空。
  推荐每个页面写一个 `function ctl() { return (controller && controller.feed) ? controller : null }`。

---

## 6. `main.qml` 契约

`main.qml` 由宿主按 `metadata.json` 的 `main_qml` 加载，根元素是
`Rectangle { width: 320; height: 170; color: Theme.bgPrimary; clip: true }`，
并暴露：

```qml
signal backButtonClicked()          // 首页再返回时触发，宿主据此退出插件
property var rootController: controller
function navigateTo(page, props)    // 入栈并切换
function goBack()                   // 出栈；栈空且在首页时 backButtonClicked()
```

页面名与文件映射见 `docs/SPEC.md` §7。`main.qml` 还负责：

* 创建 `WeiboController { id: controller }`，并在 `Component.onCompleted`
  里调用 `controller.bootstrap()`。
* 全局 `Toast`（`id: globalToast`）连接 `controller.onToastMessage`。
* 全局 `ErrorOverlay` 覆盖层，连接 `controller.globalError`。
* 处理 `controller.onImageRequested(pics, index)` → `navigateTo("viewer", {pics, index})`。
* 处理 `controller.onLoginExpired()` → toast "登录已过期，请重新导入 Cookie" 并留在当前页。
* 页面用 `Loader { active: currentPage === "x" || stackContains("x") }` 保活
  （与 bili 一致，**不要**用 StackView）。
