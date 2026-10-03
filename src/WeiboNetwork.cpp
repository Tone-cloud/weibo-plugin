#include "WeiboNetwork.h"

#include "WeiboJsonUtils.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHostAddress>
#include <QMetaObject>
#include <QNetworkRequest>
#include <QRunnable>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

// 与本地 Go sidecar 通信。三条铁律：
//   1) 单例首次创建必须落在 GUI 线程（QNAM 的线程亲和性无法迁移）；
//   2) 大响应体的 JSON 解析必须离开 GUI 线程（设备 CPU 很弱，解析 100KB 会掉帧）；
//   3) 任何限流/异常都不能让请求"硬失败"——设备界面必须始终可用。

namespace {

// 移动端 Safari UA：m.weibo.cn 对 UA 很敏感，桌面 UA 会被降级甚至拒绝
const char *const kMobileUserAgent =
    "Mozilla/5.0 (iPhone; CPU iPhone OS 16_6 like Mac OS X) AppleWebKit/605.1.15 "
    "(KHTML, like Gecko) Version/16.6 Mobile/15E148 Safari/604.1";

QString defaultErrorMessage(int code)
{
    switch (code) {
    case -2: return QStringLiteral("本地服务繁忙，请稍后重试");
    case -3: return QStringLiteral("响应格式异常");
    case -4: return QStringLiteral("请求超时");
    case -5: return QStringLiteral("本地服务未就绪");
    case -6: return QStringLiteral("请求已取消");
    case -100: return QStringLiteral("未登录");
    case -101: return QStringLiteral("登录已过期");
    default: return QStringLiteral("请求失败（%1）").arg(code);
    }
}

// 大响应体（>= JSON_ASYNC_THRESHOLD）的 JSON 解析放到线程池执行。
// QRunnable 不是 QObject，所以用 QPointer 保护着把结果投回 GUI 线程；
// 若期间 WeiboNetwork 已析构，投递会被静默丢弃。
class JsonParseTask : public QRunnable {
public:
    JsonParseTask(const QByteArray &payload, WeiboNetwork *owner,
                  std::function<void(const QJsonDocument &)> deliver)
        : m_payload(payload), m_owner(owner), m_deliver(std::move(deliver))
    {
    }

    void run() override
    {
        const QJsonDocument doc = QJsonDocument::fromJson(m_payload);
        QPointer<WeiboNetwork> guard(m_owner);
        if (!guard)
            return;
        std::function<void(const QJsonDocument &)> deliver = m_deliver;
        QMetaObject::invokeMethod(guard.data(),
                                  [guard, doc, deliver]() {
                                      if (guard)
                                          deliver(doc);
                                  },
                                  Qt::QueuedConnection);
    }

private:
    QByteArray m_payload;
    QPointer<WeiboNetwork> m_owner;
    std::function<void(const QJsonDocument &)> m_deliver;
};

}  // namespace

WeiboNetwork *WeiboNetwork::s_instance = nullptr;
QMutex WeiboNetwork::s_instanceMutex;

WeiboNetwork::WeiboNetwork(QObject *parent)
    : QObject(parent)
{
    m_apiBase = QStringLiteral("http://127.0.0.1:8010");
    m_nam = new QNetworkAccessManager(this);
    m_nam->setTransferTimeout(m_requestTimeout);  // Qt 5.15 才有：兜底超时
    m_jsonPool.setMaxThreadCount(1);              // 设备 CPU 弱，大 body 解析串行即可
}

WeiboNetwork::~WeiboNetwork()
{
    QVector<QNetworkReply *> replies;
    {
        QMutexLocker locker(&m_replyMutex);
        m_cancelingAll = true;  // 让所有 finished 处理器保持沉默
        replies = m_activeReplies.values().toVector();
        m_activeReplies.clear();
    }
    for (QNetworkReply *reply : replies) {
        if (!reply)
            continue;
        reply->disconnect(this);  // 断开我们的 finished 处理器
        reply->abort();           // 由 QNAM 析构时统一回收
    }
    m_pendingRequests.clear();
    m_jsonPool.waitForDone(1000);

    QMutexLocker locker(&s_instanceMutex);
    if (s_instance == this)
        s_instance = nullptr;
}

WeiboNetwork *WeiboNetwork::instance()
{
    QMutexLocker locker(&s_instanceMutex);
    if (!s_instance) {
        // init_plugin() 可能跑在无事件循环的加载线程，绝对不能在那里碰单例：
        // 一旦 QNAM 被钉在错误的线程，之后所有请求都不会返回。
        if (qApp && QThread::currentThread() != qApp->thread())
            qWarning() << "[WeiboNetwork] 单例首次创建不在 GUI 线程，网络请求可能失效";
        s_instance = new WeiboNetwork();
    }
    return s_instance;
}

// ---------------------------------------------------------------------------
// 请求入口
// ---------------------------------------------------------------------------

void WeiboNetwork::get(const QString &path, const QMap<QString, QString> &params,
                       SuccessCallback onSuccess, ErrorCallback onError, int timeoutMs)
{
    if (!m_apiServerReady) {
        PendingRequest pending;
        pending.kind = PendingRequest::Get;
        pending.path = path;
        pending.params = params;
        pending.onSuccess = std::move(onSuccess);
        pending.onError = std::move(onError);
        pending.timeoutMs = timeoutMs;
        if (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
            // 队列满时丢最旧的，并且必须回调它的错误处理器，否则界面一直转圈
            const PendingRequest dropped = m_pendingRequests.takeFirst();
            if (dropped.onError)
                dropped.onError(-2, QStringLiteral("本地服务繁忙，请稍后重试"));
        }
        m_pendingRequests.append(pending);
        return;
    }

    checkRateLimit();

    QUrl url(m_apiBase + path);
    if (!params.isEmpty()) {
        QUrlQuery query;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it)
            query.addQueryItem(it.key(), it.value());
        url.setQuery(query);
    }

    QNetworkRequest request(url);
    applyCommonHeaders(request);
    request.setTransferTimeout(timeoutMs > 0 ? timeoutMs : m_requestTimeout);

    send(m_nam->get(request), std::move(onSuccess), std::move(onError));
}

void WeiboNetwork::post(const QString &path, const QJsonObject &body,
                        SuccessCallback onSuccess, ErrorCallback onError, int timeoutMs)
{
    if (!m_apiServerReady) {
        PendingRequest pending;
        pending.kind = PendingRequest::PostJson;
        pending.path = path;
        pending.body = body;
        pending.onSuccess = std::move(onSuccess);
        pending.onError = std::move(onError);
        pending.timeoutMs = timeoutMs;
        if (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
            const PendingRequest dropped = m_pendingRequests.takeFirst();
            if (dropped.onError)
                dropped.onError(-2, QStringLiteral("本地服务繁忙，请稍后重试"));
        }
        m_pendingRequests.append(pending);
        return;
    }

    checkRateLimit();

    QNetworkRequest request(QUrl(m_apiBase + path));
    applyCommonHeaders(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(timeoutMs > 0 ? timeoutMs : m_requestTimeout);

    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    send(m_nam->post(request, payload), std::move(onSuccess), std::move(onError));
}

void WeiboNetwork::postForm(const QString &path, const QMap<QString, QString> &fields,
                            SuccessCallback onSuccess, ErrorCallback onError, int timeoutMs)
{
    if (!m_apiServerReady) {
        PendingRequest pending;
        pending.kind = PendingRequest::PostForm;
        pending.path = path;
        pending.params = fields;
        pending.onSuccess = std::move(onSuccess);
        pending.onError = std::move(onError);
        pending.timeoutMs = timeoutMs;
        if (m_pendingRequests.size() >= MAX_PENDING_REQUESTS) {
            const PendingRequest dropped = m_pendingRequests.takeFirst();
            if (dropped.onError)
                dropped.onError(-2, QStringLiteral("本地服务繁忙，请稍后重试"));
        }
        m_pendingRequests.append(pending);
        return;
    }

    checkRateLimit();

    QNetworkRequest request(QUrl(m_apiBase + path));
    applyCommonHeaders(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      QStringLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(timeoutMs > 0 ? timeoutMs : m_requestTimeout);

    // 少部分写操作上游只接受 form-urlencoded，Go 侧原样透传
    QUrlQuery query;
    for (auto it = fields.constBegin(); it != fields.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value());
    const QByteArray payload = query.toString(QUrl::FullyEncoded).toUtf8();

    send(m_nam->post(request, payload), std::move(onSuccess), std::move(onError));
}

void WeiboNetwork::downloadImage(const QUrl &url, RawCallback onSuccess, ErrorCallback onError)
{
    if (!url.isValid() || url.isEmpty()) {
        if (onError)
            onError(-3, QStringLiteral("图片地址无效"));
        return;
    }

    checkRateLimit();

    // 同时只保留一个图片下载：新的请求抢占旧的
    QNetworkReply *previous = m_imageReply;
    m_imageReply = nullptr;  // 先清空，避免旧回调把新 reply 置空
    if (previous)
        previous->abort();

    QNetworkRequest request(url);
    // 用 QString 而不是 QLatin1String：QVariant 的可选构造在不同 Qt 版本有差异
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kMobileUserAgent));
    request.setRawHeader("Accept", "image/avif,image/webp,image/apng,image/*,*/*;q=0.8");
    request.setRawHeader("Referer", "https://m.weibo.cn/");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    // Qt 5.15：微博图床会 302 到 wx*.sinaimg.cn，必须跟随跳转
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(15000);

    QNetworkReply *reply = m_nam->get(request);
    m_imageReply = reply;
    trackReply(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, onSuccess, onError]() {
                const QByteArray data = reply->readAll();
                const QNetworkReply::NetworkError error = reply->error();
                const QString errorString = reply->errorString();

                untrackReply(reply);
                if (m_imageReply == reply)
                    m_imageReply = nullptr;
                reply->deleteLater();

                if (error != QNetworkReply::NoError) {
                    // 被新请求抢占时静默丢弃，不打扰界面
                    if (error == QNetworkReply::OperationCanceledError)
                        return;
                    if (onError)
                        onError(-3, QStringLiteral("图片下载失败：%1").arg(errorString));
                    return;
                }
                if (data.isEmpty()) {
                    if (onError)
                        onError(-3, QStringLiteral("图片内容为空"));
                    return;
                }
                if (onSuccess)
                    onSuccess(data);
            });
}

// ---------------------------------------------------------------------------
// 就绪闸门
// ---------------------------------------------------------------------------

void WeiboNetwork::setApiServerReady(bool ready)
{
    const bool changed = (m_apiServerReady != ready);
    m_apiServerReady = ready;
    // 每次调用都推进代号：让上一次的兜底定时器失效，也避免它误放行
    ++m_readyGateGeneration;

    if (changed)
        emit apiServerReadyChanged(ready);

    if (ready)
        flushPendingRequests();

    // 兜底（开闸/关闸都挂）：若 bring-up 丢了完成回调，闸门会永远关着，
    // 界面就一直转圈；超时后强制打开并把积压的请求推出去。
    const int generation = m_readyGateGeneration;
    QTimer::singleShot(READY_GATE_TIMEOUT_MS, this, [this, generation]() {
        if (generation != m_readyGateGeneration)
            return;  // 期间闸门状态又变了，这次兜底作废
        if (!m_apiServerReady) {
            qWarning() << "[WeiboNetwork] 就绪闸门关闭过久，强制打开并补发排队请求:"
                       << m_pendingRequests.size();
            m_apiServerReady = true;
            emit apiServerReadyChanged(true);
        }
        if (!m_pendingRequests.isEmpty())
            flushPendingRequests();
    });
}

void WeiboNetwork::cancelAllRequests()
{
    QVector<QNetworkReply *> replies;
    {
        QMutexLocker locker(&m_replyMutex);
        m_cancelingAll = true;
        replies = m_activeReplies.values().toVector();
        m_activeReplies.clear();
    }

    // 必须在锁外 abort：abort() 会同步发出 finished()，重入 handleReply 还要拿这把锁
    for (QNetworkReply *reply : replies) {
        if (reply)
            reply->abort();
    }

    {
        QMutexLocker locker(&m_replyMutex);
        m_cancelingAll = false;
    }
}

void WeiboNetwork::flushPendingRequests()
{
    if (m_pendingRequests.isEmpty())
        return;

    // 先整体取走：补发过程中（回调里）可能又入队新请求
    const QVector<PendingRequest> pending = m_pendingRequests;
    m_pendingRequests.clear();

    for (const PendingRequest &request : pending) {
        switch (request.kind) {
        case PendingRequest::Get:
            get(request.path, request.params, request.onSuccess, request.onError,
                request.timeoutMs);
            break;
        case PendingRequest::PostJson:
            post(request.path, request.body, request.onSuccess, request.onError,
                 request.timeoutMs);
            break;
        case PendingRequest::PostForm:
            postForm(request.path, request.params, request.onSuccess, request.onError,
                     request.timeoutMs);
            break;
        }
    }
}

bool WeiboNetwork::probeServer(quint16 port, int timeoutMs)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(timeoutMs)) {
        socket.abort();
        return false;
    }
    socket.disconnectFromHost();
    return true;
}

// ---------------------------------------------------------------------------
// 内部实现
// ---------------------------------------------------------------------------

void WeiboNetwork::send(QNetworkReply *reply, SuccessCallback onSuccess, ErrorCallback onError)
{
    if (!reply)
        return;
    trackReply(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, onSuccess, onError]() mutable {
                handleReply(reply, std::move(onSuccess), std::move(onError));
            });
}

void WeiboNetwork::handleReply(QNetworkReply *reply, SuccessCallback onSuccess,
                               ErrorCallback onError)
{
    if (!reply)
        return;

    const QByteArray payload = reply->readAll();
    const QNetworkReply::NetworkError netError = reply->error();
    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString errorString = reply->errorString();

    untrackReply(reply);
    reply->deleteLater();

    QPointer<WeiboNetwork> self(this);

    // 统一处理 { code, message, data } 信封；同步/异步解析后都会走到这里
    auto process = [self, onSuccess, onError, netError, httpStatus, errorString](
                       const QJsonDocument &doc) {
        if (!self)
            return;

        const QJsonObject root = doc.object();
        const bool hasEnvelope = doc.isObject() && root.contains(QStringLiteral("code"));

        if (netError != QNetworkReply::NoError) {
            // nginx/代理在 4xx、5xx 时仍可能返回带信封的 JSON，能解就用里面的 code
            if (hasEnvelope) {
                const int code = static_cast<int>(WeiboJson::num(root, "code"));
                QString message = WeiboJson::str(root, "message");
                if (message.isEmpty())
                    message = defaultErrorMessage(code);
                if (code == 0) {
                    if (onSuccess)
                        onSuccess(root.value(QStringLiteral("data")).toObject());
                } else if (onError) {
                    onError(code, message);
                }
                return;
            }

            int code = -3;
            QString message;
            if (netError == QNetworkReply::TimeoutError) {
                code = -4;
                message = defaultErrorMessage(-4);
            } else if (netError == QNetworkReply::ConnectionRefusedError
                       || netError == QNetworkReply::HostNotFoundError
                       || netError == QNetworkReply::RemoteHostClosedError) {
                code = -5;
                message = defaultErrorMessage(-5);
            } else if (netError == QNetworkReply::OperationCanceledError) {
                QMutexLocker locker(&self->m_replyMutex);
                if (self->m_cancelingAll)
                    return;  // cancelAllRequests() 期间静默，不弹错误
                code = -6;
                message = defaultErrorMessage(-6);
            } else {
                code = -3;
                if (httpStatus >= 400)
                    message = QStringLiteral("服务器错误（HTTP %1）").arg(httpStatus);
                else
                    message = QStringLiteral("网络请求失败：%1").arg(errorString);
            }

            emit self->networkError(message);
            if (onError)
                onError(code, message);
            return;
        }

        if (!hasEnvelope) {
            const QString message = defaultErrorMessage(-3);
            emit self->networkError(message);
            if (onError)
                onError(-3, message);
            return;
        }

        const int code = static_cast<int>(WeiboJson::num(root, "code"));
        if (code == 0) {
            if (onSuccess)
                onSuccess(root.value(QStringLiteral("data")).toObject());
            return;
        }

        // 业务错误（含 -100/-101 未登录）交给调用方处理，这里不弹全局错误
        QString message = WeiboJson::str(root, "message");
        if (message.isEmpty())
            message = defaultErrorMessage(code);
        if (onError)
            onError(code, message);
    };

    if (payload.size() >= JSON_ASYNC_THRESHOLD) {
        // 大响应体：解析丢线程池，避免 GUI 线程卡住
        m_jsonPool.start(new JsonParseTask(payload, this, process));
        return;
    }

    process(QJsonDocument::fromJson(payload));
}

void WeiboNetwork::applyCommonHeaders(QNetworkRequest &request)
{
    // 用 QString 而不是 QLatin1String：QVariant 的可选构造在不同 Qt 版本有差异
    request.setHeader(QNetworkRequest::UserAgentHeader, QString::fromLatin1(kMobileUserAgent));
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    request.setRawHeader("Accept-Language", "zh-CN,zh;q=0.9");
    request.setRawHeader("Connection", "keep-alive");
}

void WeiboNetwork::trackReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    QMutexLocker locker(&m_replyMutex);
    m_activeReplies.insert(reply);
    // 只告警不拒绝：用户在快速滑动时并发多，不能让请求失败
    if (m_activeReplies.size() == MAX_CONCURRENT_REQUESTS + 1)
        qWarning() << "[WeiboNetwork] 并发请求过多:" << m_activeReplies.size();
}

void WeiboNetwork::untrackReply(QNetworkReply *reply)
{
    if (!reply)
        return;
    QMutexLocker locker(&m_replyMutex);
    m_activeReplies.remove(reply);
}

bool WeiboNetwork::checkRateLimit()
{
    QMutexLocker locker(&m_rateMutex);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1 秒滑动窗口
    while (!m_requestTimestamps.isEmpty() && now - m_requestTimestamps.first() > 1000)
        m_requestTimestamps.removeFirst();

    if (m_requestTimestamps.size() >= MAX_REQUESTS_PER_SECOND) {
        // 超限只告警、绝不硬失败：设备界面必须保持可用
        qWarning() << "[WeiboNetwork] 1 秒内请求过多（" << m_requestTimestamps.size()
                   << "），已放开限流";
        m_requestTimestamps.append(now);
        return false;
    }

    m_requestTimestamps.append(now);
    return true;
}
