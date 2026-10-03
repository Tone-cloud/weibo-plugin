import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"

// 话题 / 超话详情：展示该话题下的微博流；超话额外提供签到入口。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null
    property string containerId: ""
    property string topicName: ""
    property var rootRef: null

    // 已发起过请求的 containerId，避免重复请求
    property string requestedId: ""

    signal backClicked()
    signal statusSelected(var id)
    signal userSelected(var uid)
    signal checkinRequested(var topicId, string name)

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function topicOf() {
        var c = ctl()
        return (c && c.topic) ? c.topic : null
    }

    function statusModel() {
        var t = root.topicOf()
        if (!t || typeof t.statusModel !== "function") return null
        return t.statusModel()
    }

    // 超话与普通话题的 containerId 前缀不同：100808 为超话（可签到），
    // 100103 为普通话题流（没有签到）。
    function isSuperTopic() {
        return containerId.indexOf("100808") === 0
    }

    function modelCount() {
        var m = root.statusModel()
        return m ? m.count : 0
    }

    function modelLoading() {
        var m = root.statusModel()
        return m ? m.loading === true : false
    }

    function modelError() {
        var m = root.statusModel()
        if (!m) return ""
        return m.errorMessage ? m.errorMessage : ""
    }

    function load() {
        var t = root.topicOf()
        if (!t || containerId === "") return
        if (requestedId === containerId) return
        requestedId = containerId
        t.fetchTopicStatuses(containerId, "")
    }

    onContainerIdChanged: {
        requestedId = ""
        root.load()
    }

    Component.onCompleted: root.load()

    // 供 main.qml 记录 / 恢复滚动位置
    function contentYValue() {
        return topicList ? topicList.contentY : 0
    }

    function restoreContentY(y) {
        if (!topicList) return
        Qt.callLater(function() {
            // LoadMoreListView 的 contentY 是只读别名，必须走它的 restoreContentY()
            topicList.restoreContentY(Math.max(0, y))
            Qt.callLater(function() { topicList.restoreContentY(Math.max(0, y)) })
        })
    }

    TitleBar {
        id: titleBar
        title: root.topicName !== "" ? root.topicName : "话题"
        showBack: true
        showAction: root.isSuperTopic()
        actionText: "签到"
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
        onActionClicked: root.checkinRequested(root.containerId, root.topicName)
    }

    Item {
        id: contentArea
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        LoadMoreListView {
            id: topicList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            model: root.statusModel()
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            emptyText: "该话题还没有微博"
            emptyHint: "换个话题看看"
            // ready 为 true 时组件自己显示空态；出错时关掉它，交给页面级 ErrorOverlay
            ready: root.modelError() === ""
            hasMore: topicList.model ? topicList.model.hasMore === true : false
            loadingMore: topicList.model ? topicList.model.loading === true : false

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
            }

            onLoadMore: {
                var t = root.topicOf()
                if (t && typeof t.fetchMoreTopicStatuses === "function") t.fetchMoreTopicStatuses()
            }
        }

        // 加载中
        LoadingIndicator {
            anchors.centerIn: parent
            visible: root.modelCount() <= 0 && root.modelLoading()
            running: visible
            indicatorSize: 18
            text: "加载中…"
        }

        // 出错 + 重试
        ErrorOverlay {
            anchors.fill: parent
            visible: root.modelError() !== ""
            errorMessage: root.modelError()
            onRetryClicked: {
                root.requestedId = ""
                root.load()
            }
        }
    }
}
