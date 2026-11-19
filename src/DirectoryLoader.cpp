#include "DirectoryLoader.h"

#include "ArchiveStore.h"
#include "Config.h"
#include "PerfProbe.h"
#include "ZipArchive.h"

#include <QCollator>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <vector>

namespace {

struct ImageGroup
{
    const char *label;              // 文件对话框里的分组标题
    const char *const *exts;        // 不带 "*."，全小写
    int count;
};

// 解码白名单唯一事实来源。目录扫描（imageExtensions）与文件对话框过滤器
// （imageFilter）都从这张表派生，别处不许再写一份扩展名列表。
//
// 依据 Qt 6.11.2 实测 supportedImageFormats()：tga / icns / mng /
// wbmp / cur / jfif 本来就能解码，此前只是没列进来，用户才打不开。
// 分组只影响对话框展示顺序，不影响扫描结果。
// jp2/j2k 已移除（2026-10-25）：Qt 的 openjpeg 插件对常见 JP2 报
// component data type mismatch，Jasper 能读的文件现实中基本不存在——
// 与动 WebP 同一原则：做不好就不让用户碰到。插件保留，将来换解码器可直接恢复。
const char *const kCommonExts[] = { "png", "jpg", "jpeg", "jfif", "bmp", "gif",
                                    "webp", "tif", "tiff" };
const char *const kIconExts[]   = { "ico", "cur", "icns" };
const char *const kOtherExts[]  = { "tga", "mng", "wbmp",
                                    "ppm", "pgm", "pbm", "xbm", "xpm", "psd" };
const char *const kCameraExts[] = { "heic", "heif", "hif", "avif",
                                    "dng", "nef", "cr2", "arw",
                                    "rw2", "orf", "raf", "pef", "raw" };

const ImageGroup kImageGroups[] = {
    { QT_TRANSLATE_NOOP("DirectoryLoader", "常用图片"), kCommonExts,
      int(sizeof(kCommonExts) / sizeof(kCommonExts[0])) },
    { QT_TRANSLATE_NOOP("DirectoryLoader", "图标与光标"), kIconExts,
      int(sizeof(kIconExts) / sizeof(kIconExts[0])) },
    { QT_TRANSLATE_NOOP("DirectoryLoader", "其他图片"), kOtherExts,
      int(sizeof(kOtherExts) / sizeof(kOtherExts[0])) },
    { QT_TRANSLATE_NOOP("DirectoryLoader", "相机与无损"), kCameraExts,
      int(sizeof(kCameraExts) / sizeof(kCameraExts[0])) },
};

QStringList groupPatterns(const ImageGroup &g)
{
    QStringList out;
    out.reserve(g.count);
    for (int i = 0; i < g.count; ++i) {
        out << QStringLiteral("*.") + QLatin1String(g.exts[i]);
    }
    return out;
}

// 包内可解码的格式：同一张表派生，但去掉「必须走文件级 loader」的几类——
// PSD 走 PsdLoader（从磁盘读文件）、相机/HEIF 格式走 libraw / libheif（同样
// 只接受文件路径）。它们在压缩包里拿不到 QIODevice，列出来只会变成「打不开」。
bool isArchiveDecodableExt(const QString &ext)
{
    static const QSet<QString> kFileOnly = {
        QStringLiteral("psd"), QStringLiteral("heic"), QStringLiteral("heif"),
        QStringLiteral("hif"), QStringLiteral("avif"), QStringLiteral("dng"),
        QStringLiteral("nef"), QStringLiteral("cr2"), QStringLiteral("arw"),
        QStringLiteral("rw2"), QStringLiteral("orf"), QStringLiteral("raf"),
        QStringLiteral("pef"), QStringLiteral("raw")
    };
    return !kFileOnly.contains(ext);
}

// 包内图片扩展名（不带 "*."），顺序跟随 kImageGroups。
QStringList archiveImageExts()
{
    QStringList out;
    for (const ImageGroup &g : kImageGroups) {
        for (int i = 0; i < g.count; ++i) {
            const QString ext = QLatin1String(g.exts[i]);
            if (isArchiveDecodableExt(ext)) {
                out << ext;
            }
        }
    }
    return out;
}

}   // namespace

QStringList DirectoryLoader::imageExtensions()
{
    QStringList out;
    for (const ImageGroup &g : kImageGroups) {
        out << groupPatterns(g);
    }
    return out;
}

QString DirectoryLoader::imageFilter()
{
    QStringList entries;
    // QFileDialog 默认选中第一个过滤器。把「所有图片」放在首位，避免 PSD、
    // RAW 等已支持格式因为落在类型分组里而在打开窗口中被默认隐藏。
    // 2026-11-19：首项同时收 *.zip / *.cbz，压缩包不必切到「所有文件」才看得见。
    QStringList allPatterns = imageExtensions();
    allPatterns << QStringLiteral("*.zip") << QStringLiteral("*.cbz");
    entries << QStringLiteral("%1 (%2)")
                   .arg(QCoreApplication::translate("DirectoryLoader", "图片与压缩包"),
                        allPatterns.join(QLatin1Char(' ')));
    entries << QStringLiteral("%1 (%2)")
                   .arg(QCoreApplication::translate("DirectoryLoader", "压缩包"),
                        QStringLiteral("*.zip *.cbz"));
    for (const ImageGroup &g : kImageGroups) {
        entries << QStringLiteral("%1 (%2)")
                       .arg(QCoreApplication::translate("DirectoryLoader", g.label),
                            groupPatterns(g).join(QLatin1Char(' ')));
    }
    entries << QCoreApplication::translate("DirectoryLoader", "所有文件 (*)");
    return entries.join(QStringLiteral(";;"));
}

QList<DirectoryLoader::Group> DirectoryLoader::imageGroups()
{
    QList<Group> out;
    out.reserve(int(sizeof(kImageGroups) / sizeof(kImageGroups[0])));
    for (const ImageGroup &g : kImageGroups) {
        Group item;
        item.label = QCoreApplication::translate("DirectoryLoader", g.label);
        item.exts.reserve(g.count);
        for (int i = 0; i < g.count; ++i) {
            item.exts << QLatin1String(g.exts[i]);
        }
        out.append(item);
    }
    return out;
}

// 后台可调用的纯函数：整个扫描/排序/过滤过程不触碰 DirectoryLoader 成员。
// 注意：QCollator、QFileInfo 都是可重入的，工作线程内自建即用是安全的。
DirectoryLoader::Index DirectoryLoader::buildIndex(const QString &filePath, int sort,
                                                   bool sortDesc, bool skipRawWhenJpg)
{
    Index out;
    const QFileInfo fi(filePath);
    if (!fi.exists()) {
        return out;
    }
    out.found = true;
    const QDir dir = fi.dir();
    PerfProbe::mark(PerfProbe::currentSeq(), "scan-start", "");
    QFileInfoList infos = dir.entryInfoList(imageExtensions(),
                                             QDir::Files | QDir::Readable);
    PerfProbe::mark(PerfProbe::currentSeq(), "scan-done",
                    QByteArray::number(infos.size()).constData());

    // 排序：键（名称/时间/大小/类型）+ 方向（升/降），对照原版排序方式 4+1 项。
    // 名称用 QCollator（LocaleAware）；类型 = 扩展名分组、组内按名称。
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const bool asc = !sortDesc;
    std::sort(infos.begin(), infos.end(),
              [sort, &collator, asc](const QFileInfo &a, const QFileInfo &b) {
        int r = 0;
        switch (sort) {
        case 1:   // 修改时间
            r = (a.lastModified() == b.lastModified()) ? 0
                : (a.lastModified() < b.lastModified() ? -1 : 1);
            break;
        case 2:   // 大小
            r = (a.size() == b.size()) ? 0 : (a.size() < b.size() ? -1 : 1);
            break;
        case 3: { // 类型：扩展名分组，组内名称
            r = a.suffix().compare(b.suffix(), Qt::CaseInsensitive);
            if (r == 0) {
                r = collator.compare(a.fileName(), b.fileName());
            }
            break;
        }
        default:  // 名称
            r = collator.compare(a.fileName(), b.fileName());
            break;
        }
        return asc ? (r < 0) : (r > 0);
    });
    PerfProbe::mark(PerfProbe::currentSeq(), "sort-done", "");

    const QString canonical = fi.absoluteFilePath();

    // 「打开 JPEG 时不同时打开同名 RAW」（键 "config/etc/skip_raw_when_jpg"）：
    // a.jpg + a.nef 同目录时只留 JPEG。仅在当前打开的是 JPEG 时过滤；直接打开
    // .nef 时保留全量列表，否则用户双击 RAW 会被自己的过滤规则挡在门外。
    if (skipRawWhenJpg) {
        const QString curSuffix = fi.suffix().toLower();
        const bool curIsJpg = (curSuffix == QLatin1String("jpg")
                               || curSuffix == QLatin1String("jpeg")
                               || curSuffix == QLatin1String("jfif"));
        if (curIsJpg) {
            QSet<QString> jpgStems;
            for (const QFileInfo &info : infos) {
                const QString s = info.suffix().toLower();
                if (s == QLatin1String("jpg") || s == QLatin1String("jpeg")
                    || s == QLatin1String("jfif")) {
                    jpgStems.insert(info.completeBaseName().toLower());
                }
            }
            static const QStringList kRawExts = {
                QStringLiteral("dng"), QStringLiteral("nef"), QStringLiteral("cr2"),
                QStringLiteral("arw"), QStringLiteral("rw2"), QStringLiteral("orf"),
                QStringLiteral("raf"), QStringLiteral("pef"), QStringLiteral("raw")
            };
            infos.erase(std::remove_if(infos.begin(), infos.end(),
                                       [&jpgStems](const QFileInfo &info) {
                const QString s = info.suffix().toLower();
                return kRawExts.contains(s)
                       && jpgStems.contains(info.completeBaseName().toLower());
            }), infos.end());
        }
    }

    PerfProbe::mark(PerfProbe::currentSeq(), "filter-done", "");
    out.files.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        out.files << info.absoluteFilePath();
    }
    out.anchor = out.files.indexOf(canonical);
    return out;
}

bool DirectoryLoader::applyIndex(const Index &idx)
{
    m_files = idx.files;
    m_index = idx.anchor;
    return m_index >= 0;
}

bool DirectoryLoader::open(const QString &filePath)
{
    const Index idx = buildIndex(filePath, m_sort, m_sortDesc,
                                 Config::instance().skipRawWhenJpg());
    return applyIndex(idx);
}

QString DirectoryLoader::current() const
{
    return isValid() ? m_files.at(m_index) : QString();
}

QString DirectoryLoader::next()
{
    if (m_files.isEmpty()) {
        return QString();
    }
    if (m_edgeAction == QLatin1String("stop") && m_index >= m_files.size() - 1) {
        return QString();
    }
    m_index = (m_index + 1) % m_files.size();
    return m_files.at(m_index);
}

// 一次走 step 张：双页阅览一跨页是 2 张，普通浏览是 1 张。
// 到边界按 edgeAction 处理——stop 原地不动并返回空串（调用方据此提示），
// first 回绕到另一端（保留原有循环语义）。
QString DirectoryLoader::advance(int step)
{
    if (m_files.isEmpty() || step == 0) {
        return QString();
    }
    const int target = m_index + step;
    if (target < 0 || target >= m_files.size()) {
        if (m_edgeAction != QLatin1String("first")) {
            return QString();
        }
        m_index = target < 0 ? m_files.size() - 1 : 0;
    } else {
        m_index = target;
    }
    return m_files.at(m_index);
}

QString DirectoryLoader::prev()
{
    if (m_files.isEmpty()) {        return QString();
    }
    if (m_edgeAction == QLatin1String("stop") && m_index <= 0) {
        return QString();
    }
    m_index = (m_index - 1 + m_files.size()) % m_files.size();
    return m_files.at(m_index);
}

// 10 张跳转：边界 clamp 停住（原版 CMD_PREV10/NEXT10 语义），不回绕
QString DirectoryLoader::next10()
{
    if (m_files.isEmpty() || m_index >= m_files.size() - 1) {
        return QString();
    }
    m_index = qMin(m_index + 10, m_files.size() - 1);
    return m_files.at(m_index);
}

QString DirectoryLoader::prev10()
{
    if (m_files.isEmpty() || m_index <= 0) {
        return QString();
    }
    m_index = qMax(m_index - 10, 0);
    return m_files.at(m_index);
}

// 压缩包「虚拟目录」：条目清单来自 ZipArchive 的中央目录，排序在内存里做，
// 不解压任何一张图（派单 §三：5000 条目包也不预读）。
DirectoryLoader::Index DirectoryLoader::buildIndexArchive(const QString &archivePath,
                                                          const QString &anchorEntry,
                                                          int sort, bool sortDesc)
{
    Index out;
    PerfProbe::mark(PerfProbe::currentSeq(), "scan-start", "archive");

    QString openError;
    ZipArchive::Status status = ZipArchive::Status::Ok;
    const QSharedPointer<ZipArchive> archive = ArchiveStore::open(archivePath, &openError, &status);
    if (!archive) {
        out.archiveStatus = int(status);
        out.archiveError = openError;
        return out;
    }
    out.archiveStatus = int(ZipArchive::Status::Ok);

    const QStringList exts = archiveImageExts();
    QVector<const ZipArchive::Entry *> list;
    list.reserve(archive->entries().size());
    int encryptedImages = 0;
    for (const ZipArchive::Entry &entry : archive->entries()) {
        if (entry.isDir || entry.name.isEmpty()) {
            continue;
        }
        // macOS 打包产生的资源分叉，不是正片
        if (entry.name.startsWith(QLatin1String("__MACOSX/"), Qt::CaseInsensitive)
            || entry.name.endsWith(QLatin1String(".DS_Store"), Qt::CaseInsensitive)) {
            continue;
        }
        const int dot = entry.name.lastIndexOf(QLatin1Char('.'));
        if (dot < 0) {
            continue;
        }
        const QString suffix = entry.name.mid(dot + 1).toLower();
        if (!exts.contains(suffix)) {
            continue;
        }
        if (entry.encrypted) {
            // 不解密：加密条目直接不出现在可浏览序列里（派单 §二.4）
            ++encryptedImages;
            continue;
        }
        list.append(&entry);
    }
    if (list.isEmpty()) {
        out.archiveError = encryptedImages > 0
                               ? QCoreApplication::translate("DirectoryLoader",
                                                             "暂不支持加密压缩包")
                               : QCoreApplication::translate("DirectoryLoader",
                                                             "包内没有可显示的图片");
        out.archiveStatus = encryptedImages > 0 ? int(ZipArchive::Status::Encrypted)
                                                : int(ZipArchive::Status::Ok);
        return out;
    }
    PerfProbe::mark(PerfProbe::currentSeq(), "scan-done",
                    QByteArray::number(list.size()).constData());

    // 自然排序：QCollator 打开数字模式，page-2 排在 page-10 前面（派单 §五.1）
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    auto suffixOf = [](const ZipArchive::Entry *e) {
        const int dot = e->name.lastIndexOf(QLatin1Char('.'));
        return dot < 0 ? QString() : e->name.mid(dot + 1).toLower();
    };
    const bool asc = !sortDesc;
    std::sort(list.begin(), list.end(),
              [sort, &collator, &suffixOf, asc](const ZipArchive::Entry *a,
                                                const ZipArchive::Entry *b) {
        int r = 0;
        switch (sort) {
        case 1:   // 修改时间（zip 里存的是 DOS 时间，大小比较即可）
            r = (a->dosDateTime == b->dosDateTime)
                    ? 0
                    : (a->dosDateTime < b->dosDateTime ? -1 : 1);
            break;
        case 2:   // 大小
            r = (a->size == b->size) ? 0 : (a->size < b->size ? -1 : 1);
            break;
        case 3: { // 类型：先扩展名，再名称
            r = suffixOf(a).compare(suffixOf(b), Qt::CaseInsensitive);
            if (r == 0) {
                r = collator.compare(a->name, b->name);
            }
            break;
        }
        default:  // 名称
            r = collator.compare(a->name, b->name);
            break;
        }
        return asc ? (r < 0) : (r > 0);
    });
    PerfProbe::mark(PerfProbe::currentSeq(), "sort-done", "");

    const QString prefix = ArchiveStore::entryPathPrefix(archivePath);
    out.files.reserve(list.size());
    for (const ZipArchive::Entry *entry : list) {
        out.files << prefix + entry->name;
    }
    if (!anchorEntry.isEmpty()) {
        out.anchor = int(out.files.indexOf(prefix + anchorEntry));
        if (out.anchor < 0) {
            out.anchor = 0;   // 锚点不在包内（比如双击的是已被删掉的条目）：落到第一张
        }
    } else {
        out.anchor = 0;
    }
    out.found = !out.files.isEmpty();
    PerfProbe::mark(PerfProbe::currentSeq(), "filter-done", "");
    return out;
}
