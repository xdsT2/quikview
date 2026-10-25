#include "HeifLoader.h"

#include "ToneMap.h"

#include <libheif/heif.h>

#include <QFileInfo>

#include <cstdint>
#include <cmath>
#include <cstring>

namespace {

// 8x8 Bayer 矩阵（0..63），高位深→8bit 有序抖动用。
// 直接右移等于截断，渐变上会重现色带；加抖动后量化误差被打散成细颗粒。
constexpr uint8_t kBayer8x8[64] = {
     0, 32,  8, 40,  2, 34, 10, 42,
    48, 16, 56, 24, 50, 18, 58, 26,
    12, 44,  4, 36, 14, 46,  6, 38,
    60, 28, 52, 20, 62, 30, 54, 22,
     3, 35, 11, 43,  1, 33,  9, 41,
    51, 19, 59, 27, 49, 17, 57, 25,
    15, 47,  7, 39, 13, 45,  5, 37,
    63, 31, 55, 23, 61, 29, 53, 21,
};

// interleaved_RRGGBB_LE 平面（每像素 6 字节，每分量 16bit 小端）→ Format_RGB888。
// 实测（libheif 1.23.6）像素值是源位深的原值（10bit 图即 0..1023），
// 并非左移到满量程的 16bit；有效位深从 heif_image_get_bits_per_pixel_range 取。
QImage ditherRgb16LeToRgb888(const uint8_t *data, size_t stride, int w, int h, int rangeBits)
{
    QImage out(w, h, QImage::Format_RGB888);
    const int shift = qBound(0, rangeBits - 8, 8);
    const int step = 1 << shift;
    uint8_t thresh[64];
    for (int i = 0; i < 64; ++i) {
        thresh[i] = static_cast<uint8_t>(qMin(step - 1, (kBayer8x8[i] * step + step / 2) >> 6));
    }

    for (int y = 0; y < h; ++y) {
        const uint8_t *src = data + static_cast<size_t>(y) * stride;
        const uint8_t *trow = thresh + ((y & 7) << 3);
        uint8_t *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const uint8_t *p = src + static_cast<size_t>(x) * 6;
            const int t = trow[x & 7];
            const int r = (p[0] | (p[1] << 8)) + t;
            const int g = (p[2] | (p[3] << 8)) + t;
            const int b = (p[4] | (p[5] << 8)) + t;
            dst[x * 3 + 0] = static_cast<uint8_t>(qMin(255, r >> shift));
            dst[x * 3 + 1] = static_cast<uint8_t>(qMin(255, g >> shift));
            dst[x * 3 + 2] = static_cast<uint8_t>(qMin(255, b >> shift));
        }
    }
    return out;
}

// HDR 开启时替代抖动：RRGGBB_LE 平面（源位深原值，非满量程 16bit）直接做
// ACES 色调映射。白点按 99.5% 分位在实际像素值上统计，rangeBits 不参与。
QImage toneMapRgb16LeToRgb888(const uint8_t *data, size_t stride, int w, int h)
{
    const auto pixelAt = [](const uint8_t *p, int *r, int *g, int *b) {
        *r = p[0] | (p[1] << 8);
        *g = p[2] | (p[3] << 8);
        *b = p[4] | (p[5] << 8);
    };

    // 亮度抽样（~40 万样本以内），口径与 ToneMap::sampleLuma16 一致
    const int step = qMax(1, int(std::sqrt(double(w) * h / 400000.0)));
    QVector<quint32> samples;
    samples.reserve((w / step + 1) * (h / step + 1));
    for (int y = 0; y < h; y += step) {
        const uint8_t *src = data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; x += step) {
            int r, g, b;
            pixelAt(src + static_cast<size_t>(x) * 6, &r, &g, &b);
            samples.append((2126 * quint32(r) + 7152 * quint32(g) + 722 * quint32(b)) / 10000);
        }
    }
    const ToneMap::Curve16 curve = ToneMap::buildCurve16(samples);

    QImage out(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y) {
        const uint8_t *src = data + static_cast<size_t>(y) * stride;
        uint8_t *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            int r, g, b;
            pixelAt(src + static_cast<size_t>(x) * 6, &r, &g, &b);
            dst[x * 3 + 0] = curve.map[r];
            dst[x * 3 + 1] = curve.map[g];
            dst[x * 3 + 2] = curve.map[b];
        }
    }
    return out;
}

} // namespace

QImage loadHeifImage(const QString &path, QString *errorOut, bool toneMapSdr)
{
    const QByteArray local = QFileInfo(path).absoluteFilePath().toLocal8Bit();

    heif_context *ctx = heif_context_alloc();
    if (!ctx) {
        if (errorOut) *errorOut = QStringLiteral("libheif 上下文分配失败");
        return QImage();
    }

    heif_error err = heif_context_read_from_file(ctx, local.constData(), nullptr);
    if (err.code != heif_error_Ok) {
        if (errorOut) *errorOut = QStringLiteral("libheif 读取失败：%1").arg(QString::fromUtf8(err.message));
        heif_context_free(ctx);
        return QImage();
    }

    heif_image_handle *handle = nullptr;
    err = heif_context_get_primary_image_handle(ctx, &handle);
    if (err.code != heif_error_Ok || !handle) {
        if (errorOut) *errorOut = QStringLiteral("libheif 获取主图像失败：%1").arg(QString::fromUtf8(err.message));
        heif_context_free(ctx);
        return QImage();
    }

    // 位深探测：8bit 走原有路径不加开销；>8bit（10/12bit）申请 16bit 平面，
    // 避免在解码层就被砍成 8bit。申请失败（旧版 libheif 不认该 chroma 等）退回 8bit。
    const int lumaBits = heif_image_handle_get_luma_bits_per_pixel(handle);
    bool highDepth = lumaBits > 8;

    heif_image *img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB,
                            highDepth ? heif_chroma_interleaved_RRGGBB_LE
                                      : heif_chroma_interleaved_RGB,
                            nullptr);
    if (err.code != heif_error_Ok && highDepth) {
        highDepth = false;
        err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
    }
    if (err.code != heif_error_Ok || !img) {
        if (errorOut) *errorOut = QStringLiteral("libheif 解码失败：%1").arg(QString::fromUtf8(err.message));
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return QImage();
    }

    size_t stride = 0;
    const uint8_t *data = heif_image_get_plane_readonly2(img, heif_channel_interleaved, &stride);
    if (!data || stride == 0) {
        if (errorOut) *errorOut = QStringLiteral("libheif 取像素平面失败");
        heif_image_release(img);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return QImage();
    }

    const int w = heif_image_get_width(img, heif_channel_interleaved);
    const int h = heif_image_get_height(img, heif_channel_interleaved);
    if (w <= 0 || h <= 0) {
        if (errorOut) *errorOut = QStringLiteral("libheif 图像尺寸无效");
        heif_image_release(img);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        return QImage();
    }

    QImage out;
    if (highDepth) {
        const int rangeBits = heif_image_get_bits_per_pixel_range(img, heif_channel_interleaved);
        out = toneMapSdr
            ? toneMapRgb16LeToRgb888(data, stride, w, h)
            : ditherRgb16LeToRgb888(data, stride, w, h, rangeBits > 0 ? rangeBits : lumaBits);
    } else {
        // interleaved_RGB 为 8 位、每像素 3 字节；stride 可能带 padding，逐行拷贝。
        out = QImage(w, h, QImage::Format_RGB888);
        constexpr int bytesPerPixel = 3;
        for (int y = 0; y < h; ++y) {
            std::memcpy(out.scanLine(y), data + static_cast<size_t>(y) * stride,
                        static_cast<size_t>(w) * bytesPerPixel);
        }
    }

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return out;
}
