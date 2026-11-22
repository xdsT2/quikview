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
    : UiShell::DialogShell(parent)
{
    setWindowTitle(tr("Gamma 补偿"));
    // 骨架与批量转换同构（dlgTitleBar / dlgFooter / btnPrimary），外壳交给 DialogShell；
    // 2026-11-22 第 4 批：滑杆色改为 @Gen*@ 令牌，浅色主题不再留深色块。
    setStyleSheet(UiShell::DialogShell::shellQss() + QStringLiteral(R"(
QLabel#gammaValue { font-size: 14px; font-weight: 600; color: @GenText@; }
QSlider#gammaSlider { min-width: 220px; }
QSlider#gammaSlider::groove:horizontal { height: 3px; background: @GenCtrlLine@;
    border-radius: 3px; }
QSlider#gammaSlider::sub-page:horizontal { background: @GenNavSel@; border-radius: 3px; }
QSlider#gammaSlider::handle:horizontal { width: 11px; height: 11px; margin: -4px 0;
    border-radius: 6px; background: @GenKnob@; }
)"));

    setMinimumWidth(380);
    m_value = qBound(0.10, value, 3.00);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // —— 标题栏 43px（与批量转换同构）——
    auto *titleBar = new QWidget(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    titleBar->setFixedHeight(UiStyle::TitleBarH);
    attachTitleBar(titleBar);
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
    cancelBtn->setObjectName(QStringLiteral("genNeutral"));
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

// 无边框窗口（paintEvent / 标题栏拖拽）已上移到 UiShell::DialogShell。

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
