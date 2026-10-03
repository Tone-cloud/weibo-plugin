pragma Singleton
import QtQuick 2.12

// 全局主题单例：颜色 / 字号 / 间距 / 圆角 / 动画。
// 组件里禁止硬编码下列数值，一律走 Theme.*。
//
// 位置：qml/components/Theme.qml（由 qml/components/qmldir 声明为 singleton）。
// 因此所有相对路径都以 qml/components/ 为基准 —— 字体在 qml/fonts/ 下，
// 所以是 "../fonts/msyh.ttf"（改路径时别忘了这一点）。
Item {
    // 中文字体：qml/fonts/msyh.ttf（Microsoft YaHei，与 bili 插件同一份文件）。
    // 词典笔自带的字体不含中文字形，不加载它中文会显示成方块。
    // FontLoader 是异步的：刚加载完 appFont.name 可能还是 ""，
    // 此时 fontFamily 回退到 "Microsoft YaHei"（正好就是这个字体的族名，
    // 系统里装了同名字体时也能命中）。
    FontLoader {
        id: appFont
        source: "../fonts/msyh.ttf"
    }

    // ── 品牌色 ──
    readonly property color primary: "#FF8200"
    readonly property color primaryLight: "#FFA033"
    readonly property color primaryDark: "#E06E00"
    readonly property color primaryGlow: "#1AFF8200"
    readonly property color accent: "#E6162D"
    readonly property color accentDark: "#C1121F"
    readonly property color verifiedYellow: "#FFAC2D"
    readonly property color verifiedBlue: "#3C8DBC"

    // ── 背景色 ──
    readonly property color bgPrimary: "#0D0D0D"
    readonly property color bgSecondary: "#161616"
    readonly property color bgTertiary: "#242424"
    readonly property color bgCard: "#1A1A1A"
    readonly property color bgCardHover: "#262626"
    readonly property color bgInput: "#2C2C2C"
    readonly property color bgOverlay: "#CC000000"

    // ── 文字色 ──
    readonly property color textPrimary: "#F0F0F0"
    readonly property color textSecondary: "#A0A0A0"
    readonly property color textTertiary: "#666666"
    readonly property color textOnPrimary: "#FFFFFF"
    readonly property color textLink: "#FFA033"
    readonly property color textTopic: "#4A9EFF"

    // ── 边框 ──
    readonly property color border: "#2E2E2E"
    readonly property color borderLight: "#3A3A3A"
    readonly property color divider: "#1F1F1F"

    // ── 状态色 ──
    readonly property color error: "#FF5252"
    readonly property color success: "#4CAF50"
    readonly property color warning: "#FFA726"

    // 与 js/RichText.js 的 DEFAULT_LINK_COLOR 保持一致（Canvas 里取不到 color 时用）。
    readonly property string richTextLinkColor: "#FFA033"
    // 正文块等需要比 bgOverlay 稍浅的底板。
    readonly property color bgOverlayLight: "#99000000"

    // ── 字号（320×170 优化）──
    readonly property int fontTiny: 7
    readonly property int fontSmall: 8
    readonly property int fontBody: 9
    readonly property int fontNormal: 10
    readonly property int fontMedium: 11
    readonly property int fontLarge: 13
    readonly property int fontTitle: 14
    readonly property int fontHuge: 18

    // ── 间距 ──
    readonly property int spacingTiny: 2
    readonly property int spacingSmall: 4
    readonly property int spacingNormal: 6
    readonly property int spacingMedium: 8
    readonly property int spacingLarge: 12
    readonly property int spacingXL: 16

    // ── 圆角 ──
    readonly property int radiusTiny: 2
    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 6
    readonly property int radiusLarge: 10
    readonly property int radiusXL: 14
    readonly property int radiusRound: 999

    // ── 列表 / 卡片 ──
    readonly property int cardWidth: 105
    readonly property int listCacheBuffer: 640
    readonly property int listDisplayMargin: 160

    // ── 触摸尺寸 ──
    readonly property int touchMinSize: 28
    readonly property int buttonHeight: 24
    readonly property int buttonHeightLarge: 30

    // ── 标题栏 ──
    readonly property int titleBarHeight: 28

    // ── 动画时长 ──
    readonly property int animFast: 120
    readonly property int animNormal: 200
    readonly property int animSlow: 350
    readonly property int animPage: 300

    // ── 字体族 ──
    readonly property string fontFamily: appFont.name !== "" ? appFont.name : "Microsoft YaHei"

    // ── 工具函数 ──
    // 把颜色换成指定透明度。
    function withAlpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // 变亮：factor 0.1 ≈ 10%（保底至少微亮一点）。
    function lighten(c, factor) {
        return Qt.lighter(c, 1.0 + factor)
    }

    // 变暗：factor 0.1 ≈ 10%。
    function darken(c, factor) {
        return Qt.darker(c, 1.0 + factor)
    }

    // 数字 → "1.2万" / "1234"（与 TimeText.count 保持一致）。
    function countText(n) {
        var v = Number(n)
        if (!isFinite(v) || v <= 0) return "0"
        if (v < 10000) return String(Math.floor(v))
        var w = v / 10000.0
        var s = w.toFixed(1)
        if (s.slice(-2) === ".0") s = s.slice(0, -2)
        return s + "万"
    }

    // 认证色：0 个人黄 V，2 / 7 机构蓝 V，其余灰。
    function verifiedColor(type) {
        var t = Number(type)
        if (t === 0) return verifiedYellow
        if (t === 2 || t === 7) return verifiedBlue
        return textTertiary
    }

    // 热搜标签配色：爆/沸 → 红，新/热 → 橙，其余灰。
    function labelFor(label) {
        var s = label ? String(label) : ""
        if (s === "爆" || s === "沸") return accent
        if (s === "新" || s === "热") return warning
        return textTertiary
    }

    // 认证标记字形（Canvas 画不出的场景可退化为字符）。
    function verifiedGlyph(type) {
        return Number(type) === 0 ? "V" : "✓"
    }
}
