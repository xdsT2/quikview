// SVG → 位图：nanosvg 软渲染实现（third_party/nanosvg，zlib 许可）。
//
// 选型理由（详见回执）：
//   * 壳扩展 DLL 是纯 COM/纯 Win32，不能链接 Qt/QtSvg（会把 Qt 运行时拖进
//     资源管理器进程），Windows 也没有内置 SVG→位图解码器（WIC 不认 SVG）；
//   * nanosvg 是单头文件 C99（解析 nanosvg.h + 光栅化 nanosvgrast.h），
//     编译进 DLL 即零运行时依赖，体积/风险都小；
//   * 代价是能力有限：不支持 <text>（无字体栈）、<filter>、<mask>、
//     <pattern>、clipPath、CSS class（只认 presentation 属性/style 内联），
//     这些按 nanosvg 实际能力如实记进 README 与回执，不假称「完整」。
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cmath>
#include <memory>
#include <vector>

#include "svg_raster.h"

// nanosvg 是第三方 C 代码，会触发本项目（-Wall -Wextra / /W4）的告警噪音。
// 只在包含头文件这一段压掉，不影响本文件的告警档位。
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wswitch"
#pragma GCC diagnostic ignored "-Wfloat-equal"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#elif defined(_MSC_VER)
#pragma warning(push)
// 4127 常量条件 / 4244 4267 窄化 / 4305 截断 / 445x 声明遮蔽 / 4701 4702 不可达
#pragma warning(disable : 4100 4127 4244 4267 4305 4456 4457 4459 4700 4701 4702 4706 4838)
#endif
#define NANOSVG_IMPLEMENTATION
#include "nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvgrast.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

namespace {

// 源文件上限：正常 SVG 都是几十 KB；超过这个尺寸的多半不是图标/插画，
// 解析成本与内存风险不值得背（比 PSD 内嵌缩略图上限还保守）。
const ULONG kMaxSvgBytes = 8u * 1024 * 1024;
// 嗅探窗口：XML 声明 / DOCTYPE / 注释之后才是 <svg，4KB 足够
const ULONG kSniffBytes = 4096;
// 输出单边硬上限：2048² × 4B = 16MB，是 Explorer 请求尺度（≤1024）的 2 倍余量。
// 超大 viewBox（如 50000px）靠按目标尺度缩放渲染，不按原始 viewBox 分配位图。
const UINT kMaxRasterEdge = 2048;

bool ReadBytes(IStream* stm, void* buf, ULONG n, ULONG* got) {
    *got = 0;
    while (*got < n) {
        ULONG chunk = 0;
        if (FAILED(stm->Read((BYTE*)buf + *got, n - *got, &chunk)) ||
            chunk == 0) {
            return false;
        }
        *got += chunk;
    }
    return true;
}

// 位图嗅探：只认「起点是 XML 文本、且出现 <svg 元素」的流。
// 二进制守卫（出现 NUL/控制字符即否）用来挡住 PNG/JPEG 这类误判。
bool SniffSvg(const BYTE* buf, ULONG n) {
    ULONG i = 0;
    // UTF-8 BOM
    if (n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) i = 3;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t' || buf[i] == '\r' ||
                     buf[i] == '\n')) {
        ++i;
    }
    if (i >= n || buf[i] != '<') return false;   // 不是 XML 文本

    for (ULONG k = 0; k < n; ++k) {
        const BYTE b = buf[k];
        if (b < 0x20 && b != '\t' && b != '\r' && b != '\n') return false;
    }

    // 找 <svg 或 <svg:（带命名空间前缀的写法），大小写不敏感
    for (ULONG k = i; k + 4 <= n; ++k) {
        if (buf[k] != '<') continue;
        ULONG p = k + 1;
        char c0 = (char)((buf[p] >= 'A' && buf[p] <= 'Z') ? buf[p] + 32 : buf[p]);
        char c1 = (p + 1 < n) ? (char)((buf[p + 1] >= 'A' && buf[p + 1] <= 'Z')
                                           ? buf[p + 1] + 32
                                           : buf[p + 1])
                              : 0;
        char c2 = (p + 2 < n) ? (char)((buf[p + 2] >= 'A' && buf[p + 2] <= 'Z')
                                           ? buf[p + 2] + 32
                                           : buf[p + 2])
                              : 0;
        if (c0 == 's' && c1 == 'v' && c2 == 'g') return true;
    }
    return false;
}

// 读整段流（顺带回绕到 0）。超出上限返回 E_FAIL。
HRESULT SlurpStream(IStream* stm, std::vector<char>* text) {
    STATSTG stat = {};
    ULONG cap = kMaxSvgBytes;
    if (SUCCEEDED(stm->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0) {
        if (stat.cbSize.QuadPart > (ULONGLONG)kMaxSvgBytes) return E_FAIL;
        cap = (ULONG)stat.cbSize.QuadPart;
    }
    LARGE_INTEGER zero = {};
    if (FAILED(stm->Seek(zero, STREAM_SEEK_SET, nullptr))) return E_FAIL;

    text->assign(cap + 1, '\0');
    ULONG got = 0;
    while (got < cap) {
        ULONG chunk = 0;
        const HRESULT hr = stm->Read(&(*text)[got], cap - got, &chunk);
        if (FAILED(hr)) return hr;
        if (chunk == 0) break;   // EOF
        got += chunk;
    }
    if (got == 0) return E_FAIL;
    (*text)[got] = '\0';
    text->resize(got + 1);
    return S_OK;
}

// 图形边界并集（无 width/height/viewBox 的 SVG 用内容尺寸兜底）
bool ContentBounds(const NSVGimage* img, float* x0, float* y0, float* x1,
                   float* y1) {
    bool any = false;
    *x0 = *y0 = *x1 = *y1 = 0.0f;
    for (const NSVGshape* s = img->shapes; s; s = s->next) {
        const float l = s->bounds[0], t = s->bounds[1], r = s->bounds[2],
                    b = s->bounds[3];
        if (!(r > l) || !(b > t)) continue;
        if (!any) {
            *x0 = l; *y0 = t; *x1 = r; *y1 = b;
            any = true;
        } else {
            if (l < *x0) *x0 = l;
            if (t < *y0) *y0 = t;
            if (r > *x1) *x1 = r;
            if (b > *y1) *y1 = b;
        }
    }
    return any;
}

}   // namespace

bool SvgLooksLikeStream(IStream* stm) {
    if (!stm) return false;
    LARGE_INTEGER zero = {};
    if (FAILED(stm->Seek(zero, STREAM_SEEK_SET, nullptr))) return false;
    BYTE buf[kSniffBytes];
    ULONG got = 0;
    const bool ok = ReadBytes(stm, buf, kSniffBytes, &got);
    stm->Seek(zero, STREAM_SEEK_SET, nullptr);   // 回绕，后续照常读
    if (!ok && got == 0) return false;
    return SniffSvg(buf, got);
}

HRESULT SvgRenderThumb(IStream* stm, UINT cx, HBITMAP* phbm,
                       WTS_ALPHATYPE* pat) {
    if (!stm || !phbm || !pat || cx == 0) return E_INVALIDARG;
    *phbm = nullptr;
    *pat = WTSAT_UNKNOWN;

    std::vector<char> text;
    const HRESULT hr = SlurpStream(stm, &text);
    if (FAILED(hr)) return hr;

    // nsvgParse 会就地改写输入串，所以传 text 的可写缓冲（活到 parse 之后）
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> img(
        nsvgParse(text.data(), "px", 96.0f), &nsvgDelete);
    if (!img) return E_FAIL;

    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float cw = img->width, ch = img->height;
    if (!std::isfinite(cw) || !std::isfinite(ch) || !(cw > 0) || !(ch > 0)) {
        if (!ContentBounds(img.get(), &x0, &y0, &x1, &y1)) {
            return E_FAIL;   // 空文档
        }
        cw = x1 - x0;
        ch = y1 - y0;
    }
    if (!std::isfinite(cw) || !std::isfinite(ch) || !(cw > 0) || !(ch > 0)) {
        return E_FAIL;
    }

    // 目标尺寸：等比缩进 cx 盒（矢量允许放大，最长边=cx）
    if (cx > kMaxRasterEdge) cx = kMaxRasterEdge;
    float scale = (float)cx / (cw > ch ? cw : ch);
    if (!std::isfinite(scale) || !(scale > 0.0f)) {
        return E_FAIL;
    }
    UINT dstW = (UINT)(cw * scale + 0.5f);
    UINT dstH = (UINT)(ch * scale + 0.5f);
    if (dstW == 0) dstW = 1;
    if (dstH == 0) dstH = 1;
    // 单边上限（cx 本身就可能被外部给成超大值）
    if (dstW > kMaxRasterEdge || dstH > kMaxRasterEdge) {
        const float cap = (float)kMaxRasterEdge /
                          (float)(dstW > dstH ? dstW : dstH);
        dstW = (UINT)(dstW * cap);
        dstH = (UINT)(dstH * cap);
        if (dstW == 0) dstW = 1;
        if (dstH == 0) dstH = 1;
        scale *= cap;
    }

    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> rast(
        nsvgCreateRasterizer(), &nsvgDeleteRasterizer);
    if (!rast) {
        return E_OUTOFMEMORY;
    }

    std::vector<BYTE> rgba((size_t)dstW * dstH * 4);
    // nanosvg 的输出原点即画布原点；画布尺寸与 viewBox 的换算已由解析器做掉
    nsvgRasterize(rast.get(), img.get(), 0.0f, 0.0f, scale, rgba.data(), (int)dstW,
                  (int)dstH, (int)dstW * 4);

    // 写进 CreateDIBSection 缓冲：RGBA 非预乘 → BGRA 预乘（Explorer 要 ARGB 预乘）
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = (LONG)dstW;
    bi.bmiHeader.biHeight = -(LONG)dstH;   // 自顶向下，行序与光栅输出一致
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* pvBits = nullptr;
    HBITMAP hbm =
        CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    if (hbm && pvBits) {
        const BYTE* src = rgba.data();
        BYTE* dst = (BYTE*)pvBits;
        const size_t px = (size_t)dstW * dstH;
        for (size_t i = 0; i < px; ++i, src += 4, dst += 4) {
            const BYTE a = src[3];
            if (a == 0) {
                dst[0] = dst[1] = dst[2] = dst[3] = 0;
            } else if (a == 255) {
                dst[0] = src[2];   // B
                dst[1] = src[1];   // G
                dst[2] = src[0];   // R
                dst[3] = 255;
            } else {
                dst[0] = (BYTE)((src[2] * a + 127) / 255);
                dst[1] = (BYTE)((src[1] * a + 127) / 255);
                dst[2] = (BYTE)((src[0] * a + 127) / 255);
                dst[3] = a;
            }
        }
        *pat = WTSAT_ARGB;
    }

    if (!hbm || !pvBits) {
        if (hbm) DeleteObject(hbm);
        return E_OUTOFMEMORY;
    }
    *phbm = hbm;
    return S_OK;
}
