#include "modules/login/WeiboLoginModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

WeiboLoginModule::WeiboLoginModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {
    // 构造时就把自动刷新跑起来：电脑端把 cookies.json 传进插件目录后，
    // sidecar 会自动加载，这里每 4 秒问一次 /server/state 感知变化，
    // 界面上完全不需要用户操作。
    // 注意：定时器第一次触发在 interval 之后，那时 controller 的模型已经建好了。
    startAutoRefresh(4000);
}

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
            self->setIsLoading(false);

            // verified 表示「上游 /api/config 确认过」。Go 侧现在**先落盘再校验**，
            // 所以离线或 Cookie 过期时也会保存成功，但这里不能报「登录成功」。
            const bool verified =
                WeiboJson::boolean(data, "verified", WeiboJson::boolean(data, "logged_in", false));
            if (!verified) {
                const QString warning = WeiboJson::str(
                    data, "message",
                    QStringLiteral("Cookie 已保存，但未通过登录校验（可能已过期或离线）"));
                // 同步一下本地状态：票据已存下，等联网校验通过后轮询会自动登录。
                pollLocalState();
                emit importFailed(warning);
                emit self->toastMessage(warning);
                return;
            }

            WeiboUser user;
            user.uid = WeiboJson::num(data, "uid", 0);
            user.name = WeiboJson::str(data, "screen_name");
            user.avatar = WeiboJson::str(data, "avatar");
            user.isMe = true;

            // setLoginUser 会发出 loginStateChanged，是本模块唯一可写的登录态钩子
            // （m_loggedIn 是控制器私有成员）。
            self->setLoginUser(user);

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

// ====== 电脑端导入后的自动生效 ======

void WeiboLoginModule::startAutoRefresh(int intervalMs) {
    if (intervalMs < 1500)
        intervalMs = 1500; // 再快也没意义，只会白耗电

    if (!m_watchTimer) {
        m_watchTimer = new QTimer(this);
        m_watchTimer->setTimerType(Qt::CoarseTimer);
        QObject::connect(m_watchTimer, &QTimer::timeout, this,
                         [this]() { pollLocalState(); });
    }
    m_watchTimer->start(intervalMs);
    qDebug() << "[WeiboLogin] 登录态自动刷新已启动，间隔" << intervalMs << "ms";
}

void WeiboLoginModule::stopAutoRefresh() {
    if (m_watchTimer)
        m_watchTimer->stop();
}

bool WeiboLoginModule::autoRefreshRunning() const {
    return m_watchTimer && m_watchTimer->isActive();
}

void WeiboLoginModule::refreshNow() {
    pollLocalState();
}

// pollLocalState 轮询 /server/state 感知「电脑端刚把 Cookie 导进来」。
//
// 刻意不复用 /config：那个接口会顺带请求 m.weibo.cn/api/config，
// 每几秒打一次上游容易被风控；/server/state 只读 sidecar 本地缓存。
void WeiboLoginModule::pollLocalState() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/server/state"), QMap<QString, QString>(),
        [this, guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return; // self 为空说明模块也已销毁，不能再碰 this

            const bool loggedIn = WeiboJson::boolean(data, "logged_in", false);
            const bool verified = WeiboJson::boolean(data, "verified", loggedIn);
            const QString uidText = WeiboJson::str(data, "uid");
            const QString name = WeiboJson::str(data, "screen_name");
            const QString avatar = WeiboJson::str(data, "avatar");

            // 导入页地址：与登录态无关，每次轮询都刷新（sidecar 可能刚起来，
            // 或者 token 因为重启换过了）。设置页要显示给用户照抄。
            const QString url = WeiboJson::str(data, "login_url");
            if (url != m_loginUrl) {
                m_loginUrl = url;
                emit loginUrlChanged();
            }
            QStringList ips;
            const QJsonArray ipArray = data.value(QStringLiteral("lan_ips")).toArray();
            for (const QJsonValue &v : ipArray) {
                const QString ip = v.toString();
                if (!ip.isEmpty())
                    ips.append(ip);
            }
            if (ips != m_lanIps) {
                m_lanIps = ips;
                emit loginUrlChanged();
            }

            const bool firstSync = (m_lastLoggedIn < 0);
            const bool changed = firstSync || loggedIn != (m_lastLoggedIn == 1) ||
                                 uidText != m_lastUid || name != m_lastName;
            m_lastLoggedIn = loggedIn ? 1 : 0;
            m_lastUid = uidText;
            m_lastName = name;

            if (!changed)
                return;

            if (!loggedIn) {
                if (self->loggedIn())
                    self->clearLocalLoginState();
                return;
            }

            // 有票据但还没通过上游校验：不要用空昵称去覆盖界面，
            // 等校验通过（联网后监听会重试）再由下一次轮询登录。
            if (!verified)
                return;

            WeiboUser user;
            user.uid = uidText.toLongLong();
            user.name = name;
            user.avatar = avatar;
            user.isMe = true;
            self->setLoginUser(user);

            // 首次同步（插件刚加载）不提示，否则每次进来都弹一条；
            // 只有「从别的状态变成已登录」才是电脑端刚导入完。
            if (!firstSync) {
                emit loginAutoRefreshed(name);
                emit self->toastMessage(
                    QStringLiteral("已从电脑端导入登录信息：%1").arg(name));
            }
        },
        [](int, const QString &) {
            // 轮询失败静默：sidecar 可能还在启动，或者插件正在退出
        });
}
