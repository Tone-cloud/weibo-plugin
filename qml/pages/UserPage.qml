import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/TimeText.js" as TimeText
import "../components"

// 用户主页：资料头部 + 微博 / 关注 / 粉丝 三个分页列表。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null

    // 目标用户 uid（可能是数字或字符串）
    property var uid: 0
    property var rootRef: null

    // 0 微博 / 1 关注 / 2 粉丝
    property int tabIndex: 0
    // 已发起过首屏请求的 uid，避免重复请求
    property string requestedUid: ""
    property var activeUser: ({})

    signal backClicked()
    signal statusSelected(var id)
    signal imageRequested(var pics, int index)
    signal mediaRequested(var statusId)

    // 引擎尚未注入 controller 时所有入口都必须判空
    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    // 模型访问器：兼容 C++ 侧 getter 未标记 Q_INVOKABLE 的情况，避免 QML 运行时报错
    function modelOf(name) {
        var c = ctl()
        if (!c || typeof c[name] !== "function") return null
        return c[name]()
    }

    function profileOf() {
        var c = ctl()
        return (c && c.profile) ? c.profile : null
    }

    // activeUser 由 C++ 侧按当前访问的用户填充；不可用时回落到空对象
    function activeUserSafe() {
        var p = root.profileOf()
        if (!p || typeof p.activeUser !== "function") return ({})
        var u = p.activeUser()
        return u ? u : ({})
    }

    // 关注数 / 粉丝数 / 微博数，统一走 TimeText.count 格式化
    function statText(v) {
        return TimeText.count(Number(v || 0))
    }

    function statsLine() {
        var u = root.activeUser
        return "关注 " + root.statText(u.following)
              + " · 粉丝 " + root.statText(u.followers)
              + " · 微博 " + root.statText(u.statusesCount)
    }

    function isMe() {
        return root.activeUser.isMe === true
    }

    function isFollowing() {
        var p = root.profileOf()
        if (!p || typeof p.followingActive !== "function") return false
        return p.followingActive() === true
    }

    function modelEmpty(m) {
        return !m || m.count <= 0
    }

    function activeModel() {
        if (tabIndex === 1) return root.modelOf("followingModel")
        if (tabIndex === 2) return root.modelOf("followerModel")
        return root.modelOf("userStatusModel")
    }

    function isLoadingNow() {
        var m = root.activeModel()
        return m ? m.loading === true : false
    }

    function listCount() {
        var m = root.activeModel()
        return m ? m.count : 0
    }

    function errorText() {
        var m = root.activeModel()
        if (!m) return ""
        return m.errorMessage ? m.errorMessage : ""
    }

    function emptyText() {
        if (tabIndex === 1) return "还没有关注的人"
        if (tabIndex === 2) return "还没有粉丝"
        return "还没有微博"
    }

    function emptyHint() {
        if (tabIndex === 1) return "关注列表为空"
        if (tabIndex === 2) return "粉丝列表为空"
        return ""
    }

    function refreshActiveUser() {
        root.activeUser = root.activeUserSafe()
    }

    // 首屏 / uid 变化时拉取资料 + 第一条微博
    function load() {
        var c = root.ctl()
        var p = root.profileOf()
        var key = String(uid === undefined || uid === null ? "" : uid)
        if (!c || !p || key === "" || key === "0") return
        if (requestedUid === key) return
        requestedUid = key
        root.refreshActiveUser()
        p.fetchProfile(uid)
        p.fetchStatuses(uid, 1, 0)
    }

    // 分页切换：对应模型为空时才重新请求
    function fetchTab(index) {
        var p = root.profileOf()
        if (!p) return
        if (index === 1) {
            if (root.modelEmpty(root.modelOf("followingModel"))) p.fetchFollowing(uid, 1)
        } else if (index === 2) {
            if (root.modelEmpty(root.modelOf("followerModel"))) p.fetchFollowers(uid, 1)
        } else {
            if (root.modelEmpty(root.modelOf("userStatusModel"))) p.fetchStatuses(uid, 1, 0)
        }
    }

    // 当前分页对应的列表
    function activeList() {
        if (tabIndex === 1) return followingList
        if (tabIndex === 2) return followerList
        return statusList
    }

    function scrollToTop() {
        Qt.callLater(function() {
            var list = root.activeList()
            // LoadMoreListView 只暴露 restoreContentY()，contentY 是只读别名
            if (list) list.restoreContentY(0)
        })
    }

    // 供 main.qml 记录 / 恢复列表滚动位置
    function contentYValue() {
        var list = root.activeList()
        return list ? list.contentY : 0
    }

    function restoreContentY(y) {
        var list = root.activeList()
        if (!list) return
        Qt.callLater(function() {
            list.restoreContentY(Math.max(0, y))
            Qt.callLater(function() { list.restoreContentY(Math.max(0, y)) })
        })
    }

    // uid 变化（重新进入他人主页）：清状态并重新拉取
    onUidChanged: {
        requestedUid = ""
        tabIndex = 0
        root.load()
    }

    Component.onCompleted: root.load()

    // 资料 / 关注态变化后刷新头部
    Connections {
        target: controller ? controller.profile : null
        function onProfileChanged(uidValue) { root.refreshActiveUser() }
        function onFollowStateChanged(uidValue, following) { root.refreshActiveUser() }
    }

    TitleBar {
        id: titleBar
        title: "用户主页"
        showBack: true
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
    }

    // ====== 资料头部 ======
    Item {
        id: headerBlock
        height: 54
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Avatar {
            id: headerAvatar
            avatarSize: 40
            source: root.activeUser.avatar ? root.activeUser.avatar : ""
            verified: root.activeUser.verified === true
            verifiedType: root.activeUser.verifiedType !== undefined ? root.activeUser.verifiedType : -1
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall
        }

        // 右侧关注按钮，自己主页隐藏
        Rectangle {
            id: followButton
            visible: !root.isMe()
            width: visible ? followButtonText.implicitWidth + Theme.spacingLarge : 0
            height: Theme.buttonHeight
            radius: Theme.radiusRound
            color: root.isFollowing()
                   ? (followArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                   : (followArea.pressed ? Theme.primaryDark : Theme.primary)
            border.width: root.isFollowing() ? 1 : 0
            border.color: root.isFollowing() ? Theme.withAlpha(Theme.primary, 0.35) : "transparent"
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall

            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Text {
                id: followButtonText
                anchors.centerIn: parent
                text: root.isFollowing() ? "已关注" : "+ 关注"
                color: root.isFollowing() ? Theme.textSecondary : Theme.textOnPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSmall
                font.bold: true
            }

            MouseArea {
                id: followArea
                anchors.fill: parent
                onClicked: {
                    var p = root.profileOf()
                    if (!p) return
                    p.follow(root.uid, !root.isFollowing())
                }
            }
        }

        // 昵称 + 认证 V
        Row {
            id: nameRow
            width: Math.max(0, parent.width - 40 - Theme.spacingNormal - Theme.spacingSmall * 2
                            - (followButton.visible ? followButton.width + Theme.spacingSmall : 0))
            height: 14
            spacing: Theme.spacingTiny
            anchors.left: headerAvatar.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall

            Text {
                id: nameText
                width: Math.max(0, nameRow.width - verifiedBadge.width
                                - (verifiedBadge.visible ? Theme.spacingTiny : 0))
                text: root.activeUser.name ? root.activeUser.name : "微博用户"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontNormal
                font.bold: true
                elide: Text.ElideRight
                anchors.verticalCenter: parent.verticalCenter
            }

            // 认证标记：用 Unicode 字符代替图标字体（设备上没有图标字体）
            Rectangle {
                id: verifiedBadge
                visible: root.activeUser.verified === true
                width: visible ? 12 : 0
                height: 12
                radius: Theme.radiusRound
                color: (root.activeUser.verifiedType === 2 || root.activeUser.verifiedType === 7)
                       ? Theme.verifiedBlue : Theme.verifiedYellow
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    text: "V"
                    color: Theme.textOnPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontTiny
                    font.bold: true
                }
            }
        }

        // 简介：最多 2 行
        Text {
            id: descText
            anchors.left: headerAvatar.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: followButton.left
            anchors.rightMargin: Theme.spacingSmall
            anchors.top: nameRow.bottom
            anchors.topMargin: Theme.spacingTiny
            height: 20
            text: root.activeUser.description ? root.activeUser.description : "这个人很懒，什么都没写"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            maximumLineCount: 2
            elide: Text.ElideRight
            clip: true
        }

        // 关注 / 粉丝 / 微博
        Text {
            id: statsText
            anchors.left: headerAvatar.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingTiny
            text: root.statsLine()
            color: Theme.textTertiary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontTiny
            elide: Text.ElideRight
        }
    }

    // ====== 分页 ======
    TabBar {
        id: tabBar
        tabs: [{ text: "微博" }, { text: "关注" }, { text: "粉丝" }]
        currentIndex: root.tabIndex
        tabHeight: 20
        anchors.top: headerBlock.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        onTabClicked: {
            if (root.tabIndex === index) return
            root.tabIndex = index
            root.fetchTab(index)
            root.scrollToTop()
        }
    }

    // ====== 内容区 ======
    Item {
        id: contentArea
        anchors.top: tabBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        LoadMoreListView {
            id: statusList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            visible: root.tabIndex === 0
            model: root.modelOf("userStatusModel")
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            emptyText: "还没有微博"
            emptyHint: root.emptyHint()
            // ready 为 true 时组件自己显示空态；出错时关掉它，交给页面级 ErrorOverlay
            ready: root.errorText() === ""
            hasMore: statusList.model ? statusList.model.hasMore === true : false
            loadingMore: statusList.model ? statusList.model.loading === true : false

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
                    repostsCountText: model.repostsCountText, commentsCountText: model.commentsCountText,
                    attitudesCountText: model.attitudesCountText
                })
                onClicked: root.statusSelected(model.id)
                onAuthorClicked: root.statusSelected(model.id)
                onImageClicked: root.imageRequested(model.pics, index)
                onMediaClicked: root.mediaRequested(model.id)
            }

            onLoadMore: {
                var p = root.profileOf()
                if (p) p.fetchMoreStatuses()
            }
        }

        LoadMoreListView {
            id: followingList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            visible: root.tabIndex === 1
            model: root.modelOf("followingModel")
            orientation: ListView.Vertical
            spacing: Theme.spacingTiny
            emptyText: "还没有关注的人"
            emptyHint: root.emptyHint()
            ready: root.errorText() === ""
            hasMore: followingList.model ? followingList.model.hasMore === true : false
            loadingMore: followingList.model ? followingList.model.loading === true : false

            delegate: UserRow {
                uid: model.uid
                name: model.name
                avatar: model.avatar
                verified: model.verified
                verifiedType: model.verifiedType
                description: model.description
                followersText: model.followersText
                isFollowing: model.isFollowing
                showFollow: !model.isMe
                onClicked: root.rootRef.navigateTo("user", { uid: uid })
                onFollowClicked: {
                    var p = root.profileOf()
                    if (p) p.follow(uid, follow)
                }
            }

            onLoadMore: {
                var p = root.profileOf()
                if (p) p.fetchMoreUsers()
            }
        }

        LoadMoreListView {
            id: followerList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            visible: root.tabIndex === 2
            model: root.modelOf("followerModel")
            orientation: ListView.Vertical
            spacing: Theme.spacingTiny
            emptyText: "还没有粉丝"
            emptyHint: root.emptyHint()
            ready: root.errorText() === ""
            hasMore: followerList.model ? followerList.model.hasMore === true : false
            loadingMore: followerList.model ? followerList.model.loading === true : false

            delegate: UserRow {
                uid: model.uid
                name: model.name
                avatar: model.avatar
                verified: model.verified
                verifiedType: model.verifiedType
                description: model.description
                followersText: model.followersText
                isFollowing: model.isFollowing
                showFollow: !model.isMe
                onClicked: root.rootRef.navigateTo("user", { uid: uid })
                onFollowClicked: {
                    var p = root.profileOf()
                    if (p) p.follow(uid, follow)
                }
            }

            onLoadMore: {
                var p = root.profileOf()
                if (p) p.fetchMoreUsers()
            }
        }

        // 加载中
        LoadingIndicator {
            anchors.centerIn: parent
            visible: root.listCount() <= 0 && root.isLoadingNow() && root.errorText() === ""
            running: visible
            indicatorSize: 18
            text: "加载中…"
        }

        // 出错 + 重试
        ErrorOverlay {
            anchors.fill: parent
            visible: root.errorText() !== ""
            errorMessage: root.errorText()
            onRetryClicked: {
                root.requestedUid = ""
                root.load()
            }
        }
    }
}
