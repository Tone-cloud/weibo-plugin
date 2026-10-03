#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class WeiboController;

// 查看器桥：把图片 / 视频路径交给宿主（PenMods）或插件内的查看页。
//
// 与 PenMods 的约定：
//   * 图片：插件内 ImageViewerPage 自行展示，本模块只做「准备数据 + 通知」。
//   * 视频：优先 <plugin_dir>/playvideo（若存在），否则回落到 QDesktopServices，
//     宿主会拦截。
class WeiboViewerModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboViewerModule(WeiboController *controller);

    // 打开图片查看器：pics 为 [{url, large, width, height}, ...]
    Q_INVOKABLE void openImages(const QVariantList &pics, int index = 0);
    // 交给宿主播放器播放网络 URL
    Q_INVOKABLE void openVideo(const QString &url);
    // 打开外部链接（@用户 / 话题 / 原文链接）
    Q_INVOKABLE void openUrl(const QString &url);

    Q_INVOKABLE bool externalPlayerAvailable() const;

signals:
    void imagesRequested(const QVariantList &pics, int index);
    void videoRequested(const QString &url);
    void urlRequested(const QString &url);

private:
    WeiboController *m_controller = nullptr;
};
