#include "RawImageLoader.h"

#include "ToneMap.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QFileInfo>

#include <cmath>

namespace {

// HDR 开启时的 16bit→8bit：按 rec709 抽亮度、99.5% 分位白点 + ACES 曲线查表，
// 替代默认的 >>8 线性截断（高光保留细节，防止整体发灰）。
QImage toneMap16ToRgb888(const ushort *src, int w, int h, int srcChannels)
{
    const int step = qMax(1, int(std::sqrt(double(w) * h / 400000.0)));
    QVector<quint32> samples;
    samples.reserve((w / step + 1) * (h / step + 1));
    for (int y = 0; y < h; y += step) {
        const ushort *row = src + static_cast<size_t>(y) * w * srcChannels;
        for (int x = 0; x < w; x += step) {
            const quint32 r = row[x * srcChannels + 0];
            const quint32 g = row[x * srcChannels + 1];
            const quint32 b = row[x * srcChannels + 2];
            samples.append((2126 * r + 7152 * g + 722 * b) / 10000);
        }
    }
    const ToneMap::Curve16 curve = ToneMap::buildCurve16(samples);

    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        const ushort *row = src + static_cast<size_t>(y) * w * srcChannels;
        uchar *line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            line[x * 3 + 0] = curve.map[row[x * srcChannels + 0]];
            line[x * 3 + 1] = curve.map[row[x * srcChannels + 1]];
            line[x * 3 + 2] = curve.map[row[x * srcChannels + 2]];
        }
    }
    return img;
}

} // namespace

QImage loadRawImage(const QString &path, QString *errorOut, bool toneMapSdr)
{
    const QByteArray local = QFileInfo(path).absoluteFilePath().toLocal8Bit();

    LibRaw raw;
    if (raw.open_file(local.constData()) != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("libraw 打开失败");
        return QImage();
    }
    if (raw.unpack() != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("libraw 解包失败");
        raw.recycle();
        return QImage();
    }
    if (raw.dcraw_process() != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("libraw 处理失败");
        raw.recycle();
        return QImage();
    }

    int err = 0;
    libraw_processed_image_t *proc = raw.dcraw_make_mem_image(&err);
    if (proc == nullptr || err != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QStringLiteral("libraw 内存图像生成失败");
        raw.recycle();
        return QImage();
    }

    QImage img;
    const int w = proc->width;
    const int h = proc->height;
    const int colors = proc->colors; // 3 = RGB, 4 = BGRA
    const unsigned int bits = proc->bits; // 8 或 16

    if (bits == 8) {
        QImage::Format fmt = (colors == 4) ? QImage::Format_RGB32 : QImage::Format_RGB888;
        img = QImage(proc->data, w, h, static_cast<int>(proc->width * colors), fmt).copy();
    } else if (bits == 16) {
        if (toneMapSdr) {
            img = toneMap16ToRgb888(reinterpret_cast<const ushort *>(proc->data),
                                    w, h, colors);
        } else {
            // 16 位转 8 位 RGB 显示
            img = QImage(w, h, QImage::Format_RGB888);
            const ushort *src = reinterpret_cast<const ushort *>(proc->data);
            const int srcChannels = colors;
            for (int y = 0; y < h; ++y) {
                uchar *line = img.scanLine(y);
                const ushort *row = src + static_cast<size_t>(y) * w * srcChannels;
                for (int x = 0; x < w; ++x) {
                    line[x * 3 + 0] = static_cast<uchar>(row[x * srcChannels + 0] >> 8);
                    line[x * 3 + 1] = static_cast<uchar>(row[x * srcChannels + 1] >> 8);
                    line[x * 3 + 2] = static_cast<uchar>(row[x * srcChannels + 2] >> 8);
                }
            }
        }
    } else {
        if (errorOut) *errorOut = QStringLiteral("不支持的位深 %1").arg(bits);
    }

    LibRaw::dcraw_clear_mem(proc);
    raw.recycle();
    return img;
}
