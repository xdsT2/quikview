#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include "LoadError.h"

// HDR（Radiance RGBE / .hdr）与 EXR（单部件 scanline ZIP / ZIPS，.exr）平面打开链路。
//
// 定位与 RawImageLoader / HeifLoader / SvgLoader 同一层：解码在后台线程内完成，
// 到这里已经是 sRGB 编码的 8bit QImage；上层 MainWindow 的 ToneMap/ICC 链路
// 对本 loader 的输出只做直通，不会二次映射（见 .cpp 内「色彩管线顺序」注释）。
//
// 支持范围（与解码内核 hdrprobe 一致，本轮不扩展）：
//   - EXR：单部件、scanline、ZIP(3)/ZIPS(2) deflate，FLOAT/HALF，R/G/B 必需、A 可选；
//   - HDR：32-bit_rle_rgbe（新式 4 通道 RLE 与旧式扁平排列）；
//   - 不支持：tiled、multi-part、DEEP、其它压缩（RLE/PIZ/PXR24/B44/DWA/HTJ2K）、
//     UINT 位深、非 1 采样率、未识别通道。
namespace HdrImageLoader {

// 后缀判定（.hdr/.rgbe/.exr），只读路径字符串，不碰文件内容。
bool isHdrOrExrPath(const QString &path);

// 只解析文件头（不解码像素）：返回源文件真实宽高、位深/通道类型文案、是否 EXR。
// 供信息面板、总览分组、全景比例门禁使用；失败返回 false。
// depthLabel 示例：`32-bit float (RGBE)`、`32-bit float (RGB, ZIP)`、
// `16-bit half float (RGBA, ZIP)`。
bool probeHeader(const QString &path, int &width, int &height,
                  QString &depthLabel, bool &isExr);

// 解码链路：流式逐行解码 → 盒式降采样 → 曝光(×2^EV) → Reinhard 色调映射 →
// sRGB 编码 → 8bit QImage。
//   box：视口/缩略图请求盒（可为空：按内部纹理上限出全图）；
//   ev：曝光补偿档（默认 0；+2 提亮两档、-2 压暗两档）；
// 输出图已写回 QColorSpace::SRgb。失败返回空 QImage，errorOut 给可读文案，
// category 由 loader 层定性（与 PSD/SVG 同一约定，UI 只按类别取文案）。
QImage load(const QString &path, const QSize &box, float ev,
            QString *errorOut = nullptr, LoadErrorCategory *category = nullptr);

// 当前进程默认 EV：读环境变量 QUIKVIEW_HDR_EV（浮点，夹在 -8..+8），缺省 0。
// 命令行复现三档对照：启动前设 QUIKVIEW_HDR_EV=-2 / 0 / +2。
float defaultEv();

} // namespace HdrImageLoader
