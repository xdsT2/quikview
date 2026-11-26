// MinimapMath 纯几何计算无窗口单测（B2：鸟瞰图显隐条件、长图保底尺寸、高亮映射与点击回算）。
#include "MinimapMath.h"

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

static bool near(qreal a, qreal b, qreal tol = 1e-6)
{
    return std::fabs(a - b) <= tol;
}

int main()
{
    // 1. 适配视口无溢出 → 隐藏
    {
        const auto l = MinimapMath::computeLayout(
            QSizeF(800, 600), QRectF(0, 0, 800, 600), 1.0, QSizeF(1000, 800));
        CHECK(!l.visible);
    }

    // 2. 放大后溢出 → 显示，缩略图等比且高亮区对应中心一半
    {
        const auto l = MinimapMath::computeLayout(
            QSizeF(2000, 1000), QRectF(500, 250, 1000, 500), 1.0, QSizeF(1000, 500));
        CHECK(l.visible);
        CHECK(near(l.thumbSize.width(), 136.0));
        CHECK(near(l.thumbSize.height(), 68.0));
        CHECK(near(l.highlightRect.x(), 34.0));
        CHECK(near(l.highlightRect.y(), 17.0));
        CHECK(near(l.highlightRect.width(), 68.0));
        CHECK(near(l.highlightRect.height(), 34.0));
    }

    // 3. 极长竖条漫（480x4800）：高限 168，宽度不低于 36px
    {
        const auto l = MinimapMath::computeLayout(
            QSizeF(480, 4800), QRectF(0, 0, 480, 720), 1.5, QSizeF(720, 1080));
        CHECK(l.visible);
        CHECK(near(l.thumbSize.height(), 168.0));
        CHECK(l.thumbSize.width() >= 36.0);
        CHECK(l.highlightRect.height() >= 6.0);
    }

    // 4. 高倍放大时指示框保底 6x6 像素可见
    {
        const auto l = MinimapMath::computeLayout(
            QSizeF(4000, 4000), QRectF(2000, 2000, 20, 20), 40.0, QSizeF(800, 800));
        CHECK(l.visible);
        CHECK(l.highlightRect.width() >= 6.0);
        CHECK(l.highlightRect.height() >= 6.0);
    }

    // 5. 缩略图点击坐标反算回场景坐标（含越界钳制）
    {
        const QPointF mid = MinimapMath::thumbPointToScene(
            QPointF(50, 25), QSizeF(100, 50), QSizeF(2000, 1000));
        CHECK(near(mid.x(), 1000.0));
        CHECK(near(mid.y(), 500.0));

        const QPointF clamped = MinimapMath::thumbPointToScene(
            QPointF(-20, 200), QSizeF(100, 50), QSizeF(2000, 1000));
        CHECK(near(clamped.x(), 0.0));
        CHECK(near(clamped.y(), 1000.0));
    }

    if (g_failures == 0) {
        std::printf("minimap.pure: PASS\n");
        return 0;
    }
    std::printf("minimap.pure: FAIL (%d)\n", g_failures);
    return 1;
}
