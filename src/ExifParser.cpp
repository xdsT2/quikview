#include "ExifParser.h"

#include <QFile>
#include <QtEndian>

namespace {

// TIFF 字段类型长度
int typeSize(quint16 type)
{
    switch (type) {
    case 1:  // BYTE
    case 2:  // ASCII
    case 7:  // UNDEFINED
        return 1;
    case 3:  // SHORT
        return 2;
    case 4:  // LONG
    case 9:  // SLONG
        return 4;
    case 5:  // RATIONAL
    case 10: // SRATIONAL
        return 8;
    default:
        return 0;
    }
}

class Reader
{
public:
    Reader(const QByteArray &d, int tiffBase, bool little)
        : data(d), base(tiffBase), le(little) {}

    quint16 u16(int off) const { return le ? qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + off))
                                          : qFromBigEndian<quint16>(reinterpret_cast<const uchar *>(data.constData() + off)); }
    quint32 u32(int off) const { return le ? qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + off))
                                          : qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(data.constData() + off)); }
    qint32 s32(int off) const { return static_cast<qint32>(u32(off)); }

    // 读取某个 tag 的值：valueField 是 4 字节字段（内联或偏移）
    QByteArray raw(quint16 type, quint32 count, quint32 valueField) const
    {
        const int sz = typeSize(type);
        const int total = sz * static_cast<int>(count);
        int off;
        if (total <= 4) {
            off = -1; // 内联
        } else {
            off = base + static_cast<int>(valueField);
        }
        if (off < 0) {
            // 内联：直接从 valueField 所在位置读取（调用方需知道其绝对位置）
            return QByteArray();
        }
        return data.mid(off, total);
    }

    QString asciiValue(quint32 valueField, quint32 count) const
    {
        int off = base + static_cast<int>(valueField);
        QByteArray s = data.mid(off, static_cast<int>(count));
        // 去尾部 NUL
        int end = s.indexOf('\0');
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
    const QByteArray data = f.readAll();
    f.close();

    // 定位 "Exif\0\0"
    const QByteArray marker("Exif\0\0", 6);
    int exifPos = data.indexOf(marker);
    if (exifPos < 0) {
        return r; // 无 EXIF
    }
    const int tiff = exifPos + marker.size();
    if (tiff + 8 > data.size()) {
        return r;
    }

    // TIFF 头：字节序 + 魔数 42 + IFD0 偏移
    const QByteArray order = data.mid(tiff, 2);
    bool little;
    if (order == QByteArray("II", 2)) little = true;
    else if (order == QByteArray("MM", 2)) little = false;
    else return r;

    Reader rd(data, tiff, little);
    if (rd.u16(tiff + 2) != 42) {
        return r;
    }

    quint32 ifd0 = rd.u32(tiff + 4);
    quint32 gpsIfd = 0;

    // 解析 IFD0
    int ifdOff = tiff + static_cast<int>(ifd0);
    if (ifdOff + 2 <= data.size()) {
        quint16 count = rd.u16(ifdOff);
        for (int i = 0; i < count; ++i) {
            const int entry = ifdOff + 2 + i * 12;
            if (entry + 12 > data.size()) break;
            const quint16 tag = rd.u16(entry);
            const quint16 type = rd.u16(entry + 2);
            const quint32 cnt = rd.u32(entry + 4);
            const quint32 valField = rd.u32(entry + 8);

            if (tag == 0x8825) {
                gpsIfd = valField; // GPS IFD 偏移
                continue;
            }

            QString label, value;
            switch (tag) {
            case 0x010F: label = QStringLiteral("相机制造商"); value = rd.asciiValue(valField, cnt); break;
            case 0x0110: label = QStringLiteral("相机型号"); value = rd.asciiValue(valField, cnt); break;
            case 0x0131: label = QStringLiteral("软件"); value = rd.asciiValue(valField, cnt); break;
            case 0x0132: label = QStringLiteral("拍摄时间"); value = rd.asciiValue(valField, cnt); break;
            case 0x013B: label = QStringLiteral("作者"); value = rd.asciiValue(valField, cnt); break;
            case 0x8298: label = QStringLiteral("版权"); value = rd.asciiValue(valField, cnt); break;
            case 0x8827: {
                label = QStringLiteral("ISO");
                const int v = (cnt == 0) ? 0 : (type == 3 ? rd.u16(valField) : static_cast<int>(valField));
                value = QString::number(v);
                break;
            }
            case 0x829A: {
                label = QStringLiteral("曝光时间");
                if (type == 5 && cnt == 1) {
                    const int off = tiff + static_cast<int>(valField);
                    const quint32 num = rd.u32(off), den = rd.u32(off + 4);
                    value = (den != 0 && num < den) ? QStringLiteral("1/%1 秒").arg(den / num)
                                                     : QStringLiteral("%1 秒").arg(den ? double(num) / den : 0.0);
                }
                break;
            }
            case 0x829D: {
                label = QStringLiteral("光圈");
                if (type == 5 && cnt == 1) {
                    const int off = tiff + static_cast<int>(valField);
                    const quint32 num = rd.u32(off), den = rd.u32(off + 4);
                    value = den ? QStringLiteral("f/%1").arg(double(num) / den, 0, 'f', 1) : QString();
                }
                break;
            }
            case 0x920A: {
                label = QStringLiteral("焦距");
                if (type == 5 && cnt == 1) {
                    const int off = tiff + static_cast<int>(valField);
                    const quint32 num = rd.u32(off), den = rd.u32(off + 4);
                    value = den ? QStringLiteral("%1 mm").arg(double(num) / den, 0, 'f', 0) : QString();
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
        int gOff = tiff + static_cast<int>(gpsIfd);
        if (gOff + 2 <= data.size()) {
            quint16 count = rd.u16(gOff);
            QString latRef, lngRef;
            auto readCoord = [&](quint16 wantedTag) -> QString {
                for (int i = 0; i < count; ++i) {
                    const int entry = gOff + 2 + i * 12;
                    if (entry + 12 > data.size()) break;
                    if (rd.u16(entry) != wantedTag) continue;
                    if (rd.u16(entry + 2) != 5 || rd.u32(entry + 4) != 3) continue;
                    const int off = tiff + static_cast<int>(rd.u32(entry + 8));
                    const double deg  = double(rd.u32(off))      / double(rd.u32(off + 4));
                    const double min  = double(rd.u32(off + 8))  / double(rd.u32(off + 12));
                    const double sec  = double(rd.u32(off + 16)) / double(rd.u32(off + 20));
                    return QString::number(deg + min / 60.0 + sec / 3600.0, 'f', 6);
                }
                return QString();
            };
            // 参考方向标签
            // 注意：真实相机常把 'N'/'E' 写成 "N\0\0"（count=2~4），直接 fromLatin1
            // 会把内嵌 NUL 带进 QString，界面上显示成方框——先按 NUL 截断再取。
            const auto refChar = [&data](int off, int count) {
                QByteArray s = data.mid(off, count);
                const int nul = s.indexOf('\0');
                if (nul >= 0) s.truncate(nul);
                return QString::fromLatin1(s).trimmed();
            };
            for (int i = 0; i < count; ++i) {
                const int entry = gOff + 2 + i * 12;
                if (entry + 12 > data.size()) break;
                const quint16 tag = rd.u16(entry);
                if (rd.u16(entry + 2) != 2) continue;
                const int off = tiff + static_cast<int>(rd.u32(entry + 8));
                if (tag == 0x0001) latRef = refChar(off, static_cast<int>(rd.u32(entry + 4)));
                else if (tag == 0x0003) lngRef = refChar(off, static_cast<int>(rd.u32(entry + 4)));
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
