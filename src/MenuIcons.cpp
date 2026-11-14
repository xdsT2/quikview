// 中性线性图标实现（右键菜单派单 2026-11-14）。
// 全部用 QPainterPath 直线/圆弧画在 16px 网格上；线宽 1.45、圆角端点，
// 与设计图（全量配置修订 01–11）的细线图标对齐。

#include "MenuIcons.h"

#include <QPainterPath>
#include <QSet>
#include <QtMath>

namespace {

constexpr qreal kStroke = 1.45;   // 规格线宽（16px 网格）

struct G {
    QPainter &p;
    explicit G(QPainter &pp) : p(pp) {}

    QPainterPath line(qreal x1, qreal y1, qreal x2, qreal y2) const
    {
        QPainterPath path;
        path.moveTo(x1, y1);
        path.lineTo(x2, y2);
        return path;
    }
    QPainterPath poly(std::initializer_list<QPointF> pts, bool closed = false) const
    {
        QPainterPath path;
        bool first = true;
        for (const QPointF &pt : pts) {
            if (first) { path.moveTo(pt); first = false; }
            else       { path.lineTo(pt); }
        }
        if (closed) {
            path.closeSubpath();
        }
        return path;
    }
    QPainterPath circle(const QPointF &c, qreal r) const
    {
        QPainterPath path;
        path.addEllipse(c, r, r);
        return path;
    }
    QPainterPath rrect(const QRectF &r, qreal radius) const
    {
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);
        return path;
    }
    QPainterPath arcC(const QPointF &c, qreal r, qreal startDeg, qreal sweepDeg) const
    {
        // Qt 角度：逆时针为正、0° 在 3 点钟方向。
        QPainterPath path;
        path.arcMoveTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), startDeg);
        path.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), startDeg, sweepDeg);
        return path;
    }
    // 折线小箭头：pt 为箭尖，dir 为指向（1=右/-1=左/2=下/-2=上）
    QPainterPath chev(const QPointF &tip, int dir, qreal size = 3.0) const
    {
        switch (dir) {
        case 1:  return poly({{tip.x() - size, tip.y() - size}, tip, {tip.x() - size, tip.y() + size}});
        case -1: return poly({{tip.x() + size, tip.y() - size}, tip, {tip.x() + size, tip.y() + size}});
        case 2:  return poly({{tip.x() - size, tip.y() - size}, tip, {tip.x() + size, tip.y() - size}});
        default: return poly({{tip.x() - size, tip.y() + size}, tip, {tip.x() + size, tip.y() + size}});
        }
    }
    // 山形（图片内容示意）
    QPainterPath mountains(const QRectF &r) const
    {
        QPainterPath path;
        path.moveTo(r.left(), r.bottom() - 1.5);
        path.lineTo(r.left() + r.width() * 0.34, r.top() + r.height() * 0.42);
        path.lineTo(r.left() + r.width() * 0.55, r.bottom() - 3.0);
        path.moveTo(r.left() + r.width() * 0.45, r.bottom() - 1.5);
        path.lineTo(r.left() + r.width() * 0.70, r.top() + r.height() * 0.55);
        path.lineTo(r.right(), r.bottom() - 1.5);
        return path;
    }
};

void stroke(QPainter &p, const QPainterPath &path, const QColor &c)
{
    QPen pen(c, kStroke);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path);
}

void fill(QPainter &p, const QPainterPath &path, const QColor &c)
{
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawPath(path);
}

} // namespace

namespace MenuIcons {

bool paint(QPainter &p, const QString &id, const QRectF &rect, const QColor &color)
{
    // 未知 id 直接失败，调用方回退（宁可缺图标也不画错图）。
    static const QSet<QString> kKnown = {
        QStringLiteral("image"), QStringLiteral("folder-open"), QStringLiteral("images"),
        QStringLiteral("folder-target"), QStringLiteral("info"), QStringLiteral("copy"),
        QStringLiteral("pin"), QStringLiteral("browse"), QStringLiteral("nav"),
        QStringLiteral("chev-right"), QStringLiteral("chev-left"), QStringLiteral("first"),
        QStringLiteral("last"), QStringLiteral("shuffle"), QStringLiteral("folder-next"),
        QStringLiteral("folder-prev"), QStringLiteral("sort"), QStringLiteral("text"),
        QStringLiteral("clock"), QStringLiteral("size"), QStringLiteral("tag"),
        QStringLiteral("sort-desc"), QStringLiteral("view"), QStringLiteral("frame"),
        QStringLiteral("fit-window"), QStringLiteral("fit-width"), QStringLiteral("book"),
        QStringLiteral("zoom"), QStringLiteral("zoom-in"), QStringLiteral("zoom-out"),
        QStringLiteral("lock"), QStringLiteral("imageproc"), QStringLiteral("interp"),
        QStringLiteral("sparkle"), QStringLiteral("invert"), QStringLiteral("eq"),
        QStringLiteral("flip-v"), QStringLiteral("flip-h"), QStringLiteral("rotate"),
        QStringLiteral("rotate-cw"), QStringLiteral("transition"), QStringLiteral("minus"),
        QStringLiteral("folder"), QStringLiteral("monitor"), QStringLiteral("copy-more"),
        QStringLiteral("folder-copy"), QStringLiteral("open-with"), QStringLiteral("pencil"),
        QStringLiteral("printer"), QStringLiteral("trash"), QStringLiteral("sliders"),
        QStringLiteral("x"), QStringLiteral("logout"), QStringLiteral("save"),
        QStringLiteral("play"), QStringLiteral("stop"), QStringLiteral("bookmark"),
        QStringLiteral("crop"), QStringLiteral("moon"), QStringLiteral("layers"),
    };
    if (!kKnown.contains(id)) {
        return false;
    }

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    // 16px 网格 → rect；留 0.5px 线宽余量避免裁边。
    const qreal s = qMin(rect.width(), rect.height()) / 16.0;
    p.translate(rect.left() + (rect.width() - 16 * s) / 2.0,
                rect.top() + (rect.height() - 16 * s) / 2.0);
    p.scale(s, s);
    G g(p);

    if (id == QLatin1String("image")) {
        stroke(p, g.rrect(QRectF(1.5, 2.5, 13, 11), 2), color);
        stroke(p, g.mountains(QRectF(2, 3, 12, 10)), color);
        fill(p, g.circle(QPointF(10.6, 6.0), 1.15), color);
    } else if (id == QLatin1String("folder-open") || id == QLatin1String("folder")) {
        // 打开文件夹：后板 + 前板外翻；普通文件夹：单层
        if (id == QLatin1String("folder-open")) {
            stroke(p, g.poly({{1.5, 12.5}, {1.5, 3.5,}, {6.0, 3.5}, {7.5, 5.5}, {12.0, 5.5}}), color);
            stroke(p, g.poly({{1.5, 12.5}, {4.0, 8.0}, {14.5, 8.0}, {12.0, 12.5}, {1.5, 12.5}}), color);
        } else {
            stroke(p, g.poly({{1.5, 12.5}, {1.5, 3.5}, {6.0, 3.5}, {7.5, 5.5}, {14.5, 5.5}, {14.5, 12.5}}, true), color);
        }
    } else if (id == QLatin1String("images")) {
        stroke(p, g.rrect(QRectF(4.5, 1.5, 10, 10), 2), color);
        stroke(p, g.poly({{2.0, 5.0}, {2.0, 14.0}, {11.5, 14.0}}), color);
    } else if (id == QLatin1String("folder-target")) {
        stroke(p, g.poly({{1.5, 13.5}, {1.5, 3.0}, {6.5, 3.0}, {8.5, 5.0}, {10.5, 5.0}}), color);
        stroke(p, g.poly({{10.5, 5.0}, {10.5, 2.0}, {14.5, 2.0}, {14.5, 5.0}}), color);
    } else if (id == QLatin1String("info")) {
        stroke(p, g.circle(QPointF(8, 8), 6.2), color);
        stroke(p, g.line(8, 7.2, 8, 11.2), color);
        fill(p, g.circle(QPointF(8, 4.9), 0.85), color);
    } else if (id == QLatin1String("copy") || id == QLatin1String("copy-more")) {
        stroke(p, g.rrect(QRectF(5.5, 1.5, 9, 9), 2), color);
        stroke(p, g.poly({{2.0, 5.5}, {2.0, 14.5}, {11.0, 14.5}}), color);
        if (id == QLatin1String("copy-more")) {
            stroke(p, g.line(7.5, 10.0, 7.5, 13.5), color);
            stroke(p, g.line(5.75, 11.75, 9.25, 11.75), color);
        }
    } else if (id == QLatin1String("pin")) {
        stroke(p, g.line(9.6, 1.8, 14.2, 6.4), color);
        stroke(p, g.poly({{9.6, 1.8}, {6.8, 4.6}, {11.4, 9.2}, {14.2, 6.4}}, true), color);
        stroke(p, g.line(7.6, 8.4, 2.2, 13.8), color);
    } else if (id == QLatin1String("browse") || id == QLatin1String("view")) {
        stroke(p, g.rrect(QRectF(1.8, 3.0, 12.4, 10.0), 2), color);
        stroke(p, g.line(4.4, 6.2, 11.6, 6.2), color);
        stroke(p, g.line(4.4, 9.8, 9.0, 9.8), color);
    } else if (id == QLatin1String("nav")) {
        stroke(p, g.line(4.0, 8.0, 12.0, 8.0), color);
        stroke(p, g.chev(QPointF(2.6, 8.0), -1), color);
        stroke(p, g.chev(QPointF(13.4, 8.0), 1), color);
    } else if (id == QLatin1String("chev-right") || id == QLatin1String("last")) {
        if (id == QLatin1String("last")) {
            stroke(p, g.line(12.4, 3.5, 12.4, 12.5), color);
            stroke(p, g.chev(QPointF(6.4, 8.0), 1, 3.4), color);
        } else {
            stroke(p, g.chev(QPointF(8.0, 8.0), 1, 3.6), color);
        }
    } else if (id == QLatin1String("chev-left") || id == QLatin1String("first")) {
        if (id == QLatin1String("first")) {
            stroke(p, g.line(3.6, 3.5, 3.6, 12.5), color);
            stroke(p, g.chev(QPointF(9.6, 8.0), -1, 3.4), color);
        } else {
            stroke(p, g.chev(QPointF(8.0, 8.0), -1, 3.6), color);
        }
    } else if (id == QLatin1String("shuffle")) {
        stroke(p, g.poly({{1.8, 4.0}, {5.0, 4.0}, {11.0, 12.0}, {14.0, 12.0}}), color);
        stroke(p, g.poly({{1.8, 12.0}, {5.0, 12.0}, {11.0, 4.0}, {14.0, 4.0}}), color);
        stroke(p, g.chev(QPointF(13.0, 10.4), 1, 2.2), color);
        stroke(p, g.chev(QPointF(13.0, 5.6), 1, 2.2), color);
    } else if (id == QLatin1String("folder-next") || id == QLatin1String("folder-prev")) {
        stroke(p, g.poly({{1.5, 13.0}, {1.5, 3.5}, {6.0, 3.5}, {7.5, 5.5}, {14.5, 5.5}, {14.5, 13.0}}, true), color);
        stroke(p, g.chev(QPointF(id == QLatin1String("folder-next") ? 10.8 : 5.2, 9.2),
                         id == QLatin1String("folder-next") ? 1 : -1, 2.2), color);
    } else if (id == QLatin1String("sort") || id == QLatin1String("sort-desc")) {
        if (id == QLatin1String("sort")) {
            stroke(p, g.line(2.5, 4.5, 9.5, 4.5), color);
            stroke(p, g.line(2.5, 8.0, 7.5, 8.0), color);
            stroke(p, g.line(2.5, 11.5, 5.5, 11.5), color);
            stroke(p, g.line(12.6, 3.6, 12.6, 12.4), color);
            stroke(p, g.chev(QPointF(12.6, 13.2), 2, 2.4), color);
        } else {
            stroke(p, g.line(8.0, 2.8, 8.0, 12.6), color);
            stroke(p, g.chev(QPointF(8.0, 13.4), 2, 2.6), color);
        }
    } else if (id == QLatin1String("text")) {
        stroke(p, g.line(5.0, 3.0, 11.0, 3.0), color);
        stroke(p, g.line(5.0, 13.0, 11.0, 13.0), color);
        stroke(p, g.line(8.0, 3.0, 8.0, 13.0), color);
    } else if (id == QLatin1String("clock")) {
        stroke(p, g.circle(QPointF(8, 8), 6.2), color);
        stroke(p, g.poly({{8.0, 4.6}, {8.0, 8.4}, {10.6, 10.0}}), color);
    } else if (id == QLatin1String("size")) {
        stroke(p, g.rrect(QRectF(2.0, 2.0, 12, 12), 2), color);
        stroke(p, g.line(5.6, 10.4, 10.4, 5.6), color);
        stroke(p, g.chev(QPointF(5.6, 7.6), -2, 1.9), color);
        stroke(p, g.chev(QPointF(10.4, 8.4), 2, 1.9), color);
    } else if (id == QLatin1String("tag")) {
        stroke(p, g.poly({{8.6, 2.0}, {14.0, 2.0}, {14.0, 7.4}, {8.0, 13.4}, {2.6, 8.0}}, true), color);
        fill(p, g.circle(QPointF(11.2, 4.8), 1.0), color);
    } else if (id == QLatin1String("frame")) {
        stroke(p, g.rrect(QRectF(2.0, 2.0, 12, 12), 2), color);
        stroke(p, g.line(5.4, 10.6, 10.6, 5.4), color);
    } else if (id == QLatin1String("fit-window")) {
        stroke(p, g.poly({{2.0, 6.0}, {2.0, 2.0}, {6.0, 2.0}}), color);
        stroke(p, g.poly({{10.0, 14.0}, {14.0, 14.0}, {14.0, 10.0}}), color);
        stroke(p, g.line(4.6, 11.4, 11.4, 4.6), color);
    } else if (id == QLatin1String("fit-width")) {
        stroke(p, g.poly({{2.0, 6.0}, {2.0, 2.0}, {6.0, 2.0}}), color);
        stroke(p, g.poly({{10.0, 14.0}, {14.0, 14.0}, {14.0, 10.0}}), color);
        stroke(p, g.line(4.6, 8.0, 11.4, 8.0), color);
        stroke(p, g.chev(QPointF(3.6, 8.0), -1, 1.9), color);
        stroke(p, g.chev(QPointF(12.4, 8.0), 1, 1.9), color);
    } else if (id == QLatin1String("book")) {
        stroke(p, g.rrect(QRectF(1.8, 3.0, 5.8, 10.0), 1.4), color);
        stroke(p, g.rrect(QRectF(8.4, 3.0, 5.8, 10.0), 1.4), color);
    } else if (id == QLatin1String("zoom") || id == QLatin1String("zoom-in") || id == QLatin1String("zoom-out")) {
        stroke(p, g.circle(QPointF(7.0, 7.0), 4.8), color);
        stroke(p, g.line(10.4, 10.4, 14.0, 14.0), color);
        if (id == QLatin1String("zoom-in")) {
            stroke(p, g.line(5.0, 7.0, 9.0, 7.0), color);
            stroke(p, g.line(7.0, 5.0, 7.0, 9.0), color);
        } else if (id == QLatin1String("zoom-out")) {
            stroke(p, g.line(5.0, 7.0, 9.0, 7.0), color);
        } else {
            stroke(p, g.line(4.6, 7.0, 9.4, 7.0), color);
            stroke(p, g.line(7.0, 4.6, 7.0, 9.4), color);
        }
    } else if (id == QLatin1String("lock")) {
        stroke(p, g.rrect(QRectF(3.2, 7.0, 9.6, 7.0), 1.8), color);
        stroke(p, g.arcC(QPointF(8.0, 7.0), 3.0, 180.0, 180.0), color);
    } else if (id == QLatin1String("imageproc")) {
        stroke(p, g.rrect(QRectF(4.5, 1.5, 10, 10), 2), color);
        stroke(p, g.rrect(QRectF(1.5, 4.5, 10, 10), 2), color);
    } else if (id == QLatin1String("interp")) {
        stroke(p, g.rrect(QRectF(2.0, 2.0, 12, 12), 1.6), color);
        stroke(p, g.line(8.0, 2.0, 8.0, 14.0), color);
        stroke(p, g.line(2.0, 8.0, 14.0, 8.0), color);
    } else if (id == QLatin1String("sparkle")) {
        QPainterPath path;
        path.moveTo(8.0, 1.6);
        path.cubicTo(8.9, 5.6, 10.4, 7.1, 14.4, 8.0);
        path.cubicTo(10.4, 8.9, 8.9, 10.4, 8.0, 14.4);
        path.cubicTo(7.1, 10.4, 5.6, 8.9, 1.6, 8.0);
        path.cubicTo(5.6, 7.1, 7.1, 5.6, 8.0, 1.6);
        stroke(p, path, color);
    } else if (id == QLatin1String("invert")) {
        QPainterPath full, half;
        full.addEllipse(QPointF(8, 8), 6.2, 6.2);
        stroke(p, full, color);
        half.moveTo(8.0, 1.8);
        half.arcTo(QRectF(1.8, 1.8, 12.4, 12.4), 90, 180);
        half.closeSubpath();
        fill(p, half, color);
    } else if (id == QLatin1String("eq")) {
        stroke(p, g.line(2.5, 5.5, 13.5, 5.5), color);
        stroke(p, g.line(2.5, 10.5, 13.5, 10.5), color);
        fill(p, g.circle(QPointF(10.0, 5.5), 1.3), color);
        fill(p, g.circle(QPointF(6.0, 10.5), 1.3), color);
    } else if (id == QLatin1String("flip-v") || id == QLatin1String("flip-h")) {
        const bool vert = (id == QLatin1String("flip-v"));
        // 两个三角 + 中间分隔虚线
        if (vert) {
            stroke(p, g.poly({{4.5, 6.5}, {11.5, 6.5}, {8.0, 2.2}}, true), color);
            stroke(p, g.poly({{4.5, 9.5}, {11.5, 9.5}, {8.0, 13.8}}, true), color);
        } else {
            stroke(p, g.poly({{6.5, 4.5}, {6.5, 11.5}, {2.2, 8.0}}, true), color);
            stroke(p, g.poly({{9.5, 4.5}, {9.5, 11.5}, {13.8, 8.0}}, true), color);
        }
    } else if (id == QLatin1String("rotate") || id == QLatin1String("rotate-cw")) {
        // 圆弧留缺口，箭头按弧线切向收口（逆时针 / 顺时针各一份）。
        const bool ccw = (id == QLatin1String("rotate"));
        const QPointF c(8.0, 8.4);
        const qreal r = 5.0;
        const qreal start = ccw ? 55.0 : 305.0;
        const qreal sweep = ccw ? 290.0 : -290.0;
        stroke(p, g.arcC(c, r, start, sweep), color);
        const qreal aEnd = qDegreesToRadians(start + sweep);
        const QPointF tip(c.x() + r * std::cos(aEnd), c.y() - r * std::sin(aEnd));
        // 切向角：弧线末端前进方向（逆时针 = 切角 +90，顺时针 = 切角 -90）
        const qreal tang = aEnd + (ccw ? M_PI_2 : -M_PI_2);
        QPainterPath head;
        head.moveTo(tip.x() - 2.6 * std::cos(tang - 0.44), tip.y() + 2.6 * std::sin(tang - 0.44));
        head.lineTo(tip);
        head.lineTo(tip.x() - 2.6 * std::cos(tang + 0.44), tip.y() + 2.6 * std::sin(tang + 0.44));
        stroke(p, head, color);
    } else if (id == QLatin1String("transition")) {
        QPainterPath path;
        path.moveTo(1.8, 8.0);
        path.cubicTo(4.0, 3.4, 6.0, 3.4, 8.0, 8.0);
        path.cubicTo(10.0, 12.6, 12.0, 12.6, 14.2, 8.0);
        stroke(p, path, color);
    } else if (id == QLatin1String("minus")) {
        stroke(p, g.line(3.0, 8.0, 13.0, 8.0), color);
    } else if (id == QLatin1String("monitor")) {
        stroke(p, g.rrect(QRectF(1.8, 2.5, 12.4, 8.6), 1.6), color);
        stroke(p, g.mountains(QRectF(3.0, 3.6, 10.0, 6.6)), color);
        stroke(p, g.line(8.0, 11.1, 8.0, 13.4), color);
        stroke(p, g.line(5.2, 13.6, 10.8, 13.6), color);
    } else if (id == QLatin1String("open-with")) {
        stroke(p, g.poly({{2.0, 6.5}, {2.0, 13.5}, {9.5, 13.5}}), color);
        stroke(p, g.rrect(QRectF(6.5, 2.0, 7.5, 7.5), 1.8), color);
        stroke(p, g.line(9.0, 7.0, 13.6, 2.4), color);
        stroke(p, g.chev(QPointF(13.8, 4.6), 2, 1.8), color);
    } else if (id == QLatin1String("pencil")) {
        stroke(p, g.poly({{3.0, 13.0}, {3.6, 10.2}, {11.2, 2.6}, {13.4, 4.8}, {5.8, 12.4}, {3.0, 13.0}}, true), color);
        stroke(p, g.line(10.0, 3.8, 12.2, 6.0), color);
    } else if (id == QLatin1String("printer")) {
        stroke(p, g.poly({{4.0, 5.0}, {4.0, 1.8}, {12.0, 1.8}, {12.0, 5.0}}), color);
        stroke(p, g.rrect(QRectF(1.8, 5.0, 12.4, 5.6), 1.6), color);
        stroke(p, g.rrect(QRectF(4.0, 9.0, 8.0, 5.4), 1.0), color);
        fill(p, g.circle(QPointF(12.2, 7.0), 0.7), color);
    } else if (id == QLatin1String("trash")) {
        stroke(p, g.line(2.2, 4.2, 13.8, 4.2), color);
        stroke(p, g.poly({{5.5, 4.0}, {5.5, 1.8}, {10.5, 1.8}, {10.5, 4.0}}), color);
        stroke(p, g.poly({{3.6, 4.2}, {4.6, 14.2}, {11.4, 14.2}, {12.4, 4.2}}), color);
        stroke(p, g.line(6.6, 6.8, 6.9, 11.6), color);
        stroke(p, g.line(9.4, 6.8, 9.1, 11.6), color);
    } else if (id == QLatin1String("sliders")) {
        stroke(p, g.line(2.0, 4.0, 14.0, 4.0), color);
        stroke(p, g.line(2.0, 8.0, 14.0, 8.0), color);
        stroke(p, g.line(2.0, 12.0, 14.0, 12.0), color);
        fill(p, g.circle(QPointF(10.4, 4.0), 1.35), color);
        fill(p, g.circle(QPointF(5.6, 8.0), 1.35), color);
        fill(p, g.circle(QPointF(9.0, 12.0), 1.35), color);
    } else if (id == QLatin1String("x")) {
        stroke(p, g.line(3.4, 3.4, 12.6, 12.6), color);
        stroke(p, g.line(12.6, 3.4, 3.4, 12.6), color);
    } else if (id == QLatin1String("save")) {
        stroke(p, g.poly({{2, 2}, {11.5, 2}, {14, 4.5}, {14, 14}, {2, 14}}, true), color);
        stroke(p, g.poly({{5, 2}, {5, 5.5}, {10.5, 5.5}, {10.5, 2}}), color);
        stroke(p, g.poly({{5, 14}, {5, 9}, {11, 9}, {11, 14}}), color);
    } else if (id == QLatin1String("play")) {
        stroke(p, g.poly({{4.5, 2.5}, {13, 8}, {4.5, 13.5}}, true), color);
    } else if (id == QLatin1String("stop")) {
        stroke(p, g.rrect(QRectF(3, 3, 10, 10), 1.5), color);
    } else if (id == QLatin1String("bookmark")) {
        stroke(p, g.poly({{4, 2}, {12, 2}, {12, 14}, {8, 10.5}, {4, 14}}, true), color);
    } else if (id == QLatin1String("crop")) {
        stroke(p, g.poly({{4.5, 1.5}, {4.5, 11.5}, {14.5, 11.5}}), color);
        stroke(p, g.poly({{1.5, 4.5}, {11.5, 4.5}, {11.5, 14.5}}), color);
    } else if (id == QLatin1String("moon")) {
        QPainterPath moon;
        moon.moveTo(8.5, 2);
        moon.cubicTo(5.5, 7.3, 9.2, 11.9, 14, 9.4);
        moon.cubicTo(12.7, 14.8, 5.4, 15.2, 2.8, 10.7);
        moon.cubicTo(0.5, 6.7, 3.5, 2, 8.5, 2);
        stroke(p, moon, color);
    } else if (id == QLatin1String("layers")) {
        stroke(p, g.poly({{1.5, 5.5}, {8, 2}, {14.5, 5.5}, {8, 9}}, true), color);
        stroke(p, g.poly({{1.5, 8.5}, {8, 12}, {14.5, 8.5}}), color);
        stroke(p, g.poly({{1.5, 11.5}, {8, 15}, {14.5, 11.5}}), color);
    } else if (id == QLatin1String("logout")) {
        stroke(p, g.poly({{7.0, 2.0}, {2.5, 2.0}, {2.5, 14.0}, {7.0, 14.0}}), color);
        stroke(p, g.line(6.5, 8.0, 13.5, 8.0), color);
        stroke(p, g.chev(QPointF(12.4, 8.0), 1, 2.4), color);
    } else {
        p.restore();
        return false;
    }

    p.restore();
    return true;
}

QPixmap pixmap(const QString &id, const QColor &color, int px, qreal dpr)
{
    QPixmap pm(int(px * dpr), int(px * dpr));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(p, id, QRectF(0, 0, px, px), color);
    p.end();
    return pm;
}

QIcon icon(const QString &id, const QColor &color)
{
    QIcon ic;
    ic.addPixmap(pixmap(id, color, 16, 1.0));
    ic.addPixmap(pixmap(id, color, 16, 2.0));
    return ic;
}

} // namespace MenuIcons
