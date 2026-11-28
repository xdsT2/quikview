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
#include <memory>

#include "DecodeQueue.h"
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
    // P0-d：就绪帧新鲜度校验。条目命中时比对图源 mtime/size 与帧准备选项
    // （imageIcc / hdrTonemap），任一不符即把该条目从缓存与元数据表一并作废，
    // 返回 false 走重新解码；包内条目无独立磁盘文件，跳过 mtime/size 比对，只比选项。
    bool cacheFresh(const QString &path) const;
    // 同时移除全图位图缓存条目与其新鲜度元数据。
    void removeFullEntry(const QString &path);
    // preempt=true：显式跳转（打开/换目录/滑杆落点/缩略图/书签/首尾/快速反向）。
    // 进入新代次并允许在旧任务仍在解码时立刻启动新目标，旧结果只入缓存不上屏。
    // preempt=false：顺序翻页，保持 FIFO 单飞、逐张按序上屏（连按体感不变）。
    void requestFull(const QString &path, const QSize &previewSize = QSize(),
                     bool preempt = false);
    // 缩略图优先级（D2-3）：当前可见区域用 Visible，预取/未可见用 Prefetch。
    // 同一 key 已在队列里时，Visible 会把 Prefetch 提前；在飞任务不受影响。
    enum class ThumbPriority { Visible = 0, Prefetch = 1 };
    void requestThumbnail(const QString &path, const QSize &size,
                          ThumbPriority priority = ThumbPriority::Visible);
    // 外部刷新（同名文件被删后又保存回来）：丢弃该路径的全图/拖动/缩略图缓存与
    // 排队项，并作废此前的在飞结果，下一次请求从磁盘重新解码。
    void forgetPath(const QString &path);
    // HDR/EXR 曝光补偿（H1-8）：解码期参数，改变后已缓存的 HDR/EXR 结果全部失效。
    // 只影响走 HdrPreview 曲线的格式（hdr/pic/exr），8bit 图一个像素都不动。
    void setHdrExposureEv(float ev);
    float hdrExposureEv() const { return m_hdrEv; }
    void clear();
    void clearFull();   // 只清原图缓存（缩略图不受 HDR 开关影响）
    // D1-4 切目录回收：移除缓存中属于指定目录（磁盘路径前缀）的全图条目，并整路
    // 清空拖动预览缓存；缩略图缓存保留（按路径隔离、体积小，跨目录回翻仍可命中）。
    void evictDirectory(const QString &dirPath);
    // D1-4 压缩包整包回收：包内条目路径是「包路径/条目名」，不属于任何磁盘目录；
    // 离开一个包时按包路径前缀淘汰全图/拖动/排队/在飞，避免换包后旧条目继续占预算
    // （在飞项从 m_fullInFlight 摘除，解完回来直接丢弃，不再塞回缓存）。
    void evictArchive(const QString &archivePath);
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
    // D2-1 快车道独立事件：预览帧就绪。与 fullReady(preview=true) 语义相同，
    // 但由独立的预览线程池发出，主窗口可以按「预览/全量」两类事件分别处理。
    void previewReady(const QString &path, const QImage &image);
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
    // D2-1：预览快车道。独立单线程池 + 每代次至多一条在飞，只发 previewReady，
    // 不写全图缓存；全量先到时由 m_fullEmitted 把迟到的预览拦掉。
    void startPreviewDecode(const QString &path, const QSize &previewSize,
                            quint64 generation,
                            const std::shared_ptr<std::atomic<bool>> &cancel);
    void finishPreview(const QString &path, const QImage &img, quint64 generation);
    void startNext();
    void startNextPreload();
    void startDragDecode();
    void startThumbJobs();
    void insertFull(const QString &path, const QImage &img);
    // 是否有「当前代次」尚未完成的工作（全图在飞 / 预读在飞）。
    // 只有它才会阻止下一次启动：旧代次的任务已被判定为过期，允许新目标并行解码。
    bool hasCurrentGenWork() const;
    // 代次推进的唯一入口：置旧取消令牌 + 换新令牌 + 代次 +1 + 清「全量已上屏」表。
    void bumpGeneration();
    // 按路径前缀淘汰（目录 / 压缩包共用）：缓存、排队、预读、拖动与在飞全图。
    void evictPrefix(const QString &prefix);
    // 批次04 取证：SOLEVIEW_IMAGEPROC_PROBE=1（兼容 QUIKVIEW_IMAGEPROC_PROBE=1）时把缓存上限/预读/命中写进
    // Temp/imageproc-live-probe.txt；未置位时是空函数，不影响正常路径。
    void probeLog(const QString &line) const;

    QCache<QString, QImage> m_fullCache;
    // P0-d：就绪帧条目新鲜度元数据，与 m_fullCache 同键平行存放。
    struct FrameMeta {
        qint64 mtimeSec = 0;       // 图源最后修改时间（秒）；包内条目为 0（不参与比对）
        qint64 sizeBytes = 0;      // 图源字节数；包内条目为 0
        bool imageIcc = true;      // 生成该帧时的「图片内嵌 ICC」开关快照
        bool hdrTonemap = false;   // 生成该帧时的 HDR tone map 开关快照
    };
    QHash<QString, FrameMeta> m_fullMeta;
    int m_limitMb = 300;        // 当前原图缓存上限（MB，运行态口径）
    int m_hits = 0;             // requestFull 时原图缓存命中次数
    int m_misses = 0;           // requestFull 时未命中次数
    int m_preloadStarts = 0;    // 预读实际启动次数（不是请求次数）
    // D2-3 缩略图两级缓存：
    //   m_thumbCache  = L1 解码后的缩放原图（按 key 复用，跨尺寸不串）；
    //   m_thumbDisplay= L2 已转成上屏格式（ARGB32_Premultiplied / RGB32）的位图。
    // 命中 L1 只做一次格式转换即可发图，不必重新解码；L2 直接发。
    QCache<QString, QImage> m_thumbCache;
    QCache<QString, QImage> m_thumbDisplay;
    // 在飞全图任务：路径 → 提交时的代次。同一路径允许被新代次重新提交
    // （快速反向回到正在解的上一张），旧任务完成时因代次不符降级为只入缓存。
    QHash<QString, quint64> m_fullInFlight;
    // forgetPath 之后，代次小于该值的解码结果不得再进缓存（旧文件内容作废）。
    QHash<QString, quint64> m_minGen;
    // 缩略图作废时间戳：forgetPath 时自增，在飞缩略图回来按此丢弃。
    QHash<QString, quint64> m_thumbEpoch;
    QSet<QString> m_thumbInFlight;
    // D2-3 缩略图优先级队列：只排「尚未提交到池」的任务，在飞上限 = 池线程数 2。
    // Visible（当前可见格）先于 Prefetch（预取）；同级按请求顺序。
    struct ThumbTask
    {
        QString path;
        QString key;
        QSize size;
        ThumbPriority priority = ThumbPriority::Visible;
        quint64 order = 0;
    };
    QVector<ThumbTask> m_thumbQueue;
    int m_thumbActive = 0;
    quint64 m_thumbOrder = 0;
    // D2-2 待解码队列：Preempt（显式跳转）优先于 Normal（顺序翻页），上限 8。
    DecodeQueue m_queue;
    quint64 m_queueOrder = 0;
    // 请求代次：每次显式跳转（cancelPending / preempt 请求）自增。解码结果只有
    // 代次仍等于当前值才允许上屏——替代原来的「一次性布尔标志」，连续多次跳转
    // 不会互相覆盖（A→B→A 往返靠它区分「哪一代的 A」）。
    // std::atomic：解码 worker 线程在 loader 调用前后轮询代次，旧代次任务跳过后续
    // 重活与后处理（D1-5）；主线程自增，worker 只读。
    float m_hdrEv = 0.0f;       // HDR/EXR 曝光补偿（档），0 = 自动预览
    std::atomic<quint64> m_generation{0};
    // D2-2 在飞取消令牌：换代时把旧令牌置 true，worker 在下一个检查点直接放弃，
    // 不再继续做预览/后处理（QtConcurrent 本身不可中断，这是「能停就停」的落点）。
    std::shared_ptr<std::atomic<bool>> m_cancelToken;
    // 本代次已经发过全量的路径：迟到的预览帧必须丢弃，否则会用低清图盖掉全图。
    QSet<QString> m_fullEmitted;
    // 预览在飞：路径 → 提交代次。同一路径允许新代次重新提交（快速 A→B→A），
    // 旧预览回来时按代次丢弃，不会把新代次的在飞标记提前清掉。
    QHash<QString, quint64> m_previewInFlight;
    // 全图解码专用线程池：上限 2（旧任务 + 抢占新目标各一条）。不走全局池：
    // QtConcurrent 默认按逻辑核数并发，连按/预读会把 CPU 打满，抢占时也未必
    // 立刻拿得到线程。这里的上限就是「线程数按实测有上限」的落点。
    QThreadPool m_decodePool;
    // D2-1 预览快车道：单线程，只做降采样快帧/RAW 内嵌缩略图。与全量解码池分离，
    // 全量不必等预览读完才开始，重活与轻活互不排队。
    QThreadPool m_previewPool;
    QThreadPool m_dragPool;
    // D1-1 缩略图专用小池：胶片栏/总览的缩略图解码不再和全图解码、目录索引抢全局
    // 线程，上限 2，翻大目录时出图节奏更均匀。
    QThreadPool m_thumbPool;
    QSize m_previewSize;        // 快帧目标尺寸（视口像素）
    // 最新一次 requestFull 的目标：预览车道只在它上面落屏。顺序翻页不换代次，
    // 若允许旧目标的预览晚到，会把已经翻过去的新页盖回上一张。
    QString m_lastRequestedPath;
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
