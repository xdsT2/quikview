#include "ImageCache.h"

#include <QFutureWatcher>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QtConcurrent/QtConcurrent>

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
    m_fullCache.setMaxCost(512 * 1024 * 1024);   // 原图缓存上限 ~512MB
    m_thumbCache.setMaxCost(64 * 1024 * 1024);   // 缩略图缓存上限 ~64MB
}

QImage ImageCache::cachedFull(const QString &path) const
{
    return m_fullCache.contains(path) ? *m_fullCache.object(path) : QImage();
}

void ImageCache::requestFull(const QString &path)
{
    if (m_fullInFlight.contains(path)) {
        return;
    }
    if (m_fullCache.contains(path)) {
        emit fullReady(path, *m_fullCache.object(path));
        return;
    }
    m_fullInFlight.insert(path);

    auto future = QtConcurrent::run([path]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        return reader.read();
    });

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, path, watcher]() {
        m_fullInFlight.remove(path);
        const QImage img = watcher->result();
        watcher->deleteLater();
        if (img.isNull()) {
            emit loadFailed(path, QStringLiteral("无法解码"));
            return;
        }
        m_fullCache.insert(path, new QImage(img));
        emit fullReady(path, img);
    });
    watcher->setFuture(future);
}

void ImageCache::requestThumbnail(const QString &path, const QSize &size)
{
    const QString key = path + QLatin1Char('@') + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    if (m_thumbInFlight.contains(key)) {
        return;
    }
    if (m_thumbCache.contains(key)) {
        emit thumbnailReady(path, *m_thumbCache.object(key));
        return;
    }
    m_thumbInFlight.insert(key);

    auto future = QtConcurrent::run([path, size]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        // 先按 KeepAspectRatio 算出真正要解码的尺寸，再 setScaledSize。
        // 直接 setScaledSize(方形) 会把图片硬拉到正方形——缩略图被拉伸就是这么来的。
        // EXIF 旋转 90/270 时宽高互换，判断错了同样会算歪。
        QSize src = reader.size();
        if (src.isValid() && src.width() > 0 && src.height() > 0) {
            const QImageIOHandler::Transformations t = reader.transformation();
            if (t.testFlag(QImageIOHandler::TransformationRotate90)
                || t.testFlag(QImageIOHandler::TransformationRotate270)) {
                src.transpose();
            }
            const QSize fit = src.scaled(size, Qt::KeepAspectRatio);
            if (fit.isValid() && fit.width() > 0 && fit.height() > 0) {
                reader.setScaledSize(fit);
            }
        }
        QImage img = reader.read();
        // 兜底：个别解码器不认 setScaledSize，读回来再缩一次（同样保持比例）
        if (!img.isNull() && (img.width() > size.width() || img.height() > size.height())) {
            img = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return img;
    });

    auto *watcher = new QFutureWatcher<QImage>(this);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, path, key, watcher]() {
        m_thumbInFlight.remove(key);
        const QImage img = watcher->result();
        watcher->deleteLater();
        if (!img.isNull()) {
            m_thumbCache.insert(key, new QImage(img));
            emit thumbnailReady(path, img);
        }
    });
    watcher->setFuture(future);
}

void ImageCache::clear()
{
    m_fullCache.clear();
    m_thumbCache.clear();
}
