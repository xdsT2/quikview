#include "DisplayFrame.h"

#include "ToneMap.h"

#include <QColorSpace>
#include <QHash>

namespace DisplayFrame {
namespace {

// 基线 MainWindow::showImage 里的 toSrgb lambda（旧路径逐行复刻）：
// 关闭直通；无 ICC / 已是 sRGB 的图原样返回，普通截图像素不受影响。
QImage convertToSrgb(const QImage &img, bool enabled)
{
    if (!enabled || img.isNull()) {
        return img;
    }
    const QColorSpace cs = img.colorSpace();
    if (cs.isValid() && cs != QColorSpace::SRgb) {
        QImage out = img;   // 隐式共享拷贝；convertToColorSpace 为本 Qt 版本就地改
        out.convertToColorSpace(QColorSpace::SRgb);
        return out;
    }
    return img;
}

// 基线顺序：先 toneMap（高位深 → SDR），再 ICC→sRGB。
QImage prepareImpl(const QImage &raw, const PrepareOptions &opt)
{
    if (raw.isNull()) {
        return raw;
    }
    const QImage sdr = opt.hdrTonemap ? ToneMap::toneMapToSdr(raw) : raw;
    return convertToSrgb(sdr, opt.imageIcc);
}

} // namespace

QImage legacyPrepare(const QImage &raw, const PrepareOptions &opt)
{
    return prepareImpl(raw, opt);
}

QImage prepare(const QImage &raw, const PrepareOptions &opt)
{
    return prepareImpl(raw, opt);
}

QString monitorTag(const QString &screenName, const QRect &geometry,
                   const QString &profilePath, const QDateTime &profileMtime)
{
    // 几何进键：同一 profile 挂到不同屏幕（或 DPR/布局变化）不串帧。
    return QStringLiteral("%1|%2x%3@%4,%5|profile=%6|mtime=%7")
        .arg(screenName, QString::number(geometry.width()),
             QString::number(geometry.height()),
             QString::number(geometry.x()), QString::number(geometry.y()),
             profilePath, profileMtime.isValid() ? profileMtime.toString(Qt::ISODate)
                                                  : QStringLiteral("none"));
}

QString cacheKey(const QString &path, qint64 mtimeSec,
                 const PrepareOptions &opt, const QString &monitorTag)
{
    // 键覆盖四类会改变输出的条件：图源内容(mtime)、ICC 开关、HDR 开关、显示 profile。
    return QStringLiteral("%1|mtime=%2|icc=%3|tonemap=%4|monitor=%5")
        .arg(path, QString::number(mtimeSec),
             opt.imageIcc ? QStringLiteral("1") : QStringLiteral("0"),
             opt.hdrTonemap ? QStringLiteral("1") : QStringLiteral("0"),
             monitorTag);
}

} // namespace DisplayFrame
