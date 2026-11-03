#pragma once

#include <QImage>
#include <QString>

// 相机 RAW 解码（libraw）。QImageReader 不认 RAW，这里用 libraw 兜底。
// 返回空 QImage 表示该文件不是支持的 RAW 或解码失败。
// toneMapSdr：16bit RAW 不再 >>8 截断，改为 ToneMap ACES 曲线映射出 8bit
//（HDR 显示开关）；8bit RAW 两条路径逐字节相同。
QImage loadRawImage(const QString &path, QString *errorOut = nullptr,
                    bool toneMapSdr = false);

// 扩展名是否属于 libraw 能开的 RAW（nef/cr2/arw…）。只判后缀，不碰文件内容，
// 供 ImageCache 在后台线程判断该走 libraw 路径。
bool isRawFilePath(const QString &path);

// 只取 RAW 内嵌预览（libraw unpack_thumb → dcraw_make_mem_thumb）。
// 失败返回空图，调用方回落全尺寸解码。不做 dcraw_process，通常几十毫秒。
QImage loadRawThumbnail(const QString &path);
