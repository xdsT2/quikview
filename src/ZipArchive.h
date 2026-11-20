#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>

// ZIP / CBZ 只读解析（派单《ZIP 与 CBZ 压缩包直读》2026-11-18 第一阶段）。
//
// 依赖选型见回执：不引入 QuaZip / minizip-ng，走「自研中央目录解析 + 复用 in-tree miniz」。
// third_party/psd_sdk 已经随 PSD 带了一份 miniz（public domain），但编译开关是
// MINIZ_NO_ARCHIVE_APIS——archive API 不参与编译，只留 tinfl / crc32 两个原语。
// 本类正好只需要这两个原语：EOCD / 中央目录 / 局部头由本类自己解析，
// 解压调 tinfl_decompress_mem_to_mem()，校验调 mz_crc32()。
// 结果：零新增依赖、零新增 DLL、不增加 exe 体积（符号已在 Psd 静态库里）。
//
// 范围边界（与派单 §二.4 对齐）：
//  - 不落盘解压：条目字节只在内存里出现，读完即弃；
//  - 不解密：加密条目直接拒绝并给出「暂不支持加密压缩包」；
//  - 不支持分卷（multi-disk）、不支持 PKZIP 以外的压缩方式（只认 store / deflate）；
//  - 只列条目不递归：子目录的层级留给第二阶段（条目的 name 里带 '/' 时按原样保留）。
class ZipArchive
{
public:
    enum class Status {
        Ok = 0,
        NotFound,    // 文件不存在 / 打不开
        NotZip,      // 找不到 EOCD：不是 zip
        Corrupt,     // 结构损坏（越界、局部头缺失、CRC 不符）
        Encrypted,   // 条目全加密：明确拒绝，不尝试解密
        Unsupported, // 分卷 / 中央目录超上限等无法处理的特性
    };

    // 中央目录里的一条记录（只在打开后构造，之后只读，可跨线程共享）。
    struct Entry
    {
        QString name;             // 已解码的条目名（UTF-8 flag / GBK 回退后的结果）
        quint64 size = 0;         // 未压缩字节数
        quint64 compressedSize = 0;
        quint64 localOffset = 0;  // 局部头在包内的偏移
        quint32 crc = 0;
        quint32 dosDateTime = 0;  // DOS 时间（date<<16 | time），「按修改时间」排序用
        quint16 method = 0;       // 0 = store，8 = deflate
        quint16 flags = 0;
        bool encrypted = false;   // GP bit0
        bool isDir = false;
    };

    // 单图上限（派单 §三：单图 >256MB 拒绝），也是解压输出的内存上限。
    static constexpr qint64 kMaxEntryBytes = 256LL * 1024 * 1024;

    ZipArchive() = default;
    ~ZipArchive();
    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;

    // 打开并解析中央目录。失败时返回 false，error 里是可展示的文案（已本地化），
    // status() 给出机器可判的分类。
    bool open(const QString &filePath, QString *error = nullptr);
    void close();

    bool isOpen() const { return m_open; }
    Status status() const { return m_status; }
    const QString &path() const { return m_path; }
    qint64 archiveBytes() const { return m_fileSize; }
    const QList<Entry> &entries() const { return m_entries; }
    // 条目名 → 下标（-1 = 不在包内）
    int indexOf(const QString &entryName) const;

    // 取条目失败的原因。上层（ArchiveStore）按它映射 LoadErrorCategory，
    // 不对已本地化的 error 文案做字符串匹配。
    enum class ReadStatus {
        Ok = 0,
        NotOpen,
        Encrypted,
        TooLarge,
        UnsupportedMethod,
        Corrupt,
    };

    // 解压单个条目到内存。拒绝加密 / 超过 kMaxEntryBytes / 不支持的压缩方式；
    // 解完做 CRC 校验（不符按损坏处理）。失败返回空 QByteArray 并填 error/status。
    QByteArray read(const Entry &entry, QString *error = nullptr,
                    ReadStatus *status = nullptr) const;

private:
    // 线程安全：解码在 ImageCache 的线程池里跑，同一个包可能被两个线程同时要数据。
    bool readAt(quint64 offset, quint64 length, QByteArray *out) const;
    bool parseCentralDirectory(QString *errorText);
    quint64 dataOffsetOf(const Entry &entry, bool *ok, QString *error) const;

    mutable QFile m_file;
    mutable QMutex m_ioMutex;   // QFile 的 seek/read 不是线程安全的
    QList<Entry> m_entries;
    QHash<QString, int> m_byName;
    QString m_path;
    qint64 m_fileSize = 0;
    bool m_open = false;
    Status m_status = Status::Ok;
};
