#include "SvgLoader.h"

#include "PsdPch.h"
#include "Psdminiz.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>

#include <cmath>
#include <cstring>

namespace {

// 读入上限 8 MB：真实业务 SVG 多在几十 KB 量级，超过这个数已经是异常文件，
// 不读到内存里再谈解析（与壳扩展缩略图处理器的 8MB 上限同口径）。
const qint64 kMaxSvgBytes = 8 * 1024 * 1024;

// 栅格上限：单边 4096 px、总像素 800 万（ARGB32 约 32 MB）。
// SVG 是矢量，放大查看靠重采样，不靠把位图开到画布量级；上限只影响
// 「超大 viewBox」这类病态文件，正常 SVG 取自带尺寸即可。
const int kMaxRasterEdge = 4096;
const qint64 kMaxRasterPixels = 8000000;

void fail(const QString &reason, LoadErrorCategory cat, QString *error,
          LoadErrorCategory *category)
{
    if (error) {
        *error = reason;
    }
    if (category) {
        *category = cat;
    }
}

bool isGzipMagic(const QByteArray &bytes)
{
    return bytes.size() >= 2
           && static_cast<uchar>(bytes.at(0)) == 0x1F
           && static_cast<uchar>(bytes.at(1)) == 0x8B;
}

// 解压 RFC 1952 GZIP（.svgz），硬性限制解压后 <= kMaxSvgBytes 防 gzip 炸弹，
// 并校验 CRC32 与 ISIZE 防止截断/损坏包漏过。
bool decompressSvgz(const QByteArray &gz, QByteArray *outSvg, LoadErrorCategory *errCat,
                    QString *errMsg)
{
    if (gz.size() < 18 || !isGzipMagic(gz)) {
        fail(QStringLiteral("svgz header invalid"), LoadErrorCategory::Unsupported, errMsg, errCat);
        return false;
    }
    const uchar *raw = reinterpret_cast<const uchar *>(gz.constData());
    if (raw[2] != 8) {   // CM == 8 (DEFLATE)
        fail(QStringLiteral("svgz unsupported compression"), LoadErrorCategory::Unsupported,
             errMsg, errCat);
        return false;
    }
    const uchar flg = raw[3];
    int pos = 10;
    const int n = gz.size();
    if (flg & 0x04) {   // FEXTRA
        if (pos + 2 > n) {
            fail(QStringLiteral("svgz truncated extra"), LoadErrorCategory::Corrupt, errMsg, errCat);
            return false;
        }
        const int xlen = int(raw[pos]) | (int(raw[pos + 1]) << 8);
        pos += 2 + xlen;
    }
    if (flg & 0x08) {   // FNAME
        while (pos < n && raw[pos] != 0) ++pos;
        ++pos;
    }
    if (flg & 0x10) {   // FCOMMENT
        while (pos < n && raw[pos] != 0) ++pos;
        ++pos;
    }
    if (flg & 0x02) {   // FHCRC
        pos += 2;
    }
    if (pos >= n - 8) {
        fail(QStringLiteral("svgz truncated stream"), LoadErrorCategory::Corrupt, errMsg, errCat);
        return false;
    }

    struct Sink {
        QByteArray buf;
        bool overflow = false;
    } sink;

    size_t compLen = size_t(n - 8 - pos);
    auto putCb = [](const void *pBuf, int len, void *pUser) -> int {
        Sink *s = static_cast<Sink *>(pUser);
        if (len < 0 || qint64(s->buf.size()) + qint64(len) > kMaxSvgBytes) {
            s->overflow = true;
            return 0;
        }
        s->buf.append(static_cast<const char *>(pBuf), len);
        return 1;
    };

    const int ok = tinfl_decompress_mem_to_callback(raw + pos, &compLen, putCb, &sink, 0);
    if (!ok) {
        fail(sink.overflow ? QStringLiteral("svgz exceeds 8 MB")
                           : QStringLiteral("svgz inflate failed"),
             LoadErrorCategory::Corrupt, errMsg, errCat);
        return false;
    }

    const int trailerOff = n - 8;
    const quint32 expectedCrc = quint32(raw[trailerOff])
                                | (quint32(raw[trailerOff + 1]) << 8)
                                | (quint32(raw[trailerOff + 2]) << 16)
                                | (quint32(raw[trailerOff + 3]) << 24);
    const quint32 expectedSize = quint32(raw[trailerOff + 4])
                                 | (quint32(raw[trailerOff + 5]) << 8)
                                 | (quint32(raw[trailerOff + 6]) << 16)
                                 | (quint32(raw[trailerOff + 7]) << 24);
    const mz_ulong actualCrc = mz_crc32(MZ_CRC32_INIT,
                                        reinterpret_cast<const unsigned char *>(sink.buf.constData()),
                                        size_t(sink.buf.size()));
    if (quint32(actualCrc) != expectedCrc
        || (quint32(sink.buf.size()) & 0xFFFFFFFFu) != expectedSize) {
        fail(QStringLiteral("svgz crc/size mismatch"), LoadErrorCategory::Corrupt, errMsg, errCat);
        return false;
    }
    *outSvg = std::move(sink.buf);
    return true;
}

// 头部嗅探：跳过 UTF-8 BOM 与前导空白后必须以 '<' 开头，且出现 <svg 或 <?xml。
// 只判「像不像 SVG 文本」，合法性交给 QSvgRenderer；二进制文件（哪怕后缀是
// .svg）在这里就归 Unsupported，不送进解析器。
bool looksLikeSvgText(const QByteArray &head)
{
    QByteArray h = head;
    if (h.startsWith("\xEF\xBB\xBF")) {
        h.remove(0, 3);
    }
    while (!h.isEmpty()
           && (h.at(0) == ' ' || h.at(0) == '\t' || h.at(0) == '\r' || h.at(0) == '\n')) {
        h.remove(0, 1);
    }
    if (h.isEmpty() || h.at(0) != '<') {
        return false;
    }
    const QByteArray lower = h.toLower();
    return lower.contains("<svg") || lower.contains("<?xml");
}

// 目标栅格尺寸：先按请求盒缩放，再夹单边，最后夹总像素。
QSize rasterSize(const QSize &natural, const QSize &box, bool allowUpscale)
{
    if (!natural.isValid() || natural.isEmpty()) {
        return QSize();
    }
    QSize out = natural;
    if (box.isValid() && !box.isEmpty()) {
        const double s = qMin(double(box.width()) / natural.width(),
                              double(box.height()) / natural.height());
        if (s > 1.0 && allowUpscale) {
            out = QSize(qMax(1, int(std::ceil(natural.width() * s))),
                        qMax(1, int(std::ceil(natural.height() * s))));
        } else if (s < 1.0) {
            out = QSize(qMax(1, int(natural.width() * s)),
                        qMax(1, int(natural.height() * s)));
        }
    }
    if (out.width() > kMaxRasterEdge || out.height() > kMaxRasterEdge) {
        out.scale(QSize(kMaxRasterEdge, kMaxRasterEdge), Qt::KeepAspectRatio);
    }
    const qint64 px = qint64(out.width()) * qint64(out.height());
    if (px > kMaxRasterPixels) {
        const double k = std::sqrt(double(kMaxRasterPixels) / double(px));
        out = QSize(qMax(1, int(out.width() * k)), qMax(1, int(out.height() * k)));
    }
    return out;
}

// SVG 自带尺寸：width/height 属性优先，其次 viewBox。两者都没有的文件
// 没有「画布多大」的定义，按不支持处理（不猜一个尺寸出来）。
QSize intrinsicSize(QSvgRenderer &renderer)
{
    const QSize def = renderer.defaultSize();
    if (def.isValid() && !def.isEmpty()) {
        return def;
    }
    const QRectF vb = renderer.viewBoxF();
    const QSize fromBox(qRound(vb.width()), qRound(vb.height()));
    if (fromBox.isValid() && !fromBox.isEmpty()) {
        return fromBox;
    }
    return QSize();
}

}   // namespace

namespace SvgLoader {

bool isSvgPath(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("svg") || s == QLatin1String("svgz");
}

QImage render(const QString &path, const QSize &box, bool allowUpscale, QString *error,
              LoadErrorCategory *category)
{
    if (error) {
        error->clear();
    }
    if (category) {
        *category = LoadErrorCategory::None;
    }
    if (path.isEmpty()) {
        return QImage();
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        fail(QStringLiteral("svg open failed"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
    }
    if (f.size() > kMaxSvgBytes) {
        fail(QStringLiteral("svg exceeds 8 MB"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
    }
    const QByteArray rawData = f.read(kMaxSvgBytes);
    f.close();
    if (rawData.isEmpty()) {
        fail(QStringLiteral("svg read empty"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
    }

    QByteArray data;
    const bool expectGzip = QFileInfo(path).suffix().compare(QLatin1String("svgz"), Qt::CaseInsensitive) == 0
                            || isGzipMagic(rawData);
    if (expectGzip) {
        if (!decompressSvgz(rawData, &data, category, error)) {
            return QImage();
        }
        if (!looksLikeSvgText(data.left(4096))) {
            fail(QStringLiteral("svgz content not svg"), LoadErrorCategory::Unsupported,
                 error, category);
            return QImage();
        }
    } else {
        if (!looksLikeSvgText(rawData.left(4096))) {
            fail(QStringLiteral("svg header not recognized"), LoadErrorCategory::Unsupported,
                 error, category);
            return QImage();
        }
        data = rawData;
    }

    QSvgRenderer renderer;
    if (!renderer.load(data)) {
        // 能认成 SVG/XML 文本但解析不了：截断、标签未闭合、用了 Tiny 不支持的特性。
        fail(QStringLiteral("svg parse failed"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
    }
    const QSize natural = intrinsicSize(renderer);
    const QSize target = rasterSize(natural, box, allowUpscale);
    if (!target.isValid() || target.isEmpty()) {
        fail(QStringLiteral("svg has no intrinsic size"), LoadErrorCategory::Unsupported,
             error, category);
        return QImage();
    }

    QImage img(target, QImage::Format_ARGB32_Premultiplied);
    if (img.isNull()) {
        fail(QStringLiteral("svg raster alloc failed"), LoadErrorCategory::Corrupt, error,
             category);
        return QImage();
    }
    img.fill(Qt::transparent);   // 透明底由画布既有棋盘处理，不在这里铺色
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        renderer.setAspectRatioMode(Qt::KeepAspectRatio);
        renderer.render(&p, QRectF(0, 0, target.width(), target.height()));
        p.end();
    }
    // 与 PsdLoader 一致交出 ARGB32（非预乘），下游效果/保存不再区分两种 alpha 约定
    return img.convertToFormat(QImage::Format_ARGB32);
}

}   // namespace SvgLoader
