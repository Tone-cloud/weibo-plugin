import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"
import "../components" as Components
// 首页：推荐 / 关注 双 Tab 信息流。
// 内容区 = 170 - 28(titleBar) = 142，再减去 TabBar 24 → 列表 118。
Rectangle {
    id: homePage
    anchors.fill: parent
    color: Theme.bgPrimary

    property var controller: null
    property var rootRef: null
    property int tabIndex: 0
    // 是否已经发起过首屏请求（用于抑制组件内置空态在加载瞬间闪现）
    property bool requested: false

    signal backClicked()
    signal statusSelected(var id)
    signal userSelected(var uid)
    signal searchRequested()
    signal hotRequested()
    signal publishRequested()
    signal profileRequested()
    signal topicRequested()

    // 当前 Tab 对应的模型（controller/tabIndex 变化时重新求值）
    property var feedModel: {
        var c = ctl()
        if (!c) return null
        return tabIndex === 0 ? c.feed.popularModel() : c.feed.dynamicModel()
    }

    // controller 由 QML 引擎注入，理论上非空，仍全部判空
    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    // 当前 Tab 是否正在首屏加载
    function feedListLoading() {
        return homePage.feedModel ? homePage.feedModel.loading === true : false
    }

    function errorText() {
        return homePage.feedModel ? (homePage.feedModel.errorMessage ? homePage.feedModel.errorMessage : "") : ""
    }

    // 打开图片查看器：优先交给 viewer 模块，失败时回落到根页面路由
    function openImages(pics, index) {
        if (!pics || pics.length === 0) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openImages) {
            c.viewer.openImages(pics, index)
            return
        }
        if (rootRef && rootRef.navigateTo) rootRef.navigateTo("viewer", { pics: pics, index: index })
    }

    function openLink(url) {
        if (!url) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openUrl) {
            c.viewer.openUrl(url)
            return
        }
        if (c && c.toastMessage) c.toastMessage("无法打开该链接")
    }

    // ── 刷新 ──
    function refresh() {
        var c = ctl()
        if (!c) return
        if (tabIndex === 0) c.feed.refreshHome()
        else c.feed.fetchFollow()
        if (feedList) feedList.restoreContentY(0)
    }

    // 滚动位置（供 main.qml 跨页面保活）
    // 注意：LoadMoreListView 的 contentY 是只读别名，读写必须走它的方法。
    function scrollY() {
        return feedList ? feedList.contentYValue() : 0
    }

    function restoreScrollY(v) {
        if (!feedList) return
        var y = Number(v ? v : 0)
        Qt.callLater(function() {
            feedList.restoreContentY(y)
            Qt.callLater(function() { feedList.restoreContentY(y) })
        })
    }

    // bili 风格别名
    function popularContentX() { return 0 }
    function contentYValue() { return scrollY() }
    function restoreScrollOnShow() {
        if (rootRef) restoreScrollY(rootRef.homeScrollY)
    }

    Components.TitleBar {
        id: titleBar
        title: "微博"
        showBack: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: homePage.backClicked()
    }

    // 顶栏右侧入口按钮
    Row {
        id: topActions
        anchors.top: parent.top
        anchors.topMargin: 3
        anchors.right: parent.right
        anchors.rightMargin: 2
        height: 23
        spacing: 0
        z: 20

        Components.IconButton {
            glyph: "⌕"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.searchRequested()
        }
        Components.IconButton {
            glyph: "☰"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.hotRequested()
        }
        Components.IconButton {
            glyph: "✎"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.publishRequested()
        }
        Components.IconButton {
            glyph: "☺"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.profileRequested()
        }
        Components.IconButton {
            glyph: "#"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.topicRequested()
        }
        Components.IconButton {
            glyph: "↻"
            buttonSize: 23
            // IconButton 的宽度下限是 Theme.touchMinSize(28)，顶栏要放 6 个入口，
            // 这里显式收窄到 24，避免与居中标题重叠（标题约 149~171，按钮从 174 起）。
            width: 24
            onClicked: homePage.refresh()
        }
    }

    Components.TabBar {
        id: tabBar
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        tabHeight: 24
        tabs: [{ text: "推荐" }, { text: "关注" }]
        currentIndex: homePage.tabIndex
        onTabClicked: {
            homePage.tabIndex = index
            var c = homePage.ctl()
            if (!c) return
            if (index === 0 && c.feed.popularModel().count === 0) c.feed.fetchHome()
            if (index === 1 && c.feed.dynamicModel().count === 0) c.feed.fetchFollow()
            homePage.requested = true
        }
    }

    Components.LoadMoreListView {
        id: feedList
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        orientation: ListView.Vertical
        spacing: Theme.spacingSmall
        contentMargin: Theme.spacingSmall
        clip: true
        model: homePage.feedModel
        hasMore: model ? model.hasMore : false
        loadingMore: model ? model.loading : false
        // 列表为空且已发起过请求、且不在加载中时才展示内置空态，避免加载瞬间闪空
        ready: homePage.requested && !homePage.feedListLoading()
        emptyText: homePage.tabIndex === 0 ? "暂无推荐微博" : "暂无关注动态"
        emptyHint: "点右上角 ↻ 刷新试试"
        emptyGlyph: "◌"
        onLoadMore: {
            var c = homePage.ctl()
            if (!c) return
            if (homePage.tabIndex === 0) c.feed.fetchMoreHome()
            else c.feed.fetchMoreFollow()
        }

        delegate: Components.BlogCard {
            blog: ({
                id: model.id, bid: model.bid, text: model.text, textHtml: model.textHtml,
                createdAt: model.createdAt, createdTs: model.createdTs, createdText: model.createdText,
                source: model.source, regionName: model.regionName, isLongText: model.isLongText,
                authorId: model.authorId, authorName: model.authorName, authorAvatar: model.authorAvatar,
                authorVerified: model.authorVerified, authorVerifiedType: model.authorVerifiedType,
                authorVerifiedReason: model.authorVerifiedReason,
                pics: model.pics, picCount: model.picCount, firstPic: model.firstPic,
                pageType: model.pageType, pageTitle: model.pageTitle, pageCover: model.pageCover,
                pageUrl: model.pageUrl, pageMediaUrl: model.pageMediaUrl,
                pageDuration: model.pageDuration, pageLiveStatus: model.pageLiveStatus,
                hasMedia: model.hasMedia,
                hasRetweeted: model.hasRetweeted, retweetedId: model.retweetedId,
                retweetedAuthorId: model.retweetedAuthorId,
                retweetedAuthorName: model.retweetedAuthorName,
                retweetedAuthorAvatar: model.retweetedAuthorAvatar,
                retweetedText: model.retweetedText, retweetedTextHtml: model.retweetedTextHtml,
                retweetedPics: model.retweetedPics, retweetedPicCount: model.retweetedPicCount,
                retweetedFirstPic: model.retweetedFirstPic,
                retweetedPageType: model.retweetedPageType,
                retweetedPageTitle: model.retweetedPageTitle,
                retweetedPageCover: model.retweetedPageCover,
                retweetedPageUrl: model.retweetedPageUrl,
                retweetedPageMediaUrl: model.retweetedPageMediaUrl,
                repostsCount: model.repostsCount, commentsCount: model.commentsCount,
                attitudesCount: model.attitudesCount, attitudesStatus: model.attitudesStatus,
                favorited: model.favorited, canDelete: model.canDelete,
                repostsCountText: model.repostsCountText,
                commentsCountText: model.commentsCountText,
                attitudesCountText: model.attitudesCountText, topicIds: model.topicIds
            })

            onClicked: homePage.statusSelected(model.id)
            onAuthorClicked: homePage.userSelected(uid)
            onLikeClicked: {
                var c = homePage.ctl()
                if (c) c.status.like(model.id, model.attitudesStatus !== 1)
            }
            onCommentClicked: homePage.statusSelected(model.id)
            onRepostClicked: homePage.statusSelected(model.id)
            onFavoriteClicked: {
                var c = homePage.ctl()
                if (c) c.status.favorite(model.id, !model.favorited)
            }
            onMediaClicked: {
                var c = homePage.ctl()
                if (c) c.status.setActiveId(model.id)
                if (homePage.rootRef) homePage.rootRef.navigateTo("media", { statusId: model.id })
            }
            onImageClicked: homePage.openImages(model.pics, index)
            onTopicClicked: homePage.topicRequested()
            onLinkClicked: homePage.openLink(url)
        }
    }

    // ── 加载中（首屏；加载更多由列表内置的尾部指示负责） ──
    Components.LoadingIndicator {
        anchors.centerIn: feedList
        visible: homePage.feedListLoading() && feedList.count <= 0 && homePage.errorText() === ""
        running: visible
        indicatorSize: 18
        text: "加载中…"
    }

    // ── 页内错误提示（列表为空且模型报错） ──
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: tabBar.bottom
        anchors.topMargin: Theme.spacingSmall
        visible: text.length > 0 && feedList.count === 0
        text: homePage.errorText()
        color: Theme.error
        font.pixelSize: Theme.fontTiny
        font.family: Theme.fontFamily
    }

    Component.onCompleted: {
        var c = ctl()
        if (!c) return
        if (c.feed.popularModel().count === 0) c.feed.fetchHome()
        homePage.requested = true
    }

    onVisibleChanged: {
        if (visible) restoreScrollOnShow()
    }
}
