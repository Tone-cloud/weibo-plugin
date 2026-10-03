#include "modules/comment/WeiboCommentModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QModelIndex>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVector>
#include <QtGlobal>

namespace {

QVector<WeiboComment> parseCommentItems(const QJsonObject &data) {
    const QJsonArray items = WeiboJson::arr(data, "items");
    QVector<WeiboComment> comments;
    comments.reserve(items.size());
    for (const QJsonValue &value : items) {
        if (value.isObject())
            comments.append(CommentListModel::parseComment(value.toObject()));
    }
    return comments;
}

// 一级评论列表可以直接按 id 定位。
qint64 commentLikeCount(CommentListModel *model, const QString &cid) {
    if (!model)
        return 0;
    const int row = model->indexOfId(cid);
    if (row < 0)
        return 0;
    return model->data(model->index(row, 0), CommentListModel::LikeCountRole).toLongLong();
}

// 子评论模型没有 indexOfId，只能逐行比对 IdRole。
qint64 replyLikeCount(CommentReplyListModel *model, const QString &cid) {
    if (!model)
        return 0;
    const int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model->index(row, 0);
        if (model->data(index, CommentReplyListModel::IdRole).toString() == cid)
            return model->data(index, CommentReplyListModel::LikeCountRole).toLongLong();
    }
    return 0;
}

}  // namespace

WeiboCommentModule::WeiboCommentModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 一级评论 /status/comments ======

void WeiboCommentModule::fetchComments(const QString &id, int page, qint64 maxId, int maxIdType) {
    if (!m_controller)
        return;

    CommentListModel *model = m_controller->commentModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    page = qBound(1, page, 500);
    maxId = qMax<qint64>(0, maxId);
    maxIdType = qBound(0, maxIdType, 1);

    const bool firstPage = (page <= 1 || m_activeId != statusId);
    if (firstPage) {
        model->clear();
        m_maxId = 0;
        m_maxIdType = 0;
    }

    m_activeId = statusId;
    m_page = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);
    params.insert(QStringLiteral("page"), QString::number(page));
    params.insert(QStringLiteral("max_id"), QString::number(maxId));
    params.insert(QStringLiteral("max_id_type"), QString::number(maxIdType));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/status/comments"), params,
        [this, guard, model](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            const QVector<WeiboComment> comments = parseCommentItems(data);
            if (model->count() == 0 && m_page <= 1)
                model->clear();
            model->appendItems(comments);

            m_maxId = WeiboJson::num(data, "max_id", 0);
            m_maxIdType = static_cast<int>(WeiboJson::num(data, "max_id_type", 0));
            // CommentListModel 没有 has_more 字段，用游标推导「还有下一页」：
            // 热门评论接口在最后一页回 max_id=0，未到末页回非 0 游标。
            if (!WeiboJson::boolean(data, "has_more", true))
                m_maxId = 0;

            model->setTotalCount(static_cast<int>(WeiboJson::num(data, "total", 0)));
            model->setLoading(false);
            self->setIsLoading(false);

            if (m_page <= 1 && comments.isEmpty())
                model->setErrorMessage(QStringLiteral("暂无评论"));
        },
        [this, guard, model](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            m_maxId = 0;  // 失败后不再往上翻，避免重复请求坏游标
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            model->setLoading(false);
            model->setErrorMessage(msg);
            self->setIsLoading(false);
            emit self->toastMessage(QStringLiteral("评论加载失败：%1").arg(msg));
        });
}

void WeiboCommentModule::fetchMoreComments() {
    if (!m_controller)
        return;
    CommentListModel *model = m_controller->commentModel();
    if (!model || model->loading() || m_activeId.isEmpty())
        return;
    // 末页游标为 0，见 fetchComments 里的说明。
    if (m_maxId == 0)
        return;
    fetchComments(m_activeId, m_page + 1, m_maxId, m_maxIdType);
}

void WeiboCommentModule::refreshComments() {
    if (!m_controller)
        return;
    QString statusId = m_activeId;
    if (statusId.isEmpty())
        statusId = m_controller->detail().id;
    if (statusId.isEmpty())
        return;
    m_activeId = statusId;
    fetchComments(statusId, 1, 0, 0);
}

// ====== 子评论 /status/comments/replies ======

void WeiboCommentModule::fetchReplies(const QString &id, const QString &cid, int page) {
    if (!m_controller)
        return;

    CommentReplyListModel *model = m_controller->commentReplyModel();
    if (!model || model->loading())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    const QString statusId = id.trimmed();
    const QString commentId = cid.trimmed();
    if (statusId.isEmpty() || commentId.isEmpty())
        return;

    page = qBound(1, page, 200);
    // 换了父评论或换了微博就必须整段替换，不能续接。
    const bool fresh = (page <= 1 || m_repliesCid != commentId || m_repliesId != statusId);
    if (fresh)
        model->clear();

    m_repliesId = statusId;
    m_repliesCid = commentId;
    m_repliesPage = page;
    model->setLoading(true);
    model->setErrorMessage(QString());
    m_controller->setIsLoading(true);

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);
    params.insert(QStringLiteral("cid"), commentId);
    params.insert(QStringLiteral("page"), QString::number(page));

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/status/comments/replies"), params,
        [guard, model, fresh](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || !model)
                return;

            const QVector<WeiboComment> replies = parseCommentItems(data);
            if (fresh)
                model->setItems(replies);
            else
                model->appendItems(replies);

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
            emit self->toastMessage(QStringLiteral("回复加载失败：%1").arg(msg));
        });
}

void WeiboCommentModule::fetchMoreReplies() {
    if (!m_controller)
        return;
    CommentReplyListModel *model = m_controller->commentReplyModel();
    if (!model || model->loading())
        return;
    if (m_repliesId.isEmpty() || m_repliesCid.isEmpty())
        return;
    fetchReplies(m_repliesId, m_repliesCid, m_repliesPage + 1);
}

// ====== 写操作 ======

void WeiboCommentModule::postComment(const QString &id, const QString &content,
                                     const QString &cid, bool alsoRepost) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    const QString text = content.trimmed();
    const QString commentId = cid.trimmed();

    if (text.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("评论内容不能为空"));
        return;
    }
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);
    body.insert(QStringLiteral("content"), text);
    body.insert(QStringLiteral("cid"), commentId);
    body.insert(QStringLiteral("also_repost"), alsoRepost);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/comment"), body,
        [this, guard, statusId, commentId](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;

            if (m_activeId.isEmpty())
                m_activeId = statusId;
            emit commentPosted();
            emit self->toastMessage(QStringLiteral("评论成功"));
            clearReplyTo();

            // 一级评论直接刷新列表；子评论重新拉该父评论的回复。
            if (commentId.isEmpty())
                refreshComments();
            else
                fetchReplies(statusId, commentId, 1);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("评论失败：%1").arg(msg));
        });
}

void WeiboCommentModule::deleteComment(const QString &cid) {
    if (!m_controller)
        return;

    const QString commentId = cid.trimmed();
    if (commentId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("cid"), commentId);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/comment/delete"), body,
        [this, guard, commentId](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;

            // 一级评论与子评论两个列表都要摘掉这一行。
            if (CommentListModel *model = self->commentModel())
                model->removeById(commentId);
            if (CommentReplyListModel *model = self->commentReplyModel())
                model->removeById(commentId);
            if (m_replyCid == commentId)
                clearReplyTo();

            emit commentDeleted(commentId);
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

void WeiboCommentModule::likeComment(const QString &cid, bool liked) {
    if (!m_controller)
        return;

    const QString commentId = cid.trimmed();
    if (commentId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    CommentListModel *commentModel = m_controller->commentModel();
    CommentReplyListModel *replyModel = m_controller->commentReplyModel();
    // 旧计数只能从本地模型读；服务端返回值会在下一次刷新时覆盖。
    const qint64 previous = commentLikeCount(commentModel, commentId) > 0
                                ? commentLikeCount(commentModel, commentId)
                                : replyLikeCount(replyModel, commentId);

    QJsonObject body;
    body.insert(QStringLiteral("cid"), commentId);
    body.insert(QStringLiteral("liked"), liked);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/comment/like"), body,
        [guard, commentModel, replyModel, commentId, liked, previous](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self || (!commentModel && !replyModel))
                return;

            const bool result =
                data.contains(QStringLiteral("liked"))
                    ? WeiboJson::boolean(data, "liked", liked)
                    : liked;

            qint64 count = previous + (result ? 1 : -1);
            if (count < 0)
                count = 0;

            if (commentModel)
                commentModel->setLikeState(commentId, result, count);
            if (replyModel)
                replyModel->setLikeState(commentId, result, count);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("点赞失败：%1").arg(msg));
        });
}

// ====== 回复上下文 ======

void WeiboCommentModule::setReplyTo(const QString &cid, const QString &name) {
    m_replyCid = cid.trimmed();
    m_replyName = name.trimmed();
    emit repliesTargetChanged(m_replyCid, m_replyName);
}

void WeiboCommentModule::clearReplyTo() {
    if (m_replyCid.isEmpty() && m_replyName.isEmpty())
        return;
    m_replyCid.clear();
    m_replyName.clear();
    emit repliesTargetChanged(m_replyCid, m_replyName);
}

void WeiboCommentModule::setActiveId(const QString &id) {
    m_activeId = id.trimmed();
}

// ====== 模型访问器 ======

QObject *WeiboCommentModule::commentModel() {
    return m_controller ? m_controller->commentModel() : nullptr;
}

QObject *WeiboCommentModule::replyModel() {
    return m_controller ? m_controller->commentReplyModel() : nullptr;
}
