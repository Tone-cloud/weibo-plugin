import QtQuick 2.12
import WeiboPlugin 1.0

// 单行输入框 + 屏幕内虚拟键盘面板（不依赖系统键盘 / QtQuick.Controls）。
// 键盘直接挂在组件下方，方便页面按 320×170 紧凑布局。
Item {
    id: vkInput
    width: parent ? parent.width : 300
    height: field.height + (keyboard.visible ? keyboard.height : 0)

    property string text: ""
    property string placeholder: ""
    property bool cjkOnly: false
    // 契约外可选：0 表示不限长
    property int maxLength: 0
    // 是否展开键盘面板（也可由宿主绑定到输入焦点）
    property bool keyboardVisible: false

    signal textEdited(string text)
    signal accepted(string text)

    // 防止 text 回写与用户输入互相触发
    property bool _syncing: false

    function _filter(s) {
        var raw = s === null || s === undefined ? "" : String(s)
        var out = raw
        if (cjkOnly) {
            var buf = ""
            for (var i = 0; i < raw.length; ++i) {
                var ch = raw.charAt(i)
                if (/[\u4e00-\u9fa5\u3000-\u303f\uff00-\uffef]/.test(ch)) buf += ch
            }
            out = buf
        }
        if (maxLength > 0 && out.length > maxLength) out = out.slice(0, maxLength)
        return out
    }

    // 追加一段文本（键盘按键调用）
    function append(chunk) {
        var next = _filter(vkInput.text + chunk)
        if (next !== vkInput.text) vkInput.text = next
    }

    function backspace() {
        if (vkInput.text.length === 0) return
        vkInput.text = vkInput.text.slice(0, vkInput.text.length - 1)
    }

    function clear() {
        vkInput.text = ""
    }

    function toggleKeyboard() {
        keyboardVisible = !keyboardVisible
    }

    // 设置并同步（外部用 setText 可确保内部控件刷新）
    function setText(value) {
        vkInput.text = value === undefined || value === null ? "" : String(value)
    }

    onTextChanged: {
        _syncing = true
        if (input.text !== vkInput.text) input.text = vkInput.text
        _syncing = false
    }

    // 光标闪烁
    property bool _caretOn: true
    Timer {
        interval: 500
        repeat: true
        running: input.activeFocus
        onTriggered: vkInput._caretOn = !vkInput._caretOn
    }

    // ── 输入框 ──
    Rectangle {
        id: field
        width: parent.width
        height: 22
        radius: Theme.radiusMedium
        color: Theme.bgInput
        border.width: 1
        border.color: input.activeFocus ? Theme.withAlpha(Theme.primary, 0.6) : Theme.border

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter
            visible: input.text.length === 0 && vkInput.placeholder.length > 0
            text: vkInput.placeholder
            color: Theme.textTertiary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        TextInput {
            id: input
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingNormal
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingNormal
            anchors.verticalCenter: parent.verticalCenter
            text: vkInput.text
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            selectByMouse: false
            // 虚拟键盘自绘光标：关闭系统光标，用竖线表示
            cursorVisible: false
            clip: true
            onTextChanged: {
                if (vkInput._syncing) return
                var filtered = vkInput._filter(input.text)
                if (filtered !== input.text) {
                    vkInput._syncing = true
                    input.text = filtered
                    vkInput._syncing = false
                }
                if (vkInput.text !== filtered) vkInput.text = filtered
                vkInput.textEdited(filtered)
            }
            onAccepted: vkInput.accepted(vkInput.text)
        }

        // 自绘光标（贴在已输入文本右侧）
        Rectangle {
            visible: input.activeFocus && vkInput._caretOn
            width: 1
            height: Math.max(8, field.height - Theme.spacingNormal)
            color: Theme.primary
            x: Math.min(field.width - 2, input.x + input.contentWidth + 1)
            anchors.verticalCenter: parent.verticalCenter
        }

        MouseArea {
            anchors.fill: parent
            onClicked: {
                input.forceActiveFocus()
                vkInput.keyboardVisible = true
            }
        }
    }

    // ── 虚拟键盘 ──
    Rectangle {
        id: keyboard
        visible: vkInput.keyboardVisible
        anchors.top: field.bottom
        anchors.topMargin: Theme.spacingTiny
        width: parent.width
        height: rows.implicitHeight + Theme.spacingSmall * 2
        radius: Theme.radiusMedium
        color: Theme.bgTertiary
        border.width: 1
        border.color: Theme.border

        // "字母" / "符号" 两套键位
        property bool symbolMode: false
        property var letterRows: [
            ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
            ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
            ["#", "z", "x", "c", "v", "b", "n", "m", "⌫", "↵"]
        ]
        property var symbolRows: [
            ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
            ["-", "/", ":", ";", "(", ")", "￥", "&", "@"],
            ["#", ".", ",", "?", "!", "'", "\"", "⌫", "↵"]
        ]

        Column {
            id: rows
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacingSmall
            anchors.rightMargin: Theme.spacingSmall
            spacing: Theme.spacingTiny

            Repeater {
                model: keyboard.symbolMode ? keyboard.symbolRows : keyboard.letterRows

                Row {
                    width: rows.width
                    height: 13
                    spacing: Theme.spacingTiny

                    Repeater {
                        model: modelData

                        Rectangle {
                            width: (parent.width - parent.spacing * (parent.children.length - 1))
                                   / Math.max(1, parent.children.length)
                            height: 13
                            radius: Theme.radiusTiny
                            color: keyPress.pressed
                                   ? Theme.withAlpha(Theme.primary, 0.3)
                                   : (modelData === "↵"
                                      ? Theme.withAlpha(Theme.primary, 0.18)
                                      : Theme.bgInput)

                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                color: modelData === "↵" ? Theme.primary : Theme.textPrimary
                                font.pixelSize: Theme.fontTiny
                                font.family: Theme.fontFamily
                                font.bold: modelData === "↵"
                            }

                            MouseArea {
                                id: keyPress
                                anchors.fill: parent
                                onClicked: vkInput._handleKey(modelData)
                            }
                        }
                    }
                }
            }
        }
    }

    // 键位分派：# 键在字母模式下切换符号表；⌫ 退格；↵ 提交
    function _handleKey(key) {
        if (key === "⌫") {
            backspace()
        } else if (key === "↵") {
            accepted(vkInput.text)
        } else if (key === "#") {
            keyboard.symbolMode = !keyboard.symbolMode
        } else {
            append(key)
        }
    }
}
