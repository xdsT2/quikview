#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

// 文件 / 批量重命名核心（A1，纯 QtCore，无 UI 依赖）：
//   - 模板变量：{name} / {index} / {index:0N} / {date} / {time} / {ext}
//   - 冲突全量预检：非法字符、Windows 保留名、批内重复（忽略大小写）、与目录内未参与
//     重命名的已有文件碰撞。发现任意冲突即整批阻止。
//   - 环形安全执行：两阶段临时名重命名，中途失败自动逆序回滚。
namespace BatchRename {

struct Options {
    QString pattern = QStringLiteral("{name}");
    int startIndex = 1;
    bool keepExtension = true;
};

struct Item {
    QString sourcePath;
    QString oldName;
    QString newName;
    QString targetPath;
    bool unchanged = false;
    QString error;
};

struct Plan {
    QVector<Item> items;
    int changeCount = 0;
    int conflictCount = 0;

    bool hasConflicts() const { return conflictCount > 0; }
};

struct ExecuteResult {
    bool ok = false;
    int renamedCount = 0;
    QHash<QString, QString> pathMap;   // oldPath -> newPath（含未改名条目以外的全部改动）
    QString error;
};

QString expandTemplate(const QString &pattern,
                       const QString &sourcePath,
                       int index,
                       bool keepExtension);

QString validateFilename(const QString &fileName);

Plan buildPlan(const QStringList &sourcePaths, const Options &opt);

ExecuteResult executePlan(const Plan &plan);

} // namespace BatchRename
