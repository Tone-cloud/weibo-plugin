import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"
import "../components" as Components
// 热搜榜：左侧热搜列表（116px）+ 右侧该热词下的微博流（204px）。
// 布局：TitleBar(28) + 双栏(142)。
Rectangle {
    id: hotPage
    anchors.fill: parent
    color: Theme.bgPrimary

    property var controller: null
    property var rootRef: null
    // 由 main.qml 传入 / 恢复的当前关键词
    property string word: ""
    property string selectedWord: ""

    signal backClicked()
    signal wordSelected(var word)

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function hotSearchModelRef() {
        var c = ctl()
        return c ? c.feed.hotSearchModel() : null
    }

    function rankModelRef() {
        var c = ctl()
        return c ? c.feed.rankingModel() : null
    }

    function rankLoading() {
        var m = rankModelRef()
        return m ? m.loading === true : false
    }

    function refreshHot() {
        var c = ctl()
        if (c) c.feed.fetchHot()
    }

    // 注意：LoadMoreListView 的 contentY 是只读别名，读写必须走它的方法。
    function contentYValue() {
        return rankList ? rankList.contentYValue() : 0
    }

    function restoreContentY(v) {
        if (!rankList) return
        var y = Number(v ? v : 0)
        Qt.callLater(function() {
            rankList.restoreContentY(y)
            Qt.callLater(function() { rankList.restoreContentY(y) })
        })
    }

    // 选中热词 → 拉取该词下的微博流
    function selectWord(w) {
        if (!w || w === "") return
        var c = ctl()
        if (!c) return
        if (hotPage.selectedWord === w && c.feed.rankingModel().count > 0) return
        hotPage.selectedWord = String(w)
        c.feed.fetchHotStatus(String(w))
        hotPage.wordSelected(String(w))
    }

    function openStatus(id) {
        if (!id) return
        if (rootRef && rootRef.navigateTo) rootRef.navigateTo("detail", { statusId: id })
    }

    function openUser(uid) {
        if (!uid) return
        if (rootRef && rootRef.navigateTo) rootRef.navigateTo("user", { uid: uid })
    }

    function openMedia(id) {
        if (!id) return
        var c = ctl()
        if (c) c.status.setActiveId(id)
        if (rootRef && rootRef.navigateTo) rootRef.navigateTo("media", { statusId: id })
    }

    function openImages(pics, index) {
        if (!pics || pics.length === 0) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openImages) c.viewer.openImages(pics, index)
    }

    function openLink(url) {
        if (!url) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openUrl) c.viewer.openUrl(url)
    }

    Components.TitleBar {
        id: titleBar
        title: "热搜榜"
        showBack: true
        showAction: true
        actionText: "↻"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: hotPage.backClicked()
        onActionClicked: hotPage.refreshHot()
    }

    // ── 左：热搜列表 ──
    Rectangle {
        id: leftPane
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: 116
        color: Theme.bgSecondary

        Rectangle {
            width: 1
            height: parent.height
            anchors.right: parent.right
            color: Theme.divider
        }

        ListView {
            id: hotList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            orientation: ListView.Vertical
            clip: true
            spacing: 1
            cacheBuffer: Theme.listCacheBuffer
            model: hotPage.hotSearchModelRef()

            delegate: Item {
                width: hotList.width
                height: 22

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusSmall
                    color: hotPage.selectedWord === model.word
                           ? Theme.withAlpha(Theme.primary, 0.16) : "transparent"
                }

                Components.HotRow {
                    x: 0
                    y: 0
                    width: parent.width
                    height: parent.height
                    rank: model.rank
                    word: model.word
                    rawHot: model.rawHot
                    hotText: model.hotText
                    label: model.label
                    showRank: true
                    onClicked: hotPage.selectWord(word)
                }
            }
        }

        Components.LoadingIndicator {
            anchors.centerIn: hotList
            running: hotPage.hotSearchModelRef() ? hotPage.hotSearchModelRef().loading : false
            indicatorSize: 14
        }
    }

    // ── 右：热词微博流 ──
    Components.LoadMoreListView {
        id: rankList
        anchors.top: titleBar.bottom
        anchors.left: leftPane.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        orientation: ListView.Vertical
        spacing: Theme.spacingSmall
        contentMargin: Theme.spacingSmall
        clip: true
        model: hotPage.rankModelRef()
        hasMore: model ? model.hasMore : false
        loadingMore: model ? model.loading : false
        ready: true
        emptyText: hotPage.selectedWord === "" ? "点击左侧热搜词查看微博" : "该热词暂时没有微博"
        emptyHint: hotPage.selectedWord === "" ? "点右上角 ↻ 刷新热搜榜" : "换个热搜词试试"
        emptyGlyph: "☰"
        onLoadMore: {
            var c = hotPage.ctl()
            if (c) c.feed.fetchMoreHotStatus()
        }

        delegate: Components.BlogCard {
            compact: true
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

            onClicked: hotPage.openStatus(model.id)
            onAuthorClicked: hotPage.openUser(uid)
            onImageClicked: hotPage.openImages(model.pics, index)
            onMediaClicked: hotPage.openMedia(model.id)
            onLinkClicked: hotPage.openLink(url)
        }
    }

    // 右侧：首屏加载中（加载更多由列表内置尾部指示负责）
    Components.LoadingIndicator {
        anchors.centerIn: rankList
        visible: hotPage.rankLoading() && rankList.count <= 0 && hotPage.selectedWord !== ""
        running: visible
        indicatorSize: 18
        text: "加载中…"
    }

    Component.onCompleted: {
        var c = ctl()
        if (c) c.feed.fetchHot()
        if (word !== "") hotPage.selectWord(word)
    }

    onWordChanged: {
        if (word !== "" && word !== hotPage.selectedWord) hotPage.selectWord(word)
    }

    onVisibleChanged: {
        if (visible && rootRef) restoreContentY(rootRef.hotContentY)
    }
}
