#pragma once

// 主题切换耗时探针（验收专用，默认零开销）。
// 开启：设置环境变量 SOLEVIEW_THEME_PROBE=<日志路径>（兼容 QUIKVIEW_THEME_PROBE）。
// 每次打点追加一行：seq,stage,elapsed_us,mode
//   - 只在本文件内用 C 标准库 fputs/clock，不触碰 QSettings、信号、样式，
//     避免探针本身污染被测路径；未开启时每个函数首个判断即返回。
//   - 采集结束后由外部脚本按 seq 归组，算首帧 / 全表面 / 各段耗时。
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>

namespace ThemeProbe {

inline FILE *file()
{
    static FILE *f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *p = std::getenv("SOLEVIEW_THEME_PROBE");
        if (!p || !*p)
            p = std::getenv("QUIKVIEW_THEME_PROBE");
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

inline void mark(int seq, const char *stage, const char *mode)
{
    FILE *f = file();
    if (!f)
        return;
    const qint64 us = originUs();
    std::fprintf(f, "%d,%s,%lld,%s\n", seq, stage,
                 static_cast<long long>(us), mode ? mode : "");
    std::fflush(f);
}

inline bool enabled()
{
    return file() != nullptr;
}

// 切换序号：每个入口在选择确认处 begin() +1，后续阶段读 current() 带同一个 seq。
inline int &seqRef()
{
    static int s = 0;
    return s;
}
inline int begin(const char *stage, const char *mode)
{
    const int s = ++seqRef();
    mark(s, stage, mode);
    return s;
}
inline int currentSeq()
{
    return seqRef();
}

} // namespace ThemeProbe
