#pragma once

#include <QObject>
#include <QString>

class WeiboController;

// 登录：微博没有可用的开放扫码登录，走 Cookie 导入（SUB / SUBP）。
// Cookie 由 Go sidecar 持久化到插件目录 cookies.json。
class WeiboLoginModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboLoginModule(WeiboController *controller);

    // cookie 支持两种写法：
    //   1) 完整 Cookie 头："SUB=xxx; SUBP=yyy; SSOLoginState=zzz"
    //   2) 仅 SUB 的值
    Q_INVOKABLE void importCookie(const QString &cookie);
    Q_INVOKABLE void checkLogin();
    Q_INVOKABLE void logout();
    Q_INVOKABLE void fetchConfig();

    Q_INVOKABLE bool loggedIn() const;
    Q_INVOKABLE QString uid() const;

signals:
    void importSucceeded(const QString &screenName);
    void importFailed(const QString &message);
    void loggedOut();

private:
    WeiboController *m_controller = nullptr;
    bool m_busy = false;
};
