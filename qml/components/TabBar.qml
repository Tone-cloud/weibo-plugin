import QtQuick 2.12
import WeiboPlugin 1.0

// 顶部/内嵌标签栏：tabs 形如 [{text, badge}]，选中项用主色 + 底部指示条。
Item {
    id: tabBar
    width: parent ? parent.width : 200
    height: tabHeight

    property var tabs: []
    property int currentIndex: 0
    property int tabHeight: 24

    signal tabClicked(int index)

    readonly property int _count: tabs && tabs.length ? tabs.length : 0
    readonly property real _itemWidth: _count > 0 ? width / _count : width

    Row {
        anchors.fill: parent
        spacing: 0

        Repeater {
            model: tabBar.tabs

            Item {
                width: tabBar._itemWidth
                height: tabBar.height

                readonly property bool selected: index === tabBar.currentIndex
                readonly property string tabText: modelData && modelData.text !== undefined
                                               ? String(modelData.text) : String(modelData)
                readonly property var tabBadge: modelData ? modelData.badge : undefined

                Text {
                    id: tabLabel
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: badgeRect.visible ? -5 : 0
                    text: parent.tabText
                    color: parent.selected ? Theme.primary : Theme.textSecondary
                    font.pixelSize: parent.selected ? Theme.fontBody : Theme.fontSmall
                    font.family: Theme.fontFamily
                    font.bold: parent.selected
                }

                // 角标（数字 / 小圆点）
                Rectangle {
                    id: badgeRect
                    visible: parent.tabBadge !== undefined && parent.tabBadge !== null
                             && String(parent.tabBadge).length > 0 && String(parent.tabBadge) !== "0"
                    width: badgeText.implicitWidth + Theme.spacingSmall
                    height: Math.max(8, badgeText.implicitHeight + 1)
                    radius: height / 2
                    color: Theme.accent
                    anchors.left: tabLabel.right
                    anchors.leftMargin: 1
                    anchors.verticalCenter: parent.verticalCenter

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: parent.parent.tabBadge !== undefined ? String(parent.parent.tabBadge) : ""
                        color: Theme.textOnPrimary
                        font.pixelSize: Theme.fontTiny
                        font.family: Theme.fontFamily
                        font.bold: true
                    }
                }

                Rectangle {
                    width: Math.max(14, parent.width * 0.4)
                    height: 2
                    radius: 1
                    color: Theme.primary
                    visible: parent.selected
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                }

                Rectangle {
                    width: parent.width
                    height: parent.height
                    color: Theme.withAlpha(Theme.primary, 0.14)
                    visible: tabArea.pressed
                }

                MouseArea {
                    id: tabArea
                    anchors.fill: parent
                    onClicked: tabBar.tabClicked(index)
                }
            }
        }
    }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
        anchors.bottom: parent.bottom
        z: -1
    }
}
