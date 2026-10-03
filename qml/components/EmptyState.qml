import QtQuick 2.12
import WeiboPlugin 1.0

// 空状态占位：大字字形 + 主文案 + 提示 + 可选操作按钮。
Item {
    id: emptyRoot
    width: parent ? parent.width : 200
    height: contentColumn.implicitHeight + Theme.spacingLarge * 2

    property string text: "暂无内容"
    property string hint: ""
    property string glyph: "▣"
    // 有 actionText 才显示按钮
    property string actionText: ""

    signal actionClicked()

    Column {
        id: contentColumn
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacingLarge * 2, 240)
        spacing: Theme.spacingNormal

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: emptyRoot.glyph
            visible: text.length > 0
            color: Theme.withAlpha(Theme.textTertiary, 0.85)
            font.pixelSize: Theme.fontHuge
            font.family: Theme.fontFamily
        }

        Text {
            width: parent.width
            text: emptyRoot.text
            color: Theme.textSecondary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Text {
            width: parent.width
            text: emptyRoot.hint
            visible: text.length > 0
            color: Theme.textTertiary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Rectangle {
            visible: emptyRoot.actionText.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            width: actionLabel.implicitWidth + Theme.spacingXL
            height: Theme.buttonHeight
            radius: Theme.radiusRound
            color: actionArea.pressed ? Theme.primaryDark : Theme.primary
            scale: actionArea.pressed ? 0.94 : 1.0
            Behavior on color { ColorAnimation { duration: Theme.animFast } }
            Behavior on scale { NumberAnimation { duration: Theme.animFast } }

            Text {
                id: actionLabel
                anchors.centerIn: parent
                text: emptyRoot.actionText
                color: Theme.textOnPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                font.bold: true
            }

            MouseArea {
                id: actionArea
                anchors.fill: parent
                onClicked: emptyRoot.actionClicked()
            }
        }
    }
}
