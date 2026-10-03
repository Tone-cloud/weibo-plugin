#include "modules/media/WeiboMediaModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboModels.h"
#include "WeiboNetwork.h"
#include "modules/viewer/WeiboViewerModule.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QPointer>
#include <QString>
#include <QVector>
#include <QtGlobal>

WeiboMediaModule::WeiboMediaModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 解析 ======

void WeiboMediaModule::prepare(const QString &id) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_pendingId = statusId;
    m_controller->setMediaLoading(true);
    m_controller->setMediaStatus(QStringLiteral("正在解析…"));

    QMap<QString, QString> params;
    params.insert(QStringLiteral("id"), statusId);

    QPointer<WeiboController> guard(m_controller);
    network->get(
        QStringLiteral("/media/info"), params,
        [guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            const QString direct = WeiboJson::str(data, "url");

            WeiboPageInfo page;
            QString type = WeiboJson::str(data, "type");
            page.type = type.isEmpty() ? QStringLiteral("video") : type;
            page.title = WeiboJson::str(data, "title");
            page.cover = WeiboJson::str(data, "cover");
            page.url = direct;
            page.mediaUrl = direct;
            page.duration = static_cast<int>(WeiboJson::num(data, "duration", 0));
            page.liveStatus = static_cast<int>(WeiboJson::num(data, "live_status", 0));
            self->setMediaInfo(page);

            QVector<WeiboMediaQuality> qualities;
            const QJsonArray array = WeiboJson::arr(data, "qualities");
            for (const QJsonValue &value : array) {
                if (!value.isObject())
                    continue;
                const QJsonObject object = value.toObject();
                WeiboMediaQuality quality;
                quality.label = WeiboJson::str(object, "label");
                quality.url = WeiboJson::str(object, "url");
                if (quality.url.isEmpty())
                    continue;
                if (quality.label.isEmpty())
                    quality.label = QStringLiteral("默认");
                qualities.append(quality);
            }

            // data.url 是首选直链，qualities 里没有就补一条，保证列表至少有一项。
            if (!direct.isEmpty()) {
                bool exists = false;
                for (const WeiboMediaQuality &quality : qualities) {
                    if (quality.url == direct) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    WeiboMediaQuality quality;
                    quality.label = QStringLiteral("直链");
                    quality.url = direct;
                    qualities.prepend(quality);
                }
            }
            self->setMediaQualities(qualities);

            self->setMediaLoading(false);
            self->setMediaStatus(QString());
            emit resolved(page.type, direct);
            emit self->mediaReady(direct);
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            self->setMediaLoading(false);
            self->setMediaStatus(msg);
            emit self->toastMessage(QStringLiteral("媒体解析失败：%1").arg(msg));
        });
}

void WeiboMediaModule::prepareFromDetail() {
    if (!m_controller)
        return;

    const WeiboBlog &blog = m_controller->detail();
    const WeiboPageInfo page = blog.page;

    if (page.type.isEmpty() || page.type == QLatin1String("none")) {
        emit m_controller->toastMessage(QStringLiteral("该微博没有可播放的媒体"));
        return;
    }

    m_pendingId = blog.id;
    m_controller->setMediaInfo(page);
    m_controller->setMediaStatus(QString());

    // 详情里只有一条直链，构造单元素清晰度列表。
    QVector<WeiboMediaQuality> qualities;
    if (!page.mediaUrl.isEmpty()) {
        WeiboMediaQuality quality;
        quality.label = QStringLiteral("默认");
        quality.url = page.mediaUrl;
        qualities.append(quality);
    }
    m_controller->setMediaQualities(qualities);

    emit resolved(page.type, page.mediaUrl);
    emit m_controller->mediaReady(page.mediaUrl);
}

void WeiboMediaModule::clear() {
    if (m_controller)
        m_controller->clearMedia();
}

// ====== 播放 ======

void WeiboMediaModule::playUrl(const QString &url) {
    if (!m_controller)
        return;

    const QString target = url.trimmed();
    if (target.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("播放地址为空"));
        return;
    }

    emit resolved(QStringLiteral("video"), target);
    emit m_controller->mediaReady(target);

    // 交给 viewer 模块决定走宿主 playvideo 还是 QDesktopServices。
    if (WeiboViewerModule *viewer = m_controller->viewerModule())
        viewer->openVideo(target);
}
