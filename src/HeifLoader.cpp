#include "HeifLoader.h"

#include <libheif/heif.h>

#include <QFileInfo>

#include <cstring>

QImage loadHeifImage(const QString &path, QString *errorOut)
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

    heif_image *img = nullptr;
    err = heif_decode_image(handle, &img, heif_colorspace_RGB, heif_chroma_interleaved_RGB, nullptr);
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

    // interleaved_RGB 为 8 位、每像素 3 字节；stride 可能带 padding，逐行拷贝。
    QImage out(w, h, QImage::Format_RGB888);
    constexpr int bytesPerPixel = 3;
    for (int y = 0; y < h; ++y) {
        std::memcpy(out.scanLine(y), data + static_cast<size_t>(y) * stride,
                    static_cast<size_t>(w) * bytesPerPixel);
    }

    heif_image_release(img);
    heif_image_handle_release(handle);
    heif_context_free(ctx);
    return out;
}
