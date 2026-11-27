#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

#include "LoadError.h"

// 扩展图像解码与多帧动画支持（T2 零新 DLL 格式 + T3 专业/现代格式）。
//
// 所有第三方/自研解码器（除 Windows WIC/GDI+ 原生宽字符流外）均先通过 QFile
// 将字节读入内存再解析，避免 Windows 中文路径与编码页差异导致的打开失败。
//
// 统一安全上限：
//   - 单文件读入上限 256 MB（矢量 WMF/EMF 限 8 MB）
//   - 光栅单边 <= 30000 px、总像素 <= 100,000,000（矢量栅格单边 <= 4096、总像素 <= 800 万）
//   - 动图解码上限 512 帧、累计帧像素内存 <= 256 MB
namespace ExtraImageLoader {

// 扩展名是否属于本模块主解码路径（跳过 QImageReader 直接走 ExtraImageLoader）。
// 包含：qoi, hdr, pic, dds, jxr, wdp, hdp, wmf, emf, ani, pcx, sgi, rgb, rgba,
// bw, int, inta, pam, pfm, ras, sun, ff, xwd, dpx, apng,
// 以及 T3 的 jp2, j2k, j2c, jpf, jpx, jxl, exr, kra, ora。
bool isExtraPath(const QString &path);

// 是否属于「可能含多帧动画」的候选扩展名（用于 FrameNavigator 快速分流）：
// webp, png, apng, ani, avif, avifs, heics, jxl。
bool isAnimatedCandidatePath(const QString &path);

// 是否为矢量元数据图（WMF/EMF）：需按视口盒栅格化，预读阶段应跳过。
bool isVectorExtraPath(const QString &path);

// 是否仅支持从文件/容器路径读取（不可作为 ZIP/CBZ 内部条目无差分解码的格式）。
bool isArchiveExcludedExt(const QString &extLower);

// 快速头部探测该文件是否实际包含 >= 2 帧动画；单帧 PNG/WebP/AVIF/JXL 应尽快返回 false。
bool hasMultipleFrames(const QString &path);

// 主解码入口（静态图或动画首帧）。viewportHint 对 WMF/EMF 等矢量格式生效。
QImage load(const QString &path, const QSize &viewportHint = QSize(),
            QString *errorOut = nullptr, LoadErrorCategory *categoryOut = nullptr,
            bool toneMapSdr = false);

// 解码完整动画帧与每帧时长（毫秒）。支持 Animated WebP、APNG（.apng/.png）、
// ANI 动标、Animated AVIF/HEICS（T3）、Animated JXL（T3）。
bool loadAnimationFrames(const QString &path, QVector<QImage> &frames,
                         QVector<int> &delaysMs, QString *errorOut = nullptr);

} // namespace ExtraImageLoader
