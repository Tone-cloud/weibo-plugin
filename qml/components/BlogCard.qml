import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl
import "../js/TimeText.js" as TimeText

// 微博卡片（信息流 / 搜索 / 用户页 / 超话流 / 转发列表共用）。
//
// blog 是普通 JS 对象，键名 = BlogListModel 角色名，且可能是 undefined / 缺键，
// 因此所有字段一律走 _s()/_n()/_a() 防御式读取。
//
// 布局（compact: false，纵向，总高约 78–110）：
//   头部 Avatar(20) + 昵称 + 认证点 + 时间 + 来源
//   正文 RichTextLabel（最多 4 行）
//   图片九宫格（1 张 96×64；2 张 46×46 并排；3 张 30×46；≥4 张 46×46 的 2×2 + "+N"）
//   转发块（灰底圆角：原昵称 + 最多 2 行 + 40×30 缩略）
//   媒体块（视频 88×50 封面 + ▶ + 时长；直播 封面 + ● + "直播中"）
//   操作行（转发 ⇄ / 评论 ✉ / 赞 ♥♡ / 收藏 ★☆ + 计数字，fontTiny）
//
// 布局（compact: true，横向，固定高 62）：左侧 88×52 缩略图 + 右侧 3 行文本。
//
// 图片索引约定：imageClicked(index) 的 index 是"合并后的图片列表"下标 ——
// 先外层 pics，再 retweetedPics 偏移 picCount。调用方按同样顺序拼列表给查看器，
// 就能一次点开正确的那张。
Item {
    id: card
    width: parent ? parent.width : 310

    property var blog: null
    property bool compact: false
    // compact 态强制不显示操作行
    property bool showActions: true
    property bool selected: false

    signal clicked()
    signal authorClicked(var uid)
    signal likeClicked()
    signal commentClicked()
    signal repostClicked()
    signal favoriteClicked()
    signal mediaClicked()
    signal imageClicked(int index)
    signal topicClicked(string name)
    signal linkClicked(string url)
    signal retweetClicked(var id)

    // 作者区域按下标记：保证点头像/昵称只发 authorClicked，不再发 clicked。
    // 用 0ms Timer 复位，确保复位发生在本次触摸的所有 click 信号之后。
    property bool _authorZonePressed: false

    // ───────────────────────── 防御式取值 ─────────────────────────
    function _s(v) { return (v === undefined || v === null) ? "" : String(v) }
    function _n(v) { var n = Number(v); return isFinite(n) ? n : 0 }
    function _a(v) { return (v && v.length !== undefined) ? v : [] }
    function _key(k) { return card.blog ? card.blog[k] : undefined }

    function _picUrl(p) {
        if (!p) return ""
        if (p.url !== undefined && p.url !== null) return _s(p.url)
        return _s(p)
    }

    function _picCountOf(list) {
        if (list && list.length !== undefined && list.length > 0) return list.length
        return 0
    }

    function _formatCount(text, num) {
        var t = _s(text)
        if (t.length > 0) return t
        return TimeText.count(_n(num))
    }

    // 时间：优先 createdText，其次 createdTs 相对时间，最后 createdAt
    function _createdText() {
        var t = _s(_key("createdText"))
        if (t.length > 0) return t
        var ts = _key("createdTs")
        var rel = TimeText.relative(ts)
        if (rel.length > 0) return rel
        return _s(_key("createdAt"))
    }

    // 时间 + 来源 一行展示
    function _subline() {
        var t = _createdText()
        var src = _s(_key("source"))
        if (src.length > 0) return t.length > 0 ? t + " · " + src : src
        return t
    }

    // 图片列表：优先 pics，退化到 firstPic 单张
    function _pics() {
        var list = _a(_key("pics"))
        if (_picCountOf(list) > 0) return list
        var fp = _s(_key("firstPic"))
        return fp.length > 0 ? [fp] : []
    }

    function _retweetedPics() {
        return _a(_key("retweetedPics"))
    }

    // ───────────────────────── 派生状态 ─────────────────────────
    readonly property bool hasRetweet: _key("hasRetweeted") === true
                                       && (_s(_key("retweetedText")).length > 0
                                           || _picCountOf(_retweetedPics()) > 0)
    readonly property bool isVideo: _s(_key("pageType")) === "video"
    readonly property bool isLive: _s(_key("pageType")) === "live"
    readonly property bool hasMedia: (isVideo || isLive)
                                     && (_s(_key("pageCover")).length > 0
                                         || _s(_key("pageMediaUrl")).length > 0
                                         || _s(_key("hasMedia")).length > 0)
    readonly property bool isLiked: _key("attitudesStatus") === 1
                                    || _key("attitudesStatus") === "1"
    readonly property bool isFavorited: _key("favorited") === true

    // 点赞/收藏在本地乐观更新（页面可另行刷新）
    property bool localLiked: false
    property bool localFavorited: false
    onBlogChanged: {
        card.localLiked = false
        card.localFavorited = false
    }
    readonly property bool likedState: isLiked || localLiked
    readonly property bool favoritedState: isFavorited || localFavorited

    readonly property int picCount: _picCountOf(_pics())
    readonly property int retweetPicCount: _picCountOf(_retweetedPics())
    // 合并图片列表长度：查看器按同一顺序拼列表
    readonly property int combinedPicCount: picCount + retweetPicCount
    readonly property int gridCellSize: 46
    readonly property int gridCount: Math.min(4, picCount)

    // ───────────────────────── 尺寸 ─────────────────────────
    // 展开态用内容列撑高；compact 固定 62。
    height: compact ? 62 : Math.max(78, Math.min(130, contentCol.implicitHeight + Theme.spacingNormal * 2))

    // ───────────────────────── 背景 ─────────────────────────
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: cardArea.pressed ? Theme.withAlpha(Theme.primary, 0.12)
                                : (card.selected ? Theme.bgCardHover : Theme.bgCard)
        border.width: 1
        border.color: card.selected ? Theme.withAlpha(Theme.primary, 0.42) : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.animFast } }

        Rectangle {
            width: parent.width - Theme.spacingLarge
            height: 1
            color: Theme.divider
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
        }
    }

    // ───────────────────────── 展开态（竖向） ─────────────────────────
    Column {
        id: contentCol
        visible: !card.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingNormal
        spacing: Theme.spacingSmall

        // ── 头部 ──
        Item {
            id: headerRow
            width: parent.width
            height: 20

            Avatar {
                id: headAvatar
                avatarSize: 20
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                source: card._s(card._key("authorAvatar"))
                verified: card._key("authorVerified") === true
                verifiedType: card._n(card._key("authorVerifiedType"))
                onClicked: {
                    card._authorZonePressed = true
                    card.authorClicked(card._key("authorId"))
                    authorGuard.restart()
                }
            }

            Text {
                id: nameText
                anchors.left: headAvatar.right
                anchors.leftMargin: Theme.spacingSmall
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, Math.max(30, headerRow.width - 20 - Theme.spacingSmall - timeText.width - Theme.spacingSmall))
                text: card._s(card._key("authorName")).length > 0 ? card._s(card._key("authorName")) : "微博用户"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                font.bold: true
                elide: Text.ElideRight
                maximumLineCount: 1

                MouseArea {
                    id: nameArea
                    anchors.fill: parent
                    anchors.leftMargin: -Theme.spacingTiny
                    anchors.rightMargin: -Theme.spacingTiny
                    onClicked: {
                        card._authorZonePressed = true
                        card.authorClicked(card._key("authorId"))
                        authorGuard.restart()
                    }
                }
            }

            Text {
                id: timeText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 110)
                text: card._subline()
                color: Theme.textTertiary
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
                maximumLineCount: 1
                horizontalAlignment: Text.AlignRight
            }
        }

        // ── 正文（最多 4 行） ──
        RichTextLabel {
            id: bodyText
            width: parent.width
            sourceText: card._s(card._key("text"))
            maximumLines: 4
            fontSize: Theme.fontBody
            onTopicClicked: card.topicClicked(name)
            onLinkClicked: card.linkClicked(url)
            onUserClicked: card.authorClicked(0)
        }

        // ── 图片九宫格 ──
        Item {
            id: gridBox
            visible: card.gridCount > 0
            width: parent.width
            height: {
                if (!visible) return 0
                if (card.gridCount === 1) return 64
                if (card.gridCount <= 3) return card.gridCellSize
                return card.gridCellSize * 2 + Theme.spacingTiny
            }

            // 最多 4 张，分 1 / 2 两行
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                spacing: Theme.spacingTiny

                Repeater {
                    model: card.gridCount > 3 ? 2 : 1

                    Row {
                        spacing: Theme.spacingTiny
                        // 本行首张图在合并列表里的下标（每行最多 2 张）
                        readonly property int lineStart: index * 2
                        readonly property int rowCells: Math.min(2, card.gridCount - lineStart)

                        Repeater {
                            model: parent.rowCells

                            Rectangle {
                                // 单图 96×64；2 张 46×46；3 张 30×46；4 张 2×2 的 46×46
                                readonly property int cellIndex: parent.lineStart + index
                                readonly property int cellW: card.gridCount === 1 ? 96
                                                             : (card.gridCount === 3 ? 30 : card.gridCellSize)
                                readonly property int cellH: card.gridCount === 1 ? 64 : card.gridCellSize

                                width: cellW
                                height: cellH
                                radius: Theme.radiusSmall
                                color: Theme.bgTertiary
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    source: {
                                        var u = card._picUrl(card._pics()[cellIndex])
                                        return u.length > 0
                                               ? ImageUrl.previewSource(u, parent.width * 2, parent.height * 2)
                                               : ""
                                    }
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    smooth: true
                                }

                                // 超出 4 张时在右下角画 +N
                                Rectangle {
                                    visible: card.gridCount === 4 && cellIndex === 3 && card.picCount > 4
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: Theme.withAlpha(Theme.textOnPrimary, 0.0)

                                    Text {
                                        anchors.centerIn: parent
                                        text: "+" + (card.picCount - 4)
                                        color: Theme.textOnPrimary
                                        font.pixelSize: Theme.fontMedium
                                        font.family: Theme.fontFamily
                                        font.bold: true
                                    }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    visible: imgPressed.pressed
                                    color: Theme.withAlpha(Theme.primary, 0.22)
                                }

                                MouseArea {
                                    id: imgPressed
                                    anchors.fill: parent
                                    // index 即内层 Repeater 的下标，等于 cellIndex
                                    onClicked: card.imageClicked(index)
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── 转发块 ──
        Rectangle {
            id: retweetBox
            visible: card.hasRetweet
            width: parent.width
            height: visible ? retweetCol.implicitHeight + Theme.spacingSmall * 2 : 0
            radius: Theme.radiusSmall
            color: Theme.bgTertiary

            Column {
                id: retweetCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: Theme.spacingSmall
                spacing: Theme.spacingTiny

                Text {
                    width: parent.width
                    text: "@" + (card._s(card._key("retweetedAuthorName")).length > 0
                                 ? card._s(card._key("retweetedAuthorName")) : "原作者")
                    color: Theme.textLink
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSmall

                    RichTextLabel {
                        width: parent.width - (retweetThumb.visible ? retweetThumb.width + Theme.spacingSmall : 0)
                        sourceText: card._s(card._key("retweetedText"))
                        maximumLines: 2
                        fontSize: Theme.fontSmall
                        color: Theme.textSecondary
                        onTopicClicked: card.topicClicked(name)
                        onLinkClicked: card.linkClicked(url)
                        onUserClicked: card.authorClicked(0)
                    }

                    // 原博有图时给个 40×30 缩略
                    Rectangle {
                        id: retweetThumb
                        visible: card.retweetPicCount > 0
                        width: 40
                        height: 30
                        radius: Theme.radiusTiny
                        color: Theme.bgSecondary
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: {
                                var u = card._picUrl(card._retweetedPics()[0])
                                return u.length > 0 ? ImageUrl.previewSource(u, 80, 60) : ""
                            }
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }

                        Rectangle {
                            anchors.fill: parent
                            visible: rtThumbArea.pressed
                            color: Theme.withAlpha(Theme.primary, 0.22)
                        }

                        MouseArea {
                            id: rtThumbArea
                            anchors.fill: parent
                            // 原博图片在合并列表里偏移 picCount
                            onClicked: card.imageClicked(card.picCount)
                        }
                    }
                }
            }

            MouseArea {
                id: retweetArea
                anchors.fill: parent
                z: -1
                onClicked: card.retweetClicked(card._key("retweetedId"))
            }
        }

        // ── 媒体块（视频 / 直播） ──
        Rectangle {
            id: mediaBox
            visible: card.hasMedia
            width: 88
            height: 50
            radius: Theme.radiusSmall
            color: Theme.bgTertiary
            clip: true

            Image {
                anchors.fill: parent
                source: {
                    var u = card._s(card._key("pageCover"))
                    if (u.length === 0) u = card._s(card._key("pageMediaUrl"))
                    return u.length > 0 ? ImageUrl.previewSource(u, 176, 100) : ""
                }
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            // 播放三角 / 直播红点
            Text {
                anchors.centerIn: parent
                visible: card.isVideo
                text: "▶"
                color: Theme.textOnPrimary
                font.pixelSize: Theme.fontHuge
                font.family: Theme.fontFamily
            }

            Row {
                visible: card.isLive
                anchors.centerIn: parent
                spacing: Theme.spacingTiny

                Text {
                    text: "●"
                    color: Theme.accent
                    font.pixelSize: Theme.fontBody
                    font.family: Theme.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "直播中"
                    color: Theme.textOnPrimary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            // 时长
            Rectangle {
                visible: card.isVideo && card._s(card._key("pageDuration")).length > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingTiny
                width: durationLabel.implicitWidth + Theme.spacingSmall
                height: 12
                radius: Theme.radiusTiny
                color: Theme.bgOverlay

                Text {
                    id: durationLabel
                    anchors.centerIn: parent
                    text: card._s(card._key("pageDuration"))
                    color: Theme.textOnPrimary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    font.bold: true
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: mediaArea.pressed
                color: Theme.withAlpha(Theme.primary, 0.2)
            }

            MouseArea {
                id: mediaArea
                anchors.fill: parent
                onClicked: card.mediaClicked()
            }
        }

        // ── 操作行（compact 态强制隐藏）──
        Item {
            id: actionRow
            width: parent.width
            height: (card.showActions && !card.compact) ? 14 : 0
            visible: card.showActions && !card.compact

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingLarge

                // 转发
                Row {
                    spacing: Theme.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "⇄"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: card._formatCount(card._key("repostsCountText"), card._key("repostsCount"))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSmall
                        onClicked: card.repostClicked()
                    }
                }

                // 评论
                Row {
                    spacing: Theme.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: "✉"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: card._formatCount(card._key("commentsCountText"), card._key("commentsCount"))
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSmall
                        onClicked: card.commentClicked()
                    }
                }

                // 赞
                Row {
                    spacing: Theme.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: card.likedState ? "♥" : "♡"
                        color: card.likedState ? Theme.accent : Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: card._formatCount(card._key("attitudesCountText"), card._key("attitudesCount"))
                        color: card.likedState ? Theme.accent : Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSmall
                        onClicked: {
                            card.localLiked = !card.likedState
                            card.likeClicked()
                        }
                    }
                }

                // 收藏
                Row {
                    spacing: Theme.spacingTiny
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: card.favoritedState ? "★" : "☆"
                        color: card.favoritedState ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingSmall
                        onClicked: {
                            card.localFavorited = !card.favoritedState
                            card.favoriteClicked()
                        }
                    }
                }
            }
        }
    }

    // ───────────────────────── 紧凑态（横向，固定 62） ─────────────────────────
    Row {
        id: compactRow
        visible: card.compact
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingSmall
        spacing: Theme.spacingNormal

        // 缩略图：firstPic → retweetedFirstPic → pageCover
        Rectangle {
            id: compactThumb
            width: 88
            height: 52
            radius: Theme.radiusSmall
            color: Theme.bgTertiary
            clip: true
            anchors.verticalCenter: parent.verticalCenter

            // 无图时显示 ▣ 占位
            readonly property string thumbUrl: {
                var u = card._s(card._key("firstPic"))
                if (u.length === 0) u = card._s(card._key("retweetedFirstPic"))
                if (u.length === 0) u = card._s(card._key("pageCover"))
                return u
            }

            Image {
                anchors.fill: parent
                source: compactThumb.thumbUrl.length > 0
                        ? ImageUrl.previewSource(compactThumb.thumbUrl, 176, 104) : ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                smooth: true
            }

            Text {
                anchors.centerIn: parent
                visible: compactThumb.thumbUrl.length === 0
                text: "▣"
                color: Theme.textTertiary
                font.pixelSize: Theme.fontLarge
                font.family: Theme.fontFamily
            }

            // 视频/直播角标
            Rectangle {
                visible: card.isVideo || card.isLive
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Theme.spacingTiny
                width: compactMediaLabel.implicitWidth + Theme.spacingSmall
                height: 12
                radius: Theme.radiusTiny
                color: Theme.withAlpha(card.isLive ? Theme.accent : Theme.primary, 0.88)

                Text {
                    id: compactMediaLabel
                    anchors.centerIn: parent
                    text: card.isLive ? "直播" : "视频"
                    color: Theme.textOnPrimary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    font.bold: true
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: compactThumbArea.pressed
                color: Theme.withAlpha(Theme.primary, 0.22)
            }

            MouseArea {
                id: compactThumbArea
                anchors.fill: parent
                onClicked: card.imageClicked(0)
            }
        }

        // 右侧 3 行：标题 2 行 + "作者 · 时间"
        Column {
            id: compactCol
            width: parent.width - compactThumb.width - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTiny

            Text {
                width: parent.width
                text: {
                    var t = card._s(card._key("text"))
                    if (t.length === 0) t = card._s(card._key("retweetedText"))
                    if (t.length === 0) t = card._s(card._key("pageTitle"))
                    return t.length > 0 ? t : card._s(card._key("authorName"))
                }
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                text: {
                    var n = card._s(card._key("authorName"))
                    var t = card._createdText()
                    if (n.length === 0) return t
                    return t.length > 0 ? n + " · " + t : n
                }
                color: Theme.textTertiary
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }
    }

    // ───────────────────────── 整卡点击层（最底层） ─────────────────────────
    MouseArea {
        id: cardArea
        anchors.fill: parent
        z: -2
        onClicked: {
            // 作者区域/图片/媒体/操作行已各自处理，这里只发整卡点击
            if (card._authorZonePressed) return
            card.clicked()
        }
    }

    // 延迟复位作者区标记（0ms，排在本次 click 结算之后）
    Timer {
        id: authorGuard
        interval: 0
        repeat: false
        onTriggered: card._authorZonePressed = false
    }

    // 点击缩放反馈
    scale: cardArea.pressed ? 0.99 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.animFast } }
}
