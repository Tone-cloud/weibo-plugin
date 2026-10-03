import QtQuick 2.12
import WeiboPlugin 1.0

// 骨架屏占位条：圆角矩形 + 横向渐变高光循环扫过（不用 QtGraphicalEffects）。
Rectangle {
    id: pill
    property int pillWidth: 60
    property int pillHeight: 8
    // 用独立名字避免覆盖 Rectangle.radius 的用法歧义
    property int pillRadius: Theme.radiusSmall

    width: pillWidth
    height: pillHeight
    radius: pillRadius
    color: Theme.withAlpha(Theme.textTertiary, 0.16)
    clip: true

    Rectangle {
        id: shimmer
        width: Math.max(12, Math.round(pill.width * 0.35))
        height: pill.height
        y: 0
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: "transparent" }
            GradientStop { position: 0.5; color: Theme.withAlpha(Theme.textPrimary, 0.09) }
            GradientStop { position: 1.0; color: "transparent" }
        }

        NumberAnimation on x {
            running: pill.visible
            loops: Animation.Infinite
            from: -shimmer.width
            to: pill.width
            duration: 1100
            easing.type: Easing.InOutSine
        }
    }
}
