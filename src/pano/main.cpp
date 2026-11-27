// 360° 全景查看器（SoleView 可选组件，独立进程）。
// ---------------------------------------------------------------------------
// 命令行（由主程序显式启动，分参数传递，路径带空格/中文不靠引号拼接）：
//   soleview-pano.exe --image <本地图片路径>
//                     [--heading <度>] [--pitch <度>] [--fov <垂直度>]
//                     [--bench <帧数>] [--bench-shot <输出png>]
// 退出码：0 正常 / 2 参数或图片不可读。
// 本进程不读写 SoleView 配置，关闭即结束；主程序的原图与缩放状态不受影响。
#include "PanoGlobe.h"
#include "PanoWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>
#include <cstdio>

#ifndef SOLEVIEW_PANO_VERSION
#ifdef QUIKVIEW_PANO_VERSION
#define SOLEVIEW_PANO_VERSION QUIKVIEW_PANO_VERSION
#else
#define SOLEVIEW_PANO_VERSION "0.0.0"
#endif
#endif

namespace {

bool toDouble(const QString &text, double *out)
{
    bool ok = false;
    const double v = text.toDouble(&ok);
    if (ok)
        *out = v;
    return ok;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("SoleView Panorama"));
    QApplication::setApplicationVersion(QStringLiteral(SOLEVIEW_PANO_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "SoleView 360° 全景查看器：拖动转视角，滚轮调整视野，Esc 退出。"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption imageOpt(QStringLiteral("image"), QStringLiteral("等距柱状全景图路径"),
                                QStringLiteral("path"));
    QCommandLineOption headingOpt(QStringLiteral("heading"), QStringLiteral("初始朝向（度）"),
                                  QStringLiteral("deg"));
    QCommandLineOption pitchOpt(QStringLiteral("pitch"), QStringLiteral("初始俯仰（度）"),
                                QStringLiteral("deg"));
    QCommandLineOption fovOpt(QStringLiteral("fov"), QStringLiteral("初始垂直视野（度）"),
                              QStringLiteral("deg"));
    QCommandLineOption benchOpt(QStringLiteral("bench"), QStringLiteral("渲染耗时验收：帧数"),
                                QStringLiteral("frames"));
    QCommandLineOption benchOutOpt(QStringLiteral("bench-out"),
                                   QStringLiteral("验收：把耗时与尺寸写成文本"),
                                   QStringLiteral("path"));
    QCommandLineOption benchShotOpt(QStringLiteral("bench-shot"),
                                    QStringLiteral("验收：把首帧渲染结果存为 PNG"),
                                    QStringLiteral("path"));
    parser.addOption(imageOpt);
    parser.addOption(headingOpt);
    parser.addOption(pitchOpt);
    parser.addOption(fovOpt);
    parser.addOption(benchOpt);
    parser.addOption(benchOutOpt);
    parser.addOption(benchShotOpt);
    parser.process(app);

    const QString path = parser.value(imageOpt);
    if (path.isEmpty()) {
        qCritical() << "缺少 --image";
        return 2;
    }
    if (!QFileInfo(path).isFile()) {
        qCritical() << "图片不存在" << path;
        return 2;
    }

    double heading = 0.0;
    double pitch = 0.0;
    double fov = 75.0;
    if (parser.isSet(headingOpt) && !toDouble(parser.value(headingOpt), &heading))
        heading = 0.0;
    if (parser.isSet(pitchOpt) && !toDouble(parser.value(pitchOpt), &pitch))
        pitch = 0.0;
    if (parser.isSet(fovOpt) && !toDouble(parser.value(fovOpt), &fov))
        fov = 75.0;

    PanoWindow window(path, heading, pitch, std::fabs(fov) > 0.0 ? fov : 75.0);
    if (!window.ok()) {
        qCritical() << "无法解码" << path << window.property("panoError").toString();
        return 2;
    }
    window.show();

    // 验收模式：渲染若干帧打印平均耗时，可同时落一张首帧 PNG（不占屏幕也能取证据）。
    if (parser.isSet(benchOpt)) {
        bool ok = false;
        const int frames = parser.value(benchOpt).toInt(&ok);
        if (ok && frames > 0) {
            const double full = window.globe()->benchFrameMs(frames, 1.0);
            const double half = window.globe()->benchFrameMs(frames, 0.5);
            const QString report =
                QStringLiteral("size=%1x%2 src=%3x%4 full=%5ms half=%6ms")
                    .arg(window.globe()->width())
                    .arg(window.globe()->height())
                    .arg(window.globe()->sourceSize().width())
                    .arg(window.globe()->sourceSize().height())
                    .arg(full, 0, 'f', 1)
                    .arg(half, 0, 'f', 1);
            qInfo().noquote() << report;
            std::fprintf(stdout, "%s\n", report.toUtf8().constData());
            std::fflush(stdout);
            if (parser.isSet(benchOutOpt)) {
                QFile f(parser.value(benchOutOpt));
                if (f.open(QIODevice::WriteOnly | QIODevice::Text))
                    f.write(report.toUtf8() + '\n');
            }
        }
        if (parser.isSet(benchShotOpt)) {
            window.globe()->resetView();
            const QString out = parser.value(benchShotOpt);
            // 抓整窗（含底部操作条与视野标签），这样截图能同时核对渲染与外壳。
            window.grab().save(out, "PNG");
            qInfo().noquote() << QStringLiteral("shot saved: %1").arg(out);
        }
        QTimer::singleShot(0, &app, &QApplication::quit);
        return app.exec();
    }

    return app.exec();
}
