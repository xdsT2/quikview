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
// 空闲预读：队列空下来后按 MainWindow 的 300ms 空闲计时器调 preload()，向当前页
// 前后各预读 1 张，只填缓存不上屏；预读中的那页若被真的要到（requestFull），
// 升级为「解完即上屏」，不会出现「先跳过去又回跳」。
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
    // 空闲预读：只往原图缓存里填，不发信号不上屏；解码队列/在飞/已预读过的
    // 路径直接忽略。真正的「前后各一张」调度在 MainWindow（300ms 无输入才调）。
    void preload(const QString &path);
    bool isIdle() const;    // 待解队列、在飞全图、预读缓冲都空 —— 可以开始预读
    // 拖动滑杆的跟手预览：缩放解码（previewSize 级），结果只进拖动预览缓存。
    // 与 FIFO 队列 / 预读三路互不干扰；同一时刻至多 1 个在飞，位置再变就
    // 丢掉旧结果改解最新位置（latest-wins 只在本通道成立）。
    void requestDragPreview(const QString &path, const QSize &size);

signals:
    void fullReady(const QString &path, const QImage &image, bool preview);
    void dragPreviewReady(const QString &path, const QImage &image);
    void thumbnailReady(const QString &path, const QImage &image);
    void loadFailed(const QString &path, const QString &error);

private:
    void startFullDecode(const QString &path, const QSize &previewSize);
    void finishFullDecode(const QString &path, const QImage &img);
    void startNext();
    void startNextPreload();
    void startDragDecode();
    void insertFull(const QString &path, const QImage &img);

    QCache<QString, QImage> m_fullCache;
    QCache<QString, QImage> m_thumbCache;
    QSet<QString> m_fullInFlight;
    QSet<QString> m_thumbInFlight;
    QList<QString> m_queue;     // 待解码队列（FIFO，去重，上限 8）
    bool m_dropInFlightResult = false;  // cancelPending 后在飞结果只进缓存不上屏
    QSize m_previewSize;        // 快帧目标尺寸（视口像素）
    // 预读：同一时刻也只跑 1 个（与全图解码共用「单飞」），排到的路径放这里。
    QList<QString> m_preloadQueue;
    QString m_preloadPath;
    bool m_preloadPromote = false;  // 预读期间该路径被真的要过：解完直接上屏
    // 拖动预览：独立小缓存（缩放图不能混进 m_fullCache，否则落点会停成模糊帧）
    QCache<QString, QImage> m_dragCache;
    QString m_dragInFlight;     // 正在解的拖动预览（至多 1 个）
    QString m_dragPending;      // 最新落点；解完若已被更新就只入缓存不上屏
    QSize m_dragSize;
    bool m_dragPromote = false; // 在飞预览的页被真要（连按/跳转）：解完按快帧上屏
    bool m_dragMuted = false;   // 有真实请求介入：在飞预览解完只入缓存不上屏
};
