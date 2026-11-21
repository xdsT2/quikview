#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

class QTimer;

// 球形渲染画布：等距柱状图 → 透视视图（派单 2026-11-20「浏览体验」）。
// ---------------------------------------------------------------------------
// 拖动 = 转视角（yaw/pitch），滚轮 = 调整垂直视场角，双击或 R = 重置视角。
// 交互期用半分辨率渲染保跟手，停手 120ms 后补一帧全分辨率；
// 全部在 GUI 线程内完成，不额外起解码线程（原图只在打开时读一次）。
class PanoGlobe : public QWidget
{
    Q_OBJECT
public:
    explicit PanoGlobe(QWidget *parent = nullptr);

    bool load(const QString &path, QString *error);
    bool hasImage() const { return !m_src.isNull(); }
    QSize sourceSize() const { return m_src.size(); }

    // 初始视角（度）：heading 0 = 等距柱状图水平正中，pitch 正数向上。
    void setInitialView(double heading, double pitch, double fov);
    void resetView();

    double heading() const { return m_heading; }
    double pitch() const { return m_pitch; }
    double fov() const { return m_fov; }

signals:
    // 视角被用户改动（拖动结束 / 滚轮 / 重置）。只在交互落点发，不在每帧发，
    // 避免宿主每帧重排文字。
    void viewChanged();

public:
    // 验收用（--bench）：连续渲染 frames 帧，返回平均毫秒。scale=1 全分辨率，
    // 0.5 交互帧。只测渲染管线，不含窗口合成/上屏。
    double benchFrameMs(int frames, double scale);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void renderFrame(double scale);
    void scheduleFullQuality();
    void clampPitch();

private:
    QImage m_src;                 // 源图（RGB32）
    QImage m_frame;               // 当前渲染结果
    double m_frameScale = 1.0;    // 渲染分辨率比例（1.0 = 每屏幕像素一个采样）
    double m_heading = 0.0;
    double m_pitch = 0.0;
    double m_fov = 75.0;
    double m_homeHeading = 0.0;
    double m_homePitch = 0.0;
    double m_homeFov = 75.0;
    bool m_dragging = false;
    QPoint m_dragFrom;
    double m_dragHeading = 0.0;
    double m_dragPitch = 0.0;
    QTimer *m_settleTimer = nullptr;
};
