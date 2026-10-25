#include "ImageCache.h"

#include <QFutureWatcher>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QtConcurrent/QtConcurrent>

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
    m_fullCache.setMaxCost(256 * 1024 * 1024);   // 原图缓存上限 256MB（按字节成本淘汰）
    m_thumbCache.setMaxCost(64 * 1024 * 1024);   // 缩略图缓存上限 ~64MB
}

QImage ImageCache::cachedFull(const QString &path) const
{
    return m_fullCache.contains(path) ? *m_fullCache.object(path) : QImage();
}

void ImageCache::requestFull(const QString &path, const QSize &previewSize)
{
    m_currentPath = path;        // 完成时只把「最新想要的那张」上屏
    m_previewSize = previewSize;
    if (m_fullInFlight.contains(path)) {
        return;
    }
    if (m_fullCache.contains(path)) {
        emit fullReady(path, *m_fullCache.object(path), false);
        return;
    }
    if (!m_fullInFlight.isEmpty()) {
        // latest-wins 单飞：已有全图解码在飞时只覆盖待解目标，不再叠加并发任务。
        // 7 连按 = 1 路解码在飞 + 1 个待解目标，而不是 7 路并发互相抢核。
        m_pendingPath = path;
        return;
    }
    startFullDecode(path, previewSize);
}

void ImageCache::startFullDecode(const QString &path, const QSize &previewSize)
{
    m_fullInFlight.insert(path);
    QtConcurrent::run([this, path, previewSize]() {
        // 快帧先上屏：JPEG 走 DCT 缩放（12MP 约 10-20ms）、PNG 等同样支持
        // 按比例解码，先按视口像素出一张顶住连按体感，全图随后替换。
        // HEIC/RAW 的 QImageReader 读不出 size()，自然跳过快帧，仍走各自 Loader。
        if (previewSize.isValid()) {
            QImageReader fast(path);
            fast.setAutoTransform(true);
            QSize src = fast.size();
            if (qEnvironmentVariableIsSet("QUIKVIEW_PERF")) {
                qWarning("PERF fast %s: src=%dx%d supportsScaled=%d", path.toUtf8().constData(),
                         src.width(), src.height(),
                         int(fast.supportsOption(QImageIOHandler::ScaledSize)));
            }
            if (src.isValid() && src.width() > 0 && src.height() > 0
                && fast.supportsOption(QImageIOHandler::ScaledSize)) {
                // EXIF 旋转 90/270 时 size() 是转之前的，先转置再按视口适配
                const QImageIOHandler::Transformations t = fast.transformation();
                if (t.testFlag(QImageIOHandler::TransformationRotate90)
                    || t.testFlag(QImageIOHandler::TransformationRotate270)) {
                    src.transpose();
                }
                const QSize scaled = src.scaled(previewSize, Qt::KeepAspectRatio);
                if (scaled.isValid() && scaled.width() > 0 && scaled.height() > 0
                    && scaled != src) {
                    fast.setScaledSize(scaled);
                    const QImage prev = fast.read();
                    // 个别插件谎报支持 ScaledSize：读回尺寸不符就当没有快帧
                    if (qEnvironmentVariableIsSet("QUIKVIEW_PERF")) {
                        qWarning("PERF fast %s: want=%dx%d got=%dx%d", path.toUtf8().constData(),
                                 scaled.width(), scaled.height(), prev.width(), prev.height());
                    }
                    if (!prev.isNull() && prev.size() == scaled) {
                        emit fullReady(path, prev, true);
                    }
                }
            }
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QImage img = reader.read();
        QMetaObject::invokeMethod(this, [this, path, img]() {
            finishFullDecode(path, img);
        }, Qt::QueuedConnection);
    });
}

void ImageCache::finishFullDecode(const QString &path, const QImage &img)
{
    // clearFull() 会在开关 HDR 时清空在飞集合：被清任务的结果到这里静默丢弃
    if (!m_fullInFlight.contains(path)) {
        return;
    }
    m_fullInFlight.remove(path);
    if (img.isNull()) {
        emit loadFailed(path, QStringLiteral("无法解码"));
    } else {
        // 缓存成本按字节计，QCache 才会真的按容量 LRU 淘汰
        // （此前 insert 不传成本=每条记 1，512MB 上限形同虚设）
        m_fullCache.insert(path, new QImage(img),
                           static_cast<int>(qMin<qint64>(img.sizeInBytes(), qint64(INT_MAX))));
        if (path == m_currentPath || path == m_pendingPath) {
            emit fullReady(path, img, false);
        }
        // 否则静默丢弃：连按被顶掉的图只进缓存，不上屏
    }
    if (path == m_pendingPath) {
        m_pendingPath.clear();
    }
    // 接着解最新想要的那张（事件驱动，无定时器等待）
    if (!m_pendingPath.isEmpty() && m_fullInFlight.isEmpty()) {
        const QString next = m_pendingPath;
        m_pendingPath.clear();
        if (m_fullCache.contains(next)) {
            if (next == m_currentPath) {
                emit fullReady(next, *m_fullCache.object(next), false);
            }
        } else {
            startFullDecode(next, m_previewSize);
        }
    }
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

void ImageCache::clearFull()
{
    m_fullInFlight.clear();
    m_fullCache.clear();
    m_pendingPath.clear();
}
