#pragma once

#include <QMap>
#include <QString>

// 极简 EXIF 解析（MIT-clean，仅 Qt + 标准库）。
// 支持 JPEG(APP1) 与 TIFF 的 IFD0 常用标签 + GPS IFD，输出人类可读键值。
class ExifParser
{
public:
    struct Result {
        QMap<QString, QString> tags;
        bool hasGps = false;
        double lat = 0.0;
        double lng = 0.0;
    };

    static Result parse(const QString &filePath);

    // 物理分辨率（像素/英寸），供信息浮层「打印尺寸」换算。
    // 值取 IFD0 的 XResolution / YResolution（ResolutionUnit=3 时按 2.54 折算成
    // 英寸口径）；0 表示该文件没有分辨率记录（调用方按 96 dpi 假设处理）。
    struct Dpi {
        double x = 0.0;
        double y = 0.0;
    };
    // 只读文件头部（JPEG APP1 或裸 TIFF 的 IFD0），不整文件读入：
    // 信息浮层每换一张图都要算一次打印尺寸，不能按「解析整份 EXIF」的成本走。
    static Dpi dpi(const QString &filePath);
};
