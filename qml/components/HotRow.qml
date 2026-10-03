import QtQuick 2.12
import WeiboPlugin 1.0
import "../js/TimeText.js" as TimeText

// 热搜行：排名 + 词条 + 标签（爆/沸/新/热）+ 热度。
Item {
    id: hotRow
    width: parent ? parent.width : 300
    height: 30

    property int rank: 0
    property string word: ""
    property var rawHot: 0
    property string hotText: ""
    property string label: ""
    property bool showRank: true

    signal clicked(string word)

    // 排名前三名用主色/红色强调
    readonly property color _rankColor: rank === 1 ? Theme.accent
                                     : (rank === 2 ? Theme.primary
                                       : (rank === 3 ? Theme.primaryLight : Theme.textTertiary))

    // 热度：优先 hotText，其次 rawHot 数字格式化
    readonly property string _hot: {
        if (hotText && String(hotText).length > 0) return String(hotText)
        var n = Number(rawHot)
        if (!isFinite(n) || n <= 0) return ""
        return TimeText.count(n)
    }

    Row {
        id: rowContent
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        spacing: Theme.spacingSmall

        // 排名徽标
        Rectangle {
            id: rankBadge
            visible: hotRow.showRank
            width: Math.max(14, rankLabel.implicitWidth + Theme.spacingSmall)
            height: 14
            radius: Theme.radiusTiny
            color: hotRow.rank <= 3 ? Theme.withAlpha(hotRow._rankColor, 0.18) : "transparent"
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: rankLabel
                anchors.centerIn: parent
                text: hotRow.rank > 0 ? String(hotRow.rank) : "·"
                color: hotRow._rankColor
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                font.bold: hotRow.rank <= 3
            }
        }

        Text {
            id: wordLabel
            text: hotRow.word
            width: Math.max(40, rowContent.width - rankBadge.width - labelBadge.width - hotLabel.width
                            - rowContent.spacing * 3)
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
            anchors.verticalCenter: parent.verticalCenter
        }

        // 标签徽标
        Rectangle {
            id: labelBadge
            visible: hotRow.label.length > 0
            width: Math.max(12, lblText.implicitWidth + Theme.spacingSmall)
            height: 12
            radius: Theme.radiusTiny
            color: Theme.withAlpha(Theme.labelFor(hotRow.label), 0.22)
            border.width: 1
            border.color: Theme.withAlpha(Theme.labelFor(hotRow.label), 0.5)
            anchors.verticalCenter: parent.verticalCenter

            Text {
                id: lblText
                anchors.centerIn: parent
                text: hotRow.label
                color: Theme.labelFor(hotRow.label)
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                font.bold: true
            }
        }

        Text {
            id: hotLabel
            text: hotRow._hot
            color: Theme.textTertiary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
            elide: Text.ElideRight
            maximumLineCount: 1
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // 点击反馈覆盖层（整行可点）
    Rectangle {
        anchors.fill: parent
        color: Theme.withAlpha(Theme.primary, 0.14)
        visible: rowArea.pressed
    }

    MouseArea {
        id: rowArea
        anchors.fill: parent
        onClicked: hotRow.clicked(hotRow.word)
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
        anchors.bottom: parent.bottom
    }
}
