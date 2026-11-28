#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include "LoadError.h"

// Radiance HDR（.hdr/.pic）与 OpenEXR（.exr）的线性场景光解码。
//
// 与其它格式的区别：这两类文件的像素是「线性浮点场景光」，不能直接量化成 8bit。
// 这里统一走 HdrPreview 的自动白点 + ACES 肩部曲线，把它压成可看的 SDR 预览；
// 解码层不再逐个通道套固定曲线后截断（旧行为会整片压白，见 BUG-20261128-03）。
//
// 单独成文件的原因：全景组件（soleview-pano）也要读这两类文件，而它不链接
// Heif / PSD / ZIP 那一大片，把这两条解码路径抽出来即可共用。
namespace HdrExrDecode {

bool isHdrSuffix(const QString &suffixLower);
bool isExrSuffix(const QString &suffixLower);

// 解析失败时 *errOut / *catOut 与 ExtraImageLoader 既有口径一致。
// exposureEv：曝光补偿（每 ±1 档线性值 ×2 / ×0.5），在色调映射前生效；
// 0 与自动预览（I3-2 验收基线）逐像素一致（H1-8）。
QImage decodeRadiance(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut,
                      float exposureEv = 0.0f);
QImage decodeExr(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut,
                 float exposureEv = 0.0f);

} // namespace HdrExrDecode
