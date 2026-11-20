#include "ImageCache.h"
#include "PerfProbe.h"
#include <QCoreApplication>

#include <QFutureWatcher>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include "PsdLoader.h"
#include <QtConcurrent/QtConcurrent>

#include "Config.h"
#include "RawImageLoader.h"
#include "ArchiveStore.h"   // 压缩包内条目：从内存流解码，不落盘（派单 2026-11-18）

namespace {

// 解码失败分流（派单《格式兼容诊断提示》2026-11-18）：判定只发生在 loader 层。
// 读文件头做魔数白名单比对——命中白名单说明「这是个我们能认的格式，只是数据读不
// 出来」（截断/坏块/I/O），归 Corrupt；认不出来（或根本读不到头）归 Unsupported。
// 注意：这里只读前几十字节，不解码，不动解码路径。
LoadErrorCategory classifyByMagic(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return LoadErrorCategory::Corrupt;   // 打不开＝I/O 层问题，不是格式不认识
    }
    const QByteArray head = f.read(64);
    f.close();
    if (head.size() < 4) {
        return LoadErrorCategory::Corrupt;   // 短得连魔数都没有：当读取失败
    }

    auto at = [&head](int i) { return quint8(head.at(i)); };
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool png = at(0) == 0x89 && head.mid(1, 3) == "PNG";
    const bool jpg = at(0) == 0xFF && at(1) == 0xD8 && at(2) == 0xFF;
    const bool gif = head.startsWith("GIF8");
    const bool bmp = head.startsWith("BM");
    const bool webp = head.startsWith("RIFF") && head.mid(8, 4) == "WEBP";
    const bool tiff = head.startsWith("II*\0") || head.startsWith("MM\0*");
    const bool heif = head.mid(4, 4) == "ftyp"
                      && (head.mid(8, 4) == "heic" || head.mid(8, 4) == "heix"
                          || head.mid(8, 4) == "hevc" || head.mid(8, 4) == "hevx"
                          || head.mid(8, 4) == "mif1" || head.mid(8, 4) == "msf1"
                          || head.mid(8, 4) == "avif" || head.mid(8, 4) == "avis"
                          || head.mid(8, 4) == "heis" || head.mid(8, 4) == "heim");
    const bool jxl = at(0) == 0xFF && at(1) == 0x0A;          // JXL codestream
    const bool jxlBox = head.mid(4, 4) == "JXL ";             // JXL container
    const bool qoi = head.startsWith("qoif");
    const bool ico = at(0) == 0x00 && at(1) == 0x00 && (at(2) == 0x01 || at(2) == 0x02)
                     && at(3) == 0x00;
    const bool psd = head.startsWith("8BPS");
    const bool dds = head.startsWith("DDS ");
    const bool exr = head.startsWith("v/1\x01");
    const bool tgaBySuffix = (suffix == QLatin1String("tga"));   // TGA 无固定魔数，按扩展名兜底
    const bool svg = head.contains("<svg") || head.contains("<?xml");

    const bool known = png || jpg || gif || bmp || webp || tiff || heif || jxl
                       || jxlBox || qoi || ico || psd || dds || exr || tgaBySuffix
                       || svg;
    return known ? LoadErrorCategory::Corrupt : LoadErrorCategory::Unsupported;
}

} // namespace

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
    // loadFailed 的类别参数要在跨线程队列里传（解码线程 → 主线程），先注册元类型
    qRegisterMetaType<LoadErrorCategory>("LoadErrorCategory");
    setFullCacheLimit(Config::instance().cacheSizeMb());   // 原图缓存上限（MB，读配置）
    m_thumbCache.setMaxCost(64 * 1024 * 1024);   // 缩略图缓存上限 ~64MB
    m_dragCache.setMaxCost(64 * 1024 * 1024);    // 拖动预览（缩放到视口）上限 64MB：24 张目录回拖也要命中
    // 全图解码专用池：最多 2 条并行 = 在飞的旧任务 + 抢占式新目标各一条。
    // 这是「线程数按实测有上限」的落点：再快也不允许无限并发抢 CPU。
    m_decodePool.setMaxThreadCount(2);
}

void ImageCache::setFullCacheLimit(int mb)
{
    m_fullCache.setMaxCost(qint64(mb) * 1024 * 1024);
}

QImage ImageCache::cachedFull(const QString &path) const
{
    return m_fullCache.contains(path) ? *m_fullCache.object(path) : QImage();
}

void ImageCache::requestFull(const QString &path, const QSize &previewSize, bool preempt)
{
    m_previewSize = previewSize;
    if (preempt) {
        // 显式跳转：进入新代次。此刻之前提交的所有结果只入缓存、不再上屏。
        ++m_generation;
    }
    if (path == m_preloadPath) {
        // 预读正在解这一页：升级为「要上屏」，解完直接按序发出，不另起任务
        // （否则同一页两个解码，先到的可能是后一页，画面会回跳）
        m_preloadPromote = true;
        m_preloadGeneration = m_generation;   // 认领为当前代次，结果可上屏
        return;
    }
    if (path == m_dragInFlight) {
        // 拖动预览正在解这一页：解完按「快帧」上屏，全图仍照常排进 FIFO
        m_dragPromote = true;
    }
    // 有真实请求介入：拖动预览不再抢屏（旧落点的在飞结果只进缓存）
    m_dragMuted = true;
    m_dragPending.clear();
    // 当前代次正在解同一张才复用在飞结果；旧代次的同路径必须重新提交，
    // 否则它解完会被代次检查拦下，画面永远等不到。
    if (m_fullInFlight.contains(path) && m_fullInFlight.value(path) == m_generation) {
        return;   // 正在解
    }
    if (m_queue.contains(path)) {
        return;   // 已在队列
    }
    const bool busy = hasCurrentGenWork() || !m_queue.isEmpty();
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
    // 只要没有「当前代次」的工作在跑就立刻启动。旧代次在飞不阻塞新目标：
    // 它的结果会被代次检查丢弃，新目标与它并行解码（池上限 2 保证不失控）。
    if (!hasCurrentGenWork()) {
        startNext();
    }
}

void ImageCache::insertFull(const QString &path, const QImage &img)
{
    // 缓存成本按字节计，QCache 才会真的按容量 LRU 淘汰
    // （此前 insert 不传成本=每条记 1，512MB 上限形同虚设）
    m_fullCache.insert(path, new QImage(img),
                       static_cast<int>(qMin<qint64>(img.sizeInBytes(), qint64(INT_MAX))));
}

void ImageCache::preload(const QString &path)
{
    if (path.isEmpty() || m_fullCache.contains(path) || m_queue.contains(path)
        || m_fullInFlight.contains(path) || path == m_preloadPath
        || m_preloadQueue.contains(path)) {
        return;
    }
    m_preloadQueue.append(path);
    startNextPreload();
}

void ImageCache::startNextPreload()
{
    if (!m_preloadPath.isEmpty() || m_preloadQueue.isEmpty()) {
        return;
    }
    if (!m_queue.isEmpty() || !m_fullInFlight.isEmpty()) {
        return;   // 用户请求优先，预读等空闲
    }
    const QString path = m_preloadQueue.takeFirst();
    m_preloadPath = path;
    m_preloadGeneration = m_generation;
    QtConcurrent::run(&m_decodePool, [this, path]() {
        QImage img;
        if (ArchiveStore::isEntryPath(path)) {
            // 包内条目：解压到内存再解码，解完即弃
            QString ignored;
            img = ArchiveStore::decode(path, QSize(), &ignored);
        } else if (PsdLoader::isPsdPath(path)) {
            img = PsdLoader::load(path, false).composite;
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            img = reader.read();
        }
        QMetaObject::invokeMethod(this, [this, path, img]() {
            if (m_preloadPath != path) {
                return;   // clearFull() 已作废这轮预读
            }
            m_preloadPath.clear();
            const bool promote = m_preloadPromote;
            m_preloadPromote = false;
            if (!img.isNull()) {
                insertFull(path, img);
                if (promote) {
                    emit fullReady(path, img, false);
                }
            }
            // 用户排队的解码优先，其次下一张预读
            if (!m_queue.isEmpty()) {
                startNext();
            } else {
                startNextPreload();
            }
        }, Qt::QueuedConnection);
    });
}

bool ImageCache::isIdle() const
{
    return m_queue.isEmpty() && m_fullInFlight.isEmpty() && m_preloadQueue.isEmpty()
        && m_preloadPath.isEmpty();
}

bool ImageCache::isFullPending(const QString &path) const
{
    // 预读升级路径也要算：requestFull 命中 m_preloadPath 时直接置 promote 返回，
    // 不进队列，仅看队列/在飞会漏判「仍在等待」。
    return m_queue.contains(path) || m_fullInFlight.contains(path)
           || m_preloadPath == path;
}

// 拖动滑杆的跟手预览：把「当前落点」记成最新意图，实际解码交给 startDragDecode()
// 串行做（同一时刻至多 1 个）。落点变来变去时，旧结果只进缓存不上屏。
void ImageCache::requestDragPreview(const QString &path, const QSize &size)
{
    if (path.isEmpty()) {
        return;
    }
    m_dragMuted = false;   // 又回到拖动：重新允许上屏
    m_dragSize = size;
    // 全图缓存优先（拖动方向已经走过的那张站住就用它，0ms），其次拖动预览缓存
    if (m_fullCache.contains(path)) {
        m_dragPending.clear();
        emit dragPreviewReady(path, *m_fullCache.object(path));
        return;
    }
    if (m_dragCache.contains(path)) {
        m_dragPending.clear();
        emit dragPreviewReady(path, *m_dragCache.object(path));
        return;
    }
    m_dragPending = path;
    startDragDecode();
}

void ImageCache::startDragDecode()
{
    if (m_dragPending.isEmpty() || !m_dragInFlight.isEmpty()) {
        return;   // 单飞：在飞的那个解完再看最新落点
    }
    const QString path = m_dragPending;
    const QSize size = m_dragSize;
    m_dragPending.clear();
    m_dragInFlight = path;
    QtConcurrent::run([this, path, size]() {
        QImage img;
        if (ArchiveStore::isEntryPath(path)) {
            QString ignored;
            img = ArchiveStore::decode(path, size, &ignored);
        } else {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const QSize src = reader.size();
            if (size.isValid() && src.isValid() && src.width() > 0 && src.height() > 0) {
                QSize scaled = src;
                scaled.scale(size, Qt::KeepAspectRatio);
                if (scaled.width() > 0 && scaled.height() > 0) {
                    reader.setScaledSize(scaled);
                }
            }
            if (reader.canRead()) {
                img = reader.read();
            }
        }
        QMetaObject::invokeMethod(this, [this, path, img]() {
            if (m_dragInFlight != path) {
                return;   // clearFull() 已作废这轮
            }
            m_dragInFlight.clear();
            const bool promote = m_dragPromote;
            // 拖动中解完的每一张都按序发出（解码串行，顺序天然与拖动方向一致）——
            // 若按「落点已变就丢」处理，60ms 节流 + 70ms 解码会永远丢，又只剩最后一张。
            // 只有真实请求介入（松手跳转/连按）后 m_dragMuted 才置真，那时才只入缓存。
            const bool muted = m_dragMuted;
            m_dragPromote = false;
            if (!img.isNull()) {
                m_dragCache.insert(path, new QImage(img),
                                   static_cast<int>(qMin<qint64>(img.sizeInBytes(), qint64(INT_MAX))));
                if (promote && !m_fullCache.contains(path)) {
                    // 落点被真要：这张缩放图正好当快帧顶住，全图随后覆盖
                    emit fullReady(path, img, true);
                } else if (!promote && !muted) {
                    emit dragPreviewReady(path, img);
                }
            }
            startDragDecode();   // 落点又变了就接着解最新那张
        }, Qt::QueuedConnection);
    });
}

void ImageCache::startNext()
{
    while (!m_queue.isEmpty() && !hasCurrentGenWork()) {
        const QString path = m_queue.takeFirst();
        if (m_fullCache.contains(path)) {
            // 排队期间被缓存命中（如先前的解码完成入缓存）：按序直接上屏
            emit fullReady(path, *m_fullCache.object(path), false);
            continue;
        }
        PerfProbe::mark(PerfProbe::currentSeq(), "decode-start", "");
        startFullDecode(path, m_previewSize, m_generation);
        return;
    }
}

// 「当前代次是否还有活」：旧代次的在飞任务不算，它们的输出已注定被丢弃。
// 单飞语义只对同代次成立——连按（同代）仍然逐张排队，跳转（换代）允许并行。
bool ImageCache::hasCurrentGenWork() const
{
    for (auto it = m_fullInFlight.constBegin(); it != m_fullInFlight.constEnd(); ++it) {
        if (it.value() == m_generation) {
            return true;
        }
    }
    return !m_preloadPath.isEmpty() && m_preloadGeneration == m_generation;
}

// 快帧/RAW 缩略图在后台线程解出，回主线程前先核对代次：过期结果一律丢弃，
// 否则跳走后旧图的快帧会盖在新目标上（全图有 finishFullDecode 拦，快帧原本没有）。
void ImageCache::emitPreview(const QString &path, const QImage &img, quint64 generation)
{
    QMetaObject::invokeMethod(this, [this, path, img, generation]() {
        if (generation != m_generation) {
            return;
        }
        emit fullReady(path, img, true);
    }, Qt::QueuedConnection);
}

void ImageCache::startFullDecode(const QString &path, const QSize &previewSize,
                                 quint64 generation)
{
    m_fullInFlight.insert(path, generation);
    QtConcurrent::run(&m_decodePool, [this, path, previewSize, generation]() {
        // 包内条目（.zip/.cbz 里的文件）：解压进内存 → QImageReader 从内存流解码。
        // 与文件路径同构：先按视口出一张快帧顶住连按，再解全图；解码完的字节即弃，
        // 全程不落盘（派单 §二.3 / §四.5 的内存峰值就落在这两条）。
        if (ArchiveStore::isEntryPath(path)) {
            if (previewSize.isValid() && Config::instance().previewMode()) {
                QString ignored;
                const QImage prev = ArchiveStore::decode(path, previewSize, &ignored);
                if (!prev.isNull()) {
                    emitPreview(path, prev, generation);
                }
            }
            QString entryError;
            LoadErrorCategory entryCategory = LoadErrorCategory::None;
            const QImage img = ArchiveStore::decode(path, QSize(), &entryError, &entryCategory);
            QMetaObject::invokeMethod(this, [this, path, img, entryError, entryCategory,
                                             generation]() {
                finishFullDecode(path, img, generation, entryError, entryCategory);
            }, Qt::QueuedConnection);
            return;
        }
        // PSD 走 PsdLoader（QImageReader 无 PSD 插件）：用文件自带的原始合成预览，
        // 不逐层重合成，避免用近似结果冒充原文件效果。
        if (PsdLoader::isPsdPath(path)) {
            const PsdDocument psdDoc = PsdLoader::load(path, false);
            const QImage img = psdDoc.composite;
            const QString err = psdDoc.error;   // 例如「ZIP 压缩的 PSD 暂不支持」
            // 类别由 PsdLoader 判定（含 PSD 专属的 NoCompatPreview），UI 只取文案
            const LoadErrorCategory cat = psdDoc.errorCategory;
            QMetaObject::invokeMethod(this, [this, path, img, err, cat, generation]() {
                finishFullDecode(path, img, generation, err, cat);
            }, Qt::QueuedConnection);
            return;
        }
        const bool rawFile = isRawFilePath(path);
        // RAW 内嵌缩略图优先上屏（键 "config/etc/raw_thumbnail"，默认开）：
        // libraw unpack_thumb 通常几十毫秒，先顶住首屏，全尺寸随后在同一后台
        // 线程继续解。「不打开 RAW」时连缩略图也不出，交给 onLoadFailed 提示。
        if (rawFile && Config::instance().rawThumbnail() && !Config::instance().noRawLoad()) {
            const QImage thumb = loadRawThumbnail(path);
            if (!thumb.isNull()) {
                emitPreview(path, thumb, generation);
            }
        }
        // 快帧先上屏：JPEG 走 DCT 缩放（12MP 约 10-20ms）、PNG 等同样支持
        // 按比例解码，先按视口像素出一张顶住连按体感，全图随后替换。
        // HEIC/RAW 的 QImageReader 读不出 size()，自然跳过快帧，仍走各自 Loader。
        // 「先显示低品质图片以加速渲染」（C14）关闭时跳过这一级，等全图一次上屏。
        if (previewSize.isValid() && Config::instance().previewMode()) {
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
                        emitPreview(path, prev, generation);
                    }
                }
            }
        }
        QImageReader reader(path);
        reader.setAutoTransform(true);
        QImage img = reader.read();
        QString readerError;
        if (img.isNull()) {
            const QByteArray formats = QImageReader::supportedImageFormats().join(',');
            readerError = QStringLiteral("QImageReader: %1; format=%2; supported=%3")
                              .arg(reader.errorString(),
                                   QString::fromLatin1(reader.format()),
                                   QString::fromLatin1(formats));
        }
        // QImageReader 不认 RAW：全尺寸解码也留在本后台线程，避免回主线程
        // 同步解把窗口卡住（「不打开 RAW」时跳过，交给 onLoadFailed 提示）。
        if (img.isNull() && rawFile && !Config::instance().noRawLoad()) {
            QString rawErr;
            img = loadRawImage(path, &rawErr, Config::instance().hdrTonemap());
        }
        QMetaObject::invokeMethod(this, [this, path, img, readerError, generation]() {
            finishFullDecode(path, img, generation, readerError);
        }, Qt::QueuedConnection);
    });
}

void ImageCache::finishFullDecode(const QString &path, const QImage &img,
                                       quint64 generation, const QString &error,
                                       LoadErrorCategory category)
{
    // clearFull() 会在开关 HDR 时清空在飞集合：被清任务的结果到这里静默丢弃
    if (!m_fullInFlight.contains(path)) {
        return;
    }
    // forgetPath 立过代次门限：早于门限的结果属于被替换掉的旧文件内容，
    // 既不能进缓存，也不能上屏。
    const quint64 floorGen = m_minGen.value(path, 0);
    // 同一路径已被新代次重新提交（快速反向回到上一张）：这条旧结果不动状态，
    // 只把解码成果塞进缓存，等新代次那条回来再上屏。
    if (m_fullInFlight.value(path) != generation) {
        if (!img.isNull() && generation >= floorGen) {
            insertFull(path, img);
        }
        return;
    }
    m_fullInFlight.remove(path);
    m_fullInFlight.remove(path);
    PerfProbe::mark(PerfProbe::currentSeq(), "decode-full", "");
    // 唯一的上屏判据：结果仍属于当前代次。显式跳转在 cancelPending 里自增代次，
    // 「跳走后旧图才解完」自然被拦下；不再依赖会被多次跳转互相覆盖的布尔标志。
    const bool stale = (generation != m_generation);
    if (img.isNull()) {
        if (!stale) {
            // loader 没给类别（QImageReader / RAW 路径）时，在这里按魔数白名单补判：
            // 认得出格式头 = 损坏，认不出 = 不支持。UI 层不再做任何字符串匹配。
            if (category == LoadErrorCategory::None) {
                category = classifyByMagic(path);
            }
            emit loadFailed(path, error.isEmpty()
                                       ? QCoreApplication::translate("ImageCache", "无法解码")
                                       : error, category);
        }
    } else {
        if (generation >= floorGen) {
            insertFull(path, img);
        }
        if (!stale && generation >= floorGen) {
            // FIFO：按入队顺序无条件上屏（显式跳转的在飞旧结果除外）
            emit fullReady(path, img, false);
        }
    }
    if (!m_queue.isEmpty()) {
        startNext();
    } else {
        startNextPreload();   // 用户队列空了，接着填空闲预读
    }
}

void ImageCache::cancelPending()
{
    // 进入新代次：在飞任务拦不住（QtConcurrent 不可取消），但结果到达时会因
    // 代次不符只入缓存不上屏。用代次替代单个布尔标志，连续跳转不会互相覆盖
    // （A→B→A 往返也能正确区分「哪一代的 A」）。
    ++m_generation;
    m_queue.clear();
    // 旧页面的邻居预读在新位置已经没意义：待预读清掉
    m_preloadQueue.clear();
    m_preloadPromote = false;
    // 拖动预览的旧落点同理：在飞结果只入缓存，别把松手后的新页面盖回去
    m_dragPending.clear();
    m_dragMuted = true;
}

// 同名文件在外部被删掉后又保存回来：磁盘上已经是新内容，缓存里的旧图必须作废。
// 清缓存不够——在飞的解码/预读/缩略图还会把旧内容塞回来，所以：
//   ① m_minGen 给该路径立代次门限，早于门限的解码结果不得进缓存；
//   ② 在飞全图任务直接从 m_fullInFlight 摘除，完成回调找不到条目即整体丢弃；
//   ③ 预读/拖动预览命中同一路径时一并清空，回调按「路径不符」丢弃；
//   ④ 缩略图缓存按 key 前缀清掉，在飞缩略图用 epoch 作废。
void ImageCache::forgetPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_minGen.insert(path, m_generation + 1);

    m_fullCache.remove(path);
    m_dragCache.remove(path);
    m_queue.removeAll(path);
    m_preloadQueue.removeAll(path);
    if (m_preloadPath == path) {
        m_preloadPath.clear();
        m_preloadPromote = false;
    }
    if (m_dragInFlight == path) {
        m_dragInFlight.clear();
    }
    if (m_dragPending == path) {
        m_dragPending.clear();
    }
    m_fullInFlight.remove(path);

    const QString prefix = path + QLatin1Char('@');
    const QList<QString> thumbKeys = m_thumbCache.keys();
    for (const QString &key : thumbKeys) {
        if (key.startsWith(prefix)) {
            m_thumbCache.remove(key);
        }
    }
    const QList<QString> inFlight = m_thumbInFlight.values();
    for (const QString &key : inFlight) {
        if (key.startsWith(prefix)) {
            m_thumbInFlight.remove(key);
        }
    }
    m_thumbEpoch.insert(path, m_thumbEpoch.value(path) + 1);
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
    const quint64 epoch = m_thumbEpoch.value(path);

    auto future = QtConcurrent::run([path, size]() {
        if (ArchiveStore::isEntryPath(path)) {
            // 包内缩略图：解压入口字节 → 按缩略图尺寸解码（ArchiveStore 负责最终缩比）
            QString ignored;
            return ArchiveStore::decode(path, size, &ignored);
        }
        if (PsdLoader::isPsdPath(path)) {
            QImage thumb = PsdLoader::load(path, false).composite;
            if (!thumb.isNull() && (thumb.width() > size.width() || thumb.height() > size.height()))
                thumb = thumb.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            return thumb;
        }
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
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, path, key, epoch, watcher]() {
        m_thumbInFlight.remove(key);
        const QImage img = watcher->result();
        watcher->deleteLater();
        // 解码期间文件被同名新内容替换（forgetPath）：这张旧缩略图不许进缓存，
        // 也不许发出去覆盖已经重建的胶片栏；重新可见时自然会重新请求。
        if (m_thumbEpoch.value(path) != epoch) {
            return;
        }
        if (!img.isNull())
            m_thumbCache.insert(key, new QImage(img));
        emit thumbnailReady(path, img);
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
    ++m_generation;   // 已清掉的任务即使回来也不再被认作当前结果
    m_fullCache.clear();
    m_queue.clear();
    m_preloadQueue.clear();
    m_preloadPath.clear();
    m_preloadGeneration = 0;
    m_preloadPromote = false;
    m_dragCache.clear();
    m_dragInFlight.clear();
    m_dragPending.clear();
    m_dragPromote = false;
    m_dragMuted = false;
}
