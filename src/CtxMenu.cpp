// CtxMenu 自绘实现（右键菜单派单 2026-11-14）。
// 几何（行高 30/内边距/分隔）沿用全局 QSS 的 QMenu 规则，这里只重画内容；
// 命中区域因此与绘制严格一致（都来自 actionGeometry）。

#include "CtxMenu.h"

#include "MenuIcons.h"
#include "UiStyle.h"

#include <QPainter>
#include <QPainterPath>

namespace {

// —— 规格取值（完整设计规格与默认开关表.md §视觉与交互）——
constexpr int kRowH        = 30;
constexpr int kIconSize    = 16;
constexpr int kIconPadX    = 8;    // 行内左右各 8px
constexpr int kIconTextGap = 8;
constexpr int kMinShortcutW = 76;  // 快捷键列最小宽
constexpr int kArrowW      = 12;   // 级联箭头
constexpr int kMenuRadius  = 10;
constexpr int kRowRadius   = 6;
constexpr int kFocusRing   = 2;    // 键盘焦点内框

// 设计指定的菜单专属色（规格 §视觉与交互；UiStyle 现有令牌没有这两个角色，
// 本文件内联登记，不改共享令牌表）。
QColor shortcutColor()   { return UiStyle::color(UiStyle::pick("#a7a9ad", "#606369")); }
QColor dangerIconColor() { return UiStyle::color(UiStyle::pick("#e15a50", "#c4382e")); }
QColor dangerRowBg()     { return UiStyle::fluent::dangerBg(); }       // 复用既有低饱和危险底

// 菜单面板底色：磨砂开时半透明（与 appQss 的磨砂替换值一致），关时纯色。
QColor menuSurface()
{
    if (UiStyle::blurMaterial()) {
        return QColor(UiStyle::isLight() ? QColor(255, 255, 255, 209) : QColor(39, 41, 45, 209));
    }
    return QColor(UiStyle::surfaceMenu());
}

QString propText(const QAction *a, const char *key)
{
    const QVariant v = a->property(key);
    return v.isValid() ? v.toString() : QString();
}

// 显示标签：appmenu.label 优先，否则取 text 中 \t 前段。
QString displayLabel(const QAction *a)
{
    const QString custom = propText(a, CtxMenu::kLabelProp);
    if (!custom.isEmpty()) {
        return custom;
    }
    QString t = a->text();
    const int tab = t.indexOf(QLatin1Char('\t'));
    if (tab >= 0) {
        t.truncate(tab);
    }
    return t;
}

// 显示快捷键：appmenu.shortcut 优先，否则 \t 后段，否则动作主键的本地写法。
QString displayShortcut(const QAction *a)
{
    const QString custom = propText(a, CtxMenu::kShortcutProp);
    if (!custom.isEmpty()) {
        return custom;
    }
    const QString t = a->text();
    const int tab = t.indexOf(QLatin1Char('\t'));
    if (tab >= 0) {
        return t.mid(tab + 1);
    }
    if (!a->shortcut().isEmpty()) {
        return a->shortcut().toString(QKeySequence::NativeText);
    }
    return QString();
}

bool hasFlag(const QAction *a, const char *key)
{
    const QVariant v = a->property(key);
    return v.isValid() && v.toBool();
}

// 菜单里是否出现级联箭头/快捷键列（按列统一：只要有一行有，整列预留）。
bool anyShortcut(const QList<QAction *> &acts)
{
    for (const QAction *a : acts) {
        if (!a->isSeparator() && !displayShortcut(a).isEmpty()) {
            return true;
        }
    }
    return false;
}

bool anySubmenu(const QList<QAction *> &acts)
{
    for (const QAction *a : acts) {
        if (a->menu()) {
            return true;
        }
    }
    return false;
}

void drawCheck(QPainter &p, const QPointF &center, const QColor &c, bool radio)
{
    if (radio) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(center, 2.6, 2.6);
        return;
    }
    QPen pen(c, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(center.x() - 3.6, center.y() + 0.2);
    path.lineTo(center.x() - 1.0, center.y() + 2.8);
    path.lineTo(center.x() + 4.0, center.y() - 2.8);
    p.drawPath(path);
}

void drawLock(QPainter &p, const QPointF &center, const QColor &c)
{
    QPen pen(c, 1.2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(center.x() - 2.6, center.y() - 1.4, 5.2, 4.4));
    p.drawArc(QRectF(center.x() - 2.0, center.y() - 4.2, 4.0, 4.0), 0, 180 * 16);
}

} // namespace

CtxMenu::CtxMenu(QWidget *parent)
    : QMenu(parent)
{
    // 标签 13px：菜单尺寸与绘制同源（QSS 的 12px 不再作用于本菜单的排版）。
    setFont(UiStyle::font(13));
}

void CtxMenu::setMinWidth(int w)
{
    m_minWidth = w;
    setMinimumWidth(w);
}

QSize CtxMenu::sizeHint() const
{
    const QList<QAction *> acts = actions();
    const QFont fLabel = UiStyle::font(13);
    const QFont fShort = UiStyle::font(11);
    const QFontMetrics fmL(fLabel);
    const QFontMetrics fmS(fShort);

    const bool shortcutCol = anyShortcut(acts);
    const bool arrowCol = anySubmenu(acts);
    int content = 0;
    for (const QAction *a : acts) {
        if (a->isSeparator()) {
            continue;
        }
        int w = kIconPadX + kIconSize + kIconTextGap + fmL.horizontalAdvance(displayLabel(a));
        if (a->isCheckable() || hasFlag(a, kRadioProp)) {
            w += 18;   // 勾/点列
        }
        if (shortcutCol) {
            w += qMax(kMinShortcutW, fmS.horizontalAdvance(displayShortcut(a)) + 10);
        }
        if (arrowCol && a->menu()) {
            w += kArrowW + 6;
        }
        w += kIconPadX;
        content = qMax(content, w);
    }
    // 外框内边距（全局 QSS QMenu padding 4px）+ 1px 描边。
    content += 8 + 2;

    QSize sh = QMenu::sizeHint();
    sh.setWidth(qMax(qMax(sh.width(), m_minWidth), content));
    return sh;
}

void CtxMenu::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // —— 面板：圆角 10 + 1px 边框，两层软影由系统/DWM 承担（NoDropShadowWindowHint
    //     + Acrylic 路径见 UiStyle::polishMenu），这里不再自绘投影。 ——
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath bgPath;
    bgPath.addRoundedRect(frame, kMenuRadius, kMenuRadius);
    p.fillPath(bgPath, menuSurface());
    QPen borderPen(UiStyle::color(UiStyle::menuLine()), 1.0);
    p.setPen(borderPen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(bgPath);

    const QList<QAction *> acts = actions();
    const QFont fLabel = UiStyle::font(13);
    const QFont fShort = UiStyle::font(11);
    const QFontMetrics fmL(fLabel);
    const QFontMetrics fmS(fShort);
    const bool shortcutCol = anyShortcut(acts);
    const bool arrowCol = anySubmenu(acts);
    const QColor colText = QColor(UiStyle::menuText());
    const QColor colDisabled = QColor(UiStyle::menuDisabled());
    const QColor colHover = QColor(UiStyle::menuHover());
    const QPoint cursor = mapFromGlobal(QCursor::pos());

    for (QAction *a : acts) {
        const QRect row = actionGeometry(a);
        if (a->isSeparator()) {
            // 分隔线 1px，左右缩进 7px，上下各留 4px（QSS 已给条目高度，这里只画线）。
            const int y = row.center().y();
            p.setPen(QPen(QColor(UiStyle::menuSeparator()), 1.0));
            p.drawLine(QPoint(row.left() + 7, y), QPoint(row.right() - 7, y));
            continue;
        }
        if (!row.isValid() || row.width() <= 0) {
            continue;
        }

        const bool enabled = a->isEnabled();
        const bool danger = hasFlag(a, kDangerProp);
        const bool hovered = row.contains(cursor);
        const bool focused = (a == activeAction());
        const bool dangerHover = danger && hovered;

        // —— 行底 ——
        QColor rowBg;
        if (dangerHover) {
            rowBg = dangerRowBg();
        } else if (hovered && enabled) {
            rowBg = colHover;
        }
        if (rowBg.isValid()) {
            QPainterPath rowPath;
            rowPath.addRoundedRect(QRectF(row).adjusted(1, 1, -1, -1), kRowRadius, kRowRadius);
            p.fillPath(rowPath, rowBg);
        }
        // —— 键盘焦点：2px 蓝色内框（悬停底可与之叠加）——
        if (focused && enabled) {
            QPainterPath ring;
            ring.addRoundedRect(QRectF(row).adjusted(1 + 1.0, 1 + 1.0, -1 - 1.0, -1 - 1.0),
                                kRowRadius, kRowRadius);
            QPen ringPen(UiStyle::interactive(), kFocusRing);
            ringPen.setJoinStyle(Qt::RoundJoin);
            p.setPen(ringPen);
            p.setBrush(Qt::NoBrush);
            p.drawPath(ring);
        }

        // —— 列布局（规格：图标 16 → 8px → 标签 13px → 勾/点列 → 快捷键列(≥76) → 箭头）——
        const qreal iconX = row.left() + kIconPadX;
        const qreal iconY = row.center().y() - kIconSize / 2.0;
        qreal x = iconX + kIconSize + kIconTextGap;
        const QString label = displayLabel(a);
        const QString shortcut = displayShortcut(a);

        qreal arrowRight = 0;
        if (arrowCol && a->menu()) {
            arrowRight = row.right() - kIconPadX;
        }
        qreal shortcutRight = arrowRight > 0 ? arrowRight - kArrowW - 6 : row.right() - kIconPadX;
        qreal shortcutLeft = 0;
        if (shortcutCol) {
            const int sw = qMax(kMinShortcutW, fmS.horizontalAdvance(shortcut) + 10);
            shortcutLeft = shortcutRight - sw;
        }

        // 图标（危险行固定危险色；禁用灰；正常主文色）
        const QString iconId = propText(a, kIconProp);
        if (!iconId.isEmpty()) {
            const QColor ic = !enabled ? colDisabled
                            : danger   ? dangerIconColor()
                                       : colText;
            MenuIcons::paint(p, iconId, QRectF(iconX, iconY, kIconSize, kIconSize), ic);
        }

        // 标签（危险行文字保持中性主文色，规格 §视觉与交互）
        p.setFont(fLabel);
        p.setPen(enabled ? colText : colDisabled);
        QRectF labelRect(x, row.top(), 0, row.height());
        int labelAvail = int(row.width());
        if (shortcutCol) {
            labelAvail = int(shortcutLeft - x - 6);
        }
        p.drawText(QRectF(x, row.top(), labelAvail, row.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
        x += fmL.horizontalAdvance(label);
        if (hasFlag(a, kLockProp)) {
            drawLock(p, QPointF(x + 8, row.center().y()), shortcutColor());
        }

        // 勾 / 单选点：仅勾选中的行绘制，位置在快捷键列左侧独立列
        //（无快捷键列时贴右缘）
        if (a->isCheckable() && a->isChecked()) {
            const qreal markX = shortcutCol && shortcutLeft > 0 ? shortcutLeft - 6 : shortcutRight - 10;
            drawCheck(p, QPointF(markX, row.center().y()),
                      enabled ? colText : colDisabled, hasFlag(a, kRadioProp));
        }

        // 快捷键（11px 右对齐）
        if (shortcutCol && !shortcut.isEmpty()) {
            p.setFont(fShort);
            p.setPen(enabled ? shortcutColor() : colDisabled);
            p.drawText(QRectF(shortcutLeft, row.top(), shortcutRight - shortcutLeft, row.height()),
                       Qt::AlignVCenter | Qt::AlignRight, shortcut);
        }

        // 级联箭头 12px
        if (arrowRight > 0) {
            const QPointF tip(arrowRight - kArrowW + 3.0, row.center().y());
            QPen pen(enabled ? shortcutColor() : colDisabled, 1.5);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            QPainterPath path;
            path.moveTo(tip.x() - 3.6, tip.y() - 4.2);
            path.lineTo(tip.x() + 1.2, tip.y());
            path.lineTo(tip.x() - 3.6, tip.y() + 4.2);
            p.drawPath(path);
        }
    }
}
