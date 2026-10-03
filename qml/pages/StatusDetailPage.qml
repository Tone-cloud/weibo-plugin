import QtQuick 2.12
import WeiboPlugin 1.0
import "../components" as Components
import "../js/ImageUrl.js" as ImageUrl
import "../components"

// 微博详情页。
// 布局：TitleBar(28) + 滚动内容 + 底部操作栏(26)，内容区高度 = 142 - 26 = 116。
// 注意：Row / Column 的子项一律只设置 width/height，绝不使用 anchors 或 x/y，
// 需要定位时用 Item 包裹（positioner 与 anchors 混用会导致布局失效）。
Rectangle {
    id: detailPage
    anchors.fill: parent
    color: Theme.bgPrimary

    property var controller: null
    property string statusId: ""
    property var rootRef: null

    // 是否已经发起过详情请求（用于区分「加载中」与「已结束但无数据」）
    property bool requested: false
    property bool repostInputVisible: false
    property bool deleteConfirmVisible: false

    signal backClicked()
    signal commentsRequested(var statusId)
    signal userSelected(var uid)
    signal mediaRequested(var statusId)
    signal topicSelected(var containerId, var name)
    signal imageRequested(var pics, var index)

    function ctl() {
        return (controller && controller.status) ? controller : null
    }

    // ── 状态判定 ──
    function loadingNow() {
        var c = ctl()
        if (!c) return false
        return !c.detailLoaded && c.isLoading
    }

    function missingNow() {
        var c = ctl()
        if (!c) return false
        return !c.detailLoaded && !c.isLoading && requested
    }

    function bodyText() {
        var c = ctl()
        if (!c) return ""
        return c.detailTextHtml !== "" ? c.detailTextHtml : c.detailText
    }

    function liked() {
        var c = ctl()
        return c ? (c.detailAttitudesStatus === 1) : false
    }

    function favorited() {
        var c = ctl()
        return c ? c.detailFavorited : false
    }

    function canDelete() {
        var c = ctl()
        return c ? c.detailCanDelete : false
    }

    function authorName() {
        var c = ctl()
        return c ? c.detailAuthorName : ""
    }

    function authorVerified() {
        var c = ctl()
        return c ? c.detailAuthorVerified : false
    }

    function metaText() {
        var c = ctl()
        if (!c) return ""
        var s = c.detailCreatedAt ? String(c.detailCreatedAt) : ""
        if (c.detailSource) s += "  来自 " + c.detailSource
        return s
    }

    function repostsText() {
        var c = ctl()
        return c ? c.detailRepostsText : "0"
    }

    function commentsText() {
        var c = ctl()
        return c ? c.detailCommentsText : "0"
    }

    function attitudesText() {
        var c = ctl()
        return c ? c.detailAttitudesText : "0"
    }

    function hasRetweeted() {
        var c = ctl()
        return c ? c.detailHasRetweeted : false
    }

    function isMediaPage() {
        var c = ctl()
        if (!c) return false
        return c.detailPageType === "video" || c.detailPageType === "live"
    }

    function isLive() {
        var c = ctl()
        return c ? (c.detailPageType === "live") : false
    }

    function pageTitle() {
        var c = ctl()
        return c ? c.detailPageTitle : ""
    }

    // ── 图片九宫格几何（1 张 260×90 / 2 张 140×80 / 3 张 92×70 / ≥4 张 2×2 140×70） ──
    function picCount() {
        var c = ctl()
        if (!c) return 0
        var p = c.detailPics
        return p ? p.length : 0
    }

    function picWidth(i) {
        var n = picCount()
        if (n <= 1) return 260
        if (n === 2) return 140
        if (n === 3) return 92
        return 140
    }

    function picHeight(i) {
        var n = picCount()
        if (n <= 1) return 90
        if (n === 2) return 80
        if (n === 3) return 70
        return 70
    }

    function picX(i) {
        var n = picCount()
        if (n <= 1) return 4
        if (n === 2) return 4 + i * 144
        if (n === 3) return 4 + i * 96
        return 4 + (i % 2) * 144
    }

    function picY(i) {
        var n = picCount()
        if (n <= 3) return 0
        return Math.floor(i / 2) * 72
    }

    function picGridHeight() {
        var n = picCount()
        if (n <= 0) return 0
        if (n === 1) return 90
        if (n === 2) return 80
        if (n === 3) return 70
        return 142
    }

    function picSource(i) {
        var c = ctl()
        if (!c) return ""
        var p = c.detailPics
        if (!p || i >= p.length || !p[i]) return ""
        var item = p[i]
        var url = item.url ? item.url : (item.large ? item.large : "")
        if (url === "") return ""
        return ImageUrl.sizedSource(url, picWidth(i) * 2, picHeight(i) * 2)
    }

    function retweetedPicCount() {
        var c = ctl()
        if (!c) return 0
        var p = c.detailRetweetedPics
        return p ? p.length : 0
    }

    function retweetedPicSource(i) {
        var c = ctl()
        if (!c) return ""
        var p = c.detailRetweetedPics
        if (!p || i >= p.length || !p[i]) return ""
        var item = p[i]
        var url = item.url ? item.url : (item.large ? item.large : "")
        if (url === "") return ""
        return ImageUrl.sizedSource(url, 96, 64)
    }

    function retweetedAuthorName() {
        var c = ctl()
        return c ? c.detailRetweetedAuthorName : ""
    }

    function retweetedText() {
        var c = ctl()
        if (!c) return ""
        return c.detailRetweetedTextHtml !== "" ? c.detailRetweetedTextHtml : c.detailRetweetedText
    }

    function retweetedCover() {
        var c = ctl()
        return c ? c.detailRetweetedPageCover : ""
    }

    function retweetedCoverSource() {
        var u = retweetedCover()
        if (u === "") return ""
        return ImageUrl.sizedSource(u, 240, 136)
    }

    function mediaCoverSource() {
        var c = ctl()
        if (!c) return ""
        if (c.detailPageCover === "") return ""
        return ImageUrl.sizedSource(c.detailPageCover, 300, 168)
    }

    // ── 交互 ──
    function requestDetail() {
        var c = ctl()
        if (!c) return
        if (!statusId || statusId === "") return
        requested = true
        c.status.fetchDetail(statusId)
    }

    function openAuthor() {
        var c = ctl()
        if (!c) return
        var uid = c.detailAuthorId
        if (uid && Number(uid) > 0) detailPage.userSelected(uid)
    }

    function openComments() {
        var c = ctl()
        if (!c) return
        detailPage.commentsRequested(c.detailId)
    }

    function openMedia() {
        detailPage.mediaRequested(detailPage.statusId)
    }

    function openImage(i) {
        var c = ctl()
        if (!c) return
        var pics = c.detailPics
        if (!pics || pics.length === 0) return
        detailPage.imageRequested(pics, i)
    }

    function openRetweetedImage(i) {
        var c = ctl()
        if (!c) return
        var pics = c.detailRetweetedPics
        if (!pics || pics.length === 0) return
        detailPage.imageRequested(pics, i)
    }

    function openLink(url) {
        if (!url) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openUrl) c.viewer.openUrl(url)
    }

    function toggleLike() {
        var c = ctl()
        if (!c) return
        c.status.like(c.detailId, c.detailAttitudesStatus !== 1)
    }

    function toggleFavorite() {
        var c = ctl()
        if (!c) return
        c.status.favorite(c.detailId, !c.detailFavorited)
    }

    function doRemove() {
        var c = ctl()
        if (!c) return
        c.status.remove(c.detailId)
        detailPage.deleteConfirmVisible = false
        detailPage.backClicked()
    }

    function doRepost() {
        var c = ctl()
        if (!c) return
        var txt = repostInput.text ? String(repostInput.text) : ""
        c.status.repost(c.detailId, txt)
        repostInput.text = ""
        detailPage.repostInputVisible = false
    }

    Components.TitleBar {
        id: titleBar
        title: "微博正文"
        showBack: true
        showAction: true
        actionText: "↻"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: detailPage.backClicked()
        onActionClicked: detailPage.requestDetail()
    }

    // ── 滚动内容 ──
    Flickable {
        id: contentFlick
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: actionBar.top
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height + Theme.spacingMedium
        boundsBehavior: Flickable.DragOverBounds

        Column {
            id: contentColumn
            width: contentFlick.width
            spacing: Theme.spacingSmall

            // ── 作者行 ──
            Item {
                width: parent.width
                height: 40

                Components.Avatar {
                    id: authorAvatarItem
                    avatarSize: 36
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    source: detailPage.ctl() ? detailPage.ctl().detailAuthorAvatar : ""
                    verified: detailPage.authorVerified()
                    onClicked: detailPage.openAuthor()
                }

                Column {
                    anchors.left: authorAvatarItem.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingTiny

                    // 昵称 + 认证标（用 Item 定位，避免 Row 内 anchors）
                    Item {
                        width: parent.width
                        height: 14

                        Text {
                            id: authorNameText
                            x: 0
                            y: 0
                            height: parent.height
                            width: Math.min(implicitWidth, parent.width - 20)
                            text: detailPage.authorName()
                            color: Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            font.family: Theme.fontFamily
                            font.bold: true
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }

                        Rectangle {
                            x: authorNameText.width + Theme.spacingTiny
                            y: 1
                            width: 14
                            height: 12
                            radius: Theme.radiusTiny
                            visible: detailPage.authorVerified()
                            color: Theme.withAlpha(Theme.verifiedYellow, 0.18)
                            border.color: Theme.withAlpha(Theme.verifiedYellow, 0.45)
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "V"
                                color: Theme.verifiedYellow
                                font.pixelSize: Theme.fontTiny
                                font.family: Theme.fontFamily
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: detailPage.metaText()
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        elide: Text.ElideRight
                    }
                }

                MouseArea {
                    anchors.left: authorAvatarItem.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    onClicked: detailPage.openAuthor()
                }
            }

            // ── 正文 ──
            Components.RichTextLabel {
                width: parent.width - Theme.spacingMedium
                sourceText: detailPage.bodyText()
                color: Theme.textPrimary
                fontSize: Theme.fontBody
                maximumLines: 0
                onTopicClicked: detailPage.topicSelected("", name)
                onUserClicked: detailPage.openLink("weibo://user?name=" + encodeURIComponent(name))
                onLinkClicked: detailPage.openLink(url)
            }

            // ── 图片九宫格 ──
            Item {
                id: picGrid
                width: parent.width
                height: detailPage.picGridHeight()
                visible: height > 0

                Repeater {
                    model: Math.min(4, detailPage.picCount())

                    Rectangle {
                        id: picCell
                        x: detailPage.picX(index)
                        y: detailPage.picY(index)
                        width: detailPage.picWidth(index)
                        height: detailPage.picHeight(index)
                        color: Theme.bgTertiary
                        radius: Theme.radiusSmall
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: detailPage.picSource(index)
                            sourceSize: Qt.size(picCell.width * 2, picCell.height * 2)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                        }

                        Rectangle {
                            visible: detailPage.picCount() > 4 && index === 3
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            width: 26
                            height: 13
                            color: Theme.bgOverlay

                            Text {
                                anchors.centerIn: parent
                                text: "+" + (detailPage.picCount() - 4)
                                color: Theme.textOnPrimary
                                font.pixelSize: Theme.fontTiny
                                font.family: Theme.fontFamily
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: detailPage.openImage(index)
                        }
                    }
                }
            }

            // ── 转发块 ──
            Rectangle {
                visible: detailPage.hasRetweeted()
                width: parent.width - Theme.spacingMedium
                height: retweetColumn.height + Theme.spacingSmall * 2
                radius: Theme.radiusMedium
                color: Theme.bgSecondary
                border.color: Theme.border
                border.width: 1

                Column {
                    id: retweetColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingSmall
                    spacing: Theme.spacingTiny

                    Text {
                        width: parent.width
                        text: "@" + detailPage.retweetedAuthorName()
                        color: Theme.textLink
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    Components.RichTextLabel {
                        width: parent.width
                        sourceText: detailPage.retweetedText()
                        color: Theme.textSecondary
                        fontSize: Theme.fontSmall
                        maximumLines: 4
                        onTopicClicked: detailPage.topicSelected("", name)
                        onUserClicked: detailPage.openLink("weibo://user?name=" + encodeURIComponent(name))
                        onLinkClicked: detailPage.openLink(url)
                    }

                    Item {
                        width: parent.width
                        height: detailPage.retweetedPicCount() > 0 ? 32 : 0
                        visible: height > 0

                        Row {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingTiny

                            Repeater {
                                model: Math.min(3, detailPage.retweetedPicCount())

                                Rectangle {
                                    id: rtCell
                                    width: 48
                                    height: 32
                                    color: Theme.bgTertiary
                                    radius: Theme.radiusTiny
                                    clip: true

                                    Image {
                                        anchors.fill: parent
                                        source: detailPage.retweetedPicSource(index)
                                        sourceSize: Qt.size(96, 64)
                                        fillMode: Image.PreserveAspectCrop
                                        asynchronous: true
                                        cache: true
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: detailPage.openRetweetedImage(index)
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: detailPage.retweetedCover() !== ""
                        width: 120
                        height: 68
                        radius: Theme.radiusSmall
                        color: Theme.bgTertiary
                        clip: true

                        Image {
                            anchors.fill: parent
                            source: detailPage.retweetedCoverSource()
                            sourceSize: Qt.size(240, 136)
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                        }
                    }
                }
            }

            // ── 视频 / 直播块 ──
            Item {
                width: parent.width
                height: detailPage.isMediaPage() ? 92 : 0
                visible: detailPage.isMediaPage()

                Rectangle {
                    id: mediaCard
                    x: Theme.spacingSmall
                    y: 0
                    width: 150
                    height: 84
                    radius: Theme.radiusSmall
                    color: Theme.bgTertiary
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: detailPage.mediaCoverSource()
                        sourceSize: Qt.size(300, 168)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        width: 26
                        height: 26
                        radius: 13
                        color: Theme.withAlpha(Theme.bgOverlay, 0.8)

                        Text {
                            anchors.centerIn: parent
                            text: detailPage.isLive() ? "●" : "▶"
                            color: detailPage.isLive() ? Theme.accent : Theme.textOnPrimary
                            font.pixelSize: Theme.fontBody
                            font.family: Theme.fontFamily
                        }
                    }

                    Rectangle {
                        visible: detailPage.isLive()
                        x: 2
                        y: 2
                        width: 42
                        height: 12
                        radius: Theme.radiusTiny
                        color: Theme.withAlpha(Theme.accent, 0.85)

                        Text {
                            anchors.centerIn: parent
                            text: "直播中"
                            color: Theme.textOnPrimary
                            font.pixelSize: Theme.fontTiny
                            font.family: Theme.fontFamily
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: detailPage.openMedia()
                    }
                }

                Text {
                    // mediaCard 右边缘 = 4 + 150 = 154
                    x: 154 + Theme.spacingSmall
                    y: 0
                    width: parent.width - 154 - Theme.spacingSmall * 2
                    height: 40
                    text: detailPage.pageTitle()
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.fontFamily
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }

                Text {
                    x: 154 + Theme.spacingSmall
                    y: 44
                    width: parent.width - 154 - Theme.spacingSmall * 2
                    text: "点击播放"
                    color: Theme.primary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                }
            }

            // ── 数据行 ──
            Item {
                width: parent.width
                height: 14

                Row {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingSmall
                    spacing: Theme.spacingLarge

                    Text {
                        text: "转发 " + detailPage.repostsText()
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "评论 " + detailPage.commentsText()
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "赞 " + detailPage.attitudesText()
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }
            }

            Item { width: 1; height: Theme.spacingSmall }
        }
    }

    // ── 底部操作栏 ──
    Rectangle {
        id: actionBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 26
        color: Theme.bgSecondary
        z: 20

        Rectangle {
            width: parent.width
            height: 1
            anchors.top: parent.top
            color: Theme.divider
        }

        Row {
            anchors.centerIn: parent
            spacing: Theme.spacingTiny

            // 转发
            Rectangle {
                width: 54
                height: 22
                radius: Theme.radiusSmall
                color: repostArea.pressed ? Theme.withAlpha(Theme.primary, 0.18) : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTiny

                    Text {
                        text: "⇄"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "转发"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }

                MouseArea {
                    id: repostArea
                    anchors.fill: parent
                    onClicked: detailPage.repostInputVisible = true
                }
            }

            // 评论
            Rectangle {
                width: 54
                height: 22
                radius: Theme.radiusSmall
                color: commentArea.pressed ? Theme.withAlpha(Theme.primary, 0.18) : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTiny

                    Text {
                        text: "✉"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "评论"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }

                MouseArea {
                    id: commentArea
                    anchors.fill: parent
                    onClicked: detailPage.openComments()
                }
            }

            // 赞（随 detailAttitudesStatus 实时变化）
            Rectangle {
                width: 54
                height: 22
                radius: Theme.radiusSmall
                color: likeArea.pressed ? Theme.withAlpha(Theme.primary, 0.18) : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTiny

                    Text {
                        text: detailPage.liked() ? "♥" : "♡"
                        color: detailPage.liked() ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "赞"
                        color: detailPage.liked() ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }

                MouseArea {
                    id: likeArea
                    anchors.fill: parent
                    onClicked: detailPage.toggleLike()
                }
            }

            // 收藏
            Rectangle {
                width: 54
                height: 22
                radius: Theme.radiusSmall
                color: favArea.pressed ? Theme.withAlpha(Theme.primary, 0.18) : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTiny

                    Text {
                        text: detailPage.favorited() ? "★" : "☆"
                        color: detailPage.favorited() ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "收藏"
                        color: detailPage.favorited() ? Theme.primary : Theme.textSecondary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }

                MouseArea {
                    id: favArea
                    anchors.fill: parent
                    onClicked: detailPage.toggleFavorite()
                }
            }

            // 删除（仅自己的微博）
            Rectangle {
                width: 54
                height: 22
                radius: Theme.radiusSmall
                visible: detailPage.canDelete()
                color: delArea.pressed ? Theme.withAlpha(Theme.error, 0.18) : "transparent"

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.spacingTiny

                    Text {
                        text: "×"
                        color: Theme.error
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }
                    Text {
                        text: "删除"
                        color: Theme.error
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                    }
                }

                MouseArea {
                    id: delArea
                    anchors.fill: parent
                    onClicked: detailPage.deleteConfirmVisible = true
                }
            }
        }
    }

    // ── 加载中 ──
    Components.LoadingIndicator {
        anchors.centerIn: contentFlick
        running: detailPage.loadingNow()
        text: "加载中"
    }

    // ── 已结束但无数据 ──
    Components.EmptyState {
        anchors.centerIn: contentFlick
        visible: detailPage.missingNow()
        text: "微博不存在或已被删除"
        hint: "点右上角 ↻ 重试"
        glyph: "◌"
    }

    // ── 转发输入浮层 ──
    Rectangle {
        id: repostOverlay
        anchors.fill: parent
        visible: detailPage.repostInputVisible
        color: Theme.bgOverlay
        z: 60

        MouseArea {
            anchors.fill: parent
            onClicked: detailPage.repostInputVisible = false
        }

        Rectangle {
            width: parent.width - Theme.spacingXL * 2
            height: 92
            anchors.centerIn: parent
            radius: Theme.radiusMedium
            color: Theme.bgSecondary
            border.color: Theme.borderLight
            border.width: 1

            // 拦截点击，避免穿透到遮罩
            MouseArea { anchors.fill: parent }

            Item {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall

                Text {
                    id: repostTitle
                    x: 0
                    y: 0
                    width: parent.width
                    height: 13
                    text: "转发微博"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    font.family: Theme.fontFamily
                    font.bold: true
                }

                Components.TextAreaInput {
                    id: repostInput
                    x: 0
                    y: repostTitle.height + Theme.spacingTiny
                    width: parent.width
                    height: 34
                    placeholder: "说点什么…"
                    lineHeight: 14
                    showCounter: false
                }

                Rectangle {
                    x: parent.width - 98
                    y: repostInput.y + repostInput.height + Theme.spacingTiny
                    width: 46
                    height: 20
                    radius: Theme.radiusSmall
                    color: cancelRepostArea.pressed ? Theme.bgTertiary : "transparent"
                    border.color: Theme.border
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        text: "取消"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                    }

                    MouseArea {
                        id: cancelRepostArea
                        anchors.fill: parent
                        onClicked: detailPage.repostInputVisible = false
                    }
                }

                Rectangle {
                    x: parent.width - 46
                    y: repostInput.y + repostInput.height + Theme.spacingTiny
                    width: 46
                    height: 20
                    radius: Theme.radiusSmall
                    color: confirmRepostArea.pressed ? Theme.primaryDark : Theme.primary

                    Text {
                        anchors.centerIn: parent
                        text: "确定"
                        color: Theme.textOnPrimary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                        font.bold: true
                    }

                    MouseArea {
                        id: confirmRepostArea
                        anchors.fill: parent
                        onClicked: detailPage.doRepost()
                    }
                }
            }
        }
    }

    // ── 删除确认 ──
    Components.ConfirmPopup {
        visible: detailPage.deleteConfirmVisible
        title: "删除微博"
        message: "删除后不可恢复，确定删除这条微博吗？"
        confirmText: "删除"
        cancelText: "取消"
        danger: true
        onConfirmed: detailPage.doRemove()
        onCancelled: detailPage.deleteConfirmVisible = false
    }

    Component.onCompleted: {
        if (statusId !== "") requestDetail()
    }

    onStatusIdChanged: {
        if (statusId !== "") {
            requested = false
            requestDetail()
        }
    }
}
