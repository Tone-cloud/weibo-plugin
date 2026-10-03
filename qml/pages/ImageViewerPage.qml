import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl
import "../components"

// 图片查看器：全屏黑底、横向分页浏览微博配图。
// 手势缩放（双指捏合）不在本版本的范围内，因此没有 PinchArea；
// 只提供左右翻页、点击切换顶部信息栏、单图加载失败重试。
//
// 注意：这里直接用 ListView 而不是 LoadMoreListView —— 后者是 Item 包装，
// 不暴露 currentIndex / snapMode / highlightRangeMode，无法实现整页吸附翻页。
Rectangle {
    id: root
    anchors.fill: parent
    color: "#000000"
    clip: true

    property var controller: null
    // [{url, large, width, height, index}, ...]
    property var pics: null
    property int initialIndex: 0

    property bool barVisible: true

    signal backClicked()

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function picCount() {
        if (!pics || pics.length === undefined) return 0
        return pics.length
    }

    function startIndex() {
        if (initialIndex < 0) return 0
        if (initialIndex >= root.picCount()) return Math.max(0, root.picCount() - 1)
        return initialIndex
    }

    // 只在页面创建时定位到 initialIndex；之后交给 ListView 自己维护 currentIndex，
    // 避免属性绑定覆盖用户滑动手势（StrictlyEnforceRange 下必须如此）。
    function applyInitialIndex() {
        if (root.picCount() <= 0) return
        imageList.currentIndex = root.startIndex()
        imageList.positionViewAtIndex(root.startIndex(), ListView.Center)
    }

    function toggleBar() {
        root.barVisible = !root.barVisible
    }

    Component.onCompleted: applyInitialIndex()

    // 横向查看器没有纵向滚动，仅为统一页面 API 保留这两个方法
    function contentYValue() {
        return 0
    }

    function restoreContentY(y) {
    }

    ListView {
        id: imageList
        anchors.fill: parent
        visible: root.picCount() > 0
        model: root.pics
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        highlightMoveDuration: Theme.animNormal
        currentIndex: 0
        spacing: 0
        cacheBuffer: Theme.cardWidth * 2
        displayMarginBeginning: Theme.cardWidth
        displayMarginEnd: Theme.cardWidth
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        delegate: Item {
            id: pageItem
            width: imageList.width
            height: imageList.height

            property string picUrl: (modelData && modelData.url) ? modelData.url : ""
            property string picLarge: (modelData && modelData.large) ? modelData.large : ""
            property string resolvedSource: root.picCount() > 0
                                             ? ImageUrl.originalSource(picLarge !== "" ? picLarge : picUrl)
                                             : ""
            // 每次重试自增，拼到 URL 后面强制 QML 重新发起加载
            property int reloadToken: 0

            Image {
                id: pageImage
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                source: pageItem.resolvedSource !== ""
                        ? (pageItem.reloadToken > 0
                           ? (pageItem.resolvedSource
                              + (pageItem.resolvedSource.indexOf("?") >= 0 ? "&" : "?")
                              + "r=" + pageItem.reloadToken)
                           : pageItem.resolvedSource)
                        : ""
            }

            // 加载中
            LoadingIndicator {
                anchors.centerIn: parent
                visible: pageImage.status === Image.Loading
                running: visible
                indicatorSize: 18
                text: ""
            }

            // 加载失败 + 重试
            Column {
                anchors.centerIn: parent
                spacing: Theme.spacingSmall
                visible: pageImage.status === Image.Error

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "图片加载失败"
                    color: Theme.error
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                }

                Rectangle {
                    width: 64
                    height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: retryArea.pressed ? Theme.bgTertiary : Theme.bgSecondary
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        anchors.centerIn: parent
                        text: "重试"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    MouseArea {
                        id: retryArea
                        anchors.fill: parent
                        preventStealing: true
                        onClicked: pageItem.reloadToken = pageItem.reloadToken + 1
                    }
                }
            }

            // 点击图片切换顶部信息栏；轻微移动视为翻页手势，不触发切换。
            // 不用 preventStealing，否则横向拖动无法传递给 ListView 翻页。
            MouseArea {
                id: tapArea
                anchors.fill: parent
                property real pressX: 0
                property real pressY: 0
                onPressed: {
                    pressX = mouse.x
                    pressY = mouse.y
                }
                onReleased: {
                    var dx = Math.abs(mouse.x - pressX)
                    var dy = Math.abs(mouse.y - pressY)
                    if (dx < 8 && dy < 8) root.toggleBar()
                }
                onCanceled: { }
            }
        }
    }

    // ====== 顶部信息栏 ======
    Rectangle {
        id: overlayBar
        visible: root.barVisible && root.picCount() > 0
        height: Theme.titleBarHeight
        color: Theme.bgOverlay
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right

        IconButton {
            id: backButton
            glyph: "‹"
            label: "返回"
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingTiny
            anchors.verticalCenter: parent.verticalCenter
            onClicked: root.backClicked()
        }

        Text {
            anchors.centerIn: parent
            text: (imageList.currentIndex + 1) + " / " + root.picCount()
            color: Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }
    }

    // 没有图片时的兜底
    EmptyState {
        anchors.fill: parent
        visible: root.picCount() <= 0
        text: "没有图片"
        hint: ""
        glyph: "▣"
    }
}
