// ArchiveKinds 纯逻辑单测（I4-1：ZIP 容器通用抽图的后缀判定表）。
// 只用 QString，不加载图像插件，因此不需要 QGuiApplication。
#include "ArchiveKinds.h"

#include <QSet>
#include <QStringList>
#include <QStringView>

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

// 带上下文打印：失败时直接看出是哪个后缀出的问题，不必回去猜断言行号。
// 用 fromLatin1 而不是 QStringLiteral：本宏既要吃字面量，也要吃循环里的
// const char* 变量，QStringLiteral 只接受字面量，编译期会直接报错。
#define CHECK_SUFFIX(suffix, expected) \
    do { \
        const bool actual = ArchiveKinds::isOpenableSuffix(QString::fromLatin1(suffix)); \
        if (actual != (expected)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: isOpenableSuffix(\"%s\") = %d, want %d\n", \
                         __FILE__, __LINE__, suffix, int(actual), int(expected)); \
        } \
    } while (0)

int main()
{
    // 1. 各类容器后缀都能识别。
    {
        const char *const yes[] = { "zip", "cbz", "docx", "pptx", "xlsx",
                                    "epub", "apk", "jar" };
        for (const char *s : yes) {
            CHECK_SUFFIX(s, true);
        }
    }

    // 2. 大小写与前缀容错：DOCX / .Docx / *.docx 都应命中。
    {
        CHECK_SUFFIX("DOCX", true);
        CHECK_SUFFIX(".Docx", true);
        CHECK_SUFFIX("*.docx", true);
        CHECK_SUFFIX("*.EPUB", true);
        CHECK_SUFFIX("  apk  ", true);
    }

    // 3. 非容器后缀一律不认（doc/pdf 是非 zip 容器，jpg/png 是图片本身）。
    {
        const char *const no[] = { "doc", "pdf", "jpg", "png", "txt", "", "docxx" };
        for (const char *s : no) {
            CHECK_SUFFIX(s, false);
        }
    }

    // 4. 后缀表本身的卫生：无重复、全小写、不含点、覆盖关键项、数量够。
    {
        const QStringList suffixes = ArchiveKinds::openableSuffixes();
        QSet<QString> seen;
        for (const QString &suffix : suffixes) {
            if (seen.contains(suffix)) {
                ++g_failures;
                std::fprintf(stderr, "FAIL duplicate suffix: %s\n", qPrintable(suffix));
            }
            seen.insert(suffix);
            if (suffix != suffix.toLower()) {
                ++g_failures;
                std::fprintf(stderr, "FAIL not lowercase: %s\n", qPrintable(suffix));
            }
            if (suffix.contains(QLatin1Char('.')) || suffix.contains(QLatin1Char('*'))) {
                ++g_failures;
                std::fprintf(stderr, "FAIL contains dot/star: %s\n", qPrintable(suffix));
            }
            if (suffix.isEmpty()) {
                ++g_failures;
                std::fprintf(stderr, "FAIL empty suffix entry\n");
            }
        }
        CHECK(seen.size() == suffixes.size());
        CHECK(suffixes.contains(QStringLiteral("zip")));
        CHECK(suffixes.contains(QStringLiteral("cbz")));
        CHECK(suffixes.contains(QStringLiteral("docx")));
        CHECK(suffixes.contains(QStringLiteral("epub")));
        CHECK(suffixes.size() >= 14);
        if (suffixes.size() < 14) {
            std::fprintf(stderr, "FAIL suffix count = %d, want >= 14\n", int(suffixes.size()));
        }
    }

    // 5. 文档类与漫画类的区分：只有 zip/cbz 算漫画包。
    CHECK(ArchiveKinds::isDocumentContainer(QStringLiteral("docx")));
    CHECK(ArchiveKinds::isDocumentContainer(QStringLiteral("apk")));
    CHECK(!ArchiveKinds::isDocumentContainer(QStringLiteral("cbz")));
    CHECK(!ArchiveKinds::isDocumentContainer(QStringLiteral("zip")));
    CHECK(!ArchiveKinds::isDocumentContainer(QStringLiteral("txt")));

    // 6. 热路径版本（looksVirtual 用）：零分配、大小写不敏感、空串安全。
    {
        CHECK(ArchiveKinds::isOpenableSuffixView(QStringView(u"docx")));
        CHECK(ArchiveKinds::isOpenableSuffixView(QStringView(u"DOCX")));
        CHECK(ArchiveKinds::isOpenableSuffixView(QStringView(u"apk")));
        CHECK(!ArchiveKinds::isOpenableSuffixView(QStringView(u"doc")));
        CHECK(!ArchiveKinds::isOpenableSuffixView(QStringView()));
    }

    if (g_failures == 0) {
        std::printf("archivekinds.pure: PASS\n");
        return 0;
    }
    std::printf("archivekinds.pure: FAIL (%d)\n", g_failures);
    return 1;
}
