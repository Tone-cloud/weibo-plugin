import QtQuick 2.12
import WeiboPlugin 1.0

// 加载指示器：三点跳动 + 可选文字；作为列表 footer 或页面居中态使用。
Item {
    id: loadingRoot
    width: parent ? parent.width : 100
    height: contentColumn.implicitHeight + Theme.spacingSmall * 2
    // 不占用 visible 绑定：外部（列表 footer）会直接设 visible，
    // 因此这里只按「有内容」自适应，页面居中态由调用方置 visible: running。
    visible: running || text.length > 0
    opacity: running ? 1 : 0
    z: 50

    property bool running: false
    property string text: "加载中..."
    property int indicatorSize: 18
    // 兼容别名（部分页面按 message 绑定）
    property alias message: loadingRoot.text

    Behavior on opacity { NumberAnimation { duration: Theme.animNormal } }

    Column {
        id: contentColumn
        anchors.centerIn: parent
        spacing: Theme.spacingNormal

        Item {
            id: dots
            width: loadingRoot.indicatorSize
            height: loadingRoot.indicatorSize
            anchors.horizontalCenter: parent.horizontalCenter

            Repeater {
                model: 3
                Rectangle {
                    width: Math.max(3, Math.round(loadingRoot.indicatorSize / 5))
                    height: width
                    radius: width / 2
                    color: Theme.primary
                    x: (dots.width - width) / 2 + (index - 1) * (width + Theme.spacingSmall)
                    y: (dots.height - height) / 2
                    opacity: 0.35

                    SequentialAnimation on opacity {
                        running: loadingRoot.running
                        loops: Animation.Infinite
                        PauseAnimation { duration: index * 160 }
                        NumberAnimation { from: 0.35; to: 1.0; duration: 300; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.0; to: 0.35; duration: 300; easing.type: Easing.InOutSine }
                        PauseAnimation { duration: (2 - index) * 160 }
                    }

                    SequentialAnimation on y {
                        running: loadingRoot.running
                        loops: Animation.Infinite
                        PauseAnimation { duration: index * 160 }
                        NumberAnimation {
                            from: (dots.height - parent.height) / 2
                            to: (dots.height - parent.height) / 2 - 2
                            duration: 300
                            easing.type: Easing.InOutSine
                        }
                        NumberAnimation {
                            from: (dots.height - parent.height) / 2 - 2
                            to: (dots.height - parent.height) / 2
                            duration: 300
                            easing.type: Easing.InOutSine
                        }
                        PauseAnimation { duration: (2 - index) * 160 }
                    }
                }
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: loadingRoot.text
            visible: text.length > 0
            color: Theme.textTertiary
            font.pixelSize: Theme.fontTiny
            font.family: Theme.fontFamily
        }
    }
}
