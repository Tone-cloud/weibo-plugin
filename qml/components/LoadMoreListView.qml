import QtQuick 2.12
import WeiboPlugin 1.0

// 带"加载更多 / 空态 / 尾部加载指示"的列表容器。
// 距尾部 200px 内自动 loadMore()（hasMore && !loadingMore 时）。
// 提供 contentYValue()/restoreContentY() 等滚动位置存取方法，供页面保活恢复。
Item {
    id: root
    width: parent ? parent.width : 300
    height: parent ? parent.height : 140
    clip: true

    property var model: null
    property int spacing: 0
    property int contentMargin: 0
    property int orientation: ListView.Vertical
    property Component delegate: null
    property bool hasMore: true
    property bool loadingMore: false
    property string emptyText: "暂无内容"
    // 契约外可选：空态补充提示与字形
    property string emptyHint: ""
    property string emptyGlyph: "▣"
    // 契约外可选：数据是否已就绪（false 时不显示空态，避免加载瞬间闪空）
    property bool ready: true

    signal loadMore()

    readonly property bool atYBeginning: list.atYBeginning
    readonly property bool atYEnd: list.atYEnd
    readonly property alias count: list.count
    readonly property alias contentY: list.contentY
    readonly property alias contentX: list.contentX
    readonly property alias contentHeight: list.contentHeight
    readonly property alias contentWidth: list.contentWidth
    // 兼容别名
    readonly property alias loading: root.loadingMore

    // 自动加载的触发阈值
    readonly property int _threshold: 200
    // 短列表（内容不足一屏）防抖：同一条目数只尝试一次
    property int _lastShortCount: -1

    // ── 滚动位置存取（页面保活用）──
    function contentYValue() {
        return list.contentY
    }

    function contentXValue() {
        return list.contentX
    }

    function restoreContentY(v) {
        if (v === undefined || v === null) return
        list.contentY = Number(v)
    }

    function restoreContentX(v) {
        if (v === undefined || v === null) return
        list.contentX = Number(v)
    }

    // 供页面在数据变化后复用同一套防抖逻辑
    function resetLoadMoreGuard() {
        root.loadingMore = false
    }

    function _maybeLoadMore() {
        if (list.count <= 0) return
        if (root.loadingMore) return
        if (!root.hasMore) return
        if (root.orientation === ListView.Horizontal) {
            if (list.contentWidth > list.width + 2) {
                if (list.contentX < list.contentWidth - list.width - root._threshold) return
            } else {
                // 内容不满一屏时 atXEnd 恒真，改由条目数变化驱动
                if (list.count === root._lastShortCount) return
            }
        } else {
            if (list.contentHeight > list.height + 2) {
                if (list.contentY < list.contentHeight - list.height - root._threshold) return
            } else {
                if (list.count === root._lastShortCount) return
            }
        }
        root._lastShortCount = list.count
        root.loadMore()
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.visible ? footer.top : parent.bottom
        clip: true
        orientation: root.orientation
        spacing: root.spacing
        cacheBuffer: Theme.listCacheBuffer
        displayMarginBeginning: Theme.listDisplayMargin
        displayMarginEnd: Theme.listDisplayMargin
        boundsBehavior: Flickable.StopAtBounds
        model: root.model
        delegate: root.delegate

        topMargin: root.contentMargin
        bottomMargin: root.contentMargin
        leftMargin: root.orientation === ListView.Horizontal ? root.contentMargin : 0
        rightMargin: root.orientation === ListView.Horizontal ? root.contentMargin : 0

        // 滚动到尾部附近 → 加载更多
        onContentYChanged: root._maybeLoadMore()
        onContentXChanged: root._maybeLoadMore()
        onAtYEndChanged: root._maybeLoadMore()
        onAtXEndChanged: root._maybeLoadMore()
        onCountChanged: {
            root.loadingMore = false
            root._maybeLoadMore()
        }
        onModelChanged: {
            root.loadingMore = false
            root._lastShortCount = -1
        }
    }

    // 空态（数据已就绪、无条目、且不在加载中）
    EmptyState {
        anchors.centerIn: parent
        width: parent.width - Theme.spacingLarge * 2
        visible: list.count === 0 && !root.loadingMore && root.ready
        text: root.emptyText
        hint: root.emptyHint
        glyph: root.emptyGlyph
    }

    // 尾部加载指示
    LoadingIndicator {
        id: footer
        anchors.bottom: parent.bottom
        width: parent.width
        visible: root.loadingMore
        running: root.loadingMore
        text: "加载中..."
    }
}
