import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/RichText.js" as RichText

// 富文本标签：把纯文本（或已是 HTML 的 textHtml）转成 Text.RichText，
// @用户 / #话题# / 链接都可点，并通过信号上抛。
//
// ⚠ 根元素是 Text，它**自带** text / linkColor 这些属性。所以：
//   * 输入属性必须叫 sourceText，不能叫 text ——
//     真机踩过：写 `property string text: ""` 又在下面 `text: displayText`，
//     词典笔直接报
//       BlogCard.qml:233 Type RichTextLabel unavailable
//       RichTextLabel.qml:45 Property value set multiple times
//     一个属性错误会让整条引用链（main → HomePage → BlogCard → 本组件）
//     全部 unavailable，插件点开就是空白。tools/verify.py 的 V 项专门拦这个。
//   * 链接/话题颜色也不能叫 linkColor：Text 自己有 linkColor（Qt 5.1+），
//     重名会静默遮蔽内建属性。这里用 linkTextColor / topicTextColor。
//
// 输入：sourceText；输出：text（渲染后的富文本，只由 displayText 赋值一次）。
Text {
    id: richLabel

    // 参与渲染的原始文本（纯文本或 HTML）
    property string sourceText: ""
    property int maximumLines: 0
    property int fontSize: Theme.fontBody
    property color linkTextColor: Theme.textLink
    property color topicTextColor: Theme.textTopic

    signal userClicked(string name)
    signal topicClicked(string name)
    signal linkClicked(string url)

    // 渲染结果（绑定在 sourceText / 颜色上；**不读 text**，否则自己绑自己）
    readonly property string displayText: build(richLabel.sourceText,
                                                String(richLabel.linkTextColor),
                                                String(richLabel.topicTextColor))

    function build(raw, lc, tc) {
        return RichText.richText(raw, lc, tc)
    }

    textFormat: Text.RichText
    wrapMode: Text.Wrap
    font.pixelSize: richLabel.fontSize
    font.family: Theme.fontFamily
    lineHeight: 1.05
    elide: richLabel.maximumLines > 0 ? Text.ElideRight : Text.ElideNone
    maximumLineCount: richLabel.maximumLines > 0 ? richLabel.maximumLines : 32767
    // 全组件对 text 的唯一一次赋值
    text: richLabel.displayText

    onLinkActivated: {
        var href = String(link)
        if (href.indexOf("weibo://user?name=") === 0) {
            richLabel.userClicked(decodeURIComponent(href.substring(18)))
        } else if (href.indexOf("weibo://topic?name=") === 0) {
            richLabel.topicClicked(decodeURIComponent(href.substring(19)))
        } else if (href.length > 0) {
            // http(s) 与未知 scheme 都按外链交给宿主判断
            richLabel.linkClicked(href)
        }
    }
}
