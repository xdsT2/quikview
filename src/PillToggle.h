#pragma once

#include <QCheckBox>

// 胶囊拨动开关：外观页主题区/材质区专用。
// 复用 QCheckBox 的 checked / toggled / 键盘空格与可访问性语义，
// 只把指示器换成「轨道 + 圆形滑块」自绘；不带文字（旁边由 QLabel 排版）。
// 尺寸与配色全部取 UiStyle.h 令牌（SwitchTrackW/H、SwitchRadius、SwitchKnob）。
class PillToggle : public QCheckBox
{
public:
    explicit PillToggle(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 自绘后没有原生指示器，QCheckBox 默认的命中区可能只剩空文字区；
    // 整个 44×24 轨道都应可点。
    bool hitButton(const QPoint &pos) const override;

private:
    bool m_hover = false;
    bool m_pressed = false;
};
