#include "ImageProbe.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QRunnable>
#include <QtEndian>

#include "ArchiveStore.h"
#include "ImageFacts.h"

namespace {

// 头部窗口与 ImageFacts 同口径：JPEG 的 APP1(EXIF) 单段上限 64 KB，PNG 的
// IHDR / eXIf 都在最前面；256 KB 足够覆盖「APP1 + APP2(ICC) + SOF」这一段，
// 又不会为了取一个位深把几十 MB 的原图读进内存。
constexpr int kHeaderBytes = 256 * 1024;

constexpr quint16 kTagOrientation = 0x0112;       // IFD0：EXIF 方向
constexpr quint16 kTagThumbOffset = 0x0201;       // IFD1：JPEGInterchangeFormat
constexpr quint16 kTagThumbLength = 0x0202;       // IFD1：JPEGInterchangeFormatLength
constexpr quint32 kTiffMagic = 42;

quint16 rd16(const QByteArray &d, int off, bool le)
{
    if (off < 0 || off + 2 > d.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(d.constData() + off);
    return le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
}

quint32 rd32(const QByteArray &d, int off, bool le)
{
    if (off < 0 || off + 4 > d.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(d.constData() + off);
    return le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
}

quint32 be32(const QByteArray &d, int off)
{
    if (off < 0 || off + 4 > d.size()) {
        return 0;
    }
    return qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(d.constData() + off));
}

// 在 TIFF 块起点上取 IFD0 的 Orientation 与 IFD1 的内嵌缩略图存在性。
// 头部窗口可能从中间截断（大 ICC / 大缩略图），所有偏移都按「窗口内可读」
// 校验，越界即视为未知——坏文件不能把探测线程带崩。
void scanTiff(const QByteArray &d, int base, int *orientation, bool *hasThumb)
{
    if (base < 0 || base + 8 > d.size()) {
        return;
    }
    bool le = true;
    const QByteArray order = d.mid(base, 2);
    if (order == QByteArray("II", 2)) {
        le = true;
    } else if (order == QByteArray("MM", 2)) {
        le = false;
    } else {
        return;
    }
    if (rd16(d, base + 2, le) != kTiffMagic) {
        return;
    }
    // TIFF 内所有偏移都相对 TIFF 起点，不是文件起点。
    const quint64 span = quint64(d.size() - base);
    const quint64 ifd0Rel = rd32(d, base + 4, le);
    if (ifd0Rel < 8 || ifd0Rel + 2 > span) {
        return;
    }
    const int ifd0 = base + int(ifd0Rel);
    const quint16 count = rd16(d, ifd0, le);
    if (ifd0Rel + 2 + quint64(count) * 12 + 4 > span) {
        return;
    }
    for (int i = 0; i < int(count); ++i) {
        const int entry = ifd0 + 2 + i * 12;
        if (rd16(d, entry, le) != kTagOrientation) {
            continue;
        }
        const quint16 type = rd16(d, entry + 2, le);
        const quint32 n = rd32(d, entry + 4, le);
        if (n < 1) {
            continue;
        }
        // SHORT 值内联在值字段前两字节；LONG 直接读四字节。
        quint32 value = 0;
        if (type == 3) {
            value = rd16(d, entry + 8, le);
        } else if (type == 4) {
            value = rd32(d, entry + 8, le);
        }
        if (value >= 1 && value <= 8) {
            *orientation = int(value);
        }
    }
    // IFD0 末尾的四字节是下一个 IFD 的偏移（= IFD1，缩略图那张 IFD）。
    const quint64 ifd1Rel = rd32(d, ifd0 + 2 + int(count) * 12, le);
    if (ifd1Rel == 0 || ifd1Rel + 2 > span) {
        return;
    }
    const int ifd1 = base + int(ifd1Rel);
    const quint16 thumbCount = rd16(d, ifd1, le);
    if (ifd1Rel + 2 + quint64(thumbCount) * 12 > span) {
        return;
    }
    bool hasOffset = false;
    bool hasLength = false;
    for (int i = 0; i < int(thumbCount); ++i) {
        const int entry = ifd1 + 2 + i * 12;
        const quint16 tag = rd16(d, entry, le);
        if (tag == kTagThumbOffset) {
            hasOffset = rd32(d, entry + 8, le) != 0;
        } else if (tag == kTagThumbLength) {
            hasLength = rd32(d, entry + 8, le) != 0;
        }
    }
    // 两个标签都在且非零才算「真的有一张缩略图」。
    *hasThumb = hasOffset && hasLength;
}

// 走 JPEG 段链：APP1(Exif) 取方向 / 缩略图，SOF 取采样精度（= 每通道位深）。
// 只走到 SOS 为止——SOF 之前全是元数据段，不碰像素数据。
void scanJpeg(const QByteArray &d, int *orientation, bool *hasThumb, int *bitDepth)
{
    int pos = 2;   // 跳过 SOI
    while (pos + 4 <= d.size()) {
        if (quint8(d.at(pos)) != 0xFF) {
            return;   // 段链断了：后面的字节不可信，别再猜
        }
        const quint8 marker = quint8(d.at(pos + 1));
        if (marker == 0xDA || marker == 0xD9) {
            return;   // SOS / EOI：不再有元数据段
        }
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD8)) {
            pos += 2;   // TEM / RSTn / SOI 这类标记后面没有长度字段
            continue;
        }
        const int len = (quint8(d.at(pos + 2)) << 8) | quint8(d.at(pos + 3));
        if (len < 2 || pos + 2 + len > d.size()) {
            return;
        }
        if (marker == 0xE1 && len >= 8 && d.mid(pos + 4, 6) == QByteArray("Exif\0\0", 6)) {
            scanTiff(d, pos + 10, orientation, hasThumb);   // 段头(4) + "Exif\0\0"(6)
        } else if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8
                   && marker != 0xCC) {
            // SOF：段长(2) 之后第一个字节就是采样精度（8 / 12 / 16）
            if (len >= 3) {
                *bitDepth = quint8(d.at(pos + 4));
            }
        }
        pos += 2 + len;
    }
}

// PNG：IHDR 定 位深与颜色类型，tRNS 也算有 alpha，eXIf 里可能有方向。
void scanPng(const QByteArray &d, int *bitDepth, bool *hasAlpha,
             int *orientation, bool *hasThumb)
{
    // 布局：8 字节签名 + 4 长度 + "IHDR" + 宽(4) + 高(4) + 位深(1) + 颜色类型(1)
    if (d.size() < 26 || d.mid(12, 4) != QByteArray("IHDR", 4)) {
        return;
    }
    const int depth = quint8(d.at(24));
    const int colorType = quint8(d.at(25));
    if (depth >= 1 && depth <= 16) {
        *bitDepth = depth;
    }
    *hasAlpha = colorType == 4 || colorType == 6;   // 灰度+alpha / 真彩+alpha
    int pos = 8;
    while (pos + 8 <= d.size()) {
        const quint32 len = be32(d, pos);
        const QByteArray type = d.mid(pos + 4, 4);
        if (type == QByteArray("IDAT", 4) || type == QByteArray("IEND", 4)) {
            break;   // 像素数据之后不用看
        }
        if (type == QByteArray("tRNS", 4)) {
            *hasAlpha = true;   // 调色板 / 灰度图用 tRNS 补透明色
        } else if (type == QByteArray("eXIf", 4)) {
            scanTiff(d, pos + 8, orientation, hasThumb);
        }
        if (quint64(pos) + 12 + quint64(len) > quint64(d.size())) {
            break;   // 头部窗口截断（iCCP 这类大块）就到此为止
        }
        pos += 12 + int(len);   // 长度(4) + 类型(4) + 数据 + CRC(4)
    }
}

// WebP：VP8X 的标志位直接给出 alpha；EXIF 块里可能有方向。
void scanWebp(const QByteArray &d, bool *hasAlpha, int *orientation, bool *hasThumb)
{
    if (d.size() < 21) {
        return;
    }
    // RIFF(4) + size(4) + "WEBP"(4) + 块类型(4) + 块长(4) + 载荷首字节
    if (d.mid(12, 4) == QByteArray("VP8X", 4)) {
        *hasAlpha = (quint8(d.at(20)) & 0x10) != 0;
    }
    const int exif = d.indexOf(QByteArray("EXIF", 4));
    if (exif > 0) {
        scanTiff(d, exif + 8, orientation, hasThumb);
    }
}

// 格式归类：QImageReader 给的是 "jpeg" / "tiff" 这类规范名，但后缀兜底时可能是
// "jpg" / "tif"，按族归一后再分派解析器。
QString formatFamily(const QString &format)
{
    const QString f = format.toLower();
    if (f == QLatin1String("jpg") || f == QLatin1String("jpeg") || f == QLatin1String("jpe")) {
        return QStringLiteral("jpeg");
    }
    if (f == QLatin1String("tif") || f == QLatin1String("tiff") || f == QLatin1String("cr2")) {
        return QStringLiteral("tiff");
    }
    return f;
}

// mtime 毫秒：文件不存在 / 时间无效统一当 0，让「取不到时间」这件事本身稳定，
// 不会因为每次 stat 都返回不同的无效值而反复重探。
qint64 mtimeMsOf(const QFileInfo &info)
{
    const QDateTime modified = info.lastModified();
    return modified.isValid() ? modified.toMSecsSinceEpoch() : 0;
}

} // namespace

// 工作线程里的全部工作：只读文件头，不碰 QObject，失败也给一个空结果。
ImageProbeResult ImageProbe::probeFile(const QString &path)
{
    ImageProbeResult result;
    result.format = QFileInfo(path).suffix().toLower();

    // 包内条目是虚拟路径，磁盘上不存在：要拿尺寸只能把整张图解出来，
    // 代价远超一次分组整理，所以直接给空结果（format 仍取条目后缀），
    // 绝不在探测阶段解压整图。
    if (ArchiveStore::isEntryPath(path)) {
        return result;
    }

    // 尺寸与格式：QImageReader 只读文件头，不解码像素；autoTransform 关掉，
    // 因为我们要的是「文件里记录的方向」而不是「按方向旋转后的尺寸」。
    QImageReader reader(path);
    reader.setAutoTransform(false);
    const QString format = QString::fromLatin1(reader.format()).toLower();
    if (!format.isEmpty()) {
        result.format = format;
    }
    result.pixelSize = reader.size();

    // 拍摄日期沿用 ImageFacts 的口径：探测回填与同步兜底必须给出同一组值，
    // 否则同一批文件在「探完 / 没探完」两种状态下会分进不同的组。
    const ImageFacts facts = readImageFacts(path);
    result.shotDate = facts.shotDate;
    if (!result.hasSize()) {
        result.pixelSize = facts.pixelSize;   // 两条路径的 size() 同源，这里只做兜底
    }

    const QString family = formatFamily(result.format);
    // 只有能自己解析的格式才多读一次头：HEIF / PSD / RAW 等读了也拿不到
    // 位深或缩略图信息，白花一次 IO。
    const bool jpeg = family == QLatin1String("jpeg");
    const bool png = family == QLatin1String("png");
    const bool tiff = family == QLatin1String("tiff");
    const bool webp = family == QLatin1String("webp");
    const bool gif = family == QLatin1String("gif");
    if (!jpeg && !png && !tiff && !webp && !gif) {
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;   // 打不开（权限 / 已被删）：空结果，调用方按「未知」处理
    }
    const QByteArray head = file.read(kHeaderBytes);

    if (jpeg) {
        scanJpeg(head, &result.orientation, &result.hasEmbeddedThumb, &result.bitDepth);
        result.hasAlpha = false;   // JPEG 本身没有 alpha 通道
    } else if (png) {
        scanPng(head, &result.bitDepth, &result.hasAlpha,
                &result.orientation, &result.hasEmbeddedThumb);
    } else if (tiff) {
        // 裸 TIFF / CR2 的文件本身就是 TIFF 块；BitsPerSample 要跟随偏移去找，
        // 这里不做，位深保持未知（0）——不瞎猜。
        scanTiff(head, 0, &result.orientation, &result.hasEmbeddedThumb);
    } else if (webp) {
        scanWebp(head, &result.hasAlpha, &result.orientation, &result.hasEmbeddedThumb);
    } else if (gif) {
        // GIF 逻辑屏幕描述符的标志位：bit0 = 全局调色板里有透明色。
        if (head.size() >= 11) {
            result.hasAlpha = (quint8(head.at(10)) & 0x01) != 0;
        }
    }
    return result;
}

class ImageProbe::ProbeTask : public QRunnable
{
public:
    ProbeTask(ImageProbe *owner, const QString &path, qint64 mtimeMs, qint64 size, quint64 gen)
        : m_owner(owner), m_path(path), m_mtimeMs(mtimeMs), m_size(size), m_gen(gen)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        const ImageProbeResult result = ImageProbe::probeFile(m_path);
        // 结果与请求时的 mtime/size 一起回主线程：落缓存时就按这一对值记新鲜度，
        // 期间文件被改写的话，下一次 cached() 比对自然判为过期。
        QMetaObject::invokeMethod(m_owner,
            [owner = m_owner, path = m_path, mtimeMs = m_mtimeMs, size = m_size,
             gen = m_gen, result]() {
                owner->deliver(path, mtimeMs, size, gen, result);
            }, Qt::QueuedConnection);
    }

private:
    ImageProbe *m_owner;
    QString m_path;
    qint64 m_mtimeMs;
    qint64 m_size;
    quint64 m_gen;
};

ImageProbe::ImageProbe(QObject *parent)
    : QObject(parent)
{
    // 元类型注册只服务于「可能存在的 queued connect」；本类内部用 lambda 回主线程，
    // 不依赖它，但对外暴露的信号被跨线程连接时不能少这一句。
    qRegisterMetaType<ImageProbeResult>("ImageProbeResult");
    m_pool.setMaxThreadCount(2);
    m_pool.setObjectName(QStringLiteral("imageProbePool"));
}

ImageProbe::~ImageProbe()
{
    // 析构前收干净：worker 手里有 this 的裸指针，必须等它们全部退出。
    // 取消不掉的排队任务会在 waitForDone 里跑完，但结果只是投递一条
    // queued functor，对象销毁时 Qt 会连同事件一起丢弃。
    m_pool.clear();
    m_pool.waitForDone();
}

bool ImageProbe::cached(const QString &path, ImageProbeResult *out) const
{
    const auto it = m_cache.constFind(path);
    if (it == m_cache.constEnd()) {
        return false;
    }
    const QFileInfo info(path);
    if (mtimeMsOf(info) != it->mtimeMs || info.size() != it->size) {
        return false;   // 内容 / 时间变了：缓存过期，等下一次 request 重探
    }
    if (out) {
        *out = it->result;
    }
    return true;
}

void ImageProbe::request(const QString &path)
{
    if (path.isEmpty() || m_inFlight.contains(path)) {
        return;
    }
    const QFileInfo info(path);
    const qint64 mtimeMs = mtimeMsOf(info);
    const qint64 size = info.size();
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd() && it->mtimeMs == mtimeMs && it->size == size) {
        return;   // 已有新鲜结果，不重复读盘
    }
    m_inFlight.insert(path);
    m_pool.start(new ProbeTask(this, path, mtimeMs, size, m_gen.value(path)),
                 -1);   // 低优先级：预热不该和前台请求抢
}

void ImageProbe::requestAll(const QStringList &paths)
{
    for (const QString &path : paths) {
        request(path);   // request 内部按路径去重（在飞 / 新鲜缓存）
    }
}

void ImageProbe::forget(const QString &path)
{
    m_cache.remove(path);
    // 在飞任务的结果作废：代次对不上，deliver 会直接丢掉。
    m_gen[path] = m_gen.value(path) + 1;
    m_inFlight.remove(path);
}

void ImageProbe::deliver(const QString &path, qint64 mtimeMs, qint64 size, quint64 gen,
                         const ImageProbeResult &result)
{
    if (gen != m_gen.value(path)) {
        return;   // forget() 之后回来的旧结果：不落缓存，也不通知（调用方已不再关心）
    }
    m_inFlight.remove(path);
    Entry entry;
    entry.result = result;
    entry.mtimeMs = mtimeMs;
    entry.size = size;
    m_cache.insert(path, entry);
    emit probed(path, result);
}
