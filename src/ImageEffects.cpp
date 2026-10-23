#include "ImageEffects.h"

#include <QColor>
#include <QtGlobal>

namespace {

// 通用 3x3 卷积，边界按边缘像素钳位（clamp）。
QImage convolve3x3(const QImage &src, const double kernel[9], double divisor)
{
    if (src.isNull()) {
        return QImage();
    }

    const QImage img = src.convertToFormat(QImage::Format_ARGB32);
    QImage dst(img.size(), QImage::Format_ARGB32);

    const int w = img.width();
    const int h = img.height();

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0.0, g = 0.0, b = 0.0, a = 0.0;
            for (int ky = -1; ky <= 1; ++ky) {
                const int yy = qBound(0, y + ky, h - 1);
                for (int kx = -1; kx <= 1; ++kx) {
                    const int xx = qBound(0, x + kx, w - 1);
                    const QRgb p = img.pixel(xx, yy);
                    const double k = kernel[(ky + 1) * 3 + (kx + 1)];
                    r += qRed(p) * k;
                    g += qGreen(p) * k;
                    b += qBlue(p) * k;
                    a += qAlpha(p) * k;
                }
            }
            r /= divisor;
            g /= divisor;
            b /= divisor;
            a /= divisor;
            dst.setPixel(x, y, qRgba(qBound(0, qRound(r), 255),
                                     qBound(0, qRound(g), 255),
                                     qBound(0, qRound(b), 255),
                                     qBound(0, qRound(a), 255)));
        }
    }
    return dst;
}

} // namespace

QImage applySoften(const QImage &src)
{
    // 3x3 均值模糊：每个邻域像素权重 1，除以 9。
    const double kernel[9] = {1.0, 1.0, 1.0,
                              1.0, 1.0, 1.0,
                              1.0, 1.0, 1.0};
    return convolve3x3(src, kernel, 9.0);
}

QImage applySharpen(const QImage &src)
{
    // 3x3 unsharp：中心 5、四邻 -1，四角 0；核之和为 1（已归一化）。
    const double kernel[9] = { 0.0, -1.0,  0.0,
                              -1.0,  5.0, -1.0,
                               0.0, -1.0,  0.0};
    return convolve3x3(src, kernel, 1.0);
}
