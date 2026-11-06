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

QSize PillToggle::sizeHint() const
{
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

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int pad = UiStyle::SwitchPad;
    const QRectF track(pad, pad, UiStyle::SwitchTrackW, UiStyle::SwitchTrackH);
    const qreal pos = qBound(0.0, m_pos, 1.0);
    const bool disabled = !isEnabled();

    const QColor offBorder = m_hover ? UiStyle::muted() : UiStyle::lineStrong();
    QColor fill = mixColor(UiStyle::field(), UiStyle::accent(), pos);
    QColor border = mixColor(disabled && pos <= 0.0 ? UiStyle::line() : offBorder,
                             UiStyle::accent(), pos);

    // 禁用但仍勾选（跟随系统时显示系统当前模式）必须能看出「开」：
    // 整体降透明度而不是换成灰色轨道，否则滑块位置对了、颜色却像关。
    if (disabled)
        p.setOpacity(0.55);

    p.setPen(QPen(border, 1));
    p.setBrush(fill);
    p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5),
                      UiStyle::SwitchRadius, UiStyle::SwitchRadius);

    int knob = UiStyle::SwitchKnob;
    if (m_pressed && !disabled)
        knob -= 1;
    const qreal knobY = track.center().y() - knob / 2.0;
    const qreal knobLeft = track.left() + UiStyle::SwitchInset;
    const qreal knobRight = track.right() - UiStyle::SwitchInset - knob;
    const qreal knobX = knobLeft + (knobRight - knobLeft) * pos;

    p.setPen(Qt::NoPen);
    p.setBrush(mixColor(UiStyle::muted(), UiStyle::panel(), pos));
    p.drawEllipse(QRectF(knobX, knobY, knob, knob));

    if (disabled)
        p.setOpacity(1.0);

    if (hasFocus()) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(UiStyle::accent(), 2));
        p.drawRoundedRect(QRectF(1, 1, width() - 2, height() - 2),
                          UiStyle::SwitchRadius + 1, UiStyle::SwitchRadius + 1);
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
