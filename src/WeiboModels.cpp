#include "WeiboModels.h"

#include "WeiboJsonUtils.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariant>

// 所有解析都严格按 docs/SPEC.md 第 3 节的归一化 JSON 契约来（Go sidecar 已经
// 把上游字段洗过一遍），这里只做类型容错，不再猜上游字段名。

namespace {

constexpr int kMaxSearchHistory = 30;

// CommentReplyListModel 的 Roles 枚举里没有 CreatedTsRole（见头文件），
// 但 QML 侧统一按 "createdTs" 取值，所以额外分配一个不会撞车的 role id。
constexpr int kReplyCreatedTsRole = Qt::UserRole + 100;

QVariantList picturesToVariantList(const QVector<WeiboPicture> &pics)
{
    QVariantList list;
    list.reserve(pics.size());
    for (int i = 0; i < pics.size(); ++i)
        list.append(PictureListModel::pictureToMap(pics.at(i), i));
    return list;
}

QString firstPictureUrl(const QVector<WeiboPicture> &pics)
{
    return pics.isEmpty() ? QString() : pics.first().url;
}

QVector<WeiboPicture> parsePictureArray(const QJsonArray &array)
{
    QVector<WeiboPicture> pics;
    pics.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const WeiboPicture pic = PictureListModel::parsePicture(value.toObject());
        if (pic.url.isEmpty() && pic.large.isEmpty())
            continue;  // 脏数据：整条丢掉，免得 QML 出现空图槽
        pics.append(pic);
    }
    return pics;
}

WeiboPageInfo parsePageInfo(const QJsonObject &obj)
{
    WeiboPageInfo page;
    const QString type = WeiboJson::str(obj, "type");
    page.type = type.isEmpty() ? QStringLiteral("none") : type;  // 无媒体时 Go 侧给 "none"
    page.title = WeiboJson::str(obj, "title");
    page.cover = WeiboJson::str(obj, "cover");
    page.url = WeiboJson::str(obj, "url");
    page.mediaUrl = WeiboJson::str(obj, "media_url");
    page.duration = static_cast<int>(WeiboJson::num(obj, "duration"));
    page.liveStatus = static_cast<int>(WeiboJson::num(obj, "live_status"));
    return page;
}

bool pageHasMedia(const WeiboPageInfo &page)
{
    return !page.type.isEmpty() && page.type != QLatin1String("none");
}

// "reply_to" 契约上是字符串，个别接口会给 {"id":..,"name":..}，都兼容一下。
QString parseReplyTo(const QJsonObject &obj)
{
    const QJsonValue value = obj.value(QStringLiteral("reply_to"));
    if (value.isString())
        return value.toString();
    if (value.isObject())
        return WeiboJson::str(value.toObject(), "name");
    return QString();
}

// 话题/超话列表里那行状态文字。
QString topicStatusText(const WeiboTopic &topic)
{
    if (!topic.isSuper)
        return QString();  // 普通话题不能签到
    if (!topic.checked)
        return QStringLiteral("未签到");
    if (topic.signedDays > 0)
        return QStringLiteral("已签到 %1 天").arg(topic.signedDays);
    return QStringLiteral("已签到");
}

int countChecked(const QVector<WeiboTopic> &items)
{
    int checked = 0;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i).checked)
            ++checked;
    }
    return checked;
}

}  // namespace

// ===========================================================================
// BlogListModel
// ===========================================================================

BlogListModel::BlogListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int BlogListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())  // 列表模型没有子层级
        return 0;
    return m_items.size();
}

QVariant BlogListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboBlog &blog = m_items.at(index.row());
    switch (role) {
    case IdRole: return blog.id;
    case BidRole: return blog.bid;
    case TextRole: return blog.text;
    case TextHtmlRole: return blog.textHtml;
    case CreatedAtRole: return blog.createdAt;
    case CreatedTsRole: return blog.createdTs;
    case CreatedTextRole: return WeiboJson::formatRelativeTime(blog.createdTs);
    case SourceRole: return blog.source;
    case RegionNameRole: return blog.regionName;
    case IsLongTextRole: return blog.isLongText;

    case AuthorIdRole: return blog.author.uid;
    case AuthorNameRole: return blog.author.name;
    case AuthorAvatarRole: return blog.author.avatar;
    case AuthorVerifiedRole: return blog.author.verified;
    case AuthorVerifiedTypeRole: return blog.author.verifiedType;
    case AuthorVerifiedReasonRole: return blog.author.verifiedReason;

    case PicsRole: return picturesToVariantList(blog.pics);
    case PicCountRole: return blog.pics.size();
    case FirstPicRole: return firstPictureUrl(blog.pics);

    case PageTypeRole: return blog.page.type;
    case PageTitleRole: return blog.page.title;
    case PageCoverRole: return blog.page.cover;
    case PageUrlRole: return blog.page.url;
    case PageMediaUrlRole: return blog.page.mediaUrl;
    case PageDurationRole: return blog.page.duration;
    case PageLiveStatusRole: return blog.page.liveStatus;
    case HasMediaRole: return pageHasMedia(blog.page);

    case HasRetweetedRole: return blog.hasRetweeted;
    case RetweetedIdRole: return blog.retweetedId;
    case RetweetedAuthorIdRole: return blog.retweetedAuthorId;
    case RetweetedAuthorNameRole: return blog.retweetedAuthorName;
    case RetweetedAuthorAvatarRole: return blog.retweetedAuthorAvatar;
    case RetweetedTextRole: return blog.retweetedText;
    case RetweetedTextHtmlRole: return blog.retweetedTextHtml;
    case RetweetedPicsRole: return picturesToVariantList(blog.retweetedPics);
    case RetweetedPicCountRole: return blog.retweetedPics.size();
    case RetweetedFirstPicRole: return firstPictureUrl(blog.retweetedPics);
    case RetweetedPageTypeRole: return blog.retweetedPage.type;
    case RetweetedPageTitleRole: return blog.retweetedPage.title;
    case RetweetedPageCoverRole: return blog.retweetedPage.cover;
    case RetweetedPageUrlRole: return blog.retweetedPage.url;
    case RetweetedPageMediaUrlRole: return blog.retweetedPage.mediaUrl;

    case RepostsCountRole: return blog.repostsCount;
    case CommentsCountRole: return blog.commentsCount;
    case AttitudesCountRole: return blog.attitudesCount;
    case AttitudesStatusRole: return blog.attitudesStatus;
    case FavoritedRole: return blog.favorited;
    case CanDeleteRole: return blog.canDelete;
    case RepostsCountTextRole: return WeiboJson::formatCount(blog.repostsCount);
    case CommentsCountTextRole: return WeiboJson::formatCount(blog.commentsCount);
    case AttitudesCountTextRole: return WeiboJson::formatCount(blog.attitudesCount);
    case TopicIdsRole: return blog.topicIds;

    default: return QVariant();
    }
}

QHash<int, QByteArray> BlogListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[BidRole] = "bid";
    roles[TextRole] = "text";
    roles[TextHtmlRole] = "textHtml";
    roles[CreatedAtRole] = "createdAt";
    roles[CreatedTsRole] = "createdTs";
    roles[CreatedTextRole] = "createdText";
    roles[SourceRole] = "source";
    roles[RegionNameRole] = "regionName";
    roles[IsLongTextRole] = "isLongText";
    roles[AuthorIdRole] = "authorId";
    roles[AuthorNameRole] = "authorName";
    roles[AuthorAvatarRole] = "authorAvatar";
    roles[AuthorVerifiedRole] = "authorVerified";
    roles[AuthorVerifiedTypeRole] = "authorVerifiedType";
    roles[AuthorVerifiedReasonRole] = "authorVerifiedReason";
    roles[PicsRole] = "pics";
    roles[PicCountRole] = "picCount";
    roles[FirstPicRole] = "firstPic";
    roles[PageTypeRole] = "pageType";
    roles[PageTitleRole] = "pageTitle";
    roles[PageCoverRole] = "pageCover";
    roles[PageUrlRole] = "pageUrl";
    roles[PageMediaUrlRole] = "pageMediaUrl";
    roles[PageDurationRole] = "pageDuration";
    roles[PageLiveStatusRole] = "pageLiveStatus";
    roles[HasMediaRole] = "hasMedia";
    roles[HasRetweetedRole] = "hasRetweeted";
    roles[RetweetedIdRole] = "retweetedId";
    roles[RetweetedAuthorIdRole] = "retweetedAuthorId";
    roles[RetweetedAuthorNameRole] = "retweetedAuthorName";
    roles[RetweetedAuthorAvatarRole] = "retweetedAuthorAvatar";
    roles[RetweetedTextRole] = "retweetedText";
    roles[RetweetedTextHtmlRole] = "retweetedTextHtml";
    roles[RetweetedPicsRole] = "retweetedPics";
    roles[RetweetedPicCountRole] = "retweetedPicCount";
    roles[RetweetedFirstPicRole] = "retweetedFirstPic";
    roles[RetweetedPageTypeRole] = "retweetedPageType";
    roles[RetweetedPageTitleRole] = "retweetedPageTitle";
    roles[RetweetedPageCoverRole] = "retweetedPageCover";
    roles[RetweetedPageUrlRole] = "retweetedPageUrl";
    roles[RetweetedPageMediaUrlRole] = "retweetedPageMediaUrl";
    roles[RepostsCountRole] = "repostsCount";
    roles[CommentsCountRole] = "commentsCount";
    roles[AttitudesCountRole] = "attitudesCount";
    roles[AttitudesStatusRole] = "attitudesStatus";
    roles[FavoritedRole] = "favorited";
    roles[CanDeleteRole] = "canDelete";
    roles[RepostsCountTextRole] = "repostsCountText";
    roles[CommentsCountTextRole] = "commentsCountText";
    roles[AttitudesCountTextRole] = "attitudesCountText";
    roles[TopicIdsRole] = "topicIds";
    return roles;
}

void BlogListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

int BlogListModel::indexOfId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

QString BlogListModel::idAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return QString();
    return m_items.at(row).id;
}

QVariantMap BlogListModel::at(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= m_items.size())
        return map;
    // 直接用 roleNames() + data() 组装，保证与 QML 侧字段名永远一致
    const QHash<int, QByteArray> roles = roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it)
        map.insert(QString::fromUtf8(it.value()), data(index(row), it.key()));
    return map;
}

void BlogListModel::removeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.remove(row);
    endRemoveRows();
    emit countChanged();
}

void BlogListModel::removeById(const QString &id)
{
    const int row = indexOfId(id);
    if (row >= 0)
        removeAt(row);
}

void BlogListModel::appendItems(const QVector<WeiboBlog> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
}

void BlogListModel::prependItems(const QVector<WeiboBlog> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), 0, items.size() - 1);
    QVector<WeiboBlog> merged = items;
    merged += m_items;
    m_items = merged;
    endInsertRows();
    emit countChanged();
}

void BlogListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void BlogListModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore)
        return;
    m_hasMore = hasMore;
    emit hasMoreChanged();
}

void BlogListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorMessageChanged();
}

bool BlogListModel::applyLikeState(const QString &id, bool liked, qint64 attitudesCount)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;

    WeiboBlog &blog = m_items[row];
    QVector<int> roles;

    const int status = liked ? 1 : 0;
    if (blog.attitudesStatus != status) {
        blog.attitudesStatus = status;
        roles.append(AttitudesStatusRole);
    }
    // attitudesCount < 0 表示调用方也不知道新的数量，保持原值不显示错误数字
    if (attitudesCount >= 0 && blog.attitudesCount != attitudesCount) {
        blog.attitudesCount = attitudesCount;
        roles.append(AttitudesCountRole);
        roles.append(AttitudesCountTextRole);
    }

    if (!roles.isEmpty()) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, roles);
    }
    return true;
}

bool BlogListModel::applyFavoriteState(const QString &id, bool favorited)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;

    WeiboBlog &blog = m_items[row];
    if (blog.favorited != favorited) {
        blog.favorited = favorited;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, QVector<int>() << FavoritedRole);
    }
    return true;
}

bool BlogListModel::applyRepostCommentDelta(const QString &id, int repostDelta, int commentDelta)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;

    WeiboBlog &blog = m_items[row];
    QVector<int> roles;

    if (repostDelta != 0) {
        const qint64 next = qMax<qint64>(0, blog.repostsCount + repostDelta);
        if (next != blog.repostsCount) {
            blog.repostsCount = next;
            roles.append(RepostsCountRole);
            roles.append(RepostsCountTextRole);
        }
    }
    if (commentDelta != 0) {
        const qint64 next = qMax<qint64>(0, blog.commentsCount + commentDelta);
        if (next != blog.commentsCount) {
            blog.commentsCount = next;
            roles.append(CommentsCountRole);
            roles.append(CommentsCountTextRole);
        }
    }

    if (!roles.isEmpty()) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, roles);
    }
    return true;
}

WeiboBlog BlogListModel::parseBlog(const QJsonObject &obj)
{
    WeiboBlog blog;
    if (obj.isEmpty())
        return blog;

    blog.id = WeiboJson::str(obj, "id");
    blog.bid = WeiboJson::str(obj, "bid");
    blog.text = WeiboJson::str(obj, "text");
    blog.textHtml = WeiboJson::str(obj, "text_html");
    if (blog.text.isEmpty() && !blog.textHtml.isEmpty())
        blog.text = WeiboJson::htmlToPlainText(blog.textHtml);  // 只给了 HTML 时兜底

    blog.createdAt = WeiboJson::str(obj, "created_at");
    blog.createdTs = WeiboJson::num(obj, "created_ts");
    if (blog.createdTs <= 0 && !blog.createdAt.isEmpty())
        blog.createdTs = WeiboJson::parseWeiboTime(blog.createdAt);

    // source / region 有时带 <a> 标签
    blog.source = WeiboJson::stripHtml(WeiboJson::str(obj, "source"));
    blog.regionName = WeiboJson::str(obj, "region_name");
    blog.isLongText = WeiboJson::boolean(obj, "is_long_text");

    blog.author = UserListModel::parseUser(WeiboJson::obj(obj, "author"));
    blog.pics = parsePictureArray(WeiboJson::arr(obj, "pics"));
    blog.page = parsePageInfo(WeiboJson::obj(obj, "page_info"));

    // 转发原微博：只保留一层，避免无限嵌套
    const QJsonObject retweeted = WeiboJson::obj(obj, "retweeted");
    if (!retweeted.isEmpty()) {
        blog.hasRetweeted = true;
        blog.retweetedId = WeiboJson::str(retweeted, "id");
        const QJsonObject retweetedAuthor = WeiboJson::obj(retweeted, "author");
        blog.retweetedAuthorId = WeiboJson::num(retweetedAuthor, "id");
        blog.retweetedAuthorName = WeiboJson::str(retweetedAuthor, "name");
        blog.retweetedAuthorAvatar = WeiboJson::str(retweetedAuthor, "avatar");
        blog.retweetedText = WeiboJson::str(retweeted, "text");
        blog.retweetedTextHtml = WeiboJson::str(retweeted, "text_html");
        if (blog.retweetedText.isEmpty() && !blog.retweetedTextHtml.isEmpty())
            blog.retweetedText = WeiboJson::htmlToPlainText(blog.retweetedTextHtml);
        blog.retweetedPics = parsePictureArray(WeiboJson::arr(retweeted, "pics"));
        blog.retweetedPage = parsePageInfo(WeiboJson::obj(retweeted, "page_info"));
    }

    // 计数偶尔是 "1.2万" 这类字符串
    blog.repostsCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("reposts_count")));
    blog.commentsCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("comments_count")));
    blog.attitudesCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("attitudes_count")));
    blog.attitudesStatus = WeiboJson::parseAttitudeStatus(obj);
    blog.favorited = WeiboJson::boolean(obj, "favorited");
    blog.canDelete = WeiboJson::boolean(obj, "can_delete");

    const QJsonArray topicIds = WeiboJson::arr(obj, "topic_ids");
    for (const QJsonValue &value : topicIds) {
        const QString topicId = WeiboJson::str(value).trimmed();
        if (!topicId.isEmpty())
            blog.topicIds.append(topicId);
    }

    return blog;
}

// ===========================================================================
// CommentListModel
// ===========================================================================

CommentListModel::CommentListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CommentListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant CommentListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboComment &comment = m_items.at(index.row());
    switch (role) {
    case IdRole: return comment.id;
    case TextRole: return comment.text;
    case TextHtmlRole: return comment.textHtml;
    case CreatedAtRole: return comment.createdAt;
    case CreatedTsRole: return comment.createdTs;
    case CreatedTextRole: return WeiboJson::formatRelativeTime(comment.createdTs);
    case LikeCountRole: return comment.likeCount;
    case LikedRole: return comment.liked;
    case ReplyCountRole: return comment.replyCount;
    case UserIdRole: return comment.user.uid;
    case UserNameRole: return comment.user.name;
    case UserAvatarRole: return comment.user.avatar;
    case UserVerifiedRole: return comment.user.verified;
    case ReplyToRole: return comment.replyTo;
    case PicsRole: return picturesToVariantList(comment.pics);
    case PicCountRole: return comment.pics.size();
    case FirstPicRole: return firstPictureUrl(comment.pics);
    case CanDeleteRole: return comment.canDelete;
    default: return QVariant();
    }
}

QHash<int, QByteArray> CommentListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TextRole] = "text";
    roles[TextHtmlRole] = "textHtml";
    roles[CreatedAtRole] = "createdAt";
    roles[CreatedTsRole] = "createdTs";
    roles[CreatedTextRole] = "createdText";
    roles[LikeCountRole] = "likeCount";
    roles[LikedRole] = "liked";
    roles[ReplyCountRole] = "replyCount";
    roles[UserIdRole] = "userId";
    roles[UserNameRole] = "userName";
    roles[UserAvatarRole] = "userAvatar";
    roles[UserVerifiedRole] = "userVerified";
    roles[ReplyToRole] = "replyTo";
    roles[PicsRole] = "pics";
    roles[PicCountRole] = "picCount";
    roles[FirstPicRole] = "firstPic";
    roles[CanDeleteRole] = "canDelete";
    return roles;
}

void CommentListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

int CommentListModel::indexOfId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id)
            return i;
    }
    return -1;
}

void CommentListModel::removeById(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.remove(row);
    endRemoveRows();
    emit countChanged();
}

void CommentListModel::appendItems(const QVector<WeiboComment> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
}

void CommentListModel::prependItems(const QVector<WeiboComment> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), 0, items.size() - 1);
    QVector<WeiboComment> merged = items;
    merged += m_items;
    m_items = merged;
    endInsertRows();
    emit countChanged();
}

void CommentListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void CommentListModel::setTotalCount(int total)
{
    if (m_totalCount == total)
        return;
    m_totalCount = total;
    emit totalCountChanged();
}

void CommentListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorMessageChanged();
}

bool CommentListModel::setLikeState(const QString &id, bool liked, qint64 likeCount)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;

    WeiboComment &comment = m_items[row];
    QVector<int> roles;
    if (comment.liked != liked) {
        comment.liked = liked;
        roles.append(LikedRole);
    }
    if (likeCount >= 0 && comment.likeCount != likeCount) {
        comment.likeCount = likeCount;
        roles.append(LikeCountRole);
    }
    if (!roles.isEmpty()) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, roles);
    }
    return true;
}

WeiboComment CommentListModel::parseComment(const QJsonObject &obj)
{
    WeiboComment comment;
    if (obj.isEmpty())
        return comment;

    comment.id = WeiboJson::str(obj, "id");
    comment.text = WeiboJson::str(obj, "text");
    comment.textHtml = WeiboJson::str(obj, "text_html");
    if (comment.text.isEmpty() && !comment.textHtml.isEmpty())
        comment.text = WeiboJson::htmlToPlainText(comment.textHtml);

    comment.createdAt = WeiboJson::str(obj, "created_at");
    comment.createdTs = WeiboJson::num(obj, "created_ts");
    if (comment.createdTs <= 0 && !comment.createdAt.isEmpty())
        comment.createdTs = WeiboJson::parseWeiboTime(comment.createdAt);

    comment.likeCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("like_count")));
    comment.liked = WeiboJson::boolean(obj, "liked");
    comment.replyCount = WeiboJson::num(obj, "reply_count");
    comment.user = UserListModel::parseUser(WeiboJson::obj(obj, "user"));
    comment.replyTo = parseReplyTo(obj);
    comment.pics = parsePictureArray(WeiboJson::arr(obj, "pics"));
    comment.canDelete = WeiboJson::boolean(obj, "can_delete");
    return comment;
}

// ===========================================================================
// CommentReplyListModel（子评论）
// ===========================================================================

CommentReplyListModel::CommentReplyListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int CommentReplyListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant CommentReplyListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboComment &comment = m_items.at(index.row());
    switch (role) {
    case IdRole: return comment.id;
    case TextRole: return comment.text;
    case TextHtmlRole: return comment.textHtml;
    case CreatedAtRole: return comment.createdAt;
    case kReplyCreatedTsRole: return comment.createdTs;
    case CreatedTextRole: return WeiboJson::formatRelativeTime(comment.createdTs);
    case LikeCountRole: return comment.likeCount;
    case LikedRole: return comment.liked;
    case ReplyCountRole: return comment.replyCount;
    case UserIdRole: return comment.user.uid;
    case UserNameRole: return comment.user.name;
    case UserAvatarRole: return comment.user.avatar;
    case UserVerifiedRole: return comment.user.verified;
    case ReplyToRole: return comment.replyTo;
    case PicsRole: return picturesToVariantList(comment.pics);
    case PicCountRole: return comment.pics.size();
    case FirstPicRole: return firstPictureUrl(comment.pics);
    case CanDeleteRole: return comment.canDelete;
    default: return QVariant();
    }
}

QHash<int, QByteArray> CommentReplyListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TextRole] = "text";
    roles[TextHtmlRole] = "textHtml";
    roles[CreatedAtRole] = "createdAt";
    roles[kReplyCreatedTsRole] = "createdTs";
    roles[CreatedTextRole] = "createdText";
    roles[LikeCountRole] = "likeCount";
    roles[LikedRole] = "liked";
    roles[ReplyCountRole] = "replyCount";
    roles[UserIdRole] = "userId";
    roles[UserNameRole] = "userName";
    roles[UserAvatarRole] = "userAvatar";
    roles[UserVerifiedRole] = "userVerified";
    roles[ReplyToRole] = "replyTo";
    roles[PicsRole] = "pics";
    roles[PicCountRole] = "picCount";
    roles[FirstPicRole] = "firstPic";
    roles[CanDeleteRole] = "canDelete";
    return roles;
}

void CommentReplyListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

void CommentReplyListModel::removeById(const QString &id)
{
    if (id.isEmpty())
        return;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id != id)
            continue;
        beginRemoveRows(QModelIndex(), i, i);
        m_items.remove(i);
        endRemoveRows();
        emit countChanged();
        return;
    }
}

void CommentReplyListModel::setItems(const QVector<WeiboComment> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void CommentReplyListModel::appendItems(const QVector<WeiboComment> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
}

void CommentReplyListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void CommentReplyListModel::setTotalCount(int total)
{
    if (m_totalCount == total)
        return;
    m_totalCount = total;
    emit totalCountChanged();
}

void CommentReplyListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorMessageChanged();
}

bool CommentReplyListModel::setLikeState(const QString &id, bool liked, qint64 likeCount)
{
    int row = -1;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).id == id) {
            row = i;
            break;
        }
    }
    if (row < 0)
        return false;

    WeiboComment &comment = m_items[row];
    QVector<int> roles;
    if (comment.liked != liked) {
        comment.liked = liked;
        roles.append(LikedRole);
    }
    if (likeCount >= 0 && comment.likeCount != likeCount) {
        comment.likeCount = likeCount;
        roles.append(LikeCountRole);
    }
    if (!roles.isEmpty()) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, roles);
    }
    return true;
}

// ===========================================================================
// HotSearchModel
// ===========================================================================

HotSearchModel::HotSearchModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int HotSearchModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant HotSearchModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboHotItem &item = m_items.at(index.row());
    switch (role) {
    case RankRole: return item.rank;
    case WordRole: return item.word;
    case RawHotRole: return item.rawHot;
    case HotTextRole:
        // raw_hot 为 0 时不如不显示（热搜榜里部分条目没有热度值）
        return item.rawHot > 0 ? WeiboJson::formatCount(item.rawHot) : QString();
    case LabelRole: return item.label;
    case UrlRole: return item.url;
    case CategoryRole: return item.category;
    default: return QVariant();
    }
}

QHash<int, QByteArray> HotSearchModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[RankRole] = "rank";
    roles[WordRole] = "word";
    roles[RawHotRole] = "rawHot";
    roles[HotTextRole] = "hotText";
    roles[LabelRole] = "label";
    roles[UrlRole] = "url";
    roles[CategoryRole] = "category";
    return roles;
}

void HotSearchModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

QString HotSearchModel::wordAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return QString();
    return m_items.at(row).word;
}

void HotSearchModel::setItems(const QVector<WeiboHotItem> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void HotSearchModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

WeiboHotItem HotSearchModel::parseHotItem(const QJsonObject &obj)
{
    WeiboHotItem item;
    item.rank = static_cast<int>(WeiboJson::num(obj, "rank"));
    item.word = WeiboJson::stripHtml(WeiboJson::str(obj, "word")).trimmed();
    item.rawHot = WeiboJson::parseLooseCount(obj.value(QStringLiteral("raw_hot")));
    item.label = WeiboJson::str(obj, "label");
    item.url = WeiboJson::str(obj, "url");
    item.category = WeiboJson::str(obj, "category");
    return item;
}

// ===========================================================================
// UserListModel
// ===========================================================================

UserListModel::UserListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int UserListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant UserListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboUser &user = m_items.at(index.row());
    switch (role) {
    case UidRole: return user.uid;
    case NameRole: return user.name;
    case AvatarRole: return user.avatar;
    case CoverRole: return user.cover;
    case VerifiedRole: return user.verified;
    case VerifiedTypeRole: return user.verifiedType;
    case VerifiedReasonRole: return user.verifiedReason;
    case DescriptionRole: return user.description;
    case FollowersCountRole: return user.followers;
    case FollowingCountRole: return user.following;
    case StatusesCountRole: return user.statusesCount;
    case FollowersTextRole: return WeiboJson::formatCount(user.followers);
    case GenderRole: return user.gender;
    case LocationRole: return user.location;
    case FollowingMeRole: return user.followingMe;
    case IsFollowingRole: return user.isFollowing;
    case IsMeRole: return user.isMe;
    default: return QVariant();
    }
}

QHash<int, QByteArray> UserListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UidRole] = "uid";
    roles[NameRole] = "name";
    roles[AvatarRole] = "avatar";
    roles[CoverRole] = "cover";
    roles[VerifiedRole] = "verified";
    roles[VerifiedTypeRole] = "verifiedType";
    roles[VerifiedReasonRole] = "verifiedReason";
    roles[DescriptionRole] = "description";
    roles[FollowersCountRole] = "followersCount";
    roles[FollowingCountRole] = "followingCount";
    roles[StatusesCountRole] = "statusesCount";
    roles[FollowersTextRole] = "followersText";
    roles[GenderRole] = "gender";
    roles[LocationRole] = "location";
    roles[FollowingMeRole] = "followingMe";
    roles[IsFollowingRole] = "isFollowing";
    roles[IsMeRole] = "isMe";
    return roles;
}

void UserListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

int UserListModel::indexOfUid(qint64 uid) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items.at(i).uid == uid)
            return i;
    }
    return -1;
}

void UserListModel::removeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_items.remove(row);
    endRemoveRows();
    emit countChanged();
}

void UserListModel::appendItems(const QVector<WeiboUser> &items)
{
    if (items.isEmpty())
        return;
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
}

void UserListModel::setItems(const QVector<WeiboUser> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

void UserListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void UserListModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore)
        return;
    m_hasMore = hasMore;
    emit hasMoreChanged();
}

void UserListModel::setTotalCount(int total)
{
    if (m_totalCount == total)
        return;
    m_totalCount = total;
    emit totalCountChanged();
}

void UserListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorMessageChanged();
}

bool UserListModel::setFollowState(qint64 uid, bool following)
{
    const int row = indexOfUid(uid);
    if (row < 0)
        return false;

    WeiboUser &user = m_items[row];
    if (user.isFollowing != following) {
        user.isFollowing = following;
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, QVector<int>() << IsFollowingRole);
    }
    return true;
}

WeiboUser UserListModel::parseUser(const QJsonObject &obj)
{
    WeiboUser user;
    if (obj.isEmpty())
        return user;

    user.uid = WeiboJson::num(obj, "id");
    user.name = WeiboJson::str(obj, "name");
    user.avatar = WeiboJson::str(obj, "avatar");
    user.cover = WeiboJson::str(obj, "cover");
    user.verified = WeiboJson::boolean(obj, "verified");
    user.verifiedType = static_cast<int>(WeiboJson::num(obj, "verified_type", -1));
    user.verifiedReason = WeiboJson::str(obj, "verified_reason");
    user.description = WeiboJson::str(obj, "description");
    user.followers = WeiboJson::parseLooseCount(obj.value(QStringLiteral("followers")));
    user.following = WeiboJson::parseLooseCount(obj.value(QStringLiteral("following")));
    user.statusesCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("statuses_count")));
    user.gender = WeiboJson::str(obj, "gender");
    user.location = WeiboJson::str(obj, "location");
    user.followingMe = WeiboJson::boolean(obj, "following_me");
    user.isFollowing = WeiboJson::boolean(obj, "following");
    user.isMe = WeiboJson::boolean(obj, "is_me");
    return user;
}

// ===========================================================================
// TopicListModel
// ===========================================================================

TopicListModel::TopicListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TopicListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant TopicListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboTopic &topic = m_items.at(index.row());
    switch (role) {
    case IdRole: return topic.id;
    case ContainerIdRole: return topic.containerId;
    case NameRole: return topic.name;
    case DescRole: return topic.desc;
    case CoverRole: return topic.cover;
    case ReadCountRole: return topic.readCount;
    case DiscussCountRole: return topic.discussCount;
    case FansCountRole: return topic.fansCount;
    case ReadTextRole: return WeiboJson::formatCount(topic.readCount);
    case DiscussTextRole: return WeiboJson::formatCount(topic.discussCount);
    case IsSuperRole: return topic.isSuper;
    case LevelRole: return topic.level;
    case ExpRole: return topic.exp;
    case RankRole: return topic.rank;
    case CheckedRole: return topic.checked;
    case SignedDaysRole: return topic.signedDays;
    case TypeRole: return topic.type;
    case StatusTextRole: return topicStatusText(topic);
    default: return QVariant();
    }
}

QHash<int, QByteArray> TopicListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[ContainerIdRole] = "containerId";
    roles[NameRole] = "name";
    roles[DescRole] = "desc";
    roles[CoverRole] = "cover";
    roles[ReadCountRole] = "readCount";
    roles[DiscussCountRole] = "discussCount";
    roles[FansCountRole] = "fansCount";
    roles[ReadTextRole] = "readText";
    roles[DiscussTextRole] = "discussText";
    roles[IsSuperRole] = "isSuper";
    roles[LevelRole] = "level";
    roles[ExpRole] = "exp";
    roles[RankRole] = "rank";
    roles[CheckedRole] = "checked";
    roles[SignedDaysRole] = "signedDays";
    roles[TypeRole] = "type";
    roles[StatusTextRole] = "statusText";
    return roles;
}

int TopicListModel::checkedCount() const
{
    return countChecked(m_items);
}

void TopicListModel::clear()
{
    if (m_items.isEmpty())
        return;
    const int before = checkedCount();
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
    if (checkedCount() != before)
        emit checkedCountChanged();
}

int TopicListModel::indexOfId(const QString &id) const
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < m_items.size(); ++i) {
        // 普通话题的 id/container_id 可能只给了一个，两边都比一次
        if (m_items.at(i).id == id || m_items.at(i).containerId == id)
            return i;
    }
    return -1;
}

void TopicListModel::appendItems(const QVector<WeiboTopic> &items)
{
    if (items.isEmpty())
        return;
    const int before = checkedCount();
    beginInsertRows(QModelIndex(), m_items.size(), m_items.size() + items.size() - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
    if (checkedCount() != before)
        emit checkedCountChanged();
}

void TopicListModel::setItems(const QVector<WeiboTopic> &items)
{
    const int before = checkedCount();
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
    if (checkedCount() != before)
        emit checkedCountChanged();
}

void TopicListModel::setLoading(bool loading)
{
    if (m_loading == loading)
        return;
    m_loading = loading;
    emit loadingChanged();
}

void TopicListModel::setHasMore(bool hasMore)
{
    if (m_hasMore == hasMore)
        return;
    m_hasMore = hasMore;
    emit hasMoreChanged();
}

void TopicListModel::setErrorMessage(const QString &msg)
{
    if (m_errorMessage == msg)
        return;
    m_errorMessage = msg;
    emit errorMessageChanged();
}

bool TopicListModel::setChecked(const QString &id, bool checked, int signedDays, int exp)
{
    const int row = indexOfId(id);
    if (row < 0)
        return false;

    WeiboTopic &topic = m_items[row];
    const int before = checkedCount();
    const QString beforeStatus = topicStatusText(topic);

    QVector<int> roles;
    if (topic.checked != checked) {
        topic.checked = checked;
        roles.append(CheckedRole);
    }
    // 负数表示调用方没有这两个值，保持原样
    if (signedDays >= 0 && topic.signedDays != signedDays) {
        topic.signedDays = signedDays;
        roles.append(SignedDaysRole);
    }
    if (exp >= 0 && topic.exp != exp) {
        topic.exp = exp;
        roles.append(ExpRole);
    }
    if (topicStatusText(topic) != beforeStatus)
        roles.append(StatusTextRole);

    if (!roles.isEmpty()) {
        const QModelIndex idx = index(row);
        emit dataChanged(idx, idx, roles);
    }
    if (checkedCount() != before)
        emit checkedCountChanged();
    return true;
}

void TopicListModel::markAllCheckable(bool checked)
{
    const int before = checkedCount();
    bool touched = false;
    for (int i = 0; i < m_items.size(); ++i) {
        // 只有超话能签到；普通话题不参与批量勾选
        if (!m_items.at(i).isSuper || m_items.at(i).checked == checked)
            continue;
        m_items[i].checked = checked;
        touched = true;
    }
    if (!touched)
        return;

    // 一次性整表刷新（签到时 QML 要立刻看到所有勾选状态变化）
    emit dataChanged(index(0), index(m_items.size() - 1),
                     QVector<int>() << CheckedRole << StatusTextRole);
    if (checkedCount() != before)
        emit checkedCountChanged();
}

WeiboTopic TopicListModel::parseTopic(const QJsonObject &obj)
{
    WeiboTopic topic;
    if (obj.isEmpty())
        return topic;

    topic.id = WeiboJson::str(obj, "id");
    topic.containerId = WeiboJson::str(obj, "container_id");
    if (topic.containerId.isEmpty())
        topic.containerId = topic.id;  // 两者上游可能只给一个，补齐便于签到/拉流
    if (topic.id.isEmpty())
        topic.id = topic.containerId;

    topic.name = WeiboJson::str(obj, "name");
    topic.desc = WeiboJson::str(obj, "desc");
    topic.cover = WeiboJson::str(obj, "cover");
    topic.readCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("read_count")));
    topic.discussCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("discuss_count")));
    topic.fansCount = WeiboJson::parseLooseCount(obj.value(QStringLiteral("fans_count")));
    topic.isSuper = WeiboJson::boolean(obj, "is_super");
    topic.level = static_cast<int>(WeiboJson::num(obj, "level"));
    topic.exp = static_cast<int>(WeiboJson::num(obj, "exp"));
    topic.rank = static_cast<int>(WeiboJson::num(obj, "rank"));
    topic.checked = WeiboJson::boolean(obj, "checked");
    topic.signedDays = static_cast<int>(WeiboJson::num(obj, "signed_days"));
    topic.type = WeiboJson::str(obj, "type");
    if (topic.type.isEmpty())
        topic.type = topic.isSuper ? QStringLiteral("super") : QStringLiteral("topic");
    return topic;
}

// ===========================================================================
// PictureListModel
// ===========================================================================

PictureListModel::PictureListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PictureListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_items.size();
}

QVariant PictureListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const WeiboPicture &pic = m_items.at(index.row());
    switch (role) {
    case UrlRole: return pic.url;
    case LargeRole: return pic.large.isEmpty() ? pic.url : pic.large;
    case WidthRole: return pic.width;
    case HeightRole: return pic.height;
    case IndexRole: return index.row();
    default: return QVariant();
    }
}

QHash<int, QByteArray> PictureListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[UrlRole] = "url";
    roles[LargeRole] = "large";
    roles[WidthRole] = "width";
    roles[HeightRole] = "height";
    roles[IndexRole] = "index";
    return roles;
}

void PictureListModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

QVariantList PictureListModel::toVariantList() const
{
    QVariantList list;
    list.reserve(m_items.size());
    for (int i = 0; i < m_items.size(); ++i)
        list.append(pictureToMap(m_items.at(i), i));
    return list;
}

void PictureListModel::setItems(const QVector<WeiboPicture> &items)
{
    beginResetModel();
    m_items = items;
    endResetModel();
    emit countChanged();
}

WeiboPicture PictureListModel::parsePicture(const QJsonObject &obj)
{
    WeiboPicture pic;
    if (obj.isEmpty())
        return pic;

    pic.url = WeiboJson::str(obj, "url");
    pic.large = WeiboJson::str(obj, "large");
    // Go 侧偶尔只给一个，互相兜底：展示用不到原图，查看器需要 large
    if (pic.url.isEmpty())
        pic.url = pic.large;
    if (pic.large.isEmpty())
        pic.large = pic.url;
    pic.width = static_cast<int>(WeiboJson::num(obj, "width"));
    pic.height = static_cast<int>(WeiboJson::num(obj, "height"));
    return pic;
}

QVariantMap PictureListModel::pictureToMap(const WeiboPicture &pic, int index)
{
    QVariantMap map;
    map.insert(QStringLiteral("url"), pic.url);
    map.insert(QStringLiteral("large"), pic.large.isEmpty() ? pic.url : pic.large);
    map.insert(QStringLiteral("width"), pic.width);
    map.insert(QStringLiteral("height"), pic.height);
    if (index >= 0)  // index < 0 表示不要 index 字段
        map.insert(QStringLiteral("index"), index);
    return map;
}

// ===========================================================================
// SearchHistoryModel
// ===========================================================================

SearchHistoryModel::SearchHistoryModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // 这里**不**自动 load()：由 WeiboController::createModels() 显式调用，
    // 避免在构造期做磁盘 IO（可能被构造在非 GUI 线程）。
}

int SearchHistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_words.size();
}

QVariant SearchHistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_words.size())
        return QVariant();
    if (role == WordRole)
        return m_words.at(index.row());
    return QVariant();
}

QHash<int, QByteArray> SearchHistoryModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[WordRole] = "word";
    return roles;
}

void SearchHistoryModel::add(const QString &word)
{
    const QString trimmed = word.trimmed();
    if (trimmed.isEmpty())
        return;

    QStringList words = m_words;
    words.removeAll(trimmed);     // 去重
    words.prepend(trimmed);       // 新词放最前
    while (words.size() > kMaxSearchHistory)
        words.removeLast();       // 最多 30 条

    beginResetModel();
    m_words = words;
    endResetModel();
    emit countChanged();
    save();
}

void SearchHistoryModel::remove(const QString &word)
{
    const int row = m_words.indexOf(word);
    if (row < 0)
        return;

    beginRemoveRows(QModelIndex(), row, row);
    m_words.removeAt(row);
    endRemoveRows();
    emit countChanged();
    save();
}

void SearchHistoryModel::clear()
{
    if (m_words.isEmpty())
        return;

    beginResetModel();
    m_words.clear();
    endResetModel();
    emit countChanged();
    save();
}

QString SearchHistoryModel::wordAt(int row) const
{
    if (row < 0 || row >= m_words.size())
        return QString();
    return m_words.at(row);
}

void SearchHistoryModel::setWords(const QStringList &words)
{
    QStringList filtered;
    for (const QString &word : words) {
        const QString trimmed = word.trimmed();
        if (trimmed.isEmpty() || filtered.contains(trimmed))
            continue;
        filtered.append(trimmed);
        if (filtered.size() >= kMaxSearchHistory)
            break;
    }
    if (filtered == m_words)
        return;

    beginResetModel();
    m_words = filtered;
    endResetModel();
    emit countChanged();
}

void SearchHistoryModel::load()
{
    const QString path = storagePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;

    const QJsonArray array = doc.object().value(QStringLiteral("words")).toArray();
    QStringList words;
    for (const QJsonValue &value : array) {
        const QString word = value.toString().trimmed();
        if (!word.isEmpty() && !words.contains(word))
            words.append(word);
    }
    setWords(words);
}

void SearchHistoryModel::save() const
{
    const QString path = storagePath();
    if (path.isEmpty())
        return;

    QJsonArray array;
    for (const QString &word : m_words)
        array.append(word);

    QJsonObject root;
    root.insert(QStringLiteral("words"), array);

    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);  // 原子写，掉电不会把历史写成半截
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[SearchHistoryModel] 无法写入搜索历史:" << path;
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (!file.commit())
        qWarning() << "[SearchHistoryModel] 提交搜索历史失败:" << path;
}

QString SearchHistoryModel::storagePath()
{
    // 1) 环境变量覆盖（打包/调试时用）
    const QByteArray env = qgetenv("WEIBO_PLUGIN_DIR");
    if (!env.isEmpty()) {
        const QString dir = QString::fromLocal8Bit(env).trimmed();
        if (!dir.isEmpty()) {
            if (QDir(dir).exists() || QDir().mkpath(dir))
                return dir + QStringLiteral("/search_history.json");
        }
    }

    // 2) 设备上的插件目录。非 Linux（桌面调试）时只在目录已存在时使用，
    //    否则 mkpath("/userdisk/...") 会在 Windows 上凭空造出 C:\userdisk。
    const QString deviceDir = QStringLiteral("/userdisk/PenMods/plugins/weibo_plugin");
#ifdef Q_OS_LINUX
    const bool deviceOk = QDir().mkpath(deviceDir);
#else
    const bool deviceOk = QDir(deviceDir).exists();
#endif
    if (deviceOk)
        return deviceDir + QStringLiteral("/search_history.json");

    // 3) 最后兜底：标准应用数据目录（用 mkpath 试写权限）
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appData.isEmpty()) {
        if (QDir(appData).exists() || QDir().mkpath(appData))
            return appData + QStringLiteral("/weibo_search_history.json");
    }
    return QStringLiteral("weibo_search_history.json");
}
