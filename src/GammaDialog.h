#pragma once

#include <QDialog>

class QLabel;
class QSlider;

// Gamma 补偿对话框（原版 MENUSTR_IMAGE_PROCESSING_GAMMA，Ctrl+G）。
// 0.10~3.00 滑块，拖动即实时预览；取消则调用方回滚到进入前的值。
class GammaDialog : public QDialog
{
    Q_OBJECT
public:
    explicit GammaDialog(qreal value, QWidget *parent = nullptr);

    qreal value() const { return m_value; }

signals:
    void valueChanged(qreal gamma);

private:
    void applySlider(int sliderValue);
    void reset();

    QSlider *m_slider = nullptr;
    QLabel *m_valueLabel = nullptr;
    qreal m_value = 1.0;
};
