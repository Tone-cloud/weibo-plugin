#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class WeiboController;

// 发微博：文字 + 图片（base64 上传）。草稿本地持久化。
class WeiboPublishModule : public QObject {
    Q_OBJECT
public:
    explicit WeiboPublishModule(WeiboController *controller);

    // visible: 0 公开 / 1 仅自己可见 / 6 好友圈
    Q_INVOKABLE void publish(const QString &content, int visible = 0,
                             const QStringList &picIds = QStringList());
    Q_INVOKABLE void uploadPicture(const QString &base64Data,
                                   const QString &filename = QString());
    Q_INVOKABLE void deleteStatus(const QString &id);

    Q_INVOKABLE void saveDraft(const QString &content);
    Q_INVOKABLE void clearDraft();
    Q_INVOKABLE QString loadDraft();
    Q_INVOKABLE void setDraft(const QString &content);

signals:
    void publishSucceeded(const QString &id);
    void publishFailed(const QString &message);
    void pictureUploaded(const QString &picId);

private:
    WeiboController *m_controller = nullptr;
    QStringList m_picIds;
    bool m_busy = false;
};
