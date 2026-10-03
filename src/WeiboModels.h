#pragma once

#include <QAbstractListModel>
#include <QChar>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

// ======================= 归一化数据结构 =======================
// 与 docs/SPEC.md 第 3 节的 JSON 契约一一对应。
// 所有结构都由 Go sidecar 归一化好的 JSON 解析而来，C++ 侧不做上游字段猜测。

struct WeiboPicture {
    QString url;    // 展示用（通常带 @ 尺寸后缀或已压缩）
    QString large;  // 原图
    int width = 0;
    int height = 0;
};

struct WeiboUser {
    qint64 uid = 0;
    QString name;
    QString avatar;
    QString cover;
    bool verified = false;
    int verifiedType = -1;  // 0 黄V / 2 蓝V / 7 企业蓝V / -1 无
    QString verifiedReason;
    QString description;
    qint64 followers = 0;
    qint64 following = 0;
    qint64 statusesCount = 0;
    QString gender;   // m / f / n
    QString location;
    bool followingMe = false;
    bool isFollowing = false;  // 我是否已关注 TA
    bool isMe = false;
};

struct WeiboPageInfo {
    QString type = QStringLiteral("none");  // none|video|live|article|music
    QString title;
    QString cover;
    QString url;
    QString mediaUrl;
    int duration = 0;
    int liveStatus = 0;
};

struct WeiboBlog {
    QString id;
    QString bid;
    QString text;
    QString textHtml;
    QString createdAt;
    qint64 createdTs = 0;
    QString source;
    QString regionName;
    bool isLongText = false;

    WeiboUser author;
    QVector<WeiboPicture> pics;
    WeiboPageInfo page;

    // 转发的原微博（只保留一层，避免无限嵌套）
    bool hasRetweeted = false;
    QString retweetedId;
    QString retweetedAuthorName;
    qint64 retweetedAuthorId = 0;
    QString retweetedAuthorAvatar;
    QString retweetedText;
    QString retweetedTextHtml;
    QVector<WeiboPicture> retweetedPics;
    WeiboPageInfo retweetedPage;

    qint64 repostsCount = 0;
    qint64 commentsCount = 0;
    qint64 attitudesCount = 0;
    int attitudesStatus = 0;  // 0 未赞 / 1 已赞
    bool favorited = false;
    bool canDelete = false;
    QStringList topicIds;
};

struct WeiboComment {
    QString id;
    QString text;
    QString textHtml;
    QString createdAt;
    qint64 createdTs = 0;
    qint64 likeCount = 0;
    bool liked = false;
    qint64 replyCount = 0;
    WeiboUser user;
    QString replyTo;
    QVector<WeiboPicture> pics;
    bool canDelete = false;
};

struct WeiboHotItem {
    int rank = 0;
    QString word;
    qint64 rawHot = 0;
    QString label;
    QString url;
    QString category;
};

struct WeiboTopic {
    QString id;
    QString containerId;
    QString name;
    QString desc;
    QString cover;
    qint64 readCount = 0;
    qint64 discussCount = 0;
    qint64 fansCount = 0;
    bool isSuper = false;  // 超话（可签到）vs 普通话题
    int level = 0;
    int exp = 0;
    int rank = 0;
    bool checked = false;
    int signedDays = 0;
    QString type;  // super | topic
};

struct WeiboMediaQuality {
    QString label;
    QString url;
};

// ======================= 列表模型 =======================

// ---- 微博列表（信息流 / 搜索 / 用户微博 / 超话流 / 转发列表 共用） ----

class BlogListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        BidRole,
        TextRole,
        TextHtmlRole,
        CreatedAtRole,
        CreatedTsRole,
        CreatedTextRole,
        SourceRole,
        RegionNameRole,
        IsLongTextRole,
        AuthorIdRole,
        AuthorNameRole,
        AuthorAvatarRole,
        AuthorVerifiedRole,
        AuthorVerifiedTypeRole,
        AuthorVerifiedReasonRole,
        PicsRole,
        PicCountRole,
        FirstPicRole,
        PageTypeRole,
        PageTitleRole,
        PageCoverRole,
        PageUrlRole,
        PageMediaUrlRole,
        PageDurationRole,
        PageLiveStatusRole,
        HasMediaRole,
        HasRetweetedRole,
        RetweetedIdRole,
        RetweetedAuthorIdRole,
        RetweetedAuthorNameRole,
        RetweetedAuthorAvatarRole,
        RetweetedTextRole,
        RetweetedTextHtmlRole,
        RetweetedPicsRole,
        RetweetedPicCountRole,
        RetweetedFirstPicRole,
        RetweetedPageTypeRole,
        RetweetedPageTitleRole,
        RetweetedPageCoverRole,
        RetweetedPageUrlRole,
        RetweetedPageMediaUrlRole,
        RepostsCountRole,
        CommentsCountRole,
        AttitudesCountRole,
        AttitudesStatusRole,
        FavoritedRole,
        CanDeleteRole,
        RepostsCountTextRole,
        CommentsCountTextRole,
        AttitudesCountTextRole,
        TopicIdsRole
    };
    Q_ENUM(Roles)

    explicit BlogListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE QString idAt(int row) const;
    Q_INVOKABLE QVariantMap at(int row) const;
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void removeById(const QString &id);

    void appendItems(const QVector<WeiboBlog> &items);
    void prependItems(const QVector<WeiboBlog> &items);
    void setLoading(bool loading);
    void setHasMore(bool hasMore);
    void setErrorMessage(const QString &msg);
    // 就地更新点赞/收藏/计数状态；返回是否命中
    bool applyLikeState(const QString &id, bool liked, qint64 attitudesCount);
    bool applyFavoriteState(const QString &id, bool favorited);
    bool applyRepostCommentDelta(const QString &id, int repostDelta, int commentDelta);

    const QVector<WeiboBlog> &items() const { return m_items; }
    static WeiboBlog parseBlog(const QJsonObject &obj);

signals:
    void countChanged();
    void loadingChanged();
    void hasMoreChanged();
    void errorMessageChanged();

private:
    QVector<WeiboBlog> m_items;
    bool m_loading = false;
    bool m_hasMore = true;
    QString m_errorMessage;
};

// ---- 评论列表 ----

class CommentListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TextRole,
        TextHtmlRole,
        CreatedAtRole,
        CreatedTsRole,
        CreatedTextRole,
        LikeCountRole,
        LikedRole,
        ReplyCountRole,
        UserIdRole,
        UserNameRole,
        UserAvatarRole,
        UserVerifiedRole,
        ReplyToRole,
        PicsRole,
        PicCountRole,
        FirstPicRole,
        CanDeleteRole
    };
    Q_ENUM(Roles)

    explicit CommentListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }
    int totalCount() const { return m_totalCount; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE int indexOfId(const QString &id) const;
    Q_INVOKABLE void removeById(const QString &id);

    void appendItems(const QVector<WeiboComment> &items);
    void prependItems(const QVector<WeiboComment> &items);
    void setLoading(bool loading);
    void setTotalCount(int total);
    void setErrorMessage(const QString &msg);
    bool setLikeState(const QString &id, bool liked, qint64 likeCount);

    static WeiboComment parseComment(const QJsonObject &obj);

signals:
    void countChanged();
    void loadingChanged();
    void totalCountChanged();
    void errorMessageChanged();

private:
    QVector<WeiboComment> m_items;
    int m_totalCount = 0;
    bool m_loading = false;
    QString m_errorMessage;
};

// ---- 子评论列表 ----

class CommentReplyListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TextRole,
        TextHtmlRole,
        CreatedAtRole,
        CreatedTextRole,
        LikeCountRole,
        LikedRole,
        ReplyCountRole,
        UserIdRole,
        UserNameRole,
        UserAvatarRole,
        UserVerifiedRole,
        ReplyToRole,
        PicsRole,
        PicCountRole,
        FirstPicRole,
        CanDeleteRole
    };
    Q_ENUM(Roles)

    explicit CommentReplyListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }
    int totalCount() const { return m_totalCount; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE void removeById(const QString &id);

    void setItems(const QVector<WeiboComment> &items);
    void appendItems(const QVector<WeiboComment> &items);
    void setLoading(bool loading);
    void setTotalCount(int total);
    void setErrorMessage(const QString &msg);
    bool setLikeState(const QString &id, bool liked, qint64 likeCount);

signals:
    void countChanged();
    void loadingChanged();
    void totalCountChanged();
    void errorMessageChanged();

private:
    QVector<WeiboComment> m_items;
    int m_totalCount = 0;
    bool m_loading = false;
    QString m_errorMessage;
};

// ---- 热搜榜 ----

class HotSearchModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    enum Roles {
        RankRole = Qt::UserRole + 1,
        WordRole,
        RawHotRole,
        HotTextRole,
        LabelRole,
        UrlRole,
        CategoryRole
    };
    Q_ENUM(Roles)

    explicit HotSearchModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE QString wordAt(int row) const;

    void setItems(const QVector<WeiboHotItem> &items);
    void setLoading(bool loading);

    static WeiboHotItem parseHotItem(const QJsonObject &obj);

signals:
    void countChanged();
    void loadingChanged();

private:
    QVector<WeiboHotItem> m_items;
    bool m_loading = false;
};

// ---- 用户列表（关注 / 粉丝 / 用户搜索 / 点赞列表） ----

class UserListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY totalCountChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        UidRole = Qt::UserRole + 1,
        NameRole,
        AvatarRole,
        CoverRole,
        VerifiedRole,
        VerifiedTypeRole,
        VerifiedReasonRole,
        DescriptionRole,
        FollowersCountRole,
        FollowingCountRole,
        StatusesCountRole,
        FollowersTextRole,
        GenderRole,
        LocationRole,
        FollowingMeRole,
        IsFollowingRole,
        IsMeRole
    };
    Q_ENUM(Roles)

    explicit UserListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }
    int totalCount() const { return m_totalCount; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE int indexOfUid(qint64 uid) const;
    Q_INVOKABLE void removeAt(int row);

    void appendItems(const QVector<WeiboUser> &items);
    void setItems(const QVector<WeiboUser> &items);
    void setLoading(bool loading);
    void setHasMore(bool hasMore);
    void setTotalCount(int total);
    void setErrorMessage(const QString &msg);
    bool setFollowState(qint64 uid, bool following);

    static WeiboUser parseUser(const QJsonObject &obj);

signals:
    void countChanged();
    void loadingChanged();
    void hasMoreChanged();
    void totalCountChanged();
    void errorMessageChanged();

private:
    QVector<WeiboUser> m_items;
    bool m_loading = false;
    bool m_hasMore = true;
    int m_totalCount = 0;
    QString m_errorMessage;
};

// ---- 话题 / 超话列表 ----

class TopicListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedCountChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        ContainerIdRole,
        NameRole,
        DescRole,
        CoverRole,
        ReadCountRole,
        DiscussCountRole,
        FansCountRole,
        ReadTextRole,
        DiscussTextRole,
        IsSuperRole,
        LevelRole,
        ExpRole,
        RankRole,
        CheckedRole,
        SignedDaysRole,
        TypeRole,
        StatusTextRole
    };
    Q_ENUM(Roles)

    explicit TopicListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }
    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }
    int checkedCount() const;
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE void clear();
    Q_INVOKABLE int indexOfId(const QString &id) const;

    void appendItems(const QVector<WeiboTopic> &items);
    void setItems(const QVector<WeiboTopic> &items);
    void setLoading(bool loading);
    void setHasMore(bool hasMore);
    void setErrorMessage(const QString &msg);
    // 签到状态回写；返回是否命中
    bool setChecked(const QString &id, bool checked, int signedDays, int exp);
    void markAllCheckable(bool checked);

    const QVector<WeiboTopic> &items() const { return m_items; }
    static WeiboTopic parseTopic(const QJsonObject &obj);

signals:
    void countChanged();
    void loadingChanged();
    void hasMoreChanged();
    void checkedCountChanged();
    void errorMessageChanged();

private:
    QVector<WeiboTopic> m_items;
    bool m_loading = false;
    bool m_hasMore = true;
    QString m_errorMessage;
};

// ---- 图片列表（详情页 / 图片查看器） ----

class PictureListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        UrlRole = Qt::UserRole + 1,
        LargeRole,
        WidthRole,
        HeightRole,
        IndexRole
    };
    Q_ENUM(Roles)

    explicit PictureListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_items.size(); }

    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantList toVariantList() const;

    void setItems(const QVector<WeiboPicture> &items);
    static WeiboPicture parsePicture(const QJsonObject &obj);
    static QVariantMap pictureToMap(const WeiboPicture &pic, int index = -1);

signals:
    void countChanged();

private:
    QVector<WeiboPicture> m_items;
};

// ---- 搜索历史（本地持久化，最大 30 条） ----

class SearchHistoryModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles { WordRole = Qt::UserRole + 1 };

    explicit SearchHistoryModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return m_words.size(); }

    Q_INVOKABLE void add(const QString &word);
    Q_INVOKABLE void remove(const QString &word);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString wordAt(int row) const;

    QStringList words() const { return m_words; }
    void setWords(const QStringList &words);

    // 落盘到 /userdisk/PenMods/plugins/weibo_plugin/search_history.json
    void load();
    void save() const;
    static QString storagePath();

signals:
    void countChanged();

private:
    QStringList m_words;
};
