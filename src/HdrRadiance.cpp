// SPDX-License-Identifier: MIT
#include "HdrRadiance.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>
#include <algorithm>

namespace hdrprobe {

HdrRadiance::HdrRadiance() = default;

HdrRadiance::~HdrRadiance() {
    if (m_f) { std::fclose(m_f); m_f = nullptr; }
}

bool HdrRadiance::readU8(uint8_t& v, HdrError& err) {
    if (!m_f) { err = HdrError{HdrStatus::IoError, HdrCategory::Io, "文件未打开"}; return false; }
    size_t rd = std::fread(&v, 1, 1, m_f);
    if (rd != 1) {
        if (std::feof(m_f)) err = HdrError{HdrStatus::CorruptTruncated, HdrCategory::Corrupt, "读取像素时遇到 EOF"};
        else                err = HdrError{HdrStatus::IoError, HdrCategory::Io, "fread 失败"};
        return false;
    }
    return true;
}

bool HdrRadiance::readLine(std::string& line, HdrError& err) {
    line.clear();
    while (true) {
        uint8_t b;
        size_t rd = std::fread(&b, 1, 1, m_f);
        if (rd != 1) {
            if (line.empty() && std::feof(m_f)) { err = HdrError{}; return false; }
            err = HdrError{HdrStatus::CorruptTruncated, HdrCategory::Corrupt, "读取头部行时 EOF"};
            return false;
        }
        if (b == '\n') return true;
        if (b != '\r') line.push_back((char)b);
    }
}

HdrError HdrRadiance::open(const char* path) {
    m_f = openFileUtf8(path, "rb");
    if (!m_f) return HdrError{HdrStatus::IoError, HdrCategory::Io, std::string("无法打开 ") + path};

    HdrError err;
    std::string first;
    if (!readLine(first, err)) return err;
    // Radiance 变体首行有 #?RADIANCE 与 #?RGBE 两种写法，头部结构一致，都接受。
    if (first.rfind("#?RADIANCE", 0) != 0 && first.rfind("#?RGBE", 0) != 0) {
        return HdrError{HdrStatus::CorruptMagic, HdrCategory::Corrupt,
                        "首行不是 #?RADIANCE/#?RGBE：" + first};
    }

    std::string format;
    std::string resLine;
    while (true) {
        std::string line;
        if (!readLine(line, err)) return err;
        if (line.empty()) break; // 头部结束，下一行为分辨率行
        if (line.rfind("FORMAT=", 0) == 0) format = line.substr(7);
    }
    if (!readLine(resLine, err)) return err;

    if (format.empty()) format = "32-bit_rle_rgbe";
    if (format != "32-bit_rle_rgbe") {
        return HdrError{HdrStatus::UnsupportedFormat, HdrCategory::Unsupported,
                        "FORMAT=" + format};
    }

    // 分辨率行：[-|+]Y h [-|+]X w（轴与符号相邻，如 -Y / +X）
    std::istringstream iss(resLine);
    std::string t1, t3;
    int n1 = 0, n2 = 0;
    if (!(iss >> t1 >> n1 >> t3 >> n2)) {
        return HdrError{HdrStatus::CorruptHeader, HdrCategory::Corrupt,
                        "分辨率行无法解析：" + resLine};
    }
    char axis1 = t1.back(), sign1 = t1.front();
    char axis3 = t3.back(), sign3 = t3.front();
    if (axis1 == 'Y' && axis3 == 'X') {
        m_h = n1; m_w = n2;
        m_flipY = (sign1 == '+'); // +Y：文件自底向上存储
        m_flipX = (sign3 == '-'); // -X：文件自右向左存储
    } else if (axis1 == 'X' && axis3 == 'Y') {
        m_w = n1; m_h = n2;
        m_flipX = (sign1 == '-');
        m_flipY = (sign3 == '+');
    } else {
        return HdrError{HdrStatus::CorruptHeader, HdrCategory::Corrupt,
                        "分辨率行轴非法：" + resLine};
    }
    if (m_w <= 0 || m_h <= 0) {
        return HdrError{HdrStatus::CorruptHeader, HdrCategory::Corrupt, "尺寸非法"};
    }

    for (int k = 0; k < 4; ++k) m_plane[k].resize(m_w);
    m_rowBuf.resize(m_w * 3);
    return HdrError{};
}

bool HdrRadiance::decodeScanline(int /*fileRow*/, HdrError& err) {
    if (m_w < 8) {
        // 旧式扁平排列：4 字节 / 像素，RGBE 顺序。
        for (int x = 0; x < m_w; ++x) {
            for (int k = 0; k < 4; ++k) {
                uint8_t v;
                if (!readU8(v, err)) return false;
                m_plane[k][x] = v;
            }
        }
        return true;
    }

    uint8_t b0, b1;
    if (!readU8(b0, err) || !readU8(b1, err)) return false;
    if (b0 != 2 || b1 != 2) {
        err = HdrError{HdrStatus::CorruptBadRle, HdrCategory::Corrupt,
                       "RLE 扫描线标记非法：" + std::to_string(b0) + "," + std::to_string(b1)};
        return false;
    }
    uint8_t hi, lo;
    if (!readU8(hi, err) || !readU8(lo, err)) return false;
    int wCheck = (int)hi * 256 + (int)lo;
    if (wCheck != m_w) {
        err = HdrError{HdrStatus::CorruptBadRle, HdrCategory::Corrupt,
                       "RLE 宽度校验不符 " + std::to_string(wCheck) + "!=" + std::to_string(m_w)};
        return false;
    }

    for (int k = 0; k < 4; ++k) {
        int count = 0;
        while (count < m_w) {
            uint8_t code;
            if (!readU8(code, err)) return false;
            if (code > 128) {
                int n = code - 128;
                uint8_t val;
                if (!readU8(val, err)) return false;
                if (count + n > m_w) {
                    err = HdrError{HdrStatus::CorruptBadRle, HdrCategory::Corrupt, "RLE run 越界"};
                    return false;
                }
                std::fill(m_plane[k].begin() + count, m_plane[k].begin() + count + n, val);
                count += n;
            } else {
                int n = code;
                if (n == 0) continue;
                if (count + n > m_w) {
                    err = HdrError{HdrStatus::CorruptBadRle, HdrCategory::Corrupt, "RLE 字面量越界"};
                    return false;
                }
                for (int i = 0; i < n; ++i) {
                    if (!readU8(m_plane[k][count + i], err)) return false;
                }
                count += n;
            }
        }
    }
    return true;
}

bool HdrRadiance::nextRow(float* rgb, int& srcY, HdrError& err) {
    if (m_fileRow >= m_h) { err = HdrError{}; return false; }
    if (!decodeScanline(m_fileRow, err)) return false;

    for (int x = 0; x < m_w; ++x) {
        int xi = m_flipX ? (m_w - 1 - x) : x;
        uint8_t c0 = m_plane[0][xi];
        uint8_t c1 = m_plane[1][xi];
        uint8_t c2 = m_plane[2][xi];
        uint8_t e  = m_plane[3][xi];
        float r = 0, g = 0, b = 0;
        if (e != 0) {
            float mult = std::ldexpf(1.0f, (int)e - 136);
            r = (float)c0 * mult;
            g = (float)c1 * mult;
            b = (float)c2 * mult;
        }
        size_t o = (size_t)x * 3;
        rgb[o + 0] = r; rgb[o + 1] = g; rgb[o + 2] = b;
        if (std::isfinite(r) && r > m_nativeMax) m_nativeMax = r;
        if (std::isfinite(g) && g > m_nativeMax) m_nativeMax = g;
        if (std::isfinite(b) && b > m_nativeMax) m_nativeMax = b;
    }
    srcY = m_flipY ? (m_h - 1 - m_fileRow) : m_fileRow;
    ++m_fileRow;
    return true;
}

} // namespace hdrprobe
