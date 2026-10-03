import QtQuick 2.12
import WeiboPlugin 1.0
import "../components"

// 设置：账号（Cookie 导入 / 退出 / 复查登录态）、数据清理、关于。
Rectangle {
    id: root
    anchors.fill: parent
    color: Theme.bgPrimary
    clip: true

    property var controller: null

    property bool cookieEditorVisible: false
    property string cookieText: ""
    property bool logoutConfirmVisible: false

    signal backClicked()

    function ctl() {
        return (controller && controller.feed) ? controller : null
    }

    function loginOf() {
        var c = ctl()
        return (c && c.login) ? c.login : null
    }

    // 电脑端导入页地址（sidecar 通过 /server/state 上报，含一次性 token）
    function loginUrlText() {
        var l = loginOf()
        if (!l) return ""
        var u = l.loginUrl
        return (u === undefined || u === null) ? "" : String(u)
    }

    function isLoggedIn() {
        var c = ctl()
        return c ? c.loggedIn === true : false
    }

    function userName() {
        var c = ctl()
        return c && c.userName ? c.userName : ""
    }

    function userIdText() {
        var c = ctl()
        if (!c) return "—"
        return String(c.userId)
    }

    function importCookie() {
        var text = root.cookieText ? root.cookieText.trim() : ""
        if (text.length === 0) {
            var c0 = ctl()
            if (c0) c0.toastMessage("请先粘贴 Cookie")
            return
        }
        var l = root.loginOf()
        if (!l || typeof l.importCookie !== "function") return
        l.importCookie(text)
        // 提交后立即清空输入框，避免 Cookie 长时间停留在界面上
        root.cookieText = ""
        root.cookieEditorVisible = false
    }

    function logout() {
        var l = root.loginOf()
        if (l && typeof l.logout === "function") l.logout()
    }

    function checkLogin() {
        var l = root.loginOf()
        if (l && typeof l.checkLogin === "function") l.checkLogin()
    }

    function clearSearchHistory() {
        var c = ctl()
        if (!c || !c.search) return
        if (typeof c.search.clearHistory !== "function") return
        c.search.clearHistory()
        c.toastMessage("已清空搜索历史")
    }

    function clearDraft() {
        var c = ctl()
        if (!c || !c.publish) return
        if (typeof c.publish.clearDraft !== "function") return
        c.publish.clearDraft()
        c.toastMessage("已清空发布草稿")
    }

    // 供 main.qml 记录 / 恢复滚动位置
    function contentYValue() {
        return pageFlick ? pageFlick.contentY : 0
    }

    function restoreContentY(y) {
        if (!pageFlick) return
        Qt.callLater(function() {
            pageFlick.contentY = Math.max(0, y)
            Qt.callLater(function() { pageFlick.contentY = Math.max(0, y) })
        })
    }

    // 导入结果统一走全局 Toast
    Connections {
        target: controller ? controller.login : null

        function onImportSucceeded(screenName) {
            var c = root.ctl()
            if (c) c.toastMessage("登录成功：" + screenName)
        }

        function onImportFailed(message) {
            var c = root.ctl()
            var text = message ? String(message) : "导入失败"
            if (c) c.toastMessage(text)
        }
    }

    TitleBar {
        id: titleBar
        title: "设置"
        showBack: true
        height: Theme.titleBarHeight
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        onBackClicked: root.backClicked()
    }

    Flickable {
        id: pageFlick
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        contentWidth: width
        contentHeight: pageColumn.childrenRect.height + Theme.spacingLarge
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: pageColumn
            width: parent.width
            spacing: Theme.spacingSmall
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSmall

            // ============ 电脑端导入（推荐，笔上不用打字）============
            // 与 bili 的 bili-sms:8666 / netease 的登录服务:8667 同一思路：
            // sidecar 单独监听 0.0.0.0，电脑浏览器打开这个链接就能粘贴 Cookie。
            // 完整链接由 sidecar 通过 /server/state 上报（含一次性 token），
            // 这里只负责显示，用户在电脑上照抄即可。
            Rectangle {
                id: desktopCard
                width: parent.width - Theme.spacingNormal * 2
                anchors.horizontalCenter: parent.horizontalCenter
                radius: Theme.radiusMedium
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.withAlpha(Theme.primary, 0.45)
                height: desktopColumn.childrenRect.height + Theme.spacingNormal * 2

                Column {
                    id: desktopColumn
                    width: parent.width - Theme.spacingNormal * 2
                    spacing: Theme.spacingSmall
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingNormal
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: "电脑端导入（推荐）"
                        color: Theme.primaryLight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: "在电脑浏览器打开下面的链接，粘贴 SUB / SUBP 即可；"
                              + "笔上不用打字，登录后几秒内自动生效。"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        wrapMode: Text.WordWrap
                    }

                    // 链接本体：用底色块突出，方便照抄
                    Rectangle {
                        width: parent.width
                        height: urlText.height + Theme.spacingSmall * 2
                        radius: Theme.radiusSmall
                        color: Theme.bgInput
                        border.width: 1
                        border.color: Theme.withAlpha(Theme.borderLight, 0.8)
                        visible: root.loginUrlText() !== ""

                        Text {
                            id: urlText
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: Theme.spacingSmall
                            text: root.loginUrlText()
                            color: Theme.primaryLight
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontBody
                            font.bold: true
                            wrapMode: Text.WrapAnywhere
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    Text {
                        width: parent.width
                        visible: root.loginUrlText() === ""
                        text: "本地服务还没上报导入地址。确认插件已正常启动"
                              + "（或 sidecar 被 WEIBO_LOGIN_PORT=0 关掉了）。"
                        color: Theme.warning
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Rectangle {
                            width: 72
                            height: 26
                            radius: Theme.radiusMedium
                            color: refreshUrlArea.pressed ? Theme.bgCardHover : Theme.bgTertiary

                            Text {
                                anchors.centerIn: parent
                                text: "刷新地址"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                            }

                            MouseArea {
                                id: refreshUrlArea
                                anchors.fill: parent
                                onClicked: {
                                    var l = root.loginOf()
                                    if (l && l.refreshNow) l.refreshNow()
                                }
                            }
                        }

                        Text {
                            width: parent.width - 80
                            anchors.verticalCenter: parent.verticalCenter
                            text: "同一 Wi-Fi 下才能访问"
                            color: Theme.textTertiary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontTiny
                        }
                    }
                }
            }

            // ================= 账号 =================
            Rectangle {
                id: accountCard
                width: parent.width - Theme.spacingNormal * 2
                anchors.horizontalCenter: parent.horizontalCenter
                radius: Theme.radiusMedium
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.withAlpha(Theme.border, 0.9)
                height: accountColumn.childrenRect.height + Theme.spacingNormal * 2

                Column {
                    id: accountColumn
                    width: parent.width - Theme.spacingNormal * 2
                    spacing: Theme.spacingSmall
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingNormal
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: "账号"
                        color: Theme.primaryLight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: root.isLoggedIn()
                              ? ("已登录：" + root.userName() + "  UID " + root.userIdText())
                              : "未登录"
                        color: root.isLoggedIn() ? Theme.textPrimary : Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        elide: Text.ElideRight
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.spacingSmall

                        // 导入 Cookie
                        Rectangle {
                            id: importButton
                            width: 92
                            height: 28
                            radius: Theme.radiusMedium
                            color: importArea.pressed ? Theme.primaryDark : Theme.primary

                            Behavior on color { ColorAnimation { duration: Theme.animFast } }

                            Text {
                                anchors.centerIn: parent
                                text: root.cookieEditorVisible ? "收起" : "导入 Cookie"
                                color: Theme.textOnPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                                font.bold: true
                            }

                            MouseArea {
                                id: importArea
                                anchors.fill: parent
                                onClicked: root.cookieEditorVisible = !root.cookieEditorVisible
                            }
                        }

                        // 重新检查登录态
                        Rectangle {
                            id: recheckButton
                            width: 108
                            height: 28
                            radius: Theme.radiusMedium
                            color: recheckArea.pressed ? Theme.bgCardHover : Theme.bgTertiary
                            border.width: 1
                            border.color: Theme.withAlpha(Theme.primary, 0.35)

                            Text {
                                anchors.centerIn: parent
                                text: "重新检查登录态"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                                font.bold: true
                            }

                            MouseArea {
                                id: recheckArea
                                anchors.fill: parent
                                onClicked: root.checkLogin()
                            }
                        }
                    }

                    // Cookie 输入区（内联展开）
                    Item {
                        id: cookieEditorArea
                        width: parent.width
                        height: root.cookieEditorVisible ? 60 : 0
                        visible: root.cookieEditorVisible

                        TextAreaInput {
                            id: cookieEditor
                            anchors.fill: parent
                            text: root.cookieText
                            placeholder: "粘贴 Cookie：SUB=…; SUBP=…"
                            maxLength: 4000
                            lineHeight: 14
                            showCounter: false
                            onTextEdited: root.cookieText = text
                        }
                    }

                    // 确定（提交 Cookie）
                    Rectangle {
                        id: confirmButton
                        visible: root.cookieEditorVisible
                        width: 64
                        height: visible ? 28 : 0
                        radius: Theme.radiusMedium
                        color: confirmArea.pressed ? Theme.success : Theme.withAlpha(Theme.success, 0.75)

                        Behavior on color { ColorAnimation { duration: Theme.animFast } }

                        Text {
                            anchors.centerIn: parent
                            text: "确定"
                            color: Theme.textOnPrimary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }

                        MouseArea {
                            id: confirmArea
                            anchors.fill: parent
                            onClicked: root.importCookie()
                        }
                    }

                    // 退出登录
                    Rectangle {
                        id: logoutButton
                        width: 92
                        height: 28
                        radius: Theme.radiusMedium
                        color: logoutArea.pressed ? Theme.withAlpha(Theme.error, 0.35)
                                                  : Theme.withAlpha(Theme.error, 0.18)
                        border.width: 1
                        border.color: Theme.withAlpha(Theme.error, 0.55)

                        Text {
                            anchors.centerIn: parent
                            text: "退出登录"
                            color: Theme.error
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSmall
                            font.bold: true
                        }

                        MouseArea {
                            id: logoutArea
                            anchors.fill: parent
                            onClicked: root.logoutConfirmVisible = true
                        }
                    }

                    // 取 Cookie 的中文指引
                    Text {
                        id: cookieHint
                        width: parent.width
                        text: "获取 Cookie：浏览器登录 weibo.com → F12 → Application → Cookies → 复制 SUB 与 SUBP，格式 SUB=…; SUBP=…"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // ================= 数据 =================
            Rectangle {
                id: dataCard
                width: parent.width - Theme.spacingNormal * 2
                anchors.horizontalCenter: parent.horizontalCenter
                radius: Theme.radiusMedium
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.withAlpha(Theme.border, 0.9)
                height: dataColumn.childrenRect.height + Theme.spacingNormal * 2

                Column {
                    id: dataColumn
                    width: parent.width - Theme.spacingNormal * 2
                    spacing: Theme.spacingSmall
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingNormal
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: "数据"
                        color: Theme.primaryLight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    Row {
                        width: parent.width
                        spacing: Theme.spacingSmall

                        Rectangle {
                            id: clearHistoryButton
                            width: 118
                            height: 28
                            radius: Theme.radiusMedium
                            color: clearHistoryArea.pressed ? Theme.bgCardHover : Theme.bgTertiary

                            Text {
                                anchors.centerIn: parent
                                text: "清空搜索历史"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                            }

                            MouseArea {
                                id: clearHistoryArea
                                anchors.fill: parent
                                onClicked: root.clearSearchHistory()
                            }
                        }

                        Rectangle {
                            id: clearDraftButton
                            width: 118
                            height: 28
                            radius: Theme.radiusMedium
                            color: clearDraftArea.pressed ? Theme.bgCardHover : Theme.bgTertiary

                            Text {
                                anchors.centerIn: parent
                                text: "清空发布草稿"
                                color: Theme.textPrimary
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontTiny
                            }

                            MouseArea {
                                id: clearDraftArea
                                anchors.fill: parent
                                onClicked: root.clearDraft()
                            }
                        }
                    }
                }
            }

            // ================= 关于 =================
            Rectangle {
                id: aboutCard
                width: parent.width - Theme.spacingNormal * 2
                anchors.horizontalCenter: parent.horizontalCenter
                radius: Theme.radiusMedium
                color: Theme.bgCard
                border.width: 1
                border.color: Theme.withAlpha(Theme.border, 0.9)
                height: aboutColumn.childrenRect.height + Theme.spacingNormal * 2

                Column {
                    id: aboutColumn
                    width: parent.width - Theme.spacingNormal * 2
                    spacing: Theme.spacingSmall
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingNormal
                    anchors.horizontalCenter: parent.horizontalCenter

                    Text {
                        text: "关于"
                        color: Theme.primaryLight
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSmall
                        font.bold: true
                    }

                    Text {
                        width: parent.width
                        text: "笔里微博（WeiboPocket）  v1.0.0"
                        color: Theme.textPrimary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontBody
                        font.bold: true
                    }

                    Text {
                        id: aboutDesc
                        width: parent.width
                        text: "仿 bili 插件架构；数据来自微博 Web 接口，仅供学习测试。"
                        color: Theme.textSecondary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        width: parent.width
                        text: "本地服务：127.0.0.1:8010"
                        color: Theme.textTertiary
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTiny
                    }
                }
            }
        }
    }

    // 退出登录二次确认
    ConfirmPopup {
        visible: root.logoutConfirmVisible
        title: "退出登录"
        message: "退出后需要重新导入 Cookie 才能使用微博功能"
        confirmText: "退出"
        cancelText: "取消"
        danger: true
        onConfirmed: {
            root.logoutConfirmVisible = false
            root.logout()
        }
        onCancelled: root.logoutConfirmVisible = false
    }
}
