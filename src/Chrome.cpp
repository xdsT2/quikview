#include "Chrome.h"
#include "Config.h"
#include "UiStyle.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QStringList>
#include <QTimer>
#include <QVariantAnimation>
#include <QToolTip>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif


namespace Chrome {
namespace {

enum class Icon {
    Folder, Grid, Play, Bookmark, Edit, Copy, Lock, Unlock, Pin, Layers,
    Hamburger, More, ChevronDown, Minus, Plus, Left, Right, Fit,
    Fullscreen, RotateLeft, RotateRight, Picture, MinWindow, MaxWindow, Close
};

QFont uiFont(int pixelSize, int weight = QFont::Normal)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(pixelSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QColor rgba(int r, int g, int b, int a)
{
    return QColor(r, g, b, a);
}

void drawIcon(QPainter &p, Icon icon, const QRectF &r, const QColor &color,
              qreal strokeWidth = 1.55)
{
    if (r.isEmpty()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(r.x(), r.y());
    const qreal sx = r.width() / 24.0;
    const qreal sy = r.height() / 24.0;
    p.scale(sx, sy);
    QPen pen(color, strokeWidth / qMax(0.01, sx), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (icon) {
    case Icon::Folder: {
        QPainterPath path;
        path.moveTo(3, 7.5);
        path.lineTo(3, 18);
        path.quadTo(3, 20, 5, 20);
        path.lineTo(19, 20);
        path.quadTo(21, 20, 21, 18);
        path.lineTo(21, 8);
        path.quadTo(21, 6, 19, 6);
        path.lineTo(12, 6);
        path.lineTo(9, 4);
        path.lineTo(5, 4);
        path.quadTo(3, 4, 3, 6);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Grid:
        p.drawRoundedRect(QRectF(4, 4, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(14, 4, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(4, 14, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(14, 14, 6, 6), 1, 1);
        break;
    case Icon::Play: {
        QPainterPath path;
        path.moveTo(8, 5);
        path.lineTo(19, 12);
        path.lineTo(8, 19);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Bookmark: {
        QPainterPath path;
        path.moveTo(6, 4);
        path.quadTo(6, 2, 8, 2);
        path.lineTo(16, 2);
        path.quadTo(18, 2, 18, 4);
        path.lineTo(18, 22);
        path.lineTo(12, 18);
        path.lineTo(6, 22);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Edit:
        p.drawLine(QPointF(14, 4), QPointF(20, 10));
        p.drawLine(QPointF(20, 10), QPointF(9, 21));
        p.drawLine(QPointF(9, 21), QPointF(3, 21));
        p.drawLine(QPointF(3, 21), QPointF(3, 15));
        p.drawLine(QPointF(3, 15), QPointF(14, 4));
        p.drawLine(QPointF(16, 2), QPointF(22, 8));
        break;
    case Icon::Copy:
        p.drawRoundedRect(QRectF(8, 8, 13, 13), 2, 2);
        p.drawLine(QPointF(16, 8), QPointF(16, 5));
        p.drawLine(QPointF(16, 5), QPointF(14, 3));
        p.drawLine(QPointF(14, 3), QPointF(5, 3));
        p.drawLine(QPointF(5, 3), QPointF(3, 5));
        p.drawLine(QPointF(3, 5), QPointF(3, 14));
        p.drawLine(QPointF(3, 14), QPointF(5, 16));
        p.drawLine(QPointF(5, 16), QPointF(8, 16));
        break;
    case Icon::Lock:
        p.drawRoundedRect(QRectF(4, 10, 16, 11), 2, 2);
        p.drawLine(QPointF(8, 10), QPointF(8, 7));
        p.drawArc(QRectF(8, 3, 8, 8), 0, 180 * 16);
        p.drawLine(QPointF(16, 7), QPointF(16, 10));
        break;
    case Icon::Unlock:
        // 未锁定：锁梁一端留开口（与闭锁共用锁身/梁/线宽）
        p.drawRoundedRect(QRectF(4, 10, 16, 11), 2, 2);
        p.drawLine(QPointF(16, 10), QPointF(16, 7));
        p.drawArc(QRectF(8, 3, 8, 8), 0, 180 * 16);
        break;
    case Icon::Pin: {
        QPainterPath path;
        path.moveTo(8, 3);
        path.lineTo(16, 3);
        path.lineTo(15, 7);
        path.lineTo(18.5, 10.5);
        path.lineTo(18.5, 12);
        path.lineTo(5.5, 12);
        path.lineTo(5.5, 10.5);
        path.lineTo(9, 7);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(12, 12), QPointF(12, 21));
        break;
    }
    case Icon::Layers: {
        QPainterPath top;
        top.moveTo(12, 3);
        top.lineTo(21, 8);
        top.lineTo(12, 13);
        top.lineTo(3, 8);
        top.closeSubpath();
        p.drawPath(top);
        p.drawLine(QPointF(3, 12), QPointF(12, 17));
        p.drawLine(QPointF(12, 17), QPointF(21, 12));
        p.drawLine(QPointF(3, 16), QPointF(12, 21));
        p.drawLine(QPointF(12, 21), QPointF(21, 16));
        break;
    }
    case Icon::Hamburger:
        p.drawLine(QPointF(4, 7), QPointF(20, 7));
        p.drawLine(QPointF(4, 12), QPointF(20, 12));
        p.drawLine(QPointF(4, 17), QPointF(20, 17));
        break;
    case Icon::Fullscreen:
        p.drawLine(QPointF(8, 3), QPointF(3, 3));
        p.drawLine(QPointF(3, 3), QPointF(3, 8));
        p.drawLine(QPointF(21, 3), QPointF(16, 3));
        p.drawLine(QPointF(21, 3), QPointF(21, 8));
        p.drawLine(QPointF(3, 16), QPointF(3, 21));
        p.drawLine(QPointF(3, 21), QPointF(8, 21));
        p.drawLine(QPointF(21, 16), QPointF(21, 21));
        p.drawLine(QPointF(21, 21), QPointF(16, 21));
        break;
    case Icon::More:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(5, 12), 1.1, 1.1);
        p.drawEllipse(QPointF(12, 12), 1.1, 1.1);
        p.drawEllipse(QPointF(19, 12), 1.1, 1.1);
        break;
    case Icon::ChevronDown:
        p.drawLine(QPointF(7, 10), QPointF(12, 15));
        p.drawLine(QPointF(12, 15), QPointF(17, 10));
        break;
    case Icon::Minus:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::Left:
        p.drawLine(QPointF(15, 5), QPointF(8, 12));
        p.drawLine(QPointF(8, 12), QPointF(15, 19));
        break;
    case Icon::Right:
        p.drawLine(QPointF(9, 5), QPointF(16, 12));
        p.drawLine(QPointF(16, 12), QPointF(9, 19));
        break;
    case Icon::Fit:
        p.drawLine(QPointF(8, 3), QPointF(3, 3));
        p.drawLine(QPointF(3, 3), QPointF(3, 8));
        p.drawLine(QPointF(21, 3), QPointF(16, 3));
        p.drawLine(QPointF(21, 3), QPointF(21, 8));
        p.drawLine(QPointF(3, 16), QPointF(3, 21));
        p.drawLine(QPointF(3, 21), QPointF(8, 21));
        p.drawLine(QPointF(21, 16), QPointF(21, 21));
        p.drawLine(QPointF(21, 21), QPointF(16, 21));
        break;
    case Icon::RotateLeft:
        p.drawArc(QRectF(4, 4, 16, 16), 70 * 16, 260 * 16);
        p.drawLine(QPointF(4, 4), QPointF(4, 9));
        p.drawLine(QPointF(4, 4), QPointF(9, 4));
        break;
    case Icon::RotateRight:
        p.drawArc(QRectF(4, 4, 16, 16), 30 * 16, 260 * 16);
        p.drawLine(QPointF(20, 4), QPointF(20, 9));
        p.drawLine(QPointF(20, 4), QPointF(15, 4));
        break;
    case Icon::Picture:
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
        p.drawEllipse(QPointF(8.5, 8.5), 1.5, 1.5);
        p.drawLine(QPointF(4, 17), QPointF(9, 12));
        p.drawLine(QPointF(9, 12), QPointF(13, 16));
        p.drawLine(QPointF(13, 16), QPointF(16, 13));
        p.drawLine(QPointF(16, 13), QPointF(21, 18));
        break;
    case Icon::MinWindow:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::MaxWindow:
        p.drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1);
        break;
    case Icon::Close:
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    }
    p.restore();
}

void setPointerCursor(QWidget *w, bool pointer)
{
    if (!w) {
        return;
    }
    if (pointer) {
        w->setCursor(Qt::PointingHandCursor);
    } else {
        w->unsetCursor();
    }
}

// ---------------------------------------------------------------------------
// 按钮状态画刷（标题栏 / 命令栏 / 底部胶囊共用，交接单 §7「控件状态」）
// 全部只画，不动几何：按压缩放通过 painter 变换完成，rect 本身永远不变。
// ---------------------------------------------------------------------------

// 交接单 §7 指定的两个状态底色；其余颜色取自 UiStyle 令牌。
inline QColor kHoverBg() { return QColor(QString::fromLatin1(UiStyle::pick("#45484d", "#e9ebed"))); }   // Hover
inline QColor kPressBg() { return QColor(QString::fromLatin1(UiStyle::pick("#292b2f", "#dfe1e3"))); }   // Pressed

constexpr int kHoverMs = 80;               // Hover 淡入
constexpr int kPressMs = 65;               // 按下压暗
constexpr int kReleaseMs = 100;            // 松开恢复（无弹簧、无回弹）
constexpr int kCheckedMs = 120;            // 锁定/选中态切换
constexpr int kTickMs = 16;

// ---- 底部胶囊几何（交接单 §7，与原型 .control-dock 一致）----
constexpr int kDockW = 440;
constexpr int kDockH = 72;
constexpr int kDockPadX = 9;
constexpr int kDockPadTop = 6;
constexpr int kDockPadBottom = 7;
constexpr int kRowGap = 3;        // 两排之间
constexpr int kItemGap = 3;       // 同排按钮之间
constexpr int kZoomRowH = 26;
constexpr int kActionRowH = 30;
constexpr int kBtnW = 32;
constexpr int kBtnH = 30;
constexpr int kPageLabelW = 76;
constexpr int kPercentW = 44;
constexpr int kGridGap = 10;      // 缩放行两列列间距（滑杆 / 百分比）
constexpr int kSepW = 1;
constexpr int kSepH = 16;
constexpr int kSepMargin = 3;     // 分隔线左右外边距
constexpr qreal kIconSize = 17.0; // 普通图标
constexpr qreal kNavIconSize = 19.0;
constexpr qreal kNavIconStroke = 1.8;
constexpr qreal kIconStroke = 1.55;
constexpr qreal kRadius = 5.0;

// ---- Tooltip（交接单 §7「文字提示」）----
constexpr int kTipDelayMs = 400;  // 悬停多久出现
constexpr int kTipPadW = 24;      // 估算宽度：QSS 左右 padding 8+8 + 边框
constexpr int kTipPadH = 16;      // 估算高度：上下 padding 6+6 + 边框
constexpr int kTipEdge = 8;       // 距窗口左右最小留白
constexpr int kTipSlack = 16;     // 预留 Qt 显示时自带的偏移，保证不越出窗口

struct ButtonPalette {
    QColor base = QColor(0, 0, 0, 0);          // 常态底（alpha=0 表示无）
    QColor baseBorder = QColor(0, 0, 0, 0);    // 常态描边
    QColor hover = kHoverBg();                   // Hover 底
    QColor press = kPressBg();                   // Pressed 底
    QColor checkedBg = QColor(85, 192, 240, 31);    // 锁定 / 选中底
    QColor checkedBorder = QColor(85, 192, 240, 64); // 锁定 / 选中描边
    QColor ink = QColor(0xd9, 0xda, 0xdd);     // 常态前景
    QColor checkedInk;                         // 锁定 / 选中前景（无效则用 ink）
    qreal radius = kRadius;
};

QColor blendColor(const QColor &a, const QColor &b, qreal t)
{
    t = qBound(0.0, t, 1.0);
    return QColor(qRound(a.red() + (b.red() - a.red()) * t),
                  qRound(a.green() + (b.green() - a.green()) * t),
                  qRound(a.blue() + (b.blue() - a.blue()) * t));
}

// 开始绘制一个按钮：底色 / 描边 / 焦点环按状态出图，内容随后用同一坐标绘制。
// 按钮几何永不因状态改动（命中区域 = 绘制区域）；按下只压暗，不放大不上浮，
// 图标的轻微缩放（98%）由调用方按 v.press 处理。
QRectF beginButton(QPainter &p, const QRectF &rect, const BtnState &s,
                   const BtnVisual &v, const ButtonPalette &pal)
{
    p.save();
    if (s.disabled) {
        p.setOpacity(p.opacity() * 0.42);
    }

    const QRectF r = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal amt = s.disabled ? 0.0 : qMax(v.hover, v.press);
    const bool checked = s.checked && !s.danger;

    p.setPen(Qt::NoPen);
    if (pal.base.alpha() > 0) {
        p.setBrush(pal.base);
        p.drawRoundedRect(r, pal.radius, pal.radius);
    }

    if (checked) {
        QColor bg = pal.checkedBg;
        bg.setAlphaF(qMin(1.0, bg.alphaF() * (1.0 + 0.9 * v.hover)));
        p.setBrush(bg);
        p.drawRoundedRect(r, pal.radius, pal.radius);
        // 选中态按下：压暗而不是盖掉蓝色底
        if (v.press > 0.004) {
            QColor dim = pal.press;
            dim.setAlphaF(0.45 * v.press);
            p.setBrush(dim);
            p.drawRoundedRect(r, pal.radius, pal.radius);
        }
    } else if (amt > 0.004) {
        QColor bg = blendColor(pal.hover, pal.press, v.press);
        bg.setAlphaF(qMin(1.0, bg.alphaF() * amt));
        p.setBrush(bg);
        p.drawRoundedRect(r, pal.radius, pal.radius);
    }

    QColor border = QColor(0, 0, 0, 0);
    if (checked) {
        border = pal.checkedBorder;
        border.setAlphaF(qMin(1.0, border.alphaF() * (1.0 + 0.9 * v.hover)));
    } else if (pal.base.alpha() > 0) {
        border = pal.baseBorder;
    } else if (amt > 0.004) {
        border = pal.baseBorder;
        border.setAlphaF(border.alphaF() * amt);
    }
    // 按下态：参考图 03 在圆内补一圈细描边，让「按下」比「悬停」更明确。
    if (border.alpha() == 0 && !checked && !s.disabled && v.press > 0.004) {
        border = UiStyle::isLight() ? QColor(0, 0, 0, 46) : QColor(255, 255, 255, 46);
        border.setAlphaF(border.alphaF() * v.press);
    }
    if (border.alpha() > 0) {
        p.setPen(QPen(border, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, pal.radius, pal.radius);
    }

    // 键盘焦点环：2px 淡蓝，只在键盘进入时出现，和 Hover 明确区分
    if (s.focus && !s.disabled) {
        p.setPen(QPen(QColor(85, 192, 240, 215), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect.adjusted(1.5, 1.5, -1.5, -1.5),
                          pal.radius + 0.5, pal.radius + 0.5);
    }
    return rect;
}

void endButton(QPainter &p)
{
    p.restore();
}

QColor buttonInk(const BtnState &s, const ButtonPalette &pal)
{
    if (s.checked && !s.danger) {
        return pal.checkedInk.isValid() ? pal.checkedInk : pal.ink;
    }
    return pal.ink;
}

// 等宽数字：逐字符按最宽数字排版，百分比变化时整体宽度与居中都不抖。
void drawTabularText(QPainter &p, const QRectF &r, const QString &text,
                     const QColor &color, int pixelSize)
{
    const QFont f = uiFont(pixelSize);
    p.setFont(f);
    const QFontMetricsF fm(f);
    qreal digitW = 0.0;
    for (int d = 0; d <= 9; ++d) {
        digitW = qMax(digitW, fm.horizontalAdvance(QString::number(d)));
    }
    qreal total = 0.0;
    for (const QChar &ch : text) {
        total += ch.isDigit() ? digitW : fm.horizontalAdvance(QString(ch));
    }
    qreal x = r.center().x() - total / 2.0;
    p.setPen(color);
    for (const QChar &ch : text) {
        const qreal w = ch.isDigit() ? digitW : fm.horizontalAdvance(QString(ch));
        p.drawText(QRectF(x, r.top(), w, r.height()), Qt::AlignCenter, QString(ch));
        x += w;
    }
}

qreal stepToward(qreal cur, qreal target, qreal dt, int durationMs)
{
    if (durationMs <= 0) {
        return target;
    }
    const qreal d = dt / static_cast<qreal>(durationMs);
    return (target > cur) ? qMin(target, cur + d) : qMax(target, cur - d);
}

} // namespace

// ===========================================================================
// 按钮状态动画器（三个铬件共用）
// ===========================================================================

bool reducedMotion()
{
    static const bool reduced = []() -> bool {
        const QByteArray env = qgetenv("QUIKVIEW_REDUCE_MOTION").trimmed().toLower();
        if (!env.isEmpty()) {
            return !(env == "0" || env == "false" || env == "off");
        }
#ifdef Q_OS_WIN
        BOOL animations = TRUE;
        if (::SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0)) {
            return animations == FALSE;
        }
#endif
        return false;
    }();
    return reduced;
}

ButtonFeedback::ButtonFeedback(QObject *parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(kTickMs);
    connect(m_timer, &QTimer::timeout, this, &ButtonFeedback::tick);
}

void ButtonFeedback::setHover(int index)
{
    if (m_hover == index) {
        return;
    }
    m_hover = index;
    if (index >= 0 && !m_vals.contains(index)) {
        m_vals.insert(index, BtnVisual());
    }
    start();
}

void ButtonFeedback::setPressed(int index)
{
    if (m_pressed == index) {
        return;
    }
    m_pressed = index;
    if (index >= 0 && !m_vals.contains(index)) {
        m_vals.insert(index, BtnVisual());
    }
    start();
}

void ButtonFeedback::setChecked(int index, bool on)
{
    if (index < 0) {
        return;
    }
    if (m_checked.contains(index) == on) {
        // 状态未变；但按钮列表可能重排过，保证插值条目存在。
        if (!m_vals.contains(index)) {
            m_vals.insert(index, BtnVisual());
        }
        return;
    }
    if (on) {
        m_checked.insert(index);
    } else {
        m_checked.remove(index);
    }
    if (!m_vals.contains(index)) {
        m_vals.insert(index, BtnVisual());
    }
    start();
}

void ButtonFeedback::reset()
{
    m_hover = -1;
    m_pressed = -1;
    if (m_timer->isActive()) {
        m_timer->stop();
    }
    if (m_vals.isEmpty()) {
        return;
    }
    m_vals.clear();
    emit changed();
}

BtnVisual ButtonFeedback::visual(int index) const
{
    return m_vals.value(index);
}

void ButtonFeedback::start()
{
    if (reducedMotion()) {
        snap();
        return;
    }
    if (!m_timer->isActive()) {
        m_clock.restart();
        m_timer->start();
    }
}

// 减少动画：直接落到终态，仍然保留背景色反馈
void ButtonFeedback::snap()
{
    m_vals.clear();
    if (m_hover >= 0) {
        m_vals[m_hover].hover = 1.0;
    }
    if (m_pressed >= 0) {
        m_vals[m_pressed].hover = 1.0;
        m_vals[m_pressed].press = 1.0;
    }
    for (int idx : std::as_const(m_checked)) {
        m_vals[idx].checked = 1.0;
    }
    emit changed();
}

void ButtonFeedback::tick()
{
    const qreal dt = static_cast<qreal>(qMax<qint64>(1, m_clock.restart()));
    bool busy = false;
    for (auto it = m_vals.begin(); it != m_vals.end();) {
        BtnVisual &v = it.value();
        const qreal hoverTarget = (it.key() == m_hover) ? 1.0 : 0.0;
        const qreal pressTarget = (it.key() == m_pressed) ? 1.0 : 0.0;
        const qreal checkedTarget = m_checked.contains(it.key()) ? 1.0 : 0.0;
        v.hover = stepToward(v.hover, hoverTarget, dt, kHoverMs);
        // 按下与松开各用各的时长：65ms 压暗、100ms 恢复。
        v.press = stepToward(v.press, pressTarget, dt,
                             pressTarget > v.press ? kPressMs : kReleaseMs);
        v.checked = stepToward(v.checked, checkedTarget, dt, kCheckedMs);

        const bool settled = qFuzzyCompare(v.hover + 1.0, hoverTarget + 1.0)
                          && qFuzzyCompare(v.press + 1.0, pressTarget + 1.0)
                          && qFuzzyCompare(v.checked + 1.0, checkedTarget + 1.0);
        if (settled && hoverTarget == 0.0 && pressTarget == 0.0 && checkedTarget == 0.0) {
            it = m_vals.erase(it);
            continue;
        }
        if (!settled) {
            busy = true;
        }
        ++it;
    }
    emit changed();
    if (!busy) {
        m_timer->stop();
    }
}

// ===========================================================================
// Tooltip 提示器（三个铬件共用）
// ===========================================================================

TooltipHint::TooltipHint(QWidget *host)
    : QObject(host)
    , m_host(host)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &TooltipHint::reveal);
}

void TooltipHint::hover(const QString &text, const QPoint &anchorGlobal)
{
    // 「不显示鼠标悬浮工具提示」总开关（键 config/view/show_tooltip，默认显示）：
    // 关闭时既不排队也不留旧提示，切换后下一次悬停立即生效，无需重启。
    if (!Config::instance().showTooltip()) {
        hideNow();
        return;
    }
    if (text.isEmpty()) {
        hideNow();
        return;
    }
    if (text == m_text) {
        if (m_shown) {
            return;                  // 同一目标继续悬停：不重排、不闪烁
        }
        if (m_timer->isActive()) {
            m_anchor = anchorGlobal; // 计时中只跟随指针
            return;
        }
    }
    if (m_shown) {
        m_shown = false;
        QToolTip::hideText();
    }
    m_text = text;
    m_anchor = anchorGlobal;
    m_timer->start(kTipDelayMs);
}

void TooltipHint::hideNow()
{
    m_timer->stop();
    m_text.clear();
    if (m_shown) {
        m_shown = false;
        QToolTip::hideText();
    }
}

void TooltipHint::reveal()
{
    if (!m_host || m_text.isEmpty()) {
        return;
    }
    const QFontMetrics fm(QToolTip::font());
    const int tipW = fm.horizontalAdvance(m_text) + kTipPadW;
    const int tipH = fm.height() + kTipPadH;

    // 默认在锚点下方、贴住铬件本体；Qt 显示时还会再加一点自身偏移，夹紧时预留 kTipSlack。
    const QRect hostRect(m_host->mapToGlobal(QPoint(0, 0)), m_host->size());
    QPoint pos(m_anchor.x() - tipW / 2, hostRect.bottom() + 8);
    if (QWidget *win = m_host->window()) {
        const QRect frame = win->frameGeometry();
        // 下方放不下就翻到铬件上方（胶囊贴窗口下缘时必然走这一支）
        if (pos.y() + tipH + kTipSlack > frame.bottom()) {
            pos.ry() = hostRect.top() - tipH - 8;
        }
        // 夹在窗口内：左右各留 8px、上下各留 4px，保证不越出窗口
        const int leftMin = frame.left() + kTipEdge;
        const int leftMax = qMax(leftMin, frame.right() - tipW - kTipEdge - kTipSlack);
        const int topMin = frame.top() + 4;
        const int topMax = qMax(topMin, frame.bottom() - tipH - 4 - kTipSlack);
        pos.setX(qBound(leftMin, pos.x(), leftMax));
        pos.setY(qBound(topMin, pos.y(), topMax));
    }
    m_shown = true;
    QToolTip::showText(pos, m_text, m_host);
}

// ===========================================================================
// 皮肤贴图兼容接口（旧布局代码仍可能引用）
// ===========================================================================

QString skinDir()
{
    const QString base = QCoreApplication::applicationDirPath();
    const QString dirs[] = {
        base + QStringLiteral("/../resources/skin"),
        base + QStringLiteral("/../../resources/skin"),
        base + QStringLiteral("/resources/skin"),
        QStringLiteral("resources/skin"),
    };
    for (const QString &d : dirs) {
        if (QFileInfo::exists(d + QStringLiteral("/static_controlpanel.png"))) {
            return d;
        }
    }
    return QStringLiteral("resources/skin");
}

QPixmap skinPixmap(const QString &name)
{
    return QPixmap(skinDir() + QLatin1Char('/') + name);
}

QPixmap skinFrame(const QString &name, int idx)
{
    const QPixmap all = skinPixmap(name);
    if (all.isNull()) {
        return QPixmap();
    }
    const int frames = qMax(1, all.width() / qMax(1, all.height()));
    const int fw = all.width() / frames;
    return all.copy(idx * fw, 0, fw, all.height());
}

// ===========================================================================
// 顶部单行工具栏（38px）
// ===========================================================================

namespace {

constexpr int kTopH = 38;
constexpr int kTopBtn = 36;
constexpr int kTopWinBtn = 46;
constexpr int kTopMargin = 6;
constexpr int kTopLogoW = 22;

int topTextWidth(const QString &text)
{
    return QFontMetrics(uiFont(12)).horizontalAdvance(text);
}

// 图标+文字按钮的自然宽度；纯图标固定 36。
// 文字态：左内边距 10 + 图标 18 + 图标文字间距 8 + 文字 + 右内边距 10。
int topButtonWidth(const QString &label, bool withLabel)
{
    if (!withLabel || label.isEmpty()) {
        return kTopBtn;
    }
    return 10 + 18 + 8 + topTextWidth(label) + 10;
}

QString topActionLabel(int id)
{
    switch (id) {
    case 0: return QObject::tr("打开");
    case 1: return QObject::tr("浏览");
    case 2: return QObject::tr("幻灯片");
    case 3: return QObject::tr("书签");
    case 4: return QObject::tr("编辑");
    case 5: return QObject::tr("复制到");
    default: break;
    }
    return QString();
}

// 圆形反馈：36×36 命中区里画 32×32 的圆，四角仍可点。
QRectF circleVisual(const QRect &hit, int diameter = 32)
{
    return QRectF(hit.center().x() - diameter / 2.0 + 0.5,
                  hit.center().y() - diameter / 2.0 + 0.5,
                  diameter, diameter);
}

} // namespace

TopBar::TopBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(kTopH);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("顶部工具栏"));
    setAccessibleDescription(tr("打开、浏览、幻灯片、书签、编辑、复制到、图层、锁定顶栏、图片置顶与三横菜单"));
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
    m_tip = new TooltipHint(this);
}

void TopBar::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    update();
}

void TopBar::setTopLocked(bool locked)
{
    if (m_topLocked == locked) {
        return;
    }
    m_topLocked = locked;
    update();
}

void TopBar::setPinned(bool pinned)
{
    if (m_pinned == pinned) {
        return;
    }
    m_pinned = pinned;
    update();
}

void TopBar::setLayerState(bool available, bool open)
{
    if (m_layersAvailable == available && m_layersOpen == open) {
        return;
    }
    m_layersAvailable = available;
    m_layersOpen = open;
    update();
}

int TopBar::idForAction(Action action)
{
    switch (action) {
    case Action::Open: return 0;
    case Action::Browse: return 1;
    case Action::Slideshow: return 2;
    case Action::Bookmark: return 3;
    case Action::Edit: return 4;
    case Action::CopyTo: return 5;
    case Action::Layers: return 6;
    case Action::TopLock: return 7;
    case Action::Pin: return 8;
    case Action::Menu: return 9;
    case Action::Minimize: return 10;
    case Action::Maximize: return 11;
    case Action::Close: return 12;
    default: break;
    }
    return -1;
}

TopBar::Action TopBar::actionForId(int id)
{
    switch (id) {
    case 0: return Action::Open;
    case 1: return Action::Browse;
    case 2: return Action::Slideshow;
    case 3: return Action::Bookmark;
    case 4: return Action::Edit;
    case 5: return Action::CopyTo;
    case 6: return Action::Layers;
    case 7: return Action::TopLock;
    case 8: return Action::Pin;
    case 9: return Action::Menu;
    case 10: return Action::Minimize;
    case 11: return Action::Maximize;
    case 12: return Action::Close;
    default: break;
    }
    return Action::None;
}

// 单行布局。分阶段收窄（与标注同序）：
//   0 全文字 + 文件名 140 ｜ 1 文件名归零 ｜ 2 中组收成图标
//   3 复制到 ｜ 4 编辑 ｜ 5 书签 ｜ 6 幻灯片 ｜ 7 图层
//   8 顶栏锁 ｜ 9 置顶 ｜ 10 浏览
// 最后始终保留打开、三横菜单和 Windows 三按钮。
QVector<TopBar::Btn> TopBar::buttons() const
{
    struct Plan {
        bool labels = true;        // 中组是否带文字
        bool browseLabel = true;
        int filenameMin = 0;
        QVector<Action> center;
        bool browse = true;
        bool layers = true;
        bool topLock = true;
        bool pin = true;
        int required = 0;
    };

    // 文字按钮之间 4px，纯图标按钮之间 2px（36 命中区几乎相邻）。
    auto gapFor = [](bool labeled) { return labeled ? 4 : 2; };

    auto makePlan = [&](int stage) {
        Plan p;
        p.labels = stage <= 1;
        p.browseLabel = stage <= 2;
        p.filenameMin = stage == 0 ? 140 : 0;
        p.center = { Action::Slideshow, Action::Bookmark, Action::Edit, Action::CopyTo };
        p.browse = stage < 10;
        p.layers = stage < 7;
        p.topLock = stage < 8;
        p.pin = stage < 9;
        if (stage >= 3) p.center.removeOne(Action::CopyTo);
        if (stage >= 4) p.center.removeOne(Action::Edit);
        if (stage >= 5) p.center.removeOne(Action::Bookmark);
        if (stage >= 6) p.center.removeOne(Action::Slideshow);

        // 打开始终带文字（极窄窗也保留），只有中组与浏览会依次图标化/收入菜单。
        int left = kTopMargin + kTopLogoW + 8;
        left += topButtonWidth(topActionLabel(0), true) + gapFor(true);
        if (p.browse) {
            left += topButtonWidth(topActionLabel(1), p.browseLabel);
        }
        int centerW = 0;
        for (Action a : p.center) {
            centerW += topButtonWidth(topActionLabel(idForAction(a)), p.labels) + gapFor(p.labels);
        }
        if (centerW > 0) {
            centerW -= gapFor(p.labels);
        }

        int right = 3 * kTopWinBtn + 8 + kTopBtn;   // 菜单常驻
        if (p.layers) right += kTopBtn;
        if (p.topLock) right += kTopBtn;
        if (p.pin) right += kTopBtn;

        // 中组两侧各留 16px，文件名再留 24px 起收点。
        p.required = left + p.filenameMin + centerW + right + 40;
        return p;
    };

    Plan plan;
    bool fitted = false;
    for (int stage = 0; stage <= 10; ++stage) {
        plan = makePlan(stage);
        if (plan.required <= width()) {
            fitted = true;
            break;
        }
    }
    if (!fitted) {
        plan = makePlan(10);
    }

    QVector<Btn> out;
    m_hiddenIds.clear();
    m_filenameRect = QRect();

    const int h = height() > 0 ? height() : kTopH;
    const int y = (h - kTopBtn) / 2;

    // —— 左组：Q 标识、打开、浏览、文件名 ——
    int x = kTopMargin + kTopLogoW + 8;
    const int openW = topButtonWidth(topActionLabel(0), true);
    out.append(Btn{ Action::Open, QRect(x, y, openW, kTopBtn), topActionLabel(0),
                    false, false, false });
    x += openW + gapFor(true);

    if (plan.browse) {
        const int browseW = topButtonWidth(topActionLabel(1), plan.browseLabel);
        out.append(Btn{ Action::Browse, QRect(x, y, browseW, kTopBtn), topActionLabel(1),
                        !plan.browseLabel, false, false });
        x += browseW;
    } else {
        m_hiddenIds.append(1);
    }
    const int leftEnd = x;

    // —— 中组（相对整窗居中；空间不足时按计划收纳） ——
    int centerW = 0;
    for (Action a : plan.center) {
        centerW += topButtonWidth(topActionLabel(idForAction(a)), plan.labels) + gapFor(plan.labels);
    }
    if (centerW > 0) {
        centerW -= gapFor(plan.labels);
    }

    const int rightEdge = width() - 3 * kTopWinBtn - 8;
    int rightStart = rightEdge;
    if (plan.layers) rightStart -= kTopBtn;
    if (plan.topLock) rightStart -= kTopBtn;
    if (plan.pin) rightStart -= kTopBtn;
    rightStart -= kTopBtn;   // menu

    int centerStart = width() / 2 - centerW / 2;
    centerStart = qBound(leftEnd + 16, centerStart, qMax(leftEnd + 16, rightStart - 12 - centerW));
    int cx = centerStart;
    for (Action a : plan.center) {
        const int id = idForAction(a);
        const int bw = topButtonWidth(topActionLabel(id), plan.labels);
        out.append(Btn{ a, QRect(cx, y, bw, kTopBtn), topActionLabel(id),
                        !plan.labels, false, false });
        cx += bw + gapFor(plan.labels);
    }
    for (int id : { 2, 3, 4, 5 }) {
        const Action a = actionForId(id);
        if (!plan.center.contains(a)) {
            m_hiddenIds.append(id);
        }
    }

    // 文件名：优先省略，可收至零宽；低于 80px 就整段不画（避免只剩一两个字母）。
    const int fnLeft = leftEnd + 12;
    const int fnRight = qMax(fnLeft, centerStart - 20);
    const int fnW = qMin(360, fnRight - fnLeft);
    m_filenameRect = fnW >= 80 ? QRect(fnLeft, 0, fnW, h) : QRect();

    // —— 右组：图层 / 顶栏锁 / 置顶 / 三横菜单，再往右是 Windows 三按钮 ——
    int rx = rightEdge;
    rx -= kTopBtn;
    out.append(Btn{ Action::Menu, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    if (plan.pin) {
        rx -= kTopBtn;
        out.append(Btn{ Action::Pin, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    } else {
        m_hiddenIds.append(8);
    }
    if (plan.topLock) {
        rx -= kTopBtn;
        out.append(Btn{ Action::TopLock, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    } else {
        m_hiddenIds.append(7);
    }
    if (plan.layers) {
        rx -= kTopBtn;
        out.append(Btn{ Action::Layers, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false,
                        !m_layersAvailable });
    } else {
        m_hiddenIds.append(6);
    }

    for (int i = 0; i < 3; ++i) {
        const QRect r(width() - (3 - i) * kTopWinBtn, 0, kTopWinBtn, h);
        const Action a = i == 0 ? Action::Minimize : (i == 1 ? Action::Maximize : Action::Close);
        out.append(Btn{ a, r, QString(), true, false, false });
    }

    return out;
}

QVector<int> TopBar::hiddenActionIds() const
{
    buttons();
    return m_hiddenIds;
}

QPoint TopBar::actionAnchorGlobal(int id) const
{
    const QVector<Btn> list = buttons();
    const Action want = actionForId(id);
    for (const Btn &b : list) {
        if (b.action == want) {
            return mapToGlobal(b.rect.center());
        }
    }
    return mapToGlobal(QPoint(width() - 3 * kTopWinBtn - 6 - kTopBtn / 2, height() / 2));
}

QPoint TopBar::menuAnchorGlobal() const
{
    return actionAnchorGlobal(9);
}

QPoint TopBar::closeAnchorGlobal() const
{
    return actionAnchorGlobal(12);
}

void TopBar::probeHover(int id)
{
    const QVector<Btn> list = buttons();
    const Action want = actionForId(id);
    int index = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].action == want) {
            index = i;
            break;
        }
    }
    m_hover = index;
    m_feedback->setHover(index);
    // 探针也模拟「悬停够久」：把 tooltip 一起亮出来，截图才能取证。
    if (index >= 0 && index < list.size()) {
        m_tip->hover(tipText(list[index].action),
                     mapToGlobal(list[index].rect.center()));
    }
    update();
}

void TopBar::probePress(int id)
{
    const QVector<Btn> list = buttons();
    const Action want = actionForId(id);
    int index = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].action == want) {
            index = i;
            break;
        }
    }
    m_press = index;
    m_feedback->setPressed(index);
    update();
}

int TopBar::hitTest(const QPoint &pos) const
{
    const QVector<Btn> list = buttons();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

QString TopBar::tipText(Action action) const
{
    switch (action) {
    case Action::Open: return tr("打开");
    case Action::Browse: return tr("浏览");
    case Action::Slideshow: return tr("幻灯片放映");
    case Action::Bookmark: return tr("书签");
    case Action::Edit: return tr("编辑");
    case Action::CopyTo: return tr("复制到");
    case Action::Layers:
        return m_layersAvailable ? (m_layersOpen ? tr("隐藏 PSD 图层") : tr("PSD 图层"))
                                 : tr("PSD 图层（当前图片没有可用图层）");
    case Action::TopLock: return tr("保持顶部工具栏显示");
    case Action::Pin: return tr("图片窗口置顶");
    case Action::Menu: return tr("菜单");
    case Action::Minimize: return tr("最小化");
    case Action::Maximize: return (window() && window()->isMaximized()) ? tr("还原") : tr("最大化");
    case Action::Close: return tr("关闭");
    default: break;
    }
    return QString();
}

void TopBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QString::fromLatin1(UiStyle::fluent::titleBarBg())));
    p.fillRect(QRect(0, height() - 1, width(), 1),
               (UiStyle::isLight() ? QColor(0, 0, 0, 22) : QColor(255, 255, 255, 18)));

    // 应用标识：22×22 蓝色圆角方块 + Q
    const QRectF logo(kTopMargin, (height() - kTopLogoW) / 2.0, kTopLogoW, kTopLogoW);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#1688f8")));
    p.drawRoundedRect(logo, 6, 6);
    p.setFont(uiFont(11, QFont::DemiBold));
    p.setPen(QColor(Qt::white));
    p.drawText(logo, Qt::AlignCenter, QStringLiteral("Q"));

    // 文件名：优先省略；零宽时整段不画
    if (m_filenameRect.width() > 2) {
        p.setFont(uiFont(12));
        p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#9a9da2", "#5f6368"))));
        const QString elided = p.fontMetrics().elidedText(m_title, Qt::ElideMiddle,
                                                          m_filenameRect.width());
        p.drawText(m_filenameRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

    const QVector<Btn> list = buttons();
    const QColor ink = QColor(QString::fromLatin1(UiStyle::pick("#d9dadd", "#2a2c30")));
    const QColor mutedInk = QColor(QString::fromLatin1(UiStyle::pick("#8e9297", "#6b7075")));
    const QColor accent = UiStyle::accent();

    for (int i = 0; i < list.size(); ++i) {
        const Btn &b = list.at(i);
        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.focus = (m_keyboardFocus && m_focus == i);
        st.disabled = b.disabled;
        st.danger = (b.action == Action::Close);
        st.checked = (b.action == Action::Pin && m_pinned)
                     || (b.action == Action::Layers && m_layersOpen);

        BtnVisual v = m_feedback->visual(i);
        ButtonPalette pal;
        pal.ink = ink;
        pal.radius = 17;
        pal.hover = QColor(QString::fromLatin1(UiStyle::pick("#35373b", "#e7e9eb")));
        pal.press = QColor(QString::fromLatin1(UiStyle::pick("#292b2f", "#dcdfe2")));
        pal.checkedBg = UiStyle::accentSoft();
        pal.checkedBorder = QColor(accent.red(), accent.green(), accent.blue(), 60);
        pal.checkedInk = accent;

        if (b.action == Action::Close) {
            pal.radius = 0;
            pal.hover = UiStyle::color(UiStyle::Danger);
            pal.press = UiStyle::color(UiStyle::Danger).darker(122);
            pal.ink = (st.hover || st.pressed) ? QColor(Qt::white)
                                               : QColor(QString::fromLatin1(UiStyle::pick("#c9c9c9", "#333333")));
        } else if (b.action == Action::Minimize || b.action == Action::Maximize) {
            pal.radius = 0;
            pal.hover = QColor(QString::fromLatin1(UiStyle::pick("#343539", "#e9ebed")));
            pal.press = QColor(QString::fromLatin1(UiStyle::pick("#2a2b2e", "#dfe1e3")));
            pal.ink = QColor(QString::fromLatin1(UiStyle::pick("#c9c9c9", "#333333")));
        } else if (b.action == Action::TopLock) {
            // 顶栏闭锁：保持透明底；悬停仍是中性圆（与底栏蓝底明确区分）。
            pal.checkedBg = QColor(0, 0, 0, 0);
            pal.checkedBorder = QColor(0, 0, 0, 0);
            pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));
        } else if (b.action == Action::Pin) {
            // 图片置顶：独立图钉 + 自身选中样式（半透明蓝底 + 淡蓝描边/图标）。
            const QColor light(QStringLiteral("#98d0ff"));
            pal.checkedBg = QColor(accent.red(), accent.green(), accent.blue(), 64);
            pal.checkedBorder = light;
            pal.checkedInk = light;
        }

        const bool circle = (b.action != Action::Close && b.action != Action::Minimize
                             && b.action != Action::Maximize && b.iconOnly);
        if (!circle && b.action != Action::Close && b.action != Action::Minimize
            && b.action != Action::Maximize) {
            pal.radius = 9;   // 带文字按钮用圆角矩形反馈，不做胶囊
        }
        const QRectF visual = circle ? circleVisual(b.rect) : QRectF(b.rect);
        beginButton(p, visual, st, v, pal);
        const QColor drawInk = buttonInk(st, pal);

        const QPointF center = b.rect.center();
        // 按下时图标轻微缩至 98%（按下只压暗不上浮；与底岛按钮同一语义）。
        const qreal iconS = 18.0 * (1.0 - 0.02 * v.press);
        // 带文字按钮：图标靠左（左内边距 10），文字接在图标右侧 8px。
        const QRectF icon = b.iconOnly
                                ? QRectF(center.x() - iconS / 2, center.y() - iconS / 2, iconS, iconS)
                                : QRectF(b.rect.x() + 10, center.y() - iconS / 2, iconS, iconS);

        switch (b.action) {
        case Action::Open: drawIcon(p, Icon::Folder, icon, drawInk); break;
        case Action::Browse: drawIcon(p, Icon::Grid, icon, drawInk); break;
        case Action::Slideshow: drawIcon(p, Icon::Play, icon, drawInk); break;
        case Action::Bookmark: drawIcon(p, Icon::Bookmark, icon, drawInk); break;
        case Action::Edit: drawIcon(p, Icon::Edit, icon, drawInk); break;
        case Action::CopyTo: drawIcon(p, Icon::Copy, icon, drawInk); break;
        case Action::Layers: drawIcon(p, Icon::Layers, icon, drawInk); break;
        case Action::TopLock:
            drawIcon(p, m_topLocked ? Icon::Lock : Icon::Unlock, icon,
                     m_topLocked ? drawInk : drawInk, m_topLocked ? 1.75 : 1.5);
            break;
        case Action::Pin: drawIcon(p, Icon::Pin, icon, drawInk, m_pinned ? 1.8 : 1.55); break;
        case Action::Menu: drawIcon(p, Icon::Hamburger, icon, drawInk); break;
        case Action::Minimize:
            drawIcon(p, Icon::MinWindow, QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink, 1.4);
            break;
        case Action::Maximize:
            drawIcon(p, window() && window()->isMaximized() ? Icon::MinWindow : Icon::MaxWindow,
                     QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink, 1.4);
            break;
        case Action::Close:
            drawIcon(p, Icon::Close, QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink, 1.4);
            break;
        default: break;
        }

        // 带文字的按钮：图标在左，文字随后（文字光学中心与 18px 图标中心同线）。
        if (!b.iconOnly && !b.label.isEmpty()) {
            const QFont f = uiFont(12);
            p.setFont(f);
            p.setPen(drawInk);
            const int textX = b.rect.x() + 10 + 18 + 8;
            const QRect textRect(textX, b.rect.y(), b.rect.right() - textX - 10,
                                 b.rect.height());
            p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, b.label);
            if (b.caret) {
                drawIcon(p, Icon::ChevronDown,
                         QRectF(b.rect.right() - 16, center.y() - 5, 10, 10), mutedInk, 1.4);
            }
        }
        endButton(p);
    }
}

void TopBar::activate(Action action, const QPoint &globalPos)
{
    switch (action) {
    case Action::Open: emit openClicked(); break;
    case Action::Browse: emit browseMenuRequested(globalPos); break;
    case Action::Slideshow: emit slideshowMenuRequested(globalPos); break;
    case Action::Bookmark: emit bookmarkMenuRequested(globalPos); break;
    case Action::Edit: emit editMenuRequested(globalPos); break;
    case Action::CopyTo: emit copytoMenuRequested(globalPos); break;
    case Action::Layers:
        if (m_layersAvailable) {
            emit layersClicked();
        }
        break;
    case Action::TopLock: emit topLockToggled(); break;
    case Action::Pin: emit pinToggled(); break;
    case Action::Menu: emit menuRequested(globalPos); break;
    case Action::Minimize: emit minimizeClicked(); break;
    case Action::Maximize: emit maximizeClicked(); break;
    case Action::Close: emit closeClicked(); break;
    default: break;
    }
}

void TopBar::moveFocus(int step)
{
    const int n = buttons().size();
    if (n <= 0) {
        return;
    }
    int next = (m_focus < 0) ? (step > 0 ? 0 : n - 1) : m_focus + step;
    if (next < 0 || next >= n) {
        m_focus = -1;
        m_keyboardFocus = false;
        m_feedback->setHover(-1);
        update();
        focusNextPrevChild(step > 0);
        return;
    }
    m_focus = next;
    m_feedback->setHover(next);
    update();
}

void TopBar::mousePressEvent(QMouseEvent *e)
{
    m_tip->hideNow();
    if (e->button() == Qt::LeftButton) {
        if (m_keyboardFocus) {
            m_keyboardFocus = false;
            m_focus = -1;
        }
        m_press = hitTest(e->pos());
        m_feedback->setPressed(m_press);
        update();
        if (m_press >= 0) {
            return;
        }
        if (window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
        }
        return;
    }
    QWidget::mousePressEvent(e);
}

void TopBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    m_feedback->setPressed(-1);
    update();
    if (e->button() != Qt::LeftButton || i < 0 || i != hitTest(e->pos())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    const QVector<Btn> list = buttons();
    if (i < list.size()) {
        activate(list[i].action, e->globalPosition().toPoint());
    }
    QWidget::mouseReleaseEvent(e);
}

void TopBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    m_feedback->setHover(m_hover);
    setPointerCursor(this, m_hover >= 0);
    if (e->buttons() != Qt::NoButton) {
        m_tip->hideNow();
    } else if (m_hover >= 0) {
        const Btn &b = buttons().at(m_hover);
        const QPoint anchor = (b.action == Action::TopLock || b.action == Action::Pin)
                                  ? mapToGlobal(b.rect.center())
                                  : e->globalPosition().toPoint();
        m_tip->hover(tipText(b.action), anchor);
    } else {
        m_tip->hideNow();
    }
    update();
    QWidget::mouseMoveEvent(e);
}

void TopBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && hitTest(e->pos()) < 0) {
        emit maximizeClicked();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void TopBar::contextMenuEvent(QContextMenuEvent *e)
{
    emit systemMenuRequested(e->globalPos());
    e->accept();
}

void TopBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
    m_tip->hideNow();
    update();
}

void TopBar::wheelEvent(QWheelEvent *e)
{
    m_tip->hideNow();
    QWidget::wheelEvent(e);
}

void TopBar::focusInEvent(QFocusEvent *e)
{
    const bool byKeyboard = (e->reason() == Qt::TabFocusReason
                             || e->reason() == Qt::BacktabFocusReason
                             || e->reason() == Qt::ShortcutFocusReason);
    m_keyboardFocus = byKeyboard;
    m_focus = byKeyboard ? 0 : -1;
    if (byKeyboard) {
        m_feedback->setHover(qMax(0, m_focus));
    }
    update();
    QWidget::focusInEvent(e);
}

void TopBar::focusOutEvent(QFocusEvent *e)
{
    m_focus = -1;
    m_keyboardFocus = false;
    m_press = -1;
    m_feedback->reset();
    m_tip->hideNow();
    update();
    QWidget::focusOutEvent(e);
}

bool TopBar::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride && m_keyboardFocus) {
        auto *ke = static_cast<QKeyEvent *>(e);
        switch (ke->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Escape:
            ke->accept();
            return true;
        default:
            break;
        }
    }
    return QWidget::event(e);
}

void TopBar::keyPressEvent(QKeyEvent *e)
{
    if (!m_keyboardFocus) {
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        moveFocus((e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) ? -1 : 1);
        e->accept();
        return;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!e->isAutoRepeat() && m_focus >= 0) {
            m_press = m_focus;
            m_feedback->setPressed(m_press);
            m_feedback->setHover(m_focus);
            update();
        }
        e->accept();
        return;
    case Qt::Key_Escape:
        clearFocus();
        e->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void TopBar::keyReleaseEvent(QKeyEvent *e)
{
    if (m_keyboardFocus
        && (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return
            || e->key() == Qt::Key_Enter)
        && !e->isAutoRepeat()) {
        const int i = m_press;
        m_press = -1;
        m_feedback->setPressed(-1);
        update();
        const QVector<Btn> list = buttons();
        if (i >= 0 && i == m_focus && i < list.size()) {
            activate(list[i].action, mapToGlobal(list[i].rect.center()));
        }
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

// ===========================================================================
// 底部控制岛（54px 胶囊）
// ===========================================================================

namespace {

constexpr int kIslandH = 54;
constexpr int kIslandWidgetH = 64;
constexpr int kIslandPadX = 10;
constexpr int kIslandBtn = 34;      // 普通按钮热区（含锁定）
constexpr int kIslandNavBtn = 36;   // 上一张/下一张热区
constexpr int kIslandY = (kIslandH - kIslandBtn) / 2;
constexpr int kIslandInnerGap = 4;
constexpr int kIslandGroupGap = 8;
constexpr int kIslandTrackWide = 168;     // 宽窗轨道上限
constexpr int kIslandTrackStandard = 150; // 标准轨道（取代 188 长轨道）
constexpr int kIslandTrackNarrow = 119;
constexpr int kZoomCapsuleW = 72;   // 缩放胶囊：点击在 100% / 适合窗口 间切换
constexpr int kZoomCapsuleH = 32;   // 高 32 → 圆角取一半，两端为完整半圆
constexpr int kPageValueW = 44;
constexpr int kProgressPadX = 10;    // 进度块左右内边距
constexpr int kProgressInnerGap = 8; // 滑杆与页码之间

} // namespace

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(320, kIslandWidgetH);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("底部控制栏"));
    setAccessibleDescription(tr("打开、缩略图、缩放、旋转、全屏、上一张、进度、下一张与锁定"));
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
    m_tip = new TooltipHint(this);
    m_zoomFade = new QVariantAnimation(this);
    m_zoomFade->setDuration(100);
    m_zoomFade->setStartValue(0.0);
    m_zoomFade->setEndValue(1.0);
    m_zoomFade->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_zoomFade, &QVariantAnimation::valueChanged, this, [this]() { update(); });
    connect(m_zoomFade, &QVariantAnimation::finished, this, [this]() {
        m_prevZoomText.clear();
        update();
    });
    recomputeWidth();
}

void ControlBar::setPage(int current, int total)
{
    m_pageCount = qMax(0, total);
    m_page = m_pageCount > 0 ? qBound(1, current, m_pageCount) : 1;
    update();
}

void ControlBar::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    // 锁按钮恒为最后一项：用 checked 插值驱动两层视觉（透明底/蓝圆）交叉淡化。
    const QVector<Item> list = items();
    if (!list.isEmpty()) {
        m_feedback->setChecked(list.size() - 1, locked);
    }
    update();
}

void ControlBar::setZoomState(const QString &text, int kind)
{
    if (m_zoomText == text && m_zoomKind == kind) {
        return;
    }
    m_prevZoomText = m_zoomText;
    m_zoomText = text;
    m_zoomKind = kind;
    if (reducedMotion()) {
        m_zoomFade->stop();
        m_prevZoomText.clear();
        update();
        return;
    }
    m_zoomFade->stop();
    m_zoomFade->start();
    update();
}

void ControlBar::setWeakened(bool weakened)
{
    if (m_weakened == weakened) {
        return;
    }
    m_weakened = weakened;
    update();
}

bool ControlBar::keepVisible() const
{
    // 判定「底岛仍在操作中」时，弹窗必须限定为底岛自己拥有的菜单。
    // QApplication::activePopupWidget() 是全局量：画布右键菜单、设置页 QComboBox
    // 下拉、顶栏三横菜单都会点亮它。旧逻辑只判 != nullptr，等价于「任何弹窗
    // 都算底岛操作中」，于是这些与底岛无关的菜单会把本该隐藏的悬浮岛唤出来。
    // 顶栏已有 isTopBarPopup 做同样的归属判定，这里与之对称、互不影响。
    bool ownPopup = false;
    for (QObject *o = QApplication::activePopupWidget(); o; o = o->parent()) {
        if (o == this || o->property("dockOwnedPopup").toBool()) {
            ownPopup = true;
            break;
        }
    }
    // 其余合法保持条件不变：鼠标真正悬停岛体 / 键盘焦点 / 滑杆拖动。
    // 底部热区与底栏锁定由 MainWindow::updateDockProximity 单独判定。
    return m_keyboardFocus || m_dragging || m_hover >= 0 || ownPopup;
}

// 档位阈值按「该档整条岛的自然宽度 + 左右各 24px 呼吸位」定，
// 这样不会出现「岛比可用宽度还宽」的挤出，也不会过早收掉低频项。
ControlBar::Tier ControlBar::tier() const
{
    if (m_availableWidth >= 736) return Tier::Full;      // 全组 + 150 轨道 ≈ 688（宽窗 168 → ≈ 706）
    if (m_availableWidth >= 705) return Tier::Compact;   // 全组 + 119 轨道 ≈ 657
    if (m_availableWidth >= 625) return Tier::Narrow;    // 收旋转/全屏 ≈ 577
    if (m_availableWidth >= 545) return Tier::Minimal;   // 再收打开/缩略图 ≈ 497
    return Tier::Tiny;                                   // 再收缩放加减 ≈ 425
}

bool ControlBar::showGroup1() const
{
    const Tier t = tier();
    return t == Tier::Full || t == Tier::Compact || t == Tier::Narrow;
}

bool ControlBar::showGroup3() const
{
    const Tier t = tier();
    return t == Tier::Full || t == Tier::Compact;
}

bool ControlBar::showZoomButtons() const
{
    return tier() != Tier::Tiny;
}

void ControlBar::setAvailableWidth(int width)
{
    const int w = qMax(200, width);
    if (w == m_availableWidth) {
        return;
    }
    m_availableWidth = w;
    recomputeWidth();
}

void ControlBar::recomputeWidth()
{
    // 滑杆三档响应：宽窗 168 / 标准 150 / 窄窗 119（胶囊与按钮不参与收窄）。
    m_trackWidth = (tier() == Tier::Full)
                       ? (m_availableWidth >= 754 ? kIslandTrackWide : kIslandTrackStandard)
                       : kIslandTrackNarrow;
    QVector<Item> list = items();
    int width = list.isEmpty() ? 240 : list.last().rect.right() + 1 + kIslandPadX;
    // 极窄窗：先缩短轨道，保留翻页热区、页码与倍率。
    if (width > m_availableWidth) {
        const int excess = width - m_availableWidth;
        m_trackWidth = qMax(48, m_trackWidth - excess);
        list = items();
        width = list.isEmpty() ? 240 : list.last().rect.right() + 1 + kIslandPadX;
    }
    m_preferredWidth = width;
    setFixedWidth(width);
    // 档位切换会移动锁按钮的索引（恒为最后一项），同步选中态插值目标。
    m_feedback->setChecked(list.isEmpty() ? -1 : list.size() - 1, m_locked);
    update();
}

QVector<ControlBar::Item> ControlBar::items() const
{
    QVector<Item> out;
    int x = kIslandPadX;
    const int y = kIslandY;

    auto place = [&](Action a, int w, int h = kIslandBtn) {
        out.append(Item{ a, QRect(x, y + (kIslandBtn - h) / 2, w, h) });
        x += w;
    };

    if (showGroup1()) {
        place(Action::Open, kIslandBtn);
        x += kIslandInnerGap;
        place(Action::List, kIslandBtn);
        x += kIslandGroupGap;
    }

    if (showZoomButtons()) {
        place(Action::ZoomOut, kIslandBtn);
        x += kIslandInnerGap;
    }
    place(Action::ZoomValue, kZoomCapsuleW, kZoomCapsuleH);
    x += kIslandInnerGap;
    if (showZoomButtons()) {
        place(Action::ZoomIn, kIslandBtn);
    }
    x += kIslandGroupGap;

    if (showGroup3()) {
        place(Action::Rotate, kIslandBtn);
        x += kIslandInnerGap;
        place(Action::Fullscreen, kIslandBtn);
        x += kIslandGroupGap;
    }

    place(Action::Prev, kIslandNavBtn, kIslandNavBtn);
    x += 8;
    // 进度块：滑杆 + 页码放在同一个深色内嵌圆角里（对齐概念图 01/03）。
    x += kProgressPadX;
    place(Action::PageSlider, m_trackWidth);
    x += kProgressInnerGap;
    place(Action::PageValue, kPageValueW);
    x += kProgressPadX + 8;
    place(Action::Next, kIslandNavBtn, kIslandNavBtn);
    x += kIslandGroupGap;

    place(Action::Lock, kIslandBtn);

    return out;
}

// 进度块外框（滑杆 + 页码 + 左右内边距）。
QRect ControlBar::progressRect() const
{
    QRect track;
    QRect page;
    for (const Item &it : items()) {
        if (it.action == Action::PageSlider) {
            track = it.rect;
        } else if (it.action == Action::PageValue) {
            page = it.rect;
        }
    }
    if (track.isNull() || page.isNull()) {
        return QRect();
    }
    return QRect(track.left() - kProgressPadX, kIslandY - 3,
                 page.right() + kProgressPadX - (track.left() - kProgressPadX) + 1,
                 kIslandBtn + 6);
}

QRect ControlBar::sliderRect() const
{
    const QVector<Item> list = items();
    for (const Item &it : list) {
        if (it.action == Action::PageSlider) {
            return it.rect;
        }
    }
    return QRect();
}

int ControlBar::hitIndex(const QPoint &pos) const
{
    const QVector<Item> list = items();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

int ControlBar::clampedPage(int page) const
{
    return qBound(1, page, qMax(1, m_pageCount));
}

int ControlBar::sliderPageFromX(int x) const
{
    const QRect track = sliderRect();
    if (track.isNull()) {
        return m_page;
    }
    const qreal left = track.left() + 7.0;
    const qreal right = track.right() - 7.0;
    const qreal ratio = qBound(0.0, (x - left) / qMax(1.0, right - left), 1.0);
    const int maxPage = qMax(1, m_pageCount);
    return qBound(1, qRound(1.0 + ratio * (maxPage - 1)), maxPage);
}

QString ControlBar::tipText(Action action) const
{
    switch (action) {
    case Action::Open: return tr("打开");
    case Action::List: return tr("缩略图栏");
    case Action::ZoomOut: return tr("缩小");
    case Action::ZoomValue:
        // 提示跟着下一步操作走：100% 时点它去适合窗口，其余状态都先回原始尺寸。
        return m_zoomKind == 1 ? tr("适合窗口") : tr("查看原始尺寸（100%）");
    case Action::ZoomIn: return tr("放大");
    case Action::Rotate: return tr("旋转：左键逆时针 90°，右键顺时针 90°");
    case Action::Fullscreen: return tr("全屏 / 还原 (F11)");
    case Action::Prev: return tr("上一张图片 (←)");
    case Action::PageSlider:
        return sliderEnabled() ? tr("浏览进度：拖动切换图片；滚轮上一张/下一张")
                               : tr("浏览进度：只有一张图片");
    case Action::PageValue: return tr("当前页码 / 总页数");
    case Action::Next: return tr("下一张图片 (→)");
    case Action::Lock: return tr("保持底部控制栏显示");
    default: break;
    }
    return QString();
}

void ControlBar::applyPageFromX(int x)
{
    const int page = sliderPageFromX(x);
    if (page == m_page) {
        return;
    }
    m_page = page;
    update();
    if (m_dragging && (!m_previewClock.isValid() || m_previewClock.elapsed() >= 60)) {
        m_previewClock.restart();
        emit pagePreviewRequested(m_page);
    }
}

void ControlBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (m_weakened) {
        p.setOpacity(0.45);
    }

    const QRectF capsule(0.5, 0.5, width() - 1.0, kIslandH - 1.0);

    // 软阴影：胶囊下方三层，越远越淡（不依赖系统阴影，截图可复现）。
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const qreal grow = 2.0 + i * 2.5;
        QColor sh(0, 0, 0, 34 - i * 9);
        p.setBrush(sh);
        p.drawRoundedRect(capsule.adjusted(-grow, grow, grow, grow + 2),
                          (kIslandH + 2 * grow) / 2.0, (kIslandH + 2 * grow) / 2.0);
    }

    p.setPen(QPen((UiStyle::isLight() ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 30)), 1));
    p.setBrush(UiStyle::isLight() ? QColor(250, 250, 251, 242)
                                  : QColor(31, 32, 35, 227));
    p.drawRoundedRect(capsule, 27, 27);

    // 进度块：比胶囊略深的嵌入式圆角底（滑杆 + 页码同区）。
    const QRect prog = progressRect();
    if (!prog.isNull()) {
        p.setPen(Qt::NoPen);
        p.setBrush(UiStyle::isLight() ? QColor(0, 0, 0, 20) : QColor(0, 0, 0, 48));
        p.drawRoundedRect(QRectF(prog).adjusted(0.5, 0.5, -0.5, -0.5), 17, 17);
    }

    const QVector<Item> list = items();
    const QColor ink = QColor(QString::fromLatin1(UiStyle::pick("#d7d9dc", "#26282a")));
    const QColor accent = UiStyle::accent();

    for (int i = 0; i < list.size(); ++i) {
        const Item &it = list.at(i);
        const QRect r = it.rect;
        BtnVisual v = m_feedback->visual(i);

        if (it.action == Action::PageSlider) {
            const bool enabled = sliderEnabled();
            if (!enabled) {
                p.setOpacity(p.opacity() * 0.58);
            }
            const qreal y = r.center().y() + 0.5;
            const qreal left = r.left() + 7.0;
            const qreal right = r.right() - 7.0;
            QColor idle(QString::fromLatin1(UiStyle::pick("#626365", "#c4c8cc")));
            if (v.press > 0.004) {
                idle = blendColor(idle, QColor(0x8a, 0x8e, 0x94), 0.55 * v.press);
            }
            p.setPen(QPen(idle, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(left, y), QPointF(right, y));
            // 只有一张图时滑杆没有可拖动范围：不画蓝色进度、滑块停在左端并灰显，
            // 避免「满格」被误读成已到目录末尾。
            const qreal ratio = enabled ? (m_page - 1) / qMax(1.0, qreal(m_pageCount - 1)) : 0.0;
            const qreal thumbX = left + ratio * (right - left);
            if (enabled) {
                // 概念图 03 的进度填充为亮蓝 #61b1ff，比界面强调色更偏蓝。
                const QColor fill(QString::fromLatin1(UiStyle::pick("#61b1ff", "#1688f8")));
                p.setPen(QPen(fill, 3, Qt::SolidLine, Qt::RoundCap));
                p.drawLine(QPointF(left, y), QPointF(thumbX, y));
            }
            const qreal grow = 1.0 + qMax(0.12 * v.hover, 0.22 * v.press);
            QColor thumb(QStringLiteral("#f5f5f5"));
            if (!enabled) {
                thumb = QColor(QString::fromLatin1(UiStyle::pick("#8a8e94", "#a8acb1")));
            } else if (qMax(v.hover, v.press) > 0.01) {
                thumb = blendColor(thumb, accent, qMax(v.hover, v.press));
            }
            p.setPen(Qt::NoPen);
            p.setBrush(thumb);
            p.drawEllipse(QPointF(thumbX, y), 5.5 * grow, 5.5 * grow);
            continue;
        }

        if (it.action == Action::ZoomValue) {
            // 缩放胶囊：中性深底常亮，悬停提亮、按下加深，只走颜色过渡——
            // 不套 beginButton 的按压缩放，也不占蓝色选中态，避免与锁定、进度滑杆抢重点。
            const QColor base(QString::fromLatin1(UiStyle::pick("#35373b", "#e7e9eb")));
            const QColor hot(QString::fromLatin1(UiStyle::pick("#3e4147", "#dcdfe2")));
            const QColor down(QString::fromLatin1(UiStyle::pick("#2a2c30", "#cfd3d7")));
            QColor bg = base;
            const qreal over = qMax(v.hover, v.press);
            if (over > 0.004) {
                bg = blendColor(base, hot, over);
            }
            if (v.press > 0.004) {
                bg = blendColor(bg, down, v.press);
            }
            const QRectF cap(r);
            const qreal capRadius = kZoomCapsuleH / 2.0;
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(cap.adjusted(0.5, 0.5, -0.5, -0.5), capRadius, capRadius);
            // 焦点环只在键盘操作时出现：鼠标按下即清 m_keyboardFocus，不残留。
            if (m_keyboardFocus && m_focus == i) {
                p.setPen(QPen(QColor(85, 192, 240, 215), 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(cap.adjusted(1.5, 1.5, -1.5, -1.5),
                                  capRadius, capRadius);
            }
            // 档位文字交叉淡化：胶囊宽度固定，只换内容不伸缩。
            const qreal fadeT = m_zoomFade->state() == QAbstractAnimation::Running
                                    ? m_zoomFade->currentValue().toReal() : 1.0;
            if (!m_prevZoomText.isEmpty() && fadeT < 0.999) {
                QColor prevInk = ink;
                prevInk.setAlphaF(prevInk.alphaF() * (1.0 - fadeT));
                drawTabularText(p, cap, m_prevZoomText, prevInk, 12);
                QColor curInk = ink;
                curInk.setAlphaF(curInk.alphaF() * fadeT);
                drawTabularText(p, cap, m_zoomText, curInk, 12);
            } else {
                drawTabularText(p, cap, m_zoomText, ink, 12);
            }
            continue;
        }
        if (it.action == Action::PageValue) {
            const QString text = m_pageCount > 0
                                     ? tr("%1 / %2").arg(m_page).arg(m_pageCount)
                                     : QStringLiteral("– / –");
            drawTabularText(p, QRectF(r), text, ink, 12);
            continue;
        }

        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.focus = (m_keyboardFocus && m_focus == i);
        st.checked = (it.action == Action::Lock && m_locked);

        ButtonPalette pal;
        pal.radius = 17;
        pal.hover = QColor(QString::fromLatin1(UiStyle::pick("#35373b", "#e7e9eb")));
        pal.press = QColor(QString::fromLatin1(UiStyle::pick("#292b2f", "#dcdfe2")));
        pal.ink = ink;
        pal.checkedBg = UiStyle::accentSoft();
        pal.checkedBorder = QColor(accent.red(), accent.green(), accent.blue(), 60);
        pal.checkedInk = accent;

        const QPointF center = r.center();

        if (it.action == Action::Lock) {
            // 锁定态交叉淡化：透明底（开）↔ 蓝圆底+深锁（锁）两层按 checked 插值
            // 叠画；底色与锁图标同步 120ms 过渡，不做位移或缩放。蓝圆底与顶栏
            // 透明底明确区分。
            const qreal chk = v.checked;
            const qreal lockS = 20.0 * (1.0 - 0.02 * v.press);
            if (chk < 0.999) {
                p.save();
                p.setOpacity(p.opacity() * (1.0 - chk));
                BtnState stU = st;
                stU.checked = false;
                beginButton(p, circleVisual(r), stU, v, pal);
                drawIcon(p, Icon::Unlock,
                         QRectF(center.x() - lockS / 2, center.y() - lockS / 2, lockS, lockS),
                         buttonInk(stU, pal), 1.7);
                endButton(p);
                p.restore();
            }
            if (chk > 0.001) {
                p.save();
                p.setOpacity(p.opacity() * chk);
                const QRectF vr = circleVisual(r);
                p.setPen(Qt::NoPen);
                QColor blue(QStringLiteral("#1688f8"));
                if (st.hover || st.pressed) {
                    blue = blue.lighter(112);
                }
                p.setBrush(blue);
                p.drawEllipse(vr);
                drawIcon(p, Icon::Lock,
                         QRectF(center.x() - lockS / 2, center.y() - lockS / 2, lockS, lockS),
                         QColor(QStringLiteral("#0b1a24")), 1.9);
                p.restore();
            }
            continue;
        }

        beginButton(p, circleVisual(r), st, v, pal);
        const QColor drawInk = buttonInk(st, pal);
        // 按下时图标轻微缩至 98%（不放大、不上浮；松开 100ms 恢复）。
        const qreal iconS = 20.0 * (1.0 - 0.02 * v.press);
        const qreal navS = 21.0 * (1.0 - 0.02 * v.press);
        const QRectF icon(center.x() - iconS / 2, center.y() - iconS / 2, iconS, iconS);
        const QRectF navIcon(center.x() - navS / 2, center.y() - navS / 2, navS, navS);

        switch (it.action) {
        case Action::Open: drawIcon(p, Icon::Folder, icon, drawInk, 1.7); break;
        case Action::List: drawIcon(p, Icon::Grid, icon, drawInk, 1.7); break;
        case Action::ZoomOut: drawIcon(p, Icon::Minus, icon, drawInk, 1.7); break;
        case Action::ZoomIn: drawIcon(p, Icon::Plus, icon, drawInk, 1.7); break;
        case Action::Rotate: drawIcon(p, Icon::RotateLeft, icon, drawInk, 1.7); break;
        case Action::Fullscreen: drawIcon(p, Icon::Fullscreen, icon, drawInk, 1.7); break;
        case Action::Prev: drawIcon(p, Icon::Left, navIcon, drawInk, 2.0); break;
        case Action::Next: drawIcon(p, Icon::Right, navIcon, drawInk, 2.0); break;
        case Action::Lock: drawIcon(p, Icon::Unlock, icon, drawInk, 1.7); break;
        default: break;
        }
        endButton(p);
    }
}

void ControlBar::activate(Action action, Qt::MouseButton button)
{
    switch (action) {
    case Action::Open: emit openClicked(); break;
    case Action::List: emit listClicked(); break;
    case Action::ZoomIn: emit zoomInClicked(); break;
    case Action::ZoomOut: emit zoomOutClicked(); break;
    case Action::ZoomValue: emit zoomToggleClicked(); break;
    case Action::Rotate:
        if (button == Qt::RightButton) {
            emit rotateRightClicked();
        } else {
            emit rotateLeftClicked();
        }
        break;
    case Action::Fullscreen: emit fullscreenClicked(); break;
    case Action::Prev:
        if (button == Qt::RightButton) emit prev10Clicked();
        else if (button == Qt::MiddleButton) emit prevFolderClicked();
        else emit prevClicked();
        break;
    case Action::Next:
        if (button == Qt::RightButton) emit next10Clicked();
        else if (button == Qt::MiddleButton) emit nextFolderClicked();
        else emit nextClicked();
        break;
    case Action::Lock: emit lockToggled(); break;
    default: break;
    }
}

void ControlBar::probeHover(int index)
{
    m_hover = index;
    m_feedback->setHover(index);
    const QVector<Item> list = items();
    if (index >= 0 && index < list.size()) {
        m_tip->hover(tipText(list[index].action),
                     mapToGlobal(list[index].rect.center()));
    }
    update();
}

void ControlBar::probePress(int index)
{
    m_press = index;
    m_feedback->setPressed(index);
    update();
}

void ControlBar::moveFocus(int step)
{
    const int n = items().size();
    if (n <= 0) {
        return;
    }
    const int next = (m_focus < 0) ? (step > 0 ? 0 : n - 1) : m_focus + step;
    if (next < 0 || next >= n) {
        m_focus = -1;
        m_keyboardFocus = false;
        m_feedback->setHover(-1);
        update();
        focusNextPrevChild(step > 0);
        return;
    }
    m_focus = next;
    m_feedback->setHover(next);
    update();
}

void ControlBar::mousePressEvent(QMouseEvent *e)
{
    m_tip->hideNow();
    const QVector<Item> list = items();
    const int idx = hitIndex(e->pos());
    const Action action = (idx >= 0 && idx < list.size()) ? list[idx].action : Action::None;

    if (e->button() == Qt::RightButton) {
        if (action == Action::Prev) { emit prev10Clicked(); return; }
        if (action == Action::Next) { emit next10Clicked(); return; }
        if (action == Action::Rotate) { emit rotateRightClicked(); return; }
    }
    if (e->button() == Qt::MiddleButton) {
        if (action == Action::Prev) { emit prevFolderClicked(); return; }
        if (action == Action::Next) { emit nextFolderClicked(); return; }
    }
    if (e->button() == Qt::LeftButton) {
        if (m_keyboardFocus) {
            m_keyboardFocus = false;
            m_focus = -1;
        }
        m_press = idx;
        m_feedback->setPressed(idx);
        if (action == Action::PageSlider && sliderEnabled()) {
            m_dragging = true;
            m_pressPage = m_page;
            m_previewClock.invalidate();
            applyPageFromX(e->pos().x());
            return;
        }
        update();
    }
    QWidget::mousePressEvent(e);
}

void ControlBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int pressedIndex = m_press;
    m_press = -1;
    m_feedback->setPressed(-1);
    if (m_dragging) {
        m_dragging = false;
        update();
        if (pressedIndex >= 0 && pressedIndex < items().size()
            && items()[pressedIndex].action == Action::PageSlider
            && m_page != m_pressPage) {
            emit pageRequested(m_page);
        }
        m_pressPage = m_page;
        QWidget::mouseReleaseEvent(e);
        return;
    }
    update();
    if (e->button() != Qt::LeftButton || pressedIndex < 0
        || pressedIndex != hitIndex(e->pos())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    const QVector<Item> list = items();
    if (pressedIndex < list.size()) {
        activate(list[pressedIndex].action, e->button());
    }
    QWidget::mouseReleaseEvent(e);
}

void ControlBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        m_tip->hideNow();
        applyPageFromX(e->pos().x());
        return;
    }
    const QVector<Item> list = items();
    m_hover = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(e->pos())) {
            m_hover = i;
            break;
        }
    }
    m_feedback->setHover(m_hover);
    if (m_hover >= 0 && list[m_hover].action == Action::PageSlider) {
        setCursor(sliderEnabled() ? Qt::SizeHorCursor : Qt::ArrowCursor);
    } else {
        setPointerCursor(this, m_hover >= 0);
    }
    if (e->buttons() != Qt::NoButton) {
        m_tip->hideNow();
    } else if (m_hover >= 0) {
        const Item &it = list.at(m_hover);
        const QPoint anchor = (it.action == Action::Lock || it.action == Action::Rotate)
                                  ? mapToGlobal(it.rect.center())
                                  : e->globalPosition().toPoint();
        m_tip->hover(tipText(it.action), anchor);
    } else {
        m_tip->hideNow();
    }
    update();
    QWidget::mouseMoveEvent(e);
}

void ControlBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
    m_tip->hideNow();
    update();
}

void ControlBar::focusInEvent(QFocusEvent *e)
{
    const bool byKeyboard = (e->reason() == Qt::TabFocusReason
                             || e->reason() == Qt::BacktabFocusReason
                             || e->reason() == Qt::ShortcutFocusReason);
    m_keyboardFocus = byKeyboard;
    m_focus = byKeyboard ? 0 : -1;
    if (byKeyboard) {
        m_feedback->setHover(m_focus);
    }
    update();
    QWidget::focusInEvent(e);
}

void ControlBar::focusOutEvent(QFocusEvent *e)
{
    m_focus = -1;
    m_keyboardFocus = false;
    m_press = -1;
    m_dragging = false;
    m_feedback->reset();
    m_tip->hideNow();
    update();
    QWidget::focusOutEvent(e);
}

bool ControlBar::event(QEvent *e)
{
    if (e->type() == QEvent::ShortcutOverride && m_keyboardFocus) {
        auto *ke = static_cast<QKeyEvent *>(e);
        switch (ke->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Space:
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Escape:
            ke->accept();
            return true;
        default:
            break;
        }
    }
    return QWidget::event(e);
}

void ControlBar::keyPressEvent(QKeyEvent *e)
{
    if (!m_keyboardFocus) {
        QWidget::keyPressEvent(e);
        return;
    }
    if (m_focus >= 0 && m_focus < items().size()
        && items()[m_focus].action == Action::PageSlider) {
        if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
            const int step = e->key() == Qt::Key_Left ? -1 : 1;
            if (sliderEnabled()) {
                const int page = clampedPage(m_page + step);
                if (page != m_page) {
                    m_page = page;
                    update();
                    emit pageRequested(m_page);
                }
            }
            e->accept();
            return;
        }
    }
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
        moveFocus((e->key() == Qt::Key_Left || e->key() == Qt::Key_Up) ? -1 : 1);
        e->accept();
        return;
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!e->isAutoRepeat() && m_focus >= 0) {
            m_press = m_focus;
            m_feedback->setPressed(m_press);
            m_feedback->setHover(m_focus);
            update();
        }
        e->accept();
        return;
    case Qt::Key_Escape:
        clearFocus();
        e->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void ControlBar::keyReleaseEvent(QKeyEvent *e)
{
    if (m_keyboardFocus
        && (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return
            || e->key() == Qt::Key_Enter)
        && !e->isAutoRepeat()) {
        const int i = m_press;
        m_press = -1;
        m_feedback->setPressed(-1);
        update();
        const QVector<Item> list = items();
        if (i >= 0 && i == m_focus && i < list.size() && list[i].action != Action::PageSlider) {
            activate(list[i].action, Qt::LeftButton);
        }
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void ControlBar::wheelEvent(QWheelEvent *e)
{
    m_tip->hideNow();
    const QVector<Item> list = items();
    const int idx = hitIndex(e->position().toPoint());
    const bool onSlider = (idx >= 0 && idx < list.size()
                           && list[idx].action == Action::PageSlider);
    const int delta = e->angleDelta().y();
    if (onSlider && sliderEnabled()) {
        if (delta > 0) {
            emit prevClicked();
        } else if (delta < 0) {
            emit nextClicked();
        }
    }
    e->accept();
}


// ===========================================================================
// 状态浮层
// ===========================================================================

StatusOverlay::StatusOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
}

void StatusOverlay::setImageInfo(const QString &text)
{
    m_info = text;
    update();
}

void StatusOverlay::setCounter(const QString &text)
{
    m_counter = text;
    update();
}

void StatusOverlay::clear()
{
    m_info.clear();
    m_counter.clear();
    update();
}

void StatusOverlay::setBottomInset(int inset)
{
    const int v = qMax(0, inset);
    if (m_bottomInset == v) {
        return;
    }
    m_bottomInset = v;
    update();
}

void StatusOverlay::setSmallFont(bool small)
{
    m_smallFont = small;
    update();
}

void StatusOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(uiFont(m_smallFont ? 10 : 11));
    const int baselineTop = height() - 25 - m_bottomInset;
    const QRect infoRect(16, baselineTop, width() / 2, 16);
    const QRect counterRect(width() / 2, baselineTop, width() / 2 - 17, 16);
    // 信息行叠在图片上：深色主题补一层 1px 暗影，亮图上也能读清。
    if (!UiStyle::isLight()) {
        p.setPen(QColor(0, 0, 0, 150));
        p.drawText(infoRect.translated(0, 1), Qt::AlignVCenter | Qt::AlignLeft, m_info);
        p.drawText(counterRect.translated(0, 1), Qt::AlignVCenter | Qt::AlignRight, m_counter);
    }
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#797d82", "#868a8f"))));
    p.drawText(infoRect, Qt::AlignVCenter | Qt::AlignLeft, m_info);
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#85898e", "#7a7e83"))));
    p.drawText(counterRect, Qt::AlignVCenter | Qt::AlignRight, m_counter);
}

// ===========================================================================
// 空状态
// ===========================================================================

EmptyState::EmptyState(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_NoSystemBackground);
}

QRect EmptyState::primaryRect() const
{
    const int y = (height() - 340) / 2;
    return QRect(width() / 2 - 123, y + 197, 120, 36);
}

QRect EmptyState::secondaryRect() const
{
    const int y = (height() - 340) / 2;
    return QRect(width() / 2 + 7, y + 197, 116, 36);
}

void EmptyState::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int top = (height() - 340) / 2;
    const QRectF iconRect(width() / 2.0 - 38, top, 76, 76);
    p.setPen(QPen((UiStyle::isLight() ? QColor(0,0,0,31) : QColor(255,255,255,31)), 1));
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#232323", "#f4f4f4"))));
    p.drawRoundedRect(iconRect, 18, 18);
    drawIcon(p, Icon::Picture,
             QRectF(iconRect.center().x() - 17, iconRect.center().y() - 17, 34, 34),
             QColor(QString::fromLatin1(UiStyle::pick("#bdbdbd", "#444444"))), 1.35);

    p.setFont(uiFont(22, QFont::Medium));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#f3f3f3", "#3a3a3a"))));
    p.drawText(QRect(0, top + 94, width(), 30), Qt::AlignCenter,
               QStringLiteral("开始查看图片"));

    p.setFont(uiFont(13));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#999999", "#666666"))));
    p.drawText(QRect(0, top + 132, width(), 22), Qt::AlignCenter,
               QStringLiteral("把图片拖到这里，或从电脑中选择一张图片"));
    p.drawText(QRect(0, top + 154, width(), 22), Qt::AlignCenter,
               QStringLiteral("也可以直接打开整个文件夹"));

    const QRect primary = primaryRect();
    const QRect secondary = secondaryRect();

    p.setPen(QPen(QColor(QStringLiteral("#2485b3")), 1));
    p.setBrush(m_hover == 0 ? QColor(QStringLiteral("#0e7fb5"))
                            : QColor(QStringLiteral("#0b6f9f")));
    p.drawRoundedRect(QRectF(primary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    drawIcon(p, Icon::Folder,
             QRectF(primary.x() + 18, primary.center().y() - 8, 16, 16),
             QColor(Qt::white));
    p.setFont(uiFont(12, QFont::DemiBold));
    p.setPen(QColor(Qt::white));
    p.drawText(QRect(primary.x() + 40, primary.y(), primary.width() - 44, primary.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("打开图片"));

    p.setPen(QPen(QColor(QString::fromLatin1(UiStyle::pick("#4a4c50", "#b5b5b5"))), 1));
    if (m_hover == 1) {
        p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#383a3e", "#c7c9cb"))));
    } else {
        p.setBrush(Qt::NoBrush);
    }
    p.drawRoundedRect(QRectF(secondary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(12));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#d9dadd", "#242628"))));
    p.drawText(secondary, Qt::AlignCenter, QStringLiteral("选择文件夹"));

    // 快捷键提示
    struct KbdGroup {
        QStringList keys;
        QString label;
    };
    const QVector<KbdGroup> groups = {
        { { QStringLiteral("F2") }, QStringLiteral("打开图片") },
        { { QStringLiteral("F11") }, QStringLiteral("全屏查看") },
        { { QStringLiteral("F5") }, QStringLiteral("设置") },
    };
    const QFont keyFont = uiFont(11);
    const QFont labelFont = uiFont(12);
    auto groupWidth = [&](const KbdGroup &g) {
        QFontMetrics kfm(keyFont);
        QFontMetrics lfm(labelFont);
        int w = 0;
        for (const QString &key : g.keys) {
            w += kfm.horizontalAdvance(key) + 12 + 5;
        }
        return w + lfm.horizontalAdvance(g.label);
    };
    int total = 0;
    for (const KbdGroup &g : groups) {
        total += groupWidth(g);
    }
    total += 18 * (groups.size() - 1);
    int x = (width() - total) / 2;
    const int sy = top + 259;
    for (const KbdGroup &g : groups) {
        for (const QString &key : g.keys) {
            QFontMetrics kfm(keyFont);
            const int kw = kfm.horizontalAdvance(key) + 12;
            const QRect kr(x, sy, kw, 20);
            p.setPen(QPen(QColor(QString::fromLatin1(UiStyle::pick("#444444", "#bbbbbb"))), 1));
            p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#292929", "#e2e2e2"))));
            p.drawRoundedRect(QRectF(kr).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.setFont(keyFont);
            p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#aaaaaa", "#555555"))));
            p.drawText(kr, Qt::AlignCenter, key);
            x += kw + 5;
        }
        p.setFont(labelFont);
        p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#737373", "#8c8c8c"))));
        QFontMetrics lfm(labelFont);
        const int lw = lfm.horizontalAdvance(g.label);
        p.drawText(QRect(x, sy, lw, 20), Qt::AlignVCenter | Qt::AlignLeft, g.label);
        x += lw + 18;
    }
}

void EmptyState::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (primaryRect().contains(e->pos())) m_press = 0;
        else if (secondaryRect().contains(e->pos())) m_press = 1;
        update();
    }
    QWidget::mousePressEvent(e);
}

void EmptyState::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    update();
    if (e->button() == Qt::LeftButton) {
        if (i == 0 && primaryRect().contains(e->pos())) emit openClicked();
        if (i == 1 && secondaryRect().contains(e->pos())) emit folderClicked();
    }
    QWidget::mouseReleaseEvent(e);
}

void EmptyState::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = primaryRect().contains(e->pos()) ? 0
              : (secondaryRect().contains(e->pos()) ? 1 : -1);
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void EmptyState::leaveEvent(QEvent *)
{
    m_hover = -1;
    setPointerCursor(this, false);
    update();
}

// ===========================================================================
// Toast
// ===========================================================================

Toast::Toast(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::showMessage(const QString &text)
{
    m_text = text;
    reposition();
    show();
    raise();
    m_timer->start(2000);
    update();
}

void Toast::setCorner(bool corner)
{
    m_corner = corner;
    reposition();
}

void Toast::reposition()
{
    QWidget *pw = parentWidget();
    if (!pw) {
        return;
    }
    // 右下角时抬到控制胶囊上沿之上（胶囊高 72 + 下缘 19），两种位置都不遮胶囊；
    // 顶部居中沿用旧几何（顶栏 28+34 之下 76）。
    const int y = m_corner ? (pw->height() - 91 - 56) : 76;
    setGeometry(1, y, pw->width() - 2, 56);
}

void Toast::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(uiFont(12));
    const QFontMetrics fm = p.fontMetrics();
    const int w = qMin(parentWidget() ? parentWidget()->width() - 40 : 360,
                       fm.horizontalAdvance(m_text) + 32);
    // 右下角贴右缘 20px；顶部居中保持原位
    const qreal x = m_corner ? (width() - w - 20.0) : ((width() - w) / 2.0);
    const QRectF r(x, 0, w, 30);
    p.setPen(QPen(QColor(QString::fromLatin1(UiStyle::pick("#555555", "#aaaaaa"))), 1));
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#383838", "#c7c7c7"))));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#eeeeee", "#404040"))));
    p.drawText(r, Qt::AlignCenter, m_text);
}

// ===========================================================================
// ZoomBadge
// ===========================================================================

ZoomBadge::ZoomBadge(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_fade->start();
    });
    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(1.0);
    setGraphicsEffect(m_opacity);
    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fade->setDuration(320);
    m_fade->setStartValue(1.0);
    m_fade->setEndValue(0.0);
    connect(m_fade, &QPropertyAnimation::finished, this, &QWidget::hide);
}

void ZoomBadge::showText(const QString &text)
{
    m_text = text;
    m_fade->stop();
    m_opacity->setOpacity(1.0);
    const QFontMetrics fm(uiFont(11));
    setFixedSize(fm.horizontalAdvance(text) + 24, 24);
    show();
    raise();
    m_timer->start(1500);
    update();
}

void ZoomBadge::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen((UiStyle::isLight() ? QColor(0,0,0,26) : QColor(255,255,255,26)), 1));
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#282a2d", "#dfe1e3"))));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(11));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#d7d9dc", "#26282a"))));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace Chrome
