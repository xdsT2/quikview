// SPDX-License-Identifier: MIT
#include "ExrStream.h"
#include "Half.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

// 复用仓库既有 miniz（third_party/psd_sdk/Psdminiz.h，内置 tinfl + adler32/crc32）。
// 实现体由独立编译单元 Psdminiz.cpp 提供；本头仅引入声明（MINIZ_HEADER_FILE_ONLY）。
// 包含口径与 src/ZipArchive.cpp 一致：third_party/psd_sdk 已在 target include 目录。
#include "PsdPch.h"
#include "Psdminiz.h"

namespace hdrprobe {

namespace {
constexpr uint32_t EXR_MAGIC = 0x01312F76u;
// version 字段标志位（OpenEXR File Layout）
constexpr uint32_t FLAG_TILED      = 0x200u;
constexpr uint32_t FLAG_DEEP      = 0x400u;
constexpr uint32_t FLAG_LONG_NAMES = 0x800u;

// compression 枚举
constexpr int CMP_NO_COMPRESSION = 0;
constexpr int CMP_RLE            = 1;
constexpr int CMP_ZIPS           = 2;
constexpr int CMP_ZIP            = 3;

// pixelType
constexpr int PT_UINT  = 0;
constexpr int PT_HALF  = 1;
constexpr int PT_FLOAT = 2;
} // namespace

ExrStream::ExrStream() = default;

ExrStream::~ExrStream() {
    if (m_f) { std::fclose(m_f); m_f = nullptr; }
}

HdrError ExrStream::fail(HdrStatus code, const std::string& detail) {
    HdrError e;
    e.code = code;
    e.category = HdrError::CategoryOf(code);
    e.message = std::string(HdrError::ZhText(code)) + "；" + detail;
    return e;
}

long long ExrStream::pos() const { return std::ftell(m_f); }

bool ExrStream::readBytes(void* p, size_t n, HdrError& err) {
    if (!m_f) { err = fail(HdrStatus::IoError, "文件未打开"); return false; }
    if (n == 0) return true;
    size_t rd = std::fread(p, 1, n, m_f);
    if (rd != n) {
        if (std::feof(m_f)) err = fail(HdrStatus::CorruptTruncated, "读取 " + std::to_string(n) + " 字节时遇到 EOF");
        else                err = fail(HdrStatus::IoError, "fread 失败");
        return false;
    }
    return true;
}

bool ExrStream::readU8(uint8_t& v, HdrError& err)  { return readBytes(&v, 1, err); }
bool ExrStream::readU16(uint16_t& v, HdrError& err){ return readBytes(&v, 2, err); }
bool ExrStream::readU32(uint32_t& v, HdrError& err){ return readBytes(&v, 4, err); }
bool ExrStream::readU64(int64_t& v, HdrError& err) { return readBytes(&v, 8, err); }

bool ExrStream::readCstring(std::string& out, HdrError& err, size_t maxLen) {
    out.clear();
    for (size_t i = 0; i < maxLen; ++i) {
        uint8_t b;
        if (!readU8(b, err)) return false;
        if (b == 0) return true;
        out.push_back((char)b);
    }
    err = fail(HdrStatus::CorruptHeader, "属性名超过长度上限");
    return false;
}

int ExrStream::roleOfName(const std::string& name) {
    if (name == "R") return 0;
    if (name == "G") return 1;
    if (name == "B") return 2;
    if (name == "A") return 3;
    return -1;
}

HdrError ExrStream::open(const char* path) {
    m_f = openFileUtf8(path, "rb");
    if (!m_f) return fail(HdrStatus::IoError, std::string("无法打开 ") + path);

    std::fseek(m_f, 0, SEEK_END);
    m_fileSize = std::ftell(m_f);
    std::fseek(m_f, 0, SEEK_SET);

    HdrError err;
    uint8_t magic[4];
    if (!readBytes(magic, 4, err)) { return err; }
    uint32_t magicVal = (uint32_t)magic[0] | ((uint32_t)magic[1] << 8) |
                        ((uint32_t)magic[2] << 16) | ((uint32_t)magic[3] << 24);
    if (magicVal != EXR_MAGIC) {
        return fail(HdrStatus::CorruptMagic, "magic=0x" + std::to_string(magicVal));
    }

    uint32_t versionField = 0;
    if (!readU32(versionField, err)) return err;
    int version = versionField & 0xFF;
    if (version != 2) return fail(HdrStatus::CorruptHeader, "未知 EXR 版本 " + std::to_string(version));

    if (versionField & FLAG_TILED)  return fail(HdrStatus::UnsupportedTiled, "version flag 标记为 tiled");
    if (versionField & FLAG_DEEP)  return fail(HdrStatus::UnsupportedDeep,  "version flag 标记为 deep");
    m_longNames = (versionField & FLAG_LONG_NAMES) != 0;

    if (!parseHeader(err)) return err;

    // 偏移表：scanline 单部件，chunkCount 个 int64。
    int chunkCount = (m_h + m_linesPerChunk - 1) / m_linesPerChunk;
    m_offsets.resize(chunkCount);
    for (int i = 0; i < chunkCount; ++i) {
        if (!readU64(m_offsets[i], err)) return err;
    }
    return HdrError{};
}

bool ExrStream::parseHeader(HdrError& err) {
    bool sawChannels = false, sawCompression = false;
    bool sawDataWindow = false, sawDisplayWindow = false;
    int xMin = 0, yMin = 0, xMax = -1, yMax = -1;
    int dxMin = 0, dyMin = 0, dxMax = -1, dyMax = -1;
    bool multipartAttr = false;

    while (true) {
        std::string attrName;
        if (!readCstring(attrName, err, m_longNames ? 100000 : 256)) return false;
        if (attrName.empty()) break; // 头部结束

        std::string typeName;
        if (!readCstring(typeName, err, 64)) return false;
        int32_t attrSize = 0;
        {
            uint32_t u;
            if (!readU32(u, err)) return false;
            attrSize = (int32_t)u;
        }
        if (attrSize < 0) { err = fail(HdrStatus::CorruptHeader, "属性 " + attrName + " 长度为负"); return false; }

        long long attrStart = pos();
        if (typeName == "chlist") {
            sawChannels = true;
            // 通道表：name\0, pixelType i32, pLinear u8 + 3 pad, xSampling i32, ySampling i32；空名结束。
            while (true) {
                std::string chName;
                if (!readCstring(chName, err, m_longNames ? 100000 : 256)) return false;
                if (chName.empty()) break;
                Channel ch;
                ch.name = chName;
                uint32_t pt;
                if (!readU32(pt, err)) return false;
                ch.pixelType = (int)pt;
                uint8_t pLinear, pad1, pad2, pad3;
                if (!readU8(pLinear, err) || !readU8(pad1, err) || !readU8(pad2, err) || !readU8(pad3, err)) return false;
                uint32_t xs, ys;
                if (!readU32(xs, err) || !readU32(ys, err)) return false;
                ch.xSampling = (int)xs; ch.ySampling = (int)ys;

                if (ch.pixelType != PT_HALF && ch.pixelType != PT_FLOAT) {
                    err = fail(HdrStatus::UnsupportedBitDepth, "通道 " + chName + " pixelType=" + std::to_string(ch.pixelType));
                    return false;
                }
                if (ch.xSampling != 1 || ch.ySampling != 1) {
                    err = fail(HdrStatus::UnsupportedSubsampled, "通道 " + chName + " 采样率非 1");
                    return false;
                }
                ch.bps = (ch.pixelType == PT_HALF) ? 2 : 4;
                ch.role = roleOfName(ch.name);
                ch.fileIndex = (int)m_channels.size();
                m_channels.push_back(ch);
            }
        } else if (typeName == "compression") {
            sawCompression = true;
            uint8_t c;
            if (!readU8(c, err)) return false;
            m_compression = (int)c;
            if (m_compression != CMP_ZIP && m_compression != CMP_ZIPS) {
                err = fail(HdrStatus::UnsupportedCompression, "compression=" + std::to_string(m_compression));
                return false;
            }
            m_linesPerChunk = (m_compression == CMP_ZIP) ? 16 : 1;
        } else if (typeName == "box2i") {
            int32_t a, b, c, d;
            uint32_t u;
            if (!readU32(u, err)) return false; a = (int32_t)u;
            if (!readU32(u, err)) return false; b = (int32_t)u;
            if (!readU32(u, err)) return false; c = (int32_t)u;
            if (!readU32(u, err)) return false; d = (int32_t)u;
            if (attrName == "dataWindow")      { sawDataWindow = true;  dxMin=a; dyMin=b; dxMax=c; dyMax=d; }
            else if (attrName == "displayWindow") { sawDisplayWindow = true; xMin=a; yMin=b; xMax=c; yMax=d; }
        } else if (typeName == "lineOrder") {
            uint8_t lo;
            if (!readU8(lo, err)) return false;
            m_lineOrder = (int)lo;
            if (m_lineOrder == 2) { err = fail(HdrStatus::UnsupportedNonScanline, "lineOrder=RANDOM_Y"); return false; }
        } else if (typeName == "string") {
            // string 属性按 attrSize 字节存储，不保证值内含结尾 NUL；只在需要判型时读原始字节。
            if (attrName == "type") {
                std::string s(attrSize, '\0');
                if (attrSize > 0 && !readBytes(&s[0], attrSize, err)) return false;
                if (s == "deepscanlineimage" || s == "deeptileimage") {
                    err = fail(HdrStatus::UnsupportedDeep, "type=" + s); return false;
                }
            }
            // 其余 string 值不读取，下方统一按 size 跳过。
        } else if (attrName == "chunkCount") {
            multipartAttr = true; // 单部件文件不写 chunkCount 属性
        }

        // 跳到属性值末尾（防止未知类型解析错位）
        long long consumed = pos() - attrStart;
        if (consumed > attrSize) { err = fail(HdrStatus::CorruptHeader, "属性 " + attrName + " 越界"); return false; }
        if (consumed < attrSize) std::fseek(m_f, attrStart + attrSize, SEEK_SET);
    }

    if (multipartAttr) { err = fail(HdrStatus::UnsupportedMultiPart, "检测到 multi-part 头部属性"); return false; }
    if (!sawChannels)    { err = fail(HdrStatus::CorruptHeader, "缺少 channels 属性"); return false; }
    if (!sawCompression) { err = fail(HdrStatus::CorruptHeader, "缺少 compression 属性"); return false; }
    if (!sawDataWindow || !sawDisplayWindow) { err = fail(HdrStatus::CorruptHeader, "缺少窗口属性"); return false; }

    m_w = dxMax - dxMin + 1;
    m_h = dyMax - dyMin + 1;
    m_xMin = dxMin; m_yMin = dyMin;
    if (m_w <= 0 || m_h <= 0) { err = fail(HdrStatus::CorruptHeader, "窗口尺寸非法"); return false; }

    for (auto& ch : m_channels) {
        if (ch.role == 0) m_roleR = ch.fileIndex;
        else if (ch.role == 1) m_roleG = ch.fileIndex;
        else if (ch.role == 2) m_roleB = ch.fileIndex;
        else if (ch.role == 3) m_roleA = ch.fileIndex;
    }
    if (m_roleR < 0 || m_roleG < 0 || m_roleB < 0) {
        err = fail(HdrStatus::UnsupportedChannel, "缺少 R/G/B 必需通道"); return false;
    }
    // 其余未识别通道名（Z、深度等）在此明确拒绝，避免静默丢通道。
    for (auto& ch : m_channels) {
        if (ch.role < 0) { err = fail(HdrStatus::UnsupportedChannel, "未识别通道 " + ch.name); return false; }
    }
    return true;
}

void ExrStream::zipReconstruct(unsigned char* buf, size_t n) {
    // 逆预测器：与 OpenEXR ImfZip.cpp reconstruct_scalar 对齐。
    if (n == 0) return;
    for (size_t i = 1; i < n; ++i) {
        int d = int(buf[i - 1]) + int(buf[i]) - 128;
        buf[i] = (unsigned char)(d & 0xFF);
    }
}

void ExrStream::zipInterleave(const unsigned char* src, size_t n, unsigned char* dst) {
    // 逆重排：前半偶数下标、后半奇数下标 → 恢复原始字节流。
    const unsigned char* t1 = src;
    const unsigned char* t2 = src + (n + 1) / 2;
    size_t s = 0;
    while (s < n) {
        dst[s++] = *t1++;
        if (s < n) dst[s++] = *t2++;
    }
}

bool ExrStream::decodeChunk(int chunkIndex, HdrError& err) {
    if (chunkIndex < 0 || chunkIndex >= (int)m_offsets.size()) {
        err = fail(HdrStatus::CorruptTruncated, "chunk 索引越界");
        return false;
    }
    int64_t offset = m_offsets[chunkIndex];
    int64_t nextOffset = (chunkIndex + 1 < (int)m_offsets.size())
                             ? m_offsets[chunkIndex + 1] : m_fileSize;
    if (offset < 0 || nextOffset <= offset || offset >= m_fileSize) {
        err = fail(HdrStatus::CorruptHeader, "chunk 偏移表非法");
        return false;
    }

    std::fseek(m_f, (long)offset, SEEK_SET);
    int32_t yCoord = 0;
    {
        uint32_t u;
        if (!readU32(u, err)) return false;
        yCoord = (int32_t)u;
    }
    // chunk 头兼容两种写法：
    //  A) 标准单部件 scanline：yCoord 后直接跟 zlib 流。
    //  B) 部分写入器：yCoord 后再写一个显式压缩长度 int32，然后才是 zlib 流。
    // 判别不能靠 zlib 首字节 0x78（压缩流首字节可能恰好等于 0x78），而是先读出候选
    // 长度字段，用「显式长度 == 偏移表隐含长度(减 8)」且流起点为合法 zlib 头来确认 B；
    // 不满足再回退 A。
    long long implicitNoField = nextOffset - offset - 4;
    long long implicitWithField = nextOffset - offset - 8;
    uint32_t lenField = 0;
    if (!readU32(lenField, err)) return false;

    bool useField = false;
    int64_t compLen = 0;
    long long dataStart = 0;
    if ((long long)lenField > 0 && (long long)lenField == implicitWithField &&
        implicitWithField >= 0) {
        // 候选 B：流起点 offset+8，校验 zlib 头首字节 0x78
        uint8_t hdr = 0;
        long long save = pos();
        if (readU8(hdr, err) && hdr == 0x78) {
            useField = true;
            compLen = (int64_t)lenField;
            dataStart = offset + 8;
        }
        std::fseek(m_f, save, SEEK_SET);
    }
    if (!useField) {
        // 回退 A：yCoord 后直接是 zlib 流，回退刚读的 4 字节长度候选。
        std::fseek(m_f, offset + 4, SEEK_SET);
        compLen = implicitNoField;
        dataStart = offset + 4;
    }
    if (compLen <= 0 || compLen > m_fileSize) {
        err = fail(HdrStatus::CorruptTruncated, "chunk 压缩长度非法");
        return false;
    }

    std::vector<unsigned char> comp((size_t)compLen);
    std::fseek(m_f, (long)dataStart, SEEK_SET);
    if (!readBytes(comp.data(), (size_t)compLen, err)) return false;

    int linesInChunk = std::min(m_linesPerChunk, m_h - chunkIndex * m_linesPerChunk);
    if (linesInChunk <= 0) { err = fail(HdrStatus::CorruptHeader, "chunk 行数非法"); return false; }

    size_t channelBytesPerPixel = 0;
    for (auto& ch : m_channels) channelBytesPerPixel += (size_t)ch.bps;
    size_t rawSize = (size_t)linesInChunk * (size_t)m_w * channelBytesPerPixel;

    m_rawBytes.resize(rawSize);
    m_tmp.resize(rawSize);

    int flags = TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF;
    size_t out = tinfl_decompress_mem_to_mem(m_rawBytes.data(), rawSize,
                                              comp.data(), comp.size(), flags);
    if (out == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || out != rawSize) {
        err = fail(HdrStatus::CorruptInflateFailed,
                   "chunk " + std::to_string(chunkIndex) + " inflate 失败 (out=" +
                   std::to_string(out) + "/" + std::to_string(rawSize) + ")");
        return false;
    }

    zipReconstruct(m_rawBytes.data(), rawSize);
    zipInterleave(m_rawBytes.data(), rawSize, m_tmp.data());

    // 解码为 float RGB 行。解压后的 chunk 布局（与 tinyexr/OpenEXR 一致）：
    // 每条扫描线内部按通道平面排列 —— 行 l：通道0 整行、通道1 整行……。
    m_chunkRows.resize((size_t)linesInChunk * m_w * 3);
    int nCh = (int)m_channels.size();

    for (int line = 0; line < linesInChunk; ++line) {
        // 先按通道平面取出本线各通道样本
        std::vector<float> lineRgb((size_t)m_w * nCh, 0.f);
        for (int c = 0; c < nCh; ++c) {
            const Channel& ch = m_channels[c];
            const unsigned char* plane = m_tmp.data() +
                ((size_t)line * nCh + c) * m_w * ch.bps;
            for (int x = 0; x < m_w; ++x) {
                const unsigned char* s = plane + (size_t)x * ch.bps;
                float v = 0.f;
                if (ch.pixelType == PT_FLOAT) {
                    uint32_t bits = (uint32_t)s[0] | ((uint32_t)s[1] << 8) |
                                    ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
                    std::memcpy(&v, &bits, 4);
                } else {
                    uint16_t bits = (uint16_t)((uint32_t)s[0] | ((uint32_t)s[1] << 8));
                    v = HalfToFloat(bits);
                }
                lineRgb[(size_t)x * nCh + c] = v;
            }
        }
        for (int x = 0; x < m_w; ++x) {
            float rgb[3] = {0.f, 0.f, 0.f};
            for (int c = 0; c < nCh; ++c) {
                const Channel& ch = m_channels[c];
                float v = lineRgb[(size_t)x * nCh + c];
                if (ch.role == 0) rgb[0] = v;
                else if (ch.role == 1) rgb[1] = v;
                else if (ch.role == 2) rgb[2] = v;
                // A(role 3) 不参与显示，丢弃
            }
            size_t o = ((size_t)line * m_w + x) * 3;
            m_chunkRows[o + 0] = rgb[0];
            m_chunkRows[o + 1] = rgb[1];
            m_chunkRows[o + 2] = rgb[2];
            for (int k = 0; k < 3; ++k) {
                if (std::isfinite(rgb[k]) && rgb[k] > m_nativeMax) m_nativeMax = rgb[k];
            }
        }
    }

    m_rowsInCurrentChunk = linesInChunk;
    m_rowInChunk = 0;
    m_currentChunkY = yCoord;
    return true;
}

bool ExrStream::nextRow(float* rgb, int& srcY, HdrError& err) {
    if (m_rowInChunk >= m_rowsInCurrentChunk) {
        if (m_nextChunk >= (int)m_offsets.size()) {
            err = HdrError{}; // 正常 EOF：code==Ok，category==Ok
            return false;
        }
        if (!decodeChunk(m_nextChunk, err)) return false;
        ++m_nextChunk;
    }
    const float* row = m_chunkRows.data() + (size_t)m_rowInChunk * m_w * 3;
    std::memcpy(rgb, row, (size_t)m_w * 3 * sizeof(float));
    srcY = m_currentChunkY - m_yMin + m_rowInChunk;
    ++m_rowInChunk;
    return true;
}

} // namespace hdrprobe
