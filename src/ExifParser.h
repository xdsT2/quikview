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
};
