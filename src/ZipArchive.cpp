#include "ZipArchive.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <climits>

#include "PsdPch.h"
#include "Psdminiz.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // MultiByteToWideChar(936)：无 UTF-8 flag 的历史包按 GBK 回退
#endif

namespace {

// 结构签名（PKWARE APPNOTE 4.3.x）
constexpr quint32 kSigLocal     = 0x04034b50;
constexpr quint32 kSigCentral   = 0x02014b50;
constexpr quint32 kSigEocd      = 0x06054b50;
constexpr quint32 kSigZip64Eocd = 0x06064b50;
constexpr quint32 kSigZip64Loc  = 0x07064b50;

// 中央目录读进内存的上限：5000 条目约 500KB，128MB 足够挡住畸形包。
constexpr quint64 kMaxCentralDirectory = 128ull * 1024 * 1024;

// EOCD 之后最多 65535 字节注释，再往前 22 字节是定长区。
constexpr qint64 kEocdScanWindow = 0xFFFF + 22;

inline quint16 rd16(const uchar *p)
{
    return quint16(p[0] | (p[1] << 8));
}

inline quint32 rd32(const uchar *p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16)
           | (quint32(p[3]) << 24);
}

inline quint64 rd64(const uchar *p)
{
    return quint64(rd32(p)) | (quint64(rd32(p + 4)) << 32);
}

QString message(const char *text)
{
    return QCoreApplication::translate("ZipArchive", text);
}

// —— 文件名编码（派单 §二.2：历史 zip 常无 UTF-8 flag，乱码是最大坑）——
//
// 判定顺序：
//  1) 通用位 11（0x0800）= UTF-8，直接按 UTF-8 解；
//  2) 没有 flag 但字节流是合法 UTF-8（国内很多打包器写 UTF-8 却不置位）→ UTF-8；
//  3) 其余按 GBK（CP936，Windows 中文环境的 zip 默认代码页）回退。
// QString::fromUtf8 遇到非法序列会留下 U+FFFD，用它当「非 UTF-8」判据。
QString decodeGbkName(const QByteArray &raw)
{
#ifdef Q_OS_WIN
    const int len = int(raw.size());
    if (len <= 0) {
        return QString();
    }
    const int need = MultiByteToWideChar(936, 0, raw.constData(), len, nullptr, 0);
    if (need <= 0) {
        return QString::fromLatin1(raw);
    }
    QByteArray wide(need * int(sizeof(wchar_t)), Qt::Uninitialized);
    MultiByteToWideChar(936, 0, raw.constData(), len,
                        reinterpret_cast<wchar_t *>(wide.data()), need);
    // Windows 的 wchar_t 是 16 位，等价于 UTF-16LE 码元
    return QString::fromWCharArray(reinterpret_cast<const wchar_t *>(wide.constData()), need);
#else
    // 非 Windows 构建（CI 侧仅做编译冒烟）没有 CP936 转换表，退化为 UTF-8。
    return QString::fromUtf8(raw);
#endif
}

QString decodeEntryName(const QByteArray &raw, bool utf8Flag)
{
    if (raw.isEmpty()) {
        return QString();
    }
    if (utf8Flag) {
        return QString::fromUtf8(raw);
    }
    const QString asUtf8 = QString::fromUtf8(raw);
    if (!asUtf8.contains(QChar::ReplacementCharacter)) {
        return asUtf8;
    }
    return decodeGbkName(raw);
}

// 规范化路径分隔符并拦截路径穿越（../、前导 /、盘符 C:、内嵌 NUL），
// 防止虚拟路径在 Windows 上落进真实文件系统。
QString sanitizeEntryName(QString name)
{
    if (name.isEmpty() || name.contains(QChar::Null)) {
        return QString();
    }
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (name.startsWith(QLatin1Char('/'))) {
        return QString();
    }
    if (name.size() >= 2 && name.at(1) == QLatin1Char(':') && name.at(0).isLetter()) {
        return QString();
    }
    const QStringList parts = name.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    for (int i = 0; i < parts.size(); ++i) {
        const QString &seg = parts.at(i);
        if (seg == QLatin1String(".") || seg == QLatin1String("..")) {
            return QString();
        }
        if (seg.isEmpty() && i != parts.size() - 1) {
            return QString();
        }
    }
    return name;
}

}   // namespace

ZipArchive::~ZipArchive()
{
    close();
}

void ZipArchive::close()
{
    QMutexLocker lock(&m_ioMutex);
    if (m_file.isOpen()) {
        m_file.close();
    }
    m_entries.clear();
    m_byName.clear();
    m_path.clear();
    m_fileSize = 0;
    m_open = false;
    m_status = Status::Ok;
}

bool ZipArchive::readAt(quint64 offset, quint64 length, QByteArray *out) const
{
    out->clear();
    if (!length) {
        return true;
    }
    if (length > quint64(INT_MAX)) {
        return false;
    }
    QMutexLocker lock(&m_ioMutex);
    if (!m_file.isOpen()) {
        return false;
    }
    // 越界检查：包内偏移 + 长度不得超过文件大小（防畸形中央目录读出随机数据）
    if (m_fileSize < 0 || offset > quint64(m_fileSize) || length > quint64(m_fileSize) - offset) {
        return false;
    }
    if (!m_file.seek(qint64(offset))) {
        return false;
    }
    out->reserve(int(length));
    while (quint64(out->size()) < length) {
        const qint64 want = qint64(length) - qint64(out->size());
        const QByteArray chunk = m_file.read(want);
        if (chunk.isEmpty()) {
            break;
        }
        out->append(chunk);
    }
    return quint64(out->size()) == length;
}

bool ZipArchive::open(const QString &filePath, QString *error)
{
    close();

    const QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        m_status = Status::NotFound;
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ZipArchive", "文件不存在或无法读取"));
        }
        return false;
    }
    m_path = info.absoluteFilePath();
    m_file.setFileName(m_path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_status = Status::NotFound;
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ZipArchive", "文件不存在或无法读取"));
        }
        return false;
    }
    m_fileSize = m_file.size();

    QString parseError;
    if (!parseCentralDirectory(&parseError)) {
        m_file.close();
        if (error) {
            *error = parseError;
        }
        return false;
    }
    m_open = true;
    m_status = Status::Ok;
    return true;
}

// 解析 EOCD（含 ZIP64 记录）→ 中央目录 → 条目表。任何越界都按损坏处理，
// 绝不让畸形包把偏移算到文件之外。
bool ZipArchive::parseCentralDirectory(QString *errorText)
{
    auto fail = [this, errorText](Status st, const char *text) {
        m_status = st;
        if (errorText) {
            *errorText = message(text);
        }
        return false;
    };

    if (m_fileSize < 22) {
        return fail(Status::NotZip, QT_TRANSLATE_NOOP("ZipArchive", "不是有效的 ZIP/CBZ 压缩包"));
    }

    // ① EOCD：从尾部往前扫，命中即停（注释最长 64KB）
    const qint64 tailLen = qMin<qint64>(m_fileSize, kEocdScanWindow);
    QByteArray tail;
    if (!readAt(quint64(m_fileSize - tailLen), quint64(tailLen), &tail) || tail.size() < 22) {
        return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
    }
    qint64 eocdInTail = -1;
    const uchar *tailPtr = reinterpret_cast<const uchar *>(tail.constData());
    for (qint64 i = qint64(tail.size()) - 22; i >= 0; --i) {
        if (rd32(tailPtr + i) == kSigEocd) {
            const quint16 commentLen = rd16(tailPtr + i + 20);
            if (i + 22 + qint64(commentLen) == qint64(tail.size())) {
                eocdInTail = i;
                break;
            }
        }
    }
    if (eocdInTail < 0) {
        return fail(Status::NotZip, QT_TRANSLATE_NOOP("ZipArchive", "不是有效的 ZIP/CBZ 压缩包"));
    }
    const quint64 eocdOffset = quint64(m_fileSize - tailLen + eocdInTail);
    const uchar *eocd = reinterpret_cast<const uchar *>(tail.constData()) + eocdInTail;
    const quint16 diskNo = rd16(eocd + 4);
    const quint16 cdDisk = rd16(eocd + 6);
    quint64 entryCount = rd16(eocd + 10);
    quint64 cdSize = rd32(eocd + 12);
    quint64 cdOffset = rd32(eocd + 16);

    // ② ZIP64：三个定长字段任意一个是 0xFFFF/0xFFFFFFFF 就去看 ZIP64 记录
    const bool needZip64 = entryCount == 0xFFFF || cdSize == 0xFFFFFFFF
                           || cdOffset == 0xFFFFFFFF;
    if (needZip64) {
        if (eocdOffset < 20) {
            return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        QByteArray loc;
        if (!readAt(eocdOffset - 20, 20, &loc) || loc.size() < 20
            || rd32(reinterpret_cast<const uchar *>(loc.constData())) != kSigZip64Loc) {
            return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        const quint64 z64Offset = rd64(reinterpret_cast<const uchar *>(loc.constData()) + 8);
        QByteArray z64;
        if (!readAt(z64Offset, 56, &z64) || z64.size() < 56
            || rd32(reinterpret_cast<const uchar *>(z64.constData())) != kSigZip64Eocd) {
            return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        const uchar *p = reinterpret_cast<const uchar *>(z64.constData());
        entryCount = rd64(p + 32);
        cdSize = rd64(p + 40);
        cdOffset = rd64(p + 48);
    }

    if (diskNo != 0 || cdDisk != 0) {
        // 分卷 zip：本阶段不支持（现实中也只在老软盘拆分包里出现）
        return fail(Status::Unsupported,
                    QT_TRANSLATE_NOOP("ZipArchive", "暂不支持分卷压缩包"));
    }
    if (cdSize == 0 || entryCount == 0) {
        return true;   // 空包：合法，条目表为空
    }
    if (cdSize > kMaxCentralDirectory) {
        return fail(Status::Unsupported,
                    QT_TRANSLATE_NOOP("ZipArchive", "压缩包条目过多，暂不支持"));
    }
    if (cdOffset > quint64(m_fileSize) || cdSize > quint64(m_fileSize) - cdOffset) {
        return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
    }

    // ③ 中央目录逐条解析
    QByteArray cd;
    if (!readAt(cdOffset, cdSize, &cd)) {
        return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
    }
    const uchar *base = reinterpret_cast<const uchar *>(cd.constData());
    const quint64 cdLen = quint64(cd.size());
    quint64 pos = 0;
    quint64 parsed = 0;
    while (pos + 46 <= cdLen && parsed < entryCount) {
        const uchar *rec = base + pos;
        if (rd32(rec) != kSigCentral) {
            return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        Entry entry;
        entry.flags = rd16(rec + 8);
        entry.method = rd16(rec + 10);
        const quint16 dosTime = rd16(rec + 12);
        const quint16 dosDate = rd16(rec + 14);
        entry.dosDateTime = (quint32(dosDate) << 16) | quint32(dosTime);
        entry.crc = rd32(rec + 16);
        entry.compressedSize = rd32(rec + 20);
        entry.size = rd32(rec + 24);
        const quint16 nameLen = rd16(rec + 28);
        const quint16 extraLen = rd16(rec + 30);
        const quint16 commentLen = rd16(rec + 32);
        entry.localOffset = rd32(rec + 42);
        entry.encrypted = (entry.flags & 0x0001) != 0;

        if (pos + 46 + nameLen + extraLen + commentLen > cdLen) {
            return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        const QByteArray rawName = cd.mid(int(pos + 46), nameLen);
        entry.name = sanitizeEntryName(decodeEntryName(rawName, (entry.flags & 0x0800) != 0));

        // ZIP64 扩展字段（0x0001）：只补齐中央目录里置了 0xFFFFFFFF 的字段，
        // 顺序固定为 未压缩大小 / 压缩大小 / 局部头偏移 / 分卷号。
        const bool needSize = (entry.size == 0xFFFFFFFF);
        const bool needComp = (entry.compressedSize == 0xFFFFFFFF);
        const bool needOff = (entry.localOffset == 0xFFFFFFFF);
        if (needSize || needComp || needOff) {
            const uchar *extra = rec + 46 + nameLen;
            quint16 used = 0;
            bool ok = true;
            bool gotSize = !needSize;
            bool gotComp = !needComp;
            bool gotOff = !needOff;
            while (used + 4 <= extraLen) {
                const quint16 id = rd16(extra + used);
                const quint16 len = rd16(extra + used + 2);
                if (used + 4 + len > extraLen) {
                    ok = false;
                    break;
                }
                if (id == 0x0001) {
                    const uchar *f = extra + used + 4;
                    quint16 fi = 0;
                    if (needSize) {
                        if (fi + 8 > len) { ok = false; break; }
                        entry.size = rd64(f + fi);
                        fi = quint16(fi + 8);
                        gotSize = true;
                    }
                    if (needComp) {
                        if (fi + 8 > len) { ok = false; break; }
                        entry.compressedSize = rd64(f + fi);
                        fi = quint16(fi + 8);
                        gotComp = true;
                    }
                    if (needOff) {
                        if (fi + 8 > len) { ok = false; break; }
                        entry.localOffset = rd64(f + fi);
                        gotOff = true;
                    }
                    break;
                }
                used = quint16(used + 4 + len);
            }
            if (!ok || !gotSize || !gotComp || !gotOff) {
                return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
            }
        }

        // 目录条目（名字以 '/' 结尾）不参与图片列表，但保留在表里以便排查；重复条目覆写保持 entries 与 indexOf 一致
        entry.isDir = entry.name.endsWith(QLatin1Char('/'));
        if (!entry.name.isEmpty()) {
            const auto dup = m_byName.constFind(entry.name);
            if (dup != m_byName.constEnd()) {
                m_entries[dup.value()] = entry;
            } else {
                m_byName.insert(entry.name, int(m_entries.size()));
                m_entries.append(entry);
            }
        }
        pos += 46 + nameLen + extraLen + commentLen;
        ++parsed;
    }
    if (parsed < entryCount) {
        return fail(Status::Corrupt, QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
    }
    return true;
}

int ZipArchive::indexOf(const QString &entryName) const
{
    const auto it = m_byName.constFind(entryName);
    return it == m_byName.constEnd() ? -1 : it.value();
}

// 局部头的 nameLen / extraLen 可能与中央目录不同（注释、data descriptor 场景），
// 数据起点必须以局部头为准，否则解出来的偏移会歪。
quint64 ZipArchive::dataOffsetOf(const Entry &entry, bool *ok, QString *error) const
{
    if (ok) {
        *ok = false;
    }
    QByteArray lh;
    if (!readAt(entry.localOffset, 30, &lh) || lh.size() < 30) {
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        return 0;
    }
    const uchar *p = reinterpret_cast<const uchar *>(lh.constData());
    if (rd32(p) != kSigLocal) {
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        }
        return 0;
    }
    const quint16 nameLen = rd16(p + 26);
    const quint16 extraLen = rd16(p + 28);
    if (ok) {
        *ok = true;
    }
    return entry.localOffset + 30 + nameLen + extraLen;
}

QByteArray ZipArchive::read(const Entry &entry, QString *error, ReadStatus *status) const
{
    ReadStatus local = ReadStatus::Ok;
    auto fail = [error, &local](ReadStatus st, const char *text) {
        local = st;
        if (error) {
            *error = message(text);
        }
        return QByteArray();
    };

    if (!m_open) {
        const QByteArray out = fail(ReadStatus::NotOpen,
                                    QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
        if (status) {
            *status = local;
        }
        return out;
    }
    if (entry.encrypted) {
        const QByteArray out = fail(ReadStatus::Encrypted,
                                    QT_TRANSLATE_NOOP("ZipArchive", "暂不支持加密压缩包"));
        if (status) {
            *status = local;
        }
        return out;
    }
    if (entry.size > quint64(kMaxEntryBytes)) {
        const QByteArray out = fail(ReadStatus::TooLarge,
                                    QT_TRANSLATE_NOOP("ZipArchive", "包内文件超过 256 MB，已拒绝解码"));
        if (status) {
            *status = local;
        }
        return out;
    }
    if (entry.method != 0 && entry.method != 8) {
        const QByteArray out = fail(ReadStatus::UnsupportedMethod,
                                    QT_TRANSLATE_NOOP("ZipArchive", "包内文件使用了暂不支持的压缩方式"));
        if (status) {
            *status = local;
        }
        return out;
    }

    bool ok = false;
    const quint64 dataOffset = dataOffsetOf(entry, &ok, error);
    if (!ok) {
        if (status) {
            *status = ReadStatus::Corrupt;
        }
        return QByteArray();
    }

    QByteArray out;
    if (entry.method == 0) {
        // store：直接读原始字节，长度必须与中央目录一致（先校长度再分配读入）
        if (entry.compressedSize != entry.size
            || !readAt(dataOffset, entry.compressedSize, &out)
            || quint64(out.size()) != entry.size) {
            const QByteArray bad = fail(ReadStatus::Corrupt,
                                        QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
            if (status) {
                *status = local;
            }
            return bad;
        }
    } else {
        if (entry.compressedSize > quint64(kMaxEntryBytes) + 64u * 1024u) {
            const QByteArray bad = fail(ReadStatus::TooLarge,
                                        QT_TRANSLATE_NOOP("ZipArchive", "包内文件超过 256 MB，已拒绝解码"));
            if (status) {
                *status = local;
            }
            return bad;
        }
        QByteArray in;
        if (!readAt(dataOffset, entry.compressedSize, &in)) {
            const QByteArray bad = fail(ReadStatus::Corrupt,
                                        QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
            if (status) {
                *status = local;
            }
            return bad;
        }
        if (entry.size > 0) {
            // zip 的 deflate 是裸流：不带 zlib 头，flags 传 0
            out.resize(int(entry.size));
            if (out.size() != int(entry.size)) {
                const QByteArray bad = fail(ReadStatus::TooLarge,
                                            QT_TRANSLATE_NOOP("ZipArchive", "包内文件超过 256 MB，已拒绝解码"));
                if (status) {
                    *status = local;
                }
                return bad;
            }
            const size_t produced = tinfl_decompress_mem_to_mem(out.data(), size_t(entry.size),
                                                               in.constData(), size_t(in.size()), 0);
            if (produced == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || produced != size_t(entry.size)) {
                const QByteArray bad = fail(ReadStatus::Corrupt,
                                            QT_TRANSLATE_NOOP("ZipArchive", "压缩包已损坏，无法读取"));
                if (status) {
                    *status = local;
                }
                return bad;
            }
        }
    }

    // CRC 校验：损坏包（派单边界五件之一）在这里被拦下，而不是把半张图送到画布
    const mz_ulong crc = mz_crc32(MZ_CRC32_INIT,
                                  reinterpret_cast<const unsigned char *>(out.constData()),
                                  size_t(out.size()));
    if (quint32(crc) != entry.crc) {
        const QByteArray bad = fail(ReadStatus::Corrupt,
                                    QT_TRANSLATE_NOOP("ZipArchive", "包内文件校验失败（CRC 不符）"));
        if (status) {
            *status = local;
        }
        return bad;
    }
    if (status) {
        *status = ReadStatus::Ok;
    }
    return out;
}
