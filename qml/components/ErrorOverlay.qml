import QtQuick 2.12
import WeiboPlugin 1.0

// 错误浮层：bgOverlay 遮罩 + 居中卡片，提供「重试 / 关闭」。
// errorMessage 非空即显示；visible 也可由外部直接控制。
Rectangle {
    id: errorRoot
    anchors.fill: parent
    color: Theme.bgOverlay
    z: 100
    // visible 不用自引用绑定：由 opacity 收尾（淡出结束自动隐藏）。
    // 外部只设 errorMessage 即自动弹出（main.qml 就是这样用的）；
    // 也可以显式设 visible:false 关闭。
    opacity: 0
    visible: opacity > 0

    property string errorMessage: ""
    // 外部把 visible 置 false 后不再自动弹出，直到错误文案变化
    property bool _shown: true

    signal retryClicked()
    signal dismissed()

    // 有错误文案即为「有错误」
    readonly property bool hasError: errorMessage.length > 0

    onErrorMessageChanged: _sync()
    onVisibleChanged: _sync()
    Component.onCompleted: _sync()

    // 有错误文案 → 淡入；文案清空或被显式 visible:false → 淡出。
    function _sync() {
        if (hasError && errorRoot.visible) _shown = true
        var want = hasError && _shown && errorRoot.visible
        // 记住是否还在显示，避免淡出结束后 visible 变 false 再次触发同步
        _shown = want
        fade.to = want ? 1 : 0
        fade.restart()
    }

    NumberAnimation {
        id: fade
        target: errorRoot
        property: "opacity"
        duration: Theme.animSlow
        easing.type: Easing.OutCubic
    }

    // 拦截穿透点击
    MouseArea { anchors.fill: parent; onClicked: {} }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.86, 260)
        height: Math.min(parent.height - Theme.spacingMedium, errorCol.height + Theme.spacingLarge * 2)
        radius: Theme.radiusXL
        color: Theme.bgCard
        border.width: 1
        border.color: Theme.withAlpha(Theme.error, 0.3)

        Column {
            id: errorCol
            anchors.centerIn: parent
            width: parent.width - Theme.spacingLarge * 2
            spacing: Theme.spacingMedium

            Text {
                text: "⚠"
                color: Theme.error
                font.pixelSize: Theme.fontHuge
                font.family: Theme.fontFamily
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                width: parent.width
                text: errorRoot.errorMessage
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                wrapMode: Text.Wrap
                maximumLineCount: 3
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacingLarge

                Rectangle {
                    width: 62
                    height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: retryArea.pressed ? Theme.primaryDark : Theme.primary
                    scale: retryArea.pressed ? 0.94 : 1.0
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: "重试"
                        color: Theme.textOnPrimary
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontFamily
                        font.bold: true
                    }

                    MouseArea {
                        id: retryArea
                        anchors.fill: parent
                        onClicked: errorRoot.retryClicked()
                    }
                }

                Rectangle {
                    width: 62
                    height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: dismissArea.pressed ? Theme.bgTertiary : "transparent"
                    border.width: 1
                    border.color: Theme.borderLight
                    scale: dismissArea.pressed ? 0.94 : 1.0
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: "关闭"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontFamily
                    }

                    MouseArea {
                        id: dismissArea
                        anchors.fill: parent
                        onClicked: {
                            errorRoot.visible = false
                            errorRoot.dismissed()
                        }
                    }
                }
            }
        }
    }
}
