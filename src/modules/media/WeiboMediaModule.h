#pragma once

#include <QObject>
#include <QString>

class WeiboController;

// 视频 / 直播：把微博 page_info 解析成可直接播放的直链与清晰度列表。
// 实际播放交给宿主的系统播放器（WeiboViewerModule / mediaReady 信号）。
class WeiboMediaModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboMediaModule(WeiboController *controller);

    // 解析单条微博的媒体信息并填入 controller 的 media* 属性
    Q_INVOKABLE void prepare(const QString &id);
    // 直接用已知的 page_info（详情页已经有数据时省一次请求）
    Q_INVOKABLE void prepareFromDetail();
    Q_INVOKABLE void clear();

    Q_INVOKABLE void playUrl(const QString &url);

signals:
    void resolved(const QString &type, const QString &url);

private:
    WeiboController *m_controller = nullptr;
    QString m_pendingId;
};
