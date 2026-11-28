#include "ImageCache.h"
#include "PerfProbe.h"
#include <QCoreApplication>

#include <QFutureWatcher>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include "PsdLoader.h"
#include "SvgLoader.h"      // 矢量 SVG：QSvgRenderer 栅格化（派单 2026-11-18）
#include "ExtraImageLoader.h"
#include <QtConcurrent/QtConcurrent>

#include "Config.h"
#include "DisplayFrame.h"
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
    const bool hdr = head.startsWith("#?RADIANCE") || head.startsWith("#?RGBE");
    const bool jxr = at(0) == 0x49 && at(1) == 0x49 && at(2) == 0xBC;
    const bool ani = head.startsWith("RIFF") && head.mid(8, 4) == "ACON";
    const bool pcx = at(0) == 0x0A && (suffix == QLatin1String("pcx"));
    const bool sgi = at(0) == 0x01 && at(1) == 0xDA;
    const bool pamPfm = head.startsWith("P7") || head.startsWith("PF") || head.startsWith("Pf");
    const bool ras = at(0) == 0x59 && at(1) == 0xA6 && at(2) == 0x6A && at(3) == 0x95;
    const bool ff = head.startsWith("farbfeld");
    const bool dpx = head.startsWith("SDPX") || head.startsWith("XPDS");
    const bool jp2 = (head.size() >= 12 && head.mid(4, 4) == "jP  ")
                     || (at(0) == 0xFF && at(1) == 0x4F && at(2) == 0xFF && at(3) == 0x51);
    const bool kraOra = head.startsWith("PK\x03\x04")
                        && (suffix == QLatin1String("kra") || suffix == QLatin1String("ora"));
    const bool xwd = (suffix == QLatin1String("xwd") && head.size() >= 8 && at(7) == 7);
    const bool wmfEmf = (suffix == QLatin1String("wmf") || suffix == QLatin1String("emf"))
                        && (head.startsWith("\xD7\xCD\xC6\x9A") || at(0) == 0x01);
    const bool tgaBySuffix = (suffix == QLatin1String("tga"));   // TGA 无固定魔数，按扩展名兜底
    const bool svg = head.contains("<svg") || head.contains("<?xml")
                     || (at(0) == 0x1F && at(1) == 0x8B && suffix == QLatin1String("svgz"));

    const bool known = png || jpg || gif || bmp || webp || tiff || heif || jxl
                       || jxlBox || qoi || ico || psd || dds || exr || hdr || jxr
                       || ani || pcx || sgi || pamPfm || ras || ff || dpx || jp2
                       || kraOra || xwd || wmfEmf || tgaBySuffix || svg;
    return known ? LoadErrorCategory::Corrupt : LoadErrorCategory::Unsupported;
}

// D2-1 快帧：按视口尺寸降采样读一帧；后端不支持 / 读回尺寸不符就返回空图，
// 调用方静默退回全量。RAW/PSD/SVG 不走这里（各自 loader 没有廉价降采样路径）。
QImage scaledReaderPreview(const QString &path, const QSize &previewSize)
{
    if (!previewSize.isValid()) {
        return QImage();
    }
    QImageReader fast(path);
    fast.setAutoTransform(true);
    QSize src = fast.size();
    if (!src.isValid() || src.width() <= 0 || src.height() <= 0
        || !fast.supportsOption(QImageIOHandler::ScaledSize)) {
        return QImage();
    }
    // EXIF 旋转 90/270 时 size() 是转之前的，先转置再按视口适配
    const QImageIOHandler::Transformations t = fast.transformation();
    if (t.testFlag(QImageIOHandler::TransformationRotate90)
        || t.testFlag(QImageIOHandler::TransformationRotate270)) {
        src.transpose();
    }
    const QSize scaled = src.scaled(previewSize, Qt::KeepAspectRatio);
    if (!scaled.isValid() || scaled.width() <= 0 || scaled.height() <= 0
        || scaled == src) {
        return QImage();
    }
    fast.setScaledSize(scaled);
    const QImage prev = fast.read();
    // 个别插件谎报支持 ScaledSize：读回尺寸不符就当没有快帧
    return (prev.isNull() || prev.size() != scaled) ? QImage() : prev;
}

// D2-3 L2：转成上屏最省事的格式（无 alpha 用 RGB32、有 alpha 用
// ARGB32_Premultiplied）。只是同一份像素的格式转换，不重新解码。
QImage toDisplayFormat(const QImage &src)
{
    if (src.isNull()) {
        return src;
    }
    if (src.format() == QImage::Format_RGB32
        || src.format() == QImage::Format_ARGB32_Premultiplied) {
        return src;
    }
    return src.hasAlphaChannel()
               ? src.convertToFormat(QImage::Format_ARGB32_Premultiplied)
               : src.convertToFormat(QImage::Format_RGB32);
}

int imageCostBytes(const QImage &img)
{
    return static_cast<int>(qMin<qint64>(img.sizeInBytes(), qint64(INT_MAX)));
}

} // namespace

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
    // loadFailed 的类别参数要在跨线程队列里传（解码线程 → 主线程），先注册元类型
    qRegisterMetaType<LoadErrorCategory>("LoadErrorCategory");
    setFullCacheLimit(Config::instance().cacheSizeMb());   // 原图缓存上限（MB，读配置）
    m_thumbCache.setMaxCost(64 * 1024 * 1024);   // 缩略图缓存上限 ~64MB
    m_thumbDisplay.setMaxCost(64 * 1024 * 1024); // D2-3 L2：上屏格式位图，独立预算
    m_dragCache.setMaxCost(64 * 1024 * 1024);    // 拖动预览（缩放到视口）上限 64MB：24 张目录回拖也要命中
    // 全图解码专用池：最多 2 条并行 = 在飞的旧任务 + 抢占式新目标各一条。
    // 这是「线程数按实测有上限」的落点：再快也不允许无限并发抢 CPU。
    m_decodePool.setMaxThreadCount(2);
    // D2-1：预览快车道单线程。它只做降采样，任务轻；独占一条线程保证
    // 「全量在解」时预览仍能先出，不被重活压在后面。
    m_previewPool.setMaxThreadCount(1);
    m_dragPool.setMaxThreadCount(1);
    // D1-1：缩略图独立小池 2 线程，与全图/拖动池互不挤占。
    m_thumbPool.setMaxThreadCount(2);
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
}

ImageCache::~ImageCache()
{
    m_decodePool.waitForDone();
    m_previewPool.waitForDone();
    m_dragPool.waitForDone();
    m_thumbPool.waitForDone();
}

void ImageCache::setFullCacheLimit(int mb)
{
    // 与 Config::cacheSizeMb() 同一夹取口径（64..4096）：设置页数值框、命令行
    // 与验收探针都走这里，越界值按边界生效，运行态不会出现第三套范围（批次04 #22）。
    m_limitMb = qBound(64, mb, 4096);
    m_fullCache.setMaxCost(qint64(m_limitMb) * 1024 * 1024);
    probeLog(QStringLiteral("cache_limit mb=%1 used=%2 limit_bytes=%3")
                 .arg(m_limitMb)
                 .arg(m_fullCache.totalCost())
                 .arg(qint64(m_limitMb) * 1024 * 1024));
}

void ImageCache::probeLog(const QString &line) const
{
    if (!qEnvironmentVariableIsSet("SOLEVIEW_IMAGEPROC_PROBE")
        && !qEnvironmentVariableIsSet("QUIKVIEW_IMAGEPROC_PROBE")) {
        return;
    }
    QFile f(QStringLiteral("Temp/imageproc-live-probe.txt"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    f.write(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
                .toUtf8());
    f.write(" ");
    f.write(line.toUtf8());
    f.write("\n");
}

QImage ImageCache::cachedFull(const QString &path) const
{
    return cacheFresh(path) ? *m_fullCache.object(path) : QImage();
}

void ImageCache::removeFullEntry(const QString &path)
{
    m_fullCache.remove(path);
    m_fullMeta.remove(path);
}

bool ImageCache::cacheFresh(const QString &path) const
{
    if (!m_fullCache.contains(path)) {
        return false;
    }
    const auto it = m_fullMeta.constFind(path);
    if (it == m_fullMeta.constEnd()) {
        // 无元数据的旧条目：不作沿用，交出去重做（const 方法内不就地删，调用方按需 remove）。
        return false;
    }
    const FrameMeta &meta = it.value();
    // 选项维度：帧是按某档 ICC / tone map 准备的，开关变了旧帧不得复用。
    if (meta.imageIcc != Config::instance().imageIcc()
        || meta.hdrTonemap != Config::instance().hdrTonemap()) {
        return false;
    }
    // 图源内容维度：包内条目不是独立磁盘文件，mtime/size 不参与比对（归档整体新鲜度
    // 由 forgetPath / evictArchive 管）。普通文件比对最后修改时间与字节数，同路径被
    // 外部覆盖后旧帧立即作废。
    if (!ArchiveStore::isEntryPath(path)) {
        const QFileInfo fi(path);
        if (!fi.exists() || fi.lastModified().toSecsSinceEpoch() != meta.mtimeSec
            || fi.size() != meta.sizeBytes) {
            return false;
        }
    }
    return true;
}

void ImageCache::bumpGeneration()
{
    // 先取消旧令牌：在飞任务在下一个检查点直接放弃剩余重活；再换新令牌给
    // 之后提交的任务。代次只决定「结果要不要」，取消标志决定「还要不要继续解」。
    if (m_cancelToken) {
        m_cancelToken->store(true);
    }
    m_cancelToken = std::make_shared<std::atomic<bool>>(false);
    m_generation.fetch_add(1);
    m_fullEmitted.clear();
}

void ImageCache::requestFull(const QString &path, const QSize &previewSize, bool preempt)
{
    m_previewSize = previewSize;
    m_lastRequestedPath = path;
    const bool fresh = cacheFresh(path);
    if (fresh)
        ++m_hits;
    else {
        ++m_misses;
        if (m_fullCache.contains(path))
            removeFullEntry(path);   // 旧帧（文件覆盖/开关变更）作废重做
    }
    probeLog(QStringLiteral("request file=%1 hit=%2 misses=%3 used=%4")
                 .arg(QFileInfo(path).fileName())
                 .arg(fresh ? 1 : 0)
                 .arg(m_misses)
                 .arg(m_fullCache.totalCost()));
    if (preempt) {
        // 显式跳转：进入新代次。此刻之前提交的所有结果只入缓存、不再上屏。
        bumpGeneration();
    }
    // 「不打开 RAW 图片文件」= 请求这一层就拒：缓存里有（预读早就解过的）也必须
    // 拿掉，否则从邻居预读切过去会直接命中缓存、绕开下面所有解码守卫，开关看着像
    // 没生效。这里只拦 RAW，JPEG/PNG 等照旧；与解码线程里的那道守卫同一口径。
    if (isRawFilePath(path) && Config::instance().noRawLoad()) {
        removeFullEntry(path);
        if (path == m_preloadPath) {
            m_preloadPath.clear();
            m_preloadPromote = false;
        }
        QMetaObject::invokeMethod(this, [this, path]() {
            emit loadFailed(path,
                            QCoreApplication::translate("ImageCache", "无法解码"),
                            LoadErrorCategory::None);
        }, Qt::QueuedConnection);
        return;
    }
    if (path == m_preloadPath) {
        // 预读正在解这一页：升级为「要上屏」，解完直接按序发出，不另起任务
        // （否则同一页两个解码，先到的可能是后一页，画面会回跳）
        m_preloadPromote = true;
        m_preloadGeneration = m_generation.load();   // 认领为当前代次，结果可上屏
        startPreviewDecode(path, previewSize, m_generation.load(), m_cancelToken);
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
    if (m_fullInFlight.contains(path) && m_fullInFlight.value(path) == m_generation.load()) {
        return;   // 正在解
    }
    if (m_queue.contains(path)) {
        // 已在队列：若这次是显式跳转，把它的优先级提上来（后到的跳转是用户最新意图）。
        if (preempt) {
            m_queue.remove(path);
            m_queue.push(path, true, ++m_queueOrder);
        }
        return;
    }
    const bool busy = hasCurrentGenWork() || !m_queue.isEmpty();
    if (fresh && !busy) {
        // 空闲时缓存命中直接上屏；忙时也必须排队，否则会插队破坏
        // 「emit 顺序 == requestFull 调用顺序」
        m_fullEmitted.insert(path);
        emit fullReady(path, *m_fullCache.object(path), false);
        return;
    }
    if (!fresh) {
        // D2-1：全量之前先让预览快车道出一张降采样帧。预览只写自己的在飞集合，
        // 不占全量队列；全量先到也没关系，finishPreview 会按 m_fullEmitted 丢弃。
        // 顺序翻页（不换代次）时，只有队列与在飞都空才允许抢跑：否则上一页的
        // 全量/下一页的预览到达顺序可能颠倒，画面会来回跳。显式跳转已换代次，
        // 旧结果全部作废，可以立刻发预览。
        const bool previewSafe = preempt || (m_queue.isEmpty() && !hasCurrentGenWork());
        if (previewSafe) {
            startPreviewDecode(path, previewSize, m_generation.load(), m_cancelToken);
        }
    }
    m_queue.push(path, preempt, ++m_queueOrder);
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
    // P0-d：记录该就绪帧生成时的图源新鲜度与选项快照，供命中时校验防错帧。
    FrameMeta meta;
    meta.imageIcc = Config::instance().imageIcc();
    meta.hdrTonemap = Config::instance().hdrTonemap();
    if (!ArchiveStore::isEntryPath(path)) {
        const QFileInfo fi(path);
        meta.mtimeSec = fi.lastModified().toSecsSinceEpoch();
        meta.sizeBytes = fi.size();
    }
    m_fullMeta.insert(path, meta);
    probeLog(QStringLiteral("cache_insert file=%1 bytes=%2 used=%3 limit_mb=%4")
                 .arg(QFileInfo(path).fileName())
                 .arg(img.sizeInBytes())
                 .arg(m_fullCache.totalCost())
                 .arg(m_limitMb));;
}

void ImageCache::preload(const QString &path)
{
    if (path.isEmpty() || cacheFresh(path) || m_queue.contains(path)
        || m_fullInFlight.contains(path) || path == m_preloadPath
        || m_preloadQueue.contains(path)) {
        probeLog(QStringLiteral("preload_skip file=%1 reason=hit_or_pending")
                     .arg(QFileInfo(path).fileName()));
        return;
    }
    probeLog(QStringLiteral("preload_req file=%1").arg(QFileInfo(path).fileName()));
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
    m_preloadGeneration = m_generation.load();
    ++m_preloadStarts;
    probeLog(QStringLiteral("preload_start file=%1 starts=%2")
                 .arg(QFileInfo(path).fileName())
                 .arg(m_preloadStarts));
    QtConcurrent::run(&m_decodePool, [this, path]() {
        QImage img;
        if (ArchiveStore::isEntryPath(path)) {
            // 包内条目：解压到内存再解码，解完即弃
            QString ignored;
            img = ArchiveStore::decode(path, QSize(), &ignored);
        } else if (PsdLoader::isPsdPath(path)) {
            img = PsdLoader::load(path, false).composite;
        } else if (SvgLoader::isSvgPath(path)) {
            // 预读不接矢量：SVG 必须按视口尺寸栅格化，预读时拿不到视口宽度，
            // 存一张「自带尺寸」的位图，fit 查看时会被放大成糊图（图标型 SVG
            // 尤其明显，16×16 的标志放大会糊），而 SvgLoader 单张只要几十毫秒，
            // 省这点时间不值。这里不解，走「空结果」让预读队列自然跳过这张。
        } else if (ExtraImageLoader::isExtraPath(path)) {
            if (!ExtraImageLoader::isVectorExtraPath(path)) {
                img = ExtraImageLoader::load(path);
            }
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
                    m_fullEmitted.insert(path);
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
    if (cacheFresh(path)) {
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
    QtConcurrent::run(&m_dragPool, [this, path, size]() {
        QImage img;
        if (ArchiveStore::isEntryPath(path)) {
            QString ignored;
            img = ArchiveStore::decode(path, size, &ignored);
        } else if (SvgLoader::isSvgPath(path)) {
            // 拖动跟手预览：按视口盒缩到不大于目标（矢量只缩不放）
            QString ignored;
            img = SvgLoader::render(path, size, false, &ignored);
        } else if (ExtraImageLoader::isExtraPath(path)) {
            QString ignored;
            img = ExtraImageLoader::load(path, size, &ignored);
            if (!img.isNull() && size.isValid()
                && (img.width() > size.width() || img.height() > size.height())) {
                img = img.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
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
                if (promote && !cacheFresh(path)) {
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
        const QString path = m_queue.takeNext();
        if (cacheFresh(path)) {
            // 排队期间被缓存命中（如先前的解码完成入缓存）：按序直接上屏
            m_fullEmitted.insert(path);
            emit fullReady(path, *m_fullCache.object(path), false);
            continue;
        }
        PerfProbe::mark(PerfProbe::currentSeq(), "decode-start", "");
        startFullDecode(path, m_previewSize, m_generation.load());
        return;
    }
}

// 「当前代次是否还有活」：旧代次的在飞任务不算，它们的输出已注定被丢弃。
// 单飞语义只对同代次成立——连按（同代）仍然逐张排队，跳转（换代）允许并行。
bool ImageCache::hasCurrentGenWork() const
{
    for (auto it = m_fullInFlight.constBegin(); it != m_fullInFlight.constEnd(); ++it) {
        if (it.value() == m_generation.load()) {
            return true;
        }
    }
    return !m_preloadPath.isEmpty() && m_preloadGeneration == m_generation.load();
}

void ImageCache::startPreviewDecode(const QString &path, const QSize &previewSize,
                                    quint64 generation,
                                    const std::shared_ptr<std::atomic<bool>> &cancel)
{
    if (path.isEmpty() || !previewSize.isValid() || !cancel) {
        return;
    }
    // 提交时在 GUI 线程读一次配置（跨线程读 QSettings 不安全），与全量车道同口径。
    const bool previewLive = m_previewLiveKnown.load();
    const bool previewOn = previewLive ? m_previewLiveOn.load()
                                       : Config::instance().previewMode();
    if (!previewOn) {
        return;
    }
    const bool noRaw = Config::instance().noRawLoad();
    const bool rawThumb = Config::instance().rawThumbnail();
    if (isRawFilePath(path) && (noRaw || !rawThumb)) {
        return;
    }
    // 同一代次内去重；旧代次的在飞条目允许被新代次覆盖（A→B→A 快速回跳）。
    if (m_previewInFlight.value(path, 0) == generation) {
        return;
    }
    m_previewInFlight.insert(path, generation);

    QtConcurrent::run(&m_previewPool, [this, path, previewSize, generation, cancel,
                                       rawThumb]() {
        const auto stale = [this, generation, cancel]() {
            return cancel->load() || generation != m_generation.load();
        };
        QImage img;
        if (ArchiveStore::isEntryPath(path)) {
            if (!stale()) {
                QString ignored;
                img = ArchiveStore::decode(path, previewSize, &ignored);
            }
        } else if (PsdLoader::isPsdPath(path) || SvgLoader::isSvgPath(path)) {
            // PSD/SVG 没有「按比例读一帧」的廉价路径：全量车道本身就是一次
            // 合成/栅格化，再解一遍预览只会翻倍耗时。
        } else if (ExtraImageLoader::isExtraPath(path)) {
            if (!stale()) {
                img = scaledReaderPreview(path, previewSize);
            }
        } else if (isRawFilePath(path)) {
            // RAW 内嵌缩略图通常几十毫秒：先顶住首屏，全尺寸在重车道继续解。
            if (!stale() && rawThumb) {
                img = loadRawThumbnail(path);
            }
        } else if (!stale()) {
            img = scaledReaderPreview(path, previewSize);
        }
        // 取消后也回主线程清在飞标记；img 为空时 finishPreview 只做清理。
        QMetaObject::invokeMethod(this, [this, path, img, generation]() {
            finishPreview(path, img, generation);
        }, Qt::QueuedConnection);
    });
}

void ImageCache::finishPreview(const QString &path, const QImage &img, quint64 generation)
{
    if (m_previewInFlight.value(path, 0) == generation) {
        m_previewInFlight.remove(path);
    }
    if (img.isNull() || generation != m_generation.load()) {
        return;
    }
    // 顺序翻页不换代次：只允许「最新目标」的预览落屏，旧页预览晚到不能盖回去。
    if (path != m_lastRequestedPath) {
        return;
    }
    if (m_fullEmitted.contains(path) || cacheFresh(path)) {
        return;
    }
    emit previewReady(path, img);
}

void ImageCache::setPreviewModeLive(bool known, bool on)
{
    // 主线程写入、解码线程读取：只改两个原子量，不碰队列与缓存。
    m_previewLiveKnown.store(known);
    m_previewLiveOn.store(on);
}

void ImageCache::startFullDecode(const QString &path, const QSize &previewSize,
                                 quint64 generation)
{
    // 本车道只解全量；预览快帧由独立预览池先发（D2-1），这里不再重复解一遍。
    const bool noRaw = Config::instance().noRawLoad();
    const bool hdrTonemap = Config::instance().hdrTonemap();
    const auto cancel = m_cancelToken;
    // P0-a：ICC→sRGB / HDR tone map 随解码在后台线程完成，产出显示就绪帧。
    // 选项在提交任务的 GUI 线程取一次快照带入 worker，worker 不读 QSettings。
    const bool imageIcc = Config::instance().imageIcc();
    m_fullInFlight.insert(path, generation);
    QtConcurrent::run(&m_decodePool, [this, path, previewSize, generation, noRaw,
                                      hdrTonemap, imageIcc, cancel]() {
        // 后台帧准备：与基线主线程旧路径同一操作、同一顺序（先 toneMap 再 ICC→sRGB）。
        const DisplayFrame::PrepareOptions frameOpt{imageIcc, hdrTonemap};
        auto prepareFrame = [&frameOpt](const QImage &im) {
            return DisplayFrame::prepare(im, frameOpt);
        };
        // D1-5 / D2-2：取消令牌 + 代次双判。令牌在换代时立刻置真，worker 在进入
        // 每一道重活之前先看它：已被作废的任务不再继续解码与后处理，结果回主线程
        // 由 finishFullDecode 按 stale 静默丢弃（不上屏、不发 loadFailed）。
        const auto stillCurrent = [this, generation, cancel]() {
            return !cancel->load() && generation == m_generation.load();
        };
        const auto abandon = [this, path, generation]() {
            QMetaObject::invokeMethod(this, [this, path, generation]() {
                finishFullDecode(path, QImage(), generation, QString(),
                                 LoadErrorCategory::None);
            }, Qt::QueuedConnection);
        };
        // 包内条目（.zip/.cbz 里的文件）：解压进内存 → QImageReader 从内存流解码。
        // 解码完的字节即弃，全程不落盘（派单 §二.3 / §四.5 的内存峰值就落在这两条）。
        if (ArchiveStore::isEntryPath(path)) {
            if (!stillCurrent()) {
                abandon();
                return;
            }
            QString entryError;
            LoadErrorCategory entryCategory = LoadErrorCategory::None;
            const QImage img = prepareFrame(ArchiveStore::decode(path, QSize(), &entryError, &entryCategory));
            QMetaObject::invokeMethod(this, [this, path, img, entryError, entryCategory,
                                             generation]() {
                finishFullDecode(path, img, generation, entryError, entryCategory);
            }, Qt::QueuedConnection);
            return;
        }
        // PSD 走 PsdLoader（QImageReader 无 PSD 插件）：用文件自带的原始合成预览，
        // 不逐层重合成，避免用近似结果冒充原文件效果。
        if (PsdLoader::isPsdPath(path)) {
            if (!stillCurrent()) {
                abandon();
                return;
            }
            const PsdDocument psdDoc = PsdLoader::load(path, false);
            const QImage img = prepareFrame(psdDoc.composite);
            const QString err = psdDoc.error;   // 例如「ZIP 压缩的 PSD 暂不支持」
            // 类别由 PsdLoader 判定（含 PSD 专属的 NoCompatPreview），UI 只取文案
            const LoadErrorCategory cat = psdDoc.errorCategory;
            QMetaObject::invokeMethod(this, [this, path, img, err, cat, generation]() {
                finishFullDecode(path, img, generation, err, cat);
            }, Qt::QueuedConnection);
            return;
        }
        // SVG 走 SvgLoader（QSvgRenderer 栅格化，不依赖 imageformats/qsvg 插件）：
        // 矢量没有「按比例解码」的概念，一次渲染到「不小于视口、且有上限」的栅格，
        // 因此不走下面的快帧两级；失败类别由 loader 层定性（坏文件＝Corrupt、
        // 后缀是 svg 但内容不是 SVG 文本＝Unsupported）。
        if (SvgLoader::isSvgPath(path)) {
            if (!stillCurrent()) {
                abandon();
                return;
            }
            QString svgErr;
            LoadErrorCategory svgCat = LoadErrorCategory::None;
            const QImage img = prepareFrame(SvgLoader::render(path, previewSize, true, &svgErr, &svgCat));
            QMetaObject::invokeMethod(this, [this, path, img, svgErr, svgCat, generation]() {
                finishFullDecode(path, img, generation, svgErr, svgCat);
            }, Qt::QueuedConnection);
            return;
        }
        if (ExtraImageLoader::isExtraPath(path)) {
            // 降采样快帧已由预览快车道负责（D2-1）：本车道只解全量，避免同一张图
            // 在两条线程里各读一遍。插件不支持缩放时预览池会静默无输出。
            if (!stillCurrent()) {
                abandon();
                return;
            }
            QString extraErr;
            LoadErrorCategory extraCat = LoadErrorCategory::None;
            const QImage img = prepareFrame(ExtraImageLoader::load(path, previewSize, &extraErr, &extraCat, hdrTonemap, m_hdrEv));
            QMetaObject::invokeMethod(this, [this, path, img, extraErr, extraCat, generation]() {
                finishFullDecode(path, img, generation, extraErr, extraCat);
            }, Qt::QueuedConnection);
            return;
        }
        const bool rawFile = isRawFilePath(path);
        // 「不打开 RAW 图片文件」必须在最前面短路：NEF/CR2/DNG 本身就是 TIFF 容器，
        // Qt 的 tiff 插件按内容嗅探能读出内嵌预览（常见 160×120），先前的守卫只挡
        // libraw，结果开关打开仍然上屏，等于没关。这里不给任何解码路径留口子，
        // 直接按失败回主线程 → onLoadFailed 给「已在设置中禁用 RAW 解码」。
        if (rawFile && noRaw) {
            QMetaObject::invokeMethod(this, [this, path, generation]() {
                finishFullDecode(path, QImage(), generation, QString(),
                                 LoadErrorCategory::None);
            }, Qt::QueuedConnection);
            return;
        }
        // RAW 内嵌缩略图由预览快车道先发（D2-1）；本车道只做全尺寸解码。
        // RAW 不走通用 QImageReader：NEF/CR2/DNG 本身就是 TIFF 容器，Qt 的 tiff
        // 插件会按内容嗅探读出内嵌预览（Nikon D1 是 160×120）并「成功」返回，
        // libraw 全尺寸解码就永远轮不到，画面一直停在小缩略图。缩略图这一级由
        // 预览池负责，这里只做全尺寸解码，职责分开。
        if (!stillCurrent()) {
            abandon();
            return;
        }
        QImage rawImg;
        QString readerError;
        if (!rawFile) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            rawImg = reader.read();
            if (rawImg.isNull()) {
                const QByteArray formats = QImageReader::supportedImageFormats().join(',');
                readerError = QStringLiteral("QImageReader: %1; format=%2; supported=%3")
                                  .arg(reader.errorString(),
                                       QString::fromLatin1(reader.format()),
                                       formats);
            }
        }
        // RAW 全尺寸解码同样留在本后台线程，避免回主线程同步解把窗口卡住；
        // 「不打开 RAW」时 requestFull 已拦截，这里是并发/在飞任务的兜底。
        if (rawFile && !noRaw) {
            QString rawErr;
            rawImg = loadRawImage(path, &rawErr, hdrTonemap);
            if (rawImg.isNull() && !rawErr.isEmpty())
                readerError = rawErr;   // RAW 自己的失败原因比一句「无法解码」有用
        }
        // 全图解码完成：后台一次性做完 ICC→sRGB / tone map，就绪帧再回主线程。
        const QImage img = prepareFrame(rawImg);
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
    PerfProbe::mark(PerfProbe::currentSeq(), "decode-full", "");
    // 唯一的上屏判据：结果仍属于当前代次。显式跳转在 cancelPending 里自增代次，
    // 「跳走后旧图才解完」自然被拦下；不再依赖会被多次跳转互相覆盖的布尔标志。
    const bool stale = (generation != m_generation.load());
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
            // FIFO：按入队顺序无条件上屏（显式跳转的在飞旧结果除外）。
            // 记下「本代次已发全量」，迟到的预览帧据此丢弃（D2-1）。
            m_fullEmitted.insert(path);
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
    // 进入新代次：在飞任务收到取消令牌后会在下一个检查点放弃剩余重活；即使
    // loader 内部停不下来，结果到达时也会因代次不符只入缓存不上屏（A→B→A 往返
    // 靠代次区分「哪一代的 A」）。
    bumpGeneration();
    m_queue.clear();
    // 旧页面的邻居预读在新位置已经没意义：待预读清掉
    m_preloadQueue.clear();
    m_preloadPromote = false;
    // 拖动预览的旧落点同理：在飞结果只入缓存，别把松手后的新页面盖回去
    m_dragPending.clear();
    m_dragMuted = true;
    // 预览在飞标记按代次清理：新目标同路径时可以立刻重新发预览。
    m_previewInFlight.clear();
}

// 同名文件在外部被删掉后又保存回来：磁盘上已经是新内容，缓存里的旧图必须作废。
// 清缓存不够——在飞的解码/预读/缩略图还会把旧内容塞回来，所以：
//   ① m_minGen 给该路径立代次门限，早于门限的解码结果不得进缓存；
//   ② 在飞全图任务直接从 m_fullInFlight 摘除，完成回调找不到条目即整体丢弃；
//   ③ 预读/拖动预览命中同一路径时一并清空，回调按「路径不符」丢弃；
//   ④ 缩略图缓存按 key 前缀清掉，在飞缩略图用 epoch 作废。
void ImageCache::setHdrExposureEv(float ev)
{
    ev = qBound(-8.0f, ev, 8.0f);
    if (qFuzzyCompare(ev + 1.0f, m_hdrEv + 1.0f)) {
        return;
    }
    m_hdrEv = ev;
    // 曝光是解码期参数：缓存里的 HDR/EXR 全部作废（缩略图同一套曲线，一并清），
    // 在飞任务由调用方的换代请求作废。8bit 图重解一次代价可接受（EV 调整不频繁）。
    clear();
}

void ImageCache::forgetPath(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    bumpGeneration();
    m_minGen.insert(path, m_generation.load());

    removeFullEntry(path);
    m_dragCache.remove(path);
    m_queue.remove(path);
    m_preloadQueue.removeAll(path);
    m_previewInFlight.remove(path);
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
    const QList<QString> displayKeys = m_thumbDisplay.keys();
    for (const QString &key : displayKeys) {
        if (key.startsWith(prefix)) {
            m_thumbDisplay.remove(key);
        }
    }
    for (int i = m_thumbQueue.size() - 1; i >= 0; --i) {
        if (m_thumbQueue.at(i).path == path) {
            m_thumbQueue.removeAt(i);
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

void ImageCache::requestThumbnail(const QString &path, const QSize &size,
                                  ThumbPriority priority)
{
    if (path.isEmpty() || size.isEmpty()) {
        return;
    }
    const QString key = path + QLatin1Char('@') + QString::number(size.width())
                        + QLatin1Char('x') + QString::number(size.height());
    // D2-3 L2：已是上屏格式，直接发。
    if (m_thumbDisplay.contains(key)) {
        emit thumbnailReady(path, *m_thumbDisplay.object(key));
        return;
    }
    // D2-3 L1：有缩放原图，只做一次格式转换，不重新解码。
    if (m_thumbCache.contains(key)) {
        const QImage display = toDisplayFormat(*m_thumbCache.object(key));
        m_thumbDisplay.insert(key, new QImage(display), qMax(1, imageCostBytes(display)));
        emit thumbnailReady(path, display);
        return;
    }
    if (m_thumbInFlight.contains(key)) {
        return;
    }
    // 已在队列：本次是 Visible 就把优先级提上来，不重复入队。
    for (int i = 0; i < m_thumbQueue.size(); ++i) {
        if (m_thumbQueue.at(i).key == key) {
            if (priority == ThumbPriority::Visible) {
                m_thumbQueue[i].priority = ThumbPriority::Visible;
            }
            return;
        }
    }
    m_thumbQueue.append({path, key, size, priority, ++m_thumbOrder});
    startThumbJobs();
}

// D2-3：缩略图调度。池仍是 2 线程，但不再「请求即提交」——先按优先级排，
// 当前可见格（Visible）压在预取（Prefetch）前面；在飞上限 = 池线程数。
void ImageCache::startThumbJobs()
{
    constexpr int kMaxActive = 2;
    while (m_thumbActive < kMaxActive && !m_thumbQueue.isEmpty()) {
        int best = 0;
        for (int i = 1; i < m_thumbQueue.size(); ++i) {
            const ThumbTask &a = m_thumbQueue.at(i);
            const ThumbTask &b = m_thumbQueue.at(best);
            if (a.priority != b.priority) {
                if (a.priority == ThumbPriority::Visible) {
                    best = i;
                }
            } else if (a.order < b.order) {
                best = i;
            }
        }
        const ThumbTask task = m_thumbQueue.takeAt(best);
        if (m_thumbDisplay.contains(task.key)) {
            emit thumbnailReady(task.path, *m_thumbDisplay.object(task.key));
            continue;
        }
        if (m_thumbCache.contains(task.key)) {
            const QImage display = toDisplayFormat(*m_thumbCache.object(task.key));
            m_thumbDisplay.insert(task.key, new QImage(display),
                                  qMax(1, imageCostBytes(display)));
            emit thumbnailReady(task.path, display);
            continue;
        }
        if (m_thumbInFlight.contains(task.key)) {
            continue;
        }
        m_thumbInFlight.insert(task.key);
        ++m_thumbActive;
        const quint64 epoch = m_thumbEpoch.value(task.path);
        const QString path = task.path;
        const QString key = task.key;
        const QSize size = task.size;

        // D1-1：缩略图投递到独立小池，不与全图/拖动解码抢线程。
        const float hdrEv = m_hdrEv;
        auto future = QtConcurrent::run(&m_thumbPool, [path, size, hdrEv]() {
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
            if (SvgLoader::isSvgPath(path)) {
                // 缩略图/胶片栏/总览网格：直接按请求盒栅格化（矢量按目标尺寸渲染，
                // 比「先渲原尺寸再缩」既快又不糊）
                QString ignored;
                return SvgLoader::render(path, size, false, &ignored);
            }
            if (ExtraImageLoader::isExtraPath(path)) {
                QString ignored;
                QImage thumb = ExtraImageLoader::load(path, size, &ignored, nullptr, false, hdrEv);
                if (!thumb.isNull() && (thumb.width() > size.width() || thumb.height() > size.height())) {
                    thumb = thumb.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
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
        connect(watcher, &QFutureWatcher<QImage>::finished, this,
                [this, path, key, epoch, watcher]() {
            m_thumbInFlight.remove(key);
            --m_thumbActive;
            const QImage img = watcher->result();
            watcher->deleteLater();
            // 解码期间文件被同名新内容替换（forgetPath）：这张旧缩略图不许进缓存，
            // 也不许发出去覆盖已经重建的胶片栏；重新可见时自然会重新请求。
            if (m_thumbEpoch.value(path) == epoch) {
                if (!img.isNull()) {
                    m_thumbCache.insert(key, new QImage(img), qMax(1, imageCostBytes(img)));
                    const QImage display = toDisplayFormat(img);
                    m_thumbDisplay.insert(key, new QImage(display),
                                          qMax(1, imageCostBytes(display)));
                    emit thumbnailReady(path, display);
                } else {
                    emit thumbnailReady(path, QImage());
                }
            }
            startThumbJobs();
        });
        watcher->setFuture(future);
    }
}

void ImageCache::clear()
{
    m_fullCache.clear();
    m_fullMeta.clear();
    m_thumbCache.clear();
    m_thumbDisplay.clear();
    m_thumbQueue.clear();
    m_thumbInFlight.clear();
}

void ImageCache::clearFull()
{
    m_fullInFlight.clear();
    bumpGeneration();   // 已清掉的任务即使回来也不再被认作当前结果
    m_fullCache.clear();
    m_fullMeta.clear();
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
    m_previewInFlight.clear();
    m_lastRequestedPath.clear();
}

// D1-4 切目录回收：按目录前缀淘汰上一目录的全图条目，拖动预览缓存整路清空（旧落点
// 的跟手预览不该占着新目录的字节预算）。缩略图缓存保留——按路径隔离、体积小，跨
// 目录回翻仍可命中。沿用 QCache 字节成本 LRU，这里只做定向移除，不改淘汰策略。
void ImageCache::evictDirectory(const QString &dirPath)
{
    if (dirPath.isEmpty()) {
        return;
    }
    evictPrefix(QFileInfo(dirPath).absoluteFilePath() + QLatin1Char('/'));
}

// D1-4 补：包内条目路径 = 包文件绝对路径 + '/' + 条目名，不属于任何磁盘目录，
// evictDirectory 的目录前缀覆盖不到。离开一个包时按包路径前缀整包淘汰。
void ImageCache::evictArchive(const QString &archivePath)
{
    if (archivePath.isEmpty()) {
        return;
    }
    evictPrefix(QFileInfo(archivePath).absoluteFilePath() + QLatin1Char('/'));
}

// 前缀淘汰的唯一实现。除了缓存，还要处理「还在路上」的三类任务：
//   ① 待解队列/预读队列：直接移除；
//   ② 在飞全图：从 m_fullInFlight 摘除（回来找不到条目即丢弃），并给该路径立
//      「下一代次」门限——防止用户快速切回同一路径时，旧任务按「同路径被重新
//      提交」的分支把淘汰掉的内容插回缓存；
//   ③ 拖动预览在飞/待解：清空路径，回来按路径不符丢弃。
void ImageCache::evictPrefix(const QString &prefix)
{
    if (prefix.isEmpty()) {
        return;
    }
    m_dragCache.clear();
    // 旧目标的预览不再有落屏资格：淘汰后到下一次 requestFull 之间到达的结果全部丢弃。
    m_lastRequestedPath.clear();

    int removed = 0;
    const QList<QString> keys = m_fullCache.keys();
    for (const QString &key : keys) {
        if (key.startsWith(prefix, Qt::CaseInsensitive)) {
            removeFullEntry(key);
            ++removed;
        }
    }

    const QStringList queued = m_queue.paths();
    for (const QString &path : queued) {
        if (path.startsWith(prefix, Qt::CaseInsensitive)) {
            m_queue.remove(path);
        }
    }
    for (int i = m_preloadQueue.size() - 1; i >= 0; --i) {
        if (m_preloadQueue.at(i).startsWith(prefix, Qt::CaseInsensitive)) {
            m_preloadQueue.removeAt(i);
        }
    }
    if (m_preloadPath.startsWith(prefix, Qt::CaseInsensitive)) {
        m_preloadPath.clear();
        m_preloadPromote = false;
    }
    if (m_dragInFlight.startsWith(prefix, Qt::CaseInsensitive)) {
        m_dragInFlight.clear();
    }
    if (m_dragPending.startsWith(prefix, Qt::CaseInsensitive)) {
        m_dragPending.clear();
    }

    QList<QString> inFlight;
    for (auto it = m_fullInFlight.constBegin(); it != m_fullInFlight.constEnd(); ++it) {
        if (it.key().startsWith(prefix, Qt::CaseInsensitive)) {
            inFlight.append(it.key());
        }
    }
    const quint64 floor = m_generation.load() + 1;
    for (const QString &path : inFlight) {
        m_fullInFlight.remove(path);
        m_previewInFlight.remove(path);
        m_minGen.insert(path, floor);
    }
    probeLog(QStringLiteral("evict prefix=%1 cached=%2 inflight=%3")
                 .arg(prefix)
                 .arg(removed)
                 .arg(inFlight.size()));
}
