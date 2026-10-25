#pragma once

#include <QImage>
#include <QString>

// 相机 RAW 解码（libraw）。QImageReader 不认 RAW，这里用 libraw 兜底。
// 返回空 QImage 表示该文件不是支持的 RAW 或解码失败。
// toneMapSdr：16bit RAW 不再 >>8 截断，改为 ToneMap ACES 曲线映射出 8bit
//（HDR 显示开关）；8bit RAW 两条路径逐字节相同。
QImage loadRawImage(const QString &path, QString *errorOut = nullptr,
                    bool toneMapSdr = false);
