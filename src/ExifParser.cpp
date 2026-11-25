#include "ExifParser.h"

#include <QCoreApplication>
#include <QFile>
#include <QtEndian>

namespace {

class Reader
{
public:
    Reader(const QByteArray &d, int tiffBase, bool little)
        : data(d), base(tiffBase), le(little) {}

    bool inBounds(quint64 off, quint64 len) const
    {
        if (data.size() < 0) return false;
        const quint64 total = quint64(data.size());
        return off <= total && len <= total - off;
    }

    bool readU16(quint64 off, quint16 &out) const
    {
        if (!inBounds(off, 2)) return false;
        const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + off;
        out = le ? qFromLittleEndian<quint16>(p) : qFromBigEndian<quint16>(p);
        return true;
    }

    bool readU32(quint64 off, quint32 &out) const
    {
        if (!inBounds(off, 4)) return false;
        const uchar *p = reinterpret_cast<const uchar *>(data.constData()) + off;
        out = le ? qFromLittleEndian<quint32>(p) : qFromBigEndian<quint32>(p);
        return true;
    }

    QString asciiValue(quint64 entryOff, quint32 valueField, quint32 count) const
    {
        if (count == 0) return QString();
        quint64 off = 0;
        quint64 len = 0;
        if (count <= 4) {
            off = entryOff + 8;
            len = count;
        } else {
            off = quint64(base) + quint64(valueField);
            len = qMin<quint64>(count, 4096);
        }
        if (base < 0 || data.size() <= 0 || off >= quint64(data.size())) {
            return QString();
        }
        len = qMin<quint64>(len, quint64(data.size()) - off);
        QByteArray s = data.mid(int(off), int(len));
        // 去尾部 NUL
        const int end = s.indexOf('\0');
        if (end >= 0) s.truncate(end);
        return QString::fromLatin1(s).trimmed();
    }

    const QByteArray &data;
    int base;
    bool le;
};

// 常见 IFD0 标签 → 中文标签名 + 格式化函数
struct TagSpec {
    quint16 id;
    const char *label;
    int kind; // 0=ASCII, 1=整数(short/long), 2=有理数
};

} // namespace

ExifParser::Result ExifParser::parse(const QString &filePath)
{
    Result r;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return r;
    }
    const QByteArray data = f.read(1024 * 1024);
    f.close();

    // 定位 "Exif\0\0"
    const QByteArray marker("Exif\0\0", 6);
    const int exifPos = data.indexOf(marker);
    if (exifPos < 0) {
        return r; // 无 EXIF
    }
    const quint64 tiff = quint64(exifPos + marker.size());
    if (!Reader(data, int(tiff), true).inBounds(tiff, 8)) {
        return r;
    }

    // TIFF 头：字节序 + 魔数 42 + IFD0 偏移
    const QByteArray order = data.mid(int(tiff), 2);
    bool little;
    if (order == QByteArray("II", 2)) little = true;
    else if (order == QByteArray("MM", 2)) little = false;
    else return r;

    Reader rd(data, int(tiff), little);
    quint16 magic = 0;
    if (!rd.readU16(tiff + 2, magic) || magic != 42) {
        return r;
    }

    quint32 ifd0 = 0;
    if (!rd.readU32(tiff + 4, ifd0)) {
        return r;
    }
    quint32 gpsIfd = 0;

    // 解析 IFD0
    const quint64 ifdOff = tiff + quint64(ifd0);
    quint16 count = 0;
    if (rd.readU16(ifdOff, count)) {
        for (int i = 0; i < count; ++i) {
            const quint64 entry = ifdOff + 2 + quint64(i) * 12;
            quint16 tag = 0, type = 0;
            quint32 cnt = 0, valField = 0;
            if (!rd.readU16(entry, tag)
                || !rd.readU16(entry + 2, type)
                || !rd.readU32(entry + 4, cnt)
                || !rd.readU32(entry + 8, valField)) {
                break;
            }

            if (tag == 0x8825) {
                gpsIfd = valField; // GPS IFD 偏移
                continue;
            }

            QString label, value;
            switch (tag) {
            case 0x010F: label = QStringLiteral("相机制造商"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x0110: label = QStringLiteral("相机型号"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x0131: label = QStringLiteral("软件"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x0132: label = QStringLiteral("拍摄时间"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x013B: label = QStringLiteral("作者"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x8298: label = QStringLiteral("版权"); value = rd.asciiValue(entry, valField, cnt); break;
            case 0x8827: {
                label = QStringLiteral("ISO");
                int v = 0;
                if (cnt > 0) {
                    if (type == 3) {
                        quint16 iso16 = 0;
                        if (rd.readU16(entry + 8, iso16)) {
                            v = int(iso16);
                        }
                    } else {
                        v = static_cast<int>(valField);
                    }
                }
                value = QString::number(v);
                break;
            }
            case 0x829A: {
                label = QStringLiteral("曝光时间");
                if (type == 5 && cnt == 1) {
                    const quint64 off = tiff + quint64(valField);
                    quint32 num = 0, den = 0;
                    if (rd.readU32(off, num) && rd.readU32(off + 4, den)) {
                        // 标签键（label）是 MetadataPanel 分组查找的键，保持中文不译；
                        // 值模板是显示串，随界面语言翻译。
                        value = (den != 0 && num > 0 && num < den)
                                    ? QCoreApplication::translate("ExifParser", "1/%1 秒").arg(den / num)
                                    : QCoreApplication::translate("ExifParser", "%1 秒").arg(den ? double(num) / den : 0.0);
                    }
                }
                break;
            }
            case 0x829D: {
                label = QStringLiteral("光圈");
                if (type == 5 && cnt == 1) {
                    const quint64 off = tiff + quint64(valField);
                    quint32 num = 0, den = 0;
                    if (rd.readU32(off, num) && rd.readU32(off + 4, den) && den != 0) {
                        value = QStringLiteral("f/%1").arg(double(num) / den, 0, 'f', 1);
                    }
                }
                break;
            }
            case 0x920A: {
                label = QStringLiteral("焦距");
                if (type == 5 && cnt == 1) {
                    const quint64 off = tiff + quint64(valField);
                    quint32 num = 0, den = 0;
                    if (rd.readU32(off, num) && rd.readU32(off + 4, den) && den != 0) {
                        value = QStringLiteral("%1 mm").arg(double(num) / den, 0, 'f', 0);
                    }
                }
                break;
            }
            case 0xA002: label = QStringLiteral("宽度"); value = QString::number(valField); break;
            case 0xA003: label = QStringLiteral("高度"); value = QString::number(valField); break;
            default: break;
            }
            if (!label.isEmpty() && !value.isEmpty()) {
                r.tags.insert(label, value);
            }
        }
    }

    // 解析 GPS IFD
    if (gpsIfd != 0) {
        const quint64 gOff = tiff + quint64(gpsIfd);
        quint16 gCount = 0;
        if (rd.readU16(gOff, gCount)) {
            QString latRef, lngRef;
            auto readCoord = [&](quint16 wantedTag) -> QString {
                for (int i = 0; i < gCount; ++i) {
                    const quint64 entry = gOff + 2 + quint64(i) * 12;
                    quint16 tag = 0, type = 0;
                    quint32 cnt = 0, valField = 0;
                    if (!rd.readU16(entry, tag)
                        || !rd.readU16(entry + 2, type)
                        || !rd.readU32(entry + 4, cnt)
                        || !rd.readU32(entry + 8, valField)) {
                        break;
                    }
                    if (tag != wantedTag) continue;
                    if (type != 5 || cnt != 3) continue;
                    const quint64 off = tiff + quint64(valField);
                    quint32 n0 = 0, d0 = 0, n1 = 0, d1 = 0, n2 = 0, d2 = 0;
                    if (!rd.readU32(off, n0) || !rd.readU32(off + 4, d0)
                        || !rd.readU32(off + 8, n1) || !rd.readU32(off + 12, d1)
                        || !rd.readU32(off + 16, n2) || !rd.readU32(off + 20, d2)) {
                        continue;
                    }
                    if (d0 == 0 || d1 == 0 || d2 == 0) continue;
                    const double deg = double(n0) / double(d0);
                    const double min = double(n1) / double(d1);
                    const double sec = double(n2) / double(d2);
                    return QString::number(deg + min / 60.0 + sec / 3600.0, 'f', 6);
                }
                return QString();
            };
            for (int i = 0; i < gCount; ++i) {
                const quint64 entry = gOff + 2 + quint64(i) * 12;
                quint16 tag = 0, type = 0;
                quint32 cnt = 0, valField = 0;
                if (!rd.readU16(entry, tag)
                    || !rd.readU16(entry + 2, type)
                    || !rd.readU32(entry + 4, cnt)
                    || !rd.readU32(entry + 8, valField)) {
                    break;
                }
                if (type != 2) continue;
                if (tag == 0x0001) latRef = rd.asciiValue(entry, valField, cnt);
                else if (tag == 0x0003) lngRef = rd.asciiValue(entry, valField, cnt);
            }
            const QString latD = readCoord(0x0002);
            const QString lngD = readCoord(0x0004);
            if (!latD.isEmpty() && !lngD.isEmpty()) {
                r.hasGps = true;
                r.lat = latD.toDouble() * (latRef.startsWith(QLatin1Char('S')) ? -1 : 1);
                r.lng = lngD.toDouble() * (lngRef.startsWith(QLatin1Char('W')) ? -1 : 1);
                r.tags.insert(QStringLiteral("GPS 纬度"), QString::number(r.lat, 'f', 6) + QStringLiteral(" ") + latRef.trimmed());
                r.tags.insert(QStringLiteral("GPS 经度"), QString::number(r.lng, 'f', 6) + QStringLiteral(" ") + lngRef.trimmed());
            }
        }
    }

    return r;
}

// 物理分辨率：只扫描文件头部。JPEG 的 APP1（Exif）段小 <64KB，TIFF 的 IFD0 也在
// 头部附近，256KB 足够覆盖；不为一张图把几 MB 原文件读进内存。
ExifParser::Dpi ExifParser::dpi(const QString &filePath)
{
    Dpi out;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    const QByteArray data = f.read(256 * 1024);
    f.close();

    const QByteArray marker("Exif\0\0", 6);
    int tiffPos = data.indexOf(marker);
    if (tiffPos >= 0) {
        tiffPos += marker.size();
    } else if (data.startsWith(QByteArray("II*\0", 4)) || data.startsWith(QByteArray("MM\0*", 4))) {
        tiffPos = 0;   // 裸 TIFF：TIFF 头就在文件起点，IFD0 偏移相对文件头
    } else {
        return out;
    }
    const quint64 tiff = quint64(tiffPos);
    if (!Reader(data, tiffPos, true).inBounds(tiff, 8)) {
        return out;
    }
    const QByteArray order = data.mid(tiffPos, 2);
    bool little;
    if (order == QByteArray("II", 2)) little = true;
    else if (order == QByteArray("MM", 2)) little = false;
    else return out;

    Reader rd(data, tiffPos, little);
    quint16 magic = 0;
    if (!rd.readU16(tiff + 2, magic) || magic != 42) {
        return out;
    }

    quint32 ifd0 = 0;
    if (!rd.readU32(tiff + 4, ifd0)) {
        return out;
    }
    const quint64 ifdOff = tiff + quint64(ifd0);
    quint16 count = 0;
    if (!rd.readU16(ifdOff, count)) {
        return out;
    }

    // RATIONAL（type=5）：值域存的是偏移，两个 u32 = 分子/分母
    auto rational = [&](quint32 valField) -> double {
        const quint64 off = tiff + quint64(valField);
        quint32 num = 0, den = 0;
        if (!rd.readU32(off, num) || !rd.readU32(off + 4, den)) {
            return 0.0;
        }
        return den ? double(num) / double(den) : 0.0;
    };

    double rx = 0.0, ry = 0.0;
    int unit = 2;   // EXIF 默认 ResolutionUnit = 2（英寸）
    for (int i = 0; i < count; ++i) {
        const quint64 entry = ifdOff + 2 + quint64(i) * 12;
        quint16 tag = 0, type = 0;
        quint32 cnt = 0, valField = 0;
        if (!rd.readU16(entry, tag)
            || !rd.readU16(entry + 2, type)
            || !rd.readU32(entry + 4, cnt)
            || !rd.readU32(entry + 8, valField)) {
            break;
        }
        if (tag == 0x011A) {        // XResolution
            rx = (type == 5 && cnt >= 1) ? rational(valField) : 0.0;
        } else if (tag == 0x011B) { // YResolution
            ry = (type == 5 && cnt >= 1) ? rational(valField) : 0.0;
        } else if (tag == 0x0128) { // ResolutionUnit（1=无单位 2=英寸 3=厘米）
            if (type == 3 && cnt >= 1) {
                quint16 u16 = 2;
                if (rd.readU16(entry + 8, u16)) {
                    unit = int(u16);
                }
            } else if (type == 4) {
                unit = static_cast<int>(valField);
            }
        }
    }

    if (unit == 3) {   // 厘米口径折算成像素/英寸
        rx *= 2.54;
        ry *= 2.54;
    }
    // 只给了一个轴：另一轴按同值处理（单值 DPI 的图很常见）
    if (rx > 0.0 && ry <= 0.0) {
        ry = rx;
    } else if (ry > 0.0 && rx <= 0.0) {
        rx = ry;
    }
    out.x = rx > 0.0 ? rx : 0.0;
    out.y = ry > 0.0 ? ry : 0.0;
    return out;
}
