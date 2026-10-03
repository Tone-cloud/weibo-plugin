import QtQuick 2.12
import WeiboPlugin 1.0

// 顶部标题栏：返回 glyph `‹` + 居中标题/副标题 + 右侧文字动作按钮。
Rectangle {
    id: titleBar
    width: parent ? parent.width : 320
    height: Theme.titleBarHeight
    color: Theme.bgSecondary
    z: 10

    property string title: ""
    property string subtitle: ""
    property bool showBack: true
    property bool showAction: false
    property string actionText: ""
    property bool actionEnabled: true

    signal backClicked()
    signal actionClicked()

    // 左右两侧各自要给按钮留出的宽度，标题居中时按此收缩
    readonly property int _sideReserve: 58

    // ── 返回 ──
    Rectangle {
        id: backHit
        visible: titleBar.showBack
        width: 44
        height: parent.height
        anchors.left: parent.left
        color: "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: 36
            height: 22
            radius: Theme.radiusMedium
            color: backArea.pressed ? Theme.withAlpha(Theme.primary, 0.2) : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.animFast } }

            Row {
                anchors.centerIn: parent
                spacing: Theme.spacingTiny

                Text {
                    text: "‹"
                    color: Theme.primary
                    font.pixelSize: Theme.fontLarge
                    font.family: Theme.fontFamily
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "返回"
                    color: Theme.primary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        MouseArea {
            id: backArea
            anchors.fill: parent
            onClicked: titleBar.backClicked()
        }
    }

    // ── 标题 + 副标题（居中，按两侧预留宽度收缩）──
    Row {
        id: titleGroup
        anchors.centerIn: parent
        width: Math.min(parent.width - titleBar._sideReserve * 2, titleText.implicitWidth)
        height: parent.height
        spacing: Theme.spacingSmall

        Text {
            id: titleText
            text: titleBar.title
            width: Math.max(0, titleGroup.width - (subtitleText.visible ? titleGroup.spacing + subtitleText.width : 0))
            color: Theme.textPrimary
            font.pixelSize: Theme.fontMedium
            font.family: Theme.fontFamily
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 1
            horizontalAlignment: subtitleText.visible ? Text.AlignRight : Text.AlignHCenter
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: subtitleText
            visible: titleBar.subtitle.length > 0
            text: titleBar.subtitle
            width: Math.min(implicitWidth, 60)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // ── 右侧动作 ──
    Rectangle {
        id: actionHit
        visible: titleBar.showAction
        width: 56
        height: parent.height
        anchors.right: parent.right
        color: "transparent"

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(52, actionLabel.implicitWidth + Theme.spacingLarge)
            height: 22
            radius: Theme.radiusMedium
            color: actionArea.pressed && titleBar.actionEnabled
                   ? Theme.withAlpha(Theme.primary, 0.22)
                   : Theme.withAlpha(Theme.primary, titleBar.actionEnabled ? 0.1 : 0.04)

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: titleBar.actionText
                color: titleBar.actionEnabled ? Theme.primary : Theme.textTertiary
                font.pixelSize: Theme.fontSmall
                font.family: Theme.fontFamily
                font.bold: true
            }
        }

        MouseArea {
            id: actionArea
            anchors.fill: parent
            onClicked: if (titleBar.actionEnabled) titleBar.actionClicked()
        }
    }

    // ── 底部分割线（中间亮、两端淡出）──
    Rectangle {
        width: parent.width
        height: 1
        anchors.bottom: parent.bottom
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.3; color: Theme.withAlpha(Theme.primary, 0.28) }
            GradientStop { position: 0.7; color: Theme.withAlpha(Theme.primary, 0.28) }
            GradientStop { position: 1.0; color: "transparent" }
        }
    }
}
