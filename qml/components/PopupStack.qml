import QtQuick 2.12
import WeiboPlugin 1.0

// 通用弹出面板（不用 QtQuick.Controls.Popup）：bgOverlay 遮罩 + 居中深色圆角面板，
// items 形如 [{text, danger}]，点某项发 picked(index)，点遮罩或面板空白发 closed()。
Item {
    id: popupRoot
    anchors.fill: parent
    z: 500
    visible: open
    // 关闭后不拦截触摸
    enabled: false

    property bool open: false
    property var items: []
    property string title: ""

    signal picked(int index)
    signal closed()

    // 面板中心相对自身中心的偏移（默认略偏上，避免遮住底部操作行）
    property real popupOffsetY: -6

    onOpenChanged: {
        popupRoot.enabled = popupRoot.open
        if (popupRoot.open) {
            showAnim.restart()
        } else {
            hideAnim.restart()
        }
    }

    // 遮罩
    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Theme.bgOverlay
        opacity: 0

        MouseArea {
            anchors.fill: parent
            onClicked: popupRoot._close()
        }
    }

    // 面板
    Rectangle {
        id: panel
        anchors.centerIn: parent
        anchors.verticalCenterOffset: popupRoot.popupOffsetY
        width: Math.min(parent.width - Theme.spacingLarge * 2, 150)
        height: panelCol.implicitHeight + Theme.spacingNormal * 2
        radius: Theme.radiusLarge
        color: Theme.bgCard
        border.width: 1
        border.color: Theme.borderLight
        opacity: 0
        scale: 0.92

        // 拦截面板上的空白点击（不关闭）
        MouseArea { anchors.fill: parent; onClicked: {} }

        Column {
            id: panelCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingTiny

            Text {
                width: parent.width
                visible: popupRoot.title.length > 0
                text: popupRoot.title
                color: Theme.textTertiary
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
                maximumLineCount: 1
                horizontalAlignment: Text.AlignHCenter
                bottomPadding: Theme.spacingTiny
            }

            Repeater {
                model: popupRoot.items

                Rectangle {
                    width: panelCol.width
                    height: 22
                    radius: Theme.radiusSmall
                    color: itemArea.pressed
                           ? Theme.withAlpha(Theme.primary, 0.18)
                           : Theme.bgTertiary

                    readonly property bool danger: modelData && modelData.danger === true
                    readonly property string itemText: modelData && modelData.text !== undefined
                                                     ? String(modelData.text) : ""

                    Text {
                        anchors.centerIn: parent
                        text: parent.itemText
                        color: parent.danger ? Theme.error : Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontFamily
                    }

                    MouseArea {
                        id: itemArea
                        anchors.fill: parent
                        onClicked: {
                            popupRoot.picked(index)
                            popupRoot._close()
                        }
                    }
                }
            }
        }
    }

    function _close() {
        open = false
        closed()
    }

    // 对外：打开面板
    function show() {
        open = true
    }

    function close() {
        if (open) _close()
    }

    ParallelAnimation {
        id: showAnim
        NumberAnimation {
            target: scrim
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.animFast
        }
        NumberAnimation {
            target: panel
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: panel
            property: "scale"
            from: 0.92
            to: 1.0
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
    }

    ParallelAnimation {
        id: hideAnim
        NumberAnimation {
            target: scrim
            property: "opacity"
            from: scrim.opacity
            to: 0
            duration: Theme.animFast
        }
        NumberAnimation {
            target: panel
            property: "opacity"
            from: panel.opacity
            to: 0
            duration: Theme.animFast
        }
        NumberAnimation {
            target: panel
            property: "scale"
            from: panel.scale
            to: 0.92
            duration: Theme.animFast
        }
    }

    // 初始（open 直接为 true 时）也要有可见状态
    Component.onCompleted: if (open) showAnim.restart()
}
