// OpenPicker 纯函数无窗口单测（2026-11-25 原生对话框替换单）。
// 只覆盖 buildFilters / resolveFolderTarget 两个不依赖 COM 与窗口的纯函数；
// 原生 IFileOpenDialog 交互不在无环境下伪造覆盖。
#include "OpenPicker.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <cstdio>
#include <cstdlib>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

int main()
{
    // —— buildFilters：去重、保序、压缩包追加到首项通配串 ——
    {
        const QStringList patterns = { "*.jpg", "*.png", "*.jpg", "*.webp" };
        const QStringList archives = { "*.zip", "*.cbz" };
        const auto filters = OpenPicker::buildFilters(patterns, archives);
        CHECK(filters.size() == 1);
        CHECK(filters[0].spec == QLatin1String("*.jpg;*.png;*.webp;*.zip;*.cbz"));
        CHECK(filters[0].label.contains(QLatin1String("*.jpg")));
        CHECK(filters[0].label.contains(QLatin1String("*.cbz")));
    }

    // 空表 → 空筛选，不出空字符串通配项。
    {
        const auto filters = OpenPicker::buildFilters({}, {});
        CHECK(filters.isEmpty());
    }

    // 无压缩包时只收图片表。
    {
        const auto filters = OpenPicker::buildFilters({ "*.psd" }, {});
        CHECK(filters.size() == 1);
        CHECK(filters[0].spec == QLatin1String("*.psd"));
    }

    // —— resolveFolderTarget：选中目录用选中项；文件/空选中回退当前目录 ——
    {
        const QString tempDir = QDir::tempPath();
        // 现有目录 → 目录绝对路径
        const QString dirHit = OpenPicker::resolveFolderTarget(tempDir, QDir::rootPath());
        CHECK(QFileInfo(dirHit).isDir());
        CHECK(QDir(dirHit).absolutePath() == QDir(tempDir).absolutePath());

        // 不存在的文件路径 → 回退当前目录
        const QString fileMiss = OpenPicker::resolveFolderTarget(
            QDir(tempDir).absoluteFilePath("no-such-file-xyz.png"), tempDir);
        CHECK(fileMiss == QDir(tempDir).absolutePath());

        // 空选中 → 回退当前目录
        const QString emptySel = OpenPicker::resolveFolderTarget(QString(), tempDir);
        CHECK(emptySel == QDir(tempDir).absolutePath());

        // 当前目录也为空 → 空串，不抛错
        const QString bothEmpty = OpenPicker::resolveFolderTarget(QString(), QString());
        CHECK(bothEmpty.isEmpty());
    }

    if (g_failures == 0) {
        std::printf("openpicker.pure: PASS\n");
        return 0;
    }
    std::printf("openpicker.pure: FAIL (%d)\n", g_failures);
    return 1;
}
