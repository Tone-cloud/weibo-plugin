#include "modules/topic/WeiboTopicModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QtGlobal>

namespace {

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

// 控制器头文件没有「当前话题」模型，fetchTopicDetail 的结果无处可放：
// 只能按 container_id 缓存在文件级映射里（仅 GUI 线程访问，模块只有一个实例）。
QHash<QString, WeiboTopic> s_topicCache;

// 头文件同样没有保存话题搜索关键词的成员，searchMoreTopics 需要复用它，
// 这里按模块指针记录（仅 GUI 线程访问）。
QHash<const WeiboTopicModule *, QString> s_topicKeyword;

}  // namespace

WeiboTopicModule::WeiboTopicModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 我的超话 ======

void WeiboTopicModule::fetchMyTopics(int page) {
    if (!m_controller)
        return;

    TopicListModel *model = m_controller->myTopicModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    page = qBound(1, page, 200);
    if (page <= 1)
        model->clear();

    m_myTopicPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/user/topics"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            model->appendItems(parseTopicItems(data));
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);
            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("暂无超话"));
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
            emit self->toastMessage(QStringLiteral("我的超话加载失败：%1").arg(msg));
        });
}

void WeiboTopicModule::fetchMoreMyTopics() {
    if (!m_controller)
        return;
    TopicListModel *model = m_controller->myTopicModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    fetchMyTopics(m_myTopicPage + 1);
}

void WeiboTopicModule::refreshCheckinState() {
    if (!m_controller)
        return;
    // 先把本地勾选状态清掉，再拉第一页，保证签到标记完全来自服务端。
    if (TopicListModel *model = m_controller->myTopicModel())
        model->markAllCheckable(false);
    fetchMyTopics(1);
}

// ====== 超话详情 ======

void WeiboTopicModule::fetchTopicDetail(const QString &containerId) {
    if (!m_controller)
        return;

    const QString topicKey = containerId.trimmed();
    if (topicKey.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_activeContainerId = topicKey;
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("container_id"), topicKey);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/topic/detail"), params,
        [guard, topicKey](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const WeiboTopic topic = TopicListModel::parseTopic(WeiboJson::obj(data, "topic"));
            s_topicCache.insert(topicKey, topic);
            if (!topic.containerId.isEmpty())
                s_topicCache.insert(topic.containerId, topic);

            // 超话：把服务端的签到 / 等级 / 经验合并回「我的超话」列表。
            if (topic.isSuper && !topic.id.isEmpty()) {
                if (TopicListModel *model = self->myTopicModel())
                    model->setChecked(topic.id, topic.checked, topic.signedDays, topic.exp);
            }

            self->setIsLoading(false);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("话题详情加载失败：%1").arg(msg));
        });
}

// ====== 超话微博流 ======

void WeiboTopicModule::fetchTopicStatuses(const QString &containerId, const QString &sinceId) {
    if (!m_controller)
        return;

    BlogListModel *model = m_controller->topicStatusModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString topicKey = containerId.trimmed();
    if (topicKey.isEmpty())
        return;

    const QString since = sinceId.trimmed();
    if (since.isEmpty()) {
        model->clear();
        m_topicSinceId.clear();
    }

    m_activeContainerId = topicKey;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("container_id"), topicKey);
    if (!since.isEmpty())
        params.insert(QStringLiteral("since_id"), since);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/topic/statuses"), params,
        [this, guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            model->appendItems(parseBlogItems(data));
            m_topicSinceId = WeiboJson::str(data, "since_id");
            // 没有游标就无法继续翻页，直接收口。
            model->setHasMore(WeiboJson::boolean(data, "has_more", false) &&
                              !m_topicSinceId.isEmpty());
            model->setLoading(false);
            self->setIsLoading(false);

            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("超话暂无微博"));
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
            emit self->toastMessage(QStringLiteral("超话微博加载失败：%1").arg(msg));
        });
}

void WeiboTopicModule::fetchMoreTopicStatuses() {
    if (!m_controller)
        return;
    BlogListModel *model = m_controller->topicStatusModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    if (m_activeContainerId.isEmpty() || m_topicSinceId.isEmpty())
        return;
    fetchTopicStatuses(m_activeContainerId, m_topicSinceId);
}

// ====== 话题搜索 ======

void WeiboTopicModule::searchTopics(const QString &q, int page) {
    if (!m_controller)
        return;

    TopicListModel *model = m_controller->topicSearchModel();
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
    if (page <= 1 || s_topicKeyword.value(this) != keyword)
        model->clear();

    s_topicKeyword.insert(this, keyword);
    m_topicSearchPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("q"), keyword);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/topic/search"), params,
        [guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;
            model->appendItems(parseTopicItems(data));
            model->setHasMore(WeiboJson::boolean(data, "has_more", false));
            model->setLoading(false);
            self->setIsLoading(false);
            if (model->count() == 0)
                model->setErrorMessage(QStringLiteral("未找到相关话题"));
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
            emit self->toastMessage(QStringLiteral("话题搜索失败：%1").arg(msg));
        });
}

void WeiboTopicModule::searchMoreTopics() {
    if (!m_controller)
        return;
    TopicListModel *model = m_controller->topicSearchModel();
    if (!model || model->loading() || !model->hasMore())
        return;
    const QString keyword = s_topicKeyword.value(this);
    if (keyword.isEmpty())
        return;
    searchTopics(keyword, m_topicSearchPage + 1);
}

// ====== 超话签到 ======

void WeiboTopicModule::checkin(const QString &id, const QString &name) {
    if (!m_controller)
        return;

    const QString topicId = id.trimmed();
    if (topicId.isEmpty()) {
        emit checkinFinished(topicId, false, QStringLiteral("超话 ID 无效"));
        return;
    }

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString topicName = name.trimmed().isEmpty() ? topicId : name.trimmed();

    QJsonObject body;
    body.insert(QStringLiteral("id"), topicId);
    body.insert(QStringLiteral("name"), topicName);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/topic/checkin"), body,
        [this, guard, topicId, topicName](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const int expAdd = static_cast<int>(WeiboJson::num(data, "exp_add", 0));
            const QString message =
                WeiboJson::str(data, "message", QStringLiteral("签到成功"));

            if (TopicListModel *model = self->myTopicModel()) {
                int signedDays = 1;
                int exp = expAdd;
                const QVector<WeiboTopic> &topics = model->items();
                for (const WeiboTopic &topic : topics) {
                    if (topic.id == topicId) {
                        signedDays = topic.signedDays + 1;
                        exp = topic.exp + expAdd;
                        break;
                    }
                }
                model->setChecked(topicId, true, signedDays, exp);
            }

            emit checkinFinished(topicId, true, message);
            emit self->toastMessage(QStringLiteral("%1 签到成功").arg(topicName));
        },
        [this, guard, topicId](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit checkinFinished(topicId, false, msg);
            emit self->toastMessage(QStringLiteral("签到失败：%1").arg(msg));
        });
}

void WeiboTopicModule::checkinAll() {
    if (!m_controller)
        return;

    if (!m_controller->loggedIn()) {
        emit m_controller->toastMessage(QStringLiteral("请先登录"));
        return;
    }
    if (m_batchRunning)
        return;

    m_batchIds.clear();
    m_batchNames.clear();

    // 只处理「可签到的超话且今天还没签」的条目。
    if (TopicListModel *model = m_controller->myTopicModel()) {
        const QVector<WeiboTopic> &topics = model->items();
        for (const WeiboTopic &topic : topics) {
            if (!topic.isSuper || topic.checked)
                continue;
            m_batchIds.append(topic.id);
            m_batchNames.append(topic.name);
        }
    }

    m_batchIndex = 0;
    m_batchSuccess = 0;
    m_batchFailed = 0;
    m_batchCancelled = false;
    m_batchRunning = true;
    m_controller->setCheckinState(true, 0, 0, QStringLiteral("正在批量签到…"));
    runNextCheckin();
}

void WeiboTopicModule::cancelCheckinAll() {
    // 批量流程在「请求 → 回调 → 250ms 定时器」之间循环，任一时刻都有下一步在排队，
    // 因此只要置位，runNextCheckin 会在下一个 tick 收尾。
    m_batchCancelled = true;
}

// 逐个串行签到：直接走 network，避免递归调用 checkin() 导致 N 次 toast。
void WeiboTopicModule::runNextCheckin() {
    if (!m_controller)
        return;

    if (m_batchCancelled || m_batchIndex >= m_batchIds.size()) {
        if (!m_batchRunning)
            return;  // 已经收尾过，别重复弹提示
        m_batchRunning = false;

        const QString summary = m_batchIds.isEmpty()
                                    ? QStringLiteral("全部已签到")
                                    : QStringLiteral("签到完成：成功 %1，失败 %2")
                                          .arg(m_batchSuccess)
                                          .arg(m_batchFailed);
        m_controller->setCheckinState(false, m_batchSuccess, m_batchFailed, summary);
        emit checkinAllFinished(m_batchSuccess, m_batchFailed);
        emit m_controller->toastMessage(summary);

        m_batchIds.clear();
        m_batchNames.clear();
        m_batchIndex = 0;
        return;
    }

    WeiboNetwork *network = m_controller->network();
    if (!network) {
        // sidecar 不可用：直接收尾，避免 m_batchRunning 永远停在 true。
        m_batchRunning = false;
        m_batchIds.clear();
        m_batchNames.clear();
        m_controller->setCheckinState(false, m_batchSuccess, m_batchFailed,
                                      QStringLiteral("批量签到不可用"));
        emit checkinAllFinished(m_batchSuccess, m_batchFailed);
        return;
    }

    const QString topicId = m_batchIds.at(m_batchIndex);
    const QString topicName =
        m_batchIndex < m_batchNames.size() ? m_batchNames.at(m_batchIndex) : topicId;

    QJsonObject body;
    body.insert(QStringLiteral("id"), topicId);
    body.insert(QStringLiteral("name"), topicName);

    QPointer<WeiboController> guard(m_controller);
    QPointer<WeiboTopicModule> selfGuard(this);

    network->post(
        QStringLiteral("/topic/checkin"), body,
        [this, guard, selfGuard, topicId](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !selfGuard)
                return;

            const int expAdd = static_cast<int>(WeiboJson::num(data, "exp_add", 0));
            if (TopicListModel *model = self->myTopicModel()) {
                int signedDays = 1;
                int exp = expAdd;
                const QVector<WeiboTopic> &topics = model->items();
                for (const WeiboTopic &topic : topics) {
                    if (topic.id == topicId) {
                        signedDays = topic.signedDays + 1;
                        exp = topic.exp + expAdd;
                        break;
                    }
                }
                model->setChecked(topicId, true, signedDays, exp);
            }

            ++m_batchSuccess;
            ++m_batchIndex;
            self->setCheckinState(true, m_batchSuccess, m_batchFailed,
                                  QStringLiteral("正在批量签到…"));
            QTimer::singleShot(250, this, [this] { runNextCheckin(); });  // 250ms 间隔防风控
        },
        [this, guard, selfGuard](int code, const QString &msg) {
            Q_UNUSED(msg)
            WeiboController *self = guard.data();
            if (!self || !selfGuard)
                return;

            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();

            ++m_batchFailed;
            ++m_batchIndex;
            self->setCheckinState(true, m_batchSuccess, m_batchFailed,
                                  QStringLiteral("正在批量签到…"));
            QTimer::singleShot(250, this, [this] { runNextCheckin(); });
        });
}

// ====== 模型访问器 ======

QObject *WeiboTopicModule::topicModel() {
    return m_controller ? m_controller->topicSearchModel() : nullptr;
}

QObject *WeiboTopicModule::myTopicModel() {
    return m_controller ? m_controller->myTopicModel() : nullptr;
}

QObject *WeiboTopicModule::statusModel() {
    return m_controller ? m_controller->topicStatusModel() : nullptr;
}
