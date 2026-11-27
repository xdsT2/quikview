// 360° 全景查看器（QuikView 可选组件，独立进程）。
// ---------------------------------------------------------------------------
// 命令行（由主程序显式启动，分参数传递，路径带空格/中文不靠引号拼接）：
//   quikview-pano.exe --image <本地图片路径>
//                     [--heading <度>] [--pitch <度>] [--fov <垂直度>]
//                     [--ev <曝光档>] [--win-w <px>] [--win-h <px>]
//                     [--bench <帧数>] [--bench-out <输出文本>] [--bench-shot <输出png>]
//                     [--probe-pixel <输出文本>]
// 退出码：0 正常 / 2 参数或图片不可读。
// 本进程不读写 QuikView 配置，关闭即结束；主程序的原图与缩放状态不受影响。
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

#ifndef QUIKVIEW_PANO_VERSION
#define QUIKVIEW_PANO_VERSION "0.0.0"
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

// 验收模式把一行文本落到 --bench-out 文件（WIN32 子进程 stdout 不可见，
// 错误与统计都走文件，不改成控制台程序）。
void writeReport(const QString &path, const QString &line)
{
    if (path.isEmpty())
        return;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(line.toUtf8() + '\n');
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QuikView Panorama"));
    QApplication::setApplicationVersion(QStringLiteral(QUIKVIEW_PANO_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "QuikView 360° 全景查看器：拖动转视角，滚轮调整视野，+/- 调曝光，Esc 退出。"));
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
    QCommandLineOption evOpt(QStringLiteral("ev"), QStringLiteral("初始曝光补偿档（-5..+5，缺省 0）"),
                             QStringLiteral("ev"));
    QCommandLineOption winWOpt(QStringLiteral("win-w"), QStringLiteral("验收用：窗口宽（px）"),
                               QStringLiteral("px"));
    QCommandLineOption winHOpt(QStringLiteral("win-h"), QStringLiteral("验收用：窗口高（px）"),
                               QStringLiteral("px"));
    QCommandLineOption benchOpt(QStringLiteral("bench"), QStringLiteral("渲染耗时验收：帧数"),
                                QStringLiteral("frames"));
    QCommandLineOption benchOutOpt(QStringLiteral("bench-out"),
                                   QStringLiteral("验收：把耗时与尺寸写成文本"),
                                   QStringLiteral("path"));
    QCommandLineOption benchShotOpt(QStringLiteral("bench-shot"),
                                    QStringLiteral("验收：把首帧渲染结果存为 PNG"),
                                    QStringLiteral("path"));
    QCommandLineOption probeOpt(QStringLiteral("probe-pixel"),
                                QStringLiteral("验收：输出屏幕中心像素的线性值与当前 EV 8bit 值"),
                                QStringLiteral("path"));
    parser.addOption(imageOpt);
    parser.addOption(headingOpt);
    parser.addOption(pitchOpt);
    parser.addOption(fovOpt);
    parser.addOption(evOpt);
    parser.addOption(winWOpt);
    parser.addOption(winHOpt);
    parser.addOption(benchOpt);
    parser.addOption(benchOutOpt);
    parser.addOption(benchShotOpt);
    parser.addOption(probeOpt);
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
    double ev = 0.0;
    if (parser.isSet(headingOpt) && !toDouble(parser.value(headingOpt), &heading))
        heading = 0.0;
    if (parser.isSet(pitchOpt) && !toDouble(parser.value(pitchOpt), &pitch))
        pitch = 0.0;
    if (parser.isSet(fovOpt) && !toDouble(parser.value(fovOpt), &fov))
        fov = 75.0;
    if (parser.isSet(evOpt)) {
        double parsed = 0.0;
        if (toDouble(parser.value(evOpt), &parsed))
            ev = qBound(-5.0, parsed, 5.0);
    }

    PanoWindow window(path, heading, pitch, std::fabs(fov) > 0.0 ? fov : 75.0, ev);

    if (!window.ok()) {
        const QString err = window.property("panoError").toString();
        const QString category = window.property("panoErrorCategory").toString();
        qCritical() << "无法解码" << path << category << err;
        // WIN32 子进程看不到 stderr：失败类别与文案一并落进 --bench-out，供验收区分。
        writeReport(parser.value(benchOutOpt),
                    QStringLiteral("result=error category=%1 message=%2").arg(category, err));
        return 2;
    }

    // 验收用固定窗口尺寸（不设则按源图宽高比自适应）。
    if (parser.isSet(winWOpt) || parser.isSet(winHOpt)) {
        int w = window.width();
        int h = window.height();
        bool ow = false, oh = false;
        int pw = parser.value(winWOpt).toInt(&ow);
        int ph = parser.value(winHOpt).toInt(&oh);
        if (ow && pw > 0) w = pw;
        if (oh && ph > 0) h = ph;
        window.resize(w, h);
    }
    window.show();

    // 中心像素探针：线性 RGB（曝光前）与当前 EV 下的 8bit 编码值，供线性性数值对账。
    if (parser.isSet(probeOpt)) {
        float lin[3] = {0.0f, 0.0f, 0.0f};
        int srgb8[3] = {0, 0, 0};
        if (window.globe()->probeCenterPixel(lin, srgb8)) {
            const QString line = QStringLiteral("ev=%1 lin=%2,%3,%4 srgb8=%5,%6,%7")
                .arg(window.globe()->ev(), 0, 'f', 1)
                .arg(lin[0], 0, 'f', 6).arg(lin[1], 0, 'f', 6).arg(lin[2], 0, 'f', 6)
                .arg(srgb8[0]).arg(srgb8[1]).arg(srgb8[2]);
            writeReport(parser.value(probeOpt), line);
        } else {
            writeReport(parser.value(probeOpt), QStringLiteral("result=probe-unsupported"));
        }
    }

    const bool wantBench = parser.isSet(benchOpt);
    const bool wantShot = parser.isSet(benchShotOpt);
    const bool wantProbe = parser.isSet(probeOpt);

    // 验收模式：渲染若干帧打印平均耗时。
    if (wantBench) {
        bool ok = false;
        const int frames = parser.value(benchOpt).toInt(&ok);
        if (ok && frames > 0) {
            const double full = window.globe()->benchFrameMs(frames, 1.0);
            const double half = window.globe()->benchFrameMs(frames, 0.5);
            const QString report =
                QStringLiteral("size=%1x%2 src=%3x%4 full=%5ms half=%6ms ev=%7 format=%8")
                    .arg(window.globe()->width())
                    .arg(window.globe()->height())
                    .arg(window.globe()->nativeSourceSize().width())
                    .arg(window.globe()->nativeSourceSize().height())
                    .arg(full, 0, 'f', 1)
                    .arg(half, 0, 'f', 1)
                    .arg(window.globe()->ev(), 0, 'f', 1)
                    .arg(window.globe()->isHdr() ? QStringLiteral("float-linear")
                                                 : QStringLiteral("sdr8"));
            qInfo().noquote() << report;
            std::fprintf(stdout, "%s\n", report.toUtf8().constData());
            std::fflush(stdout);
            writeReport(parser.value(benchOutOpt), report);
        }
    }
    if (wantShot) {
        window.globe()->resetView();
        const QString out = parser.value(benchShotOpt);
        // 抓整窗（含底部操作条与视野/曝光标签），这样截图能同时核对渲染与外壳。
        window.grab().save(out, "PNG");
        qInfo().noquote() << QStringLiteral("shot saved: %1").arg(out);
    }

    // 验收 / 探针 / 截图模式：取证完自动退出，不占用桌面；纯交互模式才常驻事件循环。
    if (wantBench || wantShot || wantProbe) {
        QTimer::singleShot(0, &app, &QApplication::quit);
        return app.exec();
    }

    return app.exec();
}
