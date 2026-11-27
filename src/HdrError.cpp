// SPDX-License-Identifier: MIT
#include "HdrError.h"

namespace hdrprobe {

HdrCategory HdrError::CategoryOf(HdrStatus code) {
    switch (code) {
        case HdrStatus::Ok: return HdrCategory::Ok;
        case HdrStatus::CorruptMagic:
        case HdrStatus::CorruptTruncated:
        case HdrStatus::CorruptInflateFailed:
        case HdrStatus::CorruptBadChecksum:
        case HdrStatus::CorruptBadRle:
        case HdrStatus::CorruptHeader:
            return HdrCategory::Corrupt;
        case HdrStatus::UnsupportedTiled:
        case HdrStatus::UnsupportedMultiPart:
        case HdrStatus::UnsupportedDeep:
        case HdrStatus::UnsupportedCompression:
        case HdrStatus::UnsupportedNonScanline:
        case HdrStatus::UnsupportedBitDepth:
        case HdrStatus::UnsupportedChannel:
        case HdrStatus::UnsupportedSubsampled:
        case HdrStatus::UnsupportedFormat:
            return HdrCategory::Unsupported;
        case HdrStatus::IoError:
        case HdrStatus::OutOfMemory:
            return HdrCategory::Io;
    }
    return HdrCategory::Io;
}

const char* HdrError::Key(HdrStatus code) {
    switch (code) {
        case HdrStatus::Ok:                    return "ok";
        case HdrStatus::CorruptMagic:          return "corrupt.magic";
        case HdrStatus::CorruptTruncated:      return "corrupt.truncated";
        case HdrStatus::CorruptInflateFailed:  return "corrupt.inflate_failed";
        case HdrStatus::CorruptBadChecksum:    return "corrupt.bad_checksum";
        case HdrStatus::CorruptBadRle:         return "corrupt.bad_rle";
        case HdrStatus::CorruptHeader:          return "corrupt.header";
        case HdrStatus::UnsupportedTiled:       return "unsupported.tiled";
        case HdrStatus::UnsupportedMultiPart:   return "unsupported.multi_part";
        case HdrStatus::UnsupportedDeep:       return "unsupported.deep";
        case HdrStatus::UnsupportedCompression:return "unsupported.compression";
        case HdrStatus::UnsupportedNonScanline: return "unsupported.non_scanline";
        case HdrStatus::UnsupportedBitDepth:    return "unsupported.bit_depth";
        case HdrStatus::UnsupportedChannel:    return "unsupported.channel";
        case HdrStatus::UnsupportedSubsampled:  return "unsupported.subsampled";
        case HdrStatus::UnsupportedFormat:     return "unsupported.format";
        case HdrStatus::IoError:               return "io.error";
        case HdrStatus::OutOfMemory:            return "io.out_of_memory";
    }
    return "io.unknown";
}

const char* HdrError::ZhText(HdrStatus code) {
    switch (code) {
        case HdrStatus::Ok:                    return "成功";
        case HdrStatus::CorruptMagic:          return "文件损坏：文件头魔数不符，不是有效的 HDR/EXR 文件";
        case HdrStatus::CorruptTruncated:      return "文件损坏：数据被截断，字节数不足";
        case HdrStatus::CorruptInflateFailed:  return "文件损坏：数据块解压（inflate）失败";
        case HdrStatus::CorruptBadChecksum:    return "文件损坏：校验和不一致";
        case HdrStatus::CorruptBadRle:         return "文件损坏：RLE 编码流非法";
        case HdrStatus::CorruptHeader:         return "文件损坏：文件头结构不合法";
        case HdrStatus::UnsupportedTiled:       return "格式不支持：平铺（tiled）图像暂未接入";
        case HdrStatus::UnsupportedMultiPart:   return "格式不支持：多部件（multi-part）图像暂未接入";
        case HdrStatus::UnsupportedDeep:        return "格式不支持：DEEP 深度图像暂未接入";
        case HdrStatus::UnsupportedCompression: return "格式不支持：当前压缩方式暂未接入（仅支持 ZIP/ZIPS deflate）";
        case HdrStatus::UnsupportedNonScanline: return "格式不支持：非 scanline 存储方式暂未接入";
        case HdrStatus::UnsupportedBitDepth:    return "格式不支持：非法位深（仅支持 FLOAT 与 HALF）";
        case HdrStatus::UnsupportedChannel:     return "格式不支持：通道组合不支持（需要 R/G/B，A 可选）";
        case HdrStatus::UnsupportedSubsampled:   return "格式不支持：通道存在非 1 的采样率";
        case HdrStatus::UnsupportedFormat:      return "格式不支持：非 32-bit_rle_rgbe Radiance 编码";
        case HdrStatus::IoError:               return "读取文件失败：IO 错误";
        case HdrStatus::OutOfMemory:           return "内存不足";
    }
    return "未知错误";
}

} // namespace hdrprobe
