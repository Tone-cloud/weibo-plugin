import QtQuick 2.12
import WeiboPlugin 1.0
import "pages" as Pages
import "components"
import "components" as Components

// 微博插件主入口。
// 职责：页面栈路由（Loader + Component 保活，与 bili 一致，不使用 StackView）、
// 全局 Toast / 错误遮罩、图片查看请求转发、页面切换动画。
Rectangle {
    id: root
    width: 320
    height: 170
    color: Theme.bgPrimary
    clip: true

    // 首页再次返回时触发，宿主据此退出插件
    signal backButtonClicked()

    WeiboController {
        id: controller
    }

    property var rootController: controller

    // ── 页面路由 ──
    property string currentPage: "home"
    property var pageStack: []
    property string lastPage: "home"
    property bool _animating: false

    // ── 页面 props（随页面栈一起保存，实现跨页面保活） ──
    property string statusId: ""
    property string commentsStatusId: ""
    property string commentsTitle: ""
    property var userUid: 0
    property string topicContainerId: ""
    property string topicName: ""
    property string hotWord: ""
    property var viewerPics: []
    property int viewerIndex: 0

    // ── 滚动位置缓存 ──
    property real homeScrollY: 0
    property real hotContentY: 0

    // 页面是否在栈中（保活判断）
    function stackContains(page) {
        for (var i = 0; i < pageStack.length; ++i) {
            var entry = pageStack[i]
            if (entry && entry.page === page) return true
        }
        return false
    }

    // 离开某页时把该页需要保留的 props 打包入栈
    function capturePageProps(page) {
        if (page === "home") {
            return { scrollY: homeLoader.item ? homeLoader.item.scrollY() : root.homeScrollY }
        }
        if (page === "detail") {
            return { statusId: statusId }
        }
        if (page === "comments") {
            return { statusId: commentsStatusId, title: commentsTitle }
        }
        if (page === "user") {
            return { uid: userUid }
        }
        if (page === "topicDetail") {
            return { containerId: topicContainerId, topicName: topicName }
        }
        if (page === "hot") {
            return {
                word: hotWord,
                contentY: hotLoader.item ? hotLoader.item.contentYValue() : root.hotContentY
            }
        }
        if (page === "viewer") {
            return { pics: viewerPics, index: viewerIndex }
        }
        return {}
    }

    // 回到某页时恢复其 props
    function applyPageProps(page, props) {
        if (!props) return
        if (page === "home") {
            homeScrollY = props.scrollY !== undefined ? props.scrollY : 0
        }
        if (page === "detail") {
            if (props.statusId !== undefined && props.statusId !== null && String(props.statusId) !== "")
                statusId = String(props.statusId)
        }
        if (page === "comments") {
            commentsStatusId = (props.statusId !== undefined && props.statusId !== null)
                    ? String(props.statusId) : ""
            commentsTitle = props.title ? String(props.title) : ""
        }
        if (page === "user") {
            userUid = props.uid !== undefined ? props.uid : 0
        }
        if (page === "topicDetail") {
            topicContainerId = props.containerId ? String(props.containerId) : ""
            topicName = props.topicName ? String(props.topicName) : ""
        }
        if (page === "hot") {
            hotWord = props.word ? String(props.word) : ""
            hotContentY = props.contentY !== undefined ? props.contentY : 0
        }
        if (page === "viewer") {
            viewerPics = props.pics ? props.pics : []
            viewerIndex = props.index !== undefined ? props.index : 0
        }
        // 媒体页复用 detail 的 statusId 通道（MediaPage 的 props 名即为 statusId）
        if (page === "media") {
            if (props.statusId !== undefined && props.statusId !== null && String(props.statusId) !== "")
                statusId = String(props.statusId)
        }
    }

    function navigateTo(page, props) {
        if (_animating) return
        var newStack = pageStack.slice(0)
        newStack.push({ page: currentPage, props: capturePageProps(currentPage) })
        pageStack = newStack
        lastPage = currentPage
        applyPageProps(page, props ? props : ({}))
        _animating = true
        currentPage = page
        pageTransition.restart()
    }

    function goBack() {
        if (_animating) return
        if (pageStack.length > 0) {
            var newStack = pageStack.slice(0)
            var prevEntry = newStack.pop()
            var prev = (prevEntry && prevEntry.page) ? prevEntry.page : prevEntry
            if (prevEntry && prevEntry.props) {
                applyPageProps(prev, prevEntry.props)
            }
            _animating = true
            currentPage = prev
            pageStack = newStack
            pageTransitionBack.restart()
            return
        }
        if (currentPage !== "home") {
            _animating = true
            lastPage = currentPage
            currentPage = "home"
            pageTransitionBack.restart()
            return
        }
        // 首页再返回 → 交给宿主退出插件
        backButtonClicked()
    }

    // ── 页面切换动画容器 ──
    Item {
        id: pageContainer
        anchors.fill: parent
        opacity: 1
        transform: Translate { id: pageTranslate; x: 0 }

        // 前进：从右侧 12px 滑入
        SequentialAnimation {
            id: pageTransition
            ScriptAction {
                script: {
                    pageContainer.opacity = 1
                    pageTranslate.x = 12
                }
            }
            ParallelAnimation {
                NumberAnimation {
                    target: pageContainer; property: "opacity"
                    from: 0.96; to: 1; duration: Theme.animNormal
                    easing.type: Easing.OutQuad
                }
                NumberAnimation {
                    target: pageTranslate; property: "x"
                    from: 12; to: 0; duration: Theme.animNormal
                    easing.type: Easing.OutCubic
                }
            }
            onFinished: root._animating = false
        }

        // 后退：从左侧 12px 滑入
        SequentialAnimation {
            id: pageTransitionBack
            ScriptAction {
                script: {
                    pageContainer.opacity = 1
                    pageTranslate.x = -12
                }
            }
            ParallelAnimation {
                NumberAnimation {
                    target: pageContainer; property: "opacity"
                    from: 0.96; to: 1; duration: Theme.animNormal
                    easing.type: Easing.OutQuad
                }
                NumberAnimation {
                    target: pageTranslate; property: "x"
                    from: -12; to: 0; duration: Theme.animNormal
                    easing.type: Easing.OutCubic
                }
            }
            onFinished: root._animating = false
        }

        // ── 首页 ──
        Loader {
            id: homeLoader
            asynchronous: true
            active: currentPage === "home" || root.stackContains("home")
            visible: currentPage === "home"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.HomePage {
                    controller: root.rootController
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onUserSelected: root.navigateTo("user", { uid: uid })
                    onSearchRequested: root.navigateTo("search")
                    onHotRequested: root.navigateTo("hot")
                    onPublishRequested: root.navigateTo("publish")
                    onProfileRequested: root.navigateTo("profile")
                    onTopicRequested: root.navigateTo("topic")
                }
            }
        }

        // ── 微博详情 ──
        Loader {
            id: detailLoader
            active: currentPage === "detail" || root.stackContains("detail")
            visible: currentPage === "detail"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.StatusDetailPage {
                    controller: root.rootController
                    statusId: root.statusId
                    rootRef: root
                    onBackClicked: root.goBack()
                    // 信号参数与 root.statusId 同名，这里显式走 root.statusId（值相同）
                    onCommentsRequested: root.navigateTo("comments", {
                        statusId: root.statusId,
                        title: controller.detailAuthorName
                    })
                    onUserSelected: root.navigateTo("user", { uid: uid })
                    onMediaRequested: root.navigateTo("media", { statusId: root.statusId })
                    onTopicSelected: root.navigateTo("topicDetail", {
                        containerId: containerId, topicName: name
                    })
                    onImageRequested: root.navigateTo("viewer", { pics: pics, index: index })
                }
            }
        }

        // ── 评论 ──
        Loader {
            id: commentsLoader
            active: currentPage === "comments" || root.stackContains("comments")
            visible: currentPage === "comments"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.CommentsPage {
                    controller: root.rootController
                    statusId: root.commentsStatusId
                    contextTitle: root.commentsTitle
                    onBackClicked: root.goBack()
                    onUserSelected: root.navigateTo("user", { uid: uid })
                }
            }
        }

        // ── 搜索 ──
        Loader {
            id: searchLoader
            active: currentPage === "search" || root.stackContains("search")
            visible: currentPage === "search"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.SearchPage {
                    controller: root.rootController
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onUserSelected: root.navigateTo("user", { uid: uid })
                    onTopicSelected: root.navigateTo("topicDetail", {
                        containerId: containerId, topicName: name
                    })
                }
            }
        }

        // ── 热搜榜 ──
        Loader {
            id: hotLoader
            active: currentPage === "hot" || root.stackContains("hot")
            visible: currentPage === "hot"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.HotPage {
                    controller: root.rootController
                    rootRef: root
                    word: root.hotWord
                    onBackClicked: root.goBack()
                    onWordSelected: root.hotWord = word
                }
            }
        }

        // ── 用户主页 ──
        Loader {
            id: userLoader
            active: currentPage === "user" || root.stackContains("user")
            visible: currentPage === "user"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.UserPage {
                    controller: root.rootController
                    uid: root.userUid
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onImageRequested: root.navigateTo("viewer", { pics: pics, index: index })
                    // 信号参数与 root.statusId 同名：QML 中信号参数优先
                    onMediaRequested: root.navigateTo("media", { statusId: statusId })
                }
            }
        }

        // ── 我的 ──
        Loader {
            id: profileLoader
            active: currentPage === "profile"
            visible: currentPage === "profile"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.ProfilePage {
                    controller: root.rootController
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onUserSelected: root.navigateTo("user", { uid: uid })
                    onTopicRequested: root.navigateTo("topic")
                    onLoginRequested: root.navigateTo("settings")
                    onSettingsRequested: root.navigateTo("settings")
                    onImageRequested: root.navigateTo("viewer", { pics: pics, index: index })
                }
            }
        }

        // ── 发布 ──
        Loader {
            id: publishLoader
            active: currentPage === "publish"
            visible: currentPage === "publish"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.PublishPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                    onPublished: root.navigateTo("detail", { statusId: id })
                }
            }
        }

        // ── 我的超话 / 话题 ──
        Loader {
            id: topicLoader
            active: currentPage === "topic" || root.stackContains("topic")
            visible: currentPage === "topic"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.TopicPage {
                    controller: root.rootController
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onTopicSelected: root.navigateTo("topicDetail", {
                        containerId: containerId, topicName: name
                    })
                    onCheckinAllRequested: {
                        if (controller && controller.topic) controller.topic.checkinAll()
                    }
                }
            }
        }

        // ── 超话详情 ──
        Loader {
            id: topicDetailLoader
            active: currentPage === "topicDetail" || root.stackContains("topicDetail")
            visible: currentPage === "topicDetail"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.TopicDetailPage {
                    controller: root.rootController
                    containerId: root.topicContainerId
                    topicName: root.topicName
                    rootRef: root
                    onBackClicked: root.goBack()
                    onStatusSelected: root.navigateTo("detail", { statusId: id })
                    onUserSelected: root.navigateTo("user", { uid: uid })
                    onCheckinRequested: {
                        if (controller && controller.topic) controller.topic.checkin(topicId, name)
                    }
                }
            }
        }

        // ── 视频 / 直播（叶子页） ──
        Loader {
            id: mediaLoader
            active: currentPage === "media"
            visible: currentPage === "media"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.MediaPage {
                    controller: root.rootController
                    statusId: root.statusId
                    onBackClicked: root.goBack()
                }
            }
        }

        // ── 设置（叶子页） ──
        Loader {
            id: settingsLoader
            active: currentPage === "settings"
            visible: currentPage === "settings"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.SettingsPage {
                    controller: root.rootController
                    onBackClicked: root.goBack()
                }
            }
        }

        // ── 图片查看器 ──
        Loader {
            id: viewerLoader
            active: currentPage === "viewer" || root.stackContains("viewer")
            visible: currentPage === "viewer"
            enabled: visible
            anchors.fill: parent
            sourceComponent: Component {
                Pages.ImageViewerPage {
                    controller: root.rootController
                    pics: root.viewerPics
                    initialIndex: root.viewerIndex
                    onBackClicked: root.goBack()
                }
            }
        }
    }

    // ── 全局错误遮罩 ──
    Components.ErrorOverlay {
        anchors.fill: parent
        errorMessage: controller.globalError
        onRetryClicked: controller.clearError()
        onDismissed: controller.clearError()
    }

    // ── 全局 Toast ──
    Components.Toast {
        id: globalToast
    }

    Connections {
        target: controller
        function onToastMessage(message) {
            globalToast.show(message)
        }
        function onLoginExpired() {
            globalToast.show("登录已过期，请重新导入 Cookie", 3000)
        }
        function onImageRequested(pics, index) {
            root.navigateTo("viewer", { pics: pics, index: index })
        }
    }

    Component.onCompleted: {
        console.log("=== WeiboPlugin Loaded ===", width, "x", height);
        if (controller && controller.bootstrap) controller.bootstrap()
    }
}
