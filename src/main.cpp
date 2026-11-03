#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <QTimer>
#include <QTranslator>
#include "Config.h"
#include "MainWindow.h"
#include "Theme.h"
#include "UpdateChecker.h"

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

#ifndef QUIKVIEW_VERSION
#define QUIKVIEW_VERSION "0.0.0"
#endif

namespace {

// 单实例键：QuikView-<RuntimeLocation 哈希>。
// 只带固定名字会让多用户/多会话互相顶掉，所以把当前用户的运行时目录路径也算进去。
QString singleInstanceKey()
{
    const QString scope = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QByteArray digest =
        QCryptographicHash::hash(scope.toUtf8(), QCryptographicHash::Sha1).toHex().left(16);
    return QStringLiteral("QuikView-") + QString::fromLatin1(digest);
}

} // namespace

int main(int argc, char *argv[])
{
    // 高 DPI：Qt6 默认开启，这里显式声明保证跨平台一致
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QuikView"));
    QApplication::setApplicationVersion(QStringLiteral(QUIKVIEW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("QuikView"));

    // Honeyview 铬件：默认 UI 字体（微软雅黑 9pt）
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    // 应用图标：先找磁盘上的 ico（方便换肤/调试），落空则用 RCC 内嵌的 :/quikview.ico。
    // 安装目录里只有 exe + DLL、没有外部 .ico 时，全靠内嵌这一层；
    // 窗口图标不再依赖 resources/ 是否被打包进去。
    {
        const QString base = QCoreApplication::applicationDirPath();
        const QStringList candidates = {
            base + QStringLiteral("/../resources/quikview.ico"),
            base + QStringLiteral("/resources/quikview.ico"),
            base + QStringLiteral("/quikview.ico"),
            QStringLiteral(":/quikview.ico"),
        };
        for (const QString &iconPath : candidates) {
            if (QFileInfo::exists(iconPath)) {
                app.setWindowIcon(QIcon(iconPath));
                break;
            }
        }
    }

    // 翻译（qt_add_translations 生成并嵌入到 :/i18n）
    QTranslator translator;
    const QString lang = Config::instance().language();
    if (lang != QLatin1String("system") && lang != QLatin1String("zh_CN")) {
        if (translator.load(QStringLiteral("quikview_") + lang, QStringLiteral(":/i18n"))) {
            app.installTranslator(&translator);
        }
    }

    // 主题
    const bool dark = (Config::instance().theme() != QLatin1String("light"));
    Theme::apply(app, dark ? Theme::Mode::Dark : Theme::Mode::Light);

    // 一次性迁移：老版本「单击屏幕两侧翻页」是灰显项，但保存设置时 addCheckUnavailable
    // 也会把 mouse/click_side_pageturn 写盘（默认 false），存量配置里很可能是 false。
    // 本次接线默认 true，规则：etc/migrated_side_click 标记不存在时忽略旧值、强制按
    // true 处理并写入标记（顺带把键归一为 true），标记存在后才以配置为准。
    {
        QSettings &s = Config::instance().settings();
        if (!s.contains(QStringLiteral("etc/migrated_side_click"))) {
            s.setValue(QStringLiteral("etc/migrated_side_click"), true);
            s.setValue(QStringLiteral("config/mouse/click_side_pageturn"), true);
        }
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("QuikView - a fast, clean image viewer"));
    parser.addHelpOption();
    parser.addVersionOption();
    // 调试：--settings 启动后直接打开设置对话框（用于自动化截图验证）
    QCommandLineOption settingsOpt(QStringLiteral("settings"),
                                   QStringLiteral("Open the settings dialog on start"));
    parser.addOption(settingsOpt);
    // 调试：--view-mode <actual|fit|width> 打开文件后切浏览方式（冒烟自动化用）
    QCommandLineOption viewModeOpt(QStringLiteral("view-mode"),
                                   QStringLiteral("View mode after opening: actual|fit|width"),
                                   QStringLiteral("mode"));
    parser.addOption(viewModeOpt);
    // 调试：--smoke <cmd> 首图就绪后执行单条命令（next/prev/next10/prev10/...）
    QCommandLineOption smokeOpt(QStringLiteral("smoke"),
                                QStringLiteral("Run a smoke-test command after the first image loads"),
                                QStringLiteral("cmd"));
    parser.addOption(smokeOpt);
    // 调试：--screenshot <out.png> 全部命令执行完后 QWidget::grab() 自渲染保存并退出。
    // Qt 内部渲染与 DPR 无关，绕开外部 PrintWindow/ImageGrab 在高 DPI 虚拟化下的裁剪/错位。
    QCommandLineOption shotOpt(QStringLiteral("screenshot"),
                               QStringLiteral("Grab the window to <out.png> after all commands, then quit"),
                               QStringLiteral("out"));
    parser.addOption(shotOpt);
    // 调试：--shot-delay <ms> 覆盖截图前等待（默认 全部命令后 +2.2s）。
    // 用于抓只活 2 秒的 toast / 过渡动画这类「截图总是差一点」的状态。
    QCommandLineOption shotDelayOpt(QStringLiteral("shot-delay"),
                                    QStringLiteral("Override screenshot delay in ms"),
                                    QStringLiteral("ms"));
    parser.addOption(shotDelayOpt);
    // 调试：--screenshot-ui <out.png> 抓「当前最上层的那个窗口」（菜单 > 模态框 > 活动窗口 > 主窗口）。
    // 二级界面（设置/图片转换/图片信息/右键菜单）都是独立顶层窗口，window.grab() 截不到。
    QCommandLineOption shotUiOpt(QStringLiteral("screenshot-ui"),
                                 QStringLiteral("Grab the active top-level window (menu/dialog) to <out.png>"),
                                 QStringLiteral("out"));
    parser.addOption(shotUiOpt);
    // 调试：--check-update 启动 1.2s 后做一次手动检查（验证 toast / 代理链路）
    QCommandLineOption checkUpdateOpt(QStringLiteral("check-update"),
                                      QStringLiteral("Run a manual update check on start"));
    parser.addOption(checkUpdateOpt);
    // 调试：--update-selftest <out.txt> 版本比较自测，结果写文件后退出
    // （WIN32 子系统没有 stdout，所以落到文件里）
    QCommandLineOption updateSelftestOpt(QStringLiteral("update-selftest"),
                                         QStringLiteral("Write version-comparison self-test to <out.txt>"),
                                         QStringLiteral("out"));
    parser.addOption(updateSelftestOpt);
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image file to open"));
    parser.process(app);

    // 版本比较自测：只依赖纯逻辑，不创建窗口。
    if (parser.isSet(updateSelftestOpt)) {
        struct Case {
            const char *a;
            const char *b;
            int want;
        };
        const Case cases[] = {
            { "0.4.0", "0.4.1", -1 },
            { "0.4.0", "0.5.0", -1 },
            { "0.4.0-dev", "0.4.0", -1 },
            { "0.10.0", "0.9.9", 1 },
            { "v0.4.1", "0.4.0", 1 },
            { "0.4.0", "0.4.0", 0 },
        };
        QStringList lines;
        int failed = 0;
        for (const Case &c : cases) {
            const int got = UpdateChecker::compareVersions(QString::fromLatin1(c.a),
                                                           QString::fromLatin1(c.b));
            const bool ok = (got == c.want);
            if (!ok)
                ++failed;
            lines << QStringLiteral("%1 %2 vs %3 -> %4 (want %5)")
                         .arg(ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                         .arg(QString::fromLatin1(c.a))
                         .arg(QString::fromLatin1(c.b))
                         .arg(got)
                         .arg(c.want);
        }
        lines << QStringLiteral("current = %1").arg(QStringLiteral(QUIKVIEW_VERSION));
        lines << QStringLiteral("RESULT: %1").arg(failed == 0 ? QStringLiteral("PASS")
                                                              : QStringLiteral("FAIL"));
        QFile out(parser.value(updateSelftestOpt));
        if (out.open(QIODevice::WriteOnly | QIODevice::Text))
            out.write(lines.join(QLatin1Char('\n')).toUtf8() + '\n');
        return failed == 0 ? 0 : 1;
    }

    // —— 单实例 ——
    // 文件关联后，用户双击第二张图如果新开一个进程，任务栏就会堆窗口，也不像原版习惯。
    // 这里用 QLocalServer 命名管道：第二个实例把文件路径发给首实例后自己退出。
    // 带调试参数的调用（--smoke / --screenshot / --settings …）不参与转发，
    // 否则自动化会「跑到已有窗口里」，截图和冒烟全部失真。
    const bool debugRun = parser.isSet(settingsOpt) || parser.isSet(viewModeOpt)
                          || parser.isSet(smokeOpt) || parser.isSet(shotOpt)
                          || parser.isSet(shotUiOpt) || parser.isSet(shotDelayOpt)
                          || parser.isSet(checkUpdateOpt);
    // 单实例开关（设置页「不允许有多个实例」，inverted：config/allow_multiple_instances）。
    // 勾选 = 单实例（默认，键为 false）；取消 = 允许多开。关闭时 serverKey 留空，
    // 下方 `if (!serverKey.isEmpty())` 分支天然不监听、不转发。调试参数继续绕过单实例。
    const bool singleInstance = !Config::instance().allowMultipleInstances();
    QString serverKey;
    if (!debugRun && singleInstance) {
        serverKey = singleInstanceKey();
        QLocalSocket probe;
        probe.connectToServer(serverKey);
        if (probe.waitForConnected(300)) {
            const QStringList args = parser.positionalArguments();
            if (!args.isEmpty()) {
                probe.write(args.first().toUtf8());
            }
            probe.write("\n");
            probe.flush();
            probe.waitForBytesWritten(1000);
            probe.disconnectFromServer();
            return 0;
        }
    }

    MainWindow window;
    window.show();

    // 首实例挂监听：收到路径就切图并把自己拉到前台
    static QLocalServer ipcServer;
    if (!serverKey.isEmpty()) {
        QLocalServer::removeServer(serverKey);   // 上个会话崩溃留下的孤儿套接字
        if (!ipcServer.listen(serverKey)) {
            qWarning() << "[single-instance] listen failed:" << ipcServer.errorString();
        } else {
            QObject::connect(&ipcServer, &QLocalServer::newConnection, &window, [&window]() {
                while (QLocalSocket *sock = ipcServer.nextPendingConnection()) {
                    QObject::connect(sock, &QLocalSocket::readyRead, &window, [sock, &window]() {
                        const QString path = QString::fromUtf8(sock->readAll()).trimmed();
                        if (!path.isEmpty()) {
                            window.openFile(path);
                        }
                        if (window.isMinimized()) {
                            window.showNormal();
                        }
                        window.show();
                        window.raise();
                        window.activateWindow();
#ifdef Q_OS_WIN
                        // Qt 的 activateWindow 在后台进程里常被前台锁挡掉，补一次 Win32 调用
                        SetForegroundWindow(reinterpret_cast<HWND>(window.winId()));
#endif
                        sock->disconnectFromServer();
                    });
                    QObject::connect(sock, &QLocalSocket::disconnected,
                                     sock, &QLocalSocket::deleteLater);
                }
            });
        }
    }

    // 检查更新：结果提示条由 UpdateNotifier 统一负责（手动/自动共用）。
    new UpdateNotifier(&window, &window);

    // 自动检查默认关；勾上「启动时检查更新」后延迟 5s 再发，绝不阻塞启动路径。
    if (Config::instance().checkUpdatesOnStartup()) {
        QTimer::singleShot(5000, &window, []() {
            qDebug() << "[update] startup auto check (delayed 5s)";
            UpdateChecker::instance().check(UpdateChecker::Mode::Automatic);
        });
    }

    if (parser.isSet(checkUpdateOpt)) {
        QTimer::singleShot(1200, &window, []() {
            qDebug() << "[update] manual check from --check-update";
            UpdateChecker::instance().check(UpdateChecker::Mode::Manual);
        });
    }

    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        window.openFile(files.first());
    }

    int settleDelay = 2500;   // 无 smoke 命令时的默认截图等待（首图异步加载）
    if (parser.isSet(smokeOpt)) {
        // 首图异步加载，1.5s 起步逐条执行（间隔 1.3s）；不自动退出（外部 taskkill 收尾）
        const QStringList cmds = parser.value(smokeOpt).split(u',');
        int delay = 1500;
        for (const QString &c : cmds) {
            QTimer::singleShot(delay, &window, [&window, c]() {
                window.runSmokeCommand(c.trimmed());
            });
            delay += 1300;
        }
        settleDelay = delay + 900;   // 全部命令执行完再留渲染余量
    }

    if (parser.isSet(shotDelayOpt)) {
        settleDelay = parser.value(shotDelayOpt).toInt();
    }

    if (parser.isSet(shotOpt)) {
        const QString out = parser.value(shotOpt);
        QTimer::singleShot(settleDelay, &window, [&window, out]() {
            window.grab().save(out);
            QApplication::quit();
        });
    }

    if (parser.isSet(shotUiOpt)) {
        const QString out = parser.value(shotUiOpt);
        QTimer::singleShot(settleDelay + 250, &window, [&window, out]() {
            QWidget *target = QApplication::activePopupWidget();
            if (!target) target = QApplication::activeModalWidget();
            if (!target) {
                // 非模态对话框：取最后一个可见的顶层窗口
                const auto tops = QApplication::topLevelWidgets();
                for (QWidget *w : tops) {
                    if (w != &window && w->isVisible() && w->windowType() == Qt::Window) {
                        target = w;
                    }
                }
            }
            if (!target) target = QApplication::activeWindow();
            if (!target) target = &window;
            target->grab().save(out);
            QApplication::quit();
        });
    }

    if (parser.isSet(viewModeOpt)) {
        const QString vm = parser.value(viewModeOpt);
        // openFile 异步加载，showImage 会按当前浏览方式重放，这里先设好状态即可
        if (vm == QLatin1String("actual")) {
            window.setZoomMode(0);
        } else if (vm == QLatin1String("width")) {
            window.setZoomMode(2);
        } else if (vm == QLatin1String("fit")) {
            window.setZoomMode(1);
        }
    }

    if (parser.isSet(settingsOpt)) {
        window.openSettings();
    }

    return app.exec();
}
