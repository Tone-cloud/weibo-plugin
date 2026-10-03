#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;
class WeiboController;

// 登录：微博没有可用的开放扫码登录，走 Cookie 导入（SUB / SUBP）。
// Cookie 由 Go sidecar 持久化到插件目录 cookies.json。
//
// 「电脑端准备 → 词典笔自动导入」的两条路径：
//   1. 文件：电脑端用 tools\pen-push.ps1 把 cookies.json 送进插件目录
//      → sidecar 的文件监听自动加载 → 本模块轮询 /server/state 感知 → 界面自动登录；
//   2. HTTP：电脑端 POST /config/import（同样落盘，效果一致）。
// 无论哪条路径，笔上都不需要任何触屏操作。
class WeiboLoginModule : public QObject {
    Q_OBJECT
    // 电脑端导入页地址（由 sidecar 的 /server/state 上报，形如
    // http://192.168.1.23:8011/<token>）。设置页直接显示这个链接，
    // 用户在电脑浏览器打开它、粘贴 Cookie 即可，笔上不用打字。
    Q_PROPERTY(QString loginUrl READ loginUrl NOTIFY loginUrlChanged)
    Q_PROPERTY(QStringList lanIps READ lanIps NOTIFY loginUrlChanged)

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

    QString loginUrl() const { return m_loginUrl; }
    QStringList lanIps() const { return m_lanIps; }

    // 登录态自动刷新（默认 4 秒一次，构造函数里自动启动）
    Q_INVOKABLE void startAutoRefresh(int intervalMs = 4000);
    Q_INVOKABLE void stopAutoRefresh();
    Q_INVOKABLE void refreshNow();
    Q_INVOKABLE bool autoRefreshRunning() const;

signals:
    void importSucceeded(const QString &screenName);
    void importFailed(const QString &message);
    void loggedOut();
    // 电脑端改完 Cookie、界面被自动更新时发出（main.qml 用它弹提示）
    void loginAutoRefreshed(const QString &screenName);
    void loginUrlChanged();

private:
    void pollLocalState();

    WeiboController *m_controller = nullptr;
    bool m_busy = false;

    QTimer *m_watchTimer = nullptr;
    // -1 未知 / 0 未登录 / 1 已登录：用来区分「首次同步」与「真的变了」
    int m_lastLoggedIn = -1;
    QString m_lastUid;
    QString m_lastName;
    QString m_loginUrl;
    QStringList m_lanIps;
};
