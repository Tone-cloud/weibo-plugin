#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

#include "WeiboModels.h"

class WeiboController;

// 单条微博：详情快照、转发列表、点赞列表、点赞/收藏/转发/删除。
//
// 详情快照支持「多实例保活」：从详情页进入用户页再返回时不重新拉取
// （与 bili 的 VideoDetailSnapshot 同思路）。
class WeiboStatusModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboStatusModule(WeiboController *controller);

    Q_INVOKABLE void fetchDetail(const QString &id);
    // 从缓存恢复快照；返回是否命中
    Q_INVOKABLE bool restoreCachedDetail(const QString &id);
    Q_INVOKABLE void dropCachedDetail(const QString &id);
    Q_INVOKABLE void captureCurrentDetail();

    Q_INVOKABLE void fetchReposts(const QString &id, int page = 1);
    Q_INVOKABLE void fetchMoreReposts();
    Q_INVOKABLE void fetchLikers(const QString &id, int page = 1);
    Q_INVOKABLE void fetchMoreLikers();

    Q_INVOKABLE void like(const QString &id, bool liked);
    Q_INVOKABLE void favorite(const QString &id, bool fav);
    Q_INVOKABLE void repost(const QString &id, const QString &content,
                            bool alsoComment = false);
    Q_INVOKABLE void remove(const QString &id);

    Q_INVOKABLE void setActiveId(const QString &id);
    Q_INVOKABLE QString activeId() const { return m_activeId; }
    Q_INVOKABLE bool liked(const QString &id) const;
    Q_INVOKABLE bool favorited(const QString &id) const;

    Q_INVOKABLE QObject *repostModel();
    Q_INVOKABLE QObject *likerModel();

    QString loadingId() const { return m_loadingId; }

signals:
    void detailLoaded(const QString &id);
    void statusRemoved(const QString &id);

private:
    struct Snapshot {
        WeiboBlog blog;
        bool valid = false;
    };

    WeiboController *m_controller = nullptr;
    QString m_activeId;
    QString m_loadingId;
    QHash<QString, Snapshot> m_snapshots;
    QSet<QString> m_preloading;
    int m_repostPage = 1;
    int m_likerPage = 1;
    QString m_repostId;
    QString m_likerId;
};
