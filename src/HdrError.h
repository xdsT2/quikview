// SPDX-License-Identifier: MIT
// HdrError.h — 解码内核统一错误分类（不抛异常穿过 API 边界）。
// 两类错误：Corrupt（文件损坏/截断/解压失败，可提示"文件损坏"）
//           Unsupported（格式不支持：tiled、多部件、DEEP、非 ZIP、非 scanline、非法位深等）
#pragma once

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hdrprobe {

// 以 UTF-8 路径打开文件：Windows 下先转宽字符走 _wfopen，兼容含中文/空格的目录；
// 其它平台直接 fopen。返回 nullptr 表示打开失败（调用方按 IoError 归类）。
inline std::FILE* openFileUtf8(const char* path, const char* mode)
{
#ifdef _WIN32
    if (!path) return nullptr;
    const int wlen = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (wlen <= 0) return nullptr;
    std::wstring wpath(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, &wpath[0], wlen);
    std::wstring wmode;
    for (const char* p = mode; *p; ++p) wmode.push_back(static_cast<wchar_t>(*p));
    return _wfopen(wpath.c_str(), wmode.c_str());
#else
    return std::fopen(path, mode);
#endif
}


enum class HdrStatus : int {
    Ok = 0,

    // ---- 损坏类（Corrupt）----
    CorruptMagic,          // 魔数不符
    CorruptTruncated,      // 文件截断 / 字节数不足
    CorruptInflateFailed,  // zlib/inflate 解压失败（含 adler32 校验失败）
    CorruptBadChecksum,    // 显式校验和失败（预留）
    CorruptBadRle,         // RGBE RLE 流非法
    CorruptHeader,         // 头部结构损坏（属性长度越界、窗口非法等）

    // ---- 格式不支持类（Unsupported）----
    UnsupportedTiled,        // tiled 平铺图
    UnsupportedMultiPart,    // 多部件（multi-part）
    UnsupportedDeep,         // DEEP 数据
    UnsupportedCompression,   // 非 ZIP/ZIPS 压缩（RLE/PIZ/PXR24/B44/DWA/HTJ2K 等）
    UnsupportedNonScanline,   // 非 scanline 存储
    UnsupportedBitDepth,      // 非法位深（UINT 或未知 pixelType）
    UnsupportedChannel,       // 缺少 R/G/B 或出现无法映射的通道
    UnsupportedSubsampled,    // 通道 x/y 采样率非 1
    UnsupportedFormat,        // Radiance 非 32-bit_rle_rgbe 等

    // ---- IO / 其他 ----
    IoError,
    OutOfMemory
};

enum class HdrCategory : int {
    Ok = 0,
    Corrupt = 1,     // 文件损坏
    Unsupported = 2, // 格式不支持
    Io = 3           // 读写/内存等运行环境问题
};

struct HdrError {
    HdrStatus code = HdrStatus::Ok;
    HdrCategory category = HdrCategory::Ok;
    std::string message;

    static HdrCategory CategoryOf(HdrStatus code);
    static const char* Key(HdrStatus code);   // 稳定文案键（供上层 i18n 映射）
    static const char* ZhText(HdrStatus code); // 中文可读文案
};

} // namespace hdrprobe
