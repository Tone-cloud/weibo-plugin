import QtQuick 2.12
import WeiboPlugin 1.0

// 多行文本输入：bgInput 底 + 内部 Flickable 滚动 + 右下角字数统计。
Item {
    id: areaInput
    width: parent ? parent.width : 300
    height: 60

    property string text: ""
    property string placeholder: "分享新鲜事..."
    property int maxLength: 2000
    property int lineHeight: 14
    property bool showCounter: true

    signal textEdited(string text)
    signal accepted(string text)

    property bool _syncing: false

    onTextChanged: {
        _syncing = true
        if (input.text !== areaInput.text) input.text = areaInput.text
        _syncing = false
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusMedium
        color: Theme.bgInput
        border.width: 1
        border.color: input.activeFocus ? Theme.withAlpha(Theme.primary, 0.6) : Theme.border

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            visible: input.text.length === 0
            text: areaInput.placeholder
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        Flickable {
            id: flick
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: counter.visible ? counter.top : parent.bottom
            anchors.leftMargin: Theme.spacingNormal
            anchors.rightMargin: Theme.spacingNormal
            anchors.topMargin: Theme.spacingSmall
            anchors.bottomMargin: Theme.spacingTiny
            clip: true
            contentWidth: width
            contentHeight: Math.max(input.implicitHeight, height)
            boundsBehavior: Flickable.StopAtBounds

            TextInput {
                id: input
                width: flick.width
                text: areaInput.text
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                lineHeight: areaInput.lineHeight
                wrapMode: TextEdit.Wrap
                selectByMouse: false
                clip: true
                onTextChanged: {
                    if (areaInput._syncing) return
                    var v = input.text
                    if (areaInput.maxLength > 0 && v.length > areaInput.maxLength) {
                        v = v.slice(0, areaInput.maxLength)
                        areaInput._syncing = true
                        input.text = v
                        areaInput._syncing = false
                    }
                    if (areaInput.text !== v) areaInput.text = v
                    areaInput.textEdited(v)
                }
                onAccepted: areaInput.accepted(areaInput.text)
            }
        }

        // 字数统计：接近上限转 warning / error
        Text {
            id: counter
            visible: areaInput.showCounter
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: Theme.spacingNormal
            anchors.bottomMargin: Theme.spacingTiny
            text: areaInput.text.length + "/" + areaInput.maxLength
            color: areaInput.text.length >= areaInput.maxLength
                   ? Theme.error
                   : (areaInput.text.length > areaInput.maxLength * 0.9 ? Theme.warning : Theme.textTertiary)
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
        }
    }
}
