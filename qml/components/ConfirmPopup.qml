import QtQuick 2.12
import WeiboPlugin 1.0

// 确认对话框（不用 QtQuick.Controls.Popup）：遮罩 + 深色圆角面板 + 取消/确认。
// danger=true 时确认键用 error 色（删除等破坏性操作）。
Item {
    id: confirmRoot
    anchors.fill: parent
    z: 600
    // 未显示时不可见也不拦截触摸
    visible: false
    enabled: false

    property alias title: titleLabel.text
    property alias message: messageLabel.text
    property string confirmText: "确定"
    property string cancelText: "取消"
    property bool danger: false

    signal confirmed()
    signal cancelled()

    onVisibleChanged: {
        confirmRoot.enabled = confirmRoot.visible
        if (confirmRoot.visible) showAnim.restart()
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgOverlay

        MouseArea {
            anchors.fill: parent
            onClicked: confirmRoot._cancel()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacingLarge * 2, 180)
        height: panelCol.implicitHeight + Theme.spacingLarge * 2
        radius: Theme.radiusLarge
        color: Theme.bgCard
        border.width: 1
        border.color: confirmRoot.danger ? Theme.withAlpha(Theme.error, 0.35) : Theme.borderLight
        opacity: 0
        scale: 0.94

        MouseArea { anchors.fill: parent; onClicked: {} }

        Column {
            id: panelCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingLarge
            anchors.rightMargin: Theme.spacingLarge
            spacing: Theme.spacingNormal

            Text {
                id: titleLabel
                width: parent.width
                visible: text.length > 0
                color: Theme.textPrimary
                font.pixelSize: Theme.fontMedium
                font.family: Theme.fontFamily
                font.bold: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                id: messageLabel
                width: parent.width
                color: Theme.textSecondary
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
                    width: 66
                    height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: cancelArea.pressed ? Theme.bgTertiary : "transparent"
                    border.width: 1
                    border.color: Theme.borderLight
                    scale: cancelArea.pressed ? 0.94 : 1.0
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: confirmRoot.cancelText
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontFamily
                    }

                    MouseArea {
                        id: cancelArea
                        anchors.fill: parent
                        onClicked: confirmRoot._cancel()
                    }
                }

                Rectangle {
                    width: 66
                    height: Theme.buttonHeight
                    radius: Theme.radiusRound
                    color: confirmRoot.danger
                           ? (confirmArea.pressed ? Theme.darken(Theme.error, 0.15) : Theme.error)
                           : (confirmArea.pressed ? Theme.primaryDark : Theme.primary)
                    scale: confirmArea.pressed ? 0.94 : 1.0
                    Behavior on color { ColorAnimation { duration: Theme.animFast } }
                    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

                    Text {
                        anchors.centerIn: parent
                        text: confirmRoot.confirmText
                        color: Theme.textOnPrimary
                        font.pixelSize: Theme.fontBody
                        font.family: Theme.fontFamily
                        font.bold: true
                    }

                    MouseArea {
                        id: confirmArea
                        anchors.fill: parent
                        onClicked: {
                            confirmRoot.visible = false
                            confirmRoot.confirmed()
                        }
                    }
                }
            }
        }
    }

    function _cancel() {
        visible = false
        cancelled()
    }

    // 对外：打开
    function open() {
        visible = true
    }

    function close() {
        visible = false
    }

    ParallelAnimation {
        id: showAnim
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
            from: 0.94
            to: 1.0
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
    }
}
