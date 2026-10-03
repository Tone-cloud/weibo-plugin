#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <functional>
#include <memory>

#include "WeiboModels.h"

class WeiboNetwork;
class WeiboFeedModule;
class WeiboStatusModule;
class WeiboCommentModule;
class WeiboSearchModule;
class WeiboProfileModule;
class WeiboLoginModule;
class WeiboPublishModule;
class WeiboTopicModule;
class WeiboMediaModule;
class WeiboViewerModule;

// QML 边界的唯一入口：注册为 `WeiboPlugin 1.0 WeiboController`。
// 页面通过 controller.feed / status / comments / search / profile / login /
// publish / topic / media / viewer 访问各业务模块（与 bili 的 controller.feed
// / controller.video 习惯一致）。
class WeiboController : public QObject {
    Q_OBJECT

    // ---- 子模块 ----
    Q_PROPERTY(QObject *feed READ feed CONSTANT)
    Q_PROPERTY(QObject *status READ status CONSTANT)
    Q_PROPERTY(QObject *comments READ comments CONSTANT)
    Q_PROPERTY(QObject *search READ search CONSTANT)
    Q_PROPERTY(QObject *profile READ profile CONSTANT)
    Q_PROPERTY(QObject *login READ login CONSTANT)
    Q_PROPERTY(QObject *publish READ publish CONSTANT)
    Q_PROPERTY(QObject *topic READ topic CONSTANT)
    Q_PROPERTY(QObject *media READ media CONSTANT)
    Q_PROPERTY(QObject *viewer READ viewer CONSTANT)

    // ---- 登录态 / 全局 ----
    Q_PROPERTY(bool loggedIn READ loggedIn NOTIFY loginStateChanged)
    Q_PROPERTY(qint64 userId READ userId NOTIFY loginStateChanged)
    Q_PROPERTY(QString userName READ userName NOTIFY loginStateChanged)
    Q_PROPERTY(QString userAvatar READ userAvatar NOTIFY loginStateChanged)
    Q_PROPERTY(QString userCover READ userCover NOTIFY loginStateChanged)
    Q_PROPERTY(QString userDescription READ userDescription NOTIFY loginStateChanged)
    Q_PROPERTY(int userFollowers READ userFollowers NOTIFY loginStateChanged)
    Q_PROPERTY(int userFollowing READ userFollowing NOTIFY loginStateChanged)
    Q_PROPERTY(int userStatusesCount READ userStatusesCount NOTIFY loginStateChanged)
    Q_PROPERTY(QString userFollowersText READ userFollowersText NOTIFY loginStateChanged)
    Q_PROPERTY(bool userVerified READ userVerified NOTIFY loginStateChanged)

    Q_PROPERTY(QString globalError READ globalError NOTIFY globalErrorChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

    // ---- 微博详情快照 ----
    Q_PROPERTY(QString detailId READ detailId NOTIFY detailChanged)
    Q_PROPERTY(QString detailBid READ detailBid NOTIFY detailChanged)
    Q_PROPERTY(QString detailText READ detailText NOTIFY detailChanged)
    Q_PROPERTY(QString detailTextHtml READ detailTextHtml NOTIFY detailChanged)
    Q_PROPERTY(QString detailCreatedAt READ detailCreatedAt NOTIFY detailChanged)
    Q_PROPERTY(QString detailSource READ detailSource NOTIFY detailChanged)
    Q_PROPERTY(QString detailRegionName READ detailRegionName NOTIFY detailChanged)
    Q_PROPERTY(qint64 detailAuthorId READ detailAuthorId NOTIFY detailChanged)
    Q_PROPERTY(QString detailAuthorName READ detailAuthorName NOTIFY detailChanged)
    Q_PROPERTY(QString detailAuthorAvatar READ detailAuthorAvatar NOTIFY detailChanged)
    Q_PROPERTY(bool detailAuthorVerified READ detailAuthorVerified NOTIFY detailChanged)
    Q_PROPERTY(QVariantList detailPics READ detailPics NOTIFY detailChanged)
    Q_PROPERTY(int detailPicCount READ detailPicCount NOTIFY detailChanged)
    Q_PROPERTY(bool detailHasRetweeted READ detailHasRetweeted NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedId READ detailRetweetedId NOTIFY detailChanged)
    Q_PROPERTY(qint64 detailRetweetedAuthorId READ detailRetweetedAuthorId NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedAuthorName READ detailRetweetedAuthorName NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedText READ detailRetweetedText NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedTextHtml READ detailRetweetedTextHtml NOTIFY detailChanged)
    Q_PROPERTY(QVariantList detailRetweetedPics READ detailRetweetedPics NOTIFY detailChanged)
    Q_PROPERTY(int detailRetweetedPicCount READ detailRetweetedPicCount NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedPageType READ detailRetweetedPageType NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedPageTitle READ detailRetweetedPageTitle NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedPageCover READ detailRetweetedPageCover NOTIFY detailChanged)
    Q_PROPERTY(QString detailRetweetedPageMediaUrl READ detailRetweetedPageMediaUrl NOTIFY detailChanged)
    Q_PROPERTY(int detailRepostsCount READ detailRepostsCount NOTIFY detailStatsChanged)
    Q_PROPERTY(int detailCommentsCount READ detailCommentsCount NOTIFY detailStatsChanged)
    Q_PROPERTY(int detailAttitudesCount READ detailAttitudesCount NOTIFY detailStatsChanged)
    Q_PROPERTY(QString detailRepostsText READ detailRepostsText NOTIFY detailStatsChanged)
    Q_PROPERTY(QString detailCommentsText READ detailCommentsText NOTIFY detailStatsChanged)
    Q_PROPERTY(QString detailAttitudesText READ detailAttitudesText NOTIFY detailStatsChanged)
    Q_PROPERTY(int detailAttitudesStatus READ detailAttitudesStatus NOTIFY detailStatsChanged)
    Q_PROPERTY(bool detailFavorited READ detailFavorited NOTIFY detailStatsChanged)
    Q_PROPERTY(bool detailCanDelete READ detailCanDelete NOTIFY detailChanged)
    Q_PROPERTY(QString detailPageType READ detailPageType NOTIFY detailChanged)
    Q_PROPERTY(QString detailPageTitle READ detailPageTitle NOTIFY detailChanged)
    Q_PROPERTY(QString detailPageCover READ detailPageCover NOTIFY detailChanged)
    Q_PROPERTY(QString detailPageUrl READ detailPageUrl NOTIFY detailChanged)
    Q_PROPERTY(QString detailPageMediaUrl READ detailPageMediaUrl NOTIFY detailChanged)
    Q_PROPERTY(int detailPageDuration READ detailPageDuration NOTIFY detailChanged)
    Q_PROPERTY(QString detailTopicIds READ detailTopicIds NOTIFY detailChanged)
    Q_PROPERTY(bool detailLoaded READ detailLoaded NOTIFY detailChanged)

    // ---- 媒体（视频 / 直播） ----
    Q_PROPERTY(QString mediaType READ mediaType NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaCover READ mediaCover NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaUrl READ mediaUrl NOTIFY mediaChanged)
    Q_PROPERTY(QVariantList mediaQualities READ mediaQualities NOTIFY mediaChanged)
    Q_PROPERTY(bool mediaLoading READ mediaLoading NOTIFY mediaChanged)
    Q_PROPERTY(QString mediaStatus READ mediaStatus NOTIFY mediaChanged)

    // ---- 发布 ----
    Q_PROPERTY(bool publishUploading READ publishUploading NOTIFY publishStateChanged)
    Q_PROPERTY(double publishProgress READ publishProgress NOTIFY publishStateChanged)
    Q_PROPERTY(QString publishStatus READ publishStatus NOTIFY publishStateChanged)
    Q_PROPERTY(QString publishDraft READ publishDraft NOTIFY publishDraftChanged)

    // ---- 超话签到 ----
    Q_PROPERTY(bool checkinRunning READ checkinRunning NOTIFY checkinStateChanged)
    Q_PROPERTY(int checkinSuccess READ checkinSuccess NOTIFY checkinStateChanged)
    Q_PROPERTY(int checkinFailed READ checkinFailed NOTIFY checkinStateChanged)
    Q_PROPERTY(QString checkinSummary READ checkinSummary NOTIFY checkinStateChanged)

public:
    explicit WeiboController(QObject *parent = nullptr);
    ~WeiboController() override;

    // ---- getters ----
    bool loggedIn() const { return m_loggedIn; }
    qint64 userId() const { return m_userId; }
    QString userName() const { return m_userName; }
    QString userAvatar() const { return m_userAvatar; }
    QString userCover() const { return m_userCover; }
    QString userDescription() const { return m_userDescription; }
    int userFollowers() const { return m_userFollowers; }
    int userFollowing() const { return m_userFollowing; }
    int userStatusesCount() const { return m_userStatusesCount; }
    QString userFollowersText() const { return WeiboJsonFormatCount(m_userFollowers); }
    bool userVerified() const { return m_userVerified; }

    QString globalError() const { return m_globalError; }
    bool isLoading() const { return m_isLoading; }

    // 详情
    QString detailId() const { return m_detail.id; }
    QString detailBid() const { return m_detail.bid; }
    QString detailText() const { return m_detail.text; }
    QString detailTextHtml() const { return m_detail.textHtml; }
    QString detailCreatedAt() const { return m_detail.createdAt; }
    QString detailSource() const { return m_detail.source; }
    QString detailRegionName() const { return m_detail.regionName; }
    qint64 detailAuthorId() const { return m_detail.author.uid; }
    QString detailAuthorName() const { return m_detail.author.name; }
    QString detailAuthorAvatar() const { return m_detail.author.avatar; }
    bool detailAuthorVerified() const { return m_detail.author.verified; }
    QVariantList detailPics() const;
    int detailPicCount() const { return m_detail.pics.size(); }
    bool detailHasRetweeted() const { return m_detail.hasRetweeted; }
    QString detailRetweetedId() const { return m_detail.retweetedId; }
    qint64 detailRetweetedAuthorId() const { return m_detail.retweetedAuthorId; }
    QString detailRetweetedAuthorName() const { return m_detail.retweetedAuthorName; }
    QString detailRetweetedText() const { return m_detail.retweetedText; }
    QString detailRetweetedTextHtml() const { return m_detail.retweetedTextHtml; }
    QVariantList detailRetweetedPics() const;
    int detailRetweetedPicCount() const { return m_detail.retweetedPics.size(); }
    QString detailRetweetedPageType() const { return m_detail.retweetedPage.type; }
    QString detailRetweetedPageTitle() const { return m_detail.retweetedPage.title; }
    QString detailRetweetedPageCover() const { return m_detail.retweetedPage.cover; }
    QString detailRetweetedPageMediaUrl() const { return m_detail.retweetedPage.mediaUrl; }
    int detailRepostsCount() const { return static_cast<int>(m_detail.repostsCount); }
    int detailCommentsCount() const { return static_cast<int>(m_detail.commentsCount); }
    int detailAttitudesCount() const { return static_cast<int>(m_detail.attitudesCount); }
    QString detailRepostsText() const { return WeiboJsonFormatCount(m_detail.repostsCount); }
    QString detailCommentsText() const { return WeiboJsonFormatCount(m_detail.commentsCount); }
    QString detailAttitudesText() const { return WeiboJsonFormatCount(m_detail.attitudesCount); }
    int detailAttitudesStatus() const { return m_detail.attitudesStatus; }
    bool detailFavorited() const { return m_detail.favorited; }
    bool detailCanDelete() const { return m_detail.canDelete; }
    QString detailPageType() const { return m_detail.page.type; }
    QString detailPageTitle() const { return m_detail.page.title; }
    QString detailPageCover() const { return m_detail.page.cover; }
    QString detailPageUrl() const { return m_detail.page.url; }
    QString detailPageMediaUrl() const { return m_detail.page.mediaUrl; }
    int detailPageDuration() const { return m_detail.page.duration; }
    QString detailTopicIds() const { return m_detail.topicIds.join(QLatin1Char(',')); }
    bool detailLoaded() const { return !m_detail.id.isEmpty(); }
    const WeiboBlog &detail() const { return m_detail; }

    // 媒体
    QString mediaType() const { return m_media.type; }
    QString mediaTitle() const { return m_media.title; }
    QString mediaCover() const { return m_media.cover; }
    QString mediaUrl() const { return m_media.mediaUrl; }
    QVariantList mediaQualities() const;
    bool mediaLoading() const { return m_mediaLoading; }
    QString mediaStatus() const { return m_mediaStatus; }

    // 发布
    bool publishUploading() const { return m_publishUploading; }
    double publishProgress() const { return m_publishProgress; }
    QString publishStatus() const { return m_publishStatus; }
    QString publishDraft() const { return m_publishDraft; }

    // 超话
    bool checkinRunning() const { return m_checkinRunning; }
    int checkinSuccess() const { return m_checkinSuccess; }
    int checkinFailed() const { return m_checkinFailed; }
    QString checkinSummary() const { return m_checkinSummary; }

    // ---- 子模块 ----
    QObject *feed() const;
    QObject *status() const;
    QObject *comments() const;
    QObject *search() const;
    QObject *profile() const;
    QObject *login() const;
    QObject *publish() const;
    QObject *topic() const;
    QObject *media() const;
    QObject *viewer() const;

    WeiboFeedModule *feedModule() const { return m_feedModule.get(); }
    WeiboStatusModule *statusModule() const { return m_statusModule.get(); }
    WeiboCommentModule *commentModule() const { return m_commentModule.get(); }
    WeiboSearchModule *searchModule() const { return m_searchModule.get(); }
    WeiboProfileModule *profileModule() const { return m_profileModule.get(); }
    WeiboLoginModule *loginModule() const { return m_loginModule.get(); }
    WeiboPublishModule *publishModule() const { return m_publishModule.get(); }
    WeiboTopicModule *topicModule() const { return m_topicModule.get(); }
    WeiboMediaModule *mediaModule() const { return m_mediaModule.get(); }
    WeiboViewerModule *viewerModule() const { return m_viewerModule.get(); }

    WeiboNetwork *network() const { return m_network; }

    // ---- 模型访问器 ----
    // 全部标 Q_INVOKABLE：QML 页面需要 `controller.homeModel()` 这种调用形式直接
    // 拿到列表模型（Q_PROPERTY(QObject*) 只暴露了 10 个子模块，模型本身走方法）。
    // 不加 Q_INVOKABLE 的话 QML 侧调用会报 "Property 'xxxModel' is not a function"。
    Q_INVOKABLE BlogListModel *homeModel() const { return m_homeModel; }
    Q_INVOKABLE BlogListModel *followModel() const { return m_followModel; }
    Q_INVOKABLE BlogListModel *hotStatusModel() const { return m_hotStatusModel; }
    Q_INVOKABLE BlogListModel *groupModel() const { return m_groupModel; }
    Q_INVOKABLE BlogListModel *searchStatusModel() const { return m_searchStatusModel; }
    Q_INVOKABLE BlogListModel *userStatusModel() const { return m_userStatusModel; }
    Q_INVOKABLE BlogListModel *topicStatusModel() const { return m_topicStatusModel; }
    Q_INVOKABLE BlogListModel *myStatusModel() const { return m_myStatusModel; }
    Q_INVOKABLE BlogListModel *favoriteModel() const { return m_favoriteModel; }
    Q_INVOKABLE BlogListModel *mentionModel() const { return m_mentionModel; }
    Q_INVOKABLE BlogListModel *repostModel() const { return m_repostModel; }
    Q_INVOKABLE CommentListModel *commentModel() const { return m_commentModel; }
    Q_INVOKABLE CommentReplyListModel *commentReplyModel() const { return m_commentReplyModel; }
    Q_INVOKABLE HotSearchModel *hotSearchModel() const { return m_hotSearchModel; }
    Q_INVOKABLE UserListModel *userSearchModel() const { return m_userSearchModel; }
    Q_INVOKABLE UserListModel *followingModel() const { return m_followingModel; }
    Q_INVOKABLE UserListModel *followerModel() const { return m_followerModel; }
    Q_INVOKABLE UserListModel *likerModel() const { return m_likerModel; }
    Q_INVOKABLE TopicListModel *topicSearchModel() const { return m_topicSearchModel; }
    Q_INVOKABLE TopicListModel *myTopicModel() const { return m_myTopicModel; }
    Q_INVOKABLE PictureListModel *pictureModel() const { return m_pictureModel; }
    Q_INVOKABLE SearchHistoryModel *searchHistoryModel() const { return m_searchHistoryModel; }

    // ---- 状态写入（供各 Module 调用） ----
    void setIsLoading(bool loading);
    void setGlobalError(const QString &error);
    void clearLocalLoginState();

    void setLoginUser(const WeiboUser &user);
    void setDetail(const WeiboBlog &blog);
    void clearDetail();
    void setDetailStats(qint64 reposts, qint64 comments, qint64 attitudes,
                        int attitudesStatus, bool favorited);
    void applyDetailLike(bool liked, qint64 attitudesCount);
    void applyDetailFavorite(bool favorited);

    void setMediaInfo(const WeiboPageInfo &page);
    void setMediaQualities(const QVector<WeiboMediaQuality> &qualities);
    void setMediaLoading(bool loading);
    void setMediaStatus(const QString &status);
    void clearMedia();

    void setPublishState(bool uploading, double progress, const QString &status);
    void setPublishDraft(const QString &draft);

    void setCheckinState(bool running, int success, int failed,
                         const QString &summary);

    // ---- QML 可调用 ----
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void cancelAll();
    Q_INVOKABLE bool isLoggedIn() const { return m_loggedIn; }
    Q_INVOKABLE void bootstrap();  // 登录态 + 热搜 + 首页首屏

signals:
    void loginStateChanged();
    void globalErrorChanged();
    void isLoadingChanged();
    void detailChanged();
    void detailStatsChanged();
    void mediaChanged();
    void publishStateChanged();
    void publishDraftChanged();
    void checkinStateChanged();

    void toastMessage(const QString &message);
    void loginSucceeded();
    void loginExpired();
    void navigateRequested(const QString &page, const QVariantMap &props);
    void mediaReady(const QString &url);
    void imageRequested(const QVariantList &pics, int index);

private:
    friend class WeiboFeedModule;
    friend class WeiboStatusModule;
    friend class WeiboCommentModule;
    friend class WeiboSearchModule;
    friend class WeiboProfileModule;
    friend class WeiboLoginModule;
    friend class WeiboPublishModule;
    friend class WeiboTopicModule;
    friend class WeiboMediaModule;
    friend class WeiboViewerModule;

    static QString WeiboJsonFormatCount(qint64 n);
    void createModels();

    WeiboNetwork *m_network = nullptr;

    std::unique_ptr<WeiboFeedModule> m_feedModule;
    std::unique_ptr<WeiboStatusModule> m_statusModule;
    std::unique_ptr<WeiboCommentModule> m_commentModule;
    std::unique_ptr<WeiboSearchModule> m_searchModule;
    std::unique_ptr<WeiboProfileModule> m_profileModule;
    std::unique_ptr<WeiboLoginModule> m_loginModule;
    std::unique_ptr<WeiboPublishModule> m_publishModule;
    std::unique_ptr<WeiboTopicModule> m_topicModule;
    std::unique_ptr<WeiboMediaModule> m_mediaModule;
    std::unique_ptr<WeiboViewerModule> m_viewerModule;

    bool m_loggedIn = false;
    qint64 m_userId = 0;
    QString m_userName;
    QString m_userAvatar;
    QString m_userCover;
    QString m_userDescription;
    int m_userFollowers = 0;
    int m_userFollowing = 0;
    int m_userStatusesCount = 0;
    bool m_userVerified = false;

    QString m_globalError;
    bool m_isLoading = false;
    int m_loadingCount = 0;

    WeiboBlog m_detail;
    WeiboPageInfo m_media;
    QVector<WeiboMediaQuality> m_mediaQualities;
    bool m_mediaLoading = false;
    QString m_mediaStatus;

    bool m_publishUploading = false;
    double m_publishProgress = 0.0;
    QString m_publishStatus;
    QString m_publishDraft;

    bool m_checkinRunning = false;
    int m_checkinSuccess = 0;
    int m_checkinFailed = 0;
    QString m_checkinSummary;

    BlogListModel *m_homeModel = nullptr;
    BlogListModel *m_followModel = nullptr;
    BlogListModel *m_hotStatusModel = nullptr;
    BlogListModel *m_groupModel = nullptr;
    BlogListModel *m_searchStatusModel = nullptr;
    BlogListModel *m_userStatusModel = nullptr;
    BlogListModel *m_topicStatusModel = nullptr;
    BlogListModel *m_myStatusModel = nullptr;
    BlogListModel *m_favoriteModel = nullptr;
    BlogListModel *m_mentionModel = nullptr;
    BlogListModel *m_repostModel = nullptr;
    CommentListModel *m_commentModel = nullptr;
    CommentReplyListModel *m_commentReplyModel = nullptr;
    HotSearchModel *m_hotSearchModel = nullptr;
    UserListModel *m_userSearchModel = nullptr;
    UserListModel *m_followingModel = nullptr;
    UserListModel *m_followerModel = nullptr;
    UserListModel *m_likerModel = nullptr;
    TopicListModel *m_topicSearchModel = nullptr;
    TopicListModel *m_myTopicModel = nullptr;
    PictureListModel *m_pictureModel = nullptr;
    SearchHistoryModel *m_searchHistoryModel = nullptr;

    bool m_destroying = false;
};
