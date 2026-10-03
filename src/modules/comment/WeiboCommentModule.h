#pragma once

#include <QObject>
#include <QString>

class WeiboController;

// 评论：一级评论、子评论、发评论/回复、删评论、评论点赞。
class WeiboCommentModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboCommentModule(WeiboController *controller);

    // page >= 1；maxId / maxIdType 由上一次响应的游标给出
    Q_INVOKABLE void fetchComments(const QString &id, int page = 1,
                                   qint64 maxId = 0, int maxIdType = 0);
    Q_INVOKABLE void fetchMoreComments();
    Q_INVOKABLE void refreshComments();

    Q_INVOKABLE void fetchReplies(const QString &id, const QString &cid, int page = 1);
    Q_INVOKABLE void fetchMoreReplies();

    Q_INVOKABLE void postComment(const QString &id, const QString &content,
                                 const QString &cid = QString(),
                                 bool alsoRepost = false);
    Q_INVOKABLE void deleteComment(const QString &cid);
    Q_INVOKABLE void likeComment(const QString &cid, bool liked);

    // 「回复 @某人」上下文（CommentsPage 用）
    Q_INVOKABLE void setReplyTo(const QString &cid, const QString &name);
    Q_INVOKABLE void clearReplyTo();
    Q_INVOKABLE QString replyCid() const { return m_replyCid; }
    Q_INVOKABLE QString replyName() const { return m_replyName; }

    Q_INVOKABLE QObject *commentModel();
    Q_INVOKABLE QObject *replyModel();

    QString activeId() const { return m_activeId; }
    Q_INVOKABLE void setActiveId(const QString &id);

signals:
    void commentPosted();
    void commentDeleted(const QString &cid);
    void repliesTargetChanged(const QString &cid, const QString &name);

private:
    WeiboController *m_controller = nullptr;
    QString m_activeId;
    qint64 m_maxId = 0;
    int m_maxIdType = 0;
    int m_page = 1;
    QString m_replyCid;
    QString m_replyName;
    QString m_repliesId;
    QString m_repliesCid;
    int m_repliesPage = 1;
};
