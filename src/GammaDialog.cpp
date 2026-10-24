#include "GammaDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
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
    setModal(true);
    setStyleSheet(UiStyle::dialogQss());
    setMinimumWidth(360);

    m_value = qBound(0.10, value, 3.00);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 16);
    root->setSpacing(14);

    auto *hint = new QLabel(tr("往右变亮、往左变暗，1.00 为原始亮度。"), this);
    hint->setStyleSheet(QStringLiteral("color:#9aa0a6;font-size:12px;"));
    root->addWidget(hint);

    auto *row = new QHBoxLayout;
    row->setSpacing(12);
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(kSliderMin, kSliderMax);
    m_slider->setSingleStep(1);
    m_slider->setPageStep(10);
    m_slider->setValue(qRound(m_value * 100.0));
    row->addWidget(m_slider, 1);

    m_valueLabel = new QLabel(this);
    m_valueLabel->setFixedWidth(52);
    m_valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_valueLabel->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
    m_valueLabel->setText(QString::number(m_value, 'f', 2));
    row->addWidget(m_valueLabel);
    root->addLayout(row);

    auto *buttons = new QDialogButtonBox(this);
    auto *resetBtn = buttons->addButton(tr("重置为 1.00"), QDialogButtonBox::ResetRole);
    buttons->addButton(QDialogButtonBox::Ok);
    buttons->addButton(QDialogButtonBox::Cancel);
    for (auto *b : buttons->buttons()) {
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(88);
    }
    root->addWidget(buttons);

    connect(m_slider, &QSlider::valueChanged, this, &GammaDialog::applySlider);
    connect(resetBtn, &QPushButton::clicked, this, &GammaDialog::reset);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
