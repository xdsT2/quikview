// SPDX-License-Identifier: MIT
// HdrRadiance.h — Radiance RGBE（32-bit_rle_rgbe）流式读取。
// 支持新式 4 通道 RLE 与旧式/扁平排列；朝向 `-Y h +X w` 与 `+Y h -X w` 均正确
// （输出统一为自上而下、自左而右的规范顺序，srcY 回写规范坐标）。
//
// RGBE → 线性 float 口径（与 stb/ffmpeg 一致）：
//   若 e == 0: r=g=b=0
//   否则:      r = c0 * 2^(e-136), g = c1 * 2^(e-136), b = c2 * 2^(e-136)
//             （等价于 (c/256) * 2^(e-128)）
#pragma once

#include "HdrError.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hdrprobe {

class HdrRadiance {
public:
    HdrRadiance();
    ~HdrRadiance();

    HdrRadiance(const HdrRadiance&) = delete;
    HdrRadiance& operator=(const HdrRadiance&) = delete;

    HdrError open(const char* path);

    int width()  const { return m_w; }
    int height() const { return m_h; }

    // 取下一条扫描线（规范自上而下顺序由 srcY 标明；文件物理顺序可能相反）。
    // rgb 长度 width()*3，R/G/B 线性 float。false + err.code==Ok 为正常 EOF。
    bool nextRow(float* rgb, int& srcY, HdrError& err);

    float nativeMax() const { return m_nativeMax; }

private:
    std::FILE* m_f = nullptr;
    int  m_w = 0, m_h = 0;
    bool m_flipY = false;   // +Y：文件首行为底行
    bool m_flipX = false;   // -X：文件首列为右列
    int  m_fileRow = 0;
    bool m_legacyFlat = false;

    std::vector<unsigned char> m_plane[4];
    std::vector<float> m_rowBuf;
    float m_nativeMax = 0.0f;

    bool readLine(std::string& line, HdrError& err);
    bool readU8(uint8_t& v, HdrError& err);
    bool decodeScanline(int fileRow, HdrError& err);
};

} // namespace hdrprobe
