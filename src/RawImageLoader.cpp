#include "RawImageLoader.h"
#include <QCoreApplication>

#include "ToneMap.h"

#ifdef HAS_LIBRAW
#include <libraw/libraw.h>
#endif

#include <QByteArray>
#include <QFileInfo>
#include <QStringList>

#include <climits>
#include <cmath>

#ifdef HAS_LIBRAW
namespace {

int openRawFile(LibRaw &raw, const QString &path)
{
    const QString absPath = QFileInfo(path).absoluteFilePath();
#ifdef Q_OS_WIN
    const std::wstring wpath = absPath.toStdWString();
    return raw.open_file(wpath.c_str());
#else
    const QByteArray local = absPath.toLocal8Bit();
    return raw.open_file(local.constData());
#endif
}

// 内嵌预览（BITMAP 型）转 QImage：8bit 直接拷贝，16bit 按 >>8 落 8bit。
// 预览是 libraw 已经处理好的图，不再走 ToneMap（那是全图 HDR 路径的事）。
QImage thumbBitmapToQImage(const libraw_processed_image_t *t)
{
    if (!t || !t->data) {
        return QImage();
    }
    const int w = t->width;
    const int h = t->height;
    const int colors = t->colors;
    if (w <= 0 || h <= 0 || w > 30000 || h > 30000
        || (colors != 1 && colors != 3 && colors != 4)
        || (t->bits != 8 && t->bits != 16)) {
        return QImage();
    }
    const quint64 requiredBytes = quint64(w) * quint64(h) * quint64(colors) * quint64(t->bits / 8);
    if (quint64(t->data_size) < requiredBytes) {
        return QImage();
    }
    if (t->bits == 8) {
        const QImage::Format fmt = (colors == 1) ? QImage::Format_Grayscale8
                                 : (colors == 4) ? QImage::Format_RGBA8888
                                                 : QImage::Format_RGB888;
        return QImage(t->data, w, h, w * colors, fmt).copy();
    }
    if (t->bits == 16) {
        const QImage::Format fmt = (colors == 1) ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
        QImage img(w, h, fmt);
        if (img.isNull()) {
            return QImage();
        }
        const ushort *src = reinterpret_cast<const ushort *>(t->data);
        for (int y = 0; y < h; ++y) {
            uchar *line = img.scanLine(y);
            const ushort *row = src + static_cast<size_t>(y) * w * colors;
            if (colors == 1) {
                for (int x = 0; x < w; ++x) {
                    line[x] = static_cast<uchar>(row[x] >> 8);
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    line[x * 3 + 0] = static_cast<uchar>(row[x * colors + 0] >> 8);
                    line[x * 3 + 1] = static_cast<uchar>(row[x * colors + 1] >> 8);
                    line[x * 3 + 2] = static_cast<uchar>(row[x * colors + 2] >> 8);
                }
            }
        }
        return img;
    }
    return QImage();
}

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
    if (img.isNull()) {
        return QImage();
    }
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
    LibRaw raw;
    if (openRawFile(raw, path) != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "libraw 打开失败");
        return QImage();
    }
    if (raw.unpack() != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "libraw 解包失败");
        raw.recycle();
        return QImage();
    }
    if (raw.dcraw_process() != LIBRAW_SUCCESS) {
        if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "libraw 处理失败");
        raw.recycle();
        return QImage();
    }

    int err = 0;
    libraw_processed_image_t *proc = raw.dcraw_make_mem_image(&err);
    if (proc == nullptr || err != LIBRAW_SUCCESS) {
        if (proc) {
            LibRaw::dcraw_clear_mem(proc);
        }
        if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "libraw 内存图像生成失败");
        raw.recycle();
        return QImage();
    }

    QImage img;
    const int w = proc->width;
    const int h = proc->height;
    const int colors = proc->colors; // 1 = Gray, 3 = RGB, 4 = RGBA
    const unsigned int bits = proc->bits; // 8 或 16
    const quint64 requiredBytes = (w > 0 && h > 0 && colors > 0 && (bits == 8 || bits == 16))
        ? quint64(w) * quint64(h) * quint64(colors) * quint64(bits / 8)
        : 0;

    if (w <= 0 || h <= 0 || w > 30000 || h > 30000
        || (colors != 1 && colors != 3 && colors != 4)
        || (bits != 8 && bits != 16)
        || quint64(proc->data_size) < requiredBytes) {
        if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "不支持的 RAW 图像参数（colors=%1, bits=%2）").arg(colors).arg(bits);
    } else if (bits == 8) {
        const QImage::Format fmt = (colors == 1) ? QImage::Format_Grayscale8
                                 : (colors == 4) ? QImage::Format_RGBA8888
                                                 : QImage::Format_RGB888;
        img = QImage(proc->data, w, h, w * colors, fmt).copy();
    } else if (bits == 16) {
        if (colors == 1) {
            img = QImage(w, h, QImage::Format_Grayscale8);
            if (!img.isNull()) {
                const ushort *src = reinterpret_cast<const ushort *>(proc->data);
                for (int y = 0; y < h; ++y) {
                    uchar *line = img.scanLine(y);
                    const ushort *row = src + static_cast<size_t>(y) * w;
                    for (int x = 0; x < w; ++x) {
                        line[x] = static_cast<uchar>(row[x] >> 8);
                    }
                }
            }
        } else if (toneMapSdr) {
            img = toneMap16ToRgb888(reinterpret_cast<const ushort *>(proc->data),
                                    w, h, colors);
        } else {
            // 16 位转 8 位 RGB 显示
            img = QImage(w, h, QImage::Format_RGB888);
            if (!img.isNull()) {
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
        }
    }

    LibRaw::dcraw_clear_mem(proc);
    raw.recycle();
    return img;
}
#else
QImage loadRawImage(const QString &, QString *errorOut, bool)
{
    if (errorOut) *errorOut = QCoreApplication::translate("RawImageLoader", "当前构建未启用 libraw，无法解码 RAW");
    return QImage();
}
#endif

const QStringList &rawFileExtensions()
{
    static const QStringList kRawExts = {
        QStringLiteral("dng"), QStringLiteral("nef"), QStringLiteral("cr2"),
        QStringLiteral("cr3"), QStringLiteral("crw"), QStringLiteral("arw"),
        QStringLiteral("sr2"), QStringLiteral("srf"), QStringLiteral("srw"),
        QStringLiteral("rw2"), QStringLiteral("orf"), QStringLiteral("raf"),
        QStringLiteral("pef"), QStringLiteral("raw"), QStringLiteral("nrw"),
        QStringLiteral("3fr"), QStringLiteral("fff"), QStringLiteral("iiq"),
        QStringLiteral("rwl"), QStringLiteral("x3f"), QStringLiteral("mrw"),
        QStringLiteral("dcr"), QStringLiteral("kdc"), QStringLiteral("erf"),
        QStringLiteral("mef"), QStringLiteral("mos"), QStringLiteral("k25"),
        QStringLiteral("mdc"), QStringLiteral("ptx"), QStringLiteral("pxn"),
        QStringLiteral("bay"), QStringLiteral("cap"), QStringLiteral("dcs"),
        QStringLiteral("drf"), QStringLiteral("eip"), QStringLiteral("gpr"),
        QStringLiteral("rwz"), QStringLiteral("sti"), QStringLiteral("ari")
    };
    return kRawExts;
}

bool isRawFilePath(const QString &path)
{
    return rawFileExtensions().contains(QFileInfo(path).suffix().toLower());
}

QImage loadRawThumbnail(const QString &path)
{
#ifdef HAS_LIBRAW
    LibRaw raw;
    if (openRawFile(raw, path) != LIBRAW_SUCCESS) {
        return QImage();
    }
    QImage img;
    // unpack_thumb 只解内嵌预览，不跑 dcraw_process；没有预览的机背/小机 RAW
    // 会失败，调用方回落全尺寸解码。
    if (raw.unpack_thumb() == LIBRAW_SUCCESS) {
        int err = 0;
        libraw_processed_image_t *thumb = raw.dcraw_make_mem_thumb(&err);
        if (thumb != nullptr) {
            if (err == LIBRAW_SUCCESS) {
                if (thumb->type == LIBRAW_IMAGE_JPEG) {
                    if (thumb->data_size > 0 && thumb->data_size <= quint32(INT_MAX)) {
                        img.loadFromData(reinterpret_cast<const uchar *>(thumb->data),
                                         static_cast<int>(thumb->data_size), "JPG");
                    }
                } else if (thumb->type == LIBRAW_IMAGE_BITMAP) {
                    img = thumbBitmapToQImage(thumb);
                }
            }
            LibRaw::dcraw_clear_mem(thumb);
        }
    }
    raw.recycle();
    return img;
#else
    Q_UNUSED(path);
    return QImage();
#endif
}
