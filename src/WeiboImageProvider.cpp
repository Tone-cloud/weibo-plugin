#include "WeiboImageProvider.h"

#include "WeiboNetwork.h"

#include <QEventLoop>
#include <QFontDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QQuickTextureFactory>
#include <QReadLocker>
#include <QSize>
#include <QTimer>
#include <QUrl>
#include <QWriteLocker>

// image://weibo 的异步实现：下载 + 解码 + 缩放全在工作线程完成。
//
// 关键约束：工作线程里**绝对不能**使用 WeiboNetwork/QNAM（它们属于 GUI 线程），
// 所以每个响应自己 new 一个局部 QNetworkAccessManager + QEventLoop 等待。

namespace {

constexpr int kDownloadTimeoutMs = 15000;
constexpr int kMinImageSide = 4;   // 小于这个尺寸基本是统计像素/错误图
constexpr int kPlaceholderSide = 160;

const char *const kMobileUserAgent =
    "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1";

QSize placeholderSize(const QSize &requested)
{
    return QSize(requested.width() > 0 ? requested.width() : kPlaceholderSide,
                 requested.height() > 0 ? requested.height() : kPlaceholderSide);
}

// 头像走 @120w_120h 后缀：先去掉已有 @... 尺寸后缀，保留 query。
QString avatarDownloadUrl(const QString &url)
{
    const QString trimmed = url.trimmed();
    const int queryPos = trimmed.indexOf(QLatin1Char('?'));
    QString head = (queryPos >= 0) ? trimmed.left(queryPos) : trimmed;
    const QString tail = (queryPos >= 0) ? trimmed.mid(queryPos) : QString();

    // 只有最后一个 '/' 之后的 '@' 才是尺寸后缀（path 里也可能出现 '@'）
    const int at = head.lastIndexOf(QLatin1Char('@'));
    if (at > head.lastIndexOf(QLatin1Char('/')))
        head = head.left(at);

    head += QLatin1String("@120w_120h");
    return head + tail;
}

}  // namespace

// ===========================================================================
// WeiboImageResponse
// ===========================================================================

WeiboImageResponse::WeiboImageResponse(const QString &id, const QSize &requestedSize,
                                       QCache<QString, QImage> *cache,
                                       QReadWriteLock *cacheLock)
    : m_id(id),
      m_requestedSize(requestedSize),
      m_cache(cache),
      m_cacheLock(cacheLock)
{
    // QML 引擎负责销毁响应对象；线程池跑完 run() 不能顺手 delete 它
    setAutoDelete(false);
    m_cancelled.storeRelease(0);

    QString url;
    QSize forcedSize;
    bool roundAvatar = false;
    WeiboImageProvider::parseId(m_id, &url, &forcedSize, &roundAvatar);

    // parseId 的尺寸约定：
    //   QSize(0,0)     → 原图通道，不缩放
    //   有效尺寸        → size/WxH/ 或 avatar/ 的强制尺寸
    //   QSize(-1,-1)   → 用 QML 传进来的请求尺寸
    if (forcedSize == QSize(0, 0))
        m_requestedSize = QSize(-1, -1);
    else if (forcedSize.isValid())
        m_requestedSize = forcedSize;
    else if (!m_requestedSize.isValid())
        m_requestedSize = QSize(-1, -1);

    // 缓存键必须同时区分通道、URL 和变换（缩放尺寸 / 圆形裁剪）
    m_cacheKey = m_id + QLatin1Char('|') + QString::number(m_requestedSize.width())
            + QLatin1Char('x') + QString::number(m_requestedSize.height());
    if (roundAvatar)
        m_cacheKey += QLatin1String("|round");
}

QQuickTextureFactory *WeiboImageResponse::textureFactory() const
{
    if (m_image.isNull())
        return nullptr;
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

void WeiboImageResponse::run()
{
    if (m_cancelled.loadAcquire()) {
        // 即使被取消也要发 finished()：否则 QML 引擎会一直持有这个响应对象
        emit finished();
        return;
    }

    QString url;
    QSize forcedSize;
    bool roundAvatar = false;
    const bool parsed = WeiboImageProvider::parseId(m_id, &url, &forcedSize, &roundAvatar);
    if (!parsed || url.isEmpty()) {
        const QSize fallback = placeholderSize(m_requestedSize);
        m_image = placeholder(fallback.width(), fallback.height());
        emit finished();
        return;
    }

    // 1) 命中缓存就直接返回（QCache::object() 会动 LRU 链表，这里按契约用读锁）
    if (m_cache && m_cacheLock) {
        QReadLocker locker(m_cacheLock);
        if (const QImage *cached = m_cache->object(m_cacheKey))
            m_image = *cached;
    }

    // 2) 未命中：下载 + 解码 + 缩放
    if (m_image.isNull() && !m_cancelled.loadAcquire()) {
        const QString downloadUrl = roundAvatar ? avatarDownloadUrl(url) : url;
        const QImage downloaded = downloadAndDecode(downloadUrl);
        if (!downloaded.isNull()) {
            QImage result = scaledFor(downloaded);
            if (roundAvatar)
                result = roundCrop(result);
            if (!result.isNull()) {
                m_image = result;
                if (m_cache && m_cacheLock) {
                    QWriteLocker locker(m_cacheLock);
                    // cost 用字节数（QCache 的 cost 是 int），上限 24MiB
                    m_cache->insert(m_cacheKey, new QImage(m_image),
                                    qMax(1, static_cast<int>(m_image.sizeInBytes())));
                }
            }
        }
    }

    // 3) 失败也要给图：QML 侧不允许拿到空图
    if (m_image.isNull()) {
        const QSize fallback = placeholderSize(m_requestedSize);
        m_image = placeholder(fallback.width(), fallback.height());
    }

    emit finished();
}

void WeiboImageResponse::cancel()
{
    m_cancelled.storeRelease(1);
}

QImage WeiboImageResponse::downloadAndDecode(const QString &url)
{
    if (m_cancelled.loadAcquire() || url.isEmpty())
        return QImage();

    // 工作线程私有 QNAM：绝不复用 GUI 线程的 WeiboNetwork/QNAM
    QNetworkAccessManager nam;
    // 注意用花括号初始化：写成 QNetworkRequest request(QUrl(url));
    // 会被 C++ 解析成函数声明（most vexing parse），
    // 报错是 "request is of non-class type QNetworkRequest(QUrl)"。
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kMobileUserAgent));
    request.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    request.setRawHeader("Referer", "https://m.weibo.cn/");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply *reply = nam.get(request);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    // 15 秒硬超时：不能把线程池的线程永久占住
    QObject::connect(&timer, &QTimer::timeout, &loop, [&loop, reply]() {
        if (reply)
            reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kDownloadTimeoutMs);
    if (!reply->isFinished())
        loop.exec();
    timer.stop();

    // readAll 必须在事件循环退出后、reply 还活着时做
    const QByteArray data = reply->readAll();
    reply->deleteLater();   // 局部 QNAM 析构时会一并回收，这里没有事件循环也没关系

    if (m_cancelled.loadAcquire())
        return QImage();
    if (data.isEmpty() || !looksLikeImage(data))
        return QImage();

    const QImage image = QImage::fromData(data);
    if (image.isNull())
        return QImage();
    if (image.width() < kMinImageSide || image.height() < kMinImageSide)
        return QImage();   // 1x1 统计像素之类，当失败处理
    return image;
}

QImage WeiboImageResponse::scaledFor(const QImage &image) const
{
    if (image.isNull())
        return image;
    // 原图通道（QSize(-1,-1)）：不缩放
    if (!m_requestedSize.isValid() || m_requestedSize.width() <= 0
        || m_requestedSize.height() <= 0) {
        return image;
    }
    // 已经比目标小就不放大（省内存，QML 自己会拉伸）
    if (image.width() <= m_requestedSize.width() && image.height() <= m_requestedSize.height())
        return image;
    return image.scaled(m_requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QImage WeiboImageResponse::placeholder(int w, int h)
{
    if (w <= 0)
        w = kPlaceholderSide;
    if (h <= 0)
        h = kPlaceholderSide;

    // 永远不返回空图
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(0x24, 0x24, 0x24));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(0xA0, 0xA0, 0xA0), 1));
    painter.drawRect(QRect(0, 0, w - 1, h - 1));

    // 设备上中文字体不全，按优先级挑第一个装了的
    static const char *const kFamilies[] = { "Microsoft YaHei", "Noto Sans CJK SC",
                                             "WenQuanYi Micro Hei", "Source Han Sans CN",
                                             "SimHei", "Droid Sans Fallback",
                                             "sans-serif" };
    QFont font;
    // Qt 5 里 QFontDatabase::families() 是成员函数（Qt 6 才变成静态），
    // 所以这里必须构造一个实例再调用。
    const QFontDatabase database;
    const QStringList available = database.families();
    for (const char *const family : kFamilies) {
        const QString name = QString::fromLatin1(family);
        if (name == QLatin1String("sans-serif") || available.contains(name)) {
            font = QFont(name);
            break;
        }
    }
    font.setPixelSize(qMax(10, qMin(w, h) / 5));
    painter.setFont(font);
    painter.setPen(QColor(0x66, 0x66, 0x66));
    painter.drawText(QRect(0, 0, w, h), Qt::AlignCenter, QStringLiteral("微博"));
    painter.end();

    return image;
}

bool WeiboImageResponse::looksLikeImage(const QByteArray &data)
{
    if (data.size() < 128)
        return false;   // 小于 128 字节肯定不是图片（空响应/错误页）

    QByteArray head = data.left(96);
    if (head.startsWith("\xEF\xBB\xBF"))
        head.remove(0, 3);   // UTF-8 BOM
    head = head.trimmed().toLower();

    if (head.startsWith("<!doctype") || head.startsWith("<html") || head.startsWith("{\""))
        return false;   // CDN 出错时经常回一个 HTML 错误页或 JSON
    return true;
}

QImage WeiboImageResponse::roundCrop(const QImage &image)
{
    if (image.isNull())
        return image;

    const int side = qMin(image.width(), image.height());
    if (side <= 0)
        return image;

    // 先取居中的正方形，再转成预乘 ARGB（合成遮罩必须要这个格式）
    QImage square = image
                            .copy((image.width() - side) / 2, (image.height() - side) / 2,
                                  side, side)
                            .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // 圆形 alpha 遮罩（DestinationIn 把圆外像素抹成全透明，边缘由抗锯齿保证平滑）
    QImage mask(side, side, QImage::Format_ARGB32_Premultiplied);
    mask.fill(Qt::transparent);
    {
        QPainter maskPainter(&mask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        maskPainter.setPen(Qt::NoPen);
        maskPainter.setBrush(Qt::white);
        maskPainter.drawEllipse(QRectF(0.5, 0.5, side - 1.0, side - 1.0));
    }

    QPainter painter(&square);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    painter.drawImage(0, 0, mask);
    painter.end();

    return square;
}

// ===========================================================================
// WeiboImageProvider
// ===========================================================================

WeiboImageProvider::WeiboImageProvider(WeiboNetwork *network)
    : m_network(network),
      m_cache(MAX_CACHE_COST)
{
    m_threadPool.setMaxThreadCount(MAX_CONCURRENT);
}

WeiboImageProvider::~WeiboImageProvider()
{
    // 等在跑的响应收尾，避免它们用到已经析构的缓存
    m_threadPool.waitForDone(3000);
    QWriteLocker locker(&m_cacheLock);
    m_cache.clear();
}

QQuickImageResponse *WeiboImageProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    WeiboImageResponse *response =
            new WeiboImageResponse(id, requestedSize, &m_cache, &m_cacheLock);
    m_threadPool.start(response);
    return response;
}

bool WeiboImageProvider::parseId(const QString &id, QString *url, QSize *forcedSize,
                                 bool *roundAvatar)
{
    if (forcedSize)
        *forcedSize = QSize(-1, -1);   // 默认：用 QML 请求的尺寸
    if (roundAvatar)
        *roundAvatar = false;
    if (url)
        url->clear();

    // QML 侧一般把整段（含通道前缀）encodeURIComponent，所以先整体解码
    QString rest = QUrl::fromPercentEncoding(id.toUtf8());
    if (rest.startsWith(QLatin1Char('/')))
        rest.remove(0, 1);   // 容错：image://weibo//<url>

    if (rest.startsWith(QLatin1String("original/"))) {
        rest = rest.mid(9);
        if (forcedSize)
            *forcedSize = QSize(0, 0);   // 约定：QSize(0,0) = 原图，不缩放
    } else if (rest.startsWith(QLatin1String("avatar/"))) {
        rest = rest.mid(7);
        if (forcedSize)
            *forcedSize = QSize(120, 120);   // 与 @120w_120h 对齐
        if (roundAvatar)
            *roundAvatar = true;
    } else if (rest.startsWith(QLatin1String("size/"))) {
        rest = rest.mid(5);
        const int slash = rest.indexOf(QLatin1Char('/'));
        if (slash > 0) {
            const QString dim = rest.left(slash);
            rest = rest.mid(slash + 1);
            const int x = dim.indexOf(QLatin1Char('x'));
            if (x > 0) {
                bool okW = false;
                bool okH = false;
                const int w = dim.left(x).toInt(&okW);
                const int h = dim.mid(x + 1).toInt(&okH);
                if (okW && okH && w > 0 && h > 0 && forcedSize)
                    *forcedSize = QSize(w, h);
            }
        } else {
            rest.clear();   // 通道语法不对，当作空 URL
        }
    }

    rest = rest.trimmed();
    if (url)
        *url = rest;
    return !rest.isEmpty();
}
