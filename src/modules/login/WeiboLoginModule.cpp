#include "modules/login/WeiboLoginModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QString>

WeiboLoginModule::WeiboLoginModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== Cookie 导入 ======

void WeiboLoginModule::importCookie(const QString &cookie) {
    if (!m_controller)
        return;

    QString value = cookie.trimmed();
    if (value.isEmpty()) {
        emit importFailed(QStringLiteral("Cookie 不能为空"));
        emit m_controller->toastMessage(QStringLiteral("Cookie 不能为空"));
        return;
    }

    // 只填了 SUB 的值时补上名字（SPEC 第 6.4 节支持两种写法）。
    if (!value.contains(QLatin1Char('=')))
        value = QStringLiteral("SUB=") + value;

    if (m_busy)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_busy = true;
    m_controller->setIsLoading(true);

    QJsonObject body;
    body.insert(QStringLiteral("cookie"), value);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/login/import"), body,
        [this, guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;  // self 为空说明模块也已随之销毁，不能再碰 this

            m_busy = false;

            WeiboUser user;
            user.uid = WeiboJson::num(data, "uid", 0);
            user.name = WeiboJson::str(data, "screen_name");
            user.avatar = WeiboJson::str(data, "avatar");
            user.isMe = true;

            // setLoginUser 会发出 loginStateChanged，是本模块唯一可写的登录态钩子
            // （m_loggedIn 是控制器私有成员）。
            self->setLoginUser(user);
            self->setIsLoading(false);

            emit importSucceeded(user.name);
            emit self->toastMessage(QStringLiteral("登录成功"));
        },
        [this, guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();

            self->setIsLoading(false);
            emit importFailed(msg);
            emit self->toastMessage(QStringLiteral("登录失败：%1").arg(msg));
        });
}

// ====== 登录态检查 ======

void WeiboLoginModule::checkLogin() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/login/info"), QMap<QString, QString>(),
        [guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            if (!WeiboJson::boolean(data, "logged_in", false)) {
                // sidecar 明确说未登录：清掉本地登录态即可，不打扰用户。
                self->clearLocalLoginState();
                return;
            }

            // 已登录则补齐用户资料（昵称 / 头像 / 计数）。
            WeiboNetwork *inner = self->network();
            if (!inner)
                return;

            QPointer<WeiboController> innerGuard(self);
            inner->get(
                QStringLiteral("/user/me"), QMap<QString, QString>(),
                [innerGuard](const QJsonObject &meData) {
                    WeiboController *me = innerGuard.data();
                    if (!me)
                        return;
                    me->setLoginUser(UserListModel::parseUser(WeiboJson::obj(meData, "user")));
                },
                [innerGuard](int code, const QString &msg) {
                    WeiboController *me = innerGuard.data();
                    if (!me)
                        return;
                    if (code == -100 || code == -101 || code == 401 || code == -401)
                        me->clearLocalLoginState();
                    else
                        emit me->toastMessage(QStringLiteral("资料加载失败：%1").arg(msg));
                });
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("登录态检查失败：%1").arg(msg));
        });
}

// ====== 退出登录 ======

void WeiboLoginModule::logout() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    if (m_busy)
        return;
    m_busy = true;

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/logout"), QJsonObject(),
        [this, guard](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            self->clearLocalLoginState();
            emit loggedOut();
            emit self->toastMessage(QStringLiteral("已退出登录"));
        },
        [this, guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("退出登录失败：%1").arg(msg));
        });
}

// ====== sidecar 配置 ======

void WeiboLoginModule::fetchConfig() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/config"), QMap<QString, QString>(),
        [this, guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (WeiboJson::boolean(data, "logged_in", false))
                checkLogin();
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("配置加载失败：%1").arg(msg));
        });
}

// ====== 查询 ======

bool WeiboLoginModule::loggedIn() const {
    return m_controller ? m_controller->loggedIn() : false;
}

QString WeiboLoginModule::uid() const {
    return m_controller ? QString::number(m_controller->userId()) : QString();
}
