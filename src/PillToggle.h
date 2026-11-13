#pragma once

#include <QCheckBox>

class QVariantAnimation;

// 胶囊拨动开关：外观页主题区/材质区专用。
// 复用 QCheckBox 的 checked / toggled / 键盘空格与可访问性语义，
// 只把指示器换成「轨道 + 圆形滑块」自绘；不带文字（旁边由 QLabel 排版）。
// 设置页 Fluent 模式使用 30×16 轨道与 12×12 滑块；旧模式仍读 UiStyle 令牌。
class PillToggle : public QCheckBox
{
public:
    explicit PillToggle(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // 设置页 v2 母版专用：mini switch 轨道 30×16、滑块 12、命中区 38×24
    // （轨道 + 2px 焦点环 + 2px 外距）。
    void setFluent(bool on);
    bool fluent() const { return m_fluent; }

    // 滑块从当前位置滑到新位置的时长（毫秒）。0 表示不做过渡。
    static int motionDurationMs() { return 120; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 自绘后没有原生指示器，QCheckBox 默认的命中区可能只剩空文字区；
    // 整个 44×24 轨道都应可点。
    bool hitButton(const QPoint &pos) const override;
    // 用户操作（点击 / 空格）拨动时走短过渡；程序改值（同步主题状态）直接落位。
    void nextCheckState() override;
    void checkStateSet() override;

private:
    void startMotion(bool on);
    // 样板模式也可由动态属性 fluent="general" 触发（PillToggle 无 Q_OBJECT，
    // 调用方用 findChildren<QCheckBox*> + setProperty，不用 qobject_cast）。
    bool fluentMode() const;

    bool m_hover = false;
    bool m_pressed = false;
    bool m_userToggle = false;
    bool m_fluent = false;
    qreal m_pos = 0.0;   // 0 = 关，1 = 开；过渡中为当前插值位置
    QVariantAnimation *m_motion = nullptr;
};
