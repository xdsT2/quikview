#include "HdrPanel.h"

#include "UiStyle.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>

namespace {

constexpr int kPanelW = 300;
constexpr int kPanelH = 198;
constexpr int kPad = 16;
constexpr int kRowH = 30;
constexpr int kRowsTop = 12;

QFont panelFont(int px, int weight = QFont::Normal)
{
    QFont f = QApplication::font();
    f.setPixelSize(px);
    f.setWeight(QFont::Weight(weight));
    return f;
}

QString evText(double ev)
{
    if (qFuzzyIsNull(ev)) {
        return QStringLiteral("0 EV");
    }
    return QStringLiteral("%1%2 EV")
        .arg(ev > 0 ? QStringLiteral("+") : QStringLiteral("-"))
        .arg(std::abs(ev), 0, 'f', 1);
}

} // namespace

HdrPanel::HdrPanel(QWidget *parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kPanelW, kPanelH);
    setFocusPolicy(Qt::StrongFocus);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(-6, 6);          // 半档步进：-3.0 ~ +3.0 EV
    m_slider->setSingleStep(1);
    m_slider->setPageStep(2);
    m_slider->setGeometry(kPad, 140, kPanelW - 2 * kPad, 22);
    m_slider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal{height:3px;background:%1;border-radius:1px;}"
        "QSlider::sub-page:horizontal{background:%2;border-radius:1px;}"
        "QSlider::handle:horizontal{width:12px;margin:-5px 0;border-radius:6px;background:%3;}")
                                .arg(QString::fromLatin1(UiStyle::pick("#c4c8cc", "#626365")),
                                     QString::fromLatin1(UiStyle::pick("#1688f8", "#61b1ff")),
                                     QString::fromLatin1(UiStyle::pick("#ffffff", "#f5f5f5"))));
    connect(m_slider, &QSlider::valueChanged, this, [this](int v) {
        m_ev = v / 2.0;
        update();
        emit evPicked(m_ev);
    });
}

void HdrPanel::setState(int mode, double ev, bool hdrImage)
{
    m_mode = qBound(0, mode, 2);
    m_ev = qBound(-3.0, ev, 3.0);
    m_hdrImage = hdrImage;
    if (m_slider) {
        const QSignalBlocker block(m_slider);
        m_slider->setValue(int(qRound(m_ev * 2.0)));
        m_slider->setEnabled(m_hdrImage);
    }
    update();
}

QRect HdrPanel::rowRect(int index) const
{
    return QRect(kPad, kRowsTop + index * kRowH, kPanelW - 2 * kPad, kRowH);
}

int HdrPanel::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < 3; ++i) {
        if (rowRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

QString HdrPanel::statusText() const
{
    if (!m_hdrImage) {
        return tr("当前图片不是 HDR／EXR，曝光不可用。");
    }
    switch (m_mode) {
    case 1:
        return tr("已回退到 SDR 预览：当前环境没有原生 HDR 输出路径。");
    case 2:
        return tr("固定使用 SDR 预览；HDR／EXR 仍按色调映射正确显示。");
    default:
        return tr("自动检测：本机未启用 HDR 显示，使用 SDR 预览。");
    }
}

void HdrPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card(0.5, 0.5, width() - 1.0, height() - 1.0);
    p.setPen(QPen(QColor(QString::fromLatin1(UiStyle::pick("#000000", "#ffffff"))), 1));
    p.setBrush(QColor(QString::fromLatin1(UiStyle::pick("#fafafb", "#1f2023"))));
    p.drawRoundedRect(card, 10, 10);

    const QColor ink(QString::fromLatin1(UiStyle::pick("#2a2c30", "#d9dadd")));
    const QColor muted(QString::fromLatin1(UiStyle::pick("#6b7075", "#8e9297")));
    const QColor accent = UiStyle::accent();

    // 三档单选行
    p.setFont(panelFont(12));
    const QStringList labels{tr("自动检测"), tr("开启"), tr("关闭")};
    for (int i = 0; i < 3; ++i) {
        const QRect r = rowRect(i);
        const bool on = (m_mode == i);
        const QPointF c(r.left() + 10, r.center().y() + 0.5);
        p.setPen(QPen(on ? accent : muted, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, 7, 7);
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(accent);
            p.drawEllipse(c, 3.6, 3.6);
        }
        p.setPen(ink);
        p.drawText(QRect(r.left() + 26, r.top(), r.width() - 26, r.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, labels.at(i));
    }

    // 分隔线
    p.setPen(QPen(QColor(QString::fromLatin1(UiStyle::pick("#00000018", "#ffffff1a"))), 1));
    p.drawLine(QPointF(kPad, 106.5), QPointF(width() - kPad, 106.5));

    // 曝光行
    p.setFont(panelFont(12));
    p.setPen(ink);
    p.drawText(QRect(kPad, 110, 120, 22), Qt::AlignVCenter | Qt::AlignLeft, tr("曝光"));
    p.setPen(m_hdrImage ? ink : muted);
    p.drawText(QRect(width() - kPad - 120, 110, 120, 22),
               Qt::AlignVCenter | Qt::AlignRight, evText(m_ev));

    // 状态说明
    p.setFont(panelFont(10));
    p.setPen(muted);
    p.drawText(QRect(kPad, 166, width() - 2 * kPad, 26),
               Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, statusText());
}

void HdrPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        const int row = rowAt(event->pos());
        if (row >= 0 && row != m_mode) {
            m_mode = row;
            update();
            emit modePicked(m_mode);
        }
    }
    QWidget::mousePressEvent(event);
}

void HdrPanel::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
