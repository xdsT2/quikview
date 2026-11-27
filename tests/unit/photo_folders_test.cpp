// Config 照片文件夹 1–20 读写与兼容（H1-1）无窗口单测。
#include "Config.h"

#include <QDir>
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

int main()
{
    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    const QString ini = tmp.filePath("test-folders.ini");
    qputenv("SOLEVIEW_CONFIG_PATH", ini.toLocal8Bit());
    Config cfg;

    // 1. 默认配置兼容老两格
    CHECK(cfg.photoFolderCount() == 2);
    CHECK(cfg.photoFolders().size() == 2);

    // 2. 兼容仅写 photo_folder_1 / photo_folder_2 的旧 ini
    cfg.settings().setValue(QStringLiteral("config/etc/photo_folder_1"), QStringLiteral("D:/P1"));
    cfg.settings().setValue(QStringLiteral("config/etc/photo_folder_2"), QStringLiteral("D:/P2"));
    CHECK(cfg.photoFolder(1) == QLatin1String("D:/P1"));
    CHECK(cfg.photoFolder(2) == QLatin1String("D:/P2"));

    // 3. 扩容到 5 个并重排
    cfg.setPhotoFolders({
        QStringLiteral("D:/A"),
        QStringLiteral("D:/B"),
        QStringLiteral("D:/C"),
        QStringLiteral("D:/D"),
        QStringLiteral("D:/E"),
    });
    CHECK(cfg.photoFolderCount() == 5);
    CHECK(cfg.photoFolder(5) == QLatin1String("D:/E"));

    // 4. 缩减到 1 个时，尾部旧键应被清理，计数下限钳制为 1
    cfg.setPhotoFolders({ QStringLiteral("D:/OnlyOne") });
    CHECK(cfg.photoFolderCount() == 1);
    CHECK(cfg.photoFolder(1) == QLatin1String("D:/OnlyOne"));
    CHECK(cfg.photoFolder(5).isEmpty());

    // 5. 超限传入 25 个时钳制到 20
    QStringList many;
    for (int i = 1; i <= 25; ++i) {
        many.append(QStringLiteral("D:/F%1").arg(i));
    }
    cfg.setPhotoFolders(many);
    CHECK(cfg.photoFolderCount() == Config::kMaxPhotoFolders);
    CHECK(cfg.photoFolder(20) == QLatin1String("D:/F20"));

    // 6. 空列表回落到最少 1 个空槽
    cfg.setPhotoFolders({});
    CHECK(cfg.photoFolderCount() == Config::kMinPhotoFolders);

    if (g_failures == 0) {
        std::printf("photofolders.pure: PASS\n");
        return 0;
    }
    std::printf("photofolders.pure: FAIL (%d)\n", g_failures);
    return 1;
}
