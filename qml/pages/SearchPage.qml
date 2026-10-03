import QtQuick 2.12
import WeiboPlugin 1.0
import "../components" as Components
import "../components"

// 搜索页：微博 / 用户 / 话题 三类结果 + 搜索历史 + 热搜词。
// 布局：TitleBar(28) + SearchInput(22) + TabBar(22) + 结果区(98)。
Rectangle {
    id: searchPage
    anchors.fill: parent
    color: Theme.bgPrimary

    property var controller: null
    property var rootRef: null

    property int kindIndex: 0
    property string keyword: ""
    // 是否已经发起过搜索（决定显示「历史/热搜」还是「结果/空状态」）
    property bool searched: false

    signal backClicked()
    signal statusSelected(var id)
    signal userSelected(var uid)
    signal topicSelected(var containerId, var name)

    function ctl() {
        return (controller && controller.search) ? controller : null
    }

    function kindName() {
        if (kindIndex === 1) return "user"
        if (kindIndex === 2) return "topic"
        return "status"
    }

    function historyModelRef() {
        var c = ctl()
        return c ? c.search.historyModel() : null
    }

    function hotSearchModelRef() {
        var c = ctl()
        return c ? c.search.hotSearchModel() : null
    }

    function statusModelRef() {
        var c = ctl()
        return c ? c.search.searchModel() : null
    }

    function userModelRef() {
        var c = ctl()
        return c ? c.search.userModel() : null
    }

    function topicModelRef() {
        var c = ctl()
        return c ? c.search.topicModel() : null
    }

    function historyCount() {
        var m = historyModelRef()
        return m ? m.count : 0
    }

    function hotSearchCount() {
        var m = hotSearchModelRef()
        return m ? m.count : 0
    }

    function hasDiscover() {
        return (historyCount() + hotSearchCount()) > 0
    }

    function emptyResultText() {
        if (kindIndex === 1) return "没有找到相关用户"
        if (kindIndex === 2) return "没有找到相关话题"
        return "没有找到相关微博"
    }

    function runSearch(q) {
        var c = ctl()
        if (!c) return
        var k = (q === undefined || q === null) ? "" : String(q)
        if (k.replace(/\s/g, "") === "") return
        searchPage.keyword = k
        searchInput.text = k
        searchPage.searched = true
        c.search.search(k, searchPage.kindName())
    }

    function clearInput() {
        searchInput.text = ""
        searchPage.keyword = ""
        searchPage.searched = false
    }

    function clearHistory() {
        var c = ctl()
        if (c) c.search.clearHistory()
    }

    function openImages(pics, index) {
        if (!pics || pics.length === 0) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openImages) c.viewer.openImages(pics, index)
    }

    function openMedia(id) {
        if (!id) return
        if (rootRef && rootRef.navigateTo) rootRef.navigateTo("media", { statusId: id })
    }

    function openLink(url) {
        if (!url) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openUrl) c.viewer.openUrl(url)
    }

    function checkin(topicId, name) {
        var c = ctl()
        if (c && c.topic) c.topic.checkin(topicId, name)
    }

    Components.TitleBar {
        id: titleBar
        title: "搜索"
        showBack: true
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: searchPage.backClicked()
    }

    Components.SearchInput {
        id: searchInput
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 22
        placeholder: "搜索微博 / 用户 / 话题"
        showCancel: true
        onTextEdited: {
            searchPage.keyword = text
            if (text === "") searchPage.searched = false
        }
        onAccepted: searchPage.runSearch(text)
        onCancelClicked: searchPage.clearInput()
    }

    Components.TabBar {
        id: kindBar
        anchors.top: searchInput.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        tabHeight: 22
        tabs: [{ text: "微博" }, { text: "用户" }, { text: "话题" }]
        currentIndex: searchPage.kindIndex
        onTabClicked: {
            searchPage.kindIndex = index
            if (searchPage.keyword !== "") searchPage.runSearch(searchPage.keyword)
        }
    }

    // ── 结果区 ──
    Item {
        id: contentArea
        anchors.top: kindBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        Components.LoadMoreListView {
            id: statusList
            anchors.fill: parent
            visible: searchPage.kindIndex === 0
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            contentMargin: Theme.spacingSmall
            clip: true
            model: searchPage.kindIndex === 0 ? searchPage.statusModelRef() : null
            hasMore: model ? model.hasMore : false
            loadingMore: model ? model.loading : false
            ready: searchPage.searched && !searchPage.loadingResults()
            emptyText: "没有找到相关微博"
            emptyHint: "换个关键词试试"
            emptyGlyph: "⌕"
            onLoadMore: {
                var c = searchPage.ctl()
                if (c) c.search.searchMore()
            }

            delegate: Components.BlogCard {
                compact: true
                blog: ({
                    id: model.id, bid: model.bid, text: model.text, textHtml: model.textHtml,
                    createdAt: model.createdAt, createdTs: model.createdTs,
                    createdText: model.createdText, source: model.source,
                    regionName: model.regionName, isLongText: model.isLongText,
                    authorId: model.authorId, authorName: model.authorName,
                    authorAvatar: model.authorAvatar, authorVerified: model.authorVerified,
                    authorVerifiedType: model.authorVerifiedType,
                    authorVerifiedReason: model.authorVerifiedReason,
                    pics: model.pics, picCount: model.picCount, firstPic: model.firstPic,
                    pageType: model.pageType, pageTitle: model.pageTitle,
                    pageCover: model.pageCover, pageUrl: model.pageUrl,
                    pageMediaUrl: model.pageMediaUrl, pageDuration: model.pageDuration,
                    pageLiveStatus: model.pageLiveStatus, hasMedia: model.hasMedia,
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

                onClicked: searchPage.statusSelected(model.id)
                onAuthorClicked: searchPage.userSelected(uid)
                onTopicClicked: searchPage.topicSelected("", name)
                onImageClicked: searchPage.openImages(model.pics, index)
                onMediaClicked: searchPage.openMedia(model.id)
                onLinkClicked: searchPage.openLink(url)
            }
        }

        Components.LoadMoreListView {
            id: userList
            anchors.fill: parent
            visible: searchPage.kindIndex === 1
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            contentMargin: Theme.spacingSmall
            clip: true
            model: searchPage.kindIndex === 1 ? searchPage.userModelRef() : null
            hasMore: model ? model.hasMore : false
            loadingMore: model ? model.loading : false
            ready: searchPage.searched && !searchPage.loadingResults()
            emptyText: "没有找到相关用户"
            emptyHint: "换个关键词试试"
            emptyGlyph: "⌕"
            onLoadMore: {
                var c = searchPage.ctl()
                if (c) c.search.searchUsersMore()
            }

            delegate: Components.UserRow {
                uid: model.uid
                name: model.name
                avatar: model.avatar
                verified: model.verified
                verifiedType: model.verifiedType
                description: model.description
                followersText: model.followersText
                isFollowing: model.isFollowing
                showFollow: false
                onClicked: searchPage.userSelected(model.uid)
            }
        }

        Components.LoadMoreListView {
            id: topicList
            anchors.fill: parent
            visible: searchPage.kindIndex === 2
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            contentMargin: Theme.spacingSmall
            clip: true
            model: searchPage.kindIndex === 2 ? searchPage.topicModelRef() : null
            hasMore: model ? model.hasMore : false
            loadingMore: model ? model.loading : false
            ready: searchPage.searched && !searchPage.loadingResults()
            emptyText: "没有找到相关话题"
            emptyHint: "换个关键词试试"
            emptyGlyph: "⌕"
            onLoadMore: {
                var c = searchPage.ctl()
                if (c) c.search.searchTopicsMore()
            }

            delegate: Components.TopicRow {
                // 超话用 containerId（100808xxxx），普通话题只有 id
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
                onClicked: searchPage.topicSelected(topicId, name)
                onCheckinClicked: searchPage.checkin(topicId, name)
            }
        }

        // 尚未搜索（结果区三个列表的 ready 在未搜索时为 false，不会与内置空态冲突）
        Components.EmptyState {
            anchors.centerIn: parent
            visible: !searchPage.searched && !discoverPanel.visible
            text: "输入关键词搜索微博 / 用户 / 话题"
            hint: "支持微博 / 用户 / 话题三类结果"
            glyph: "⌕"
        }

        // 搜索中（结果为空时居中提示；加载更多由列表内置尾部指示负责）
        Components.LoadingIndicator {
            anchors.centerIn: parent
            visible: searchPage.loadingResults() && searchPage.activeResultCount() === 0
            running: visible
            indicatorSize: 18
            text: "搜索中…"
        }

        // ── 搜索历史 + 热搜词（未搜索时覆盖结果区） ──
        Rectangle {
            id: discoverPanel
            anchors.fill: parent
            color: Theme.bgPrimary
            visible: searchPage.keyword === "" && !searchPage.searched && searchPage.hasDiscover()
            z: 10

            Flickable {
                id: discoverFlick
                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: discoverColumn.height + Theme.spacingMedium
                boundsBehavior: Flickable.DragOverBounds

                Column {
                    id: discoverColumn
                    width: discoverFlick.width
                    spacing: Theme.spacingSmall

                    // 历史标题 + 清空
                    Item {
                        width: parent.width
                        height: 14
                        visible: searchPage.historyCount() > 0

                        Text {
                            x: Theme.spacingSmall
                            y: 0
                            height: parent.height
                            width: 100
                            text: "搜索历史"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.fontFamily
                            font.bold: true
                            verticalAlignment: Text.AlignVCenter
                        }

                        Rectangle {
                            x: parent.width - 40
                            y: 0
                            width: 36
                            height: 14
                            radius: Theme.radiusSmall
                            visible: searchPage.historyCount() > 0
                            color: clearHistoryArea.pressed ? Theme.bgTertiary : "transparent"
                            border.color: Theme.border
                            border.width: 1

                            Text {
                                anchors.centerIn: parent
                                text: "清空"
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontTiny
                                font.family: Theme.fontFamily
                            }

                            MouseArea {
                                id: clearHistoryArea
                                anchors.fill: parent
                                onClicked: searchPage.clearHistory()
                            }
                        }
                    }

                    Flow {
                        id: historyFlow
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: searchPage.historyModelRef()

                            Rectangle {
                                height: 18
                                width: historyChipText.implicitWidth + 14
                                radius: Theme.radiusRound
                                color: historyChipArea.pressed
                                       ? Theme.withAlpha(Theme.primary, 0.25) : Theme.bgTertiary

                                Text {
                                    id: historyChipText
                                    anchors.centerIn: parent
                                    text: model.word ? model.word : ""
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.fontSmall
                                    font.family: Theme.fontFamily
                                }

                                MouseArea {
                                    id: historyChipArea
                                    anchors.fill: parent
                                    onClicked: searchPage.runSearch(model.word)
                                }
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: searchPage.hotSearchCount() > 0
                        text: "微博热搜"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSmall
                        font.family: Theme.fontFamily
                        font.bold: true
                    }

                    Flow {
                        id: hotFlow
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: searchPage.hotSearchModelRef()

                            Rectangle {
                                height: 18
                                width: hotChipText.implicitWidth + 14
                                radius: Theme.radiusRound
                                color: hotChipArea.pressed
                                       ? Theme.withAlpha(Theme.primary, 0.25)
                                       : Theme.withAlpha(Theme.primary, 0.12)

                                Text {
                                    id: hotChipText
                                    anchors.centerIn: parent
                                    text: model.word ? model.word : ""
                                    color: Theme.primaryLight
                                    font.pixelSize: Theme.fontSmall
                                    font.family: Theme.fontFamily
                                }

                                MouseArea {
                                    id: hotChipArea
                                    anchors.fill: parent
                                    onClicked: searchPage.runSearch(model.word)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // 当前 Tab 已加载的结果条数
    function activeResultCount() {
        var m = null
        if (kindIndex === 0) m = statusModelRef()
        else if (kindIndex === 1) m = userModelRef()
        else m = topicModelRef()
        return m ? m.count : 0
    }

    function loadingResults() {
        var m = null
        if (kindIndex === 0) m = statusModelRef()
        else if (kindIndex === 1) m = userModelRef()
        else m = topicModelRef()
        return m ? m.loading : false
    }

    // 话题名：TopicRow 的 clicked/checkinClicked 只回传 topicId，
    // 话题昵称直接取 delegate 自身的 name 属性（见上面的 TopicRow delegate）。
    Component.onCompleted: {
        var c = ctl()
        if (c) c.search.fetchHotSearch()
    }
}
