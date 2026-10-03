#pragma once

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

class QNetworkRequest;

// 与本地 Go sidecar（server, 127.0.0.1:8010）通信的客户端。
// 契约见 docs/SPEC.md 第 3 节：响应统一为 { code, message, data }。
//
// 线程约定（与 BiliNetwork 一致）：
//   单例必须在 GUI / qApp 线程首次创建。init_plugin() 可能跑在无事件循环的
//   加载线程，绝不能在那里 instance()；首次触碰放在 attach_engine()。
class WeiboNetwork : public QObject {
    Q_OBJECT

public:
    using SuccessCallback = std::function<void(const QJsonObject &data)>;
    using ErrorCallback = std::function<void(int code, const QString &message)>;
    using RawCallback = std::function<void(const QByteArray &data)>;

    static WeiboNetwork *instance();

    // GET：path 形如 "/feed/home"，params 会拼到 query。
    void get(const QString &path, const QMap<QString, QString> &params,
             SuccessCallback onSuccess, ErrorCallback onError = nullptr,
             int timeoutMs = 0);

    // POST JSON：body 会被序列化成 JSON 请求体。
    void post(const QString &path, const QJsonObject &body,
              SuccessCallback onSuccess, ErrorCallback onError = nullptr,
              int timeoutMs = 0);

    // POST 表单（部分写操作上游只接受 form-urlencoded，Go 侧透传）。
    void postForm(const QString &path, const QMap<QString, QString> &fields,
                  SuccessCallback onSuccess, ErrorCallback onError = nullptr,
                  int timeoutMs = 0);

    // 下载图片原始字节（给 WeiboImageProvider 用）。
    void downloadImage(const QUrl &url, RawCallback onSuccess,
                       ErrorCallback onError = nullptr);

    QString apiBase() const { return m_apiBase; }

    // sidecar 未就绪时请求入队，就绪后自动补发。
    void setApiServerReady(bool ready);

    Q_INVOKABLE void cancelAllRequests();

    // 探测 sidecar 是否已在监听（同步，最多 300ms）。
    static bool probeServer(quint16 port = 8010, int timeoutMs = 300);

signals:
    void networkError(const QString &message);
    void apiServerReadyChanged(bool ready);

private:
    explicit WeiboNetwork(QObject *parent = nullptr);
    ~WeiboNetwork() override;

    WeiboNetwork(const WeiboNetwork &) = delete;
    WeiboNetwork &operator=(const WeiboNetwork &) = delete;

    // 统一的请求发送与响应处理
    void send(QNetworkReply *reply, SuccessCallback onSuccess,
              ErrorCallback onError);
    void handleReply(QNetworkReply *reply, SuccessCallback onSuccess,
                     ErrorCallback onError);

    void applyCommonHeaders(QNetworkRequest &request);
    bool checkRateLimit();
    void trackReply(QNetworkReply *reply);
    void untrackReply(QNetworkReply *reply);
    void flushPendingRequests();

    struct PendingRequest {
        enum Kind { Get, PostJson, PostForm } kind = Get;
        QString path;
        QMap<QString, QString> params;
        QJsonObject body;
        SuccessCallback onSuccess;
        ErrorCallback onError;
        int timeoutMs = 0;
    };

    QNetworkAccessManager *m_nam = nullptr;
    QString m_apiBase;
    int m_requestTimeout = 12000;

    bool m_apiServerReady = true;
    QVector<PendingRequest> m_pendingRequests;
    static constexpr int MAX_PENDING_REQUESTS = 32;
    // 关闸兜底：若 bring-up 丢了完成回调，闸门会永远关着（界面一直转圈）。
    static constexpr int READY_GATE_TIMEOUT_MS = 60000;
    int m_readyGateGeneration = 0;

    QMutex m_replyMutex;
    QSet<QNetworkReply *> m_activeReplies;
    QPointer<QNetworkReply> m_imageReply;
    bool m_cancelingAll = false;

    QMutex m_rateMutex;
    QVector<qint64> m_requestTimestamps;
    static constexpr int MAX_REQUESTS_PER_SECOND = 12;
    static constexpr int MAX_CONCURRENT_REQUESTS = 20;

    QThreadPool m_jsonPool;
    static constexpr int JSON_ASYNC_THRESHOLD = 32 * 1024;

    static WeiboNetwork *s_instance;
    static QMutex s_instanceMutex;
};
