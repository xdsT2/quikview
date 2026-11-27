#pragma once

#include <QCache>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QThreadPool>

#include <atomic>

#include "LoadError.h"

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
    ~ImageCache() override;

    QImage cachedFull(const QString &path) const;
    // preempt=true：显式跳转（打开/换目录/滑杆落点/缩略图/书签/首尾/快速反向）。
    // 进入新代次并允许在旧任务仍在解码时立刻启动新目标，旧结果只入缓存不上屏。
    // preempt=false：顺序翻页，保持 FIFO 单飞、逐张按序上屏（连按体感不变）。
    void requestFull(const QString &path, const QSize &previewSize = QSize(),
                     bool preempt = false);
    void requestThumbnail(const QString &path, const QSize &size);
    // 外部刷新（同名文件被删后又保存回来）：丢弃该路径的全图/拖动/缩略图缓存与
    // 排队项，并作废此前的在飞结果，下一次请求从磁盘重新解码。
    void forgetPath(const QString &path);
    void clear();
    void clearFull();   // 只清原图缓存（缩略图不受 HDR 开关影响）
    // D1-4 切目录回收：移除缓存中属于指定目录（磁盘路径前缀）的全图条目，并整路
    // 清空拖动预览缓存；缩略图缓存保留（按路径隔离、体积小，跨目录回翻仍可命中）。
    void evictDirectory(const QString &dirPath);
    void cancelPending();   // 显式跳转：清空待解队列，在飞结果静默入缓存不上屏
    // 空闲预读：只往原图缓存里填，不发信号不上屏；解码队列/在飞/已预读过的
    // 路径直接忽略。真正的「前后各一张」调度在 MainWindow（300ms 无输入才调）。
    void preload(const QString &path);
    bool isIdle() const;    // 待解队列、在飞全图、预读缓冲都空 —— 可以开始预读
    // 指定路径是否还有未完成的全图任务（待解队列 / 在飞 / 预读升级中）。
    // 连续翻页加载反馈用：等待提示只在目标仍 pending 时展示；旧任务失败的
    // loadFailed 若路径已被重新请求（新任务在路上），用同一查询判定不覆盖。
    bool isFullPending(const QString &path) const;
    // 拖动滑杆的跟手预览：缩放解码（previewSize 级），结果只进拖动预览缓存。
    // 与 FIFO 队列 / 预读三路互不干扰；同一时刻至多 1 个在飞，位置再变就
    // 丢掉旧结果改解最新位置（latest-wins 只在本通道成立）。
    void requestDragPreview(const QString &path, const QSize &size);
    // 原图缓存上限（MB）：设置页「缓存大小」在保存时调进来，立即改 m_fullCache
    // 的淘汰水位，不用重启；构造时也读配置设初值（见 ImageCache.cpp）。
    void setFullCacheLimit(int mb);
    // 运行态读数（批次04 验收/设置页状态行共用）：命中上限、当前已用字节、
    // 命中/未命中计数、预读实际启动次数。都读真实对象，不读配置。
    int fullCacheLimitMb() const { return m_limitMb; }
    qint64 fullCacheBytes() const { return m_fullCache.totalCost(); }
    int cacheHits() const { return m_hits; }
    int cacheMisses() const { return m_misses; }
    int preloadStarts() const { return m_preloadStarts; }
    // 「先显示低品质图片以加速渲染」（键 imageproc/preview_mode）的会话内覆盖：
    // 设置页切换时先下发覆盖（known=true），关闭设置页后 known=false 回到
    // 「一律读配置」。原子量给 worker 线程只读，不上锁队列。
    void setPreviewModeLive(bool known, bool on);

signals:
    void fullReady(const QString &path, const QImage &image, bool preview);
    void dragPreviewReady(const QString &path, const QImage &image);
    // A null image reports a failed preview decode.
    void thumbnailReady(const QString &path, const QImage &image);
    // category 由 loader 层判定（PSD 分支取 PsdDocument::errorCategory，其余按魔数
    // 白名单判「损坏 / 不支持」）；UI 只按它取文案，不匹配 error 字符串。
    void loadFailed(const QString &path, const QString &error, LoadErrorCategory category);

private:
    void startFullDecode(const QString &path, const QSize &previewSize, quint64 generation);
    void finishFullDecode(const QString &path, const QImage &img,
                             quint64 generation, const QString &error = QString(),
                             LoadErrorCategory category = LoadErrorCategory::None);
    void startNext();
    void startNextPreload();
    void startDragDecode();
    void insertFull(const QString &path, const QImage &img);
    // 是否有「当前代次」尚未完成的工作（全图在飞 / 预读在飞）。
    // 只有它才会阻止下一次启动：旧代次的任务已被判定为过期，允许新目标并行解码。
    bool hasCurrentGenWork() const;
    // 快帧/RAW 缩略图回主线程上屏；代次不符（已被后续跳转作废）则丢弃。
    void emitPreview(const QString &path, const QImage &img, quint64 generation);
    // 批次04 取证：SOLEVIEW_IMAGEPROC_PROBE=1（兼容 QUIKVIEW_IMAGEPROC_PROBE=1）时把缓存上限/预读/命中写进
    // Temp/imageproc-live-probe.txt；未置位时是空函数，不影响正常路径。
    void probeLog(const QString &line) const;

    QCache<QString, QImage> m_fullCache;
    int m_limitMb = 300;        // 当前原图缓存上限（MB，运行态口径）
    int m_hits = 0;             // requestFull 时原图缓存命中次数
    int m_misses = 0;           // requestFull 时未命中次数
    int m_preloadStarts = 0;    // 预读实际启动次数（不是请求次数）
    QCache<QString, QImage> m_thumbCache;
    // 在飞全图任务：路径 → 提交时的代次。同一路径允许被新代次重新提交
    // （快速反向回到正在解的上一张），旧任务完成时因代次不符降级为只入缓存。
    QHash<QString, quint64> m_fullInFlight;
    // forgetPath 之后，代次小于该值的解码结果不得再进缓存（旧文件内容作废）。
    QHash<QString, quint64> m_minGen;
    // 缩略图作废时间戳：forgetPath 时自增，在飞缩略图回来按此丢弃。
    QHash<QString, quint64> m_thumbEpoch;
    QSet<QString> m_thumbInFlight;
    QList<QString> m_queue;     // 待解码队列（FIFO，去重，上限 8）
    // 请求代次：每次显式跳转（cancelPending / preempt 请求）自增。解码结果只有
    // 代次仍等于当前值才允许上屏——替代原来的「一次性布尔标志」，连续多次跳转
    // 不会互相覆盖（A→B→A 往返靠它区分「哪一代的 A」）。
    // std::atomic：解码 worker 线程在 loader 调用前后轮询代次，旧代次任务跳过后续
    // 重活与后处理（D1-5）；主线程自增，worker 只读。
    std::atomic<quint64> m_generation{0};
    // 全图解码专用线程池：上限 2（旧任务 + 抢占新目标各一条）。不走全局池：
    // QtConcurrent 默认按逻辑核数并发，连按/预读会把 CPU 打满，抢占时也未必
    // 立刻拿得到线程。这里的上限就是「线程数按实测有上限」的落点。
    QThreadPool m_decodePool;
    QThreadPool m_dragPool;
    // D1-1 缩略图专用小池：胶片栏/总览的缩略图解码不再和全图解码、目录索引抢全局
    // 线程，上限 2，翻大目录时出图节奏更均匀。
    QThreadPool m_thumbPool;
    QSize m_previewSize;        // 快帧目标尺寸（视口像素）
    // 预读：同一时刻也只跑 1 个（与全图解码共用「单飞」），排到的路径放这里。
    QList<QString> m_preloadQueue;
    QString m_preloadPath;
    quint64 m_preloadGeneration = 0;   // 预读启动时的代次（旧代不阻塞新目标）
    bool m_preloadPromote = false;  // 预读期间该路径被真的要过：解完直接上屏
    // 拖动预览：独立小缓存（缩放图不能混进 m_fullCache，否则落点会停成模糊帧）
    QCache<QString, QImage> m_dragCache;
    QString m_dragInFlight;     // 正在解的拖动预览（至多 1 个）
    QString m_dragPending;      // 最新落点；解完若已被更新就只入缓存不上屏
    QSize m_dragSize;
    bool m_dragPromote = false; // 在飞预览的页被真要（连按/跳转）：解完按快帧上屏
    bool m_dragMuted = false;   // 有真实请求介入：在飞预览解完只入缓存不上屏
    // 批次05 #24 的会话内覆盖（见 setPreviewModeLive 注释）。
    std::atomic<bool> m_previewLiveKnown { false };
    std::atomic<bool> m_previewLiveOn { true };
};
