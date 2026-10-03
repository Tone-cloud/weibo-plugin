#pragma once

#include <QObject>
#include <QString>

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

private:
    void pollLocalState();

    WeiboController *m_controller = nullptr;
    bool m_busy = false;

    QTimer *m_watchTimer = nullptr;
    // -1 未知 / 0 未登录 / 1 已登录：用来区分「首次同步」与「真的变了」
    int m_lastLoggedIn = -1;
    QString m_lastUid;
    QString m_lastName;
};
