#include "Chrome.h"
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
    Folder, Grid, Play, Bookmark, Edit, Copy, Lock, More, ChevronDown,
    Minus, Plus, Left, Right, Fit, RotateLeft, RotateRight, Picture,
    MinWindow, MaxWindow, Close
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
const QColor kHoverBg(0x45, 0x48, 0x4d);   // Hover  #45484d
const QColor kPressBg(0x29, 0x2b, 0x2f);   // Pressed #292b2f

constexpr qreal kPressScale = 0.94;        // 按下时绘制缩放
constexpr int kHoverMs = 120;              // Hover 淡入
constexpr int kPressMs = 90;               // 按下 / 释放
constexpr int kTickMs = 16;

// ---- 底部胶囊几何（交接单 §7，与原型 .control-dock 一致）----
constexpr int kDockW = 408;
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
constexpr int kZoomBtnW = 30;
constexpr int kZoomBtnH = 26;
constexpr int kPercentW = 44;
constexpr int kGridGap = 7;       // 缩放行四列列间距
constexpr int kSepW = 1;
constexpr int kSepH = 16;
constexpr int kSepMargin = 3;     // 分隔线左右外边距
constexpr qreal kIconSize = 17.0; // 普通图标
constexpr qreal kNavIconSize = 19.0;
constexpr qreal kNavIconStroke = 1.8;
constexpr qreal kIconStroke = 1.55;
constexpr qreal kRadius = 5.0;

struct ButtonPalette {
    QColor base = QColor(0, 0, 0, 0);          // 常态底（alpha=0 表示无）
    QColor baseBorder = QColor(0, 0, 0, 0);    // 常态描边
    QColor hover = kHoverBg;                   // Hover 底
    QColor press = kPressBg;                   // Pressed 底
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
// 按压缩放只作用于 painter，调用方传入的 rect（= 命中区域）不被修改。
QRectF beginButton(QPainter &p, const QRectF &rect, const BtnState &s,
                   const BtnVisual &v, const ButtonPalette &pal)
{
    p.save();

    const qreal scale = 1.0 - (1.0 - kPressScale) * v.press;
    if (scale < 0.9995) {
        p.translate(rect.center());
        p.scale(scale, scale);
        p.translate(-rect.center());
    }
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
        v.hover = stepToward(v.hover, hoverTarget, dt, kHoverMs);
        v.press = stepToward(v.press, pressTarget, dt, kPressMs);

        const bool settled = qFuzzyCompare(v.hover + 1.0, hoverTarget + 1.0)
                          && qFuzzyCompare(v.press + 1.0, pressTarget + 1.0);
        if (settled && hoverTarget == 0.0 && pressTarget == 0.0) {
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
// 标题栏
// ===========================================================================

CaptionBar::CaptionBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(28);
    setAttribute(Qt::WA_OpaquePaintEvent);
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
}

void CaptionBar::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QRect CaptionBar::buttonRect(int index) const
{
    const int bw = 44;
    return QRect(width() - (3 - index) * bw, 0, bw, height());
}

int CaptionBar::hitTest(const QPoint &pos) const
{
    for (int i = 0; i < 3; ++i) {
        if (buttonRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CaptionBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QStringLiteral("#1d1e20")));
    p.fillRect(QRect(0, height() - 1, width(), 1), rgba(255, 255, 255, 10));

    // 应用标识
    const QRectF logoRect(11, (height() - 16) / 2.0, 16, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#0b84c6")));
    p.drawRoundedRect(logoRect, 4, 4);
    p.setFont(uiFont(10, QFont::DemiBold));
    p.setPen(QColor(QStringLiteral("#ffffff")));
    p.drawText(logoRect, Qt::AlignCenter, QStringLiteral("Q"));

    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#d7d7d7")));
    p.drawText(QRect(34, 0, 78, height()), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("QuikView"));

    const int dividerX = 103;
    p.fillRect(QRect(dividerX, 9, 1, height() - 17), rgba(255, 255, 255, 19));

    // 文件名
    const int controlsLeft = width() - 3 * 44;
    const int titleLeft = dividerX + 12;
    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#96999d")));
    const QRect titleRect(titleLeft, 0, qMax(0, controlsLeft - titleLeft - 12), height());
    p.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, titleRect.width()));

    // 窗口按钮
    for (int i = 0; i < 3; ++i) {
        const QRect r = buttonRect(i);
        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.danger = (i == 2);
        BtnVisual v = m_feedback->visual(i);

        ButtonPalette pal;
        pal.radius = 0;   // Windows 窗口按钮保持直角
        pal.ink = QColor(QStringLiteral("#c9c9c9"));
        if (st.danger) {
            pal.hover = UiStyle::color(UiStyle::Danger);
            pal.press = UiStyle::color(UiStyle::Danger).darker(122);
            pal.ink = (st.hover || st.pressed) ? QColor(Qt::white)
                                               : QColor(QStringLiteral("#c9c9c9"));
        } else {
            pal.hover = QColor(QStringLiteral("#343539"));
            pal.press = QColor(QStringLiteral("#2a2b2e"));
        }

        beginButton(p, QRectF(r), st, v, pal);
        const Icon icon = i == 0 ? Icon::MinWindow
                                 : (i == 1 ? Icon::MaxWindow : Icon::Close);
        drawIcon(p, icon, QRectF(r.center().x() - 6, r.center().y() - 6, 12, 12),
                 pal.ink, 1.4);
        endButton(p);
    }
}

void CaptionBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_press = hitTest(e->pos());
        if (m_press >= 0) {
            m_feedback->setPressed(m_press);   // 按下即出反馈
            update();
            return;
        }
        if (window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
        }
        return;
    }
    QWidget::mousePressEvent(e);
}

void CaptionBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    m_feedback->setPressed(-1);
    update();
    if (e->button() == Qt::LeftButton && i >= 0 && i == hitTest(e->pos())) {
        switch (i) {
        case 0: emit minimizeClicked(); break;
        case 1: emit maximizeClicked(); break;
        case 2: emit closeClicked(); break;
        default: break;
        }
    }
    QWidget::mouseReleaseEvent(e);
}

void CaptionBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    m_feedback->setHover(m_hover);
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void CaptionBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && hitTest(e->pos()) < 0) {
        emit maximizeClicked();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CaptionBar::contextMenuEvent(QContextMenuEvent *e)
{
    emit systemMenuRequested(e->globalPos());
    e->accept();
}

void CaptionBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
    update();
}

// ===========================================================================
// 命令栏
// ===========================================================================

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(34);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus);   // 键盘可达：Tab 进入后显示 2px 焦点环
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
}

void ToolBar::setShowModeIcon(int index)
{
    m_showModeIcon = qBound(0, index, 4);
}

void ToolBar::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    update();
}

QVector<ToolBar::Btn> ToolBar::buttons() const
{
    QVector<Btn> out;
    const int y = 4;
    const int h = 26;
    out.append({ 0, QStringLiteral("打开"), QRect(8, y, 68, h), true, false });
    out.append({ 1, QStringLiteral("浏览"), QRect(79, y, 72, h), false, true });
    out.append({ 2, QStringLiteral("幻灯片放映"), QRect(154, y, 96, h), false, false });

    int x = 262;
    out.append({ 3, QString(), QRect(x, y, 30, h), false, false });
    x += 33;
    out.append({ 4, QString(), QRect(x, y, 30, h), false, false });
    x += 33;
    out.append({ 5, QString(), QRect(x, y, 30, h), false, false });

    out.append({ 6, QString(), QRect(width() - 68, y, 30, h), false, false });
    out.append({ 7, QString(), QRect(width() - 38, y, 30, h), false, false });
    return out;
}

int ToolBar::hitTest(const QPoint &pos) const
{
    const QVector<Btn> btns = buttons();
    for (int i = 0; i < btns.size(); ++i) {
        if (btns[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ToolBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QStringLiteral("#232426")));
    p.fillRect(QRect(0, height() - 1, width(), 1), rgba(0, 0, 0, 82));
    p.fillRect(QRect(0, height() - 2, width(), 1), rgba(255, 255, 255, 5));

    const QVector<Btn> btns = buttons();
    for (int i = 0; i < btns.size(); ++i) {
        const Btn &b = btns[i];
        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.checked = (b.id == 6 && m_locked);
        st.focus = (m_keyboardFocus && m_focus == i);
        BtnVisual v = m_feedback->visual(i);

        ButtonPalette pal;
        if (b.primary) {
            pal.base = QColor(QStringLiteral("#0b6f9f"));
            pal.baseBorder = QColor(QStringLiteral("#2485b3"));
            pal.hover = QColor(QStringLiteral("#0e7fb5"));
            pal.press = QColor(QStringLiteral("#0a5c84"));
            pal.ink = QColor(Qt::white);
        } else {
            pal.hover = kHoverBg;
            pal.press = kPressBg;
            pal.baseBorder = rgba(255, 255, 255, 19);
            pal.checkedBg = UiStyle::accentSoft();
            pal.checkedBorder = rgba(85, 192, 240, 64);
            pal.ink = QColor(QStringLiteral("#d9dadd"));
            pal.checkedInk = UiStyle::accent();
        }

        beginButton(p, QRectF(b.rect), st, v, pal);
        const QColor ink = buttonInk(st, pal);
        if (b.primary) {
            drawIcon(p, Icon::Folder, QRectF(b.rect.x() + 10, b.rect.center().y() - 8, 16, 16),
                     ink);
            p.setFont(uiFont(12, QFont::DemiBold));
            p.setPen(ink);
            p.drawText(QRect(b.rect.x() + 31, b.rect.y(), b.rect.width() - 35, b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, b.text);
            endButton(p);
            continue;
        }

        const QRectF iconRect(b.rect.x() + 9, b.rect.center().y() - 8, 16, 16);
        const QRectF centerIcon(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16);
        switch (b.id) {
        case 1: drawIcon(p, Icon::Grid, iconRect, ink); break;
        case 2: drawIcon(p, Icon::Play, iconRect, ink); break;
        case 3: drawIcon(p, Icon::Bookmark, centerIcon, ink); break;
        case 4: drawIcon(p, Icon::Edit, centerIcon, ink); break;
        case 5: drawIcon(p, Icon::Copy, centerIcon, ink); break;
        case 6: drawIcon(p, Icon::Lock, centerIcon, ink); break;
        case 7: drawIcon(p, Icon::More, centerIcon, ink); break;
        default: break;
        }

        if (b.id == 1 || b.id == 2) {
            p.setFont(uiFont(12));
            p.setPen(ink);
            p.drawText(QRect(b.rect.x() + 30, b.rect.y(),
                             b.rect.width() - (b.id == 1 ? 47 : 34), b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, b.text);
            if (b.caret) {
                drawIcon(p, Icon::ChevronDown,
                         QRectF(b.rect.right() - 18, b.rect.center().y() - 5, 10, 10),
                         QColor(QStringLiteral("#8e9297")), 1.4);
            }
        }
        endButton(p);
    }

    // 工具组前的分隔线
    const QRect slideRect = btns.size() > 2 ? btns[2].rect : QRect();
    const int sepX = slideRect.right() + 8;
    p.fillRect(QRect(sepX, 8, 1, height() - 17), rgba(255, 255, 255, 19));
}

void ToolBar::activate(int index, const QPoint &globalPos)
{
    switch (index) {
    case 0: emit openClicked(); break;
    case 1: emit browseMenuRequested(globalPos); break;
    case 2: emit slideshowMenuRequested(globalPos); break;
    case 3: emit bookmarkMenuRequested(globalPos); break;
    case 4: emit editMenuRequested(globalPos); break;
    case 5: emit copytoMenuRequested(globalPos); break;
    case 6: emit lockToggled(); break;
    case 7: emit moreMenuRequested(globalPos); break;
    default: break;
    }
}

void ToolBar::moveFocus(int step)
{
    const int n = buttons().size();
    if (n <= 0) {
        return;
    }
    int next = (m_focus < 0) ? (step > 0 ? 0 : n - 1) : m_focus + step;
    if (next < 0 || next >= n) {
        // 走到边界：把焦点交还给外层焦点链
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

void ToolBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_keyboardFocus) {
            m_keyboardFocus = false;   // 鼠标接管后不再显示焦点环
            m_focus = -1;
        }
        m_press = hitTest(e->pos());
        m_feedback->setPressed(m_press);   // 按下即出反馈，不等 clicked
        update();
    }
    QWidget::mousePressEvent(e);
}

void ToolBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    m_feedback->setPressed(-1);
    update();
    if (e->button() != Qt::LeftButton || i < 0 || i != hitTest(e->pos())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    activate(i, e->globalPosition().toPoint());
    QWidget::mouseReleaseEvent(e);
}

void ToolBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    m_feedback->setHover(m_hover);
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void ToolBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
    update();
}

void ToolBar::focusInEvent(QFocusEvent *e)
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

void ToolBar::focusOutEvent(QFocusEvent *e)
{
    m_focus = -1;
    m_keyboardFocus = false;
    m_press = -1;
    m_feedback->reset();
    update();
    QWidget::focusOutEvent(e);
}

void ToolBar::keyPressEvent(QKeyEvent *e)
{
    if (!m_keyboardFocus) {
        // 鼠标点进来的焦点不抢全局快捷键（方向键翻页 / 空格下一张）
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        moveFocus((e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier)) ? -1 : 1);
        e->accept();
        return;
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

void ToolBar::keyReleaseEvent(QKeyEvent *e)
{
    if (m_keyboardFocus
        && (e->key() == Qt::Key_Space || e->key() == Qt::Key_Return
            || e->key() == Qt::Key_Enter)
        && !e->isAutoRepeat()) {
        const int i = m_press;
        m_press = -1;
        m_feedback->setPressed(-1);
        update();
        if (i >= 0 && i == m_focus) {
            const QVector<Btn> btns = buttons();
            if (i < btns.size()) {
                activate(i, mapToGlobal(btns[i].rect.center()));
            }
        }
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

// ===========================================================================
// 底部胶囊控制栏
// ===========================================================================

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(kDockW, kDockH);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
}

void ControlBar::setZoomPercent(int percent)
{
    m_percent = qBound(1, percent, 3200);
    update();
}

void ControlBar::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    update();
}

QVector<ControlBar::Item> ControlBar::items() const
{
    return layoutItems(nullptr);
}

// 胶囊的唯一布局来源（交接单 §7）：
//   内边距 上 6 / 左右 9 / 下 7，两排间距 3；缩放行 26，操作行 30。
QVector<ControlBar::Item> ControlBar::layoutItems(QVector<int> *separators) const
{
    QVector<Item> out;

    // ---- 缩放行：减号 / 滑杆 / 加号 / 百分比 四列网格，列间距 7，垂直居中 ----
    const int zoomY = kDockPadTop;
    const int sliderW = kDockW - 2 * kDockPadX - 2 * kZoomBtnW - kPercentW - 3 * kGridGap;
    int x = kDockPadX;
    out.append({ Action::ZoomOut, QRect(x, zoomY, kZoomBtnW, kZoomBtnH) });
    x += kZoomBtnW + kGridGap;
    out.append({ Action::Slider, QRect(x, zoomY, sliderW, kZoomRowH) });
    x += sliderW + kGridGap;
    out.append({ Action::ZoomIn, QRect(x, zoomY, kZoomBtnW, kZoomBtnH) });
    x += kZoomBtnW + kGridGap;
    out.append({ Action::ZoomValue, QRect(x, zoomY, kPercentW, kZoomRowH) });

    // ---- 操作行：8 × 32×30 按钮 + 4 条分隔线，整行在两内边距之间水平居中 ----
    struct Cell {
        Action action;
        bool sepBefore;
    };
    const Cell cells[] = {
        { Action::Open, false },        { Action::List, false },
        { Action::Fit, true },          { Action::RotateLeft, true },
        { Action::RotateRight, false },
        { Action::Prev, true },         { Action::Next, false },
        { Action::Lock, true },
    };
    const int count = static_cast<int>(sizeof(cells) / sizeof(cells[0]));

    int contentW = kItemGap * (count - 1);
    for (const Cell &c : cells) {
        contentW += kBtnW;
        if (c.sepBefore) {
            // 分隔线：自身 1px + 左右各 3px 外边距 + 它自己占一个按钮间距
            contentW += kSepW + 2 * kSepMargin + kItemGap;
        }
    }

    const int actionY = kDockPadTop + kZoomRowH + kRowGap;
    const int contentArea = kDockW - 2 * kDockPadX;
    int cur = kDockPadX + qRound((contentArea - contentW) / 2.0);
    for (const Cell &c : cells) {
        if (c.sepBefore) {
            const int sepX = cur + kSepMargin;   // cur 已含上一个按钮后的间距
            if (separators) {
                separators->append(sepX);
            }
            cur = sepX + kSepW + kSepMargin + kItemGap;
        }
        out.append({ c.action, QRect(cur, actionY, kBtnW, kBtnH) });
        cur += kBtnW + kItemGap;
    }
    return out;
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

int ControlBar::currentSliderPercent() const
{
    return qBound(10, m_percent, 200);
}

void ControlBar::applySliderValueFromX(int x)
{
    QRect track;
    const QVector<Item> list = items();
    for (const Item &item : list) {
        if (item.action == Action::Slider) {
            track = item.rect;
            break;
        }
    }
    if (track.isNull()) {
        return;
    }
    const qreal left = track.left() + 5.5;    // 手柄半径内缩，与绘制一致
    const qreal right = track.right() - 5.5;
    const qreal ratio = qBound(0.0, (x - left) / qMax(1.0, right - left), 1.0);
    const int pct = qRound(10.0 + ratio * 190.0);
    if (pct == m_percent) {
        return;
    }
    m_percent = pct;
    emit zoomRequested(pct);
    update();
}

void ControlBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 胶囊底：整条只有这一层，缩放行不再叠第二层背景
    p.setPen(QPen(rgba(255, 255, 255, 33), 1));
    p.setBrush(rgba(40, 42, 45, 242));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    QVector<int> separators;
    const QVector<Item> list = layoutItems(&separators);

    // 分隔线：1px 宽 / 16px 高，操作行垂直居中
    const int sepY = kDockPadTop + kZoomRowH + kRowGap + (kActionRowH - kSepH) / 2;
    for (int sx : separators) {
        p.fillRect(QRect(sx, sepY, kSepW, kSepH), rgba(255, 255, 255, 23));
    }

    for (int i = 0; i < list.size(); ++i) {
        const Item &item = list.at(i);
        BtnVisual v = m_feedback->visual(i);

        if (item.action == Action::Slider) {
            const QRectF track = item.rect;
            const qreal y = track.center().y() + 0.5;
            const qreal left = track.left() + 5.5;     // 手柄半径，避免两端被裁
            const qreal right = track.right() - 5.5;
            // 拖动中轨道略微提亮，和单纯 Hover 区分
            QColor idle(QStringLiteral("#5d6166"));
            if (v.press > 0.004) {
                idle = blendColor(idle, QColor(0x7a, 0x7e, 0x84), 0.55 * v.press);
            }
            p.setPen(QPen(idle, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(left, y), QPointF(right, y));
            const qreal ratio = (currentSliderPercent() - 10.0) / 190.0;
            const qreal thumbX = left + ratio * (right - left);
            p.setPen(QPen(UiStyle::accent(), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(left, y), QPointF(thumbX, y));
            // 手柄：Hover 1.12 / Dragging 1.22，取较大者不叠加
            const qreal grow = 1.0 + qMax(0.12 * v.hover, 0.22 * v.press);
            const qreal hot = qMax(v.hover, v.press);
            QColor thumb(QStringLiteral("#f3f5f6"));
            if (hot > 0.01) {
                thumb = blendColor(thumb, UiStyle::accent(), hot);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(thumb);
            p.drawEllipse(QPointF(thumbX, y), 5.5 * grow, 5.5 * grow);
            continue;
        }

        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.checked = (item.action == Action::Lock && m_locked);
        st.focus = (m_keyboardFocus && m_focus == i);

        ButtonPalette pal;
        pal.hover = kHoverBg;
        pal.press = kPressBg;
        pal.baseBorder = rgba(255, 255, 255, 19);
        pal.checkedBg = UiStyle::accentSoft();
        pal.checkedBorder = rgba(85, 192, 240, 64);
        pal.ink = QColor(QStringLiteral("#d7d9dc"));
        pal.checkedInk = UiStyle::accent();
        switch (item.action) {
        case Action::Prev:
        case Action::Next:
            pal.ink = QColor(QStringLiteral("#f0f0f0"));
            break;
        case Action::ZoomOut:
        case Action::ZoomIn:
        case Action::ZoomValue:
            pal.ink = QColor(QStringLiteral("#b7bbc0"));
            break;
        default:
            break;
        }

        const QRect r = item.rect;
        beginButton(p, QRectF(r), st, v, pal);
        const QColor ink = buttonInk(st, pal);
        const QRectF centerIcon(r.center().x() - kIconSize / 2.0,
                                r.center().y() - kIconSize / 2.0, kIconSize, kIconSize);
        const QRectF navIcon(r.center().x() - kNavIconSize / 2.0,
                             r.center().y() - kNavIconSize / 2.0, kNavIconSize, kNavIconSize);

        switch (item.action) {
        case Action::Open:
            drawIcon(p, Icon::Folder, centerIcon, ink, kIconStroke);
            break;
        case Action::List:
            drawIcon(p, Icon::Grid, centerIcon, ink, kIconStroke);
            break;
        case Action::Fit:
            drawIcon(p, Icon::Fit, centerIcon, ink, kIconStroke);
            break;
        case Action::RotateLeft:
            drawIcon(p, Icon::RotateLeft, centerIcon, ink, kIconStroke);
            break;
        case Action::RotateRight:
            drawIcon(p, Icon::RotateRight, centerIcon, ink, kIconStroke);
            break;
        case Action::Prev:
            drawIcon(p, Icon::Left, navIcon, ink, kNavIconStroke);
            break;
        case Action::Next:
            drawIcon(p, Icon::Right, navIcon, ink, kNavIconStroke);
            break;
        case Action::Lock:
            drawIcon(p, Icon::Lock, centerIcon, ink, kIconStroke);
            break;
        case Action::ZoomOut:
            drawIcon(p, Icon::Minus, centerIcon, ink, kIconStroke);
            break;
        case Action::ZoomIn:
            drawIcon(p, Icon::Plus, centerIcon, ink, kIconStroke);
            break;
        case Action::ZoomValue:
            drawTabularText(p, QRectF(r), QStringLiteral("%1%").arg(m_percent), ink, 11);
            break;
        default:
            break;
        }
        endButton(p);
    }
}

void ControlBar::activate(Action action)
{
    switch (action) {
    case Action::Open: emit openClicked(); break;
    case Action::List: emit listClicked(); break;
    case Action::Fit: emit fitClicked(); break;
    case Action::RotateLeft: emit rotateLeftClicked(); break;
    case Action::RotateRight: emit rotateRightClicked(); break;
    case Action::Prev: emit prevClicked(); break;
    case Action::Next: emit nextClicked(); break;
    case Action::Lock: emit lockToggled(); break;
    case Action::ZoomOut: emit zoomOutClicked(); break;
    case Action::ZoomIn: emit zoomInClicked(); break;
    case Action::ZoomValue: emit fitClicked(); break;
    default: break;
    }
}

void ControlBar::moveFocus(int step)
{
    const int n = items().size();
    if (n <= 0) {
        return;
    }
    const int next = (m_focus < 0) ? (step > 0 ? 0 : n - 1) : m_focus + step;
    if (next < 0 || next >= n) {
        // 走到边界：把焦点交还给外层焦点链
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
    const QVector<Item> list = items();
    const int idx = hitIndex(e->pos());
    const Action action = (idx >= 0 && idx < list.size()) ? list[idx].action : Action::None;

    if (e->button() == Qt::RightButton) {
        if (action == Action::Prev) { emit prev10Clicked(); return; }
        if (action == Action::Next) { emit next10Clicked(); return; }
    }
    if (e->button() == Qt::MiddleButton) {
        if (action == Action::Prev) { emit prevFolderClicked(); return; }
        if (action == Action::Next) { emit nextFolderClicked(); return; }
    }
    if (e->button() == Qt::LeftButton) {
        if (m_keyboardFocus) {
            m_keyboardFocus = false;   // 鼠标接管后不再显示焦点环
            m_focus = -1;
        }
        m_press = idx;
        m_feedback->setPressed(idx);   // 按下即出反馈，不等 clicked
        if (action == Action::Slider) {
            m_dragging = true;
            applySliderValueFromX(e->pos().x());
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
        activate(list[pressedIndex].action);
    }
    QWidget::mouseReleaseEvent(e);
}

void ControlBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        applySliderValueFromX(e->pos().x());
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
    if (m_hover >= 0 && list[m_hover].action == Action::Slider) {
        setCursor(Qt::SizeHorCursor);
    } else {
        setPointerCursor(this, m_hover >= 0);
    }
    update();
    QWidget::mouseMoveEvent(e);
}

void ControlBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
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
    update();
    QWidget::focusOutEvent(e);
}

void ControlBar::keyPressEvent(QKeyEvent *e)
{
    if (!m_keyboardFocus) {
        // 鼠标点进来的焦点不抢全局快捷键（方向键翻页 / 空格下一张）
        QWidget::keyPressEvent(e);
        return;
    }
    switch (e->key()) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        moveFocus((e->key() == Qt::Key_Backtab || (e->modifiers() & Qt::ShiftModifier)) ? -1 : 1);
        e->accept();
        return;
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
        if (i >= 0 && i == m_focus) {
            const QVector<Item> list = items();
            if (i < list.size() && list[i].action != Action::Slider) {
                activate(list[i].action);
            }
        }
        e->accept();
        return;
    }
    QWidget::keyReleaseEvent(e);
}

void ControlBar::wheelEvent(QWheelEvent *e)
{
    const int step = e->angleDelta().y() > 0 ? 10 : -10;
    emit zoomRequested(qBound(10, m_percent + step, 200));
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

void StatusOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(uiFont(11));
    const int baselineTop = height() - 25;
    p.setPen(QColor(QStringLiteral("#797d82")));
    p.drawText(QRect(16, baselineTop, width() / 2, 16),
               Qt::AlignVCenter | Qt::AlignLeft, m_info);
    p.setPen(QColor(QStringLiteral("#85898e")));
    p.drawText(QRect(width() / 2, baselineTop, width() / 2 - 17, 16),
               Qt::AlignVCenter | Qt::AlignRight, m_counter);
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
    p.setPen(QPen(rgba(255, 255, 255, 31), 1));
    p.setBrush(QColor(QStringLiteral("#232323")));
    p.drawRoundedRect(iconRect, 18, 18);
    drawIcon(p, Icon::Picture,
             QRectF(iconRect.center().x() - 17, iconRect.center().y() - 17, 34, 34),
             QColor(QStringLiteral("#bdbdbd")), 1.35);

    p.setFont(uiFont(22, QFont::Medium));
    p.setPen(QColor(QStringLiteral("#f3f3f3")));
    p.drawText(QRect(0, top + 94, width(), 30), Qt::AlignCenter,
               QStringLiteral("开始查看图片"));

    p.setFont(uiFont(13));
    p.setPen(QColor(QStringLiteral("#999999")));
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

    p.setPen(QPen(QColor(QStringLiteral("#4a4c50")), 1));
    if (m_hover == 1) {
        p.setBrush(QColor(QStringLiteral("#383a3e")));
    } else {
        p.setBrush(Qt::NoBrush);
    }
    p.drawRoundedRect(QRectF(secondary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#d9dadd")));
    p.drawText(secondary, Qt::AlignCenter, QStringLiteral("选择文件夹"));

    // 快捷键提示
    struct KbdGroup {
        QStringList keys;
        QString label;
    };
    const QVector<KbdGroup> groups = {
        { { QStringLiteral("Ctrl"), QStringLiteral("O") }, QStringLiteral("打开图片") },
        { { QStringLiteral("F11") }, QStringLiteral("全屏查看") },
        { { QStringLiteral("F9") }, QStringLiteral("设置") },
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
            p.setPen(QPen(QColor(QStringLiteral("#444444")), 1));
            p.setBrush(QColor(QStringLiteral("#292929")));
            p.drawRoundedRect(QRectF(kr).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.setFont(keyFont);
            p.setPen(QColor(QStringLiteral("#aaaaaa")));
            p.drawText(kr, Qt::AlignCenter, key);
            x += kw + 5;
        }
        p.setFont(labelFont);
        p.setPen(QColor(QStringLiteral("#737373")));
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
    if (parentWidget()) {
        setGeometry(0, y(), parentWidget()->width(), 56);
    }
    show();
    raise();
    m_timer->start(2000);
    update();
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
    const QRectF r((width() - w) / 2.0, 0, w, 30);
    p.setPen(QPen(QColor(QStringLiteral("#555555")), 1));
    p.setBrush(QColor(QStringLiteral("#383838")));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.setPen(QColor(QStringLiteral("#eeeeee")));
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
    p.setPen(QPen(rgba(255, 255, 255, 26), 1));
    p.setBrush(QColor(QStringLiteral("#282a2d")));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(11));
    p.setPen(QColor(QStringLiteral("#d7d9dc")));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace Chrome
