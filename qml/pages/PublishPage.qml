import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"

// 发微博：纯文字发布（本设备无相册选择器，图片上传入口见 controller.publish.uploadPicture）。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null

    property string draftText: ""
    // 0 公开 / 6 好友圈 / 1 自己可见
    property int visibility: 0
    property bool submitting: false
    // 草稿是否已经从本地读到内存（避免初次加载时立刻回写文件）
    property bool draftLoaded: false
    property bool submitPending: false

    signal backClicked()
    signal published(var id)

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function publishOf() {
        var c = ctl()
        return (c && c.publish) ? c.publish : null
    }

    function uploading() {
        var c = ctl()
        return c ? c.publishUploading === true : false
    }

    function progressValue() {
        var c = ctl()
        if (!c) return 0
        var p = Number(c.publishProgress)
        if (isNaN(p)) return 0
        return Math.max(0, Math.min(1, p))
    }

    function statusText() {
        var c = ctl()
        return c && c.publishStatus ? c.publishStatus : ""
    }

    // 载入本地草稿
    function loadDraft() {
        var p = root.publishOf()
        if (!p || typeof p.loadDraft !== "function") return
        var d = p.loadDraft()
        root.draftText = d ? String(d) : ""
        root.draftLoaded = true
    }

    function submit() {
        if (root.uploading() || root.submitting) return
        var text = root.draftText ? root.draftText.trim() : ""
        if (text.length === 0) {
            var c = root.ctl()
            if (c) c.toastMessage("请输入内容")
            return
        }
        var p = root.publishOf()
        if (!p) return
        root.submitting = true
        root.submitPending = true
        // 发布成功后草稿已无意义，顺带清掉本地草稿文件
        if (typeof p.clearDraft === "function") p.clearDraft()
        p.publish(text, root.visibility, [])
    }

    Component.onCompleted: root.loadDraft()

    // 文本变化后 600ms 才落盘，避免每次按键都写文件
    Timer {
        id: draftTimer
        interval: 600
        repeat: false
        onTriggered: {
            var p = root.publishOf()
            if (!p || typeof p.setDraft !== "function") return
            if (!root.draftLoaded) return
            p.setDraft(root.draftText)
        }
    }

    onDraftTextChanged: {
        if (!root.draftLoaded) return
        draftTimer.restart()
    }

    Connections {
        target: controller ? controller.publish : null

        function onPublishSucceeded(id) {
            if (!root.submitPending) return
            root.submitPending = false
            root.submitting = false
            root.published(id)
            root.backClicked()
        }

        function onPublishFailed(message) {
            root.submitPending = false
            root.submitting = false
            var c = root.ctl()
            var text = message ? String(message) : "发布失败"
            if (c) c.toastMessage(text)
        }
    }

    TitleBar {
        id: titleBar
        title: "发微博"
        showBack: true
        showAction: true
        actionText: "发送"
        actionEnabled: !root.uploading() && !root.submitting
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
        onActionClicked: root.submit()
    }

    // ====== 正文输入 ======
    TextAreaInput {
        id: editor
        anchors.top: titleBar.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        height: 62
        text: root.draftText
        placeholder: "分享新鲜事…"
        maxLength: 2000
        lineHeight: 14
        showCounter: true
        onTextEdited: root.draftText = text
    }

    // 本构建只支持文字发布：controller.publish.uploadPicture(base64, filename) 存在，
    // 但本设备没有相册 / 文件选择器，因此页面不提供图片选择入口。
    Text {
        id: noteText
        anchors.top: editor.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        height: 18
        text: "本版本仅支持文字微博；图片上传接口已保留，但本设备无图片选择器。"
        color: Theme.textTertiary
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontTiny
        wrapMode: Text.WordWrap
        maximumLineCount: 2
        elide: Text.ElideRight
        clip: true
    }

    // ====== 可见性 ======
    Row {
        id: visibilityRow
        anchors.top: noteText.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        spacing: Theme.spacingSmall
        height: 20

        Text {
            id: visibilityLabelText
            width: 32
            anchors.verticalCenter: parent.verticalCenter
            text: "可见性"
            color: Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
        }

        Repeater {
            model: [
                { label: "公开", value: 0 },
                { label: "好友圈", value: 6 },
                { label: "自己可见", value: 1 }
            ]

            delegate: Rectangle {
                id: visibilityChip
                height: 20
                width: visibilityChipText.implicitWidth + Theme.spacingLarge
                radius: Theme.radiusRound
                color: root.visibility === modelData.value
                       ? Theme.primary
                       : (chipArea.pressed ? Theme.bgTertiary : Theme.bgSecondary)
                border.width: root.visibility === modelData.value ? 0 : 1
                border.color: Theme.withAlpha(Theme.primary, 0.25)
                anchors.verticalCenter: parent.verticalCenter

                Behavior on color { ColorAnimation { duration: Theme.animFast } }

                Text {
                    id: visibilityChipText
                    anchors.centerIn: parent
                    text: modelData.label
                    color: root.visibility === modelData.value ? Theme.textOnPrimary : Theme.textSecondary
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSmall
                    font.bold: root.visibility === modelData.value
                }

                MouseArea {
                    id: chipArea
                    anchors.fill: parent
                    onClicked: root.visibility = modelData.value
                }
            }
        }
    }

    // ====== 进度 / 状态 ======
    Item {
        id: progressArea
        anchors.top: visibilityRow.bottom
        anchors.topMargin: Theme.spacingTiny
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSmall
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        height: 26
        visible: root.uploading() || root.statusText() !== ""

        // 进度条底槽
        Rectangle {
            id: progressTrack
            width: parent.width
            height: 4
            radius: Theme.radiusTiny
            color: Theme.bgTertiary
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingTiny
            visible: root.uploading()

            Rectangle {
                width: parent.width * root.progressValue()
                height: parent.height
                radius: parent.radius
                color: Theme.primary

                Behavior on width { NumberAnimation { duration: Theme.animFast } }
            }
        }

        Text {
            id: progressText
            anchors.top: progressTrack.visible ? progressTrack.bottom : parent.top
            anchors.topMargin: Theme.spacingTiny
            anchors.left: parent.left
            anchors.right: parent.right
            text: root.statusText() !== ""
                  ? root.statusText()
                  : ("发布中 " + Math.round(root.progressValue() * 100) + "%")
            color: root.uploading() ? Theme.primaryLight : Theme.textSecondary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSmall
            elide: Text.ElideRight
        }
    }
}
