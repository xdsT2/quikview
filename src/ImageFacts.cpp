#include "ImageFacts.h"

#include <QByteArray>
#include <QFile>
#include <QImageReader>
#include <QtEndian>
#include <QVector>

namespace {

// 头部读取上限：JPEG 的 APP1（EXIF）一般 <64 KB，PNG 的 eXIf、WebP 的 EXIF
// 块也都落在头部；不为了取一个日期把几十 MB 的原图读进内存。
constexpr int kHeaderBytes = 256 * 1024;
constexpr quint16 kTagDateTime = 0x0132;         // IFD0 DateTime（"拍摄时间"）
constexpr quint16 kTagExifIfd = 0x8769;          // 指向 Exif SubIFD
constexpr quint16 kTagDateTimeOriginal = 0x9003; // 原始拍摄时间（优先）
constexpr quint16 kTagDateTimeDigitized = 0x9004;
constexpr quint16 kTiffMagic = 42;

quint16 rd16(const QByteArray &d, int off, bool le)
{
    if (off < 0 || off + 2 > d.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(d.constData() + off);
    return le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
}

quint32 rd32(const QByteArray &d, int off, bool le)
{
    if (off < 0 || off + 4 > d.size()) {
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(d.constData() + off);
    return le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
}

// EXIF 日期是 "YYYY:MM:DD HH:MM:SS"；少部分写入方用 ISO 的 "YYYY-MM-DD"。
// 只取日历日、不取时刻（分组按日历日），并挡掉 0000:00:00 这类占位值。
QDate parseExifDate(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.size() < 10) {
        return QDate();
    }
    QDate date = QDate::fromString(text.left(10), QStringLiteral("yyyy:MM:dd"));
    if (!date.isValid()) {
        date = QDate::fromString(text.left(10), QStringLiteral("yyyy-MM-dd"));
    }
    if (!date.isValid() || date.year() < 1900 || date.year() > 2100) {
        return QDate();
    }
    return date;
}

// ASCII 标签取值：count<=4 时字符串内联在值字段里，否则值字段是偏移。
QString asciiValue(const QByteArray &d, int base, bool le, int entryOff)
{
    const quint32 count = rd32(d, entryOff + 4, le);
    const quint32 value = rd32(d, entryOff + 8, le);
    const int off = (count <= 4) ? entryOff + 8 : base + static_cast<int>(value);
    if (count == 0 || off < 0 || off >= d.size()) {
        return QString();
    }
    const int n = qMin(static_cast<int>(count), d.size() - off);
    QByteArray s = d.mid(off, n);
    const int nul = s.indexOf('\0');
    if (nul >= 0) {
        s.truncate(nul);
    }
    return QString::fromLatin1(s).trimmed();
}

quint32 numericValue(const QByteArray &d, bool le, int entryOff, quint16 type)
{
    if (type == 3) {   // SHORT
        return rd16(d, entryOff + 8, le);
    }
    return rd32(d, entryOff + 8, le);   // LONG
}

// 扫一个 IFD：拿到 DateTime / DateTimeOriginal（只取第一个有效值）。
void scanIfd(const QByteArray &d, int base, bool le, int ifdAbs, quint32 *exifIfd,
             QDate *dateTime, QDate *dateOriginal)
{
    if (ifdAbs < 0 || ifdAbs + 2 > d.size()) {
        return;
    }
    const quint16 count = rd16(d, ifdAbs, le);
    for (int i = 0; i < count; ++i) {
        const int entry = ifdAbs + 2 + i * 12;
        if (entry + 12 > d.size()) {
            break;
        }
        const quint16 tag = rd16(d, entry, le);
        const quint16 type = rd16(d, entry + 2, le);
        if (type == 2) {   // ASCII
            if (tag == kTagDateTime && !dateTime->isValid()) {
                *dateTime = parseExifDate(asciiValue(d, base, le, entry));
            } else if ((tag == kTagDateTimeOriginal || tag == kTagDateTimeDigitized)
                       && !dateOriginal->isValid()) {
                *dateOriginal = parseExifDate(asciiValue(d, base, le, entry));
            }
        } else if ((type == 3 || type == 4) && tag == kTagExifIfd && *exifIfd == 0) {
            *exifIfd = numericValue(d, le, entry, type);
        }
    }
}

// 在指定的 TIFF 起点上取日期；起点不是合法 TIFF 头就返回无效日期。
// EXIF 里同时有两种日期时，原始拍摄时间（0x9003）优先于 IFD0 的拍摄时间。
QDate readTiffDate(const QByteArray &d, int base)
{
    if (base < 0 || base + 8 > d.size()) {
        return QDate();
    }
    bool le = true;
    if (d.mid(base, 2) == QByteArray("II", 2)) {
        le = true;
    } else if (d.mid(base, 2) == QByteArray("MM", 2)) {
        le = false;
    } else {
        return QDate();
    }
    if (rd16(d, base + 2, le) != kTiffMagic) {
        return QDate();
    }

    const quint32 ifd0Rel = rd32(d, base + 4, le);
    quint32 exifIfd = 0;
    QDate dateTime;
    QDate dateOriginal;
    scanIfd(d, base, le, base + static_cast<int>(ifd0Rel), &exifIfd, &dateTime, &dateOriginal);
    if (exifIfd != 0) {
        scanIfd(d, base, le, base + static_cast<int>(exifIfd), &exifIfd, &dateTime, &dateOriginal);
    }
    return dateOriginal.isValid() ? dateOriginal : dateTime;
}

// 容器标记 → TIFF 起点：JPEG APP1 的 "Exif\0\0"、PNG 的 eXIf、WebP 的 EXIF，
// 以及裸 TIFF/CR2 这种文件本身就是 TIFF 的情况。
QDate exifShotDate(const QByteArray &head)
{
    QVector<int> bases;
    if (head.size() >= 4) {
        const QByteArray magic = head.left(2);
        if (magic == QByteArray("II", 2) || magic == QByteArray("MM", 2)) {
            bases.append(0);
        }
    }
    struct Marker {
        const char *text;
        int len;    // "Exif\0\0" 内嵌 NUL，长度只能写死，不能靠 strlen
        int skip;
    };
    static const Marker markers[] = {
        { "Exif\0\0", 6, 6 },   // JPEG APP1
        { "eXIf", 4, 4 },       // PNG eXIf 块：4 字节长度之后才是 TIFF
        { "EXIF", 4, 4 },       // WebP EXIF 块：同上
    };
    for (const Marker &m : markers) {
        int from = 0;
        for (int hit = 0; hit < 3; ++hit) {
            const int at = head.indexOf(QByteArray(m.text, m.len), from);
            if (at < 0) {
                break;
            }
            bases.append(at + m.skip);
            from = at + 1;
        }
    }
    for (int base : bases) {
        const QDate date = readTiffDate(head, base);
        if (date.isValid()) {
            return date;
        }
    }
    return QDate();
}

} // namespace

ImageFacts readImageFacts(const QString &path)
{
    ImageFacts facts;

    // 尺寸：QImageReader 只读文件头，不解码像素；不支持的格式（PSD/HEIF/包内
    // 虚拟路径等）拿到无效尺寸，归入「未知大小」。
    QImageReader reader(path);
    reader.setDecideFormatFromContent(true);
    facts.pixelSize = reader.size();
    if (!facts.hasSize()) {
        facts.pixelSize = QSize();
    }

    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        facts.shotDate = exifShotDate(file.read(kHeaderBytes));
    }
    return facts;
}
