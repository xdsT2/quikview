#include "Chrome.h"
#include "Config.h"
#include "UiStyle.h"
#include "AppIcon.h"
#include "HudShell.h"

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
#include <QTextStream>
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

enum class CaptionGlyph { Minimize, Maximize, Restore, Close };

QFont uiFont(int pixelSize, int weight = QFont::Normal)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(pixelSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

// 顶栏/底岛功能图标统一走 AppIcon（图标派单 2026-11-18）。
// 这里不再保留 Chrome 自己的图形表：只把语义 id 递过去，缺资源时静默跳过，
// 不退回手绘，避免出现「手绘版 + SVG 版」两套真源（派单 §4 末段）。
bool drawIcon(QPainter &p, const char *id, const QRectF &r, const QColor &color,
              AppIcon::Style style = AppIcon::Style::Regular)
{
    if (r.isEmpty()) {
        return false;
    }
    return AppIcon::paint(p, QLatin1String(id), r, color, style);
}

// 仅两处保留自绘：菜单按钮三横线、窗口按钮（最小化/最大化/还原/关闭）。
// Fluent 精选包没有这两类家族，且窗口按钮属原生窗口外观；两份实现各自唯一，
// 缺口登记在 管理/SoleView-图标系统-20261118/missing-and-custom.csv。
void drawHamburger(QPainter &p, const QRectF &r, const QColor &color)
{
    if (r.isEmpty()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(r.x(), r.y());
    const qreal s = r.width() / 24.0;
    p.scale(s, s);
    QPen pen(color, 1.55 / qMax(0.01, s), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.drawLine(QPointF(4, 7), QPointF(20, 7));
    p.drawLine(QPointF(4, 12), QPointF(20, 12));
    p.drawLine(QPointF(4, 17), QPointF(20, 17));
    p.restore();
}

void drawCaptionGlyph(QPainter &p, CaptionGlyph glyph, const QRectF &r, const QColor &color)
{
    if (r.isEmpty()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(r.x(), r.y());
    const qreal s = qMin(r.width(), r.height()) / 24.0;
    p.scale(s, s);
    QPen pen(color, 1.4 / qMax(0.01, s), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (glyph) {
    case CaptionGlyph::Minimize:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case CaptionGlyph::Maximize:
        p.drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1);
        break;
    case CaptionGlyph::Restore:
        // 已最大化：两层方框（后层只留右上两笔），与最小化/关闭同一线宽。
        p.drawRoundedRect(QRectF(4, 8, 12, 12), 1, 1);
        p.drawLine(QPointF(8, 8), QPointF(8, 4));
        p.drawLine(QPointF(8, 4), QPointF(20, 4));
        p.drawLine(QPointF(20, 4), QPointF(20, 16));
        p.drawLine(QPointF(20, 16), QPointF(16, 16));
        break;
    case CaptionGlyph::Close:
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

// 状态底色统一取自全局反馈套系 UiStyle::feedback（hover/press/plate），不再本地维护兜底色对。

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
    QColor hover = UiStyle::feedback::hover();   // Hover 底（全局反馈套系）
    QColor press = UiStyle::feedback::press();  // Pressed 底（全局反馈套系）
    QColor checkedBg = QColor(0, 0, 0, 0);      // 锁定 / 选中底（调用点覆盖为 feedback::plate()）
    QColor checkedBorder = QColor(0, 0, 0, 0);  // 锁定 / 选中描边（透明，不留蓝色兜底）
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
    // 2026-11-22 全局交互统一：悬停/按下/开启一律不再画外圈描边，
    // 只靠中性底板深浅分层；开启态由 Filled 图标表达（图一）。
    // 仅保留两处：常态自带描边（pal.baseBorder，窗口三键等）与键盘焦点环。
    if (!checked && pal.base.alpha() > 0) {
        border = pal.baseBorder;
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
        QByteArray env = qgetenv("SOLEVIEW_REDUCE_MOTION").trimmed().toLower();
        if (env.isEmpty()) {
            env = qgetenv("QUIKVIEW_REDUCE_MOTION").trimmed().toLower();
        }
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
    snap();
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
// 2026-11-19 定稿：功能按钮底板与点击区 30×30、圆角 8、图标 18×18。
constexpr int kTopBtn = 30;
constexpr int kTopWinBtn = 46;
constexpr int kTopMargin = 6;
constexpr int kTopLogoW = 22;

int topTextWidth(const QString &text)
{
    return QFontMetrics(uiFont(12)).horizontalAdvance(text);
}

// 图标+文字按钮的自然宽度；纯图标固定 30（定稿 30×30）。
// 文字态：左内边距 10 + 图标 18 + 图标文字间距 8 + 文字 + 右内边距 10。
int topButtonWidth(const QString &label, bool withLabel)
{
    if (!withLabel || label.isEmpty()) {
        return kTopBtn;
    }
    return 10 + 18 + 8 + topTextWidth(label) + 10;
}

// 只有「打开 / 浏览」两个按钮带文字；右组图标按钮与 Windows 三键不走这里。
QString topActionLabel(int id)
{
    switch (id) {
    case 0: return QObject::tr("打开");
    case 1: return QObject::tr("浏览");
    default: break;
    }
    return QString();
}

// 圆形反馈已不再用于顶栏/底栏按钮（定稿统一圆角矩形）；保留工具函数会触发
// 未使用告警，故移除。

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

void TopBar::setOverlayBackground(bool on)
{
    m_overlayBackground = on;
    setAttribute(Qt::WA_OpaquePaintEvent, !on);
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

void TopBar::setLayerButtonVisible(bool visible)
{
    if (m_showLayers == visible) {
        return;
    }
    m_showLayers = visible;
    m_hover = m_press = m_focus = -1;
    m_feedback->setHover(-1);
    m_feedback->setPressed(-1);
    m_tip->hideNow();
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

void TopBar::setOpenPickerActive(bool active)
{
    if (m_openPickerActive == active) {
        return;
    }
    m_openPickerActive = active;
    update();
}

QRect TopBar::openButtonRect() const
{
    const QVector<Btn> list = buttons();
    for (const Btn &b : list) {
        if (b.action == Action::Open) {
            return b.rect;
        }
    }
    return QRect();
}

int TopBar::idForAction(Action action)
{
    switch (action) {
    case Action::Open: return 0;
    case Action::Browse: return 1;
    case Action::Layers: return 2;
    case Action::TopLock: return 3;
    case Action::Pin: return 4;
    case Action::Menu: return 5;
    case Action::Minimize: return 6;
    case Action::Maximize: return 7;
    case Action::Close: return 8;
    default: break;
    }
    return -1;
}

TopBar::Action TopBar::actionForId(int id)
{
    switch (id) {
    case 0: return Action::Open;
    case 1: return Action::Browse;
    case 2: return Action::Layers;
    case 3: return Action::TopLock;
    case 4: return Action::Pin;
    case 5: return Action::Menu;
    case 6: return Action::Minimize;
    case 7: return Action::Maximize;
    case 8: return Action::Close;
    default: break;
    }
    return Action::None;
}

// 单行布局。分阶段收窄（与标注同序）：
//   0 全文字 + 文件名 140 ｜ 1 文件名归零 ｜ 2 浏览收成图标
//   3 图层 ｜ 4 顶栏锁 ｜ 5 置顶 ｜ 6 浏览
// 最后始终保留打开、三横菜单和 Windows 三按钮。
// 2026-11-22 顶栏入口精简：书签 / 幻灯片放映 / 编辑 / 复制到四个快捷入口已移除，
// 不再为它们留槽、也不迁入任何溢出列表；腾出的中段全部交给文件名。
QVector<TopBar::Btn> TopBar::buttons() const
{
    struct Plan {
        bool browseLabel = true;
        int filenameMin = 0;
        bool browse = true;
        bool layers = true;
        bool topLock = true;
        bool pin = true;
        int required = 0;
    };

    // 文字按钮之间 4px，纯图标按钮之间 2px（30 命中区几乎相邻）。
    auto gapFor = [](bool labeled) { return labeled ? 4 : 2; };

    auto makePlan = [&](int stage) {
        Plan p;
        p.browseLabel = stage <= 2;
        p.filenameMin = stage == 0 ? 140 : 0;
        p.browse = stage < 6;
        p.layers = m_showLayers && stage < 3;
        p.topLock = stage < 4;
        p.pin = stage < 5;

        // 打开始终带文字（极窄窗也保留），只有浏览会依次图标化/收入菜单。
        int left = kTopMargin + kTopLogoW + 8;
        left += topButtonWidth(topActionLabel(0), true) + gapFor(true);
        if (p.browse) {
            left += topButtonWidth(topActionLabel(1), p.browseLabel);
        }

        int right = 3 * kTopWinBtn + 8 + kTopBtn;   // 菜单常驻
        if (p.layers) right += kTopBtn;
        if (p.topLock) right += kTopBtn;
        if (p.pin) right += kTopBtn;

        // 文件名两侧各留 12/20px 呼吸位，再留 28px 起收点。
        p.required = left + p.filenameMin + right + 32;
        return p;
    };

    Plan plan;
    bool fitted = false;
    for (int stage = 0; stage <= 6; ++stage) {
        plan = makePlan(stage);
        if (plan.required <= width()) {
            fitted = true;
            break;
        }
    }
    if (!fitted) {
        plan = makePlan(6);
    }

    QVector<Btn> out;
    m_hiddenIds.clear();
    m_filenameRect = QRect();

    const int h = height() > 0 ? height() : kTopH;
    const int y = (h - kTopBtn) / 2;

    // —— 左组：Q 标识、打开、浏览 ——
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

    // —— 右组起点（图层 / 顶栏锁 / 置顶 / 三横菜单） ——
    const int rightEdge = width() - 3 * kTopWinBtn - 8;
    int rightStart = rightEdge;
    if (plan.layers) rightStart -= kTopBtn;
    if (plan.topLock) rightStart -= kTopBtn;
    if (plan.pin) rightStart -= kTopBtn;
    rightStart -= kTopBtn;   // menu

    // 文件名：优先省略，可收至零宽；低于 80px 就整段不画（避免只剩一两个字母）。
    // 中段原本留给四个快捷入口；入口移除后这块空间直接归文件名，不再设上限。
    const int fnLeft = leftEnd + 12;
    const int fnRight = qMax(fnLeft, rightStart - 20);
    const int fnW = fnRight - fnLeft;
    m_filenameRect = fnW >= 80 ? QRect(fnLeft, 0, fnW, h) : QRect();

    // —— 右组：图层 / 顶栏锁 / 置顶 / 三横菜单，再往右是 Windows 三按钮 ——
    int rx = rightEdge;
    rx -= kTopBtn;
    out.append(Btn{ Action::Menu, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    if (plan.pin) {
        rx -= kTopBtn;
        out.append(Btn{ Action::Pin, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    } else {
        m_hiddenIds.append(4);
    }
    if (plan.topLock) {
        rx -= kTopBtn;
        out.append(Btn{ Action::TopLock, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false, false });
    } else {
        m_hiddenIds.append(3);
    }
    if (plan.layers) {
        rx -= kTopBtn;
        out.append(Btn{ Action::Layers, QRect(rx, y, kTopBtn, kTopBtn), QString(), true, false,
                        !m_layersAvailable });
    } else if (m_showLayers) {
        m_hiddenIds.append(2);
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

// 验收探针：把「实际参与命中/绘制」的按钮与文件名块原样落文本，
// 四入口移除后据此逐项核对没有残槽、文件名确实接手中段空间。
QString TopBar::layoutDump() const
{
    const QVector<Btn> list = buttons();
    QString out;
    out += QStringLiteral("width=%1 height=%2 buttons=%3 filename=%4,%5,%6,%7\n")
               .arg(width())
               .arg(height())
               .arg(list.size())
               .arg(m_filenameRect.x())
               .arg(m_filenameRect.y())
               .arg(m_filenameRect.width())
               .arg(m_filenameRect.height());
    for (const Btn &b : list) {
        out += QStringLiteral("id=%1 tip=%2 rect=%3,%4,%5,%6 iconOnly=%7 caret=%8\n")
                   .arg(idForAction(b.action))
                   .arg(tipText(b.action))
                   .arg(b.rect.x())
                   .arg(b.rect.y())
                   .arg(b.rect.width())
                   .arg(b.rect.height())
                   .arg(b.iconOnly ? 1 : 0)
                   .arg(b.caret ? 1 : 0);
    }
    return out;
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
    return actionAnchorGlobal(5);
}

QPoint TopBar::closeAnchorGlobal() const
{
    return actionAnchorGlobal(8);
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
    // 背景与画布同源（2026-11-20 顶栏同色派单，按果核参考修订）：
    //   · 空态、纯色画布：顶栏铺画布纯色 solid()，与画布连续、无分隔线；
    //   · 棋盘画布：棋盘只铺画布区域，顶栏保持同主题纯色 solid()，
    //     交界由材质自然区分，同样不画横向分隔线。
    // 三种场景都用同一令牌，深浅主题各自取值。
    QColor surface = UiStyle::canvas::solid(UiStyle::isLight());
    if (m_overlayBackground)
        surface.setAlphaF(0.88);
    p.fillRect(rect(), surface);

    // 应用标识：22×22 蓝色圆角方块 + Q
    const QRectF logo(kTopMargin, (height() - kTopLogoW) / 2.0, kTopLogoW, kTopLogoW);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#1688f8")));
    p.drawRoundedRect(logo, 6, 6);
    p.setFont(uiFont(11, QFont::DemiBold));
    p.setPen(QColor(Qt::white));
    p.drawText(logo, Qt::AlignCenter, QStringLiteral("Q"));

    const QVector<Btn> list = buttons();

    // 文件名：优先省略；零宽时整段不画
    if (m_filenameRect.width() > 2) {
        p.setFont(uiFont(12));
        p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#9a9da2", "#5f6368"))));
        const QString elided = p.fontMetrics().elidedText(m_title, Qt::ElideMiddle,
                                                          m_filenameRect.width());
        p.drawText(m_filenameRect, Qt::AlignVCenter | Qt::AlignLeft, elided);
    }

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
                     || (b.action == Action::Layers && m_layersOpen)
                     || (b.action == Action::Open && m_openPickerActive);

        BtnVisual v = m_feedback->visual(i);
        ButtonPalette pal;
        pal.ink = ink;
        // 顶栏按钮反馈底统一 8px 圆角矩形（Fluent 图标统一审图修订 2026-11-18）。
        // 命中区是定稿的 kTopBtn(30)×kTopBtn(30)，只改视觉，不改按钮顺序与功能。
        pal.radius = 8;
        pal.hover = UiStyle::feedback::hover();
        pal.press = UiStyle::feedback::press();
        // 开启态只留中性底板（2026-11-22 图一）：强调色改成由 Filled 图标表达。
        pal.checkedBg = UiStyle::feedback::plate();
        pal.checkedBorder = QColor(0, 0, 0, 0);
        pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));

        if (b.action == Action::Close) {
            pal.radius = 0;
            pal.hover = UiStyle::color(UiStyle::Danger);
            pal.press = UiStyle::color(UiStyle::Danger).darker(122);
            pal.ink = (st.hover || st.pressed) ? QColor(Qt::white)
                                               : QColor(QString::fromLatin1(UiStyle::pick("#c9c9c9", "#333333")));
        } else if (b.action == Action::Minimize || b.action == Action::Maximize) {
            pal.radius = 0;
            pal.hover = UiStyle::feedback::hover();
            pal.press = UiStyle::feedback::press();
            pal.ink = QColor(QString::fromLatin1(UiStyle::pick("#c9c9c9", "#333333")));
        } else if (b.action == Action::TopLock) {
            // 顶栏闭锁：保持透明底；悬停仍是中性圆（与底栏明确区分）。
            pal.checkedBg = QColor(0, 0, 0, 0);
            pal.checkedBorder = QColor(0, 0, 0, 0);
            pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));
        } else if (b.action == Action::Pin) {
            // 图片置顶：沿用中性底板 + Filled 图钉，去掉原来的淡蓝描边（图二问题）。
            pal.checkedBg = UiStyle::feedback::plate();
            pal.checkedBorder = QColor(0, 0, 0, 0);
            pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));
        }

        // 全部功能按钮（图标/带文字）反馈底统一用整个命中区的 8px 圆角矩形；
        // 不再用 circleVisual 的 32×32 圆形反馈。命中区、图标位置、文字不变。
        // 窗口三键（关/最小化/最大化）pal.radius=0，保持系统标题栏直角惯例。
        const QRectF visual = QRectF(b.rect);
        beginButton(p, visual, st, v, pal);
        const QColor drawInk = buttonInk(st, pal);

        const QPointF center = b.rect.center();
        // 按下时图标轻微缩至 98%（按下只压暗不上浮；与底岛按钮同一语义）。
        const qreal iconS = 18.0 * (1.0 - 0.02 * v.press);
        // 一次性命令按下时短暂用 filled，松开恢复 regular（2026-11-19 定稿状态规则）。
        const AppIcon::Style iconStyle = (st.pressed && !st.disabled)
                                             ? AppIcon::Style::Filled
                                             : AppIcon::Style::Regular;
        // 带文字按钮：图标靠左（左内边距 10），文字接在图标右侧 8px。
        const QRectF icon = b.iconOnly
                                ? QRectF(center.x() - iconS / 2, center.y() - iconS / 2, iconS, iconS)
                                : QRectF(b.rect.x() + 10, center.y() - iconS / 2, iconS, iconS);

        switch (b.action) {
        case Action::Open:
            // 选择器存续期间保持 Filled（与 Layers 同一持续态范式）；按下瞬时填充照旧。
            drawIcon(p, "folder-open", icon, drawInk,
                     (m_openPickerActive || st.pressed) ? AppIcon::Style::Filled
                                                        : AppIcon::Style::Regular);
            break;
        case Action::Browse: drawIcon(p, "apps", icon, drawInk, iconStyle); break;
        case Action::Layers:
            // 面板展开是持续状态：展开期间保持 filled，收起回 regular；按下瞬时填充。
            drawIcon(p, "stack", icon, drawInk,
                     (m_layersOpen || st.pressed) ? AppIcon::Style::Filled
                                                  : AppIcon::Style::Regular);
            break;
        case Action::TopLock:
            // 顶部锁：锁定态用 filled（派单 §5：锁定=持续激活），解锁用 regular。
            drawIcon(p, m_topLocked ? "lock-closed" : "lock-open", icon, drawInk,
                     (m_topLocked || st.pressed) ? AppIcon::Style::Filled
                                                 : AppIcon::Style::Regular);
            break;
        case Action::Pin:
            drawIcon(p, "pin", icon, drawInk,
                     (m_pinned || st.pressed) ? AppIcon::Style::Filled
                                              : AppIcon::Style::Regular);
            break;
        case Action::Menu: drawHamburger(p, icon, drawInk); break;
        case Action::Minimize:
            drawCaptionGlyph(p, CaptionGlyph::Minimize,
                             QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink);
            break;
        case Action::Maximize:
            drawCaptionGlyph(p, window() && window()->isMaximized() ? CaptionGlyph::Restore
                                                                   : CaptionGlyph::Maximize,
                             QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink);
            break;
        case Action::Close:
            drawCaptionGlyph(p, CaptionGlyph::Close,
                             QRectF(center.x() - 7, center.y() - 7, 14, 14), pal.ink);
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
                drawIcon(p, "chevron-down",
                         QRectF(b.rect.right() - 16, center.y() - 5, 10, 10), mutedInk);
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

// 探针：在标题栏中部合成一次右键菜单请求，走的是与 contextMenuEvent 完全
// 相同的信号与锚点，验收「标题栏右键」入口（第 3 批两个入口之一）。
void TopBar::probeSystemMenu()
{
    emit systemMenuRequested(mapToGlobal(QPoint(width() / 2, height() / 2)));
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
// 底部控制岛（2026-11-19 定稿：596×44 外壳、14px 圆角长矩形）
// ===========================================================================

namespace {

constexpr int kIslandH = 44;
constexpr qreal kIslandRadius = 14.0;
constexpr int kIslandPadX = 10;      // 左右端内边距（垂直内边距 7 = 44-30 除以 2）
constexpr int kIslandBtn = 30;       // 按钮底板与点击区（含锁定）
constexpr int kIslandNavBtn = 30;    // 上一张/下一张
constexpr int kIslandY = (kIslandH - kIslandBtn) / 2;
constexpr int kIslandInnerGap = 3;
constexpr int kIslandGroupGap = 3;
constexpr int kIslandTrackWide = 120;      // 定稿滑条宽 120（只有宽档带滑杆）
constexpr int kZoomCapsuleW = 52;   // 缩放胶囊：点击在 100% / 适合窗口 间切换
constexpr int kZoomCapsuleH = 30;   // 与按钮同高，8px 圆角（不再是全圆胶囊）
constexpr int kPageValueW = 41;
constexpr int kProgressPadX = 7;     // 进度区左右呼吸位（定稿无内嵌底）
constexpr int kProgressInnerGap = 6; // 滑杆与页码之间

} // namespace

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(320, kIslandH);
    setGraphicsEffect(new HudEffect(this, kIslandRadius));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("底部控制栏"));
    setAccessibleDescription(tr("打开、缩略图、缩放、旋转、全屏、上一张、进度、下一张、锁定与更多"));
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
    // 锁图标的插值按动作标识保存，与窄窗换档后的按钮位置无关。
    m_feedback->setChecked(static_cast<int>(Action::Lock), locked);
    update();
}

void ControlBar::setOverviewActive(bool active)
{
    if (m_overviewActive == active) {
        return;
    }
    m_overviewActive = active;
    update();
}

void ControlBar::setGroupingActive(bool active)
{
    if (m_groupingActive == active) {
        return;
    }
    m_groupingActive = active;
    update();
}

void ControlBar::setOpenPickerActive(bool active)
{
    if (m_openPickerActive == active) {
        return;
    }
    m_openPickerActive = active;
    update();
}

QRect ControlBar::openButtonRect() const
{
    const QVector<Item> list = items();
    for (const Item &it : list) {
        if (it.action == Action::Open) {
            return it.rect;
        }
    }
    return QRect();
}

// 总览内外同位置的那颗按钮：总览打开是「分组方式」，关闭是「缩略图栏」。
QRect ControlBar::groupButtonRect() const
{
    for (const Item &it : items()) {
        if (it.action == Action::GroupBy || it.action == Action::List) {
            return it.rect;
        }
    }
    return QRect();
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
    static_cast<HudEffect *>(graphicsEffect())->setShadowOpacity(weakened ? 0.45 : 1.0);
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
// 宽档沿用 596 定稿；中档收到页码与锁定；窄档只留上一张/下一张/更多。
// 阈值由控件与本地化文案的真实宽度算出，不是浏览器原型里的 650/383 固定值。
namespace {
constexpr int kTierBreath = 24;      // 每侧呼吸位
constexpr int kTierHysteresis = 6;   // 跨档回滞：拉宽回上一档要多出的宽度
} // namespace

int ControlBar::tierLevel() const
{
    return static_cast<int>(m_tier);
}

ControlBar::Tier ControlBar::resolveTier(int availableWidth) const
{
    const int wideThreshold = naturalWidthFor(Tier::Wide, kIslandTrackWide) + 2 * kTierBreath;
    const int mediumThreshold = naturalWidthFor(Tier::Medium, 0) + 2 * kTierBreath;
    // 回到更宽档要多 6 逻辑 px：阈值附近拖宽/拖窄不会来回跳档。
    if (availableWidth >= wideThreshold + (m_tier == Tier::Wide ? 0 : kTierHysteresis)) {
        return Tier::Wide;
    }
    if (availableWidth >= mediumThreshold + (m_tier == Tier::Medium ? 0 : kTierHysteresis)) {
        return Tier::Medium;
    }
    return Tier::Narrow;
}

int ControlBar::naturalWidthFor(Tier t, int trackWidth) const
{
    const QVector<Item> list = itemsFor(t, trackWidth);
    return list.isEmpty() ? 0 : list.last().rect.right() + 1 + kIslandPadX;
}

void ControlBar::setAvailableWidth(int width)
{
    const int w = qMax(160, width);
    if (m_tierResolved && w == m_availableWidth) {
        return;
    }
    m_availableWidth = w;
    m_tierResolved = true;
    const Tier want = resolveTier(w);
    const bool changed = want != m_tier;
    m_tier = want;
    recomputeWidth();
    if (changed) {
        emit tierChanged(tierLevel());
    }
}

void ControlBar::recomputeWidth()
{
    // 只有宽档带进度滑杆；中/窄档滑杆收进「更多」，页码/倍率占独立宽度。
    m_trackWidth = (m_tier == Tier::Wide) ? kIslandTrackWide : 0;
    int width = naturalWidthFor(m_tier, m_trackWidth);
    // 宽档极窄窗：先缩短轨道（下限 48），保留翻页热区与页码/倍率。
    if (width > m_availableWidth && m_trackWidth > 48) {
        m_trackWidth = qMax(48, m_trackWidth - (width - m_availableWidth));
        width = naturalWidthFor(m_tier, m_trackWidth);
    }
    m_preferredWidth = width;
    // 任何档位都不得宽于可用宽度：宁可两侧留白，也不让岛被窗口裁掉。
    setFixedWidth(qMin(width, m_availableWidth));
    update();
}

QVector<ControlBar::Item> ControlBar::items() const
{
    return itemsFor(m_tier, m_trackWidth);
}

// 三档共用同一套动作与状态：换档只改「谁在岛上、谁进更多」，不改任何命令语义。
QVector<ControlBar::Item> ControlBar::itemsFor(Tier t, int trackWidth) const
{
    QVector<Item> out;
    int x = kIslandPadX;
    const int y = kIslandY;
    const bool wide = t == Tier::Wide;
    const bool medium = t == Tier::Medium;

    auto place = [&](Action a, int w, int h = kIslandBtn) {
        out.append(Item{ a, QRect(x, y + (kIslandBtn - h) / 2, w, h) });
        x += w;
    };

    // —— 打开 / 总览 / 缩略图（总览内同格位切成「分组方式」）——
    if (wide) {
        place(Action::Open, kIslandBtn);
        x += kIslandInnerGap;
        // 图片总览入口紧挨缩略图条入口（2026-11-19 定稿：打开、总览、缩略图条）。
        // 总览打开后同一格位切成「分组方式」（2026-11-22 派单）：按钮尺寸不变，
        // 退出总览仍回到「缩略图栏」。
        place(Action::Overview, kIslandBtn);
        x += kIslandInnerGap;
        place(m_overviewActive ? Action::GroupBy : Action::List, kIslandBtn);
        x += kIslandGroupGap;
    }

    // —— 缩放：宽/中档保留加减与倍率胶囊；窄档整体进「更多」——
    if (wide || medium) {
        place(Action::ZoomOut, kIslandBtn);
        x += kIslandInnerGap;
        place(Action::ZoomValue, kZoomCapsuleW, kZoomCapsuleH);
        x += kIslandInnerGap;
        place(Action::ZoomIn, kIslandBtn);
        x += kIslandGroupGap;
    }

    // —— 旋转 / 全屏：只有宽档上岛 ——
    if (wide) {
        place(Action::Rotate, kIslandBtn);
        x += kIslandInnerGap;
        place(Action::Fullscreen, kIslandBtn);
        x += kIslandGroupGap;
    }

    place(Action::Prev, kIslandNavBtn, kIslandNavBtn);
    x += 8;
    // 进度块：宽档是「滑杆 + 页码」；中档只留页码（滑杆进更多）；
    // 窄档连页码一起收走，页数仍在右下角序号与菜单里可读。
    if (wide) {
        x += kProgressPadX;
        place(Action::PageSlider, trackWidth);
        x += kProgressInnerGap;
        place(Action::PageValue, kPageValueW);
        x += kProgressPadX + 8;
    } else if (medium) {
        place(Action::PageValue, kPageValueW);
        x += 8;
    }
    place(Action::Next, kIslandNavBtn, kIslandNavBtn);

    // —— 锁定：宽/中档常驻；窄档收进「更多」——
    if (wide || medium) {
        x += kIslandGroupGap;
        place(Action::Lock, kIslandBtn);
    }

    // —— 更多：中/窄档的兜底入口，永远排在最后 ——
    if (!wide) {
        x += kIslandGroupGap;
        place(Action::More, kIslandBtn);
    }

    return out;
}

QVector<int> ControlBar::hiddenActionIds() const
{
    QVector<int> out;
    const bool wide = m_tier == Tier::Wide;
    const bool medium = m_tier == Tier::Medium;
    if (!wide) {
        out << 0 << 1 << 2;   // 打开 / 总览 / 缩略图或分组
    }
    if (!wide && !medium) {
        out << 3 << 4 << 5;   // 缩小 / 倍率 / 放大
    }
    if (!wide) {
        out << 6 << 7 << 9;   // 旋转 / 全屏 / 进度滑杆
    }
    if (m_tier == Tier::Narrow) {
        out << 10 << 12;      // 页码 / 锁定
    }
    return out;
}

QRect ControlBar::moreButtonRect() const
{
    for (const Item &it : items()) {
        if (it.action == Action::More) {
            return it.rect;
        }
    }
    return QRect();
}

void ControlBar::focusMore()
{
    const QVector<Item> list = items();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].action == Action::More) {
            // 用 TabFocusReason：菜单返回属于键盘续接，focusInEvent 才不会把
            // 焦点位打回 -1；随后把位置钉在「更多」上。
            setFocus(Qt::TabFocusReason);
            m_keyboardFocus = true;
            m_focus = i;
            m_feedback->setHover(i);
            update();
            return;
        }
    }
}

bool ControlBar::moreFocusHeld() const
{
    const QVector<Item> list = items();
    // 只看自己的键盘焦点位：离屏冒烟里窗口不是活动窗口，hasFocus() 恒为 false，
    // 但焦点环已按 m_keyboardFocus 绘出（截图可核对）。
    return m_keyboardFocus && m_focus >= 0 && m_focus < list.size()
           && list[m_focus].action == Action::More;
}

QString ControlBar::layoutDump() const{
    QStringList hidden;
    for (int id : hiddenActionIds()) {
        hidden << QString::number(id);
    }
    const QRect more = moreButtonRect();
    return QStringLiteral("tier=%1 avail=%2 width=%3 track=%4 hidden=[%5] more=%6,%7,%8,%9 kbd=%10 focus=%11")
        .arg(tierLevel())
        .arg(m_availableWidth)
        .arg(width())
        .arg(m_trackWidth)
        .arg(hidden.join(u','))
        .arg(more.x())
        .arg(more.y())
        .arg(more.width())
        .arg(more.height())
        .arg(m_keyboardFocus ? 1 : 0)
        .arg(m_focus);
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
    case Action::GroupBy: return tr("分组方式");
    case Action::Overview: return tr("图片总览");
    case Action::ZoomOut: return m_overviewActive ? tr("缩小缩略图") : tr("缩小");
    case Action::ZoomValue:
        // 总览态（kind=3）：胶囊只显示缩略图档位，点击不循环切画布。
        if (m_zoomKind == 3) return tr("缩略图大小");
        // 提示跟着下一步操作走：100% 时点它去适合窗口，其余状态都先回原始尺寸。
        return m_zoomKind == 1 ? tr("适合窗口") : tr("查看原始尺寸（100%）");
    case Action::ZoomIn: return m_overviewActive ? tr("放大缩略图") : tr("放大");
    case Action::Rotate: return tr("旋转：左键逆时针 90°，右键顺时针 90°");
    case Action::Fullscreen: return tr("全屏 / 还原 (F11)");
    case Action::Prev: return tr("上一张图片 (←)");
    case Action::PageSlider:
        return sliderEnabled() ? tr("浏览进度：拖动切换图片；滚轮上一张/下一张")
                               : tr("浏览进度：只有一张图片");
    case Action::PageValue: return tr("当前页码 / 总页数");
    case Action::Next: return tr("下一张图片 (→)");
    case Action::Lock: return tr("保持底部控制栏显示");
    case Action::More: return tr("更多命令");
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

    // 底栏外壳：14px 圆角长矩形（Fluent 图标统一审图修订 2026-11-18）。
    // 定稿 596×44：四角 14px 圆角 → 两端各留 44-2*14=16px 直边；
    // 进度条与页码直接落在外壳上，不再套内嵌底（2026-11-19 定稿）。
    p.setPen(QPen((UiStyle::isLight() ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 30)), 1));
    p.setBrush(UiStyle::isLight() ? QColor(250, 250, 251, 242)
                                  : QColor(31, 32, 35, 227));
    p.drawRoundedRect(capsule, kIslandRadius, kIslandRadius);

    // 进度条与页码直接落在外壳上（定稿无内嵌底）。

    const QVector<Item> list = items();
    const QColor ink = QColor(QString::fromLatin1(UiStyle::pick("#d7d9dc", "#26282a")));
    const QColor accent = UiStyle::accent();

    for (int i = 0; i < list.size(); ++i) {
        const Item &it = list.at(i);
        const QRect r = it.rect;
        BtnVisual v = m_feedback->visual(i);

        if (it.action == Action::PageSlider) {
            p.save();
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
            p.restore();
            continue;
        }

        if (it.action == Action::ZoomValue) {
            // 缩放胶囊：中性深底常亮，悬停提亮、按下加深，只走颜色过渡——
            // 不套 beginButton 的按压缩放，也不占蓝色选中态，避免与锁定、进度滑杆抢重点。
            // 常亮胶囊底 = 开启底板 plate；悬停 hover、按下 press，统一走反馈套系。
            const QColor base = UiStyle::feedback::plate();
            const QColor hot = UiStyle::feedback::hover();
            const QColor down = UiStyle::feedback::press();
            QColor bg = base;
            const qreal over = qMax(v.hover, v.press);
            if (over > 0.004) {
                bg = blendColor(base, hot, over);
            }
            if (v.press > 0.004) {
                bg = blendColor(bg, down, v.press);
            }
            const QRectF cap(r);
            // 定稿：缩放值胶囊与按钮同为 8px 圆角矩形（不再是全圆胶囊）。
            constexpr qreal capRadius = 8.0;
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
        st.checked = (it.action == Action::Lock && m_locked)
                     || (it.action == Action::Overview && m_overviewActive)
                     || (it.action == Action::GroupBy && m_groupingActive)
                     || (it.action == Action::Open && m_openPickerActive);

        ButtonPalette pal;
        // 定稿：按钮底板 30×30、圆角 7。
        pal.radius = 7;
        pal.hover = UiStyle::feedback::hover();
        pal.press = UiStyle::feedback::press();
        pal.ink = ink;
        // 开启态：中性底板 + Filled 图标，不叠蓝底/蓝描边（2026-11-22 图一）。
        pal.checkedBg = UiStyle::feedback::plate();
        pal.checkedBorder = QColor(0, 0, 0, 0);
        pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));

        const QPointF center = r.center();

        if (it.action == Action::Lock) {
            // 锁定态交叉淡化：透明底（开）↔ 中性底板 + 白色 Filled 锁（锁），
            // 2026-11-22 起不再用蓝底，避免与进度条抢重点。
            const qreal chk = m_feedback->visual(static_cast<int>(Action::Lock)).checked;
            const qreal lockS = 18.0 * (1.0 - 0.02 * v.press);
            if (chk < 0.999) {
                p.save();
                p.setOpacity(p.opacity() * (1.0 - chk));
                BtnState stU = st;
                stU.checked = false;
                beginButton(p, QRectF(r), stU, v, pal);
                drawIcon(p, "lock-open",
                         QRectF(center.x() - lockS / 2, center.y() - lockS / 2, lockS, lockS),
                         buttonInk(stU, pal),
                         stU.pressed ? AppIcon::Style::Filled : AppIcon::Style::Regular);
                endButton(p);
                p.restore();
            }
            if (chk > 0.001) {
                p.save();
                p.setOpacity(p.opacity() * chk);
                QColor plate = UiStyle::feedback::plate();
                if (st.hover || st.pressed) {
                    plate = blendColor(plate, UiStyle::feedback::hover(), 0.6);
                }
                p.setPen(Qt::NoPen);
                p.setBrush(plate);
                p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
                drawIcon(p, "lock-closed",
                         QRectF(center.x() - lockS / 2, center.y() - lockS / 2, lockS, lockS),
                         QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214"))),
                         AppIcon::Style::Filled);
                p.restore();
            }
            continue;
        }

        beginButton(p, QRectF(r), st, v, pal);
        const QColor drawInk = buttonInk(st, pal);
        // 定稿：底栏图标 18×18；按下时轻微缩至 98%（不放大、不上浮）。
        const qreal iconS = 18.0 * (1.0 - 0.02 * v.press);
        const QRectF icon(center.x() - iconS / 2, center.y() - iconS / 2, iconS, iconS);
        // 一次性命令按下时短暂用 filled，松开恢复 regular（2026-11-19 定稿状态规则）。
        const AppIcon::Style iconStyle = st.pressed ? AppIcon::Style::Filled
                                                    : AppIcon::Style::Regular;

        switch (it.action) {
        case Action::Open:
            // 选择器存续期间保持 Filled（与 Overview/GroupBy 同一持续态范式）；按下瞬时填充照旧。
            drawIcon(p, "folder-open", icon, drawInk,
                     (m_openPickerActive || st.pressed) ? AppIcon::Style::Filled
                                                         : AppIcon::Style::Regular);
            break;
        case Action::List: drawIcon(p, "list", icon, drawInk, iconStyle); break;
        case Action::GroupBy:
            // 总览内的分组入口：已分组时 Filled 图标表达「正在分组」，底板走 checked 中性底。
            drawIcon(p, "sort", icon, drawInk,
                     (m_groupingActive || st.pressed) ? AppIcon::Style::Filled
                                                      : AppIcon::Style::Regular);
            break;
        case Action::Overview:
            drawIcon(p, "apps", icon, drawInk,
                     (m_overviewActive || st.pressed) ? AppIcon::Style::Filled
                                                      : AppIcon::Style::Regular);
            break;
        case Action::ZoomOut: drawIcon(p, "subtract", icon, drawInk, iconStyle); break;
        case Action::ZoomIn: drawIcon(p, "add", icon, drawInk, iconStyle); break;
        case Action::Rotate:
            drawIcon(p, "arrow-rotate-counterclockwise", icon, drawInk, iconStyle);
            break;
        case Action::Fullscreen: drawIcon(p, "full-screen-maximize", icon, drawInk, iconStyle); break;
        case Action::Prev: drawIcon(p, "chevron-left", icon, drawInk, iconStyle); break;
        case Action::Next: drawIcon(p, "chevron-right", icon, drawInk, iconStyle); break;
        case Action::More: drawIcon(p, "dots", icon, drawInk, iconStyle); break;
        case Action::Lock: drawIcon(p, "lock-open", icon, drawInk, iconStyle); break;
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
    case Action::GroupBy: emit groupByClicked(); break;
    case Action::Overview: emit overviewClicked(); break;
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
    case Action::More: {
        const QRect r = moreButtonRect();
        emit moreClicked(r.isNull() ? mapToGlobal(QPoint(width() / 2, 0))
                                    : mapToGlobal(r.center()));
        break;
    }
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
// GIF / MNG 播放控制栏
// ===========================================================================

namespace {
constexpr int kPlayBarH = 40;
constexpr int kPlayBarBtn = 30;
constexpr int kPlayBarGap = 5;
constexpr int kPlayBarPadX = 8;
constexpr int kPlayBarSliderW = 150;
constexpr int kPlayBarSliderGap = 12;
constexpr qreal kPlayBarRadius = 14.0;
} // namespace

PlayBar::PlayBar(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setFixedHeight(kPlayBarH);
    setGraphicsEffect(new HudEffect(this, kPlayBarRadius));
    setAccessibleName(tr("动画播放控制栏"));
    setAccessibleDescription(tr("首帧、上一帧、播放暂停、下一帧、末帧与帧进度滑条"));
    m_feedback = new ButtonFeedback(this);
    connect(m_feedback, &ButtonFeedback::changed, this, [this]() { update(); });
    m_tip = new TooltipHint(this);
}

int PlayBar::preferredWidth() const
{
    return 2 * kPlayBarPadX + 5 * kPlayBarBtn + 4 * kPlayBarGap
           + kPlayBarSliderGap + kPlayBarSliderW;
}

void PlayBar::setState(int index, int total, bool playing)
{
    m_total = qMax(0, total);
    m_index = m_total > 0 ? qBound(0, index, m_total - 1) : 0;
    m_playing = playing;
    update();
}

QVector<PlayBar::Item> PlayBar::items() const
{
    QVector<Item> out;
    const int y = (kPlayBarH - kPlayBarBtn) / 2;
    int x = kPlayBarPadX;
    auto place = [&](Action a) {
        out.append(Item{ a, QRect(x, y, kPlayBarBtn, kPlayBarBtn) });
        x += kPlayBarBtn + kPlayBarGap;
    };
    place(Action::First);
    place(Action::Prev);
    place(Action::Play);
    place(Action::Next);
    place(Action::Last);
    x -= kPlayBarGap;
    out.append(Item{ Action::FrameSlider,
                     QRect(x + kPlayBarSliderGap, y, kPlayBarSliderW, kPlayBarBtn) });
    return out;
}

int PlayBar::hitIndex(const QPoint &pos) const
{
    const QVector<Item> list = items();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

QRect PlayBar::sliderRect() const
{
    const QVector<Item> list = items();
    for (const Item &it : list) {
        if (it.action == Action::FrameSlider) {
            return it.rect;
        }
    }
    return QRect();
}

QRect PlayBar::playButtonRect() const
{
    const QVector<Item> list = items();
    for (const Item &it : list) {
        if (it.action == Action::Play) {
            return it.rect;
        }
    }
    return QRect();
}

int PlayBar::frameFromX(int x) const
{
    const QRect track = sliderRect();
    if (track.isNull() || !sliderEnabled()) {
        return m_index;
    }
    const qreal left = track.left() + 7.0;
    const qreal right = track.right() - 7.0;
    const qreal ratio = qBound(0.0, (x - left) / qMax(1.0, right - left), 1.0);
    return qBound(0, qRound(ratio * (m_total - 1)), m_total - 1);
}

QString PlayBar::tipText(Action action) const
{
    switch (action) {
    case Action::First: return canJumpEnds() ? tr("首帧") : tr("首帧（帧数未知）");
    case Action::Prev: return tr("上一帧");
    case Action::Play: return m_playing ? tr("暂停") : tr("播放");
    case Action::Next: return tr("下一帧");
    case Action::Last: return canJumpEnds() ? tr("末帧") : tr("末帧（帧数未知）");
    case Action::FrameSlider:
        return sliderEnabled() ? tr("帧进度：拖动定位到帧") : tr("帧进度：帧数未知");
    default: break;
    }
    return QString();
}

QString PlayBar::layoutDump() const
{
    const QVector<Item> list = items();
    QString out;
    QTextStream ts(&out);
    ts << "index=" << m_index << " total=" << m_total << " playing=" << (m_playing ? 1 : 0)
       << " slider=" << (sliderEnabled() ? 1 : 0) << " ends=" << (canJumpEnds() ? 1 : 0) << "\n";
    for (const Item &it : list) {
        const char *name = "?";
        switch (it.action) {
        case Action::First: name = "first"; break;
        case Action::Prev: name = "prev"; break;
        case Action::Play: name = "play"; break;
        case Action::Next: name = "next"; break;
        case Action::Last: name = "last"; break;
        case Action::FrameSlider: name = "slider"; break;
        default: break;
        }
        ts << name << " rect=" << it.rect.left() << "," << it.rect.top() << ","
           << it.rect.width() << "x" << it.rect.height() << "\n";
    }
    return out;
}

void PlayBar::activate(Action action)
{
    switch (action) {
    case Action::First:
        if (canJumpEnds()) emit firstClicked();
        break;
    case Action::Prev: emit prevClicked(); break;
    case Action::Play: emit playToggleClicked(); break;
    case Action::Next: emit nextClicked(); break;
    case Action::Last:
        if (canJumpEnds()) emit lastClicked();
        break;
    default: break;
    }
}

void PlayBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF capsule(0.5, 0.5, width() - 1.0, kPlayBarH - 1.0);
    p.setPen(QPen((UiStyle::isLight() ? QColor(0, 0, 0, 28) : QColor(255, 255, 255, 30)), 1));
    p.setBrush(UiStyle::isLight() ? QColor(250, 250, 251, 242)
                                  : QColor(31, 32, 35, 227));
    p.drawRoundedRect(capsule, kPlayBarRadius, kPlayBarRadius);

    const QVector<Item> list = items();
    const QColor ink = QColor(QString::fromLatin1(UiStyle::pick("#d7d9dc", "#26282a")));
    const QColor accent = UiStyle::accent();

    for (int i = 0; i < list.size(); ++i) {
        const Item &it = list.at(i);
        const QRect r = it.rect;
        BtnVisual v = m_feedback->visual(i);

        if (it.action == Action::FrameSlider) {
            p.save();
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
            const qreal ratio = enabled ? m_index / qMax(1.0, qreal(m_total - 1)) : 0.0;
            const qreal thumbX = left + ratio * (right - left);
            if (enabled) {
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
            p.restore();
            continue;
        }

        const bool endsDisabled = (it.action == Action::First || it.action == Action::Last)
                                  && !canJumpEnds();
        BtnState st;
        st.hover = (m_hover == i);
        st.pressed = (m_press == i);
        st.disabled = endsDisabled;

        ButtonPalette pal;
        pal.radius = 7;
        pal.hover = UiStyle::feedback::hover();
        pal.press = UiStyle::feedback::press();
        pal.ink = ink;
        pal.checkedBg = UiStyle::feedback::plate();
        pal.checkedBorder = QColor(0, 0, 0, 0);
        pal.checkedInk = QColor(QString::fromLatin1(UiStyle::pick("#ffffff", "#101214")));

        const QPointF center = r.center();
        beginButton(p, QRectF(r), st, v, pal);
        const QColor drawInk = buttonInk(st, pal);
        const qreal iconS = 18.0 * (1.0 - 0.02 * v.press);
        const QRectF icon(center.x() - iconS / 2, center.y() - iconS / 2, iconS, iconS);
        const AppIcon::Style iconStyle = st.pressed ? AppIcon::Style::Filled
                                                    : AppIcon::Style::Regular;

        switch (it.action) {
        case Action::First: drawIcon(p, "first", icon, drawInk, iconStyle); break;
        case Action::Prev: drawIcon(p, "chevron-left", icon, drawInk, iconStyle); break;
        case Action::Play:
            if (m_playing) {
                drawIcon(p, "stop", icon, drawInk, AppIcon::Style::Regular);
            } else {
                drawIcon(p, "play", icon, drawInk, AppIcon::Style::Filled);
            }
            break;
        case Action::Next: drawIcon(p, "chevron-right", icon, drawInk, iconStyle); break;
        case Action::Last: drawIcon(p, "last", icon, drawInk, iconStyle); break;
        default: break;
        }
        endButton(p);
    }
}

void PlayBar::mousePressEvent(QMouseEvent *e)
{
    m_tip->hideNow();
    const QVector<Item> list = items();
    const int idx = hitIndex(e->pos());
    const Action action = (idx >= 0 && idx < list.size()) ? list[idx].action : Action::None;

    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_press = idx;
    m_feedback->setPressed(idx);
    if (action == Action::FrameSlider && sliderEnabled()) {
        m_dragging = true;
        m_pressFrame = m_index;
        const int f = frameFromX(e->pos().x());
        if (f != m_index) {
            m_index = f;
            m_previewClock.restart();
            emit framePreviewRequested(m_index);
        }
        update();
        return;
    }
    update();
    QWidget::mousePressEvent(e);
}

void PlayBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        m_tip->hideNow();
        const int f = frameFromX(e->pos().x());
        if (f != m_index) {
            m_index = f;
            if (!m_previewClock.isValid() || m_previewClock.elapsed() >= 60) {
                m_previewClock.restart();
                emit framePreviewRequested(m_index);
            }
            update();
        }
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
    if (m_hover >= 0 && list[m_hover].action == Action::FrameSlider) {
        setCursor(sliderEnabled() ? Qt::SizeHorCursor : Qt::ArrowCursor);
    } else {
        setPointerCursor(this, m_hover >= 0);
    }
    if (e->buttons() != Qt::NoButton) {
        m_tip->hideNow();
    } else if (m_hover >= 0) {
        const Item &it = list.at(m_hover);
        m_tip->hover(tipText(it.action), e->globalPosition().toPoint());
    } else {
        m_tip->hideNow();
    }
    update();
    QWidget::mouseMoveEvent(e);
}

void PlayBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int pressedIndex = m_press;
    m_press = -1;
    m_feedback->setPressed(-1);
    if (m_dragging) {
        m_dragging = false;
        update();
        if (pressedIndex >= 0 && pressedIndex < items().size()
            && items()[pressedIndex].action == Action::FrameSlider
            && m_index != m_pressFrame) {
            emit frameRequested(m_index);
        }
        m_pressFrame = m_index;
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

void PlayBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    m_feedback->setHover(-1);
    setPointerCursor(this, false);
    m_tip->hideNow();
    update();
}

void PlayBar::wheelEvent(QWheelEvent *e)
{
    m_tip->hideNow();
    const QVector<Item> list = items();
    const int idx = hitIndex(e->position().toPoint());
    const bool onSlider = (idx >= 0 && idx < list.size()
                           && list[idx].action == Action::FrameSlider);
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
    // 左下信息与右下序号同用次要文字令牌 muted（深 #92979e / 浅 #6b7075）。
    // 改前是两处各写一档更暗的灰（#797d82/#85898e 族），10px 小字实测对比度
    // 深色 4.45:1、浅色 3.19:1（Temp/panel-unify-20261114/before 实测），
    // 用户把两角看成「空白」；规范 §5 的 4.5:1 目标按令牌口径达成。
    p.setPen(UiStyle::muted());
    p.drawText(infoRect, Qt::AlignVCenter | Qt::AlignLeft, m_info);
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

void EmptyState::setStatus(const QString &text, bool isError)
{
    if (m_status == text && m_statusError == isError) {
        return;
    }
    m_status = text;
    m_statusError = isError;
    update();
}

void EmptyState::setOpenPickerActive(bool active)
{
    if (m_openPickerActive == active) {
        return;
    }
    m_openPickerActive = active;
    update();
}

QRect EmptyState::openButtonRect() const
{
    return primaryRect();
}

QRect EmptyState::primaryRect() const
{
    const int y = (height() - 340) / 2;
    // 单一主按钮居中（2026-11-21 打开入口单）：原「打开图片 / 选择文件夹」两钮
    // 合并为一个「打开」，同一个选择器里既能选图片也能确认文件夹。
    return QRect(width() / 2 - 80, y + 197, 160, 36);
}

void EmptyState::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int top = (height() - 340) / 2;
    const QRectF iconRect(width() / 2.0 - 38, top, 76, 76);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#232323", "#f4f4f4"))));
    p.drawRoundedRect(iconRect, 18, 18);
    drawIcon(p, "image",
             QRectF(iconRect.center().x() - 17, iconRect.center().y() - 17, 34, 34),
             QColor(QString::fromLatin1(UiStyle::pick("#bdbdbd", "#444444"))));

    p.setFont(uiFont(22, QFont::Medium));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#f3f3f3", "#3a3a3a"))));
    p.drawText(QRect(0, top + 94, width(), 30), Qt::AlignCenter,
               QCoreApplication::translate("EmptyState", "开始查看图片"));

    p.setFont(uiFont(13));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#999999", "#666666"))));
    p.drawText(QRect(0, top + 132, width(), 22), Qt::AlignCenter,
               QCoreApplication::translate("EmptyState", "把图片拖到这里，或点「打开」选择图片或文件夹"));
    // 加载/失败状态行（连续翻页派单 §二）：仅首次打开（画布无上一张）时可见，
    // 等待用中性灰，失败用警示色，不挤占按钮与快捷键提示的布局位。
    if (!m_status.isEmpty()) {
        p.setFont(uiFont(12, QFont::Medium));
        const QFontMetrics fm = p.fontMetrics();
        const QColor statusInk = m_statusError
                                     ? QColor(QString::fromLatin1(UiStyle::fluent::error()))
                                     : QColor(QString::fromLatin1(UiStyle::pick("#8a8f96", "#5a5f66")));
        // 静态提示按语义配 Fluent 图标（2026-11-19 定稿）：失败 Error Circle，
        // 等待/加载 Info。图标与文字作为一组居中，避免文字单边漂移。
        const int iconPx = 16;
        const int gap = 6;
        const int textW = fm.horizontalAdvance(m_status);
        const qreal groupX = (width() - (iconPx + gap + textW)) / 2.0;
        const qreal rowY = top + 176;
        const bool drew = drawIcon(p, m_statusError ? "error-circle" : "info",
                                   QRectF(groupX, rowY + 2, iconPx, iconPx), statusInk);
        p.setPen(statusInk);
        p.drawText(QRectF(groupX + (drew ? iconPx + gap : 0), rowY,
                          width() - groupX, 20),
                   Qt::AlignVCenter | Qt::AlignLeft, m_status);
    }

    const QRect primary = primaryRect();

    p.setPen(QPen(QColor(QStringLiteral("#2485b3")), 1));
    p.setBrush(m_hover == 0 ? QColor(QStringLiteral("#0e7fb5"))
                            : QColor(QStringLiteral("#0b6f9f")));
    p.drawRoundedRect(QRectF(primary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    drawIcon(p, "folder-open",
             QRectF(primary.x() + 18, primary.center().y() - 8, 16, 16),
             QColor(Qt::white),
             m_openPickerActive ? AppIcon::Style::Filled : AppIcon::Style::Regular);
    p.setFont(uiFont(12, QFont::DemiBold));
    p.setPen(QColor(Qt::white));
    p.drawText(QRect(primary.x() + 40, primary.y(), primary.width() - 44, primary.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QCoreApplication::translate("EmptyState", "打开"));

    // 快捷键提示
    struct KbdGroup {
        QStringList keys;
        QString label;
    };
    const QVector<KbdGroup> groups = {
        { { QStringLiteral("F2") }, QCoreApplication::translate("EmptyState", "打开") },
        { { QStringLiteral("F11") }, QCoreApplication::translate("EmptyState", "全屏查看") },
        { { QStringLiteral("F5") }, QCoreApplication::translate("EmptyState", "设置") },
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
    }
    QWidget::mouseReleaseEvent(e);
}

void EmptyState::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = primaryRect().contains(e->pos()) ? 0 : -1;
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

void Toast::showMessage(const QString &text, Tone tone)
{
    m_text = text;
    m_tone = tone;
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
    // 图标 16 + 间距 8 + 左右内边距 14（定稿：通知也用同义 Fluent 图标，不再纯文本）。
    const int iconPx = 16;
    const int gap = 8;
    const int padX = 14;
    const int w = qMin(parentWidget() ? parentWidget()->width() - 40 : 360,
                       fm.horizontalAdvance(m_text) + iconPx + gap + padX * 2);
    // 右下角贴右缘 20px；顶部居中保持原位
    const qreal x = m_corner ? (width() - w - 20.0) : ((width() - w) / 2.0);
    const QRectF r(x, 0, w, 30);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#383838", "#c7c7c7"))));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);

    QColor iconInk = UiStyle::muted();
    const char *iconId = "info";
    switch (m_tone) {
    case Tone::Success:
        iconId = "checkmark-circle";
        iconInk = UiStyle::fluent::statusOkColor();
        break;
    case Tone::Warning:
        iconId = "warning";
        iconInk = UiStyle::fluent::statusWarnColor();
        break;
    case Tone::Error:
        iconId = "error-circle";
        iconInk = QColor(QString::fromLatin1(UiStyle::fluent::error()));
        break;
    case Tone::Info:
    default:
        break;
    }
    const bool drewIcon = drawIcon(
        p, iconId,
        QRectF(r.x() + padX, r.center().y() - iconPx / 2.0, iconPx, iconPx), iconInk);

    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#eeeeee", "#404040"))));
    // 图标缺失时文字仍从内边距起排，不留空洞（回退与全量缺图路径同用）。
    const qreal textX = r.x() + padX + (drewIcon ? iconPx + gap : 0);
    p.drawText(QRectF(textX, r.y(), r.right() - textX - padX, r.height()),
               Qt::AlignVCenter | Qt::AlignLeft, m_text);
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
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#282a2d", "#dfe1e3"))));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(11));
    p.setPen(QColor(QString::fromLatin1(UiStyle::pick("#d7d9dc", "#26282a"))));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace Chrome
