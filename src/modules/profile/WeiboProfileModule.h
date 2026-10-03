#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include "WeiboModels.h"

class WeiboController;

// 用户：我的资料、他人主页、微博列表、关注/粉丝、关注/取关、我的微博/收藏/提到我的。
class WeiboProfileModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboProfileModule(WeiboController *controller);

    // 当前登录用户
    Q_INVOKABLE void fetchMe();

    // 他人主页
    Q_INVOKABLE void fetchProfile(qint64 uid);
    Q_INVOKABLE void fetchStatuses(qint64 uid, int page = 1, int feature = 0);
    Q_INVOKABLE void fetchMoreStatuses();
    Q_INVOKABLE void fetchFollowing(qint64 uid, int page = 1);
    Q_INVOKABLE void fetchFollowers(qint64 uid, int page = 1);
    Q_INVOKABLE void fetchMoreUsers();

    Q_INVOKABLE void follow(qint64 uid, bool follow);

    // 我的
    Q_INVOKABLE void myStatuses(int page = 1);
    Q_INVOKABLE void myFavorites(int page = 1);
    Q_INVOKABLE void myMentions(int page = 1);
    Q_INVOKABLE void fetchUserSearch(const QString &q, int page = 1);
    Q_INVOKABLE void fetchGroups();
    Q_INVOKABLE QObject *groupsModel();

    // 当前查看的主页（供 QML 读取）
    Q_INVOKABLE qint64 activeUid() const { return m_activeUid; }
    Q_INVOKABLE void setActiveUid(qint64 uid);
    Q_INVOKABLE bool followingActive() const { return m_activeFollowing; }

    // 当前主页用户资料，供 QML 读取。
    // WeiboUser 不是 QML 已知类型（没有 Q_GADGET），直接返回它 QML 拿不到任何字段，
    // 所以这里返回 camelCase 键的 QVariantMap；同时附上 followers / following /
    // statuses 这几个短别名，页面写 `u.following` 或 `u.followingCount` 都能取到。
    Q_INVOKABLE QVariantMap activeUser() const;

signals:
    void profileChanged(qint64 uid);
    void followStateChanged(qint64 uid, bool following);

private:
    WeiboController *m_controller = nullptr;
    qint64 m_activeUid = 0;
    WeiboUser m_activeUser;
    bool m_activeFollowing = false;
    int m_statusPage = 1;
    int m_userPage = 1;
    qint64 m_statusUid = 0;
    qint64 m_userUid = 0;
    int m_userKind = 0;  // 0 关注 1 粉丝
};
