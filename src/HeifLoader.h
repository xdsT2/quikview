#pragma once

#include <QImage>
#include <QString>
#include <QVector>

// HEIC/AVIF 解码（libheif）。QImageReader 不认 HEIF/AVIF，这里用 libheif 兜底。
// 返回空 QImage 表示该文件不是 HEIF/AVIF 或解码失败。
// toneMapSdr：>8bit 图不抖动截断，改为 ToneMap::Curve16 色调映射后出 8bit
//（HDR 显示开关）；8bit 图两条路径逐字节相同。
QImage loadHeifImage(const QString &path, QString *errorOut = nullptr,
                     bool toneMapSdr = false);

// 探测 HEIF/AVIF 文件是否包含多帧图像序列（如 .avifs / .heics / 动态 .avif）。
bool hasHeifAnimation(const QString &path);

// 解码 HEIF/AVIF 图像序列全部帧与每帧时长（毫秒）。
bool loadHeifAnimationFrames(const QString &path, QVector<QImage> &frames,
                             QVector<int> &delaysMs, QString *errorOut = nullptr);
