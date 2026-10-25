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
    m_previewSize = previewSize;
    if (m_fullInFlight.contains(path)) {
        return;   // 正在解
    }
    if (m_queue.contains(path)) {
        return;   // 已在队列
    }
    const bool busy = !m_fullInFlight.isEmpty() || !m_queue.isEmpty();
    if (m_fullCache.contains(path) && !busy) {
        // 空闲时缓存命中直接上屏；忙时也必须排队，否则会插队破坏
        // 「emit 顺序 == requestFull 调用顺序」
        emit fullReady(path, *m_fullCache.object(path), false);
        return;
    }
    m_queue.append(path);
    // 队列上限 8：超出丢最旧的「尚未开始解码」项（在飞队首不动），
    // 最新请求永远在队尾，最终必停在最新那张
    while (m_queue.size() > 8) {
        bool dropped = false;
        for (int i = 0; i < m_queue.size(); ++i) {
            if (!m_fullInFlight.contains(m_queue.at(i))) {
                m_queue.removeAt(i);
                dropped = true;
                break;
            }
        }
        if (!dropped) {
            break;   // 理论到不了：在飞至多 1 个 < 队列上限
        }
    }
    if (m_fullInFlight.isEmpty()) {
        startNext();
    }
}

void ImageCache::startNext()
{
    while (!m_queue.isEmpty() && m_fullInFlight.isEmpty()) {
        const QString path = m_queue.takeFirst();
        if (m_fullCache.contains(path)) {
            // 排队期间被缓存命中（如先前的解码完成入缓存）：按序直接上屏
            emit fullReady(path, *m_fullCache.object(path), false);
            continue;
        }
        startFullDecode(path, m_previewSize);
        return;
    }
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
    const bool drop = m_dropInFlightResult;
    m_dropInFlightResult = false;
    if (img.isNull()) {
        emit loadFailed(path, QStringLiteral("无法解码"));
    } else {
        // 缓存成本按字节计，QCache 才会真的按容量 LRU 淘汰
        // （此前 insert 不传成本=每条记 1，512MB 上限形同虚设）
        m_fullCache.insert(path, new QImage(img),
                           static_cast<int>(qMin<qint64>(img.sizeInBytes(), qint64(INT_MAX))));
        if (!drop) {
            // FIFO：按入队顺序无条件上屏（显式跳转的在飞旧结果除外）
            emit fullReady(path, img, false);
        }
    }
    startNext();
}

void ImageCache::cancelPending()
{
    m_queue.clear();
    // 在飞的那个拦不住了（QtConcurrent 不可取消），结果到达时只进缓存不上屏
    m_dropInFlightResult = !m_fullInFlight.isEmpty();
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
    m_queue.clear();
    m_dropInFlightResult = false;
}
