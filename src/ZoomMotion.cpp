#include "ZoomMotion.h"

#include <algorithm>
#include <cmath>

namespace ZoomMotion {

namespace {

constexpr double kMinZoom = 1e-12;
constexpr double kMinDisplacement = 1e-9;
constexpr double kDoneEps = 1e-9;

double safeLog(double z)
{
    return std::log(z > kMinZoom ? z : kMinZoom);
}

int sign(double v)
{
    return (v > 0.0) - (v < 0.0);
}

void finish(State &s)
{
    s.count = 0;
    s.active = false;
    s.logZoom = safeLog(s.target);
}

void addSegment(State &s, double displacement, double durationMs)
{
    if (s.count < kMaxSegments) {
        s.segs[s.count++] = Segment{durationMs, displacement / durationMs};
        return;
    }
    Segment &last = s.segs[kMaxSegments - 1];
    const double remainingDisp = last.rate * last.remainingMs;
    last.remainingMs = std::max(last.remainingMs, durationMs);
    last.rate = (remainingDisp + displacement) / last.remainingMs;
}

} // namespace

void reset(State &s, double zoom)
{
    s.target = zoom > kMinZoom ? zoom : 1.0;
    s.logZoom = safeLog(s.target);
    s.count = 0;
    s.active = false;
}

void push(State &s, double currentZoom, double targetZoom, double durationMs)
{
    const double cur = currentZoom > kMinZoom ? currentZoom : kMinZoom;
    const double tgt = targetZoom > kMinZoom ? targetZoom : kMinZoom;
    const double ms = durationMs > 1.0 ? durationMs : 1.0;

    if (!s.active) {
        s.logZoom = safeLog(cur);
        s.target = cur;
        s.count = 0;
    }

    const double logNew = safeLog(tgt);
    const int pendingDir = sign(rate(s));
    const int newDir = sign(logNew - s.logZoom);
    double base = safeLog(s.target);
    if (s.count > 0 && pendingDir != 0 && newDir != 0 && newDir != pendingDir) {
        // 反向操作立即清掉积压段，从当前位置起步。
        s.count = 0;
        base = s.logZoom;
    }

    s.target = tgt;
    const double displacement = logNew - base;
    if (std::fabs(displacement) < kMinDisplacement) {
        if (s.count == 0) {
            finish(s);
        }
        return;
    }
    addSegment(s, displacement, ms);
    s.active = true;
}

bool step(State &s, double dtMs)
{
    if (!s.active) {
        return false;
    }
    if (!(dtMs > 0.0)) {
        return true;
    }

    int kept = 0;
    for (int i = 0; i < s.count; ++i) {
        Segment seg = s.segs[i];
        const double take = std::min(dtMs, seg.remainingMs);
        s.logZoom += seg.rate * take;
        seg.remainingMs -= take;
        if (seg.remainingMs > kDoneEps) {
            s.segs[kept++] = seg;
        }
    }
    s.count = kept;
    if (s.count == 0) {
        finish(s);
        return false;
    }
    return true;
}

double current(const State &s)
{
    return s.active ? std::exp(s.logZoom) : s.target;
}

double target(const State &s)
{
    return s.target;
}

double rate(const State &s)
{
    double r = 0.0;
    for (int i = 0; i < s.count; ++i) {
        r += s.segs[i].rate;
    }
    return r;
}

} // namespace ZoomMotion
