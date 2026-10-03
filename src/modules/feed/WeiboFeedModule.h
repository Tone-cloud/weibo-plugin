#pragma once

#include <QObject>
#include <QString>
#include <QtGlobal>

class WeiboController;

// 首页信息流 / 关注流 / 分组流 / 热搜榜 / 热搜微博流。
class WeiboFeedModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboFeedModule(WeiboController *controller);

    // 推荐流（containerid=102803）
    Q_INVOKABLE void fetchHome(const QString &sinceId = QString(), int freshType = 0);
    Q_INVOKABLE void fetchMoreHome();
    Q_INVOKABLE void refreshHome();

    // 关注流
    Q_INVOKABLE void fetchFollow(const QString &sinceId = QString());
    Q_INVOKABLE void fetchMoreFollow();

    // 分组流
    Q_INVOKABLE void fetchGroup(const QString &gid, const QString &sinceId = QString());
    Q_INVOKABLE void fetchMoreGroup();

    // 热搜榜
    Q_INVOKABLE void fetchHot();

    // 热搜词对应的微博流
    Q_INVOKABLE void fetchHotStatus(const QString &word, const QString &sinceId = QString());
    Q_INVOKABLE void fetchMoreHotStatus();

    // 与 bili 的 QML 调用习惯保持一致的别名
    Q_INVOKABLE QObject *popularModel();     // 推荐流
    Q_INVOKABLE QObject *dynamicModel();     // 关注流
    Q_INVOKABLE QObject *rankingModel();     // 热搜微博流
    Q_INVOKABLE QObject *hotSearchModel();   // 热搜榜

    QString homeSinceId() const { return m_homeSinceId; }
    QString followSinceId() const { return m_followSinceId; }

signals:
    void homeRefreshed();

private:
    WeiboController *m_controller = nullptr;
    QString m_homeSinceId;
    QString m_followSinceId;
    QString m_groupSinceId;
    QString m_groupGid;
    QString m_hotWord;
    QString m_hotSinceId;
    bool m_homeLoaded = false;
};
