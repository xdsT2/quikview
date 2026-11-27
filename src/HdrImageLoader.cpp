// SPDX-License-Identifier: MIT
#include "HdrImageLoader.h"

#include "BoxDownsampler.h"
#include "ExrStream.h"
#include "HdrError.h"
#include "HdrRadiance.h"
#include "RenderSdr.h"

#include <QColorSpace>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// 显示纹理上限：4K 原生（4096×2048 ≤ 上限，原样保留）；8K/16K 先按 8192×4096
// 评估，若实测峰值内存或首帧耗时不可接受，再降到 4096×2048（见交付说明）。
// 默认值固定 8192×4096；QUIKVIEW_HDR_TEXW/TEXH 仅为内存受限场景的复测开关，
// 不改变产品默认行为。
constexpr int kDefaultTextureW = 8192;
constexpr int kDefaultTextureH = 4096;

int textureCapW()
{
    bool ok = false;
    const int v = qEnvironmentVariable("QUIKVIEW_HDR_TEXW").toInt(&ok);
    return ok && v > 0 ? v : kDefaultTextureW;
}

int textureCapH()
{
    bool ok = false;
    const int v = qEnvironmentVariable("QUIKVIEW_HDR_TEXH").toInt(&ok);
    return ok && v > 0 ? v : kDefaultTextureH;
}

bool isHdrSuffix(const QString &s) { return s == QLatin1String("hdr") || s == QLatin1String("rgbe"); }
bool isExrSuffix(const QString &s) { return s == QLatin1String("exr"); }

// 内核分类 → 产品 LoadErrorCategory。
// Corrupt → Corrupt；Unsupported → Unsupported；Io（打不开/IO/内存）按现有惯例
// 归 Corrupt（与 ImageCache::classifyByMagic「打不开＝I/O 层问题」同一口径）。
LoadErrorCategory mapCategory(hdrprobe::HdrCategory c)
{
    switch (c) {
    case hdrprobe::HdrCategory::Corrupt:    return LoadErrorCategory::Corrupt;
    case hdrprobe::HdrCategory::Unsupported: return LoadErrorCategory::Unsupported;
    case hdrprobe::HdrCategory::Io:         return LoadErrorCategory::Corrupt;
    default:                                 return LoadErrorCategory::None;
    }
}

// 失败文案：直接采用内核稳定中文文案（HdrError::ZhText，message 已带该前缀 + 细节）。
// 类别由本层定性，UI 不做字符串匹配。
void fillFailure(QString *errorOut, LoadErrorCategory *category,
                 const hdrprobe::HdrError &err)
{
    if (errorOut) {
        // 类别前缀（「文件损坏：…」/「格式不支持：…」）始终由 ZhText 给出，
        // 内核 message 只作细节追加；避免个别截断类错误丢掉「损坏/不支持」区分。
        const QString base = QString::fromUtf8(hdrprobe::HdrError::ZhText(err.code));
        const QString detail = QString::fromUtf8(err.message);
        if (detail.isEmpty()) {
            *errorOut = base;
        } else if (detail.startsWith(base)) {
            *errorOut = detail;
        } else {
            *errorOut = base + QStringLiteral("；") + detail;
        }
    }
    if (category) {
        *category = mapCategory(err.category);
    }
}

QString exrDepthLabel(const hdrprobe::ExrStream &exr)
{
    const bool half = exr.pixelType() == 1;
    const QString base = half ? QStringLiteral("16-bit half float")
                               : QStringLiteral("32-bit float");
    const QString channels = exr.hasAlpha() ? QStringLiteral("RGBA") : QStringLiteral("RGB");
    const QString comp = exr.compression() == 2 ? QStringLiteral("ZIPS") : QStringLiteral("ZIP");
    return QStringLiteral("%1 (%2, %3)").arg(base, channels, comp);
}

// 目标尺寸：源图不超过上限时原生保留；超过则按 2:1 等比压到上限。
// box（视口/缩略图盒）有效时再向 box 内收缩，只缩不放。
QSize targetSize(int srcW, int srcH, const QSize &box)
{
    QSize cap(srcW, srcH);
    const int maxW = textureCapW();
    const int maxH = textureCapH();
    if (cap.width() > maxW || cap.height() > maxH) {
        cap = QSize(maxW, maxH);
    }
    if (box.isValid() && box.width() > 0 && box.height() > 0) {
        QSize fit = cap.scaled(box, Qt::KeepAspectRatio);
        if (fit.width() <= 0 || fit.height() <= 0) {
            fit = cap;
        }
        return fit;
    }
    return cap;
}

// sRGB 编码 8bit LUT：线性 [0,1] → 8bit，1024 级。LUT 表项由内核同一公式
// hdrprobe::srgbEncode 预计算，像素口径不变；避免逐通道 std::pow 在 4K/8K/16K
// 目标帧上成为首帧瓶颈（pow 是这里最大的单项开销）。
class SrgbLut {
public:
    SrgbLut()
    {
        for (int i = 0; i < 1024; ++i) {
            float v = static_cast<float>(i) / 1023.0f;
            v = hdrprobe::srgbEncode(v);
            v = std::min(1.0f, std::max(0.0f, v));
            m_lut[i] = static_cast<uint8_t>(std::lround(v * 255.0f));
        }
    }
    uint8_t operator()(float v) const
    {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        const int idx = std::min(1023, static_cast<int>(v * 1023.0f + 0.5f));
        return m_lut[idx];
    }
private:
    uint8_t m_lut[1024]{};
};

// 线性浮点帧 → sRGB 编码 8bit QImage。
//
// 色彩管线顺序（硬约束，谁先谁后）：
//   线性 float → 曝光增益 ×2^EV（施加在线性数据上）→ Reinhard 色调映射 →
//   sRGB 编码 → 夹取到 [0,1] → 8bit。
// 曝光必须在 sRGB 编码之前；本函数输入已是线性，输出已编码。
// 输出写回 QColorSpace::SRgb：MainWindow 4495 行的 ToneMap::toneMapToSdr 对
// 8bit 图原样放行，4485-4494 的 toSrgb 看到已是 sRGB 空间会跳过转换——
// 因此这里绝不把线性浮点图交给 ToneMap 二次映射。
QImage renderFrameToImage(const float *frame, int w, int h, float ev)
{
    QImage out(w, h, QImage::Format_RGB888);
    if (out.isNull()) {
        return QImage();
    }
    static const SrgbLut srgb;
    const float gain = std::pow(2.0f, ev);
    for (int y = 0; y < h; ++y) {
        const float *src = frame + (size_t)y * w * 3;
        uint8_t *line = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float *sp = src + x * 3;
            uint8_t *px = line + x * 3;
            for (int k = 0; k < 3; ++k) {
                float v = sp[k] * gain;
                if (v < 0.0f) v = 0.0f;
                v = v / (1.0f + v);                 // Reinhard（与基线口径一致）
                px[k] = srgb(v);                    // sRGB 编码（在线性映射之后）
            }
        }
    }
    out.setColorSpace(QColorSpace::SRgb);
    return out;
}

} // namespace

namespace HdrImageLoader {

bool isHdrOrExrPath(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return isHdrSuffix(s) || isExrSuffix(s);
}

bool probeHeader(const QString &path, int &width, int &height,
                  QString &depthLabel, bool &isExr)
{
    width = 0;
    height = 0;
    depthLabel.clear();
    isExr = false;

    const QString suffix = QFileInfo(path).suffix().toLower();
    const QByteArray utf8 = path.toUtf8();
    const char *cpath = utf8.constData();

    if (isExrSuffix(suffix)) {
        hdrprobe::ExrStream exr;
        hdrprobe::HdrError err = exr.open(cpath);
        if (err.code != hdrprobe::HdrStatus::Ok) {
            return false;
        }
        width = exr.width();
        height = exr.height();
        isExr = true;
        depthLabel = exrDepthLabel(exr);
        return true;
    }
    if (isHdrSuffix(suffix)) {
        hdrprobe::HdrRadiance hdr;
        hdrprobe::HdrError err = hdr.open(cpath);
        if (err.code != hdrprobe::HdrStatus::Ok) {
            return false;
        }
        width = hdr.width();
        height = hdr.height();
        depthLabel = QStringLiteral("32-bit float (RGBE)");
        return true;
    }
    return false;
}

float defaultEv()
{
    const QString v = qEnvironmentVariable("QUIKVIEW_HDR_EV");
    if (!v.isEmpty()) {
        bool ok = false;
        const float f = v.toFloat(&ok);
        if (ok) {
            return std::min(8.0f, std::max(-8.0f, f));
        }
    }
    return 0.0f;
}

QImage load(const QString &path, const QSize &box, float ev,
            QString *errorOut, LoadErrorCategory *category)
{
    if (errorOut) errorOut->clear();
    if (category) *category = LoadErrorCategory::None;

    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool wantExr = isExrSuffix(suffix);
    const bool wantHdr = isHdrSuffix(suffix);
    if (!wantExr && !wantHdr) {
        return QImage();
    }

    const QByteArray utf8 = path.toUtf8();
    const char *cpath = utf8.constData();

    int srcW = 0, srcH = 0;
    QSize dst;
    hdrprobe::HdrError err;
    QImage out;

    if (wantExr) {
        // ---- EXR：流式逐行解码 → 盒式降采样 → 逐行映射编码 ----
        hdrprobe::ExrStream exr;
        err = exr.open(cpath);
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(errorOut, category, err);
            return QImage();
        }
        srcW = exr.width();
        srcH = exr.height();
        dst = targetSize(srcW, srcH, box);
        // 降采样器作用域：内部只驻留「一行源行缓冲 + 一份目标累加帧」；
        // 渲染直接从累加帧逐行读，不再另拷一份整帧线性缓冲，渲染完作用域结束即释放。
        hdrprobe::BoxDownsampler ds(srcW, srcH, dst.width(), dst.height());
        std::vector<float> row(static_cast<size_t>(srcW) * 3);
        int srcY = 0;
        while (exr.nextRow(row.data(), srcY, err)) {
            ds.pushRow(srcY, row.data());
        }
        // nextRow 正常 EOF 时 err.code==Ok；其余为解码错误。
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(errorOut, category, err);
            return QImage();
        }
        ds.finish();
        out = renderFrameToImage(ds.frameData(), dst.width(), dst.height(), ev);
    } else {
        // ---- Radiance RGBE：同构链路 ----
        hdrprobe::HdrRadiance hdr;
        err = hdr.open(cpath);
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(errorOut, category, err);
            return QImage();
        }
        srcW = hdr.width();
        srcH = hdr.height();
        dst = targetSize(srcW, srcH, box);
        hdrprobe::BoxDownsampler ds(srcW, srcH, dst.width(), dst.height());
        std::vector<float> row(static_cast<size_t>(srcW) * 3);
        int srcY = 0;
        while (hdr.nextRow(row.data(), srcY, err)) {
            ds.pushRow(srcY, row.data());
        }
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(errorOut, category, err);
            return QImage();
        }
        ds.finish();
        out = renderFrameToImage(ds.frameData(), dst.width(), dst.height(), ev);
    }

    return out;
}

} // namespace HdrImageLoader
