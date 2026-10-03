// =============================================================================
// WeiboController.cpp
//
// QML 边界的唯一入口，包含三部分：
//   1) WeiboController：全局登录态 / 详情快照 / 媒体 / 发布 / 超话签到状态，
//      以及 10 个业务模块与全部列表模型的持有者；
//   2) Go sidecar（server，127.0.0.1:8010）的同步 bring-up；
//   3) PenMods 插件三入口：init_plugin / attach_engine / destroy_plugin。
//
// 契约见 docs/SPEC.md：第 1 节（插件 ABI + 线程规则）、第 3 节（响应信封）、
// 第 6 节（C++ 层契约）、第 9 节（编码约定）。
// =============================================================================

#include "WeiboController.h"

#include "WeiboImageProvider.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"
#include "modules/comment/WeiboCommentModule.h"
#include "modules/feed/WeiboFeedModule.h"
#include "modules/login/WeiboLoginModule.h"
#include "modules/media/WeiboMediaModule.h"
#include "modules/profile/WeiboProfileModule.h"
#include "modules/publish/WeiboPublishModule.h"
#include "modules/search/WeiboSearchModule.h"
#include "modules/status/WeiboStatusModule.h"
#include "modules/topic/WeiboTopicModule.h"
#include "modules/viewer/WeiboViewerModule.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QQmlContext>
#include <QQmlEngine>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml>

#include <csignal>
#include <signal.h>

namespace {

// ---- Go sidecar 常量（与 docs/SPEC.md 第 1 / 3 节一致）----
// 注意：kPluginRoot 自带结尾斜杠，拼接可执行文件名时不要再补 '/'，
// 否则 pgrep -f 拿 "//server" 去匹配会漏掉以单斜杠路径启动的进程。
const QString kPluginRoot = QStringLiteral("/userdisk/PenMods/plugins/weibo_plugin/");
const QString kServerExec = QStringLiteral("server");
const quint16 kServerPort = 8010;

// 本插件自己拉起的 sidecar 进程。只有持有它时才由我们负责停止/销毁；
// 若 sidecar 是别的实例（或上次运行残留）拉起的，绝不能杀。
QPointer<QProcess> s_apiServerProcess;

// ---- QML 引擎与图片 Provider（attach_engine / destroy_plugin 共享）----
// s_imageProvider 的所有权在 addImageProvider 之后归 QQmlEngine，
// destroy_plugin 只解除引用，不 delete。
QQmlEngine *s_engine = nullptr;
QMutex s_providerMutex;
WeiboImageProvider *s_imageProvider = nullptr;

}  // namespace

// =============================================================================
// WeiboController
// =============================================================================

// 模块构造时会把 controller 传给 QObject 基类，因此模块同时是 controller 的
// 子对象；header 里它们又由 std::unique_ptr 独占持有。这在 Qt 下是安全的：
// ~WeiboController 先析构成员（unique_ptr 删除模块，模块的 ~QObject 会把
// 自己从父对象的 children 列表里摘掉），之后基类 ~QObject 已经没有子对象可删。
WeiboController::WeiboController(QObject *parent)
    : QObject(parent),
      m_network(WeiboNetwork::instance()),
      m_feedModule(std::make_unique<WeiboFeedModule>(this)),
      m_statusModule(std::make_unique<WeiboStatusModule>(this)),
      m_commentModule(std::make_unique<WeiboCommentModule>(this)),
      m_searchModule(std::make_unique<WeiboSearchModule>(this)),
      m_profileModule(std::make_unique<WeiboProfileModule>(this)),
      m_loginModule(std::make_unique<WeiboLoginModule>(this)),
      m_publishModule(std::make_unique<WeiboPublishModule>(this)),
      m_topicModule(std::make_unique<WeiboTopicModule>(this)),
      m_mediaModule(std::make_unique<WeiboMediaModule>(this)),
      m_viewerModule(std::make_unique<WeiboViewerModule>(this)) {
    // 本类只允许在 GUI 线程（attach_engine 所在线程）构造：WeiboNetwork 单例会
    // 把 QNAM 钉在首次 instance() 的线程上，钉错线程后请求发得出去也收不到
    // finished。这里只做诊断告警，宿主若在加载线程构造 QML 对象属于宿主 bug。
    QCoreApplication *app = QCoreApplication::instance();
    if (app && QThread::currentThread() != app->thread()) {
        qWarning() << "WeiboPlugin: WeiboController 在非 GUI 线程构造，"
                      "WeiboNetwork 的线程亲和可能出错";
    }

    // 网络层错误统一冒泡到 globalError；销毁过程中不再打搅已解体的界面
    if (m_network) {
        connect(m_network, &WeiboNetwork::networkError, this,
                [this](const QString &message) {
                    if (!m_destroying && !message.isEmpty())
                        setGlobalError(message);
                });
    }

    // 模型同样以 this 为父对象。注意：成员初始化列表里模块先于模型构造完成
    // （header 中模块声明在模型之前，初始化顺序必须与声明顺序一致），所以
    // 模块构造函数里不要去取这些模型指针；模块只在 Q_INVOKABLE 里使用它们。
    createModels();
}

WeiboController::~WeiboController() {
    m_destroying = true;

    // 取消所有在途请求，防止回调访问正在析构的模型 / 模块
    if (m_network)
        m_network->cancelAllRequests();
}

void WeiboController::createModels() {
    // 全部以 this 为父对象，随 controller 一起销毁
    m_homeModel = new BlogListModel(this);
    m_followModel = new BlogListModel(this);
    m_hotStatusModel = new BlogListModel(this);
    m_groupModel = new BlogListModel(this);
    m_searchStatusModel = new BlogListModel(this);
    m_userStatusModel = new BlogListModel(this);
    m_topicStatusModel = new BlogListModel(this);
    m_myStatusModel = new BlogListModel(this);
    m_favoriteModel = new BlogListModel(this);
    m_mentionModel = new BlogListModel(this);
    m_repostModel = new BlogListModel(this);

    m_commentModel = new CommentListModel(this);
    m_commentReplyModel = new CommentReplyListModel(this);

    m_hotSearchModel = new HotSearchModel(this);

    m_userSearchModel = new UserListModel(this);
    m_followingModel = new UserListModel(this);
    m_followerModel = new UserListModel(this);
    m_likerModel = new UserListModel(this);

    m_topicSearchModel = new TopicListModel(this);
    m_myTopicModel = new TopicListModel(this);

    m_pictureModel = new PictureListModel(this);

    m_searchHistoryModel = new SearchHistoryModel(this);
}

// ====== 加载状态（引用计数，避免并发请求闪烁） ======

void WeiboController::setIsLoading(bool loading) {
    if (loading) {
        ++m_loadingCount;
    } else {
        // 夹紧到 0：重复的完成回调不能把计数压成负数
        m_loadingCount = qMax(0, m_loadingCount - 1);
    }

    const bool newLoading = m_loadingCount > 0;
    if (m_isLoading != newLoading) {
        m_isLoading = newLoading;
        emit isLoadingChanged();
    }
}

void WeiboController::setGlobalError(const QString &error) {
    const bool changed = (m_globalError != error);
    m_globalError = error;
    if (changed)
        emit globalErrorChanged();

    // 空串表示「清除错误」，不弹 toast
    if (!error.isEmpty())
        emit toastMessage(error);
}

void WeiboController::clearError() { setGlobalError(QString()); }

void WeiboController::cancelAll() {
    // 先落定前端可见状态再 abort：abort() 会同步触发 onError 回调，
    // 不先复位 loading 的话，用户主动取消会被误报成请求失败。
    m_loadingCount = 0;
    if (m_isLoading) {
        m_isLoading = false;
        emit isLoadingChanged();
    }

    // 复位各列表模型的 loading，避免回调没跑到导致界面一直转圈
    BlogListModel *blogModels[] = {
        m_homeModel,       m_followModel,       m_hotStatusModel,
        m_groupModel,      m_searchStatusModel, m_userStatusModel,
        m_topicStatusModel, m_myStatusModel,    m_favoriteModel,
        m_mentionModel,    m_repostModel};
    for (BlogListModel *model : blogModels) {
        if (model)
            model->setLoading(false);
    }
    if (m_commentModel)
        m_commentModel->setLoading(false);
    if (m_commentReplyModel)
        m_commentReplyModel->setLoading(false);
    if (m_hotSearchModel)
        m_hotSearchModel->setLoading(false);

    UserListModel *userModels[] = {m_userSearchModel, m_followingModel,
                                   m_followerModel, m_likerModel};
    for (UserListModel *model : userModels) {
        if (model)
            model->setLoading(false);
    }

    TopicListModel *topicModels[] = {m_topicSearchModel, m_myTopicModel};
    for (TopicListModel *model : topicModels) {
        if (model)
            model->setLoading(false);
    }

    if (m_network)
        m_network->cancelAllRequests();

    emit toastMessage(QStringLiteral("已取消当前请求"));
}

// ====== 登录态 ======

void WeiboController::clearLocalLoginState() {
    // Cookie 失效 / 未登录（信封 code -100/-101）时由各模块调用
    m_loggedIn = false;
    m_userId = 0;
    m_userName.clear();
    m_userAvatar.clear();
    m_userCover.clear();
    m_userDescription.clear();
    m_userFollowers = 0;
    m_userFollowing = 0;
    m_userStatusesCount = 0;
    m_userVerified = false;

    emit loginStateChanged();
    emit loginExpired();
}

void WeiboController::setLoginUser(const WeiboUser &user) {
    m_loggedIn = (user.uid > 0) || !user.name.isEmpty();
    m_userId = user.uid;
    m_userName = user.name;
    m_userAvatar = user.avatar;
    m_userCover = user.cover;
    m_userDescription = user.description;
    m_userFollowers = static_cast<int>(user.followers);
    m_userFollowing = static_cast<int>(user.following);
    m_userStatusesCount = static_cast<int>(user.statusesCount);
    m_userVerified = user.verified;

    emit loginStateChanged();
}

// ====== 详情快照 ======

void WeiboController::setDetail(const WeiboBlog &blog) {
    m_detail = blog;
    // 计数类属性挂在 detailStatsChanged 上，快照整体替换时两个信号一起发
    emit detailChanged();
    emit detailStatsChanged();
}

void WeiboController::clearDetail() {
    m_detail = WeiboBlog();
    emit detailChanged();
    emit detailStatsChanged();
}

void WeiboController::setDetailStats(qint64 reposts, qint64 comments,
                                     qint64 attitudes, int attitudesStatus,
                                     bool favorited) {
    m_detail.repostsCount = reposts;
    m_detail.commentsCount = comments;
    m_detail.attitudesCount = attitudes;
    m_detail.attitudesStatus = attitudesStatus;
    m_detail.favorited = favorited;

    emit detailStatsChanged();
}

void WeiboController::applyDetailLike(bool liked, qint64 attitudesCount) {
    // 传负值表示调用方不知道最新计数，沿用快照里的旧值
    if (attitudesCount < 0)
        attitudesCount = m_detail.attitudesCount;

    m_detail.attitudesStatus = liked ? 1 : 0;
    m_detail.attitudesCount = attitudesCount;
    emit detailStatsChanged();

    // 同步刷新列表里的同一行，避免从详情返回列表时点赞态回退
    const QString id = m_detail.id;
    if (id.isEmpty())
        return;

    BlogListModel *models[] = {m_homeModel,   m_searchStatusModel,
                               m_userStatusModel, m_topicStatusModel,
                               m_repostModel};
    for (BlogListModel *model : models) {
        if (model)
            model->applyLikeState(id, liked, attitudesCount);
    }
}

void WeiboController::applyDetailFavorite(bool favorited) {
    m_detail.favorited = favorited;
    emit detailStatsChanged();
}

QVariantList WeiboController::detailPics() const {
    QVariantList list;
    list.reserve(m_detail.pics.size());
    for (int i = 0; i < m_detail.pics.size(); ++i)
        list.append(PictureListModel::pictureToMap(m_detail.pics.at(i), i));
    return list;
}

QVariantList WeiboController::detailRetweetedPics() const {
    QVariantList list;
    list.reserve(m_detail.retweetedPics.size());
    for (int i = 0; i < m_detail.retweetedPics.size(); ++i)
        list.append(PictureListModel::pictureToMap(m_detail.retweetedPics.at(i), i));
    return list;
}

// ====== 媒体（视频 / 直播） ======

void WeiboController::setMediaInfo(const WeiboPageInfo &page) {
    m_media = page;
    emit mediaChanged();
}

void WeiboController::setMediaQualities(const QVector<WeiboMediaQuality> &qualities) {
    m_mediaQualities = qualities;
    emit mediaChanged();
}

void WeiboController::setMediaLoading(bool loading) {
    if (m_mediaLoading == loading)
        return;
    m_mediaLoading = loading;
    emit mediaChanged();
}

void WeiboController::setMediaStatus(const QString &status) {
    if (m_mediaStatus == status)
        return;
    m_mediaStatus = status;
    emit mediaChanged();
}

void WeiboController::clearMedia() {
    m_media = WeiboPageInfo();
    m_mediaQualities.clear();
    m_mediaLoading = false;
    m_mediaStatus.clear();
    emit mediaChanged();
}

QVariantList WeiboController::mediaQualities() const {
    QVariantList list;
    list.reserve(m_mediaQualities.size());
    for (const WeiboMediaQuality &quality : m_mediaQualities) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), quality.label);
        item.insert(QStringLiteral("url"), quality.url);
        list.append(item);
    }
    return list;
}

// ====== 发布 ======

void WeiboController::setPublishState(bool uploading, double progress,
                                      const QString &status) {
    m_publishUploading = uploading;
    m_publishProgress = progress;
    m_publishStatus = status;
    emit publishStateChanged();
}

void WeiboController::setPublishDraft(const QString &draft) {
    if (m_publishDraft == draft)
        return;
    m_publishDraft = draft;
    emit publishDraftChanged();
}

// ====== 超话签到 ======

void WeiboController::setCheckinState(bool running, int success, int failed,
                                      const QString &summary) {
    m_checkinRunning = running;
    m_checkinSuccess = qMax(0, success);
    m_checkinFailed = qMax(0, failed);
    m_checkinSummary = summary;
    emit checkinStateChanged();
}

// ====== 子模块访问器（QML 侧 controller.feed / controller.status ...） ======

QObject *WeiboController::feed() const { return m_feedModule.get(); }
QObject *WeiboController::status() const { return m_statusModule.get(); }
QObject *WeiboController::comments() const { return m_commentModule.get(); }
QObject *WeiboController::search() const { return m_searchModule.get(); }
QObject *WeiboController::profile() const { return m_profileModule.get(); }
QObject *WeiboController::login() const { return m_loginModule.get(); }
QObject *WeiboController::publish() const { return m_publishModule.get(); }
QObject *WeiboController::topic() const { return m_topicModule.get(); }
QObject *WeiboController::media() const { return m_mediaModule.get(); }
QObject *WeiboController::viewer() const { return m_viewerModule.get(); }

// ====== QML 入口 ======

void WeiboController::bootstrap() {
    // 这里不 emit 任何信号：界面靠各模块自己的模型 / 信号刷新，
    // controller 只负责把首屏需要的三条请求发出去。
    if (m_loginModule)
        m_loginModule->checkLogin();
    if (m_feedModule) {
        m_feedModule->fetchHot();
        m_feedModule->fetchHome();
    }
}

QString WeiboController::WeiboJsonFormatCount(qint64 n) {
    return WeiboJson::formatCount(n);
}

// =============================================================================
// Go sidecar bring-up（同步，不依赖事件循环）
//
// init_plugin() 可能在无事件循环的加载线程里被调用，纯 QTimer/QTcpSocket 的
// 异步链会永久卡住（表现为 Go 不启动、ready 闸门一直关着、一个请求都不发），
// 所以整条链路用 waitForStarted + 阻塞式端口探测实现。
// =============================================================================

bool weibo_startApiServerSync() {
    qDebug() << "WeiboPlugin: Starting API server (sync)...";

    // 1) 端口已经在监听：直接复用（可能是别的插件实例拉起的），不再重复启动
    if (WeiboNetwork::probeServer(kServerPort)) {
        qDebug() << "WeiboPlugin: API server already running, reuse it";
        return true;
    }

    // 2) 清理同路径残留进程（先 SIGTERM，500ms 后强杀仍存活的）
    const QString execPath = kPluginRoot + kServerExec;
    {
        QProcess pgrepProcess;
        pgrepProcess.start(QStringLiteral("pgrep"),
                           QStringList() << QStringLiteral("-f") << execPath);
        pgrepProcess.waitForFinished(2000);

        QVector<int> pidNums;
        if (pgrepProcess.exitCode() == 0) {
            const QString output =
                QString::fromLocal8Bit(pgrepProcess.readAllStandardOutput()).trimmed();
            const QStringList pids = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &pid : pids) {
                bool ok = false;
                const int pidNum = pid.toInt(&ok);
                if (ok && pidNum > 0)
                    pidNums.append(pidNum);
            }
        }
        // pgrep 不可用（不存在 / 退出码非 0）时静默跳过残留清理，继续往下走

        for (int pidNum : pidNums) {
            qDebug() << "WeiboPlugin: Terminating existing API server PID:" << pidNum;
            ::kill(pidNum, SIGTERM);
        }
        if (!pidNums.isEmpty()) {
            QThread::msleep(500);
            for (int pidNum : pidNums) {
                if (::kill(pidNum, 0) == 0) {
                    qWarning() << "WeiboPlugin: Force killing PID:" << pidNum;
                    ::kill(pidNum, SIGKILL);
                }
            }
        }
    }

    // 3) go 端优雅关闭期间端口仍被占用，轮询等待释放（40 × 250ms = 10s）
    for (int i = 0; i < 40; ++i) {
        if (!WeiboNetwork::probeServer(kServerPort))
            break;
        QThread::msleep(250);
    }

    // 4) 可执行文件不存在就直接失败（不阻塞插件加载）
    if (!QFile::exists(execPath)) {
        qWarning() << "WeiboPlugin: API server executable not found:" << execPath;
        return false;
    }

    // 5) 确保可执行位（CI 产物解压后可能丢权限）
    QFile serverFile(execPath);
    if (!(serverFile.permissions() & QFile::ExeUser)) {
        serverFile.setPermissions(serverFile.permissions() | QFile::ExeUser |
                                  QFile::ExeGroup | QFile::ExeOther);
    }

    // 上次 bring-up 留下的死进程对象先丢掉，避免 QPointer 指向已结束的进程
    if (s_apiServerProcess) {
        s_apiServerProcess->disconnect();
        s_apiServerProcess->deleteLater();
        s_apiServerProcess = nullptr;
    }

    // 6) 启动子进程，并把插件目录 / 端口透传给它
    s_apiServerProcess = new QProcess();
    s_apiServerProcess->setWorkingDirectory(kPluginRoot);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("WEIBO_PLUGIN_DIR"), kPluginRoot);
    env.insert(QStringLiteral("PORT"), QString::number(static_cast<uint>(kServerPort)));
    s_apiServerProcess->setProcessEnvironment(env);

    QObject::connect(s_apiServerProcess, &QProcess::readyReadStandardOutput, []() {
        if (s_apiServerProcess) {
            qDebug() << "WeiboPlugin: API Server:"
                     << s_apiServerProcess->readAllStandardOutput().constData();
        }
    });
    QObject::connect(s_apiServerProcess, &QProcess::readyReadStandardError, []() {
        if (s_apiServerProcess) {
            qWarning() << "WeiboPlugin: API Server Error:"
                       << s_apiServerProcess->readAllStandardError().constData();
        }
    });
    QObject::connect(
        s_apiServerProcess,
        QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        [](int exitCode, QProcess::ExitStatus exitStatus) {
            qWarning() << "WeiboPlugin: API server exited with code" << exitCode
                       << ", status:"
                       << (exitStatus == QProcess::NormalExit ? "normal" : "crashed");
        });

    s_apiServerProcess->start(execPath, QStringList());
    if (!s_apiServerProcess->waitForStarted(5000)) {
        qWarning() << "WeiboPlugin: Failed to start API server:"
                   << s_apiServerProcess->errorString();
        delete s_apiServerProcess;
        s_apiServerProcess = nullptr;
        return false;
    }

    // 7) 端口探测：go 端先 Listen 再 Init，通常很快可连。
    //    探测超时不等于启动失败（子进程还活着就保留，请求侧有重试）。
    bool ready = false;
    for (int i = 0; i < 50; ++i) {  // 50 × 100ms = 5s
        if (WeiboNetwork::probeServer(kServerPort)) {
            ready = true;
            break;
        }
        if (s_apiServerProcess->state() != QProcess::Running) {
            qWarning() << "WeiboPlugin: API server exited during bring-up";
            delete s_apiServerProcess;
            s_apiServerProcess = nullptr;
            return false;
        }
        QThread::msleep(100);
    }

    if (!ready) {
        qWarning() << "WeiboPlugin: API server started but port" << kServerPort
                   << "not ready yet; keeping process, requests may retry";
    } else {
        qDebug() << "WeiboPlugin: API server started successfully, PID:"
                 << s_apiServerProcess->processId();
    }

    return s_apiServerProcess && s_apiServerProcess->state() == QProcess::Running;
}

void weibo_stopApiServer() {
    qDebug() << "WeiboPlugin: Stopping API server...";

    if (!s_apiServerProcess) {
        // 当前插件没有持有 server 进程（可能是复用了已有服务），不要误杀
        qDebug() << "WeiboPlugin: No owned API server process, skip stop";
        return;
    }

    // 只停本插件拉起的进程，绝不 pgrep 全局清理（会杀掉可复用的服务）
    QProcess *proc = s_apiServerProcess;
    s_apiServerProcess = nullptr;  // 先置空：后续回调不会再访问这个对象

    proc->disconnect();
    if (proc->state() != QProcess::NotRunning) {
        proc->terminate();
        if (!proc->waitForFinished(3000)) {
            qWarning() << "WeiboPlugin: API server did not exit, killing it";
            proc->kill();
            proc->waitForFinished(1000);
        }
    }
    delete proc;

    qDebug() << "WeiboPlugin: API server stopped";
}

bool weibo_restartApiServerSync() {
    weibo_stopApiServer();
    return weibo_startApiServerSync();
}

// =============================================================================
// PenMods 插件入口
// =============================================================================

#if defined(__GNUC__)
#define WEIBO_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define WEIBO_PLUGIN_EXPORT
#endif

extern "C" {

WEIBO_PLUGIN_EXPORT void init_plugin() {
    qDebug() << "WeiboPlugin: Initializing...";

    qmlRegisterType<WeiboController>("WeiboPlugin", 1, 0, "WeiboController");
    qmlRegisterType<BlogListModel>("WeiboPlugin", 1, 0, "BlogListModel");
    qmlRegisterType<CommentListModel>("WeiboPlugin", 1, 0, "CommentListModel");
    qmlRegisterType<CommentReplyListModel>("WeiboPlugin", 1, 0,
                                          "CommentReplyListModel");
    qmlRegisterType<HotSearchModel>("WeiboPlugin", 1, 0, "HotSearchModel");
    qmlRegisterType<UserListModel>("WeiboPlugin", 1, 0, "UserListModel");
    qmlRegisterType<TopicListModel>("WeiboPlugin", 1, 0, "TopicListModel");
    qmlRegisterType<PictureListModel>("WeiboPlugin", 1, 0, "PictureListModel");
    qmlRegisterType<SearchHistoryModel>("WeiboPlugin", 1, 0, "SearchHistoryModel");

    // 同步拉起 Go sidecar（见 weibo_startApiServerSync 注释）。
    // 这里绝对不能碰 WeiboNetwork::instance()：
    //   1) m_apiServerReady 默认 true，同步启动返回时端口通常已可连，init 阶段
    //      无需关闸；
    //   2) 宿主常在无事件循环的加载线程里调 init_plugin——若在这里首次
    //      instance()，会把 QNAM 单例钉在错误线程，之后 GUI 线程发出的请求
    //      收不到 finished（表现同样是「一个请求都不发」）。
    // WeiboNetwork 的首次触碰放在 attach_engine()（GUI 线程）。
    if (!weibo_startApiServerSync()) {
        qWarning() << "WeiboPlugin: Warning - API server failed to start";
    }

    qDebug() << "WeiboPlugin: Registered successfully!";
}

WEIBO_PLUGIN_EXPORT void attach_engine(QQmlEngine *engine) {
    QMutexLocker locker(&s_providerMutex);

    s_engine = engine;

    if (!s_engine) {
        qWarning() << "WeiboPlugin: attach_engine 收到空引擎，忽略";
        return;
    }

    engine->addImportPath(QStringLiteral("/userdisk/PenMods/plugins/weibo_plugin/qml"));
    engine->addImportPath(QStringLiteral("/userdisk/PenMods/plugins/weibo_plugin/"));

    // 在引擎 / GUI 线程首次触及 WeiboNetwork，保证 QNAM 线程亲和正确
    WeiboNetwork *network = WeiboNetwork::instance();
    // 插件热重载后若上次 destroy 关过闸又丢了回调，这里兜底放行
    network->setApiServerReady(true);

    // 所有权交给 QQmlEngine（destroy 时引擎会删掉它，这里只保留裸引用）
    s_imageProvider = new WeiboImageProvider(network);
    s_engine->addImageProvider(QStringLiteral("weibo"), s_imageProvider);

    qDebug() << "WeiboPlugin: Engine attached, ImageProvider registered";
}

WEIBO_PLUGIN_EXPORT void destroy_plugin() {
    qDebug() << "WeiboPlugin: Destroying...";

    // 先停止本插件拉起的 API 服务器
    weibo_stopApiServer();

    {
        QMutexLocker locker(&s_providerMutex);
        // ImageProvider 归 QQmlEngine 所有，这里只解除引用
        s_imageProvider = nullptr;
    }

    // 恢复 ready 标记，让后续请求快速失败而不是继续排队
    WeiboNetwork *network = WeiboNetwork::instance();
    if (network) {
        network->cancelAllRequests();
        network->setApiServerReady(true);
    }

    s_engine = nullptr;

    qDebug() << "WeiboPlugin: Destroyed successfully";
}

}  // extern "C"
