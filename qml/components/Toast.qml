import QtQuick 2.12
import WeiboPlugin 1.0

// 底部居中的轻量提示：show(message, duration) 弹出并在 duration 后淡出。
// 正在显示时再次调用采取「排队」策略（最多缓存 2 条，超出丢最旧）。
Rectangle {
    id: toast
    width: Math.min(toastText.implicitWidth + Theme.spacingXL * 2,
                    parent ? parent.width - Theme.spacingLarge * 2 : 200)
    height: 24
    radius: Theme.radiusRound
    color: Theme.withAlpha(Theme.bgTertiary, 0.96)
    border.width: 1
    border.color: Theme.withAlpha(Theme.primary, 0.22)
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    anchors.bottom: parent ? parent.bottom : undefined
    anchors.bottomMargin: Theme.spacingLarge
    visible: false
    opacity: 0
    scale: 0.9
    z: 200

    property var queue: []

    // 立即切换文案并重新计时（内部使用）
    function _display(message, duration) {
        fadeOut.stop()
        toastText.text = message === undefined || message === null ? "" : String(message)
        toast.visible = true
        fadeIn.restart()
        hideTimer.interval = duration > 0 ? duration : 2000
        hideTimer.restart()
    }

    // 对外接口：duration 省略时 2000ms
    function show(message, duration) {
        var d = (duration === undefined || duration === null || duration <= 0) ? 2000 : duration
        if (toast.visible) {
            queue.push({ message: message, duration: d })
            if (queue.length > 2) queue.shift()
            return
        }
        _display(message, d)
    }

    // 立刻隐藏（页面切换时可用）
    function hide() {
        queue = []
        hideTimer.stop()
        fadeOut.restart()
    }

    Text {
        id: toastText
        anchors.centerIn: parent
        width: parent.width - Theme.spacingMedium
        text: ""
        color: Theme.textPrimary
        font.pixelSize: Theme.fontBody
        font.family: Theme.fontFamily
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        maximumLineCount: 1
    }

    ParallelAnimation {
        id: fadeIn
        NumberAnimation {
            target: toast
            property: "opacity"
            from: 0
            to: 1
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: toast
            property: "scale"
            from: 0.9
            to: 1.0
            duration: Theme.animNormal
            easing.type: Easing.OutCubic
        }
    }

    ParallelAnimation {
        id: fadeOut
        NumberAnimation {
            target: toast
            property: "opacity"
            from: toast.opacity
            to: 0
            duration: Theme.animSlow
            easing.type: Easing.InCubic
        }
        NumberAnimation {
            target: toast
            property: "scale"
            from: toast.scale
            to: 0.9
            duration: Theme.animSlow
            easing.type: Easing.InCubic
        }
        onFinished: {
            if (toast.opacity <= 0.01) {
                toast.visible = false
                if (queue.length > 0) {
                    var next = queue.shift()
                    _display(next.message, next.duration)
                }
            }
        }
    }

    Timer {
        id: hideTimer
        interval: 2000
        repeat: false
        onTriggered: fadeOut.restart()
    }
}
