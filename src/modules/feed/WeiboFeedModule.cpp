#include "modules/feed/WeiboFeedModule.h"

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

// 契约（SPEC 第 3 节）：列表统一放在 data.items，且恒为数组。
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

QVector<WeiboHotItem> parseHotItems(const QJsonObject &data) {
    const QJsonArray items = WeiboJson::arr(data, "items");
    QVector<WeiboHotItem> hots;
    hots.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject())
            hots.append(HotSearchModel::parseHotItem(value.toObject()));
    }
    return hots;
}

// has_more 为真但没给游标时强制收口，否则 fetchMoreXxx 会原地打转。
bool hasMoreWithCursor(const QJsonObject &data, const QString &cursor) {
    if (!WeiboJson::boolean(data, "has_more", false))
        return false;
    return !cursor.isEmpty();
}

}  // namespace

WeiboFeedModule::WeiboFeedModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 模型别名（保持与 bili QML 调用习惯一致） ======

QObject *WeiboFeedModule::popularModel() {
    return m_controller ? m_controller->homeModel() : nullptr;
}

QObject *WeiboFeedModule::dynamicModel() {
    return m_controller ? m_controller->followModel() : nullptr;
}

QObject *WeiboFeedModule::rankingModel() {
    return m_controller ? m_controller->hotStatusModel() : nullptr;
}

QObject *WeiboFeedModule::hotSearchModel() {
    return m_controller ? m_controller->hotSearchModel() : nullptr;
}

// ====== 首页推荐流 /feed/home ======

void WeiboFeedModule::fetchHome(const QString &sinceId, int freshType) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->homeModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString since = sinceId.trimmed();
    const bool firstPage = since.isEmpty();
    freshType = qBound(0, freshType, 9);

    if (firstPage) {
        model->clear();  // 首屏 / 下拉刷新：整段重来
        m_homeSinceId.clear();
    }
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("fresh_type"), QString::number(freshType));
    if (!since.isEmpty())
        params.insert(QStringLiteral("since_id"), since);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/feed/home"), params,
        [this, guard, model, firstPage](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            const QString nextSince = WeiboJson::str(data, "since_id");
            m_homeSinceId = nextSince;
            model->setHasMore(hasMoreWithCursor(data, nextSince));
            model->setLoading(false);
            self->setIsLoading(false);

            if (firstPage) {
                m_homeLoaded = true;
                if (model->count() == 0)
                    model->setErrorMessage(QStringLiteral("暂无微博"));
                emit homeRefreshed();
            }
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
            emit self->toastMessage(QStringLiteral("首页加载失败：%1").arg(msg));
        });
}

void WeiboFeedModule::fetchMoreHome() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->homeModel();
    if (!model || model->loading() || !model->hasMore() || m_homeSinceId.isEmpty())
        return;
    fetchHome(m_homeSinceId, 0);
}

void WeiboFeedModule::refreshHome() {
    fetchHome(QString(), 1);
}

// ====== 关注流 /feed/follow ======

void WeiboFeedModule::fetchFollow(const QString &sinceId) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->followModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString since = sinceId.trimmed();
    const bool firstPage = since.isEmpty();

    if (firstPage) {
        model->clear();
        m_followSinceId.clear();
    }
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    if (!since.isEmpty())
        params.insert(QStringLiteral("since_id"), since);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/feed/follow"), params,
        [this, guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            const QString nextSince = WeiboJson::str(data, "since_id");
            m_followSinceId = nextSince;
            model->setHasMore(hasMoreWithCursor(data, nextSince));
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("暂无关注动态"));
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
            emit self->toastMessage(QStringLiteral("关注流加载失败：%1").arg(msg));
        });
}

void WeiboFeedModule::fetchMoreFollow() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->followModel();
    if (!model || model->loading() || !model->hasMore() || m_followSinceId.isEmpty())
        return;
    fetchFollow(m_followSinceId);
}

// ====== 分组流 /feed/group ======

void WeiboFeedModule::fetchGroup(const QString &gid, const QString &sinceId) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->groupModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QString groupId = gid.trimmed();
    if (groupId.isEmpty())
        groupId = m_groupGid;  // 翻页 / 刷新时沿用上一次的 gid
    if (groupId.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("分组无效"));
        return;
    }

    const QString since = sinceId.trimmed();
    const bool firstPage = since.isEmpty() || groupId != m_groupGid;

    if (firstPage) {
        model->clear();
        m_groupSinceId.clear();
    }
    m_groupGid = groupId;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("gid"), groupId);
    if (!since.isEmpty())
        params.insert(QStringLiteral("since_id"), since);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/feed/group"), params,
        [this, guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            const QString nextSince = WeiboJson::str(data, "since_id");
            m_groupSinceId = nextSince;
            model->setHasMore(hasMoreWithCursor(data, nextSince));
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("该分组暂无微博"));
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
            emit self->toastMessage(QStringLiteral("分组流加载失败：%1").arg(msg));
        });
}

void WeiboFeedModule::fetchMoreGroup() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->groupModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    if (m_groupGid.isEmpty() || m_groupSinceId.isEmpty())
        return;
    fetchGroup(m_groupGid, m_groupSinceId);
}

// ====== 热搜榜 /feed/hot ======

void WeiboFeedModule::fetchHot() {
    if (!m_controller)
        return;

    HotSearchModel *model = m_controller->hotSearchModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    model->setLoading(true);
    m_controller->setIsLoading(true);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/feed/hot"), QMap<QString, QString>(),
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            // HotSearchModel 只有 loading，没有 errorMessage 字段。
            model->setItems(parseHotItems(data));
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
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("热搜加载失败：%1").arg(msg));
        });
}

// ====== 热搜词微博流 /feed/hot/status ======

void WeiboFeedModule::fetchHotStatus(const QString &word, const QString &sinceId) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->hotStatusModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString keyword = word.trimmed();
    if (keyword.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("热搜词为空"));
        return;
    }

    const QString since = sinceId.trimmed();
    const bool firstPage = since.isEmpty() || keyword != m_hotWord;

    if (firstPage) {
        model->clear();
        m_hotSinceId.clear();
    }
    m_hotWord = keyword;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("word"), keyword);
    if (!since.isEmpty())
        params.insert(QStringLiteral("since_id"), since);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/feed/hot/status"), params,
        [this, guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            const QString nextSince = WeiboJson::str(data, "since_id");
            m_hotSinceId = nextSince;
            model->setHasMore(hasMoreWithCursor(data, nextSince));
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("暂无相关微博"));
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
            emit self->toastMessage(QStringLiteral("热搜微博加载失败：%1").arg(msg));
        });
}

void WeiboFeedModule::fetchMoreHotStatus() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->hotStatusModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    if (m_hotWord.isEmpty() || m_hotSinceId.isEmpty())
        return;
    fetchHotStatus(m_hotWord, m_hotSinceId);
}
