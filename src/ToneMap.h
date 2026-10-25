#pragma once

#include <QImage>

// HDR → SDR 色调映射（显示用途：Qt 6 Windows 后端只出 8bit SDR，不做真 HDR 输出）。
// 硬规则：8bit/SDR 图原样返回（同一份 QImage，一个像素都不动）；
// 高位深图（RGBA64/Grayscale16 等）按 99.5% 亮度分位归一化白点 +
// ACES 近似曲线压到 8bit，高光收肩保留细节。
namespace ToneMap {

// 16bit 亮度样本 → 映射表（输入 16bit 原值 → 输出 8bit）。
// 供 HeifLoader / RawImageLoader 在解码层直接用原始平面做映射（那时还没有 QImage）。
struct Curve16 {
    quint8 map[65536] = {};
};

// rec709 加权亮度抽样（≤40 万样本，输入 0..65535）。
// 同文件内的 toneMapToSdr 用它；loader 侧在拿到 QImage 前也能对齐这个口径抽样。
QVector<quint32> sampleLuma16(const QImage &image);

// lumaSamples：按 rec709 加权折算出的 16bit 亮度样本（抽样即可）。
Curve16 buildCurve16(const QVector<quint32> &lumaSamples);

// 通用入口：检测位深，8bit 原样返回；16bit 整型格式做映射，浮点格式先转 RGBA64。
QImage toneMapToSdr(const QImage &image);

} // namespace ToneMap
