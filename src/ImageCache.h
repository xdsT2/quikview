#pragma once

#include <QCache>
#include <QObject>
#include <QSet>
#include <QSize>

class QImage;

// 异步解码 + 二级缓存（缩略图 / 原图）。
// 原图解码走 latest-wins 单飞：同一时刻最多 1 个全图解码在飞，连按/长按时
// 新请求只覆盖 m_pendingPath，在飞任务完成后按最新目标接着解——杜绝
// 「7 连按起 7 路并发解码、结果 6 份被过期守卫丢弃」的空转。
// 支持按比例解码的格式（JPEG/PNG 等）在全图之前先按视口像素出一张快帧
// 先上屏顶住体感，全图到了再替换（fullReady 的 preview 参数区分）。
class ImageCache : public QObject
{
    Q_OBJECT
public:
    explicit ImageCache(QObject *parent = nullptr);

    QImage cachedFull(const QString &path) const;
    void requestFull(const QString &path, const QSize &previewSize = QSize());
    void requestThumbnail(const QString &path, const QSize &size);
    void clear();
    void clearFull();   // 只清原图缓存（缩略图不受 HDR 开关影响）

signals:
    void fullReady(const QString &path, const QImage &image, bool preview);
    void thumbnailReady(const QString &path, const QImage &image);
    void loadFailed(const QString &path, const QString &error);

private:
    void startFullDecode(const QString &path, const QSize &previewSize);
    void finishFullDecode(const QString &path, const QImage &img);

    QCache<QString, QImage> m_fullCache;
    QCache<QString, QImage> m_thumbCache;
    QSet<QString> m_fullInFlight;
    QSet<QString> m_thumbInFlight;
    QString m_currentPath;    // 最新一次请求的路径（== 完成后是否上屏的判据之一）
    QString m_pendingPath;    // 单飞占用时最新想要的那张；在飞完成后立即接解
    QSize m_previewSize;      // 快帧目标尺寸（视口像素），随 pending 一起传递
};
