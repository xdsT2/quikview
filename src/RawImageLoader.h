#pragma once

#include <QImage>
#include <QString>

// 相机 RAW 解码（libraw）。QImageReader 不认 RAW，这里用 libraw 兜底。
// 返回空 QImage 表示该文件不是支持的 RAW 或解码失败。
QImage loadRawImage(const QString &path, QString *errorOut = nullptr);
