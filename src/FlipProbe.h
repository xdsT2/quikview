#pragma once

// 快速翻图响应计时探针（验收专用，默认零开销）。
// 开启：设置环境变量 SOLEVIEW_FLIP_PROBE=<CSV 路径>（兼容 QUIKVIEW_FLIP_PROBE）。
// 每次打点追加一行：elapsed_us,event,target,mode
//   event: input（输入时刻）/ first（首个目标画面上屏）/ full（目标清晰帧上屏）
//   - 只用 C 标准库与 QElapsedTimer，不触碰 QSettings、信号与样式，
//     避免探针本身污染被测路径；未开启时每个函数首个判断即返回。
//   - 外部脚本按 target 归组：首个 input → first 算「输入到首个目标画面」，
//     最后一个 input → full 算「最后输入到目标清晰帧」。
#include <cstdio>
#include <cstdlib>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>

namespace FlipProbe {

inline FILE *file()
{
    static FILE *f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *p = std::getenv("SOLEVIEW_FLIP_PROBE");
        if (!p || !*p)
            p = std::getenv("QUIKVIEW_FLIP_PROBE");
        if (p && *p)
            f = std::fopen(p, "a");
    }
    return f;
}

// 全进程单调起点；第一次打点时 lazily 启动。
inline qint64 originUs()
{
    static QElapsedTimer t;
    static bool started = false;
    if (!started) {
        t.start();
        started = true;
    }
    return t.nsecsElapsed() / 1000;
}

inline bool enabled()
{
    return file() != nullptr;
}

inline void mark(const char *event, const QString &target, const char *mode)
{
    FILE *f = file();
    if (!f)
        return;
    const QByteArray name = target.toUtf8();
    std::fprintf(f, "%lld,%s,%s,%s\n",
                 static_cast<long long>(originUs()), event,
                 name.constData(), mode ? mode : "");
    std::fflush(f);
}

inline void input(const QString &target, const char *mode)
{
    mark("input", target, mode);
}

inline void firstFrame(const QString &target)
{
    mark("first", target, "");
}

inline void fullFrame(const QString &target)
{
    mark("full", target, "");
}

} // namespace FlipProbe
