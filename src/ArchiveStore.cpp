#include "ArchiveStore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImageReader>
#include <QList>
#include <QMutexLocker>

QMutex ArchiveStore::s_mutex;
QHash<QString, QSharedPointer<ZipArchive>> ArchiveStore::s_archives;
QString ArchiveStore::s_active;

namespace {

// 最近使用顺序（队首最新）。只放包路径，配合 s_archives 做上限淘汰。
QList<QString> g_lru;

// 包文件的「指纹」（大小 + 修改时间）：同一路径被重新生成/覆盖后要重新解析，
// 否则会把旧中央目录当成新的用。
struct FileStamp
{
    qint64 size = -1;
    qint64 mtimeMs = 0;
    bool operator==(const FileStamp &other) const
    {
        return size == other.size && mtimeMs == other.mtimeMs;
    }
};
QHash<QString, FileStamp> g_stamps;

FileStamp stampOf(const QString &path)
{
    const QFileInfo info(path);
    FileStamp stamp;
    stamp.size = info.exists() ? info.size() : -1;
    stamp.mtimeMs = info.lastModified().toMSecsSinceEpoch();
    return stamp;
}

// 虚拟路径的快速判据：包文件后缀后面紧跟 '/' 或 '\'。这一条不过就不必取锁，
// 解码热路径（每张图若干次）不会被注册表的互斥量反复拦住。
bool looksVirtual(const QString &path)
{
    return path.contains(QLatin1String(".zip/"), Qt::CaseInsensitive)
           || path.contains(QLatin1String(".cbz/"), Qt::CaseInsensitive)
           || path.contains(QLatin1String(".zip\\"), Qt::CaseInsensitive)
           || path.contains(QLatin1String(".cbz\\"), Qt::CaseInsensitive);
}

QString canonicalKey(const QString &absPath)
{
#ifdef Q_OS_WIN
    return absPath.toLower();
#else
    return absPath;
#endif
}

QString message(const char *text)
{
    return QCoreApplication::translate("ArchiveStore", text);
}

// zip 存的是 DOS 时间（date<<16 | time），拆成 QDateTime 显示用。
// 无效日期（0 值）返回无效 QDateTime，调用方回落到 QFileInfo。
QDateTime dosToDateTime(quint32 stamp)
{
    const int date = int(stamp >> 16);
    const int time = int(stamp & 0xFFFF);
    const QDate d(1980 + ((date >> 9) & 0x7F), (date >> 5) & 0x0F, date & 0x1F);
    const QTime t((time >> 11) & 0x1F, (time >> 5) & 0x3F, (time & 0x1F) * 2);
    if (!d.isValid() || !t.isValid()) {
        return QDateTime();
    }
    return QDateTime(d, t);   // DOS 时间本身就是本地时间，不再做时区换算
}

}   // namespace

QString ArchiveStore::normalized(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

bool ArchiveStore::isArchiveFile(const QString &path)
{
    if (path.isEmpty()) {
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1String("zip") || suffix == QLatin1String("cbz");
}

QString ArchiveStore::entryPathPrefix(const QString &archivePath)
{
    if (archivePath.isEmpty()) {
        return QString();
    }
    return normalized(archivePath) + QLatin1Char('/');
}

QString ArchiveStore::joinEntryPath(const QString &archivePath, const QString &entryName)
{
    if (archivePath.isEmpty() || entryName.isEmpty()) {
        return QString();
    }
    return normalized(archivePath) + QLatin1Char('/') + entryName;
}

bool ArchiveStore::splitEntryPath(const QString &path, QString *archivePath, QString *entryName)
{
    if (path.isEmpty() || !looksVirtual(path)) {
        return false;
    }
    QString normPath = path;
    normPath.replace(QLatin1Char('\\'), QLatin1Char('/'));
    QMutexLocker lock(&s_mutex);
    QString bestKey;
    QString bestArchivePath;
    for (auto it = s_archives.constBegin(); it != s_archives.constEnd(); ++it) {
        const QString &key = it.key();
        const QString candPath = (it.value() && !it.value()->path().isEmpty()) ? it.value()->path() : key;
        // 条目名里允许出现 '/'（第二阶段子目录递归），所以按最长前缀匹配
        if (normPath.size() > candPath.size() + 1 && normPath.at(candPath.size()) == QLatin1Char('/')
            && normPath.startsWith(candPath, Qt::CaseInsensitive) && candPath.size() > bestArchivePath.size()) {
            bestKey = key;
            bestArchivePath = candPath;
        }
    }
    if (bestKey.isEmpty()) {
        return false;
    }
    if (archivePath) {
        *archivePath = bestArchivePath;
    }
    if (entryName) {
        *entryName = normPath.mid(bestArchivePath.size() + 1);
    }
    return true;
}

bool ArchiveStore::isEntryPath(const QString &path)
{
    return splitEntryPath(path);
}

QString ArchiveStore::archivePathOf(const QString &path)
{
    QString archive;
    return splitEntryPath(path, &archive, nullptr) ? archive : QString();
}

QString ArchiveStore::entryNameOf(const QString &path)
{
    QString entry;
    return splitEntryPath(path, nullptr, &entry) ? entry : QString();
}

QString ArchiveStore::displayName(const QString &path)
{
    QString archive;
    QString entry;
    if (splitEntryPath(path, &archive, &entry)) {
        return QFileInfo(archive).fileName() + QStringLiteral(" › ") + entry;
    }
    return QFileInfo(path).fileName();
}

QSharedPointer<ZipArchive> ArchiveStore::open(const QString &archivePath, QString *error,
                                              ZipArchive::Status *status)
{
    if (archivePath.isEmpty()) {
        if (status) {
            *status = ZipArchive::Status::NotFound;
        }
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ArchiveStore", "文件不存在或无法读取"));
        }
        return {};
    }
    const QString normPath = normalized(archivePath);
    const QString key = canonicalKey(normPath);
    const FileStamp stamp = stampOf(normPath);

    {
        QMutexLocker lock(&s_mutex);
        const auto existing = s_archives.constFind(key);
        if (existing != s_archives.constEnd() && existing.value() && existing.value()->isOpen()
            && g_stamps.value(key) == stamp) {
            g_lru.removeAll(key);
            g_lru.prepend(key);
            if (status) {
                *status = ZipArchive::Status::Ok;
            }
            return existing.value();
        }
        // 同一个路径的旧包（文件被覆盖后）：先摘掉，避免拿到过期条目表
        s_archives.remove(key);
        g_lru.removeAll(key);
        g_stamps.remove(key);
    }

    auto archive = QSharedPointer<ZipArchive>::create();
    QString openError;
    if (!archive->open(normPath, &openError)) {
        if (status) {
            *status = archive->status();
        }
        if (error) {
            *error = openError;
        }
        return {};
    }
    if (status) {
        *status = ZipArchive::Status::Ok;
    }

    QMutexLocker lock(&s_mutex);
    const auto raced = s_archives.constFind(key);
    if (raced != s_archives.constEnd() && raced.value() && raced.value()->isOpen()
        && g_stamps.value(key) == stamp) {
        g_lru.removeAll(key);
        g_lru.prepend(key);
        return raced.value();
    }

    // 上限淘汰：只丢「非当前浏览」的旧包，mmap/QFile 句柄不堆积
    g_lru.removeAll(key);
    g_lru.prepend(key);
    while (g_lru.size() > kMaxOpenArchives) {
        int victimIdx = -1;
        for (int i = g_lru.size() - 1; i >= 0; --i) {
            const QString &cand = g_lru.at(i);
            if (cand != s_active && cand != key) {
                victimIdx = i;
                break;
            }
        }
        if (victimIdx < 0) {
            break;
        }
        const QString victim = g_lru.takeAt(victimIdx);
        s_archives.remove(victim);
        g_stamps.remove(victim);
    }
    s_archives.insert(key, archive);
    g_stamps.insert(key, stamp);
    return archive;
}

void ArchiveStore::setActive(const QString &archivePath)
{
    const QString key = canonicalKey(normalized(archivePath));
    QMutexLocker lock(&s_mutex);
    s_active = key;
    if (s_archives.contains(key)) {
        g_lru.removeAll(key);
        g_lru.prepend(key);
    }
}

QSharedPointer<ZipArchive> ArchiveStore::active()
{
    QMutexLocker lock(&s_mutex);
    if (s_active.isEmpty()) {
        return {};
    }
    return s_archives.value(s_active);
}

QSharedPointer<ZipArchive> ArchiveStore::lookup(const QString &entryPath, QString *entryName)
{
    QString archivePath;
    QString entry;
    if (!splitEntryPath(entryPath, &archivePath, &entry)) {
        return {};
    }
    if (entryName) {
        *entryName = entry;
    }
    QMutexLocker lock(&s_mutex);
    return s_archives.value(canonicalKey(archivePath));
}

QByteArray ArchiveStore::readEntry(const QString &entryPath, QString *error)
{
    return readEntry(entryPath, error, nullptr);
}

QByteArray ArchiveStore::readEntry(const QString &entryPath, QString *error,
                                   ZipArchive::ReadStatus *status)
{
    if (status) {
        *status = ZipArchive::ReadStatus::Ok;
    }
    QString entryName;
    const auto archive = lookup(entryPath, &entryName);
    if (!archive) {
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ArchiveStore", "压缩包已关闭或未打开"));
        }
        if (status) {
            *status = ZipArchive::ReadStatus::NotOpen;
        }
        return {};
    }
    const int index = archive->indexOf(entryName);
    if (index < 0) {
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ArchiveStore", "包内找不到该文件"));
        }
        if (status) {
            *status = ZipArchive::ReadStatus::NotOpen;
        }
        return {};
    }
    return archive->read(archive->entries().at(index), error, status);
}

qint64 ArchiveStore::entryBytes(const QString &entryPath)
{
    QString entryName;
    const auto archive = lookup(entryPath, &entryName);
    if (!archive) {
        return -1;
    }
    const int index = archive->indexOf(entryName);
    if (index < 0) {
        return -1;
    }
    return qint64(archive->entries().at(index).size);
}

QDateTime ArchiveStore::entryModified(const QString &entryPath)
{
    QString entryName;
    const auto archive = lookup(entryPath, &entryName);
    if (!archive) {
        return QDateTime();
    }
    const int index = archive->indexOf(entryName);
    if (index < 0) {
        return QDateTime();
    }
    return dosToDateTime(archive->entries().at(index).dosDateTime);
}

QImage ArchiveStore::decode(const QString &entryPath, const QSize &scaledTo, QString *error,
                            LoadErrorCategory *category)
{
    if (category) {
        *category = LoadErrorCategory::None;
    }
    QString readError;
    ZipArchive::ReadStatus readStatus = ZipArchive::ReadStatus::Ok;
    const QByteArray data = readEntry(entryPath, &readError, &readStatus);
    if (data.isEmpty()) {
        if (error) {
            *error = readError.isEmpty()
                         ? message(QT_TRANSLATE_NOOP("ArchiveStore", "包内文件解码失败"))
                         : readError;
        }
        if (category) {
            // 加密/超限/压缩方式属于「不支持」，CRC/结构问题属于「损坏」。
            // 判定只看 ZipArchive::ReadStatus，不匹配已本地化的文案。
            switch (readStatus) {
            case ZipArchive::ReadStatus::Encrypted:
            case ZipArchive::ReadStatus::TooLarge:
            case ZipArchive::ReadStatus::UnsupportedMethod:
            case ZipArchive::ReadStatus::NotOpen:
                *category = LoadErrorCategory::Unsupported;
                break;
            default:
                *category = LoadErrorCategory::Corrupt;
                break;
            }
        }
        return QImage();
    }

    QBuffer buffer;
    buffer.setData(data);
    if (!buffer.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = message(QT_TRANSLATE_NOOP("ArchiveStore", "包内文件解码失败"));
        }
        if (category) {
            *category = LoadErrorCategory::Corrupt;
        }
        return QImage();
    }
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (scaledTo.isValid()) {
        QSize source = reader.size();
        if (source.isValid() && source.width() > 0 && source.height() > 0) {
            const QImageIOHandler::Transformations t = reader.transformation();
            if (t.testFlag(QImageIOHandler::TransformationRotate90)
                || t.testFlag(QImageIOHandler::TransformationRotate270)) {
                source.transpose();
            }
            const QSize fit = source.scaled(scaledTo, Qt::KeepAspectRatio);
            if (fit.isValid() && fit.width() > 0 && fit.height() > 0) {
                reader.setScaledSize(fit);
            }
        }
    }
    QImage image = reader.read();
    if (image.isNull()) {
        if (error) {
            *error = reader.errorString().isEmpty()
                         ? message(QT_TRANSLATE_NOOP("ArchiveStore", "包内文件解码失败"))
                         : reader.errorString();
        }
        if (category) {
            *category = LoadErrorCategory::Corrupt;
        }
        return image;
    }
    // 个别解码器（如 PNG）不认 setScaledSize：读完再按比例缩一次，
    // 别把整张原图当缩略图塞进 64MB 的缩略图缓存
    if (scaledTo.isValid()) {
        const QSize fit = image.size().scaled(scaledTo, Qt::KeepAspectRatio);
        if (fit.isValid() && fit != image.size()) {
            image = image.scaled(fit, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }
    return image;
}
