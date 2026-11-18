// SVG 软渲染：nanosvg（third_party/nanosvg）解析 + 自带扫描线光栅化。
// 壳扩展是纯 COM/纯 Win32 工程，不能拉 Qt/QtSvg 进来（硬约束见 README），
// 也不依赖系统：Windows 没有内置的 SVG 位图解码器（WIC 不认 SVG），
// 所以这里自带一个单头文件 C 解析器，编译进 DLL，目标机零额外依赖。
#pragma once
#include <windows.h>
#include <objidl.h>

#include "quikthumb.h"

// 内容嗅探：流首部是否为 SVG（XML 文本且出现 <svg 元素）。
// Explorer 只按扩展名注册表项选择本 Handler，拿不到文件名，故按内容判断。
// 读过的位置会回绕到 0，调用后流仍可正常使用。
bool SvgLooksLikeStream(IStream* stm);

// 把流里的 SVG 渲染成 32bpp 预乘 BGRA 的 DIB（WTSAT_ARGB），
// 输出最长边 = cx（小图不缩小、允许放大，矢量放大不糊）。
// 单边硬上限见实现里的 kMaxRasterEdge（防超大 viewBox 炸内存）。
HRESULT SvgRenderThumb(IStream* stm, UINT cx, HBITMAP* phbm,
                        WTS_ALPHATYPE* pat);
