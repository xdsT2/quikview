#include "BatchRename.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace BatchRename {

namespace {

QString formatIndex(int value, int width)
{
    const int w = qBound(1, width, 10);
    if (value < 0) {
        return QStringLiteral("-") + QStringLiteral("%1").arg(-value, w, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1").arg(value, w, 10, QLatin1Char('0'));
}

bool isReservedWindowsName(const QString &stemUpper)
{
    if (stemUpper == QLatin1String("CON") || stemUpper == QLatin1String("PRN")
        || stemUpper == QLatin1String("AUX") || stemUpper == QLatin1String("NUL")) {
        return true;
    }
    if (stemUpper.size() == 4 && (stemUpper.startsWith(QLatin1String("COM"))
                                  || stemUpper.startsWith(QLatin1String("LPT")))) {
        const QChar d = stemUpper.at(3);
        return d >= QLatin1Char('1') && d <= QLatin1Char('9');
    }
    return false;
}

} // namespace

QString expandTemplate(const QString &pattern,
                       const QString &sourcePath,
                       int index,
                       bool keepExtension)
{
    const QFileInfo fi(sourcePath);
    const QString stem = fi.completeBaseName();
    const QString ext = fi.suffix();
    QDateTime dt = fi.lastModified();
    if (!dt.isValid()) {
        dt = QDateTime::currentDateTime();
    }
    const QString dateStr = dt.toString(QStringLiteral("yyyyMMdd"));
    const QString timeStr = dt.toString(QStringLiteral("HHmmss"));

    QString out = pattern;
    out.replace(QStringLiteral("{name}"), stem);
    out.replace(QStringLiteral("{ext}"), ext);
    out.replace(QStringLiteral("{date}"), dateStr);
    out.replace(QStringLiteral("{time}"), timeStr);
    out.replace(QStringLiteral("{index}"), QString::number(index));

    static const QRegularExpression indexRe(QStringLiteral("\\{index:0?(\\d+)\\}"));
    QRegularExpressionMatchIterator it = indexRe.globalMatch(out);
    QList<QRegularExpressionMatch> matches;
    while (it.hasNext()) {
        matches.append(it.next());
    }
    for (int i = matches.size() - 1; i >= 0; --i) {
        const QRegularExpressionMatch &m = matches.at(i);
        const int width = m.captured(1).toInt();
        out.replace(m.capturedStart(), m.capturedLength(), formatIndex(index, width));
    }

    if (keepExtension && !ext.isEmpty() && !pattern.contains(QStringLiteral("{ext}"))) {
        const QString dotExt = QLatin1Char('.') + ext;
        if (!out.endsWith(dotExt, Qt::CaseInsensitive)) {
            out += dotExt;
        }
    }
    return out;
}

QString validateFilename(const QString &fileName)
{
    if (fileName.isEmpty()) {
        return QStringLiteral("文件名不能为空");
    }
    if (fileName == QLatin1String(".") || fileName == QLatin1String("..")) {
        return QStringLiteral("不能使用 . 或 .. 作为文件名");
    }
    if (fileName.endsWith(QLatin1Char(' ')) || fileName.endsWith(QLatin1Char('.'))) {
        return QStringLiteral("文件名不能以空格或句点结尾");
    }
    for (const QChar ch : fileName) {
        const ushort u = ch.unicode();
        if (u < 0x20
            || ch == QLatin1Char('\\') || ch == QLatin1Char('/')
            || ch == QLatin1Char(':') || ch == QLatin1Char('*')
            || ch == QLatin1Char('?') || ch == QLatin1Char('"')
            || ch == QLatin1Char('<') || ch == QLatin1Char('>')
            || ch == QLatin1Char('|')) {
            return QStringLiteral("包含非法字符：%1").arg(ch);
        }
    }
    const int dot = fileName.indexOf(QLatin1Char('.'));
    const QString stem = (dot >= 0 ? fileName.left(dot) : fileName).toUpper();
    if (isReservedWindowsName(stem)) {
        return QStringLiteral("保留设备名不可用：%1").arg(stem);
    }
    return QString();
}

Plan buildPlan(const QStringList &sourcePaths, const Options &opt)
{
    Plan plan;
    plan.items.reserve(sourcePaths.size());

    QSet<QString> sourceKeys;
    for (const QString &src : sourcePaths) {
        const QFileInfo fi(src);
        sourceKeys.insert(QDir::cleanPath(fi.absoluteFilePath()).toLower());
    }

    QHash<QString, QList<int>> targetBuckets;
    for (int i = 0; i < sourcePaths.size(); ++i) {
        const QString &src = sourcePaths.at(i);
        const QFileInfo fi(src);
        Item item;
        item.sourcePath = fi.absoluteFilePath();
        item.oldName = fi.fileName();
        item.newName = expandTemplate(opt.pattern, src, opt.startIndex + i, opt.keepExtension);
        item.targetPath = QDir(fi.absolutePath()).filePath(item.newName);
        item.unchanged = (QDir::cleanPath(item.targetPath).compare(
                              QDir::cleanPath(item.sourcePath), Qt::CaseInsensitive) == 0);
        item.error = validateFilename(item.newName);
        if (item.error.isEmpty()) {
            const QString key = QDir::cleanPath(item.targetPath).toLower();
            targetBuckets[key].append(i);
        }
        plan.items.append(item);
    }

    // 批内重名冲突（含与未改名条目撞车）
    for (auto it = targetBuckets.constBegin(); it != targetBuckets.constEnd(); ++it) {
        if (it.value().size() <= 1) {
            continue;
        }
        for (const int idx : it.value()) {
            plan.items[idx].error = QStringLiteral("批内目标文件名冲突");
        }
    }

    // 与磁盘上非本批源文件的已有文件撞车
    for (int i = 0; i < plan.items.size(); ++i) {
        Item &item = plan.items[i];
        if (!item.error.isEmpty() || item.unchanged) {
            continue;
        }
        const QString targetKey = QDir::cleanPath(item.targetPath).toLower();
        if (!sourceKeys.contains(targetKey) && QFileInfo::exists(item.targetPath)) {
            item.error = QStringLiteral("目标文件已存在于目录中");
        }
    }

    for (const Item &item : plan.items) {
        if (!item.error.isEmpty()) {
            ++plan.conflictCount;
        } else if (!item.unchanged) {
            ++plan.changeCount;
        }
    }
    return plan;
}

ExecuteResult executePlan(const Plan &plan)
{
    ExecuteResult res;
    if (plan.hasConflicts()) {
        res.error = QStringLiteral("存在 %1 处冲突，已阻止执行").arg(plan.conflictCount);
        return res;
    }

    struct Pending {
        QString src;
        QString tmp;
        QString dst;
    };
    QVector<Pending> jobs;
    const QString tag = QUuid::createUuid().toString(QUuid::Id128).left(8);

    for (int i = 0; i < plan.items.size(); ++i) {
        const Item &it = plan.items.at(i);
        if (it.unchanged) {
            continue;
        }
        if (!QFileInfo::exists(it.sourcePath)) {
            res.error = QStringLiteral("源文件不存在：%1").arg(it.oldName);
            return res;
        }
        const QFileInfo fi(it.sourcePath);
        QString tmpPath;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const QString cand = QDir(fi.absolutePath()).filePath(
                QStringLiteral(".qv-ren-%1-%2-%3.tmp").arg(tag).arg(i).arg(attempt));
            if (!QFileInfo::exists(cand)) {
                tmpPath = cand;
                break;
            }
        }
        if (tmpPath.isEmpty()) {
            res.error = QStringLiteral("无法生成唯一临时文件名");
            return res;
        }
        jobs.append({it.sourcePath, tmpPath, it.targetPath});
    }

    if (jobs.isEmpty()) {
        res.ok = true;
        return res;
    }

    // 阶段 1：全部迁入临时名，避免 a->b / b->a 环形重命名互相覆盖。
    QVector<Pending> phase1Done;
    for (const Pending &job : jobs) {
        if (!QFile::rename(job.src, job.tmp)) {
            for (int r = phase1Done.size() - 1; r >= 0; --r) {
                QFile::rename(phase1Done[r].tmp, phase1Done[r].src);
            }
            res.error = QStringLiteral("重命名失败（可能被占用或无权限）：%1").arg(job.src);
            return res;
        }
        phase1Done.append(job);
    }

    // 阶段 2：从临时名落入最终目标名；失败时逆序恢复阶段 2 和阶段 1。
    QVector<Pending> phase2Done;
    for (int i = 0; i < jobs.size(); ++i) {
        const Pending &job = jobs.at(i);
        if (!QFile::rename(job.tmp, job.dst)) {
            for (int r = phase2Done.size() - 1; r >= 0; --r) {
                QFile::rename(phase2Done[r].dst, phase2Done[r].tmp);
            }
            for (int r = phase1Done.size() - 1; r >= 0; --r) {
                QFile::rename(phase1Done[r].tmp, phase1Done[r].src);
            }
            res.error = QStringLiteral("写入目标文件失败：%1").arg(job.dst);
            return res;
        }
        phase2Done.append(job);
        res.pathMap.insert(job.src, job.dst);
    }

    res.ok = true;
    res.renamedCount = phase2Done.size();
    return res;
}

} // namespace BatchRename
