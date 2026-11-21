#include "PanoChip.h"

#include "UiStyle.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

namespace {

constexpr int kHeight = 30;
constexpr int kPadX = 14;
const char *kLabel = "360° 查看";

} // namespace

PanoChip::PanoChip(QWidget *parent)
    : QWidget(parent)
{
    // 不抢焦点：画布保持键盘焦点，←/→、+/-、Tab 等快捷键不受影响。
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setAttribute(Qt::WA_Hover, true);
    setToolTip(tr("用 360° 全景查看器打开这张球形图（可选组件）"));
    setAccessibleName(tr("360° 查看"));
    hide();
}

QSize PanoChip::sizeHint() const
{
    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    const QFontMetrics fm(f);
    return QSize(fm.horizontalAdvance(QString::fromUtf8(kLabel)) + kPadX * 2, kHeight);
}

void PanoChip::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (active) {
        setFixedSize(sizeHint());
        show();
        raise();
    } else {
        m_hover = false;
        m_pressed = false;
        hide();
    }
}

void PanoChip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal r = height() / 2.0;
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);

    // 与底岛同一族表面色：常态 0.9 黑、悬停提亮、按下回落，不发光不加阴影。
    QColor bg(QLatin1String(UiStyle::Panel));
    bg.setAlpha(m_pressed ? 240 : (m_hover ? 235 : 216));
    p.setBrush(bg);
    p.setPen(QPen(QColor(255, 255, 255, m_hover ? 60 : 40), 1));
    p.drawRoundedRect(box, r, r);

    QFont f = font();
    f.setPixelSize(12);
    f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(QColor(QLatin1String(m_pressed ? UiStyle::Body : UiStyle::Text)));
    p.drawText(rect(), Qt::AlignCenter, QString::fromUtf8(kLabel));
}

void PanoChip::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_pressed = true;
    update();
    event->accept();
}

void PanoChip::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    const bool wasPressed = m_pressed;
    m_pressed = false;
    update();
    // 按下后移出再松开按取消处理，与胶囊/按钮的口径一致。
    if (wasPressed && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    event->accept();
}

void PanoChip::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void PanoChip::leaveEvent(QEvent *event)
{
    m_hover = false;
    m_pressed = false;
    update();
    QWidget::leaveEvent(event);
}
