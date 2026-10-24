#pragma once

#include <QImage>

// 图像效果纯函数（无状态）：柔化与锐化。

// 轻微柔化：3x3 均值模糊。
QImage applySoften(const QImage &src);

// 锐化：3x3 unsharp 核（中心 5、四邻 -1，其余 0，归一化）。
QImage applySharpen(const QImage &src);

// 反相：RGB 三个通道取补，Alpha 原样保留（透明 PNG 反相后仍透明）。
QImage applyNegative(const QImage &src);

// Gamma 补偿：out = 255 * (in/255)^(1/gamma)。
// gamma > 1 变亮、< 1 变暗，1.0 原样返回；用 256 项查找表，整图只算一次表。
QImage applyGamma(const QImage &src, qreal gamma);
