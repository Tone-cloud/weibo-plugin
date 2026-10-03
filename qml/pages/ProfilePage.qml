import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/TimeText.js" as TimeText
import "../components"

// 我的：未登录时提示导入 Cookie；已登录时展示资料 + 快捷入口 + 内嵌列表。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null
    property var rootRef: null

    // "" / "statuses" / "favorites" / "mentions"：决定内嵌列表展示哪个模型
    property string listMode: ""
    property bool logoutConfirmVisible: false

    signal backClicked()
    signal statusSelected(var id)
    signal userSelected(var uid)
    signal topicRequested()
    signal loginRequested()
    signal settingsRequested()
    signal imageRequested(var pics, int index)

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function profileOf() {
        var c = ctl()
        return (c && c.profile) ? c.profile : null
    }

    function isLoggedIn() {
        var c = ctl()
        return c ? c.loggedIn === true : false
    }

    // 模型访问器：兼容 C++ 侧 getter 未标记 Q_INVOKABLE 的情况
    function modelOf(name) {
        var c = ctl()
        if (!c || typeof c[name] !== "function") return null
        return c[name]()
    }

    function statText(v) {
        return TimeText.count(Number(v || 0))
    }

    function statsLine() {
        var c = ctl()
        if (!c) return "关注 0 · 粉丝 0 · 微博 0"
        return "关注 " + root.statText(c.userFollowing)
              + " · 粉丝 " + root.statText(c.userFollowers)
              + " · 微博 " + root.statText(c.userStatusesCount)
    }

    function currentModel() {
        if (listMode === "statuses") return root.modelOf("myStatusModel")
        if (listMode === "favorites") return root.modelOf("favoriteModel")
        if (listMode === "mentions") return root.modelOf("mentionModel")
        return null
    }

    function currentModelEmpty() {
        var m = root.currentModel()
        return !m || m.count <= 0
    }

    function currentLoading() {
        var m = root.currentModel()
        return m ? m.loading === true : false
    }

    function currentError() {
        var m = root.currentModel()
        if (!m) return ""
        return m.errorMessage ? m.errorMessage : ""
    }

    function currentEmptyText() {
        if (listMode === "favorites") return "还没有收藏"
        if (listMode === "mentions") return "还没有 @ 我的微博"
        return "还没有发过微博"
    }

    function currentHasMore() {
        var m = root.currentModel()
        return m ? m.hasMore === true : false
    }

    // 入口按钮：只在切换模式时重新请求，避免重复拉取
    function switchMode(mode) {
        if (listMode === mode) {
            listMode = ""
            return
        }
        listMode = mode
        var p = root.profileOf()
        if (!p) return
        if (mode === "statuses") {
            if (root.currentModelEmpty()) p.myStatuses(1)
        } else if (mode === "favorites") {
            if (root.currentModelEmpty()) p.myFavorites(1)
        } else if (mode === "mentions") {
            if (root.currentModelEmpty()) p.myMentions(1)
        }
    }

    function doLogout() {
        var c = ctl()
        if (c && c.login) c.login.logout()
    }

    // 供 main.qml 记录 / 恢复滚动位置
    function contentYValue() {
        return bodyFlick ? bodyFlick.contentY : 0
    }

    function restoreContentY(y) {
        if (!bodyFlick) return
        Qt.callLater(function() {
            bodyFlick.contentY = Math.max(0, y)
            Qt.callLater(function() { bodyFlick.contentY = Math.max(0, y) })
        })
    }

    // 退出登录后回到未登录视图
    Connections {
        target: controller ? controller.login : null
        function onLoggedOut() {
            root.listMode = ""
            root.logoutConfirmVisible = false
        }
    }

    TitleBar {
        id: titleBar
        title: "我的"
        showBack: true
        showAction: true
        actionText: "⚙"
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
        onActionClicked: root.settingsRequested()
    }

    // ====== 未登录 ======
    Item {
        id: loggedOutArea
        visible: !root.isLoggedIn()
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        EmptyState {
            anchors.fill: parent
            text: "未登录"
            hint: "导入微博 Cookie 后即可查看"
            glyph: "◎"
        }

        Rectangle {
            id: loginButton
            width: 96
            height: Theme.buttonHeight
            radius: Theme.radiusRound
            color: loginArea.pressed ? Theme.primaryDark : Theme.primary
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingNormal

            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Text {
                anchors.centerIn: parent
                text: "登录"
                color: Theme.textOnPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.bold: true
            }

            MouseArea {
                id: loginArea
                anchors.fill: parent
                onClicked: root.loginRequested()
            }
        }
    }

    // ====== 已登录 ======
    Flickable {
        id: bodyFlick
        visible: root.isLoggedIn()
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: Math.max(height, bodyColumn.childrenRect.height + Theme.spacingMedium)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: bodyColumn
            width: parent.width
            spacing: Theme.spacingSmall
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall

            // ── 资料头部 ──
            Item {
                id: profileHeader
                width: parent.width
                height: 46

                Avatar {
                    id: meAvatar
                    avatarSize: 40
                    source: root.ctl() ? root.ctl().userAvatar : ""
                    verified: root.ctl() ? root.ctl().userVerified === true : false
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingNormal
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingTiny
                }

                Text {
                    id: meNameText
                    anchors.left: meAvatar.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingNormal
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingTiny
                    height: 14
                    text: (root.ctl() && root.ctl().userName) ? root.ctl().userName : "微博用户"
                    color: Theme.textPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontNormal
                    font.bold: true
                    elide: Text.ElideRight
                }

                Text {
                    anchors.left: meAvatar.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingNormal
                    anchors.top: meNameText.bottom
                    anchors.topMargin: Theme.spacingTiny
                    height: 18
                    text: (root.ctl() && root.ctl().userDescription) ? root.ctl().userDescription : "这个人很懒，什么都没写"
                    color: Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    clip: true
                }

                Text {
                    anchors.left: meAvatar.right
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingNormal
                    anchors.bottom: parent.bottom
                    text: root.statsLine()
                    color: Theme.textTertiary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTiny
                    elide: Text.ElideRight
                }
            }

            // ── 入口按钮：2 列 × 3 行，每格 150×28 ──
            Column {
                id: entryGrid
                width: parent.width
                spacing: Theme.spacingSmall

                Repeater {
                    model: [
                        [{ label: "我的微博", action: "statuses" }, { label: "我的收藏", action: "favorites" }],
                        [{ label: "@我的", action: "mentions" }, { label: "我的超话", action: "topic" }],
                        [{ label: "发布微博", action: "publish" }, { label: "退出登录", action: "logout" }]
                    ]

                    delegate: Row {
                        width: entryGrid.width
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: modelData

                            delegate: Rectangle {
                                id: entryButton
                                width: 150
                                height: 28
                                radius: Theme.radiusMedium
                                color: entryArea.pressed
                                       ? Theme.bgCardHover
                                       : (root.listMode === modelData.action
                                          ? Theme.withAlpha(Theme.primary, 0.22) : Theme.bgSecondary)
                                border.width: 1
                                border.color: root.listMode === modelData.action
                                              ? Theme.primary : Theme.withAlpha(Theme.border, 0.8)

                                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                Text {
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: modelData.action === "logout" ? Theme.error : Theme.textPrimary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontBody
                                    font.bold: true
                                }

                                MouseArea {
                                    id: entryArea
                                    anchors.fill: parent
                                    onClicked: {
                                        var action = modelData.action
                                        if (action === "statuses" || action === "favorites" || action === "mentions") {
                                            root.switchMode(action)
                                        } else if (action === "topic") {
                                            root.topicRequested()
                                        } else if (action === "publish") {
                                            if (root.rootRef) root.rootRef.navigateTo("publish")
                                        } else if (action === "logout") {
                                            root.logoutConfirmVisible = true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── 内嵌列表：我的微博 / 我的收藏 / @我的 ──
            Item {
                id: inlineListWrap
                width: parent.width
                visible: root.currentModel() !== null
                height: visible ? 96 : 0

                LoadMoreListView {
                    id: inlineList
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingTiny
                    anchors.rightMargin: Theme.spacingTiny
                    model: root.currentModel()
                    orientation: ListView.Vertical
                    spacing: Theme.spacingSmall
                    emptyText: root.currentEmptyText()
                    emptyHint: ""
                    // ready 为 true 时组件自己显示空态；出错时关掉它
                    ready: root.currentError() === ""
                    hasMore: root.currentHasMore()
                    loadingMore: root.currentLoading()

                    delegate: BlogCard {
                        compact: true
                        blog: ({
                            id: model.id, bid: model.bid, text: model.text, textHtml: model.textHtml,
                            createdAt: model.createdAt, createdText: model.createdText, source: model.source,
                            regionName: model.regionName, authorId: model.authorId, authorName: model.authorName,
                            authorAvatar: model.authorAvatar, authorVerified: model.authorVerified,
                            authorVerifiedType: model.authorVerifiedType,
                            authorVerifiedReason: model.authorVerifiedReason,
                            pics: model.pics, picCount: model.picCount, firstPic: model.firstPic,
                            pageType: model.pageType, pageTitle: model.pageTitle, pageCover: model.pageCover,
                            pageUrl: model.pageUrl, pageMediaUrl: model.pageMediaUrl,
                            pageDuration: model.pageDuration, pageLiveStatus: model.pageLiveStatus,
                            hasMedia: model.hasMedia,
                            hasRetweeted: model.hasRetweeted, retweetedAuthorName: model.retweetedAuthorName,
                            retweetedText: model.retweetedText, retweetedPics: model.retweetedPics,
                            retweetedPicCount: model.retweetedPicCount,
                            retweetedFirstPic: model.retweetedFirstPic,
                            retweetedPageType: model.retweetedPageType,
                            retweetedPageTitle: model.retweetedPageTitle,
                            retweetedPageCover: model.retweetedPageCover,
                            repostsCount: model.repostsCount, commentsCount: model.commentsCount,
                            attitudesCount: model.attitudesCount, attitudesStatus: model.attitudesStatus,
                            favorited: model.favorited, canDelete: model.canDelete,
                            repostsCountText: model.repostsCountText,
                            commentsCountText: model.commentsCountText,
                            attitudesCountText: model.attitudesCountText
                        })
                        onClicked: root.statusSelected(model.id)
                        onAuthorClicked: root.userSelected(model.authorId)
                        onImageClicked: root.imageRequested(model.pics, index)
                    }

                    onLoadMore: {
                        var p = root.profileOf()
                        if (!p) return
                        // profile 只暴露通用的 fetchMoreStatuses，收藏 / 提到我的分页由 C++ 侧按当前模式处理
                        if (typeof p.fetchMoreStatuses === "function") p.fetchMoreStatuses()
                    }
                }

                LoadingIndicator {
                    anchors.centerIn: parent
                    visible: root.currentModelEmpty() && root.currentLoading()
                    running: visible
                    indicatorSize: 18
                    text: "加载中…"
                }
            }
        }
    }

    // 退出登录二次确认
    ConfirmPopup {
        visible: root.logoutConfirmVisible
        title: "退出登录"
        message: "退出后需要重新导入 Cookie 才能使用微博功能"
        confirmText: "退出"
        cancelText: "取消"
        danger: true
        onConfirmed: {
            root.logoutConfirmVisible = false
            root.doLogout()
        }
        onCancelled: root.logoutConfirmVisible = false
    }
}
