#pragma once

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

#include <vector>

class QTimer;

// 球形渲染画布：等距柱状图 → 透视视图（派单 2026-11-20「浏览体验」）。
// ---------------------------------------------------------------------------
// 拖动 = 转视角（yaw/pitch），滚轮 = 调整垂直视场角，双击或 R = 重置视角。
// 交互期用半分辨率渲染保跟手，停手 120ms 后补一帧全分辨率；
// 全部在 GUI 线程内完成，不额外起解码线程（原图只在打开时读一次）。
//
// 2026-11-27 起支持 HDR（Radiance RGBE）/ EXR（scanline ZIP/ZIPS）：
// 解码产物是驻留的线性浮点纹理（RGB float），投影采样 → 曝光 ×2^EV →
// Reinhard → sRGB 编码 才逐像素发生；JPEG/PNG 仍走既有 8bit QImage 路径，
// 不经过曝光/色调映射，行为不变。
class PanoGlobe : public QWidget
{
    Q_OBJECT
public:
    explicit PanoGlobe(QWidget *parent = nullptr);

    // 读取图片。error 带回可读文案；errorCategory 带回大类键
    // （Corrupt / Unsupported / Io，供验收日志区分「损坏」与「格式不支持」）。
    bool load(const QString &path, QString *error, QString *errorCategory = nullptr);
    bool hasImage() const { return !m_src.isNull() || !m_tex.empty(); }
    QSize sourceSize() const;
    // 源文件原生宽高（HDR/EXR 未降采样前的真实尺寸；SDR 同 sourceSize）。
    QSize nativeSourceSize() const { return QSize(m_nativeW, m_nativeH); }
    // 是否为线性浮点 HDR/EXR 纹理（false = 既有 8bit SDR 图）。
    bool isHdr() const { return m_isFloat; }

    // 初始视角（度）：heading 0 = 等距柱状图水平正中，pitch 正数向上。
    void setInitialView(double heading, double pitch, double fov);
    void resetView();

    double heading() const { return m_heading; }
    double pitch() const { return m_pitch; }
    double fov() const { return m_fov; }

    // 曝光补偿（EV 档，夹在 [-5, +5]）。只作用于 HDR/EXR 浮点路径；
    // SDR 路径忽略曝光（避免对已编码的 sRGB 二次映射）。
    double ev() const { return m_ev; }
    void setEv(double ev);
    void stepEv(double delta);
    void resetEv();

    // 验收探针：取屏幕中心视线在当前视角下采样到的线性 RGB（曝光前），
    // 以及当前 EV 下经 Reinhard + sRGB 编码后的 8bit 值。无浮点纹理返回 false。
    bool probeCenterPixel(float lin[3], int srgb8[3]) const;

signals:
    // 视角被用户改动（拖动结束 / 滚轮 / 重置）。只在交互落点发，不在每帧发，
    // 避免宿主每帧重排文字。
    void viewChanged();
    // 曝光被用户改动（+/- / 0 复位 / Shift+滚轮）。
    void evChanged();

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
    // 视线（相机空间 nx/ny，+z 朝屏幕正中）→ 等距柱状 UV（u∈[0,1] 横向，v∈[0,1] 纵向）。
    void projectRay(double nx, double ny, double cosP, double sinP,
                    double cosY, double sinY, double &u, double &v) const;
    // 从浮点纹理双线性取样（横向环绕、纵向夹极）；仅 m_isFloat 时可用。
    void sampleTex(double u, double v, float rgb[3]) const;

private:
    QImage m_src;                 // SDR 源图（RGB32，既有路径）
    std::vector<float> m_tex;     // HDR/EXR 线性浮点纹理（RGB，行优先）
    int m_texW = 0;
    int m_texH = 0;
    int m_nativeW = 0;
    int m_nativeH = 0;
    bool m_isFloat = false;
    QImage m_frame;               // 当前渲染结果
    double m_frameScale = 1.0;    // 渲染分辨率比例（1.0 = 每屏幕像素一个采样）
    double m_heading = 0.0;
    double m_pitch = 0.0;
    double m_fov = 75.0;
    double m_homeHeading = 0.0;
    double m_homePitch = 0.0;
    double m_homeFov = 75.0;
    double m_ev = 0.0;
    bool m_dragging = false;
    QPoint m_dragFrom;
    double m_dragHeading = 0.0;
    double m_dragPitch = 0.0;
    QTimer *m_settleTimer = nullptr;
};
