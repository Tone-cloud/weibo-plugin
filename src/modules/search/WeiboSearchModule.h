#pragma once

#include <QObject>
#include <QString>

class WeiboController;

// 搜索：微博 / 用户 / 话题 三类，外加热搜榜与本地搜索历史。
class WeiboSearchModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboSearchModule(WeiboController *controller);

    // kind: "status" | "user" | "topic"
    Q_INVOKABLE void search(const QString &q, const QString &kind = QStringLiteral("status"));
    Q_INVOKABLE void searchMore();
    // 搜索页「加载更多」按当前 tab 分别调用（两个都只是 searchMore() 的语义化入口，
    // 真正的分发在 searchMore() 里按 m_kind 做）。
    Q_INVOKABLE void searchUsersMore();
    Q_INVOKABLE void searchTopicsMore();
    Q_INVOKABLE void searchStatus(const QString &q, int page = 1);
    Q_INVOKABLE void searchUsers(const QString &q, int page = 1);
    Q_INVOKABLE void searchTopics(const QString &q, int page = 1);

    Q_INVOKABLE void fetchHotSearch();

    Q_INVOKABLE void saveHistory(const QString &q);
    Q_INVOKABLE void clearHistory();

    Q_INVOKABLE QObject *searchModel();     // 微博结果
    Q_INVOKABLE QObject *userModel();       // 用户结果
    Q_INVOKABLE QObject *topicModel();      // 话题结果
    Q_INVOKABLE QObject *hotSearchModel();  // 热搜榜
    Q_INVOKABLE QObject *historyModel();    // 本地搜索历史

    QString keyword() const { return m_keyword; }
    QString kind() const { return m_kind; }

signals:
    void searchStarted(const QString &keyword);
    void searchFinished(const QString &keyword, int count);

private:
    WeiboController *m_controller = nullptr;
    QString m_keyword;
    QString m_kind = QStringLiteral("status");
    int m_statusPage = 1;
    int m_userPage = 1;
    int m_topicPage = 1;
};
