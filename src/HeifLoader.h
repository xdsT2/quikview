#pragma once

#include <QImage>
#include <QString>

// HEIC/AVIF 解码（libheif）。QImageReader 不认 HEIF/AVIF，这里用 libheif 兜底。
// 返回空 QImage 表示该文件不是 HEIF/AVIF 或解码失败。
// toneMapSdr：>8bit 图不抖动截断，改为 ToneMap::Curve16 色调映射后出 8bit
//（HDR 显示开关）；8bit 图两条路径逐字节相同。
QImage loadHeifImage(const QString &path, QString *errorOut = nullptr,
                     bool toneMapSdr = false);
