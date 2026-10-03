import QtQuick 2.12
import WeiboPlugin 1.0
import "../components" as Components
import "../components"

// 评论页：一级评论列表 + 底部输入条 + 子评论（回复）底部面板。
// 布局：TitleBar(28) + 列表(112) + 输入条(30)。
Rectangle {
    id: commentsPage
    anchors.fill: parent
    color: Theme.bgPrimary

    property var controller: null
    property string statusId: ""
    property string contextTitle: ""

    property bool requested: false
    property bool repliesPanelVisible: false
    property string repliesTitle: ""
    // 当前「回复 @某人」的目标昵称（用于输入条前缀与占位符）
    property string replyToName: ""

    signal backClicked()
    signal userSelected(var uid)

    function ctl() {
        return (controller && controller.comments) ? controller : null
    }

    // replyCid / replyName 在 C++ 侧是 Q_INVOKABLE 方法，这里兼容「方法」与「属性」两种暴露方式
    function replyCid() {
        var c = ctl()
        if (!c) return ""
        var v = c.comments.replyCid
        if (typeof v === "function") v = c.comments.replyCid()
        return v ? String(v) : ""
    }

    function replyName() {
        var c = ctl()
        if (!c) return ""
        var v = c.comments.replyName
        if (typeof v === "function") v = c.comments.replyName()
        return v ? String(v) : ""
    }

    function commentModelRef() {
        var c = ctl()
        return c ? c.comments.commentModel() : null
    }

    function replyModelRef() {
        var c = ctl()
        return c ? c.comments.replyModel() : null
    }

    function firstLoading() {
        var m = commentModelRef()
        return commentsPage.requested && m && m.loading && m.count === 0
    }

    function emptyNow() {
        var m = commentModelRef()
        return commentsPage.requested && m && !m.loading && m.count === 0
    }

    function errorText() {
        var m = commentModelRef()
        if (!m) return ""
        return m.errorMessage ? m.errorMessage : ""
    }

    function openImages(pics, index) {
        if (!pics || pics.length === 0) return
        var c = ctl()
        if (c && c.viewer && c.viewer.openImages) c.viewer.openImages(pics, index)
    }

    // ── 加载 ──
    function loadComments() {
        var c = ctl()
        if (!c) return
        if (!statusId || statusId === "") return
        commentsPage.requested = true
        commentsPage.repliesPanelVisible = false
        c.comments.setActiveId(statusId)
        c.comments.fetchComments(statusId, 1, 0, 0)
    }

    function refreshComments() {
        var c = ctl()
        if (!c) return
        if (!statusId || statusId === "") return
        if (c.comments.refreshComments) {
            c.comments.refreshComments()
            return
        }
        c.comments.fetchComments(statusId, 1, 0, 0)
    }

    // ── 交互 ──
    function startReply(cid, name) {
        var c = ctl()
        if (!c) return
        c.comments.setReplyTo(cid, name)
        commentsPage.replyToName = name ? String(name) : ""
        commentsPage.repliesPanelVisible = false
    }

    function cancelReply() {
        var c = ctl()
        if (c) c.comments.setReplyTo("", "")
        commentsPage.replyToName = ""
    }

    function likeComment(cid, liked) {
        var c = ctl()
        if (!c) return
        c.comments.likeComment(cid, !liked)
    }

    function deleteComment(cid) {
        var c = ctl()
        if (!c) return
        c.comments.deleteComment(cid)
    }

    function openReplies(cid) {
        var c = ctl()
        if (!c) return
        commentsPage.repliesTitle = "回复"
        commentsPage.repliesPanelVisible = true
        c.comments.fetchReplies(commentsPage.statusId, cid, 1)
    }

    function send() {
        var c = ctl()
        if (!c) return
        var t = commentInput.text ? String(commentInput.text) : ""
        if (t.replace(/\s/g, "") === "") return
        c.comments.postComment(commentsPage.statusId, t, commentsPage.replyCid(), false)
        commentInput.text = ""
        commentsPage.cancelReply()
    }

    Components.TitleBar {
        id: titleBar
        title: "评论"
        subtitle: commentsPage.contextTitle
        showBack: true
        showAction: true
        actionText: "↻"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: commentsPage.backClicked()
        onActionClicked: commentsPage.refreshComments()
    }

    // ── 一级评论列表 ──
    Components.LoadMoreListView {
        id: commentList
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: composer.top
        orientation: ListView.Vertical
        spacing: Theme.spacingSmall
        contentMargin: Theme.spacingSmall
        clip: true
        // CommentListModel 没有 hasMore 角色，用「有条目即可继续加载」作为条件
        model: commentsPage.commentModelRef()
        hasMore: model ? model.count > 0 : false
        loadingMore: model ? model.loading : false
        // 已发起请求且不在加载中时才显示内置空态
        ready: commentsPage.requested && !commentsPage.firstLoading()
        emptyText: "还没有评论，来抢沙发"
        emptyHint: "在下方输入框发表评论"
        emptyGlyph: "✉"
        onLoadMore: {
            var c = commentsPage.ctl()
            if (c) c.comments.fetchMoreComments()
        }

        delegate: Components.CommentRow {
            cid: model.id
            userName: model.userName
            userAvatar: model.userAvatar
            userVerified: model.userVerified
            text: model.text
            createdText: model.createdText
            likeCount: model.likeCount
            liked: model.liked
            replyTo: model.replyTo
            replyCount: model.replyCount
            pics: model.pics
            canDelete: model.canDelete
            onUserClicked: commentsPage.userSelected(uid)
            onReplyClicked: commentsPage.startReply(cid, name)
            onLikeClicked: commentsPage.likeComment(model.id, model.liked)
            onDeleteClicked: commentsPage.deleteComment(model.id)
            onRepliesClicked: commentsPage.openReplies(model.id)
            onImageClicked: commentsPage.openImages(model.pics, index)
        }
    }

    // ── 首屏加载中（加载更多由列表内置尾部指示负责） ──
    Components.LoadingIndicator {
        anchors.centerIn: commentList
        visible: commentsPage.firstLoading() && commentList.count <= 0 && commentsPage.errorText() === ""
        running: visible
        indicatorSize: 18
        text: "加载中…"
    }

    Text {
        anchors.horizontalCenter: commentList.horizontalCenter
        anchors.bottom: composer.top
        anchors.bottomMargin: Theme.spacingTiny
        visible: text.length > 0 && commentList.count === 0
        text: commentsPage.errorText()
        color: Theme.error
        font.pixelSize: Theme.fontTiny
        font.family: Theme.fontFamily
    }

    // ── 底部输入条 ──
    Rectangle {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 30
        color: Theme.bgSecondary
        z: 40

        Rectangle {
            width: parent.width
            height: 1
            anchors.top: parent.top
            color: Theme.divider
        }

        Item {
            anchors.fill: parent
            anchors.margins: 3

            Text {
                id: replyPrefix
                x: 0
                y: 0
                width: 72
                height: parent.height
                visible: commentsPage.replyToName !== ""
                text: "回复 @" + commentsPage.replyToName
                color: Theme.primary
                font.pixelSize: Theme.fontTiny
                font.family: Theme.fontFamily
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }

            Components.TextAreaInput {
                id: commentInput
                x: replyPrefix.visible ? (replyPrefix.width + Theme.spacingTiny) : 0
                y: 0
                width: parent.width - (replyPrefix.visible ? (replyPrefix.width + Theme.spacingTiny) : 0) - 44
                height: 24
                placeholder: commentsPage.replyToName !== ""
                             ? "回复 @" + commentsPage.replyToName
                             : "说点什么…"
                lineHeight: 14
                showCounter: false
            }

            Rectangle {
                id: sendButton
                x: parent.width - 40
                y: 0
                width: 40
                height: 24
                radius: Theme.radiusSmall
                color: sendArea.pressed ? Theme.primaryDark : Theme.primary

                Text {
                    anchors.centerIn: parent
                    text: "发送"
                    color: Theme.textOnPrimary
                    font.pixelSize: Theme.fontSmall
                    font.family: Theme.fontFamily
                    font.bold: true
                }

                MouseArea {
                    id: sendArea
                    anchors.fill: parent
                    onClicked: commentsPage.send()
                }
            }
        }
    }

    // ── 子评论底部面板的点击遮罩 ──
    MouseArea {
        anchors.fill: parent
        visible: commentsPage.repliesPanelVisible
        z: 45
        onClicked: commentsPage.repliesPanelVisible = false
    }

    // ── 子评论（回复）面板 ──
    Rectangle {
        id: repliesPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 110
        color: Theme.bgSecondary
        border.color: Theme.borderLight
        border.width: 1
        visible: commentsPage.repliesPanelVisible
        z: 50

        Item {
            id: repliesHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 20

            Text {
                x: Theme.spacingSmall
                y: 0
                height: parent.height
                width: parent.width - 60
                text: commentsPage.repliesTitle + " · 子评论"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontSmall
                font.family: Theme.fontFamily
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }

            Rectangle {
                x: parent.width - 44
                y: 1
                width: 40
                height: 18
                radius: Theme.radiusSmall
                color: closeRepliesArea.pressed ? Theme.bgTertiary : "transparent"
                border.color: Theme.border
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: "关闭"
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontTiny
                    font.family: Theme.fontFamily
                }

                MouseArea {
                    id: closeRepliesArea
                    anchors.fill: parent
                    onClicked: commentsPage.repliesPanelVisible = false
                }
            }

            Rectangle {
                width: parent.width
                height: 1
                anchors.bottom: parent.bottom
                color: Theme.divider
            }
        }

        Components.LoadMoreListView {
            id: replyList
            anchors.top: repliesHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacingTiny
            orientation: ListView.Vertical
            spacing: Theme.spacingSmall
            contentMargin: Theme.spacingSmall
            clip: true
            model: commentsPage.replyModelRef()
            hasMore: model ? model.count > 0 : false
            loadingMore: model ? model.loading : false
            ready: true
            emptyText: "暂无子评论"
            emptyHint: "还没有人回复这条评论"
            emptyGlyph: "✉"
            onLoadMore: {
                var c = commentsPage.ctl()
                if (c) c.comments.fetchMoreReplies()
            }

            delegate: Components.CommentRow {
                isReply: true
                cid: model.id
                userName: model.userName
                userAvatar: model.userAvatar
                userVerified: model.userVerified
                text: model.text
                createdText: model.createdText
                likeCount: model.likeCount
                liked: model.liked
                replyTo: model.replyTo
                replyCount: model.replyCount
                pics: model.pics
                canDelete: model.canDelete
                onUserClicked: commentsPage.userSelected(uid)
                onReplyClicked: commentsPage.startReply(cid, name)
                onLikeClicked: commentsPage.likeComment(model.id, model.liked)
                onDeleteClicked: commentsPage.deleteComment(model.id)
                onImageClicked: commentsPage.openImages(model.pics, index)
            }
        }
    }

    Connections {
        target: commentsPage.ctl() ? commentsPage.ctl().comments : null
        function onCommentPosted() {
            commentsPage.replyToName = ""
            commentInput.text = ""
            if (commentList) commentList.restoreContentY(0)
            if (replyList) replyList.restoreContentY(0)
        }
        function onCommentDeleted(cid) {
            var c = commentsPage.ctl()
            if (!c) return
            var m = c.comments.commentModel()
            if (m && m.removeById) m.removeById(cid)
            var rm = c.comments.replyModel()
            if (rm && rm.removeById) rm.removeById(cid)
        }
    }

    Component.onCompleted: {
        if (statusId !== "") loadComments()
    }

    onStatusIdChanged: {
        if (statusId !== "") {
            commentsPage.requested = false
            commentsPage.replyToName = ""
            loadComments()
        }
    }
}
