#pragma once

// 打开图片/目录分段耗时探针（验收专用，默认零开销）。
// 开启：设置环境变量 QUIKVIEW_PERF_LOG=<日志路径>。
// 每次打点追加一行：seq,stage,elapsed_us,detail
//   - 只在本文件内用 C 标准库 fputs 与 QElapsedTimer，不触碰 QSettings、
//     信号和样式，避免探针本身污染被测路径；未开启时每个函数首个判断即返回。
//   - 外部脚本按 seq 归组，算「点击 → 首帧」「扫描/排序/胶片栏/解码」分段。
//   - 首帧判定：begin() 时置 pendingPaint；ImageCanvas 首帧 paintEvent 消费。
#include <cstdio>
#include <cstdlib>

#include <QByteArray>
#include <QElapsedTimer>
#include <QString>

namespace PerfProbe {

inline FILE *file()
{
    static FILE *f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        const char *p = std::getenv("QUIKVIEW_PERF_LOG");
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

inline void mark(int seq, const char *stage, const char *detail)
{
    FILE *f = file();
    if (!f || seq <= 0)
        return;
    const qint64 us = originUs();
    std::fprintf(f, "%d,%s,%lld,%s\n", seq, stage,
                 static_cast<long long>(us), detail ? detail : "");
    std::fflush(f);
}

inline bool enabled()
{
    return file() != nullptr;
}

inline int &seqRef()
{
    static int s = 0;
    return s;
}

inline int currentSeq()
{
    return seqRef();
}

inline bool &pendingPaintRef()
{
    static bool p = false;
    return p;
}

inline bool &imageShownRef()
{
    static bool s = false;
    return s;
}

// 一次打开流程的起点。trigger: file=folder/…；detail 建议带文件名（可为空）。
inline int begin(const char *trigger, const char *detail)
{
    const int s = ++seqRef();
    pendingPaintRef() = true;
    imageShownRef() = false;
    mark(s, "open", trigger);
    if (detail)
        mark(s, "trigger-detail", detail);
    return s;
}

// 图片已经 setPixmap 到画布：此后发生的 paint 才是「图片首帧」。
inline void markImageShown()
{
    imageShownRef() = true;
}

// ImageCanvas 首帧 paint 时调用；同一 seq 只记一次，且必须在图片上屏之后。
inline void firstPaint()
{
    bool &pending = pendingPaintRef();
    if (!pending || !imageShownRef())
        return;
    pending = false;
    mark(seqRef(), "first-paint", "");
}

} // namespace PerfProbe
