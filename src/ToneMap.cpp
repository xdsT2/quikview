#include "ToneMap.h"

#include <QColor>
#include <algorithm>
#include <cmath>

namespace ToneMap {

namespace {

// ACES 近似（Narkowicz 2015）：x 定义域 [0,∞)，输出 [0,1)。
// 归一化白点后白色在曲线上的落点不是 1，再除以该值把白点锚回 255，
// 否则整张图都会被压灰一档。
constexpr double kA = 2.51, kB = 0.03, kC = 2.43, kD = 0.59, kE = 0.14;

double aces(double x)
{
    const double y = (x * (kA * x + kB)) / (x * (kC * x + kD) + kE);
    return qBound(0.0, y, 1.0);
}

// 99.5% 分位白点：样本里最亮的 0.5% 视作高光，其余压进 SDR 范围。
// 没有这一步（按理论满量程归一化）实际内容偏暗的图会整体发灰。
double whitePointOf(const QVector<quint32> &lumaSamples)
{
    if (lumaSamples.isEmpty()) {
        return 65535.0;
    }
    QVector<quint32> sorted = lumaSamples;
    std::sort(sorted.begin(), sorted.end());
    // 上侧 0.5% 分位：第 ceil(n*0.995) 个样本（0-based）
    int idx = int(std::ceil(sorted.size() * 0.995)) - 1;
    idx = qBound(0, idx, sorted.size() - 1);
    double white = sorted[idx];
    // 白点异常低（全黑图/坏数据）时兜底，避免除小数把暗部炸亮
    return white < 1.0 ? 65535.0 : white;
}

} // namespace

QVector<quint32> sampleLuma16(const QImage &image)
{
    QVector<quint32> samples;
    if (image.isNull()) {
        return samples;
    }
    const int w = image.width();
    const int h = image.height();
    // 控制在 ~40 万样本以内，避免大图上白点统计本身变成瓶颈
    const int step = qMax(1, int(std::sqrt(double(w) * h / 400000.0)));
    const bool gray = image.format() == QImage::Format_Grayscale16;
    const bool premul = image.format() == QImage::Format_RGBA64_Premultiplied;
    samples.reserve((w / step + 1) * (h / step + 1));
    for (int y = 0; y < h; y += step) {
        if (gray) {
            const quint16 *line = reinterpret_cast<const quint16 *>(image.constScanLine(y));
            for (int x = 0; x < w; x += step) {
                samples.append(line[x]);
            }
            continue;
        }
        const QRgba64 *line = reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for (int x = 0; x < w; x += step) {
            QRgba64 p = line[x];
            if (premul) {
                p = qUnpremultiply(p);
            }
            // rec709：Y = 0.2126R + 0.7152G + 0.0722B（保持 16bit 量纲）
            samples.append((2126 * quint32(p.red()) + 7152 * quint32(p.green())
                            + 722 * quint32(p.blue())) / 10000);
        }
    }
    return samples;
}

Curve16 buildCurve16(const QVector<quint32> &lumaSamples)
{
    Curve16 curve;
    const double white = whitePointOf(lumaSamples);
    const double whiteOut = aces(1.0);
    for (int v = 0; v < 65536; ++v) {
        const double x = v / white;
        curve.map[v] = quint8(qRound(aces(x) / whiteOut * 255.0));
    }
    return curve;
}

namespace {

bool is16BitIntFormat(QImage::Format fmt)
{
    switch (fmt) {
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA64_Premultiplied:
    case QImage::Format_Grayscale16:
        return true;
    default:
        return false;
    }
}

// RGBA64（非预乘）→ 8bit：按曲线逐通道查表。alpha 只做 16→8bit 量化。
QImage mapRgba64(const QImage &src, const Curve16 &curve)
{
    QImage out(src.width(), src.height(),
               src.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    for (int y = 0; y < src.height(); ++y) {
        const QRgba64 *line = reinterpret_cast<const QRgba64 *>(src.constScanLine(y));
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const QRgba64 p = line[x];
            const int r = curve.map[p.red()];
            const int g = curve.map[p.green()];
            const int b = curve.map[p.blue()];
            const int a = p.alpha() >> 8;
            dst[x] = qRgba(r, g, b, a);
        }
    }
    return out;
}

QImage mapGray16(const QImage &src, const Curve16 &curve)
{
    QImage out(src.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < src.height(); ++y) {
        const quint16 *line = reinterpret_cast<const quint16 *>(src.constScanLine(y));
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            dst[x] = curve.map[line[x]];
        }
    }
    return out;
}

} // namespace

QImage toneMapToSdr(const QImage &image)
{
    if (image.isNull()) {
        return image;
    }
    const QImage::Format fmt = image.format();
    if (!is16BitIntFormat(fmt)) {
        // 浮点高位深（RGBA16FP/RGBA32FP）先折算成 16bit 整型再映射；转换失败
        // 保持原样交回上层（画布会按旧路径截断），绝不动 8bit 图。
        if (fmt == QImage::Format_RGBX16FPx4 || fmt == QImage::Format_RGBA16FPx4
            || fmt == QImage::Format_RGBA16FPx4_Premultiplied
            || fmt == QImage::Format_RGBX32FPx4 || fmt == QImage::Format_RGBA32FPx4
            || fmt == QImage::Format_RGBA32FPx4_Premultiplied) {
            const QImage conv = image.convertToFormat(QImage::Format_RGBA64);
            if (conv.isNull()) {
                return image;
            }
            const Curve16 curve = buildCurve16(sampleLuma16(conv));
            return mapRgba64(conv, curve);
        }
        return image;   // 8bit SDR：硬规则，一个像素都不动
    }

    const Curve16 curve = buildCurve16(sampleLuma16(image));
    return fmt == QImage::Format_Grayscale16 ? mapGray16(image, curve)
                                             : mapRgba64(image, curve);
}

} // namespace ToneMap
