#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include "LoadError.h"

// 矢量 SVG / SVGZ 打开（派单《格式扩展三件》2026-11-18，T1 补齐 .svgz）。
//
// 白名单仍然只有一份：`DirectoryLoader.cpp` 的 kImageGroups 里加一项 svg，
// 目录扫描、文件对话框过滤器、设置页「全部支持格式」都从那张表派生。
// 本模块只负责「SVG 文件 → QImage」这一步，与 PsdLoader 同一定位。
//
// 技术路线（盘点结论）：QSvgRenderer（Qt6::Svg，主程序已因 Fluent 图标硬依赖
// Qt6Svg.dll），**不走 imageformats/qsvg 插件**：
//  - 插件 read() 按 SVG 自带的 width/height 一次栅格化，没有外部可调的栅格上限，
//    viewBox 50000px 的样张会直接申请同等量级位图（56MB/边，平方级爆内存）；
//  - QSvgRenderer 由调用方给栅格尺寸，单边/总像素上限在本模块兜住；
//  - 插件只是「能不能读」的开关，读回什么尺寸不可控，栏图/缩略图/全图三处
//    需要不同尺寸，插件做不到。
// 结果带 alpha（SVG 常带透明底），画布透明处理沿用既有棋盘逻辑，不新增开关。
namespace SvgLoader {

bool isSvgPath(const QString &path);

// 栅格化到 QImage。
//  box          视口/缩略图请求盒（可为空：按 SVG 自带尺寸渲染）
//  allowUpscale true  —— 全图：小于视口时放大到视口像素（100% 查看不糊）
//                false —— 缩略图/拖动预览：只缩不放
// 失败返回空 QImage：error 是英文技术细节（只进诊断报告，不直接上屏），
// category 由 loader 层定性，UI 只按类别取文案（与 PSD/魔数白名单同一约定）。
QImage render(const QString &path, const QSize &box, bool allowUpscale,
              QString *error = nullptr, LoadErrorCategory *category = nullptr);

}   // namespace SvgLoader
