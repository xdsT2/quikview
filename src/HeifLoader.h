#pragma once

#include <QImage>
#include <QString>

// HEIC/AVIF 解码（libheif）。QImageReader 不认 HEIF/AVIF，这里用 libheif 兜底。
// 返回空 QImage 表示该文件不是 HEIF/AVIF 或解码失败。
QImage loadHeifImage(const QString &path, QString *errorOut = nullptr);
