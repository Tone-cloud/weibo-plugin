.pragma library

// 时间 / 数字格式化（.pragma library）。
// 时间戳统一按「秒级」处理；对 0 / undefined / 字符串 / 毫秒级都做防御。

// 内部：把任意输入归一成秒级数值；无效返回 0（调用方视为「未知时间」）。
function _toSeconds(ts) {
    if (ts === null || ts === undefined || ts === "") return 0
    var v = Number(ts)
    if (!isFinite(v) || v <= 0) return 0
    // 毫秒级时间戳（> 1e11）自动降级
    if (v > 100000000000) v = Math.floor(v / 1000)
    return v
}

function _pad2(n) {
    return n < 10 ? "0" + n : "" + n
}

// 秒级时间戳 → "刚刚 / 5分钟前 / 3小时前 / 昨天 12:30 / 01-01 / 2024-01-01"
function relative(ts) {
    var sec = _toSeconds(ts)
    if (sec === 0) return ""
    var now = Math.floor(Date.now() / 1000)
    var diff = now - sec
    if (diff < 0) diff = 0
    if (diff < 60) return "刚刚"
    if (diff < 3600) return Math.floor(diff / 60) + "分钟前"
    if (diff < 86400) return Math.floor(diff / 3600) + "小时前"

    var d = new Date(sec * 1000)
    var n = new Date(now * 1000)
    var hm = _pad2(d.getHours()) + ":" + _pad2(d.getMinutes())
    // 用「日历日差」而不是 86400 秒差，避免 23:59 与 00:01 被判成同一天
    var day0 = new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
    var day1 = new Date(n.getFullYear(), n.getMonth(), n.getDate()).getTime()
    var dayDiff = Math.round((day1 - day0) / 86400000)
    if (dayDiff === 0) return "今天 " + hm
    if (dayDiff === 1) return "昨天 " + hm
    if (d.getFullYear() === n.getFullYear()) {
        return _pad2(d.getMonth() + 1) + "-" + _pad2(d.getDate())
    }
    return d.getFullYear() + "-" + _pad2(d.getMonth() + 1) + "-" + _pad2(d.getDate())
}

// 数字 → "1.2万"；无效 / 0 返回 "0"。
function count(n) {
    var v = Number(n)
    if (!isFinite(v) || v <= 0) return "0"
    if (v < 10000) return String(Math.floor(v))
    var s = (v / 10000.0).toFixed(1)
    if (s.slice(-2) === ".0") s = s.slice(0, -2)
    return s + "万"
}
