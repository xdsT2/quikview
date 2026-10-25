#pragma once

#include <QCache>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

class QImage;

// 异步解码 + 二级缓存（缩略图 / 原图）。
// 原图解码走 FIFO 单飞队列：同一时刻最多 1 个全图解码在飞，连按/长按把路径
// 依次入队，解完一张立刻按入队顺序解下一张并逐张上屏——像播放连续帧，
// 而不是「只出首尾两张」。emit 顺序 == requestFull 调用顺序（缓存命中也排队
// 保证次序）。队列上限 8，超出丢最旧的未开始项，最终停在最新那张。
// 显式跳转（打开/换目录/滑块/缩略图/书签）先 cancelPending() 清队。
// 支持按比例解码的格式（JPEG/PNG 等）在全图之前先按视口像素出一张快帧
// 先上屏，全图到了再替换（fullReady 的 preview 参数区分）。
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
    void cancelPending();   // 显式跳转：清空待解队列，在飞结果静默入缓存不上屏

signals:
    void fullReady(const QString &path, const QImage &image, bool preview);
    void thumbnailReady(const QString &path, const QImage &image);
    void loadFailed(const QString &path, const QString &error);

private:
    void startFullDecode(const QString &path, const QSize &previewSize);
    void finishFullDecode(const QString &path, const QImage &img);
    void startNext();

    QCache<QString, QImage> m_fullCache;
    QCache<QString, QImage> m_thumbCache;
    QSet<QString> m_fullInFlight;
    QSet<QString> m_thumbInFlight;
    QList<QString> m_queue;     // 待解码队列（FIFO，去重，上限 8）
    bool m_dropInFlightResult = false;  // cancelPending 后在飞结果只进缓存不上屏
    QSize m_previewSize;        // 快帧目标尺寸（视口像素）
};
