#include "PillToggle.h"

#include "UiStyle.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>

PillToggle::PillToggle(QWidget *parent)
    : QCheckBox(parent)
{
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
    const bool on = isChecked();
    const bool disabled = !isEnabled();

    QColor fill = on ? UiStyle::accent() : UiStyle::field();
    QColor border = on ? UiStyle::accent()
                       : (m_hover ? UiStyle::muted() : UiStyle::lineStrong());
    if (disabled && !on)
        border = UiStyle::line();

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
    const qreal knobX = on
        ? track.right() - UiStyle::SwitchInset - knob
        : track.left() + UiStyle::SwitchInset;

    p.setPen(Qt::NoPen);
    p.setBrush(on ? UiStyle::panel() : UiStyle::muted());
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
