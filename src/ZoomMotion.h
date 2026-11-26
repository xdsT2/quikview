#pragma once

// 画布平滑缩放的纯算术引擎（D1）。
// 借鉴 core-ui/src/ui/zoom_motion（MIT 许可）的 log-space 匀速叠加算法：
//   - 每格缩放 = log(zoom) 空间内固定时长的匀速运动，视觉缩放速度恒定；
//   - 连续滚轮/连按多段速度叠加，终点精确等于逐格乘积；
//   - 新目标反向时立即清空积压段，从当前实时比例平滑折返；
//   - 按真实经过毫秒 dtMs 推进，帧率无关。
namespace ZoomMotion {

constexpr int kMaxSegments = 32;

struct Segment {
    double remainingMs = 0.0;
    double rate = 0.0;   // d(logZoom)/ms
};

struct State {
    double logZoom = 0.0;
    double target = 1.0;
    Segment segs[kMaxSegments];
    int count = 0;
    bool active = false;
};

void reset(State &s, double zoom = 1.0);
void push(State &s, double current, double target, double durationMs);
bool step(State &s, double dtMs);

double current(const State &s);
double target(const State &s);
double rate(const State &s);

} // namespace ZoomMotion
