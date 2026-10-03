.pragma library

// 微博图片 URL 工具（.pragma library，components/pages 共享）。
// data:image/... 与 image://... 一律直通原样返回，绝不能再包一层 provider。
// 微博 CDN 形如 wx1.sinaimg.cn/large/<id>.jpg，尺寸后缀是 @120w_120h（可带 ?query）。

// 直通判断：已经是 data:image/ 或 image:// 的字符串不再处理。
function _isPassthrough(s) {
    return s.indexOf("data:image/") === 0 || s.indexOf("image://") === 0
}

// 安全转字符串：null / undefined 一律 → ""。
function _str(url) {
    return (url === null || url === undefined) ? "" : String(url)
}

// 把已有的 @尺寸后缀替换成 suffix（如 "@120w_120h"），保留 ?query。
// 注意：后缀必须插在 ?query 之前，且不能出现第二个 @。
function sinaSized(url, suffix) {
    var s = _str(url)
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    var q = s.indexOf("?")
    var base = q >= 0 ? s.slice(0, q) : s
    var query = q >= 0 ? s.slice(q) : ""
    // @ 只可能出现在最后一个 / 之后（路径末段），避免误伤 query 里的 @。
    var slash = base.lastIndexOf("/")
    var at = base.indexOf("@", slash + 1)
    if (at >= 0) base = base.slice(0, at)
    return base + _str(suffix) + query
}

// 去掉 @xxx 尺寸后缀，还原原图 URL，保留 ?query。
function stripSinaSize(url) {
    var s = _str(url)
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    var q = s.indexOf("?")
    var base = q >= 0 ? s.slice(0, q) : s
    var query = q >= 0 ? s.slice(q) : ""
    var slash = base.lastIndexOf("/")
    var at = base.indexOf("@", slash + 1)
    if (at >= 0) base = base.slice(0, at)
    return base + query
}

// 统一包装进 image://weibo/ 通道；prefix 是通道前缀（"" / "avatar/" / "original/"）。
function _provider(s, prefix) {
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    return "image://weibo/" + _str(prefix) + encodeURIComponent(s)
}

// 圆形头像通道（C++ 端额外裁剪成圆 + 补 @120w_120h）。
function avatarSource(url) {
    return _provider(sinaSized(url, "@120w_120h"), "avatar/")
}

// 信息流缩略图：走 size 通道，具体宽高由调用方传。
function previewSource(url, w, h) {
    return sizedSource(url, w, h)
}

// image://weibo/size/<w>x<h>/<enc>，不改动 URL 自身的 @ 后缀。
function sizedSource(url, w, h) {
    var s = _str(url)
    if (s === "") return ""
    if (_isPassthrough(s)) return s
    var sw = Number(w)
    var sh = Number(h)
    if (!isFinite(sw) || sw <= 0) sw = 320
    if (!isFinite(sh) || sh <= 0) sh = 170
    return "image://weibo/size/" + Math.round(sw) + "x" + Math.round(sh) + "/" + encodeURIComponent(s)
}

// 原图通道（先剥掉 @ 后缀，C++ 端不再缩放）。
function originalSource(url) {
    return _provider(stripSinaSize(url), "original/")
}

// 原样走 image://weibo/ 通道，不改尺寸、不剥后缀。
function rawSource(url) {
    return _provider(_str(url), "")
}
