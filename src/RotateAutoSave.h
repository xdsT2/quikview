#pragma once

#include <QImage>
#include <QString>

// 旋转后自动保存（A2）的写回链路，与界面解耦：
//   unsupportedReason —— 格式白名单判定（RAW / 动图 / 多页 TIFF / PSD 跳过）；
//   writeBack         —— 原子写回：同目录临时文件 → fsync → 替换原文件，
//                        失败时原文件完好；JPEG 保留原 EXIF 且 Orientation 归 1。
namespace RotateAutoSave {

// 返回跳过原因键（PSD/RAW/ANIM/MULTITIFF/OTHER）；空串 = 支持写回。
QString unsupportedReason(const QString &path);

// 原子写回 image 到 path。成功 true；失败 false 且 err 为可展示原因，原文件不变。
bool writeBack(const QString &path, const QImage &image, QString &err);

}   // namespace RotateAutoSave