#include "RotateAutoSave.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QThread>

#include <io.h>
#include <windows.h>

#include "ExtraImageLoader.h"
#include "RawImageLoader.h"

namespace {

quint16 rd16(const QByteArray &b, int off, bool le)
{
    const quint8 a = quint8(b[off]), c = quint8(b[off + 1]);
    return le ? quint16(a | (c << 8)) : quint16((a << 8) | c);
}

quint32 rd32(const QByteArray &b, int off, bool le)
{
    const quint32 a = quint8(b[off]), c = quint8(b[off + 1]),
                  d = quint8(b[off + 2]), e = quint8(b[off + 3]);
    return le ? (a | (c << 8) | (d << 16) | (e << 24))
              : ((a << 24) | (c << 16) | (d << 8) | e);
}

void wr16(QByteArray &b, int off, quint16 v, bool le)
{
    b[off] = char(le ? (v & 0xff) : (v >> 8));
    b[off + 1] = char(le ? (v >> 8) : (v & 0xff));
}

void wr32(QByteArray &b, int off, quint32 v, bool le)
{
    for (int i = 0; i < 4; ++i) {
        const int shift = le ? i * 8 : (3 - i) * 8;
        b[off + i] = char((v >> shift) & 0xff);
    }
}

// 定位 JPEG 的 APP1/Exif 段：返回段起点（含 FF E1），segLen 为整段长（含 marker+len）。
int findExifApp1(const QByteArray &b, int &segLen)
{
    int pos = 2;   // SOI 之后
    while (pos + 4 <= b.size()) {
        if (quint8(b[pos]) != 0xFF)
            return -1;
        const quint8 marker = quint8(b[pos + 1]);
        if (marker == 0xDA || marker == 0xD9)   // SOS/EOI：后面没有元数据段
            return -1;
        const int len = (quint8(b[pos + 2]) << 8) | quint8(b[pos + 3]);
        if (len < 2 || pos + 2 + len > b.size())
            return -1;
        if (marker == 0xE1 && len >= 8 && b.mid(pos + 4, 6) == QByteArray("Exif\0\0", 6)) {
            segLen = 2 + len;
            return pos;
        }
        pos += 2 + len;
    }
    return -1;
}

// 把 orig 的 EXIF APP1 搬进 newJpeg（SOI 之后），并保证 IFD0 Orientation=1：
// 有该标签就地改值；没有则在 TIFF 块尾部重建 IFD0 追加一条（子 IFD 偏移不变）。
// 解析任何一步越界/不自洽 → 返回空（调用方据此放弃写回，绝不落双重旋转的盘）。
QByteArray spliceExifOrientation1(const QByteArray &orig, const QByteArray &newJpeg)
{
    int segLen = 0;
    const int seg = findExifApp1(orig, segLen);
    if (seg < 0 || newJpeg.size() < 2)
        return QByteArray();
    const int tiff = seg + 10;   // FF E1(2) + 段长(2) + "Exif\0\0"(6) 之后
    if (tiff + 8 > seg + segLen)
        return QByteArray();
    const QByteArray order = orig.mid(tiff, 2);
    if (order != "II" && order != "MM")
        return QByteArray();
    const bool le = (order == "II");
    if (rd16(orig, tiff + 2, le) != 42)
        return QByteArray();
    const quint32 ifd0 = rd32(orig, tiff + 4, le);
    const quint64 tiffSize = quint64(segLen - 10);
    if (ifd0 < 8 || quint64(ifd0) + 2 > tiffSize)
        return QByteArray();
    const int ifdAbs = tiff + int(ifd0);
    const quint16 n = rd16(orig, ifdAbs, le);
    if (quint64(ifd0) + 2 + quint64(n) * 12 + 4 > tiffSize)
        return QByteArray();

    QByteArray tiffData = orig.mid(tiff, segLen - 10);   // 纯 TIFF 块（去掉 marker2+len2+Exif6）
    const int ifdRel = int(ifd0);
    bool patched = false;
    for (int i = 0; i < n; ++i) {
        const int e = ifdRel + 2 + i * 12;
        if (rd16(tiffData, e, le) == 0x0112) {
            const quint16 type = rd16(tiffData, e + 2, le);
            const quint32 cnt = rd32(tiffData, e + 4, le);
            if (type != 3 || cnt != 1)
                return QByteArray();
            wr16(tiffData, e + 8, 1, le);   // SHORT count=1：值内联
            patched = true;
            break;
        }
    }
    if (!patched) {
        // 尾部追加新 IFD0：旧条目全抄 + Orientation 一条 + 原 next-IFD 指针。
        const int newIfdRel = tiffData.size();
        QByteArray add;
        add.resize(2 + int(n) * 12 + 12 + 4);
        wr16(add, 0, quint16(n + 1), le);
        add.replace(2, int(n) * 12, tiffData.mid(ifdRel + 2, int(n) * 12));
        const int e = 2 + int(n) * 12;
        wr16(add, e, 0x0112, le);
        wr16(add, e + 2, 3, le);
        wr32(add, e + 4, 1, le);
        wr16(add, e + 8, 1, le);
        wr16(add, e + 10, 0, le);
        const quint32 nextIfd = rd32(tiffData, ifdRel + 2 + int(n) * 12, le);
        wr32(add, e + 12, nextIfd, le);
        tiffData += add;
        wr32(tiffData, 4, quint32(newIfdRel), le);
    }
    QByteArray payload("Exif\0\0", 6);
    payload += tiffData;
    if (payload.size() + 2 > 0xFFFF)
        return QByteArray();
    QByteArray app1;
    app1.resize(2 + 2 + payload.size());
    app1[0] = char(0xFF);
    app1[1] = char(0xE1);
    wr16(app1, 2, quint16(payload.size() + 2), false);   // 段长大端恒序
    app1.replace(4, payload.size(), payload);
    return newJpeg.left(2) + app1 + newJpeg.mid(2);
}

bool fsyncFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadWrite))
        return false;
    const bool ok = f.flush() && (_commit(f.handle()) == 0);
    f.close();
    return ok;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

}   // namespace

namespace RotateAutoSave {

QString unsupportedReason(const QString &path)
{
    const QString suf = QFileInfo(path).suffix().toLower();
    if (suf == QLatin1String("psd") || suf == QLatin1String("psb"))
        return QStringLiteral("PSD");
    if (isRawFilePath(path))
        return QStringLiteral("RAW");
    if (ExtraImageLoader::hasMultipleFrames(path))
        return QStringLiteral("ANIM");
    QImageReader r(path);
    r.setAutoTransform(false);
    const int n = r.imageCount();
    if (r.supportsAnimation() && n > 1)
        return QStringLiteral("ANIM");
    if ((suf == QLatin1String("tif") || suf == QLatin1String("tiff")) && n > 1)
        return QStringLiteral("MULTITIFF");
    if (suf == QLatin1String("jpg") || suf == QLatin1String("jpeg")
        || suf == QLatin1String("png") || suf == QLatin1String("webp")
        || suf == QLatin1String("tif") || suf == QLatin1String("tiff"))
        return QString();
    return QStringLiteral("OTHER");
}

bool writeBack(const QString &path, const QImage &image, QString &err)
{
    const QFileInfo fi(path);
    if (!fi.exists() || image.isNull()) {
        err = QCoreApplication::translate("RotateAutoSave", "文件不存在或图像为空");
        return false;
    }
    if (!fi.isWritable()) {
        err = QCoreApplication::translate("RotateAutoSave", "只读或无写权限");
        return false;
    }
    const QString suf = fi.suffix().toLower();
    const QByteArray fmt = (suf == QLatin1String("jpg") || suf == QLatin1String("jpeg"))
                               ? QByteArray("JPEG")
                               : (suf == QLatin1String("png") ? QByteArray("PNG")
                                                              : (suf == QLatin1String("webp")
                                                                     ? QByteArray("WEBP")
                                                                     : QByteArray("TIFF")));
    const QString tmp = fi.dir().filePath(
        fi.fileName() + QStringLiteral(".qvautosave-%1.tmp").arg(QCoreApplication::applicationPid()));
    {
        // writer 必须在此作用域结束前析构：Windows 上目标文件仍被写句柄占用时
        // MoveFileEx 会以共享冲突失败。
        QImageWriter w(tmp, fmt);
        if (fmt == "JPEG")
            w.setQuality(95);
        QImage img = image;
        if (fmt == "JPEG" && img.hasAlphaChannel())
            img = img.convertToFormat(QImage::Format_RGB32);
        if (!w.write(img)) {
            QFile::remove(tmp);
            err = QCoreApplication::translate("RotateAutoSave", "临时文件编码失败");
            return false;
        }
    }
    if (!fsyncFile(tmp)) {
        QFile::remove(tmp);
        err = QCoreApplication::translate("RotateAutoSave", "临时文件落盘失败");
        return false;
    }
    if (fmt == "JPEG") {
        const QByteArray orig = readAll(path);
        if (orig.isEmpty()) {
            QFile::remove(tmp);
            err = QCoreApplication::translate("RotateAutoSave", "读取原文件失败");
            return false;
        }
        int segLen = 0;
        if (findExifApp1(orig, segLen) >= 0) {
            const QByteArray patched = spliceExifOrientation1(orig, readAll(tmp));
            if (patched.isEmpty()) {
                QFile::remove(tmp);
                err = QCoreApplication::translate("RotateAutoSave", "EXIF 方向标记改写失败");
                return false;
            }
            QFile pf(tmp);
            if (!pf.open(QIODevice::WriteOnly | QIODevice::Truncate)
                || pf.write(patched) != patched.size()) {
                pf.close();
                QFile::remove(tmp);
                err = QCoreApplication::translate("RotateAutoSave", "临时文件落盘失败");
                return false;
            }
            pf.close();
            if (!fsyncFile(tmp)) {
                QFile::remove(tmp);
                err = QCoreApplication::translate("RotateAutoSave", "临时文件落盘失败");
                return false;
            }
        }
    }
    const std::wstring tmpW = tmp.toStdWString();
    const std::wstring dstW = path.toStdWString();
    // 目标可能被杀软/索引服务短暂持锁：重试 3 次（每次 100ms），仍失败才报错。
    bool replaced = false;
    for (int i = 0; i < 10 && !replaced; ++i) {
        if (i) {
            QThread::msleep(150);
        }
        replaced = MoveFileExW(tmpW.c_str(), dstW.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!replaced) {
        QFile::remove(tmp);
        err = QCoreApplication::translate("RotateAutoSave", "替换原文件失败");
        return false;
    }
    return true;
}

}   // namespace RotateAutoSave