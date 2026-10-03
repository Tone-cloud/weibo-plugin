import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/RichText.js" as RichText

// 富文本标签：把纯文本（或已是 HTML 的 textHtml）转成 Text.RichText，
// @用户 / #话题# / 链接都可点，并通过信号上抛。
//
// 输入：`text`（契约 §4 的属性名）或等价的 `sourceText` 别名，二者取非空者。
// 注意根元素是 Text，因此这里不直接给根元素的 text 绑输入，而是绑到内部
// 的 displayText，避免"属性自己绑自己"的循环。
Text {
    id: richLabel

    property string text: ""
    property int maximumLines: 0
    property int fontSize: Theme.fontBody
    property color linkColor: Theme.textLink
    property color topicColor: Theme.textTopic

    // 等价别名：sourceText 非空时优先
    property alias sourceText: richLabel.text

    signal userClicked(string name)
    signal topicClicked(string name)
    signal linkClicked(string url)

    // 参与渲染的原始文本
    readonly property string _raw: richLabel.text.length > 0 ? richLabel.text : richLabel.sourceText
    // 渲染结果（绑定在 _raw / linkColor / topicColor 上）
    readonly property string displayText: build(richLabel._raw,
                                                String(richLabel.linkColor),
                                                String(richLabel.topicColor))

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
