.pragma library

// 富文本工具库（.pragma library）：HTML 转义 + @提及 / #话题# / http 链接化。
// 库内取不到 Theme，linkColor / topicColor 必须由调用方传入。
// 关键：先转义、再在「已转义文本」上匹配，锚点显示文本直接复用已转义片段
//        （可安全二次转义）；href 用未转义的原始片段 + encodeURIComponent。

var DEFAULT_LINK_COLOR = "#FFA033"

// 直通判断：已经是 HTML 片段的内容不再转义 / 链接化。
function _isPassthrough(text) {
    if (!text) return false
    var s = String(text)
    return s.indexOf("<a ") >= 0 || s.indexOf("<br") >= 0 || s.indexOf("<img ") >= 0
}

// 正文转义（& < > 顺序敏感，& 必须最先）。
function escapeRichText(text) {
    if (text === null || text === undefined) return ""
    var s = String(text)
    s = s.replace(/&/g, "&amp;")
    s = s.replace(/</g, "&lt;")
    s = s.replace(/>/g, "&gt;")
    return s
}

// HTML 属性转义（& " < >，& 必须最先）。
function escapeHtmlAttribute(text) {
    if (text === null || text === undefined) return ""
    var s = String(text)
    s = s.replace(/&/g, "&amp;")
    s = s.replace(/"/g, "&quot;")
    s = s.replace(/</g, "&lt;")
    s = s.replace(/>/g, "&gt;")
    return s
}

// 内部：把「原始（未转义）token」变成 <a> 锚点。
// 显示文本在这里转义一次；href 由原始文本拼出后做属性转义。
function _anchor(displayRaw, href, color) {
    return "<a href=\"" + escapeHtmlAttribute(href) + "\" style=\"color:" + color +
            ";text-decoration:none;\">" + escapeRichText(displayRaw) + "</a>"
}

// 内部：逐 token 链接化。escaped 必须已是转义后的文本。
// 匹配顺序（先长后短，靠单个交替正则保证不会重叠）：
//   1. https?://\S+   2. @提及   3. #话题#
function _linkifyEscaped(escaped, linkColor, topicColor) {
    var color = linkColor || DEFAULT_LINK_COLOR
    var tcolor = topicColor || color
    var pattern = /https?:\/\/\S+|@[\w\u4e00-\u9fa5\-]{1,30}|#([^#\n]{1,40})#/g
    var out = ""
    var last = 0
    var m
    while ((m = pattern.exec(escaped)) !== null) {
        var start = m.index
        var token = m[0]
        out += escaped.slice(last, start)
        // 还原成「原始文本」：显示文本与 href 都基于它计算（各自转义一次）
        var raw = token.replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">")
        if (token.indexOf("http") === 0) {
            out += _anchor(raw, raw, color)
        } else if (token.charAt(0) === "@") {
            out += _anchor(raw, "weibo://user?name=" + encodeURIComponent(raw.slice(1)), color)
        } else {
            // #话题#：m[1] 是话题名（不含两侧 #），显示文本保留 #...#。
            var name = m[1] ? m[1] : token.replace(/^#+|#+$/g, "")
            out += _anchor(raw, "weibo://topic?name=" + encodeURIComponent(name), tcolor)
        }
        last = start + token.length
    }
    out += escaped.slice(last)
    return out.replace(/\r\n/g, "<br>").replace(/\n/g, "<br>").replace(/\r/g, "<br>")
}

// 转义 + 链接化；linkColor 也作为话题色。
function linkify(text, linkColor) {
    if (text === null || text === undefined) return ""
    var s = String(text)
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    return _linkifyEscaped(escapeRichText(s), linkColor, linkColor)
}

// linkify 增强版：#话题# 使用独立的 topicColor。
function richText(text, linkColor, topicColor) {
    if (text === null || text === undefined) return ""
    var s = String(text)
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    return _linkifyEscaped(escapeRichText(s), linkColor, topicColor)
}
