// SPDX-License-Identifier: MIT
// BoxDownsampler.h — 流式盒式（box）降采样。
// 一次只驻留目标宽度的横向/纵向累加器 + 当前源行；不缓存整帧源图。
// 源行按规范自上而下推送（srcY 为规范坐标），目标行在源行组收齐后成组吐出。
#pragma once

#include "HdrError.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace hdrprobe {

class BoxDownsampler {
public:
    BoxDownsampler(int srcW, int srcH, int dstW, int dstH)
        : m_sw(srcW), m_sh(srcH), m_dw(dstW), m_dh(dstH) {
        m_colSum.assign((size_t)dstW * 3, 0.0f);
        m_rowSum.assign((size_t)dstW * 3, 0.0f);
        m_colCnt.assign(dstW, 0);
        m_rowCnt.assign(dstW, 0);
        m_dst.assign((size_t)dstW * dstH * 3, 0.0f);
        m_filled.assign(dstH, 0);
    }

    int dstWidth()  const { return m_dw; }
    int dstHeight() const { return m_dh; }

    // 只读访问目标帧某行（线性 float，dstW*3）。供上层逐行做曝光/映射/编码，
    // 避免再拷一份整帧线性缓冲；不改变任何像素口径。
    const float* rowData(int y) const { return m_dst.data() + (size_t)y * m_dw * 3; }
    const float* frameData() const { return m_dst.data(); }

    // 推送一条源扫描线，rgb 长度 srcW*3（R,G,B 线性 float）。
    void pushRow(int srcY, const float* rgb) {
        int dy = std::min(m_dh - 1, std::max(0, srcY * m_dh / m_sh));
        if (m_curDy < 0) m_curDy = dy;

        if (dy != m_curDy) {
            storeGroup(m_curDy);
            resetColumnAccum();
            m_curDy = dy;
        }

        // 横向盒式累加当前源行
        std::fill(m_rowSum.begin(), m_rowSum.end(), 0.0f);
        std::fill(m_rowCnt.begin(), m_rowCnt.end(), 0);
        for (int x = 0; x < m_sw; ++x) {
            int dx = std::min(m_dw - 1, x * m_dw / m_sw);
            const float* p = rgb + (size_t)x * 3;
            m_rowSum[dx * 3 + 0] += p[0];
            m_rowSum[dx * 3 + 1] += p[1];
            m_rowSum[dx * 3 + 2] += p[2];
            ++m_rowCnt[dx];
        }
        for (int dx = 0; dx < m_dw; ++dx) {
            if (m_rowCnt[dx] == 0) continue;
            float inv = 1.0f / (float)m_rowCnt[dx];
            m_colSum[dx * 3 + 0] += m_rowSum[dx * 3 + 0] * inv;
            m_colSum[dx * 3 + 1] += m_rowSum[dx * 3 + 1] * inv;
            m_colSum[dx * 3 + 2] += m_rowSum[dx * 3 + 2] * inv;
        }
        ++m_colGroupRows;
    }

    // 冲刷残留组到内部累加帧（不外发缓冲）；完成后用 frameData()/rowData() 读取。
    void finish() { if (m_curDy >= 0) storeGroup(m_curDy); }

    // 源图全部推送完毕后调用：冲刷残留组，写出全部目标行到 dst（dstH*dstW*3）。
    void flush(float* dst) {
        if (m_curDy >= 0) storeGroup(m_curDy);
        for (int y = 0; y < m_dh; ++y) {
            if (!m_filled[y]) continue;
            for (int x = 0; x < m_dw; ++x) {
                size_t o = ((size_t)y * m_dw + x) * 3;
                dst[o + 0] = m_dst[o + 0];
                dst[o + 1] = m_dst[o + 1];
                dst[o + 2] = m_dst[o + 2];
            }
        }
    }

private:
    int m_sw, m_sh, m_dw, m_dh;
    std::vector<float> m_colSum;
    std::vector<float> m_rowSum;
    std::vector<int>   m_colCnt;
    std::vector<int>   m_rowCnt;
    std::vector<float> m_dst;
    std::vector<char>  m_filled;
    int m_curDy = -1;
    int m_colGroupRows = 0;

    void resetColumnAccum() {
        std::fill(m_colSum.begin(), m_colSum.end(), 0.0f);
        std::fill(m_colCnt.begin(), m_colCnt.end(), 0);
        m_colGroupRows = 0;
    }

    void storeGroup(int dy) {
        if (m_colGroupRows <= 0) return;
        float inv = 1.0f / (float)m_colGroupRows;
        for (int dx = 0; dx < m_dw; ++dx) {
            size_t o = ((size_t)dy * m_dw + dx) * 3;
            m_dst[o + 0] = m_colSum[dx * 3 + 0] * inv;
            m_dst[o + 1] = m_colSum[dx * 3 + 1] * inv;
            m_dst[o + 2] = m_colSum[dx * 3 + 2] * inv;
        }
        m_filled[dy] = 1;
    }
};

} // namespace hdrprobe
