// SPDX-License-Identifier: MIT
// Half.h — IEEE 754 half (float16) -> float32 无损转换。
#pragma once
#include <cstdint>
#include <cstring>

namespace hdrprobe {

inline float HalfToFloat(uint16_t h) {
    uint32_t sign = (uint32_t)(h >> 15) & 1u;
    uint32_t exp  = (uint32_t)(h >> 10) & 0x1Fu;
    uint32_t man  = (uint32_t)(h & 0x3FFu);
    uint32_t f;

    if (exp == 0) {
        // 亚正规数 / 零
        if (man == 0) {
            f = sign << 31;
        } else {
            // 归一化亚正规数：找最高位
            uint32_t e = 127 - 15;
            while ((man & 0x400u) == 0) { man <<= 1; --e; }
            man &= 0x3FFu;
            f = (sign << 31) | (e << 23) | (man << 13);
        }
    } else if (exp == 31) {
        // Inf / NaN
        f = (sign << 31) | 0x7F800000u | (man << 13);
    } else {
        // 正规数：指数偏置 127-15=112
        f = (sign << 31) | ((exp + 112u) << 23) | (man << 13);
    }
    float out;
    std::memcpy(&out, &f, 4);
    return out;
}

} // namespace hdrprobe
