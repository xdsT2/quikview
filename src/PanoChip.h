#pragma once

#include <QWidget>

// 主窗「360° 查看」入口（派单 2026-11-20「识别结果与入口」）。
// ---------------------------------------------------------------------------
// 只对「完整球形 + 未明确关闭提示」的图片出现；位置在底部控制岛上沿居中，
// 与胶囊同一条中轴，不遮挡图片主体。
// 刻意设成 Qt::NoFocus：它是一张图的附加入口，不能抢走画布的键盘焦点，
// 翻转/缩放/翻页快捷键必须保持原样可达（派单「不抢焦点」）。
class PanoChip : public QWidget
{
    Q_OBJECT
public:
    explicit PanoChip(QWidget *parent = nullptr);

    void setActive(bool active);  // false = 隐藏（当前图不是完整球形图）
    QSize sizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    bool m_active = false;
    bool m_hover = false;
    bool m_pressed = false;
};
