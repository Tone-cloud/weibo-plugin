import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl

// 评论行：头像 + 昵称 + 正文 + 图片缩略 + 底部（时间/回复/点赞/删除）。
Item {
    id: commentRow
    width: parent ? parent.width : 300
    height: bodyCol.implicitHeight + Theme.spacingNormal * 2

    property var cid: 0
    property string userName: ""
    property string userAvatar: ""
    property bool userVerified: false
    property string text: ""
    property string createdText: ""
    property int likeCount: 0
    property bool liked: false
    property string replyTo: ""
    property int replyCount: 0
    property var pics: []
    property bool canDelete: false
    property bool isReply: false
    // 契约外可选属性：需要 uid 的页面（userClicked 目前传 0）可自行绑定
    property var uid: 0

    signal userClicked(var uid)
    signal replyClicked(var cid, string name)
    signal likeClicked(var cid, bool liked)
    signal deleteClicked(var cid)
    signal imageClicked(int index)
    signal repliesClicked(var cid)

    readonly property int _picCount: pics && pics.length ? pics.length : 0

    Column {
        id: bodyCol
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        spacing: Theme.spacingSmall

        Row {
            width: parent.width
            spacing: Theme.spacingSmall

            Avatar {
                id: cAvatar
                avatarSize: commentRow.isReply ? 16 : 18
                source: commentRow.userAvatar
                verified: commentRow.userVerified
                anchors.top: parent.top
                onClicked: commentRow.userClicked(commentRow.uid)
            }

            Column {
                width: parent.width - cAvatar.width - parent.spacing
                spacing: Theme.spacingTiny

                Text {
                    width: parent.width
                    text: {
                        var n = commentRow.userName.length > 0 ? commentRow.userName : "微博用户"
                        if (commentRow.replyTo && commentRow.replyTo.length > 0)
                            return n + " 回复 " + commentRow.replyTo
                        return n
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                RichTextLabel {
                    width: parent.width
                    sourceText: commentRow.text
                    color: Theme.textPrimary
                    fontSize: Theme.fontSmall
                    maximumLines: 3
                    onUserClicked: commentRow.userClicked(commentRow.uid)
                    onTopicClicked: commentRow.userClicked(commentRow.uid)
                }

                // 评论图片（最多 3 张缩略）
                Row {
                    width: parent.width
                    height: commentRow._picCount > 0 ? 30 : 0
                    visible: commentRow._picCount > 0
                    spacing: Theme.spacingTiny

                    Repeater {
                        model: Math.min(3, commentRow._picCount)

                        Rectangle {
                            width: 30
                            height: 30
                            radius: Theme.radiusTiny
                            color: Theme.bgTertiary
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: {
                                    var p = commentRow.pics[index]
                                    var u = p && p.url !== undefined ? p.url : p
                                    return u ? ImageUrl.previewSource(u, 60, 60) : ""
                                }
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                smooth: true
                            }

                            Rectangle {
                                anchors.fill: parent
                                visible: picArea.pressed
                                color: Theme.withAlpha(Theme.primary, 0.2)
                            }

                            MouseArea {
                                id: picArea
                                anchors.fill: parent
                                onClicked: commentRow.imageClicked(index)
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingMedium

                    Text {
                        text: commentRow.createdText
                        visible: text.length > 0
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter
                    }

                    Text {
                        text: commentRow.replyCount > 0 ? "回复 " + commentRow.replyCount : "回复"
                        color: Theme.textTertiary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.spacingSmall
                            onClicked: commentRow.repliesClicked(commentRow.cid)
                        }
                    }

                    // 点赞
                    Row {
                        spacing: Theme.spacingTiny
                        anchors.verticalCenter: parent.verticalCenter

                        Text {
                            text: commentRow.liked ? "♥" : "♡"
                            color: commentRow.liked ? Theme.accent : Theme.textTertiary
                            font.pixelSize: Theme.fontSmall
                            font.family: Theme.fontFamily
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Text {
                            text: Theme.countText(commentRow.likeCount)
                            color: commentRow.liked ? Theme.accent : Theme.textTertiary
                            font.pixelSize: Theme.fontTiny
                            font.family: Theme.fontFamily
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.spacingSmall
                            onClicked: commentRow.likeClicked(commentRow.cid, !commentRow.liked)
                        }
                    }

                    // 删除（仅自己的评论）
                    Text {
                        visible: commentRow.canDelete
                        text: "删除"
                        color: Theme.error
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        anchors.verticalCenter: parent.verticalCenter

                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -Theme.spacingSmall
                            onClicked: commentRow.deleteClicked(commentRow.cid)
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
        anchors.bottom: parent.bottom
    }
}
