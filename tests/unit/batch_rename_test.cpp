// BatchRename 纯逻辑无窗口单测（A1：模板变量、合法性校验、批内/磁盘冲突全量拒绝、环形安全执行）。
#include "BatchRename.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static bool touchFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    return f.write(data) == data.size();
}

static QByteArray readAllBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return f.readAll();
}

int main()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString a = tmp.filePath("alpha.png");
    const QString b = tmp.filePath("beta.png");
    const QString c = tmp.filePath("gamma.jpg");
    CHECK(touchFile(a, "A"));
    CHECK(touchFile(b, "B"));
    CHECK(touchFile(c, "C"));

    // 1. 模板展开：{name}/{index:03}/{date}/{time}/{ext} 与自动保留扩展名
    {
        const QString r1 = BatchRename::expandTemplate(
            QStringLiteral("{name}_{index:03}"), a, 7, true);
        CHECK(r1 == QLatin1String("alpha_007.png"));

        const QString r2 = BatchRename::expandTemplate(
            QStringLiteral("shot_{index}.{ext}"), c, 12, true);
        CHECK(r2 == QLatin1String("shot_12.jpg"));

        const QString r3 = BatchRename::expandTemplate(
            QStringLiteral("{date}_{time}_{name}"), a, 1, true);
        CHECK(r3.endsWith(QLatin1String("_alpha.png")));
        CHECK(r3.size() >= QStringLiteral("20260101_000000_alpha.png").size());
    }

    // 2. 文件名合法性校验
    {
        CHECK(!BatchRename::validateFilename(QString()).isEmpty());
        CHECK(!BatchRename::validateFilename(QStringLiteral("bad:name.png")).isEmpty());
        CHECK(!BatchRename::validateFilename(QStringLiteral("CON.png")).isEmpty());
        CHECK(!BatchRename::validateFilename(QStringLiteral("taildot.")).isEmpty());
        CHECK(BatchRename::validateFilename(QStringLiteral("good_001.png")).isEmpty());
    }

    // 3. 批内撞名冲突：整批阻止且不允许落盘修改
    {
        BatchRename::Options opt;
        opt.pattern = QStringLiteral("same_name");
        opt.keepExtension = true;
        const auto plan = BatchRename::buildPlan({a, b}, opt);
        CHECK(plan.hasConflicts());
        CHECK(plan.conflictCount == 2);
        const auto res = BatchRename::executePlan(plan);
        CHECK(!res.ok);
        CHECK(QFileInfo::exists(a));
        CHECK(QFileInfo::exists(b));
    }

    // 4. 与目录内未参与批处理的已有文件撞车：阻止
    {
        BatchRename::Options opt;
        opt.pattern = QStringLiteral("gamma.jpg");
        opt.keepExtension = false;
        const auto plan = BatchRename::buildPlan({a}, opt);
        CHECK(plan.hasConflicts());
        CHECK(plan.conflictCount == 1);
    }

    // 5. 环形重命名（alpha.png <-> beta.png）：合法、无冲突、内容交换正确
    {
        BatchRename::Plan plan;
        BatchRename::Item it1;
        it1.sourcePath = a;
        it1.oldName = QStringLiteral("alpha.png");
        it1.newName = QStringLiteral("beta.png");
        it1.targetPath = b;
        BatchRename::Item it2;
        it2.sourcePath = b;
        it2.oldName = QStringLiteral("beta.png");
        it2.newName = QStringLiteral("alpha.png");
        it2.targetPath = a;
        plan.items = {it1, it2};
        plan.changeCount = 2;
        plan.conflictCount = 0;

        const auto res = BatchRename::executePlan(plan);
        CHECK(res.ok);
        CHECK(res.renamedCount == 2);
        CHECK(readAllBytes(a) == "B");
        CHECK(readAllBytes(b) == "A");
    }

    // 6. 正常批量重命名（a, b -> img_001.png, img_002.png）
    {
        BatchRename::Options opt;
        opt.pattern = QStringLiteral("img_{index:03}");
        opt.startIndex = 1;
        opt.keepExtension = true;
        const auto plan = BatchRename::buildPlan({a, b}, opt);
        CHECK(!plan.hasConflicts());
        CHECK(plan.changeCount == 2);
        const auto res = BatchRename::executePlan(plan);
        CHECK(res.ok);
        CHECK(res.renamedCount == 2);
        CHECK(QFileInfo::exists(tmp.filePath("img_001.png")));
        CHECK(QFileInfo::exists(tmp.filePath("img_002.png")));
        CHECK(!QFileInfo::exists(a));
        CHECK(!QFileInfo::exists(b));
    }

    if (g_failures == 0) {
        std::printf("batchrename.pure: PASS\n");
        return 0;
    }
    std::printf("batchrename.pure: FAIL (%d)\n", g_failures);
    return 1;
}
