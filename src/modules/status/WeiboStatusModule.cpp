#include "modules/status/WeiboStatusModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
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

// 详情页改动的点赞 / 收藏 / 计数必须同步回所有已加载列表（共用同一份 BlogItem）。
QVector<BlogListModel *> allBlogModels(WeiboController *self) {
    QVector<BlogListModel *> models;
    if (!self)
        return models;
    models << self->homeModel() << self->followModel() << self->hotStatusModel()
           << self->groupModel() << self->searchStatusModel() << self->userStatusModel()
           << self->topicStatusModel() << self->myStatusModel() << self->favoriteModel()
           << self->mentionModel() << self->repostModel();
    return models;
}

void patchLikeEverywhere(WeiboController *self, const QString &id, bool liked,
                         qint64 attitudesCount) {
    const QVector<BlogListModel *> models = allBlogModels(self);
    for (BlogListModel *model : models) {
        if (model)
            model->applyLikeState(id, liked, attitudesCount);
    }
}

void patchFavoriteEverywhere(WeiboController *self, const QString &id, bool favorited) {
    const QVector<BlogListModel *> models = allBlogModels(self);
    for (BlogListModel *model : models) {
        if (model)
            model->applyFavoriteState(id, favorited);
    }
}

void patchRepostDeltaEverywhere(WeiboController *self, const QString &id, int repostDelta,
                                int commentDelta) {
    const QVector<BlogListModel *> models = allBlogModels(self);
    for (BlogListModel *model : models) {
        if (model)
            model->applyRepostCommentDelta(id, repostDelta, commentDelta);
    }
}

void removeEverywhere(WeiboController *self, const QString &id) {
    const QVector<BlogListModel *> models = allBlogModels(self);
    for (BlogListModel *model : models) {
        if (model)
            model->removeById(id);
    }
}

}  // namespace

WeiboStatusModule::WeiboStatusModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 详情 ======

void WeiboStatusModule::fetchDetail(const QString &id) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    // 命中快照就不打网络（从详情页进用户页再返回的场景）。
    if (restoreCachedDetail(statusId))
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    if (m_preloading.contains(statusId))
        return;

    m_preloading.insert(statusId);
    m_loadingId = statusId;
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/status/detail"), params,
        [this, guard, statusId](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self) {
                m_preloading.remove(statusId);
                return;
            }

            m_preloading.remove(statusId);
            if (m_loadingId == statusId)
                m_loadingId.clear();

            const WeiboBlog blog = BlogListModel::parseBlog(WeiboJson::obj(data, "status"));
            if (blog.id.isEmpty()) {
                self->setIsLoading(false);
                emit self->toastMessage(QStringLiteral("详情加载失败：数据为空"));
                return;
            }

            self->setDetail(blog);

            Snapshot snapshot;
            snapshot.blog = blog;
            snapshot.valid = true;
            m_snapshots.insert(statusId, snapshot);
            m_activeId = statusId;

            // 列表卡片与详情保持同步。
            patchLikeEverywhere(self, blog.id, blog.attitudesStatus == 1, blog.attitudesCount);
            patchFavoriteEverywhere(self, blog.id, blog.favorited);

            self->setIsLoading(false);
            emit detailLoaded(statusId);
        },
        [this, guard, statusId](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_preloading.remove(statusId);
            if (m_loadingId == statusId)
                m_loadingId.clear();
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();

            // 详情没有对应的列表模型，只能复位全局 loading。
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("详情加载失败：%1").arg(msg));
        });
}

bool WeiboStatusModule::restoreCachedDetail(const QString &id) {
    if (!m_controller)
        return false;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return false;

    const auto it = m_snapshots.constFind(statusId);
    if (it == m_snapshots.constEnd() || !it->valid || it->blog.id.isEmpty())
        return false;

    m_controller->setDetail(it->blog);
    m_activeId = statusId;
    emit m_controller->detailChanged();
    emit detailLoaded(statusId);
    return true;
}

void WeiboStatusModule::dropCachedDetail(const QString &id) {
    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;
    if (statusId == m_activeId)
        return;  // 当前正在展示的快照不允许丢弃
    m_snapshots.remove(statusId);
}

void WeiboStatusModule::captureCurrentDetail() {
    if (!m_controller)
        return;

    const WeiboBlog &blog = m_controller->detail();
    const QString statusId = m_activeId.isEmpty() ? blog.id : m_activeId;
    if (statusId.isEmpty() || blog.id.isEmpty())
        return;

    Snapshot snapshot;
    snapshot.blog = blog;
    snapshot.valid = true;
    m_snapshots.insert(statusId, snapshot);
    m_activeId = statusId;
}

void WeiboStatusModule::setActiveId(const QString &id) {
    m_activeId = id.trimmed();
}

bool WeiboStatusModule::liked(const QString &id) const {
    if (!m_controller)
        return false;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return false;

    const auto it = m_snapshots.constFind(statusId);
    if (it != m_snapshots.constEnd() && it->valid && it->blog.id == statusId)
        return it->blog.attitudesStatus == 1;

    const WeiboBlog &current = m_controller->detail();
    if (current.id == statusId)
        return current.attitudesStatus == 1;
    return false;
}

bool WeiboStatusModule::favorited(const QString &id) const {
    if (!m_controller)
        return false;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return false;

    const auto it = m_snapshots.constFind(statusId);
    if (it != m_snapshots.constEnd() && it->valid && it->blog.id == statusId)
        return it->blog.favorited;

    const WeiboBlog &current = m_controller->detail();
    if (current.id == statusId)
        return current.favorited;
    return false;
}

// ====== 转发列表 / 点赞列表 ======

void WeiboStatusModule::fetchReposts(const QString &id, int page) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->repostModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    page = qBound(1, page, 500);
    const bool firstPage = (page <= 1 || m_repostId != statusId);
    if (firstPage)
        model->clear();

    m_repostId = statusId;
    m_repostPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/status/reposts"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
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
            emit self->toastMessage(QStringLiteral("转发列表加载失败：%1").arg(msg));
        });
}

void WeiboStatusModule::fetchMoreReposts() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->repostModel();
    if (!model || model->loading() || !model->hasMore() || m_repostId.isEmpty())
        return;
    fetchReposts(m_repostId, m_repostPage + 1);
}

void WeiboStatusModule::fetchLikers(const QString &id, int page) {
    if (!m_controller)
        return;

    UserListModel *model = m_controller->likerModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    page = qBound(1, page, 500);
    const bool firstPage = (page <= 1 || m_likerId != statusId);
    if (firstPage)
        model->clear();

    m_likerId = statusId;
    m_likerPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/status/likers"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseUserItems(data));
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
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
            emit self->toastMessage(QStringLiteral("点赞列表加载失败：%1").arg(msg));
        });
}

void WeiboStatusModule::fetchMoreLikers() {
    if (!m_controller)
        return;
    UserListModel *model = m_controller->likerModel();
    if (!model || model->loading() || !model->hasMore() || m_likerId.isEmpty())
        return;
    fetchLikers(m_likerId, m_likerPage + 1);
}

// ====== 写操作 ======

void WeiboStatusModule::like(const QString &id, bool liked) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    // 请求前先记下旧计数：响应里没有 attitudes_count 时按 ±1 乐观调整。
    const WeiboBlog &current = m_controller->detail();
    const qint64 previous = (current.id == statusId) ? current.attitudesCount : 0;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);
    body.insert(QStringLiteral("liked"), liked);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/like"), body,
        [this, guard, statusId, liked, previous](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            qint64 count = WeiboJson::num(data, "attitudes_count", -1);
            if (count < 0) {
                count = previous + (liked ? 1 : -1);
                if (count < 0)
                    count = 0;
            }

            // applyDetailLike 会无条件改写详情快照，只有当前详情就是这一条时才调用，
            // 否则在首页给卡片点赞会把另一条微博的计数写进详情。
            if (self->detail().id == statusId)
                self->applyDetailLike(liked, count);
            patchLikeEverywhere(self, statusId, liked, count);
            auto it = m_snapshots.find(statusId);
            if (it != m_snapshots.end() && it->valid) {
                it->blog.attitudesStatus = liked ? 1 : 0;
                it->blog.attitudesCount = count;
            }
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

void WeiboStatusModule::favorite(const QString &id, bool fav) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);
    body.insert(QStringLiteral("fav"), fav);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/favorite"), body,
        [this, guard, statusId, fav](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const bool result =
                data.contains(QStringLiteral("favorited"))
                    ? WeiboJson::boolean(data, "favorited", fav)
                    : fav;

            // 同 like：收藏状态也只回写当前详情，列表行单独补齐。
            if (self->detail().id == statusId)
                self->applyDetailFavorite(result);
            patchFavoriteEverywhere(self, statusId, result);

            auto it = m_snapshots.find(statusId);
            if (it != m_snapshots.end() && it->valid)
                it->blog.favorited = result;
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

void WeiboStatusModule::repost(const QString &id, const QString &content, bool alsoComment) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);
    body.insert(QStringLiteral("content"), content);
    body.insert(QStringLiteral("also_comment"), alsoComment);

    const int commentDelta = alsoComment ? 1 : 0;

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/repost"), body,
        [this, guard, statusId, commentDelta](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;

            // 控制器头文件没有 applyDetailRepostCommentDelta 这类助手，
            // 只能用 setDetailStats 覆盖式写回计数。
            const WeiboBlog &blog = self->detail();
            if (blog.id == statusId) {
                self->setDetailStats(blog.repostsCount + 1, blog.commentsCount + commentDelta,
                                     blog.attitudesCount, blog.attitudesStatus, blog.favorited);
            }

            auto it = m_snapshots.find(statusId);
            if (it != m_snapshots.end() && it->valid) {
                it->blog.repostsCount += 1;
                it->blog.commentsCount += commentDelta;
            }

            patchRepostDeltaEverywhere(self, statusId, 1, commentDelta);
            emit self->toastMessage(QStringLiteral("转发成功"));
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("转发失败：%1").arg(msg));
        });
}

void WeiboStatusModule::remove(const QString &id) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/delete"), body,
        [this, guard, statusId](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;

            removeEverywhere(self, statusId);
            m_snapshots.remove(statusId);
            if (m_activeId == statusId) {
                m_activeId.clear();
                self->clearDetail();
            }
            emit statusRemoved(statusId);
            emit self->toastMessage(QStringLiteral("已删除"));
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("删除失败：%1").arg(msg));
        });
}

// ====== 模型访问器 ======

QObject *WeiboStatusModule::repostModel() {
    return m_controller ? m_controller->repostModel() : nullptr;
}

QObject *WeiboStatusModule::likerModel() {
    return m_controller ? m_controller->likerModel() : nullptr;
}
