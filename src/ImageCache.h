#pragma once

#include <QCache>
#include <QObject>
#include <QSet>

class QImage;

// 异步解码 + 二级缓存（缩略图 / 原图）。
// 解码在 QtConcurrent 线程池执行，结果通过信号回主线程。
class ImageCache : public QObject
{
    Q_OBJECT
public:
    explicit ImageCache(QObject *parent = nullptr);

    QImage cachedFull(const QString &path) const;
    void requestFull(const QString &path);
    void requestThumbnail(const QString &path, const QSize &size);
    void clear();

signals:
    void fullReady(const QString &path, const QImage &image);
    void thumbnailReady(const QString &path, const QImage &image);
    void loadFailed(const QString &path, const QString &error);

private:
    QCache<QString, QImage> m_fullCache;
    QCache<QString, QImage> m_thumbCache;
    QSet<QString> m_fullInFlight;
    QSet<QString> m_thumbInFlight;
};
