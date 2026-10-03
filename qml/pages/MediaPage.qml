import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl
import "../components"

// 播放：解析微博里的视频 / 直播直链，展示清晰度列表并把 URL 交给宿主播放器。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null
    property string statusId: ""

    // 用户手动选择的清晰度直链（空串表示用 controller.mediaUrl）
    property string selectedUrl: ""
    // true 表示 selectedUrl 由用户点击清晰度写入，模型刷新时不应被覆盖
    property bool manualSelected: false
    // 已发起过解析的 statusId
    property string requestedId: ""

    signal backClicked()

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function mediaOf() {
        var c = ctl()
        return (c && c.media) ? c.media : null
    }

    function mediaType() {
        var c = ctl()
        return c && c.mediaType ? c.mediaType : ""
    }

    function mediaTitle() {
        var c = ctl()
        return c && c.mediaTitle ? c.mediaTitle : ""
    }

    function mediaUrl() {
        var c = ctl()
        return c && c.mediaUrl ? c.mediaUrl : ""
    }

    function mediaStatus() {
        var c = ctl()
        return c && c.mediaStatus ? c.mediaStatus : ""
    }

    function mediaCover() {
        var c = ctl()
        return c && c.mediaCover ? c.mediaCover : ""
    }

    function mediaLoading() {
        var c = ctl()
        return c ? c.mediaLoading === true : false
    }

    function qualityList() {
        var c = ctl()
        if (!c || !c.mediaQualities) return []
        return c.mediaQualities
    }

    function firstQualityUrl() {
        var list = root.qualityList()
        for (var i = 0; i < list.length; ++i) {
            var q = list[i]
            if (q && q.url) return String(q.url)
        }
        return ""
    }

    // 播放按钮最终使用的直链：优先用户选择，其次媒体主直链，再次第一个清晰度
    function resolvedUrl() {
        if (selectedUrl !== "") return selectedUrl
        if (root.mediaUrl() !== "") return root.mediaUrl()
        return root.firstQualityUrl()
    }

    function hasQualities() {
        return root.qualityList().length > 0
    }

    // 解析完成后 mediaUrl 或清晰度列表可能变化，此时重置为自动选择
    function hasAnyUrl() {
        if (root.mediaUrl() !== "") return true
        var list = root.qualityList()
        for (var i = 0; i < list.length; ++i) {
            var q = list[i]
            if (q && q.url) return true
        }
        return false
    }

    function prepare() {
        var m = root.mediaOf()
        if (!m || statusId === "") return
        if (requestedId === statusId) return
        requestedId = statusId
        m.prepare(statusId)
    }

    function play() {
        var m = root.mediaOf()
        if (!m || typeof m.playUrl !== "function") return
        var url = root.resolvedUrl()
        if (url === "") {
            var c = root.ctl()
            if (c) c.toastMessage("暂无可播放的地址")
            return
        }
        m.playUrl(url)
    }

    // 自动选择：主直链优先，其次第一个清晰度
    function applyAutoSelect() {
        root.manualSelected = false
        root.selectedUrl = root.mediaUrl() !== "" ? root.mediaUrl() : root.firstQualityUrl()
    }

    onStatusIdChanged: {
        requestedId = ""
        root.manualSelected = false
        root.selectedUrl = ""
        root.prepare()
    }

    Component.onCompleted: root.prepare()

    Connections {
        target: controller ? controller.media : null
        function onResolved(type, url) {
            // 解析出新结果时回到自动选择（用户手动挑过清晰度则不覆盖）
            if (!root.manualSelected) root.applyAutoSelect()
        }
    }

    TitleBar {
        id: titleBar
        title: "播放"
        showBack: true
        showAction: true
        actionText: "↻"
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
        onActionClicked: {
            root.requestedId = ""
            root.prepare()
        }
    }

    Item {
        id: contentArea
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right

        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingTiny
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingTiny

            // ── 封面区 160×90 ──
            Item {
                id: coverArea
                width: 160
                height: 90
                anchors.horizontalCenter: parent.horizontalCenter

                Rectangle {
                    anchors.fill: parent
                    color: Theme.bgTertiary
                    radius: Theme.radiusSmall
                    clip: true

                    Image {
                        id: coverImage
                        anchors.fill: parent
                        source: root.mediaCover() !== "" ? ImageUrl.rawSource(root.mediaCover()) : ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        cache: true
                    }

                    // 视频：居中播放三角
                    Text {
                        anchors.centerIn: parent
                        visible: root.mediaType() === "video" || root.mediaType() === "music"
                        text: "▶"
                        color: Theme.textOnPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontHuge
                    }

                    // 直播：左上角脉冲红点 + 文案
                    Row {
                        id: liveBadge
                        visible: root.mediaType() === "live"
                        spacing: Theme.spacingTiny
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spacingSmall

                        Text {
                            id: liveDot
                            anchors.verticalCenter: parent.verticalCenter
                            text: "●"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody

                            SequentialAnimation on opacity {
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 600 }
                                NumberAnimation { to: 1.0; duration: 600 }
                            }
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: "直播中"
                            color: Theme.accent
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }
                    }
                }
            }

            // ── 标题（最多 2 行）──
            Text {
                id: titleText
                width: parent.width - Theme.spacingSmall * 2
                anchors.horizontalCenter: parent.horizontalCenter
                height: 22
                text: root.mediaTitle() !== "" ? root.mediaTitle() : "未获取到媒体标题"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontBody
                font.bold: true
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                clip: true
            }

            // ── 状态行 ──
            Text {
                id: statusText
                width: parent.width - Theme.spacingSmall * 2
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.mediaStatus() !== "" ? root.mediaStatus() : "等待解析…"
                color: Theme.textSecondary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontTiny
                elide: Text.ElideRight
            }

            // ── 清晰度选择 ──
            Item {
                id: qualityStrip
                width: parent.width
                height: 22
                visible: root.hasQualities()

                Flickable {
                    id: qualityFlick
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacingSmall
                    anchors.rightMargin: Theme.spacingSmall
                    contentWidth: qualityRow.width
                    contentHeight: qualityStrip.height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    Row {
                        id: qualityRow
                        spacing: Theme.spacingSmall

                        Repeater {
                            model: root.qualityList()

                            delegate: Rectangle {
                                id: qualityChip
                                height: 20
                                width: qualityChipText.implicitWidth + Theme.spacingLarge
                                radius: Theme.radiusRound
                                color: root.resolvedUrl() === modelData.url
                                       ? Theme.primary
                                       : (qualityArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                                border.width: root.resolvedUrl() === modelData.url ? 0 : 1
                                border.color: Theme.withAlpha(Theme.primary, 0.25)
                                anchors.verticalCenter: parent.verticalCenter

                                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                                Text {
                                    id: qualityChipText
                                    anchors.centerIn: parent
                                    text: modelData.label ? modelData.label : "默认"
                                    color: root.resolvedUrl() === modelData.url
                                           ? Theme.textOnPrimary : Theme.textSecondary
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontTiny
                                    font.bold: root.resolvedUrl() === modelData.url
                                }

                                MouseArea {
                                    id: qualityArea
                                    anchors.fill: parent
                                    onClicked: {
                                        root.selectedUrl = modelData.url ? modelData.url : ""
                                        root.manualSelected = root.selectedUrl !== ""
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── 播放按钮 ──
            Rectangle {
                id: playButton
                width: 72
                height: Theme.buttonHeight
                radius: Theme.radiusRound
                color: playArea.pressed ? Theme.primaryDark : Theme.primary
                anchors.horizontalCenter: parent.horizontalCenter

                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    anchors.centerIn: parent
                    text: "播放"
                    color: Theme.textOnPrimary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontBody
                    font.bold: true
                }

                MouseArea {
                    id: playArea
                    anchors.fill: parent
                    onClicked: root.play()
                }
            }

            // ── 直链展示：设备自带播放器不可用时用户可手动复制 ──
            // 设备侧播放器桥是 WeiboViewerModule::openVideo(url)，
            // controller.media.playUrl(url) 最终会走这条桥；这里把解析出的地址显示出来。
            Item {
                id: urlBox
                width: parent.width - Theme.spacingSmall * 2
                height: 26
                anchors.horizontalCenter: parent.horizontalCenter
                clip: true

                Rectangle {
                    anchors.fill: parent
                    color: Theme.bgInput
                    radius: Theme.radiusSmall
                }

                Flickable {
                    id: urlFlick
                    anchors.fill: parent
                    anchors.margins: Theme.spacingTiny
                    contentWidth: width
                    contentHeight: urlText.implicitHeight
                    flickableDirection: Flickable.VerticalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    Text {
                        id: urlText
                        width: urlFlick.width
                        text: root.resolvedUrl() !== "" ? root.resolvedUrl() : "（暂无可用直链）"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }

        // 解析中
        Rectangle {
            anchors.fill: parent
            visible: root.mediaLoading()
            color: Theme.withAlpha(Theme.bgPrimary, 0.75)

            LoadingIndicator {
                anchors.centerIn: parent
                running: root.mediaLoading()
                indicatorSize: 18
                text: "正在解析媒体…"
            }
        }

        // 空：解析结束仍无任何直链（不用 LoadMoreListView，因此不涉及组件内置空态）
        EmptyState {
            anchors.fill: parent
            visible: !root.mediaLoading() && !root.hasAnyUrl() && root.mediaStatus() !== ""
            text: "该微博没有可播放的媒体"
            hint: "可以返回后重试"
            glyph: "▶"
        }
    }
}
