#include "SvgLoader.h"

#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSvgRenderer>

#include <cmath>

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
    return QFileInfo(path).suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0;
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
    const QByteArray head = f.read(4096);
    if (!looksLikeSvgText(head)) {
        fail(QStringLiteral("svg header not recognized"), LoadErrorCategory::Unsupported,
             error, category);
        return QImage();
    }
    if (f.size() > kMaxSvgBytes) {
        fail(QStringLiteral("svg exceeds 8 MB"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
    }
    f.seek(0);
    const QByteArray data = f.read(kMaxSvgBytes);
    f.close();
    if (data.isEmpty()) {
        fail(QStringLiteral("svg read empty"), LoadErrorCategory::Corrupt, error, category);
        return QImage();
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
