// SPDX-License-Identifier: MIT
// RenderSdr.h — 0 EV 基线显示映射：2^EV → Reinhard → sRGB 编码。
// 与基线口径一致：曝光在映射到 SDR 之前施加。
#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace hdrprobe {

inline float srgbEncode(float c) {
    if (c <= 0.0031308f) return 12.92f * c;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline void renderSdr(const float* linear, int w, int h, float ev,
                      std::vector<unsigned char>& rgb8) {
    float gain = std::pow(2.0f, ev);
    rgb8.resize((size_t)w * h * 3);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        for (int k = 0; k < 3; ++k) {
            float v = linear[i * 3 + k] * gain;
            if (v < 0) v = 0;
            v = v / (1.0f + v);            // Reinhard
            v = srgbEncode(v);
            v = std::min(1.0f, std::max(0.0f, v));
            rgb8[i * 3 + k] = (unsigned char)std::lround(v * 255.0f);
        }
    }
}

} // namespace hdrprobe
