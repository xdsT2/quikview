// SoleView 配置与书签继承单测（验证 sole_config_keys.h、单向迁移、便携模式优先、环境变量兼容）。
#include "Config.h"
#include "sole_config_keys.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
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

static QByteArray sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex();
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    // 便携模式只认 applicationDirPath()/config.ini；测试 exe 位于 build/，那里
    // 有真实 config.ini。把自身复制到临时目录再以子进程跑，确保被测代码走
    // AppConfigLocation 分支，既不依赖也不改写构建目录的 config.ini。
    if (qgetenv("SOLEVIEW_MIGRATION_TEST_CHILD").isEmpty()) {
        QCoreApplication parent(argc, argv);
        const QString src = QCoreApplication::applicationFilePath();
        QTemporaryDir sandbox;
        if (!sandbox.isValid()) {
            std::fprintf(stderr, "FAIL migration.pure: cannot create sandbox\n");
            return 1;
        }
        const QString dst = sandbox.filePath(QFileInfo(src).fileName());
        if (!QFile::copy(src, dst)) {
            std::fprintf(stderr, "FAIL migration.pure: cannot copy test exe\n");
            return 1;
        }
        const QString srcDir = QDir::toNativeSeparators(QFileInfo(src).absolutePath());
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("PATH"), srcDir + QLatin1Char(';')
                   + env.value(QStringLiteral("PATH")));
        env.insert(QStringLiteral("QT_PLUGIN_PATH"), srcDir);
        env.insert(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"),
                   srcDir + QStringLiteral("/platforms"));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("SOLEVIEW_MIGRATION_TEST_CHILD"), QStringLiteral("1"));
        env.remove(QStringLiteral("SOLEVIEW_CONFIG_PATH"));
        env.remove(QStringLiteral("QUIKVIEW_CONFIG_PATH"));

        QProcess child;
        child.setProcessEnvironment(env);
        child.setProcessChannelMode(QProcess::ForwardedChannels);
        child.start(dst, QCoreApplication::arguments().mid(1));
        if (!child.waitForFinished(120000)) {
            child.kill();
            child.waitForFinished();
            std::fprintf(stderr, "FAIL migration.pure: child timeout\n");
            return 1;
        }
        return child.exitCode();
    }
#endif

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("SoleView"));
    QCoreApplication::setApplicationName(QStringLiteral("SoleView"));

    // Windows 上 QStandardPaths 缓存真实 LOCALAPPDATA，qputenv 对其无效；
    // 测试模式把标准位置重定向到 <LOCALAPPDATA>/qttest/<AppName>，期望值据此计算。
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir tmp;
    CHECK(tmp.isValid());

    const QString genericCfg =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    const QString appCfg =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString legacyIni = QDir::cleanPath(QString::fromStdWString(
        solecfg::LegacyInstallConfigPath(genericCfg.toStdWString())));
    // AppConfigLocation 本身已经是 .../SoleView/SoleView，直接拼接文件名。
    const QString newIni = appCfg + QStringLiteral("/config.ini");
    const QString newDir = QFileInfo(newIni).absolutePath();
    const QString legacyDir = QFileInfo(legacyIni).absolutePath();
    const QString marker = newDir + QStringLiteral("/.migrated-from-quikview");
    CHECK(newDir == appCfg);

    // 清理历史运行残留，保证重复执行仍从「旧配置存在 / 新配置不存在」开始。
    QFile::remove(newIni);
    QFile::remove(marker);
    QFile::remove(legacyIni);
    CHECK(QDir().mkpath(legacyDir));

    // 1. 隔离配置目录验证从 QuikView\QuikView\config.ini 单向迁移配置与书签
    {
        QSettings s(legacyIni, QSettings::IniFormat);
        s.setValue(QStringLiteral("config/theme"), QStringLiteral("light"));
        s.setValue(QStringLiteral("config/default_zoom"), QStringLiteral("actual"));
        s.setValue(QStringLiteral("config/bookmark/max_bookmarks"), 77);
        s.setValue(QStringLiteral("config/bookmark/auto_bookmark"), true);
        s.setValue(QStringLiteral("favorites"),
                   QStringList{ QStringLiteral("E:/img/a.png"), QStringLiteral("E:/img/b.jpg") });
        s.setValue(QStringLiteral("recent_files"),
                   QStringList{ QStringLiteral("E:/img/recent.png") });
        s.sync();
    }
    const QByteArray legacyShaBefore = sha256File(legacyIni);
    CHECK(!legacyShaBefore.isEmpty());

    qunsetenv("SOLEVIEW_CONFIG_PATH");
    qunsetenv("QUIKVIEW_CONFIG_PATH");

    // Config() 内部走 defaultConfigPath()，应完成单向迁移并落在测试模式安装目录。
    {
        Config cfg;
        CHECK(!cfg.isPortable());
        CHECK(cfg.settings().fileName() == newIni);
        CHECK(cfg.theme() == QLatin1String("light"));
        CHECK(cfg.defaultZoom() == QLatin1String("actual"));
        CHECK(cfg.maxBookmarks() == 77);
        CHECK(cfg.autoBookmark() == true);
        CHECK(cfg.favorites() == (QStringList{ QStringLiteral("E:/img/a.png"), QStringLiteral("E:/img/b.jpg") }));
        CHECK(cfg.recentFiles() == (QStringList{ QStringLiteral("E:/img/recent.png") }));
    }

    CHECK(QFileInfo::exists(newIni));
    CHECK(QFileInfo::exists(marker));
    CHECK(sha256File(legacyIni) == legacyShaBefore);

    // 2. 再次打开且修改 SoleView 配置不得回写旧目录
    {
        Config cfg2;
        cfg2.toggleFavorite(QStringLiteral("E:/img/c.webp"));
        cfg2.settings().sync();
        CHECK(cfg2.favorites().size() == 3);
    }
    CHECK(sha256File(legacyIni) == legacyShaBefore);

    // 3. 环境变量优先级：SOLEVIEW_CONFIG_PATH > QUIKVIEW_CONFIG_PATH
    const QString envOld = tmp.filePath(QStringLiteral("old-env.ini"));
    const QString envNew = tmp.filePath(QStringLiteral("new-env.ini"));
    {
        QSettings sOld(envOld, QSettings::IniFormat);
        sOld.setValue(QStringLiteral("config/theme"), QStringLiteral("light"));
        sOld.sync();
        QSettings sNew(envNew, QSettings::IniFormat);
        sNew.setValue(QStringLiteral("config/theme"), QStringLiteral("system"));
        sNew.sync();
    }
    qputenv("QUIKVIEW_CONFIG_PATH", envOld.toLocal8Bit());
    {
        Config cfgLegacyEnv;
        CHECK(cfgLegacyEnv.settings().fileName() == envOld);
        CHECK(cfgLegacyEnv.theme() == QLatin1String("light"));
    }
    qputenv("SOLEVIEW_CONFIG_PATH", envNew.toLocal8Bit());
    {
        Config cfgNewEnv;
        CHECK(cfgNewEnv.settings().fileName() == envNew);
        CHECK(cfgNewEnv.theme() == QLatin1String("system"));
    }

    // 4. shared/sole_config_keys.h 常量检查
    CHECK(std::wstring(solecfg::kConfigPathEnvVar) == L"SOLEVIEW_CONFIG_PATH");
    CHECK(std::wstring(solecfg::kLegacyConfigPathEnvVar) == L"QUIKVIEW_CONFIG_PATH");
    CHECK(solecfg::InstallConfigPath(L"C:\\Users\\Test\\AppData\\Local")
          == L"C:\\Users\\Test\\AppData\\Local\\SoleView\\SoleView\\config.ini");
    CHECK(solecfg::LegacyInstallConfigPath(L"C:\\Users\\Test\\AppData\\Local")
          == L"C:\\Users\\Test\\AppData\\Local\\QuikView\\QuikView\\config.ini");

    qunsetenv("SOLEVIEW_CONFIG_PATH");
    qunsetenv("QUIKVIEW_CONFIG_PATH");
    QFile::remove(newIni);
    QFile::remove(marker);
    QFile::remove(legacyIni);

    if (g_failures == 0) {
        std::printf("migration.pure: PASS\n");
        return 0;
    }
    std::printf("migration.pure: FAIL (%d)\n", g_failures);
    return 1;
}
