#pragma once

#include <QDate>
#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QThreadPool>

// 单个文件的头信息探测结果（需求池 D2-4：开图前头信息预探测层）。
// 除尺寸与拍摄日期外，其余字段都是「能判定就填、判定不了就是未知/否」：
// 不同格式能从文件头拿到的信息量差别很大（JPEG 有 precision 与 IFD1 缩略图，
// 裸 TIFF 的 BitsPerSample 要跟随偏移、代价高），调用方不要拿它们做硬判断。
struct ImageProbeResult
{
    QSize pixelSize;                // 文件头给出的原始像素尺寸；无效 = 未知
    QDate shotDate;                 // EXIF 拍摄日期；无效 = 未知
    int orientation = 0;            // EXIF Orientation，0 = 未知，1 = 正常
    int bitDepth = 0;               // 每通道位深，0 = 未知
    bool hasAlpha = false;          // 文件头能判定时填
    bool hasEmbeddedThumb = false;  // JPEG EXIF IFD1 / HEIF 内嵌缩略图存在性，尽力而为
    QString format;                 // 小写格式名（QImageReader::format() 或后缀）

    bool hasSize() const { return pixelSize.width() > 0 && pixelSize.height() > 0; }
};
Q_DECLARE_METATYPE(ImageProbeResult)

// 后台头信息探测服务：把「每个文件读最多 256 KB 文件头」从 GUI 线程挪到专用小池。
//
// 线程契约（务必遵守）：
//  - cached / request / requestAll / forget 只能在 GUI 线程调用（缓存与在飞集合不上锁）；
//  - worker 只跑纯函数 probeFile()，结果用 queued invokeMethod 回 GUI 线程再落缓存，
//    所以 probed 信号也只在 GUI 线程发出。
//
// 缓存新鲜度 = 路径 + mtime + size 三者都没变；文件被改写后自动重探，
// 文件被删除 / 换目录时由调用方 forget()，同时作废该路径的在飞结果。
class ImageProbe : public QObject
{
    Q_OBJECT
public:
    explicit ImageProbe(QObject *parent = nullptr);
    ~ImageProbe() override;

    // 命中新鲜缓存时回填 *out 并返回 true；不发起探测。
    bool cached(const QString &path, ImageProbeResult *out) const;
    // 已在飞 / 已有新鲜缓存则忽略，否则投递一次后台探测（任何路径都会走到 probed）。
    void request(const QString &path);
    void requestAll(const QStringList &paths);
    // 文件变更 / 删除 / 换目录：清缓存，并让在飞结果回来时被丢弃。
    void forget(const QString &path);

signals:
    void probed(const QString &path, const ImageProbeResult &result);

private:
    struct Entry
    {
        ImageProbeResult result;
        qint64 mtimeMs = 0;
        qint64 size = 0;
    };

    class ProbeTask;

    // worker 结果回主线程的落点：代次不符（期间被 forget 过）就直接丢弃。
    void deliver(const QString &path, qint64 mtimeMs, qint64 size, quint64 gen,
                 const ImageProbeResult &result);
    // 纯函数：只读文件头，不碰任何成员，可安全在工作线程调用。
    static ImageProbeResult probeFile(const QString &path);

    // 上限 2：探测是纯 IO + 头解析，再多人也不会更快，反而和缩略图解码抢磁盘。
    QThreadPool m_pool;
    QHash<QString, Entry> m_cache;
    QSet<QString> m_inFlight;
    // 每路径的作废代次：forget() 自增，在飞任务带回旧代次时结果作废。
    // 只增不减的小表（长度 ≈ 本次会话 forget 过的路径数），不值得为它做回收。
    QHash<QString, quint64> m_gen;
};
