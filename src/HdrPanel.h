#pragma once

#include <QWidget>

class QSlider;

// 顶栏 HDR 浮层（需求池 H1-8）：三档显示模式 + 曝光 EV。
//
// 三档只决定「请求什么输出」，不是三种画面算法：本版没有原生 HDR 输出路径
// （Qt6 Windows 后端只出 8bit SDR），所以三种模式最终都走 I3-2 的 SDR 预览，
// 面板底部把「选了什么 / 实际生效什么 / 为什么」写清楚，不含糊其辞。
//
// 交互沿用现有浮层习惯：Qt::Popup（点外面自动关）、Esc 关闭、尺寸固定不挤压画布。
class HdrPanel : public QWidget
{
    Q_OBJECT
public:
    explicit HdrPanel(QWidget *parent = nullptr);

    // mode：0 自动检测 / 1 开启 / 2 关闭。hdrImage：当前图是否 HDR/EXR（决定滑块可用）。
    void setState(int mode, double ev, bool hdrImage);
    int mode() const { return m_mode; }
    double ev() const { return m_ev; }

signals:
    void modePicked(int mode);
    void evPicked(double ev);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QRect rowRect(int index) const;
    int rowAt(const QPoint &pos) const;
    QString statusText() const;

    int m_mode = 0;
    double m_ev = 0.0;
    bool m_hdrImage = false;
    QSlider *m_slider = nullptr;
};
