#include "modules/profile/WeiboProfileModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QtGlobal>

namespace {

QVector<WeiboBlog> parseBlogItems(const QJsonObject &data) {
    const QJsonArray items = WeiboJson::arr(data, "items");
    QVector<WeiboBlog> blogs;
    blogs.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject())
            blogs.append(BlogListModel::parseBlog(value.toObject()));
    }
    return blogs;
}

QVector<WeiboUser> parseUserItems(const QJsonObject &data) {
    const QJsonArray items = WeiboJson::arr(data, "items");
    QVector<WeiboUser> users;
    users.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject())
            users.append(UserListModel::parseUser(value.toObject()));
    }
    return users;
}

// QML（ProfilePage）只调用通用的 fetchMoreStatuses()，收藏 / 提到我的分页要由
// C++ 侧按「当前模式」决定，而冻结的头文件里没有记录该模式的成员，所以这里用
// 文件级映射按模块指针记录（仅 GUI 线程访问，ProfilePage / UserPage 各自先设置
// 模式再触发翻页）。mode：0 用户主页时间线，1 我的微博，2 我的收藏，3 提到我的。
struct BlogListTrack {
    int mode = 0;
    int page = 1;
};

QHash<const WeiboProfileModule *, BlogListTrack> s_blogListTrack;

// 「我的微博 / 我的收藏 / 提到我的」三条列表形态完全一致，收敛到一处。
void fetchBlogList(WeiboController *controller, BlogListModel *model, const QString &path,
                   const QMap<QString, QString> &params, bool reset,
                   const QString &errorLabel) {
    if (!controller || !model || model->loading())
        return;

    WeiboNetwork *network = controller->network();
    if (!network)
        return;

    if (reset)
        model->clear();
    model->setLoading(true);
    model->setErrorMessage(QString());
    controller->setIsLoading(true);

    QPointer<WeiboController> guard(controller);
    network->get(
        path, params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            const QVector<WeiboBlog> blogs = parseBlogItems(data);
            model->appendItems(blogs);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);
            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("暂无微博"));
        },
        [guard, model, errorLabel](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("%1失败：%2").arg(errorLabel, msg));
        });
}

}  // namespace

WeiboProfileModule::WeiboProfileModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 我的资料 ======

void WeiboProfileModule::fetchMe() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_controller->setIsLoading(true);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/me"), QMap<QString, QString>(),
        [guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            const WeiboUser user = UserListModel::parseUser(WeiboJson::obj(data, "user"));
            self->setLoginUser(user);
            self->setIsLoading(false);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("资料加载失败：%1").arg(msg));
        });
}

// ====== 他人主页 ======

void WeiboProfileModule::fetchProfile(qint64 uid) {
    if (!m_controller)
        return;

    if (uid <= 0) {
        emit m_controller->toastMessage(QStringLiteral("用户无效"));
        return;
    }

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    setActiveUid(uid);
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("uid"), QString::number(uid));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/profile"), params,
        [this, guard, uid](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const QJsonObject userObject = WeiboJson::obj(data, "user");
            const WeiboUser user = UserListModel::parseUser(userObject);

            m_activeUid = uid;
            m_activeUser = user;
            // following 可能是顶层字段，也可能只在 UserItem 里。
            m_activeFollowing = WeiboJson::boolean(userObject, "following", user.isFollowing);

            // 自己的主页顺便刷新全局登录资料。
            if (uid == self->userId() || user.isMe)
                self->setLoginUser(user);

            self->setIsLoading(false);
            emit profileChanged(uid);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("主页加载失败：%1").arg(msg));
        });
}

void WeiboProfileModule::setActiveUid(qint64 uid) {
    m_activeUid = uid;
    m_activeUser = WeiboUser();
    m_activeUser.uid = uid;
    m_activeFollowing = false;
    emit profileChanged(uid);
}

// 当前主页用户资料 → QVariantMap，供 QML 直接读字段。
// 键名与 UserListModel 的 roleNames() 对齐（camelCase），并额外给出
// followers / following / statuses 三个短别名，页面两种写法都能取到。
QVariantMap WeiboProfileModule::activeUser() const {
    const WeiboUser &u = m_activeUser;
    QVariantMap m;
    m["uid"] = static_cast<qlonglong>(u.uid);
    m["name"] = u.name;
    m["avatar"] = u.avatar;
    m["cover"] = u.cover;
    m["verified"] = u.verified;
    m["verifiedType"] = u.verifiedType;
    m["verifiedReason"] = u.verifiedReason;
    m["description"] = u.description;
    m["followersCount"] = static_cast<qlonglong>(u.followers);
    m["followingCount"] = static_cast<qlonglong>(u.following);
    m["statusesCount"] = static_cast<qlonglong>(u.statusesCount);
    m["followersText"] = WeiboJson::formatCount(u.followers);
    m["gender"] = u.gender;
    m["location"] = u.location;
    m["followingMe"] = u.followingMe;
    m["isFollowing"] = u.isFollowing || m_activeFollowing;
    m["isMe"] = u.isMe;
    // 短别名（UserPage.qml 用 u.following / u.followers / u.statuses 读）
    m["followers"] = static_cast<qlonglong>(u.followers);
    m["following"] = static_cast<qlonglong>(u.following);
    m["statuses"] = static_cast<qlonglong>(u.statusesCount);
    return m;
}

// ====== 用户微博列表 ======

void WeiboProfileModule::fetchStatuses(qint64 uid, int page, int feature) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->userStatusModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    if (uid <= 0) {
        emit m_controller->toastMessage(QStringLiteral("用户无效"));
        return;
    }

    page = qBound(1, page, 500);
    const bool firstPage = (page <= 1 || m_statusUid != uid);
    if (firstPage)
        model->clear();

    m_statusUid = uid;
    m_statusPage = page;
    // 进入用户主页：把通用的 fetchMoreStatuses 切到时间线模式。
    BlogListTrack track;
    track.mode = 0;
    track.page = page;
    s_blogListTrack.insert(this, track);
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("uid"), QString::number(uid));
    params.insert(QStringLiteral("page"), QString::number(page));
    params.insert(QStringLiteral("feature"), QString::number(qBound(0, feature, 1)));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/statuses"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            const QVector<WeiboBlog> blogs = parseBlogItems(data);
            model->appendItems(blogs);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);
            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("暂无微博"));
        },
        [guard, model](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("微博列表加载失败：%1").arg(msg));
        });
}

void WeiboProfileModule::fetchMoreStatuses() {
    if (!m_controller)
        return;

    const BlogListTrack track = s_blogListTrack.value(this);

    // ProfilePage 的「我的微博 / 收藏 / 提到我的」共用这一个翻页入口，
    // 按最近一次使用过的模式继续拉下一页。
    if (track.mode == 1 || track.mode == 2 || track.mode == 3) {
        BlogListModel *model = nullptr;
        if (track.mode == 1)
            model = m_controller->myStatusModel();
        else if (track.mode == 2)
            model = m_controller->favoriteModel();
        else
            model = m_controller->mentionModel();

        if (!model || model->loading() || !model->hasMore())
            return;

        const int next = track.page + 1;
        if (track.mode == 1)
            myStatuses(next);
        else if (track.mode == 2)
            myFavorites(next);
        else
            myMentions(next);
        return;
    }

    // 用户主页时间线（UserPage）
    BlogListModel *model = m_controller->userStatusModel();
    if (!model || model->loading() || !model->hasMore() || m_statusUid <= 0)
        return;
    fetchStatuses(m_statusUid, m_statusPage + 1, 0);
}

// ====== 关注 / 粉丝 ======

void WeiboProfileModule::fetchFollowing(qint64 uid, int page) {
    if (!m_controller)
        return;

    UserListModel *model = m_controller->followingModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    if (uid <= 0) {
        emit m_controller->toastMessage(QStringLiteral("用户无效"));
        return;
    }

    page = qBound(1, page, 500);
    const bool firstPage = (page <= 1 || m_userUid != uid || m_userKind != 0);
    if (firstPage)
        model->clear();

    m_userUid = uid;
    m_userPage = page;
    m_userKind = 0;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("uid"), QString::number(uid));
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/following"), params,
        [guard, model, firstPage](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            const QVector<WeiboUser> users = parseUserItems(data);
            if (firstPage)
                model->setItems(users);
            else
                model->appendItems(users);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setTotalCount(static_cast<int>(WeiboJson::num(data, "total", 0)));
            model->setLoading(false);
            self->setIsLoading(false);
        },
        [guard, model](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("关注列表加载失败：%1").arg(msg));
        });
}

void WeiboProfileModule::fetchFollowers(qint64 uid, int page) {
    if (!m_controller)
        return;

    UserListModel *model = m_controller->followerModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    if (uid <= 0) {
        emit m_controller->toastMessage(QStringLiteral("用户无效"));
        return;
    }

    page = qBound(1, page, 500);
    const bool firstPage = (page <= 1 || m_userUid != uid || m_userKind != 1);
    if (firstPage)
        model->clear();

    m_userUid = uid;
    m_userPage = page;
    m_userKind = 1;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("uid"), QString::number(uid));
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/followers"), params,
        [guard, model, firstPage](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            const QVector<WeiboUser> users = parseUserItems(data);
            if (firstPage)
                model->setItems(users);
            else
                model->appendItems(users);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setTotalCount(static_cast<int>(WeiboJson::num(data, "total", 0)));
            model->setLoading(false);
            self->setIsLoading(false);
        },
        [guard, model](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("粉丝列表加载失败：%1").arg(msg));
        });
}

void WeiboProfileModule::fetchMoreUsers() {
    if (!m_controller || m_userUid <= 0)
        return;

    if (m_userKind == 1) {
        UserListModel *model = m_controller->followerModel();
        if (!model || model->loading() || !model->hasMore())
            return;
        fetchFollowers(m_userUid, m_userPage + 1);
    } else {
        UserListModel *model = m_controller->followingModel();
        if (!model || model->loading() || !model->hasMore())
            return;
        fetchFollowing(m_userUid, m_userPage + 1);
    }
}

// ====== 关注 / 取关 ======

void WeiboProfileModule::follow(qint64 uid, bool follow) {
    if (!m_controller)
        return;

    if (!m_controller->loggedIn()) {
        emit m_controller->toastMessage(QStringLiteral("请先登录"));
        return;
    }
    if (uid <= 0)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("uid"), static_cast<double>(uid));
    body.insert(QStringLiteral("follow"), follow);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/user/follow"), body,
        [this, guard, uid, follow](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const bool result =
                data.contains(QStringLiteral("following"))
                    ? WeiboJson::boolean(data, "following", follow)
                    : follow;

            if (m_activeUid == uid)
                m_activeFollowing = result;
            m_activeUser.isFollowing = result;

            if (UserListModel *model = self->followingModel())
                model->setFollowState(uid, result);
            if (UserListModel *model = self->followerModel())
                model->setFollowState(uid, result);

            emit followStateChanged(uid, result);
            emit self->toastMessage(result ? QStringLiteral("已关注")
                                           : QStringLiteral("已取消关注"));
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("操作失败：%1").arg(msg));
        });
}

// ====== 我的微博 / 收藏 / 提到我的 ======

void WeiboProfileModule::myStatuses(int page) {
    if (!m_controller)
        return;
    const qint64 uid = m_controller->userId();
    if (uid <= 0 || !m_controller->loggedIn()) {
        emit m_controller->toastMessage(QStringLiteral("请先登录"));
        return;
    }

    page = qBound(1, page, 500);
    const BlogListTrack previous = s_blogListTrack.value(this);
    BlogListTrack track;
    track.mode = 1;
    track.page = page;
    s_blogListTrack.insert(this, track);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("uid"), QString::number(uid));
    params.insert(QStringLiteral("page"), QString::number(page));
    params.insert(QStringLiteral("feature"), QStringLiteral("0"));

    fetchBlogList(m_controller, m_controller->myStatusModel(), QStringLiteral("/user/statuses"),
                  params, page <= 1 || previous.mode != 1, QStringLiteral("我的微博加载"));
}

void WeiboProfileModule::myFavorites(int page) {
    if (!m_controller)
        return;
    if (!m_controller->loggedIn()) {
        emit m_controller->toastMessage(QStringLiteral("请先登录"));
        return;
    }

    page = qBound(1, page, 500);
    const BlogListTrack previous = s_blogListTrack.value(this);
    BlogListTrack track;
    track.mode = 2;
    track.page = page;
    s_blogListTrack.insert(this, track);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("page"), QString::number(page));

    fetchBlogList(m_controller, m_controller->favoriteModel(), QStringLiteral("/status/favorites"),
                  params, page <= 1 || previous.mode != 2, QStringLiteral("我的收藏加载"));
}

void WeiboProfileModule::myMentions(int page) {
    if (!m_controller)
        return;
    if (!m_controller->loggedIn()) {
        emit m_controller->toastMessage(QStringLiteral("请先登录"));
        return;
    }

    page = qBound(1, page, 500);
    const BlogListTrack previous = s_blogListTrack.value(this);
    BlogListTrack track;
    track.mode = 3;
    track.page = page;
    s_blogListTrack.insert(this, track);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("page"), QString::number(page));

    fetchBlogList(m_controller, m_controller->mentionModel(), QStringLiteral("/status/mentions"),
                  params, page <= 1 || previous.mode != 3, QStringLiteral("提到我的加载"));
}

// ====== 用户搜索 ======

void WeiboProfileModule::fetchUserSearch(const QString &q, int page) {
    if (!m_controller)
        return;

    UserListModel *model = m_controller->userSearchModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString keyword = q.trimmed().left(100);
    if (keyword.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("请输入搜索内容"));
        return;
    }

    page = qBound(1, page, 200);
    if (page <= 1)
        model->clear();

    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("q"), keyword);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/search"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            model->appendItems(parseUserItems(data));
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setTotalCount(static_cast<int>(WeiboJson::num(data, "total", 0)));
            model->setLoading(false);
            self->setIsLoading(false);
            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("未找到相关用户"));
        },
        [guard, model](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("用户搜索失败：%1").arg(msg));
        });
}

// ====== 分组 ======

// 控制器头文件没有分组模型（m_groupsModel 之类），因此本模块无法把结果交给 QML：
// fetchGroups() 只负责发请求，失败时提示；groupsModel() 恒返回 nullptr（QML 不可解引用）。
void WeiboProfileModule::fetchGroups() {
    if (!m_controller)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/groups"), QMap<QString, QString>(),
        [](const QJsonObject &data) {
            Q_UNUSED(data)
            // 没有可写入的模型，成功路径保持静默。
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("分组加载失败：%1").arg(msg));
        });
}

QObject *WeiboProfileModule::groupsModel() {
    // 冻结的控制器头文件没有分组列表模型，这里只能返回空指针。
    return nullptr;
}
