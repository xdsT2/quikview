#pragma once

#include <QDate>
#include <QSize>
#include <QString>

// 图片总览「按日期 / 按图片大小」分组所需的文件事实。
// 两条口径都不需要整图解码：像素尺寸读文件头（QImageReader::size()），
// 拍摄日期读 EXIF 的 IFD0 / Exif SubIFD 时间标签。读不到就是「未知」，
// 由调用方放进置底的兜底组，不拿文件修改时间冒充拍摄日期。
struct ImageFacts
{
    QSize pixelSize;    // 无效尺寸 = 未知大小
    QDate shotDate;     // 无效日期 = 无拍摄日期

    bool hasSize() const { return pixelSize.width() > 0 && pixelSize.height() > 0; }
    bool hasDate() const { return shotDate.isValid(); }
    // 原始宽 × 高的总像素数（未知大小按 0，调用方只看 hasSize()）。
    qint64 pixelCount() const
    {
        return hasSize() ? qint64(pixelSize.width()) * qint64(pixelSize.height()) : 0;
    }
};

// 只读文件头部（最多 256 KB）取尺寸与拍摄日期，可安全在大批文件上逐个调用。
ImageFacts readImageFacts(const QString &path);
