#pragma once

#include <QString>

// 360° 全景识别（派单 2026-11-20）。
// ---------------------------------------------------------------------------
// 只做「这张 JPG 能不能按完整球形打开」的判定，不渲染、不碰界面。
// 判定依据是 JPG 内嵌 XMP 的 GPano 字段
// （https://developers.google.com/streetview/spherical-metadata）：
//   ProjectionType / UsePanoramaViewer / CroppedAreaImageWidthPixels /
//   CroppedAreaImageHeightPixels / FullPanoWidthPixels / FullPanoHeightPixels /
//   CroppedAreaLeftPixels / CroppedAreaTopPixels /
//   InitialViewHeadingDegrees / InitialViewPitchDegrees / InitialViewRollDegrees /
//   InitialViewVerticalFOVDegrees
//
// 判定口径（派单「识别结果与入口」表）：
//   Complete  完整球形 + 未要求关闭提示 → 主窗常驻入口 + 右键入口；
//   Suppressed 完整球形但 UsePanoramaViewer=false → 只保留右键手动入口；
//   AspectOnly 无有效标记但实际尺寸是 2:1 宽图 → 只给右键「尝试以 360° 查看」；
//   Unsupported 明确局部裁切 / 非等距柱状投影 / 标记与图片冲突 → 不提供入口，
//               首版不把局部图强行绕满一圈；
//   NotFound   其它（无标记且不是 2:1）。
// 等比例缩图（标记仍写原尺寸）只有在宽高缩放比一致时才按完整球形认，避免
// 「4:3 的普通照片带着别人的标记」被当成全景。
namespace Pano {

enum class Kind {
    NotFound = 0,
    Complete = 1,
    Suppressed = 2,
    AspectOnly = 3,
    Unsupported = 4,
};

struct Info {
    Kind kind = Kind::NotFound;
    // autoEntry：主窗是否常驻「360° 查看」入口（点一次即打开全景）。
    // manualEntry：右键是否给「尝试以 360° 查看」。
    bool autoEntry = false;
    bool manualEntry = false;
    // 合法初始视角（取自 GPano InitialView*；不合法则回落 0/0/0/75）。
    double heading = 0.0;   // 度，0 指向等距柱状图正中
    double pitch = 0.0;     // 度，正数向上
    double roll = 0.0;      // 度，首版不参与渲染，只记录
    double fov = 75.0;      // 垂直接管视场角（度）
    QString detail;         // 识别依据（探针、回执与 tooltip 用）

    bool eligible() const { return autoEntry || manualEntry; }
};

// 路径探针：仅支持本地存在的 JPG（含 .jpeg/.jfif）。压缩包内条目、其它格式、
// 文件不可读一律返回 NotFound（detail 写原因）。
Info probeFile(const QString &path);

QString kindName(Kind kind);

} // namespace Pano
