// ZoomMotion 纯算术引擎无窗口单测（D1：log 空间匀速叠加、反向清积压、帧率无关）。
#include "ZoomMotion.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

static bool near(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) <= tol;
}

int main()
{
    // 1. 单步收敛到精确目标
    {
        ZoomMotion::State s;
        ZoomMotion::push(s, 1.0, 1.25, 120.0);
        CHECK(s.active);
        CHECK(ZoomMotion::step(s, 60.0));
        // log 空间中点 = sqrt(1.0 * 1.25)
        CHECK(near(ZoomMotion::current(s), std::sqrt(1.25), 1e-6));
        CHECK(!ZoomMotion::step(s, 60.0));
        CHECK(!s.active);
        CHECK(near(ZoomMotion::current(s), 1.25, 1e-12));
    }

    // 2. 连按三格放大：终点精确等于 1.25^3
    {
        ZoomMotion::State s;
        ZoomMotion::push(s, 1.0, 1.25, 120.0);
        ZoomMotion::step(s, 30.0);
        ZoomMotion::push(s, ZoomMotion::current(s), 1.25 * 1.25, 120.0);
        ZoomMotion::step(s, 30.0);
        const double finalTarget = 1.25 * 1.25 * 1.25;
        ZoomMotion::push(s, ZoomMotion::current(s), finalTarget, 120.0);
        CHECK(s.count == 3);
        while (ZoomMotion::step(s, 16.0)) {
        }
        CHECK(!s.active);
        CHECK(near(ZoomMotion::current(s), finalTarget, 1e-12));
    }

    // 3. 中途反向：清掉积压段，立刻折返
    {
        ZoomMotion::State s;
        ZoomMotion::push(s, 1.0, 2.0, 120.0);
        ZoomMotion::push(s, 1.0, 4.0, 120.0);
        ZoomMotion::step(s, 40.0);
        const double mid = ZoomMotion::current(s);
        CHECK(mid > 1.0 && mid < 4.0);
        ZoomMotion::push(s, mid, mid * 0.8, 120.0);
        CHECK(s.count == 1);
        CHECK(ZoomMotion::rate(s) < 0.0);
        while (ZoomMotion::step(s, 20.0)) {
        }
        CHECK(near(ZoomMotion::current(s), mid * 0.8, 1e-12));
    }

    // 4. 微小位移不起动画
    {
        ZoomMotion::State s;
        ZoomMotion::push(s, 1.0, 1.0 + 1e-11, 120.0);
        CHECK(!s.active);
        CHECK(s.count == 0);
    }

    // 5. 超过 kMaxSegments (32) 段时位移守恒
    {
        ZoomMotion::State s;
        double target = 1.0;
        for (int i = 0; i < 40; ++i) {
            target *= 1.05;
            ZoomMotion::push(s, 1.0, target, 120.0);
        }
        CHECK(s.count == ZoomMotion::kMaxSegments);
        while (ZoomMotion::step(s, 15.0)) {
        }
        CHECK(near(ZoomMotion::current(s), target, 1e-9));
    }

    // 6. 帧率无关性：4×30ms 与 12×10ms 进度一致
    {
        ZoomMotion::State a;
        ZoomMotion::State b;
        ZoomMotion::push(a, 1.0, 2.0, 160.0);
        ZoomMotion::push(b, 1.0, 2.0, 160.0);
        for (int i = 0; i < 4; ++i) {
            ZoomMotion::step(a, 30.0);
        }
        for (int i = 0; i < 12; ++i) {
            ZoomMotion::step(b, 10.0);
        }
        CHECK(near(ZoomMotion::current(a), ZoomMotion::current(b), 1e-9));
    }

    if (g_failures == 0) {
        std::printf("zoommotion.pure: PASS\n");
        return 0;
    }
    std::printf("zoommotion.pure: FAIL (%d)\n", g_failures);
    return 1;
}
