#include "modules/viewer/WeiboViewerModule.h"

#include "WeiboController.h"
#include "WeiboModels.h"

#include <QByteArray>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
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

QString externalPlayerPath() {
    return QDir(pluginDir()).filePath(QStringLiteral("playvideo"));
}

}  // namespace

WeiboViewerModule::WeiboViewerModule(WeiboController *controller)
    : QObject(controller), m_controller(controller) {}

// ====== 图片查看器 ======

void WeiboViewerModule::openImages(const QVariantList &pics, int index) {
    if (!m_controller || pics.isEmpty())
        return;

    // 越界索引一律夹到合法区间。
    const int clamped = qBound(0, index, pics.size() - 1);

    QVector<WeiboPicture> pictures;
    pictures.reserve(pics.size());
    for (const QVariant &value : pics) {
        const QVariantMap map = value.toMap();
        WeiboPicture picture;
        picture.url = map.value(QStringLiteral("url")).toString();
        picture.large = map.value(QStringLiteral("large")).toString();
        picture.width = map.value(QStringLiteral("width")).toInt();
        picture.height = map.value(QStringLiteral("height")).toInt();
        pictures.append(picture);
    }

    // 图片数据交给控制器持有的模型，QML 的查看页直接从模型取图。
    if (PictureListModel *model = m_controller->pictureModel())
        model->setItems(pictures);

    emit imagesRequested(pics, clamped);
    emit m_controller->imageRequested(pics, clamped);
}

// ====== 视频 ======

void WeiboViewerModule::openVideo(const QString &url) {
    if (!m_controller)
        return;

    const QString target = url.trimmed();
    if (target.isEmpty())
        return;

    const QString player = externalPlayerPath();
    const QFileInfo info(player);
    if (info.exists() && info.isExecutable()) {
        // 宿主的 playvideo 只接受一个 URL 参数。
        QProcess::startDetached(player, QStringList{target});
    } else {
        // 回落到系统打开（PenMods 会拦截并转交给宿主播放器）。
        QDesktopServices::openUrl(QUrl(target));
    }

    emit videoRequested(target);
}

// ====== 外部链接 ======

void WeiboViewerModule::openUrl(const QString &url) {
    if (!m_controller)
        return;

    const QUrl parsed(url.trimmed());
    const QString scheme = parsed.scheme().toLower();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        emit m_controller->toastMessage(QStringLiteral("不支持的链接"));
        return;
    }

    QDesktopServices::openUrl(parsed);
    emit urlRequested(parsed.toString());
}

bool WeiboViewerModule::externalPlayerAvailable() const {
    return QFile::exists(externalPlayerPath());
}
