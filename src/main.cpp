#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QIcon>

#include <QTimer>
#include <QTranslator>
#include "Config.h"
#include "MainWindow.h"
#include "Theme.h"

#ifndef QUIKVIEW_VERSION
#define QUIKVIEW_VERSION "0.0.0"
#endif

int main(int argc, char *argv[])
{
    // 高 DPI：Qt6 默认开启，这里显式声明保证跨平台一致
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QuikView"));
    QApplication::setApplicationVersion(QStringLiteral(QUIKVIEW_VERSION));
    QApplication::setOrganizationName(QStringLiteral("QuikView"));

    // Honeyview 铬件：默认 UI 字体（微软雅黑 9pt）
    QApplication::setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));

    // 应用图标：EXE 在 build/，资源在上一级 resources/
    {
        const QString base = QCoreApplication::applicationDirPath();
        QString iconPath = base + QStringLiteral("/../resources/quikview.ico");
        if (!QFileInfo::exists(iconPath)) {
            iconPath = base + QStringLiteral("/resources/quikview.ico");
        }
        if (!QFileInfo::exists(iconPath)) {
            iconPath = base + QStringLiteral("/quikview.ico");
        }
        if (QFileInfo::exists(iconPath)) {
            app.setWindowIcon(QIcon(iconPath));
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
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("Image file to open"));
    parser.process(app);

    MainWindow window;
    window.show();

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
