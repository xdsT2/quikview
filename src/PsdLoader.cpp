#include "PsdLoader.h"

#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QPainter>

// MolecularMatters/psd_sdk（BSD-2-Clause，vendor 在 third_party/psd_sdk）。
// 这里只做「二进制解析 -> 本项目 PsdDocument」的适配，不复制其实现代码。
#include "Psd.h"
#include "PsdPlatform.h"
#include "PsdMallocAllocator.h"
#include "PsdNativeFile.h"
#include "PsdDocument.h"
#include "PsdColorMode.h"
#include "PsdLayer.h"
#include "PsdChannel.h"
#include "PsdChannelType.h"
#include "PsdLayerType.h"
#include "PsdLayerMaskSection.h"
#include "PsdImageDataSection.h"
#include "PsdPlanarImage.h"
#include "PsdParseDocument.h"
#include "PsdParseLayerMaskSection.h"
#include "PsdParseImageDataSection.h"
#include "PsdBlendMode.h"
#include "PsdFixedSizeString.h"

#include <algorithm>

namespace {

QString fromUtf16Name(const uint16_t *p)
{
    if (!p) return {};
    QString s;
    while (*p) {
        s.append(QChar(ushort(*p)));
        ++p;
    }
    return s;
}

QString fourCC(quint32 key)
{
    char c[5] = { char((key >> 24) & 0xff), char((key >> 16) & 0xff),
                  char((key >> 8) & 0xff), char(key & 0xff), 0 };
    return QString::fromLatin1(c, 4);
}

// 从平面通道数据组装 QImage。16bit 按 >>8、32bit float 按 *255 降采样到 8bit，
// 由调用方在 limitations 里如实标注，不冒充原始精度。
QImage makeImage(int w, int h, const void *r, const void *g, const void *b,
                 const void *a, int sampleBytes, bool hasAlpha)
{
    if (w <= 0 || h <= 0 || !r || !g || !b) return {};
    QImage img(w, h, hasAlpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (img.isNull()) return {};
    QRgb *px = reinterpret_cast<QRgb *>(img.bits());
    auto get = [&](const void *p, int i) -> uchar {
        if (!p) return 0;
        if (sampleBytes == 1) return uchar(static_cast<const uchar *>(p)[i]);
        if (sampleBytes == 2) return uchar(static_cast<const uint16_t *>(p)[i] >> 8);
        const float v = static_cast<const float *>(p)[i];
        return uchar(std::min(255, std::max(0, int(v * 255.0f))));
    };
    const int total = w * h;
    for (int i = 0; i < total; ++i)
        px[i] = qRgba(get(r, i), get(g, i), get(b, i), hasAlpha ? get(a, i) : 255);
    return img;
}

} // namespace

namespace PsdLoader {

bool isPsdPath(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    return s == QLatin1String("psd") || s == QLatin1String("psb");
}

PsdDocument load(const QString &path, bool withLayerPixels)
{
    PsdDocument out;
    psd::MallocAllocator allocator;
    psd::NativeFile file(&allocator);
    const std::wstring nativePath = path.toStdWString();
    if (!file.OpenRead(nativePath.c_str())) {
        out.error = QStringLiteral("无法打开 PSD 文件");
        return out;
    }
    psd::Document *doc = psd::CreateDocument(&file, &allocator);
    if (!doc) {
        file.Close();
        out.error = QStringLiteral("不是有效的 PSD/PSB 文件");
        return out;
    }

    const int w = int(doc->width);
    const int h = int(doc->height);
    out.canvasSize = QSize(w, h);
    const unsigned bits = doc->bitsPerChannel;
    const int sampleBytes = bits == 8 ? 1 : (bits == 16 ? 2 : 4);
    const bool rgb = (doc->colorMode == psd::colorMode::RGB);
    if (w <= 0 || h <= 0) {
        psd::DestroyDocument(doc, &allocator);
        file.Close();
        out.error = QStringLiteral("PSD 画布尺寸异常");
        return out;
    }
    // 前置隔离：psd_sdk 的 ParseLayerMaskSection 在 ZIP 压缩的层结构上会死循环
    // （实测 psd-zip.psd 挂起 18s+）。ZIP 属边界样本，这里直接明确报错，
    // 绝不让进程进入会挂起的第三方路径；能力限制由回执如实记录。
    {
        QFile probe(path);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray d = probe.readAll();
            auto be32 = [&](int q) -> quint32 {
                if (q < 0 || q + 4 > d.size()) return 0;
                const uchar *p = reinterpret_cast<const uchar *>(d.constData()) + q;
                return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
                       | (quint32(p[2]) << 8) | quint32(p[3]);
            };
            int q = 26;
            q += 4 + int(be32(q));   // color mode data
            q += 4 + int(be32(q));   // image resources
            q += 4 + int(be32(q));   // layer & mask info -> Image Data 起点
            if (q + 2 <= d.size()) {
                const quint16 comp = (quint16(uchar(d.at(q))) << 8)
                                     | quint16(uchar(d.at(q + 1)));
                if (comp == 2 || comp == 3) {
                    out.error = QStringLiteral("ZIP 压缩的 PSD 暂不支持（已隔离第三方解析器的挂起路径）");
                    out.limitations << QStringLiteral("ZIP / ZIP+Prediction 未接入，文件未修改");
                    psd::DestroyDocument(doc, &allocator);
                    file.Close();
                    return out;
                }
            }
        }
    }

    if (!rgb)
        out.limitations << QStringLiteral("仅 RGB 色彩模式可显示（当前 mode=%1）").arg(int(doc->colorMode));
    if (bits != 8)
        out.limitations << QStringLiteral("%1bit 像素已降采样为 8bit 显示").arg(bits);

    // ---- 原始合成预览（Image Data 段 / Section 5）----
    // 派单要求「真实 PSD 整图仍用可用的原始合成预览」：只用 Photoshop 保存的
    // 合成结果，绝不逐层重合成冒充。没有该段就明确报错，不静默降级。
    {
        psd::ImageDataSection *section = psd::ParseImageDataSection(doc, &file, &allocator);
        if (section && section->imageCount >= 3 && rgb) {
            const void *p0 = section->images[0].data;
            const void *p1 = section->images[1].data;
            const void *p2 = section->images[2].data;
            const bool hasAlpha = section->imageCount >= 4;
            const void *p3 = hasAlpha ? section->images[3].data : nullptr;
            out.composite = makeImage(w, h, p0, p1, p2, p3, sampleBytes, hasAlpha);
        }
        psd::DestroyImageDataSection(section, &allocator);
        if (out.composite.isNull())
            out.error = QStringLiteral("缺少可用的原始合成预览（保存 PSD 时请开启「最大兼容」）");
    }

    // ---- 图层树与单层像素 ----
    psd::LayerMaskSection *lms = psd::ParseLayerMaskSection(doc, &file, &allocator);
    if (lms) {
        int nonNormal = 0, masked = 0, noPixel = 0;
        for (unsigned i = 0; i < lms->layerCount; ++i) {
            psd::Layer *L = &lms->layers[i];
            PsdLayer layer;
            const int lw = int(L->right - L->left);
            const int lh = int(L->bottom - L->top);
            layer.bounds = QRect(int(L->left), int(L->top), lw, lh);
            layer.visible = L->isVisible;
            layer.opacity = int(L->opacity);
            layer.blendKey = fourCC(L->blendModeKey);
            layer.groupStart = (L->type == psd::layerType::OPEN_FOLDER
                                || L->type == psd::layerType::CLOSED_FOLDER);
            layer.groupEnd = (L->type == psd::layerType::SECTION_DIVIDER);

            const QString uni = fromUtf16Name(L->utf16Name);
            const QString ascii = QString::fromUtf8(L->name.c_str());
            layer.name = !uni.isEmpty() ? uni
                       : !ascii.isEmpty() ? ascii
                       : QStringLiteral("图层 %1").arg(i + 1);

            // psd_sdk 已建立 parent 链，用它得到分组深度，比 lsct 推导更准
            int depth = 0;
            for (psd::Layer *p = L->parent; p; p = p->parent)
                ++depth;
            layer.groupDepth = depth;

            if (psd::blendMode::KeyToEnum(L->blendModeKey) != psd::blendMode::NORMAL) {
                ++nonNormal;
                layer.limitation = QStringLiteral("混合模式 %1 未在简化合成中还原").arg(layer.blendKey);
            }
            if (L->layerMask || L->vectorMask) {
                ++masked;
                layer.limitation += QStringLiteral("；蒙版未应用");
            }
            if (L->smartObject)
                layer.limitation += QStringLiteral("；智能对象未展开");

            const bool groupMarker = (L->type != psd::layerType::ANY);
            if (groupMarker || lw <= 0 || lh <= 0 || !rgb) {
                layer.previewable = false;
                layer.limitation += groupMarker
                    ? QStringLiteral("；分组边界，无可预览像素")
                    : QStringLiteral("；无独立像素或色彩模式不支持");
                ++noPixel;
            } else if (withLayerPixels) {
                psd::ExtractLayer(doc, &file, &allocator, L);
                const void *r = nullptr, *g = nullptr, *b = nullptr, *a = nullptr;
                for (unsigned c = 0; c < L->channelCount; ++c) {
                    const psd::Channel &ch = L->channels[c];
                    switch (ch.type) {
                    case psd::channelType::R: r = ch.data; break;
                    case psd::channelType::G: g = ch.data; break;
                    case psd::channelType::B: b = ch.data; break;
                    case psd::channelType::TRANSPARENCY_MASK: a = ch.data; break;
                    default: break;
                    }
                }
                if (r && g && b)
                    layer.image = makeImage(lw, lh, r, g, b, a, sampleBytes, a != nullptr);
                if (layer.image.isNull()) {
                    layer.previewable = false;
                    layer.limitation += QStringLiteral("；图层像素解码失败");
                    ++noPixel;
                }
            } else {
                layer.previewable = false;   // 仅取元信息，单层预览不可用
            }
            out.layers.append(layer);
        }
        if (nonNormal)
            out.limitations << QStringLiteral("%1 个图层为非 normal 混合，简化合成不还原").arg(nonNormal);
        if (masked)
            out.limitations << QStringLiteral("%1 个图层带蒙版，简化合成不应用").arg(masked);
        if (noPixel)
            out.limitations << QStringLiteral("%1 个图层无独立像素（分组边界 / 调整层）").arg(noPixel);
        psd::DestroyLayerMaskSection(lms, &allocator);
    }
    psd::DestroyDocument(doc, &allocator);
    file.Close();
    return out;
}

QImage compositeFromVisible(const PsdDocument &doc, const QVector<bool> &visible)
{
    if (doc.canvasSize.isEmpty()) return {};
    QImage out(doc.canvasSize, QImage::Format_ARGB32);
    if (out.isNull()) return {};
    out.fill(Qt::transparent);
    QPainter p(&out);
    // psd_sdk 的 layer 顺序为「底层在前」（实测 [0]=背景、[N-1]=最顶），
    // 因此按正序从底往上绘制；面板显示时再倒序成文档顺序。
    for (int i = 0; i < doc.layers.size(); ++i) {
        const PsdLayer &L = doc.layers.at(i);
        if (!L.previewable || L.image.isNull()) continue;
        if (i < visible.size() && !visible.at(i)) continue;
        p.setOpacity(qreal(L.opacity) / 255.0);
        p.drawImage(L.bounds.topLeft(), L.image);
    }
    p.end();
    return out;
}

QImage layerOnCanvas(const PsdDocument &doc, int index)
{
    if (doc.canvasSize.isEmpty() || index < 0 || index >= doc.layers.size()) return {};
    QImage out(doc.canvasSize, QImage::Format_ARGB32);
    if (out.isNull()) return {};
    out.fill(Qt::transparent);
    const PsdLayer &L = doc.layers.at(index);
    if (L.previewable && !L.image.isNull()) {
        QPainter p(&out);
        p.drawImage(L.bounds.topLeft(), L.image);
        p.end();
    }
    return out;
}

} // namespace PsdLoader
