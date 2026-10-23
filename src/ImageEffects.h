#pragma once

#include <QImage>

// 图像效果纯函数（无状态）：柔化与锐化。

// 轻微柔化：3x3 均值模糊。
QImage applySoften(const QImage &src);

// 锐化：3x3 unsharp 核（中心 5、四邻 -1，其余 0，归一化）。
QImage applySharpen(const QImage &src);
