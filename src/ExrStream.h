// SPDX-License-Identifier: MIT
// ExrStream.h — 自研 OpenEXR 流式读取（单部件 / scanline / ZIP(3) 与 ZIPS(2) /
// FLOAT 与 HALF / RGBA，A 可缺省且不参与显示）。
//
// 内存口径：一次只驻留当前 chunk（最多 16 条扫描线）的解压缓冲 + 当前 chunk 已解码
// 的 float 行；不整帧常驻。消费方按行取数，配合 BoxDownsampler 做有界流式降采样。
//
// 错误约定：所有失败通过 HdrError 返回，不抛异常穿过本头文件声明的接口。
#pragma once

#include "HdrError.h"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace hdrprobe {

class ExrStream {
public:
    ExrStream();
    ~ExrStream();

    ExrStream(const ExrStream&) = delete;
    ExrStream& operator=(const ExrStream&) = delete;

    // 打开文件并解析头部与 chunk 偏移表；不解码像素。
    HdrError open(const char* path);

    int  width()  const { return m_w; }
    int  height() const { return m_h; }
    bool hasAlpha() const { return m_roleA >= 0; }

    // 取下一条扫描线（按文件内 chunk 顺序；srcY 回写该线在 dataWindow 中的 Y 坐标）。
    // rgb 长度必须为 width()*3，顺序 R,G,B 线性 float。返回 false 时：
    //   err.code==CorruptTruncated 且 srcY 已到末尾 → 正常 EOF；其余为错误。
    bool nextRow(float* rgb, int& srcY, HdrError& err);

    // 解码过程中累计的原生像素最大通道值（仅 R/G/B）。
    float nativeMax() const { return m_nativeMax; }

    // 头部元信息（供信息面板/总览分组读取真实位深与通道组合，不解码像素）。
    // pixelType：1=HALF，2=FLOAT；compression：2=ZIPS，3=ZIP。
    int pixelType() const { return m_channels.empty() ? 2 : m_channels[0].pixelType; }
    int compression() const { return m_compression; }
    int channelCount() const { return (int)m_channels.size(); }

private:
    struct Channel {
        std::string name;
        int  pixelType = 2;   // 1=HALF, 2=FLOAT
        int  bps = 4;         // 每样本字节数
        int  role = -1;       // 0=R 1=G 2=B 3=A -1=未识别
        int  fileIndex = -1;  // 在文件通道顺序中的下标
        int  xSampling = 1;
        int  ySampling = 1;
    };

    std::FILE* m_f = nullptr;
    long long  m_fileSize = 0;
    int  m_w = 0, m_h = 0;
    int  m_xMin = 0, m_yMin = 0;
    int  m_compression = -1;
    int  m_lineOrder = 0;       // 0=INCREASING_Y 1=DECREASING_Y 2=RANDOM_Y
    bool m_longNames = false;
    std::vector<Channel> m_channels;
    int  m_roleR = -1, m_roleG = -1, m_roleB = -1, m_roleA = -1;
    int  m_linesPerChunk = 16;
    std::vector<int64_t> m_offsets;

    int  m_nextChunk = 0;
    int  m_rowsInCurrentChunk = 0;
    int  m_rowInChunk = 0;
    int  m_currentChunkY = 0;

    std::vector<unsigned char> m_rawBytes;   // inflate 输出（chunk 原始字节）
    std::vector<unsigned char> m_tmp;       // reconstruct 后、interleave 前
    std::vector<float> m_chunkRows;          // 当前 chunk 解码后的 RGB 行
    float m_nativeMax = 0.0f;

    // ---- 内部解析辅助 ----
    bool readU8(uint8_t& v, HdrError& err);
    bool readU16(uint16_t& v, HdrError& err);
    bool readU32(uint32_t& v, HdrError& err);
    bool readU64(int64_t& v, HdrError& err);
    bool readBytes(void* p, size_t n, HdrError& err);
    bool readCstring(std::string& out, HdrError& err, size_t maxLen = 256);
    long long pos() const;

    HdrError fail(HdrStatus code, const std::string& detail);

    bool parseHeader(HdrError& err);
    bool decodeChunk(int chunkIndex, HdrError& err);
    static int  roleOfName(const std::string& name);
    static void zipReconstruct(unsigned char* buf, size_t n);
    static void zipInterleave(const unsigned char* src, size_t n, unsigned char* dst);
};

} // namespace hdrprobe
