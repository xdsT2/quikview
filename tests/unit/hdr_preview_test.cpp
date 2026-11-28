// HdrPreview 纯逻辑单测（需求池 I3-2 / BUG-20261128-03）：
// 自动白点要排除纯黑像素、天空这类高亮度 HDR 不能再整片压白、全黑图不能炸亮。
#include "HdrPreview.h"

#include <cmath>
#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

int main()
{
    // 1. 全黑 / NaN 不进统计：白点回退 1.0，曲线不炸亮暗部。
    {
        HdrPreview::Meter m;
        m.add(0.0f, 0.0f, 0.0f);
        m.add(-1.0f, -1.0f, -1.0f);       // 负亮度（超出色域）同样不计入
        m.add(std::nanf(""), 1.0f, 1.0f);
        CHECK(m.count() == 0);
        CHECK(std::fabs(m.whitePoint() - 1.0f) < 1e-6f);

        const HdrPreview::Curve c(1.0f);
        CHECK(c(0.0f) == 0);
        CHECK(c(-5.0f) == 0);
        CHECK(c(1.0f) >= 250);            // 白点落在 255 附近
        CHECK(c(100.0f) == 255);
    }

    // 2. 白点 = 有效非黑亮的 99.5% 分位；纯黑像素不参与，不会把白点拉低。
    {
        HdrPreview::Meter m;
        // 950 个 0.5，40 个 4.0，10 个 40.0，再混 1000 个纯黑
        for (int i = 0; i < 950; ++i) m.add(0.5f, 0.5f, 0.5f);
        for (int i = 0; i < 40; ++i) m.add(4.0f, 4.0f, 4.0f);
        for (int i = 0; i < 10; ++i) m.add(40.0f, 40.0f, 40.0f);
        for (int i = 0; i < 1000; ++i) m.add(0.0f, 0.0f, 0.0f);
        CHECK(m.count() == 1000);
        // 1000 个样本的 99.5% 分位在 40.0 那一段
        CHECK(m.whitePoint() > 3.9f);
        CHECK(m.whitePoint() <= 40.0f);
    }

    // 3. 天空样本回归：中位 3.2、p90 7.4、p99 26.6、p99.9 200 的分布，
    //    旧曲线（白点 1.0）会把 3.2 直接压到 250+；新曲线必须留出层次。
    {
        HdrPreview::Meter m;
        for (int i = 0; i < 880; ++i) m.addLuma(3.203f);
        for (int i = 0; i < 90; ++i) m.addLuma(7.449f);
        for (int i = 0; i < 29; ++i) m.addLuma(26.594f);
        for (int i = 0; i < 1; ++i) m.addLuma(200.0f);
        const float white = m.whitePoint();
        const HdrPreview::Curve c(white);

        // 旧行为对照：白点 1.0 时中位亮度已经 250+（这就是用户看到的死白）
        const HdrPreview::Curve oldCurve(1.0f);
        CHECK(oldCurve(3.203f) >= 250);

        // 新行为：中位回到中间调，高光仍亮但不再整片纯白，且严格单调。
        CHECK(c(3.203f) < 200);
        CHECK(c(3.203f) > 60);
        CHECK(c(7.449f) > c(3.203f));
        CHECK(c(26.594f) >= 230);
        CHECK(c(200.0f) == 255);
        for (float v = 0.05f; v < 50.0f; v *= 1.07f) {
            CHECK(c(v) <= c(v * 1.07f));
        }
    }

    // 4. 曲线单调性与端点（白点 4.0 的常规夜景）。
    {
        const HdrPreview::Curve c(4.0f);
        unsigned char prev = 0;
        for (int i = 0; i <= 400; ++i) {
            const float lin = float(i) * 0.05f;   // 0 ~ 20
            const unsigned char v = c(lin);
            CHECK(v >= prev);
            prev = v;
        }
        CHECK(c(4.0f) >= 250);
    }

    if (g_failures == 0) {
        std::printf("hdrpreview.pure: PASS\n");
        return 0;
    }
    std::fprintf(stderr, "hdrpreview.pure: %d failure(s)\n", g_failures);
    return 1;
}
