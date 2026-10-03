#include "modules/publish/WeiboPublishModule.h"

#include "WeiboController.h"
#include "WeiboJsonUtils.h"
#include "WeiboNetwork.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace {

// 插件目录：环境变量 > 设备固定路径 > 桌面调试用的 AppDataLocation。
QString pluginDir() {
    const QByteArray env = qgetenv("WEIBO_PLUGIN_DIR");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);

    const QString devicePath = QStringLiteral("/userdisk/PenMods/plugins/weibo_plugin");
    if (QDir(devicePath).exists())
        return devicePath;

    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString draftFilePath() {
    return QDir(pluginDir()).filePath(QStringLiteral("publish_draft.txt"));
}

}  // namespace

WeiboPublishModule::WeiboPublishModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 发布微博 ======

void WeiboPublishModule::publish(const QString &content, int visible, const QStringList &picIds) {
    if (!m_controller)
        return;

    const QString text = content.trimmed();
    // 不传 picIds 时沿用 uploadPicture 累积的图片。
    const QStringList images = picIds.isEmpty() ? m_picIds : picIds;

    if (text.isEmpty() && images.isEmpty()) {
        emit publishFailed(QStringLiteral("内容不能为空"));
        emit m_controller->toastMessage(QStringLiteral("内容不能为空"));
        return;
    }

    if (m_busy)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_busy = true;
    m_controller->setPublishState(true, 0.15, QStringLiteral("正在发布…"));

    QJsonArray picArray;
    for (const QString &picId : images) {
        if (!picId.trimmed().isEmpty())
            picArray.append(picId.trimmed());
    }

    QJsonObject body;
    body.insert(QStringLiteral("content"), text);
    body.insert(QStringLiteral("visible"), qBound(0, visible, 6));
    body.insert(QStringLiteral("pic_ids"), picArray);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/publish"), body,
        [this, guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            m_picIds.clear();  // 图片已被这条微博消费掉

            self->setPublishState(false, 1.0, QStringLiteral("发布成功"));
            const QString id = WeiboJson::str(data, "id");
            emit publishSucceeded(id);
            emit self->toastMessage(QStringLiteral("发布成功"));
            clearDraft();
        },
        [this, guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();

            self->setPublishState(false, 0.0, msg);
            emit publishFailed(msg);
            emit self->toastMessage(QStringLiteral("发布失败：%1").arg(msg));
        });
}

void WeiboPublishModule::uploadPicture(const QString &base64Data, const QString &filename) {
    if (!m_controller)
        return;

    const QString data = base64Data.trimmed();
    if (data.isEmpty()) {
        emit m_controller->toastMessage(QStringLiteral("图片数据为空"));
        return;
    }

    if (m_busy)
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    m_busy = true;
    m_controller->setPublishState(true, 0.4, QStringLiteral("正在上传图片…"));

    QJsonObject body;
    body.insert(QStringLiteral("data"), data);
    body.insert(QStringLiteral("filename"), filename);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/upload_pic"), body,
        [this, guard](const QJsonObject &data) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            const QString picId = WeiboJson::str(data, "pic_id");
            if (!picId.isEmpty())
                m_picIds.append(picId);

            self->setPublishState(false, 1.0, QStringLiteral("图片已上传"));
            emit pictureUploaded(picId);
        },
        [this, guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;

            m_busy = false;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();

            self->setPublishState(false, 0.0, msg);
            emit self->toastMessage(QStringLiteral("图片上传失败：%1").arg(msg));
        });
}

void WeiboPublishModule::deleteStatus(const QString &id) {
    if (!m_controller)
        return;

    const QString statusId = id.trimmed();
    if (statusId.isEmpty())
        return;

    WeiboNetwork *network = m_controller->network();
    if (!network)
        return;

    QJsonObject body;
    body.insert(QStringLiteral("id"), statusId);

    QPointer<WeiboController> guard(m_controller);
    network->post(
        QStringLiteral("/status/delete"), body,
        [guard](const QJsonObject &data) {
            Q_UNUSED(data)
            WeiboController *self = guard.data();
            if (!self)
                return;
            emit self->toastMessage(QStringLiteral("已删除"));
        },
        [guard](int code, const QString &msg) {
            WeiboController *self = guard.data();
            if (!self)
                return;
            if (code == -100 || code == -101 || code == 401 || code == -401)
                self->clearLocalLoginState();
            emit self->toastMessage(QStringLiteral("删除失败：%1").arg(msg));
        });
}

// ====== 草稿 ======

void WeiboPublishModule::saveDraft(const QString &content) {
    const QString path = draftFilePath();

    QDir dir = QFileInfo(path).absoluteDir();
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return;  // 落盘失败只能放弃，不能影响发布流程

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    file.write(content.toUtf8());
    file.close();
}

QString WeiboPublishModule::loadDraft() {
    QFile file(draftFilePath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    const QString content = QString::fromUtf8(file.readAll());
    file.close();
    return content;
}

void WeiboPublishModule::clearDraft() {
    QFile::remove(draftFilePath());
    // 草稿文件没了，控制器上的 publishDraft 也必须一起清空，否则 QML 仍显示旧文案。
    if (m_controller)
        m_controller->setPublishDraft(QString());
}

void WeiboPublishModule::setDraft(const QString &content) {
    if (m_controller)
        m_controller->setPublishDraft(content);
    saveDraft(content);
}
