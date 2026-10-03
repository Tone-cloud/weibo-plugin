#include "modules/search/WeiboSearchModule.h"

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

QVector<WeiboTopic> parseTopicItems(const QJsonObject &data) {
    const QJsonArray items = WeiboJson::arr(data, "items");
    QVector<WeiboTopic> topics;
    topics.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject())
            topics.append(TopicListModel::parseTopic(value.toObject()));
    }
    return topics;
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

// WeiboController::createModels() 只构造模型、不读磁盘，且模块构造时模型还是
// nullptr（控制器成员按声明顺序初始化，createModels 尚未执行）。头文件已冻结、
// 不能新增成员，所以用文件级静态标志做「只加载一次」的懒加载，在第一次用到
// 搜索历史的入口处触发 SearchHistoryModel::load()。
bool s_historyLoaded = false;

void ensureHistoryLoaded(WeiboController *controller) {
    if (s_historyLoaded || !controller)
        return;
    SearchHistoryModel *model = controller->searchHistoryModel();
    if (!model)
        return;
    s_historyLoaded = true;  // 先置位，防止 load() 回调再次进到这里
    model->load();
}

}  // namespace

WeiboSearchModule::WeiboSearchModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 搜索入口 ======

void WeiboSearchModule::search(const QString &q, const QString &kind) {
    if (!m_controller)
        return;

    ensureHistoryLoaded(m_controller);

    const QString keyword = q.trimmed().left(100);
    if (keyword.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("请输入搜索内容"));
        return;
    }

    QString normalized = kind.trimmed().toLower();
    if (normalized != QLatin1String("user") && normalized != QLatin1String("topic"))
        normalized = QStringLiteral("status");

    m_keyword = keyword;
    m_kind = normalized;
    m_statusPage = 1;
    m_userPage = 1;
    m_topicPage = 1;

    saveHistory(keyword);
    emit searchStarted(keyword);

    // 三类结果共用一个搜索页，切换类型时清掉另外两类，避免串页。
    if (normalized == QLatin1String("user")) {
        if (BlogListModel *model = m_controller->searchStatusModel())
            model->clear();
        if (TopicListModel *model = m_controller->topicSearchModel())
            model->clear();
        searchUsers(keyword, 1);
    } else if (normalized == QLatin1String("topic")) {
        if (BlogListModel *model = m_controller->searchStatusModel())
            model->clear();
        if (UserListModel *model = m_controller->userSearchModel())
            model->clear();
        searchTopics(keyword, 1);
    } else {
        if (UserListModel *model = m_controller->userSearchModel())
            model->clear();
        if (TopicListModel *model = m_controller->topicSearchModel())
            model->clear();
        searchStatus(keyword, 1);
    }
}

void WeiboSearchModule::searchStatus(const QString &q, int page) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->searchStatusModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString keyword = q.trimmed().left(100);
    if (keyword.isEmpty())
        return;

    page = qBound(1, page, 200);
    const bool firstPage = (page <= 1 || m_keyword != keyword);
    if (firstPage)
        model->clear();

    m_keyword = keyword;
    m_kind = QStringLiteral("status");
    m_statusPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("q"), keyword);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/search/status"), params,
        // 注意：lambda 里要 `emit searchFinished(...)`，必须捕获 this，
        // 否则 GCC 报 "'this' was not captured for this lambda function"。
        [this, guard, model, firstPage, keyword](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            const QVector<WeiboBlog> blogs = parseBlogItems(data);
            model->appendItems(blogs);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);

            if (firstPage && blogs.isEmpty())
                model->setErrorMessage(QStringLiteral("未找到相关微博"));
            emit searchFinished(keyword, blogs.size());
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
            emit self->toastMessage(QStringLiteral("搜索失败：%1").arg(msg));
        });
}

void WeiboSearchModule::searchUsers(const QString &q, int page) {
    if (!m_controller)
        return;

    UserListModel *model = m_controller->userSearchModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString keyword = q.trimmed().left(100);
    if (keyword.isEmpty())
        return;

    page = qBound(1, page, 200);
    const bool firstPage = (page <= 1 || m_keyword != keyword);
    if (firstPage)
        model->clear();

    m_keyword = keyword;
    m_kind = QStringLiteral("user");
    m_userPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("q"), keyword);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/search/user"), params,
        [this, guard, model, keyword](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            const QVector<WeiboUser> users = parseUserItems(data);
            model->appendItems(users);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setTotalCount(static_cast<int>(WeiboJson::num(data, "total", 0)));
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("未找到相关用户"));
            emit searchFinished(keyword, users.size());
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
            emit self->toastMessage(QStringLiteral("搜索失败：%1").arg(msg));
        });
}

void WeiboSearchModule::searchTopics(const QString &q, int page) {
    if (!m_controller)
        return;

    TopicListModel *model = m_controller->topicSearchModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString keyword = q.trimmed().left(100);
    if (keyword.isEmpty())
        return;

    page = qBound(1, page, 200);
    const bool firstPage = (page <= 1 || m_keyword != keyword);
    if (firstPage)
        model->clear();

    m_keyword = keyword;
    m_kind = QStringLiteral("topic");
    m_topicPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("q"), keyword);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/search/topic"), params,
        [this, guard, model, keyword](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            const QVector<WeiboTopic> topics = parseTopicItems(data);
            model->appendItems(topics);
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("未找到相关话题"));
            emit searchFinished(keyword, topics.size());
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
            emit self->toastMessage(QStringLiteral("搜索失败：%1").arg(msg));
        });
}

void WeiboSearchModule::searchMore() {
    if (!m_controller || m_keyword.isEmpty())
        return;

    if (m_kind == QLatin1String("user")) {
        UserListModel *model = m_controller->userSearchModel();
        if (!model || model->loading() || !model->hasMore())
            return;
        searchUsers(m_keyword, m_userPage + 1);
    } else if (m_kind == QLatin1String("topic")) {
        TopicListModel *model = m_controller->topicSearchModel();
        if (!model || model->loading() || !model->hasMore())
            return;
        searchTopics(m_keyword, m_topicPage + 1);
    } else {
        BlogListModel *model = m_controller->searchStatusModel();
        if (!model || model->loading() || !model->hasMore())
            return;
        searchStatus(m_keyword, m_statusPage + 1);
    }
}

// 「用户」tab 的加载更多：直接锁定 user 分类，不依赖当前 m_kind。
void WeiboSearchModule::searchUsersMore() {
    if (!m_controller || m_keyword.isEmpty())
        return;
    UserListModel *model = m_controller->userSearchModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    searchUsers(m_keyword, m_userPage + 1);
}

// 「话题」tab 的加载更多。
void WeiboSearchModule::searchTopicsMore() {
    if (!m_controller || m_keyword.isEmpty())
        return;
    TopicListModel *model = m_controller->topicSearchModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    searchTopics(m_keyword, m_topicPage + 1);
}

// ====== 热搜榜 ======

void WeiboSearchModule::fetchHotSearch() {
    if (!m_controller)
        return;

    ensureHistoryLoaded(m_controller);

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

// ====== 搜索历史（本地 + 服务端清理） ======

void WeiboSearchModule::saveHistory(const QString &q) {
    if (!m_controller)
        return;
    ensureHistoryLoaded(m_controller);
    SearchHistoryModel *model = m_controller->searchHistoryModel();
    if (!model)
        return;
    const QString word = q.trimmed();
    if (word.isEmpty())
        return;
    model->add(word);
}

void WeiboSearchModule::clearHistory() {
    if (!m_controller)
        return;

    if (SearchHistoryModel *model = m_controller->searchHistoryModel())
        model->clear();

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/search/history/clear"), QJsonObject(),
        [guard](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;
            emit self->toastMessage(QStringLiteral("已清空搜索历史"));
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("清空搜索历史失败：%1").arg(msg));
        });
}

// ====== 模型访问器 ======

QObject *WeiboSearchModule::searchModel() {
    return m_controller ? m_controller->searchStatusModel() : nullptr;
}

QObject *WeiboSearchModule::userModel() {
    return m_controller ? m_controller->userSearchModel() : nullptr;
}

QObject *WeiboSearchModule::topicModel() {
    return m_controller ? m_controller->topicSearchModel() : nullptr;
}

QObject *WeiboSearchModule::hotSearchModel() {
    return m_controller ? m_controller->hotSearchModel() : nullptr;
}

QObject *WeiboSearchModule::historyModel() {
    if (!m_controller)
        return nullptr;
    ensureHistoryLoaded(m_controller);
    return m_controller->searchHistoryModel();
}
