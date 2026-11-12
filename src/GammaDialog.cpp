#include "GammaDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include "UiStyle.h"

namespace {

// 滑块用整数，值域 10..300 对应 0.10..3.00，一格 0.01
constexpr int kSliderMin = 10;
constexpr int kSliderMax = 300;

} // namespace

GammaDialog::GammaDialog(qreal value, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Gamma 补偿"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 骨架与批量转换同构（dlgTitleBar / dlgFooter / btnPrimary）；
    // 滑杆行内 QSS 同批量转换的品质滑杆，浅色 appQss 无 QSlider 兜底，
    // 颜色按主题取令牌，不自造色值。
    setStyleSheet(UiStyle::dialogQss() + QStringLiteral(R"(
QDialog { background: transparent; }
QWidget#dlgTitleBar { border-top-left-radius: 10px; border-top-right-radius: 10px; }
QWidget#dlgFooter { border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }
QLabel#gammaValue { font-size: 14px; font-weight: 600; }
QSlider#gammaSlider { min-width: 220px; }
QSlider#gammaSlider::groove:horizontal { height: 3px; background: %1; border-radius: 3px; }
QSlider#gammaSlider::sub-page:horizontal { background: %2; border-radius: 3px; }
QSlider#gammaSlider::handle:horizontal { width: 11px; height: 11px; margin: -4px 0;
    border-radius: 6px; background: %3; }
)")
        .arg(UiStyle::pick("#5d6166", "#c9ccd1"))
        .arg(UiStyle::pick("#55c0f0", "#1689bd"))
        .arg(UiStyle::pick("#f2f4f5", "#ffffff")));
    setFont(UiStyle::font(12));
    setMinimumWidth(380);

    m_value = qBound(0.10, value, 3.00);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— 标题栏 43px（与批量转换同构）——
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    titleBar->setFixedHeight(UiStyle::TitleBarH);
    titleBar->installEventFilter(this);
    auto *tb = new QHBoxLayout(titleBar);
    tb->setContentsMargins(18, 0, 14, 0);
    tb->setSpacing(9);
    auto *title = new QLabel(tr("Gamma 补偿"), titleBar);
    title->setObjectName(QStringLiteral("dlgTitle"));
    tb->addWidget(title);
    tb->addStretch(1);
    auto *closeBtn = new QPushButton(QStringLiteral("\u2715"), titleBar);
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setFixedSize(32, 30);
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
    tb->addWidget(closeBtn);
    root->addWidget(titleBar);

    // —— 紧凑调节面板：说明一行，数值与滑杆同行同基线 ——
    auto *body = new QWidget(this);
    auto *bl = new QVBoxLayout(body);
    bl->setContentsMargins(20, 14, 20, 16);
    bl->setSpacing(12);
    auto *hint = new QLabel(tr("往右变亮、往左变暗，1.00 为原始亮度。"), body);
    hint->setObjectName(QStringLiteral("hint"));
    bl->addWidget(hint);

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    m_slider = new QSlider(Qt::Horizontal, body);
    m_slider->setObjectName(QStringLiteral("gammaSlider"));
    m_slider->setRange(kSliderMin, kSliderMax);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(10);
    m_slider->setValue(qRound(m_value * 100.0));
    row->addWidget(m_slider, 1);

    m_valueLabel = new QLabel(body);
    m_valueLabel->setObjectName(QStringLiteral("gammaValue"));
    m_valueLabel->setFixedWidth(52);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setText(QString::number(m_value, 'f', 2));
    row->addWidget(m_valueLabel);
    bl->addLayout(row);
    root->addWidget(body, 1);

    // —— 底栏：重置为次要动作，确定为主按钮 ——
    auto *footer = new QWidget(this);
    footer->setObjectName(QStringLiteral("dlgFooter"));
    footer->setFixedHeight(UiStyle::FooterH);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(18, 0, 18, 0);
    fl->setSpacing(9);
    auto *resetBtn = new QPushButton(tr("重置为 1.00"), footer);
    resetBtn->setObjectName(QStringLiteral("btnSubtle"));
    resetBtn->setFixedHeight(32);
    resetBtn->setCursor(Qt::PointingHandCursor);
    connect(resetBtn, &QPushButton::clicked, this, &GammaDialog::reset);
    fl->addWidget(resetBtn);
    fl->addStretch(1);
    auto *cancelBtn = new QPushButton(tr("取消"), footer);
    cancelBtn->setFixedHeight(32);
    cancelBtn->setCursor(Qt::PointingHandCursor);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    fl->addWidget(cancelBtn);
    auto *okBtn = new QPushButton(tr("确定"), footer);
    okBtn->setObjectName(QStringLiteral("btnPrimary"));
    okBtn->setFixedHeight(32);
    okBtn->setDefault(true);
    okBtn->setCursor(Qt::PointingHandCursor);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    fl->addWidget(okBtn);
    root->addWidget(footer);

    connect(m_slider, &QSlider::valueChanged, this, &GammaDialog::applySlider);
    m_slider->setFocus();

    adjustSize();
    if (parent && parent->isVisible()) {
        const QRect pg = parent->frameGeometry();
        move(pg.center() - rect().center());
    }
}

void GammaDialog::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(UiStyle::color(UiStyle::Border), 1));
    p.setBrush(UiStyle::color(UiStyle::Panel));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                      UiStyle::RadiusDialog, UiStyle::RadiusDialog);
}

bool GammaDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched && watched->objectName() == QLatin1String("dlgTitleBar")) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->buttons() & Qt::LeftButton) {
                move(me->globalPosition().toPoint() - m_dragOffset);
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void GammaDialog::applySlider(int sliderValue)
{
    m_value = sliderValue / 100.0;
    m_valueLabel->setText(QString::number(m_value, 'f', 2));
    emit valueChanged(m_value);
}

void GammaDialog::reset()
{
    m_slider->setValue(100);   // 会经 applySlider 触发一次预览
}
