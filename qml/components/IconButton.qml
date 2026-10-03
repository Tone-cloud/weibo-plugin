import QtQuick 2.12
import WeiboPlugin 1.0

// 通用图标按钮：Unicode 字形 + 可选文字标签，支持高亮（选中）态。
Rectangle {
    id: btn
    width: Math.max(Theme.touchMinSize, innerRow.width + Theme.spacingMedium * 2)
    height: buttonSize
    radius: Theme.radiusMedium
    color: btnArea.pressed && btn.enabled
           ? Theme.withAlpha(Theme.primary, 0.22)
           : (btn.highlight ? Theme.withAlpha(Theme.primary, 0.12) : "transparent")
    opacity: btn.enabled ? 1.0 : 0.45
    scale: btnArea.pressed && btn.enabled ? 0.92 : 1.0
    Behavior on color { ColorAnimation { duration: Theme.animFast } }
    Behavior on scale { NumberAnimation { duration: Theme.animFast } }

    property string glyph: ""
    property string label: ""
    property int buttonSize: Theme.touchMinSize
    property bool enabled: true
    property bool highlight: false

    signal clicked()

    Row {
        id: innerRow
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        Text {
            text: btn.glyph
            color: btn.highlight ? Theme.primary : (btn.enabled ? Theme.textSecondary : Theme.textTertiary)
            font.pixelSize: Theme.fontMedium
            font.family: Theme.fontFamily
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            text: btn.label
            visible: text.length > 0
            color: btn.highlight ? Theme.primary : (btn.enabled ? Theme.textSecondary : Theme.textTertiary)
            font.pixelSize: Theme.fontSmall
            font.family: Theme.fontFamily
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        id: btnArea
        anchors.fill: parent
        onClicked: if (btn.enabled) btn.clicked()
    }
}
