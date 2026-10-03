import QtQuick 2.12
import WeiboPlugin 1.0

// 搜索输入框：`⌕` 字形 + 单行输入 + 清空 + 可选取消按钮。
Item {
    id: searchInput
    width: parent ? parent.width : 300
    height: 24

    property string text: ""
    property string placeholder: "搜索微博"
    property bool showCancel: true
    // 契约外可选：输入框右侧为取消键预留的宽度
    property int cancelWidth: 40

    signal textEdited(string text)
    signal accepted(string text)
    signal cancelClicked()

    property bool _syncing: false

    onTextChanged: {
        _syncing = true
        if (input.text !== searchInput.text) input.text = searchInput.text
        _syncing = false
    }

    Rectangle {
        id: box
        anchors.left: parent.left
        anchors.right: searchInput.showCancel ? cancelBtn.left : parent.right
        anchors.rightMargin: searchInput.showCancel ? Theme.spacingSmall : 0
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height
        radius: Theme.radiusRound
        color: Theme.bgInput
        border.width: 1
        border.color: input.activeFocus ? Theme.withAlpha(Theme.primary, 0.6) : Theme.border

        Text {
            id: searchGlyph
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter
            text: "⌕"
            color: Theme.textTertiary
            font.pixelSize: Theme.fontMedium
            font.family: Theme.fontFamily
        }

        Text {
            anchors.left: searchGlyph.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: clearBtn.visible ? clearBtn.left : parent.right
            anchors.rightMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0 && searchInput.placeholder.length > 0
            text: searchInput.placeholder
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        TextInput {
            id: input
            anchors.left: searchGlyph.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: clearBtn.visible ? clearBtn.left : parent.right
            anchors.rightMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            text: searchInput.text
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            selectByMouse: false
            clip: true
            onTextChanged: {
                if (searchInput._syncing) return
                if (searchInput.text !== input.text) searchInput.text = input.text
                searchInput.textEdited(input.text)
            }
            onAccepted: searchInput.accepted(searchInput.text)
        }

        // 清空
        Rectangle {
            id: clearBtn
            visible: input.text.length > 0
            width: 14
            height: 14
            radius: width / 2
            color: clearArea.pressed ? Theme.withAlpha(Theme.primary, 0.3) : Theme.borderLight
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter

            Text {
                anchors.centerIn: parent
                text: "×"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                font.bold: true
            }

            MouseArea {
                id: clearArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingSmall
                onClicked: {
                    input.text = ""
                    searchInput.text = ""
                    searchInput.textEdited("")
                }
            }
        }
    }

    // 取消
    Rectangle {
        id: cancelBtn
        visible: searchInput.showCancel
        width: Math.max(Theme.touchMinSize, searchInput.cancelWidth)
        height: parent.height
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        radius: Theme.radiusRound
        color: cancelArea.pressed ? Theme.withAlpha(Theme.primary, 0.22) : "transparent"

        Text {
            anchors.centerIn: parent
            text: "取消"
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSmall
            font.family: Theme.fontFamily
        }

        MouseArea {
            id: cancelArea
            anchors.fill: parent
            onClicked: searchInput.cancelClicked()
        }
    }
}
