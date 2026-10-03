import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/ImageUrl.js" as ImageUrl

// 超话/话题行：封面 + 名称/简介 + 阅读/讨论数 + 签到按钮。
Item {
    id: topicRow
    width: parent ? parent.width : 300
    height: 40

    property var topicId: 0
    property string name: ""
    property string desc: ""
    property string cover: ""
    property string readText: ""
    property string discussText: ""
    property bool isSuper: false
    property int level: 0
    property bool checked: false
    property int signedDays: 0
    property string statusText: ""

    signal clicked(var topicId)
    signal checkinClicked(var topicId, string name)

    // 签到按钮按下标记，避免同一次触摸又触发整行 clicked
    property bool _childHandled: false

    Rectangle {
        id: coverBox
        width: 34
        height: 34
        radius: Theme.radiusSmall
        color: Theme.bgTertiary
        clip: true
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter

        Image {
            anchors.fill: parent
            source: topicRow.cover.length > 0 ? ImageUrl.previewSource(topicRow.cover, 68, 68) : ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
        }

        Text {
            anchors.centerIn: parent
            visible: topicRow.cover.length === 0
            text: "#"
            color: Theme.primary
            font.pixelSize: Theme.fontLarge
            font.family: Theme.fontFamily
            font.bold: true
        }
    }

    Column {
        id: infoCol
        anchors.left: coverBox.right
        anchors.leftMargin: Theme.spacingNormal
        anchors.right: checkinBtn.visible ? checkinBtn.left : parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTiny

        Row {
            width: parent.width
            spacing: Theme.spacingSmall

            Text {
                text: topicRow.name
                width: Math.min(implicitWidth, Math.max(20, parent.width
                                                       - (superBadge.visible ? superBadge.width + 2 : 0)
                                                       - (levelLabel.visible ? levelLabel.width + 2 : 0)))
                color: Theme.textPrimary
                font.pixelSize: Theme.fontBody
                font.family: Theme.fontFamily
                font.bold: true
                elide: Text.ElideRight
                maximumLineCount: 1
                anchors.verticalCenter: parent.verticalCenter
            }

            // 超话徽标
            Rectangle {
                id: superBadge
                visible: topicRow.isSuper
                width: superText.implicitWidth + Theme.spacingSmall
                height: 11
                radius: Theme.radiusTiny
                color: Theme.withAlpha(Theme.primary, 0.2)
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    id: superText
                    anchors.centerIn: parent
                    text: "超话"
                    color: Theme.primary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                    font.bold: true
                }
            }

            // 等级
            Text {
                id: levelLabel
                visible: topicRow.level > 0
                text: "Lv" + topicRow.level
                color: Theme.warning
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Text {
            width: parent.width
            text: {
                if (topicRow.desc && topicRow.desc.length > 0) return topicRow.desc
                var parts = []
                if (topicRow.readText && topicRow.readText.length > 0) parts.push("阅读 " + topicRow.readText)
                if (topicRow.discussText && topicRow.discussText.length > 0) parts.push("讨论 " + topicRow.discussText)
                return parts.join(" · ")
            }
            color: Theme.textTertiary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
        }
    }

    // ── 签到按钮 ──
    Rectangle {
        id: checkinBtn
        width: 44
        height: 20
        radius: Theme.radiusRound
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        color: topicRow.checked
               ? (checkinArea.pressed ? Theme.bgCardHover : Theme.withAlpha(Theme.success, 0.14))
               : (checkinArea.pressed ? Theme.primaryDark : Theme.primary)
        border.width: topicRow.checked ? 1 : 0
        border.color: Theme.withAlpha(Theme.success, 0.4)
        scale: checkinArea.pressed ? 0.94 : 1.0
        Behavior on color { ColorAnimation { duration: Theme.animFast } }
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }

        Text {
            anchors.centerIn: parent
            text: topicRow.checked
                  ? (topicRow.signedDays > 0 ? "已签 " + topicRow.signedDays + "天" : "✓ 已签到")
                  : "签到"
            color: topicRow.checked ? Theme.success : Theme.textOnPrimary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            font.bold: !topicRow.checked
        }

        MouseArea {
            id: checkinArea
            anchors.fill: parent
            onClicked: {
                topicRow._childHandled = true
                topicRow.checkinClicked(topicRow.topicId, topicRow.name)
                resetGuard.restart()
            }
        }
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        z: -1
        onClicked: {
            if (topicRow._childHandled) return
            topicRow.clicked(topicRow.topicId)
        }
    }

    Timer {
        id: resetGuard
        interval: 0
        repeat: false
        onTriggered: topicRow._childHandled = false
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
        anchors.bottom: parent.bottom
    }
}
