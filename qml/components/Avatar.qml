import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl

// 圆形头像：source 传原始 URL（内部走 image://weibo/avatar/ 圆形通道）。
// 认证角标用 Canvas 手绘，不依赖图标字体。
Item {
    id: avatar
    width: avatarSize
    height: avatarSize

    property string source: ""
    property int avatarSize: 20
    property bool verified: false
    property int verifiedType: -1
    property bool online: false

    signal clicked()

    // 认证角标直径
    readonly property int _badgeSize: Math.max(7, Math.round(avatarSize * 0.42))
    readonly property color _badgeColor: Theme.verifiedColor(verified ? (verifiedType < 0 ? 0 : verifiedType) : -1)

    Rectangle {
        id: circle
        anchors.fill: parent
        radius: width / 2
        color: Theme.bgTertiary
        clip: true
        border.width: source.length > 0 ? 0 : 1
        border.color: Theme.borderLight

        Image {
            anchors.fill: parent
            source: avatar.source.length > 0 ? ImageUrl.avatarSource(avatar.source) : ""
            sourceSize: Qt.size(Math.max(48, avatar.avatarSize * 2), Math.max(48, avatar.avatarSize * 2))
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
        }

        // 无图时的占位字形
        Text {
            anchors.centerIn: parent
            visible: avatar.source.length === 0
            text: "▣"
            color: Theme.textTertiary
            font.pixelSize: Math.max(7, Math.round(avatar.avatarSize * 0.5))
            font.family: Theme.fontFamily
        }
    }

    // 认证角标（Canvas 画圆 + 勾）
    Canvas {
        id: badge
        visible: avatar.verified
        width: avatar._badgeSize
        height: avatar._badgeSize
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: -1
        anchors.bottomMargin: -1
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        Component.onCompleted: requestPaint()

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var c = width / 2
            ctx.fillStyle = avatar._badgeColor
            ctx.beginPath()
            ctx.arc(c, c, c - 0.5, 0, Math.PI * 2)
            ctx.fill()
            ctx.strokeStyle = Theme.bgCard
            ctx.lineWidth = 1
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath()
            ctx.moveTo(width * 0.28, height * 0.52)
            ctx.lineTo(width * 0.45, height * 0.70)
            ctx.lineTo(width * 0.74, height * 0.32)
            ctx.stroke()
        }
    }

    // 在线小绿点
    Rectangle {
        visible: avatar.online
        width: Math.max(4, Math.round(avatar.avatarSize * 0.22))
        height: width
        radius: width / 2
        color: Theme.success
        border.width: 1
        border.color: Theme.bgCard
        anchors.right: parent.right
        anchors.top: parent.top
    }

    MouseArea {
        id: avatarArea
        anchors.fill: parent
        // 不缓冲点击，避免下层卡片 MouseArea 被"卡住"
        onClicked: avatar.clicked()
    }
}
