#pragma once

#include <QAtomicInt>
#include <QCache>
#include <QImage>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QRunnable>
#include <QReadWriteLock>
#include <QSize>
#include <QString>
#include <QThreadPool>

class WeiboNetwork;

// 异步图片响应：在工作线程下载 + 解码 + 缩放，完成后回 GUI 线程。
class WeiboImageResponse : public QQuickImageResponse, public QRunnable {
    Q_OBJECT
public:
    WeiboImageResponse(const QString &id, const QSize &requestedSize,
                       QCache<QString, QImage> *cache, QReadWriteLock *cacheLock);

    QQuickTextureFactory *textureFactory() const override;
    void run() override;
    void cancel() override;

private:
    QImage downloadAndDecode(const QString &url);
    QImage scaledFor(const QImage &image) const;
    static QImage placeholder(int w, int h);
    static bool looksLikeImage(const QByteArray &data);
    static QImage roundCrop(const QImage &image);

    QString m_id;
    QSize m_requestedSize;
    QImage m_image;
    QCache<QString, QImage> *m_cache = nullptr;
    QReadWriteLock *m_cacheLock = nullptr;
    QAtomicInt m_cancelled;
    QString m_cacheKey;
};

// image://weibo/<urlencoded-url>             原图（按请求尺寸缩放）
// image://weibo/original/<urlencoded-url>    原图不缩放
// image://weibo/avatar/<urlencoded-url>      圆形裁剪，加 @120w_120h
// image://weibo/size/<w>x<h>/<urlencoded-url> 指定尺寸
class WeiboImageProvider : public QQuickAsyncImageProvider {
public:
    explicit WeiboImageProvider(WeiboNetwork *network);
    ~WeiboImageProvider() override;

    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

    // 解析 image:// id 里的通道前缀与目标尺寸
    static bool parseId(const QString &id, QString *url, QSize *forcedSize,
                        bool *roundAvatar);

private:
    QPointer<WeiboNetwork> m_network;
    QReadWriteLock m_cacheLock;
    QCache<QString, QImage> m_cache;
    QThreadPool m_threadPool;

    static constexpr int MAX_CACHE_COST = 24 * 1024 * 1024;  // 24 MiB
    static constexpr int MAX_CONCURRENT = 8;
};
