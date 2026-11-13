#include "PillToggle.h"

#include "UiStyle.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QVariantAnimation>

namespace {

// 主题切换时轨道与滑块都要换色；过渡期间按位置插值，避免颜色先跳、位置后到。
QColor mixColor(const QColor &from, const QColor &to, qreal t)
{
    const qreal k = qBound(0.0, t, 1.0);
    return QColor(qRound(from.red() + (to.red() - from.red()) * k),
                  qRound(from.green() + (to.green() - from.green()) * k),
                  qRound(from.blue() + (to.blue() - from.blue()) * k),
                  qRound(from.alpha() + (to.alpha() - from.alpha()) * k));
}

} // namespace

PillToggle::PillToggle(QWidget *parent)
    : QCheckBox(parent)
{
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_pos = isChecked() ? 1.0 : 0.0;

    m_motion = new QVariantAnimation(this);
    m_motion->setDuration(motionDurationMs());
    m_motion->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_motion, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pos = v.toReal();
        update();
    });
}

void PillToggle::nextCheckState()
{
    m_userToggle = true;
    QCheckBox::nextCheckState();
    m_userToggle = false;
    startMotion(isChecked());
}

void PillToggle::checkStateSet()
{
    QCheckBox::checkStateSet();
    // 程序改值（打开对话框时同步主题状态、跟随系统解析值变化）：直接落位。
    if (!m_userToggle) {
        m_motion->stop();
        m_pos = isChecked() ? 1.0 : 0.0;
        update();
    }
}

void PillToggle::startMotion(bool on)
{
    const qreal target = on ? 1.0 : 0.0;
    // 禁用态不会有用户拨动；不可见时没必要花一帧一帧的动画。
    if (motionDurationMs() <= 0 || !isEnabled() || !isVisible()) {
        m_motion->stop();
        m_pos = target;
        update();
        return;
    }
    if (qFuzzyCompare(m_pos + 1.0, target + 1.0))
        return;   // 已经在目标位置（连续点击被打断后回到原状态）
    // 从当前位置续走：快速反向切换时位置连续，不会跳回起点。
    m_motion->stop();
    m_motion->setStartValue(m_pos);
    m_motion->setEndValue(target);
    m_motion->start();
}

void PillToggle::setFluent(bool on)
{
    if (m_fluent == on)
        return;
    m_fluent = on;
    updateGeometry();
    update();
}

bool PillToggle::fluentMode() const
{
    return m_fluent || property("fluent").toString() == QLatin1String("v2");
}

QSize PillToggle::sizeHint() const
{
    if (fluentMode())
        return QSize(38, 24);   // mini switch：轨道 30×16 + 2px 环 + 2px 外距
    return QSize(UiStyle::SwitchTrackW + 2 * UiStyle::SwitchPad,
                 UiStyle::SwitchTrackH + 2 * UiStyle::SwitchPad);
}

QSize PillToggle::minimumSizeHint() const
{
    return sizeHint();
}

void PillToggle::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    const bool isFluent = fluentMode();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int pad = UiStyle::SwitchPad;
    // v2 mini switch：轨道 30×16 居中放在 38×24 命中区里（四面留 2px 环位）；
    // 非 v2 页面维持原 40×20。
    const int trackW = isFluent ? 30 : UiStyle::SwitchTrackW;
    const int trackH = isFluent ? 16 : UiStyle::SwitchTrackH;
    const QRectF track = isFluent
        ? QRectF((width() - trackW) / 2.0, (height() - trackH) / 2.0, trackW, trackH)
        : QRectF(pad, pad, trackW, trackH);
    const qreal pos = qBound(0.0, m_pos, 1.0);
    const bool disabled = !isEnabled();

    // v2 母版：开启轨道 #32D66B 绿色，关闭轨道中性灰 #65676C/#797C81，
    // 滑块恒为近白色；状态由滑块位置 + 填色表达，禁止蓝色开关。
    // 非样板走原主题令牌，其他十个设置页不受影响。
    const QColor fluentOff = UiStyle::fluent::switchOffColor();
    const QColor fluentOffHover = mixColor(fluentOff, UiStyle::fluent::textColor(), 0.14);
    const QColor offBorder = isFluent
        ? (m_hover ? fluentOffHover : fluentOff)
        : (m_hover ? UiStyle::muted() : UiStyle::lineStrong());
    const QColor onColor = isFluent ? UiStyle::fluent::switchOnColor() : UiStyle::accent();
    const QColor offFill = isFluent ? UiStyle::fluent::switchOffColor() : UiStyle::field();
    QColor fill = mixColor(offFill, onColor, pos);
    QColor border = mixColor(disabled && pos <= 0.0
                                 ? (isFluent ? UiStyle::fluent::switchOffColor() : UiStyle::line())
                                 : offBorder,
                             onColor, pos);

    // 禁用但仍勾选（跟随系统时显示系统当前模式）必须能看出「开」：
    // 整体降透明度而不是换成灰色轨道，否则滑块位置对了、颜色却像关。
    if (disabled)
        p.setOpacity(0.52);
    else if (m_pressed)
        p.setOpacity(0.88);

    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    const qreal trackRadius = isFluent ? trackH / 2.0 : UiStyle::SwitchRadius;
    p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5),
                      trackRadius, trackRadius);

    const int knob = isFluent ? 12 : UiStyle::SwitchKnob;
    const int inset = isFluent ? 2 : UiStyle::SwitchInset;
    const qreal knobY = track.center().y() - knob / 2.0;
    const qreal knobLeft = track.left() + inset;
    const qreal knobRight = track.right() - inset - knob;
    qreal knobX = knobLeft + (knobRight - knobLeft) * pos;
    if (isFluent && m_pressed && !disabled)
        knobX += (pos >= 0.5 ? -0.35 : 0.35);

    p.setPen(Qt::NoPen);
    const QColor knobOff = isFluent ? UiStyle::fluent::knobColor() : UiStyle::muted();
    const QColor knobOn = isFluent ? UiStyle::fluent::knobColor() : UiStyle::panel();
    p.setBrush(mixColor(knobOff, knobOn, pos));
    p.drawEllipse(QRectF(knobX, knobY, knob, knob));

    if (disabled || m_pressed)
        p.setOpacity(1.0);

    // 浅色主题开启轨道加 1px 内描边 #218941，增强白色卡片上的边界。
    if (isFluent && UiStyle::isLight() && pos > 0.5) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(UiStyle::fluent::switchEdgeColor(), 1));
        p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5),
                          trackRadius, trackRadius);
    }

    // 焦点环：只在「键盘导航 + 当前持有焦点」时出现。鼠标点击后
    // SettingsDialog 的输入来源追踪会把 kbFocus 置 false，环立即消失；
    // 未使用 v2 的旧控件保持原行为。
    const bool ringVisible = isFluent
        ? (hasFocus() && property("kbFocus").toBool())
        : hasFocus();
    if (ringVisible) {
        p.setBrush(Qt::NoBrush);
        const QColor ring = isFluent ? UiStyle::fluent::accentColor() : UiStyle::accent();
        p.setPen(QPen(ring, 2));
        // 环围绕轨道外扩 2px；38×24 命中区里再外留 2px 到控件边缘。
        const QRectF focusRect = isFluent
            ? track.adjusted(-2, -2, 2, 2)
            : QRectF(1, 1, width() - 2, height() - 2);
        const qreal focusRadius = isFluent ? focusRect.height() / 2.0
                                           : UiStyle::SwitchRadius + 1;
        p.drawRoundedRect(focusRect, focusRadius, focusRadius);
    }
}

void PillToggle::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QCheckBox::enterEvent(event);
}

void PillToggle::leaveEvent(QEvent *event)
{
    m_hover = false;
    m_pressed = false;
    update();
    QCheckBox::leaveEvent(event);
}

void PillToggle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressed = true;
        update();
    }
    QCheckBox::mousePressEvent(event);
}

void PillToggle::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressed = false;
    update();
    QCheckBox::mouseReleaseEvent(event);
}

bool PillToggle::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}
