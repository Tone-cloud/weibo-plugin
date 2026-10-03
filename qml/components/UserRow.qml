import QtQuick 2.12
import WeiboPlugin 1.0

// 用户行：头像 + 昵称/认证 + 简介 + 粉丝数 + 关注按钮。
// 点击整行 → clicked(uid)；点关注按钮只发 followClicked，不再发 clicked。
Item {
    id: userRow
    width: parent ? parent.width : 300
    height: 34

    property var uid: 0
    property string name: ""
    property string avatar: ""
    property bool verified: false
    property int verifiedType: -1
    property string description: ""
    property string followersText: ""
    property bool isFollowing: false
    property bool showFollow: true

    signal clicked(var uid)
    signal followClicked(var uid, bool follow)

    // 关注按钮按下标记：避免同一次触摸又触发整行 clicked
    property bool _followHandled: false

    Avatar {
        id: rowAvatar
        avatarSize: 24
        source: userRow.avatar
        verified: userRow.verified
        verifiedType: userRow.verifiedType
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        onClicked: {
            userRow._followHandled = true
            userRow.clicked(userRow.uid)
            resetGuard.restart()
        }
    }

    Column {
        id: infoCol
        anchors.left: rowAvatar.right
        anchors.leftMargin: Theme.spacingNormal
        anchors.right: followBtn.visible ? followBtn.left : parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTiny

        Text {
            width: parent.width
            text: userRow.name.length > 0 ? userRow.name : "微博用户"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            font.bold: true
            elide: Text.ElideRight
            maximumLineCount: 1
        }

        Text {
            width: parent.width
            visible: text.length > 0
            text: {
                var parts = []
                if (userRow.description && userRow.description.length > 0)
                    parts.push(userRow.description)
                if (userRow.followersText && userRow.followersText.length > 0)
                    parts.push("粉丝 " + userRow.followersText)
                return parts.join(" · ")
            }
            color: Theme.textTertiary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    // ── 关注按钮 ──
    Rectangle {
        id: followBtn
        visible: userRow.showFollow
        width: 46
        height: 20
        radius: Theme.radiusRound
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        color: userRow.isFollowing
               ? (followArea.pressed ? Theme.bgCardHover : "transparent")
               : (followArea.pressed ? Theme.primaryDark : Theme.primary)
        border.width: userRow.isFollowing ? 1 : 0
        border.color: Theme.borderLight
        scale: followArea.pressed ? 0.94 : 1.0
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }

        Text {
            anchors.centerIn: parent
            text: userRow.isFollowing ? "已关注" : "+ 关注"
            color: userRow.isFollowing ? Theme.textSecondary : Theme.textOnPrimary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            font.bold: !userRow.isFollowing
        }

        MouseArea {
            id: followArea
            anchors.fill: parent
            onClicked: {
                userRow._followHandled = true
                userRow.followClicked(userRow.uid, !userRow.isFollowing)
                resetGuard.restart()
            }
        }
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        z: -1
        onClicked: {
            if (userRow._followHandled) return
            userRow.clicked(userRow.uid)
        }
    }

    // 延迟复位内部标记：clicked 一定在本次触摸序列结束后才结算
    Timer {
        id: resetGuard
        interval: 0
        repeat: false
        onTriggered: userRow._followHandled = false
    }
}
