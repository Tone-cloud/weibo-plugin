import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"

// 超话：我的超话列表 + 话题搜索 + 一键签到。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null
    property var rootRef: null

    // 话题搜索关键词，非空时展示搜索结果
    property string query: ""
    property bool searchMode: false
    // 话题搜索当前页码（TopicListModel 没有 page 属性，翻页状态只能放页面里）
    property int searchPage: 1

    signal backClicked()
    signal topicSelected(var containerId, string name)
    signal statusSelected(var id)
    signal checkinAllRequested()

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function topicOf() {
        var c = ctl()
        return (c && c.topic) ? c.topic : null
    }

    function topicModel() {
        var t = root.topicOf()
        if (!t || typeof t.topicModel !== "function") return null
        return t.topicModel()
    }

    function myTopicModel() {
        var t = root.topicOf()
        if (!t || typeof t.myTopicModel !== "function") return null
        return t.myTopicModel()
    }

    function checkinRunning() {
        var c = ctl()
        return c ? c.checkinRunning === true : false
    }

    function checkinSummary() {
        var c = ctl()
        return c && c.checkinSummary ? c.checkinSummary : ""
    }

    function bannerVisible() {
        return root.checkinRunning() || root.checkinSummary() !== ""
    }

    function searchActive() {
        return root.searchMode && root.query.length > 0
    }

    function topicSearchLoading() {
        var m = root.topicModel()
        return m ? m.loading === true : false
    }

    function myTopicLoading() {
        var m = root.myTopicModel()
        return m ? m.loading === true : false
    }

    // TopicRow.clicked / checkinClicked 传出的是它自己的 topicId 属性；
    // 这里统一映射成 main.qml 需要的 containerId（超话是 100808xxxx，
    // 普通话题是 100103type=...）。
    function containerIdOf(item) {
        if (!item) return ""
        if (item.containerId) return item.containerId
        if (item.topicId) return item.topicId
        if (item.id) return item.id
        return ""
    }

    function doCheckinAll() {
        if (root.checkinRunning()) return
        root.checkinAllRequested()
        var t = root.topicOf()
        if (!t || typeof t.checkinAll !== "function") return
        t.checkinAll()
    }

    // 供 main.qml 记录 / 恢复滚动位置
    function contentYValue() {
        var t = root.searchActive() ? topicSearchList : myTopicList
        return t ? t.contentY : 0
    }

    function restoreContentY(y) {
        var t = root.searchActive() ? topicSearchList : myTopicList
        if (!t) return
        Qt.callLater(function() {
            // LoadMoreListView 的 contentY 是只读别名，必须走它的 restoreContentY()
            t.restoreContentY(Math.max(0, y))
            Qt.callLater(function() { t.restoreContentY(Math.max(0, y)) })
        })
    }

    Component.onCompleted: {
        var t = root.topicOf()
        if (t && typeof t.fetchMyTopics === "function") t.fetchMyTopics(1)
    }

    // 签到结果统一走全局 Toast 提示
    Connections {
        target: controller ? controller.topic : null

        function onCheckinAllFinished(success, failed) {
            var c = root.ctl()
            if (c) c.toastMessage("签到完成：成功 " + success + "，失败 " + failed)
        }

        function onCheckinFinished(id, ok, message) {
            var c = root.ctl()
            if (c && message) c.toastMessage(message)
        }
    }

    TitleBar {
        id: titleBar
        title: "超话"
        showBack: true
        showAction: true
        actionText: "一键签到"
        actionEnabled: !root.checkinRunning()
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
        onActionClicked: root.doCheckinAll()
    }

    // ====== 签到状态条 ======
    Rectangle {
        id: checkinBanner
        height: 24
        visible: root.bannerVisible()
        color: Theme.bgSecondary
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        LoadingIndicator {
            id: bannerSpinner
            visible: root.checkinRunning()
            running: visible
            indicatorSize: 14
            text: ""
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: bannerText
            anchors.left: bannerSpinner.visible ? bannerSpinner.right : parent.left
            anchors.leftMargin: bannerSpinner.visible ? Theme.spacingSmall : Theme.spacingNormal
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter
            text: root.checkinRunning() ? "正在签到…" : root.checkinSummary()
            color: root.checkinRunning() ? Theme.primaryLight : Theme.success
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }

    // ====== 话题搜索 ======
    SearchInput {
        id: searchInput
        height: 24
        anchors.top: checkinBanner.visible ? checkinBanner.bottom : titleBar.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        text: root.query
        placeholder: "搜索话题 / 超话"
        showCancel: true
        onTextEdited: root.query = text
        onAccepted: {
            var t = root.topicOf()
            if (!t || typeof t.searchTopics !== "function") return
            root.searchMode = text.length > 0
            root.searchPage = 1
            if (text.length > 0) t.searchTopics(text, 1)
        }
        onCancelClicked: {
            root.query = ""
            root.searchMode = false
        }
    }

    // ====== 内容区 ======
    Item {
        id: contentArea
        anchors.top: searchInput.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        // 搜索结果
        LoadMoreListView {
            id: topicSearchList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            visible: root.searchActive()
            model: root.topicModel()
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            emptyText: "没有找到相关话题"
            emptyHint: "换个关键词试试"
            ready: !root.topicSearchLoading()
            hasMore: topicSearchList.model ? topicSearchList.model.hasMore === true : false
            loadingMore: topicSearchList.model ? topicSearchList.model.loading === true : false

            delegate: TopicRow {
                // TopicListModel 的角色是 containerId / id，没有 topicId
                topicId: model.containerId ? model.containerId : model.id
                name: model.name
                desc: model.desc
                cover: model.cover
                readText: model.readText
                discussText: model.discussText
                isSuper: model.isSuper
                level: model.level
                checked: model.checked
                signedDays: model.signedDays
                statusText: model.statusText
                onClicked: root.topicSelected(root.containerIdOf(model), model.name)
            }

            onLoadMore: {
                var t = root.topicOf()
                if (!t || typeof t.searchTopics !== "function") return
                if (root.query.length <= 0) return
                // topic 模块只暴露 searchTopics(q, page)，搜索翻页沿用同一入口；
                // 页码由页面自己维护（TopicListModel 没有 page 属性）。
                var nextPage = root.searchPage + 1
                root.searchPage = nextPage
                t.searchTopics(root.query, nextPage)
            }
        }

        // 我的超话
        LoadMoreListView {
            id: myTopicList
            anchors.fill: parent
            anchors.margins: Theme.spacingTiny
            visible: !root.searchActive()
            model: root.myTopicModel()
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            emptyText: "还没有关注的超话"
            emptyHint: "去搜索里找找感兴趣的话题"
            ready: !root.myTopicLoading()
            hasMore: myTopicList.model ? myTopicList.model.hasMore === true : false
            loadingMore: myTopicList.model ? myTopicList.model.loading === true : false

            delegate: TopicRow {
                // TopicListModel 的角色是 containerId / id，没有 topicId
                topicId: model.containerId ? model.containerId : model.id
                name: model.name
                desc: model.desc
                cover: model.cover
                readText: model.readText
                discussText: model.discussText
                isSuper: model.isSuper
                level: model.level
                checked: model.checked
                signedDays: model.signedDays
                statusText: model.statusText
                onClicked: root.topicSelected(root.containerIdOf(model), model.name)
                onCheckinClicked: {
                    var t = root.topicOf()
                    if (t && typeof t.checkin === "function") t.checkin(root.containerIdOf(model), model.name)
                }
            }

            onLoadMore: {
                var t = root.topicOf()
                if (t && typeof t.fetchMoreMyTopics === "function") t.fetchMoreMyTopics()
            }
        }

        // 首屏 / 搜索首次加载
        LoadingIndicator {
            anchors.centerIn: parent
            visible: (root.searchActive() ? root.topicSearchLoading() : root.myTopicLoading())
                     && (root.searchActive()
                         ? (topicSearchList.count <= 0)
                         : (myTopicList.count <= 0))
            running: visible
            indicatorSize: 18
            text: "加载中…"
        }
    }
}
