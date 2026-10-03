#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class WeiboController;

// 超话 / 话题：我的超话列表、超话详情、超话微博流、单个 / 一键签到、话题搜索。
class WeiboTopicModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboTopicModule(WeiboController *controller);

    Q_INVOKABLE void fetchMyTopics(int page = 1);
    Q_INVOKABLE void fetchMoreMyTopics();
    Q_INVOKABLE void refreshCheckinState();

    Q_INVOKABLE void fetchTopicDetail(const QString &containerId);
    Q_INVOKABLE void fetchTopicStatuses(const QString &containerId,
                                        const QString &sinceId = QString());
    Q_INVOKABLE void fetchMoreTopicStatuses();

    Q_INVOKABLE void searchTopics(const QString &q, int page = 1);
    Q_INVOKABLE void searchMoreTopics();

    // 单个超话签到；name 只用于提示文案
    Q_INVOKABLE void checkin(const QString &id, const QString &name = QString());
    // 我的超话一键签到（串行，逐个请求，避免风控）
    Q_INVOKABLE void checkinAll();
    Q_INVOKABLE void cancelCheckinAll();

    Q_INVOKABLE QObject *topicModel();      // 话题搜索结果
    Q_INVOKABLE QObject *myTopicModel();    // 我的超话
    Q_INVOKABLE QObject *statusModel();     // 超话微博流

    QString activeContainerId() const { return m_activeContainerId; }

signals:
    void checkinFinished(const QString &id, bool ok, const QString &message);
    void checkinAllFinished(int success, int failed);

private:
    void runNextCheckin();

    WeiboController *m_controller = nullptr;
    QString m_activeContainerId;
    QString m_topicSinceId;
    int m_myTopicPage = 1;
    int m_topicSearchPage = 1;

    bool m_batchRunning = false;
    bool m_batchCancelled = false;
    int m_batchIndex = 0;
    int m_batchSuccess = 0;
    int m_batchFailed = 0;
    QStringList m_batchIds;
    QStringList m_batchNames;
};
