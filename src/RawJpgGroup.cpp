#include "RawJpgGroup.h"

#include "RawImageLoader.h"

#include <QFileInfo>
#include <QHash>
#include <QVector>

namespace RawJpgGroup {

namespace {

// 原始组扩展名（与 RawImageLoader::rawFileExtensions() 保持一致，
// 外加 PSD/PSB：BandiView 把 PSD+JPG 当同一个场景）。
bool rawSideSuffix(const QString &s)
{
    return s == QLatin1String("psd") || s == QLatin1String("psb")
           || rawFileExtensions().contains(s);
}

bool jpgSideSuffix(const QString &s)
{
    return s == QLatin1String("jpg") || s == QLatin1String("jpeg")
           || s == QLatin1String("jpe") || s == QLatin1String("jif")
           || s == QLatin1String("jfi") || s == QLatin1String("jfif");
}

QString suffixLower(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

QString stemLower(const QString &path)
{
    return QFileInfo(path).completeBaseName().toLower();
}

} // namespace

bool isRawSideExt(const QString &suffixLower)
{
    return rawSideSuffix(suffixLower);
}

bool isJpgSideExt(const QString &suffixLower)
{
    return jpgSideSuffix(suffixLower);
}

QString Entry::displayPath(bool showRaw) const
{
    if (!grouped) {
        return members.value(0);
    }
    if (showRaw && !raw.isEmpty()) {
        return raw;
    }
    return jpg.isEmpty() ? raw : jpg;
}

bool Entry::contains(const QString &path) const
{
    for (const QString &m : members) {
        if (m.compare(path, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

int indexOfPath(const QList<Entry> &entries, const QString &path)
{
    if (path.isEmpty()) {
        return -1;
    }
    for (int i = 0; i < entries.size(); ++i) {
        if (entries.at(i).contains(path)) {
            return i;
        }
    }
    return -1;
}

QStringList displayFiles(const QList<Entry> &entries, bool showRaw)
{
    QStringList out;
    out.reserve(entries.size());
    for (const Entry &e : entries) {
        out << e.displayPath(showRaw);
    }
    return out;
}

QList<Entry> build(const QStringList &sortedFiles, bool showRaw)
{
    // 主名 → 成员下标。同一主名可能跨扩展名多份（cr2 + jpg + psd），
    // 目录内同名不同扩展正是本单要合并的对象。
    QHash<QString, QVector<int>> buckets;
    for (int i = 0; i < sortedFiles.size(); ++i) {
        buckets[stemLower(sortedFiles.at(i))].append(i);
    }

    // 每个桶挑出 JPG 侧与原始侧的首个成员（输入已排序，首个即排序键）。
    struct BucketInfo
    {
        int jpg = -1;
        int raw = -1;
        bool grouped = false;
        int anchor = -1;
    };
    QHash<QString, BucketInfo> info;
    for (auto it = buckets.constBegin(); it != buckets.constEnd(); ++it) {
        BucketInfo b;
        for (int idx : it.value()) {
            const QString s = suffixLower(sortedFiles.at(idx));
            if (b.jpg < 0 && jpgSideSuffix(s)) {
                b.jpg = idx;
            } else if (b.raw < 0 && rawSideSuffix(s)) {
                b.raw = idx;
            }
        }
        b.grouped = (b.jpg >= 0 && b.raw >= 0);
        // 位次：取 JPG 的排序键位置；没有 JPG 的桶不会成组。
        b.anchor = b.grouped ? b.jpg : -1;
        info.insert(it.key(), b);
    }

    QList<Entry> out;
    out.reserve(sortedFiles.size());
    for (int i = 0; i < sortedFiles.size(); ++i) {
        const QString stem = stemLower(sortedFiles.at(i));
        const BucketInfo b = info.value(stem);

        if (!b.grouped) {
            // 普通单条：与未分组时的列表逐位一致。
            Entry e;
            e.members << sortedFiles.at(i);
            e.grouped = false;
            out.append(e);
            continue;
        }
        if (i != b.anchor) {
            continue;   // 组内其它成员被折叠，只在锚点位置出一次
        }
        Entry e;
        e.grouped = true;
        e.jpg = sortedFiles.at(b.jpg);
        e.raw = sortedFiles.at(b.raw);
        for (int idx : buckets.value(stem)) {
            e.members << sortedFiles.at(idx);
        }
        e.members.removeDuplicates();
        out.append(e);
    }
    return out;
}

} // namespace RawJpgGroup
