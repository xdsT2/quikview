#pragma once

#include <QElapsedTimer>
#include <QGraphicsView>
#include <QMargins>
#include <QTimer>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QLabel;
class QWheelEvent;
class QMouseEvent;
class QKeyEvent;

// 图片画布：缩放 + 平移 + 适配窗口 + 裁剪选区。M0 用 QGraphicsView 实现；
// 后续 M1 接自绘高性能渲染（自定义 QWidget + 分块纹理）再替换。
class ImageCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // fitAfter=false 时不做适配，只装图（调用方随后自行 fit/setViewMode），
    // 用于消除「setPixmap 内部 fit 一次 + setViewMode 又 fit 一次」的双重计算。
    void setPixmap(const QPixmap &pixmap, bool fitAfter = true);
    // 旋转/翻转等原地替换：保持当前缩放与视口中心（anchorNorm 为新图中要占据
    // 视口中心的归一化坐标），避免转完后图像跳位或被重新适配。
    void setPixmapPreservingView(const QPixmap &pixmap, const QPointF &anchorNorm,
                                 qreal zoom, bool fitWindow, bool fitWidth);
    // 视口中心落在当前图片内的归一化坐标（0..1），无图时返回 (0.5, 0.5)
    QPointF viewCenterNormalized() const;
    void clear();
    void setSmooth(bool on);
    // 插值算法："bilinear"(0) / "bicubic"(1) / "lanczos"(2)。bicubic 走 GPU 平滑，
    // lanczos 在缩小显示时按目标尺寸做一次高质量重采样（原版标注「慢，高品质」）。
    enum InterpMode { InterpBilinear = 0, InterpBicubic = 1, InterpLanczos = 2 };
    void setInterpolationMode(int mode);
    int interpolationMode() const { return m_interp; }
    // 原版「拉伸小图像」：关（默认）时小于窗口的图保持 100% 不放大
    void setStretchSmall(bool on);
    bool stretchSmall() const { return m_stretchSmall; }
    void setWheelZoomEnabled(bool on) { m_wheelZoom = on; }
    void setMiddlePanEnabled(bool on) { m_middlePan = on; }
    void setContentMargins(const QMargins &margins);
    void fitToWindow();
    void fitToWidth();                    // 浏览模式 2：按图宽缩放，垂直可滚
    void zoomIn();
    void zoomOut();
    void zoomActual();
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    // 以 anchorViewportPt（视口坐标）为锚点应用缩放，进入手动模式且不触发 Fit。
    // 滚轮用鼠标位置，按钮/键盘/滑杆用视口中心。
    void applyZoomAt(qreal zoom, const QPoint &anchorViewportPt);
    bool isFitToWindow() const { return m_fitToWindow; }
    bool isFitToWidth() const { return m_fitWidth; }

    // ↑/↓（原版「上移/下移」）：按视口高度比例滚动，边界由滚动条自己钳制。
    // 画面小于视口（适应窗口）时没有可滚动范围，等于不动。
    void scrollVerticallyBy(qreal fraction);

    // 裁剪模式：进入后左键拖拽画选区，selectionRect() 返回图像像素坐标
    void setCropMode(bool on);
    bool cropMode() const { return m_cropMode; }
    QRect selectionRect() const;
    void clearSelection();

    // 空态风车 logo（原版画布中央灰色 4 菱形）
    void setLogoVisible(bool on);

signals:
    // 任何缩放来源（滚轮/按钮/适配模式）变更后发出，供控制栏滑条与浮标联动
    void zoomChanged(qreal zoom);

protected:
    void wheelEvent(QWheelEvent *event) override;
    // 画布不消费导航键：QGraphicsView/QAbstractScrollArea 默认会把方向键吃掉做滚动，
    // 事件就传不到 MainWindow::keyPressEvent。这里统一 ignore() 冒泡。
    // 注意不能用 setFocusPolicy(NoFocus) 一刀切——裁剪模式还要靠画布收 Esc。
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // 把场景点重新对回指定的视口点（用于边距/缩放变化后还原缩放锚点）
    void keepScenePointAt(const QPointF &scenePt, const QPoint &viewportPt);
    // Lanczos：缩小显示时按目标尺寸重采样一次，用 devicePixelRatio 保持逻辑尺寸
    // 不变（scene 几何、fit/center/归一化锚点全都不受影响），只提高采样质量。
    // refreshSampling() 带交互态闸门：拖窗口/连续缩放时只记账不计算，
    // 停止 150ms 后由 m_sampleFlush 调 doRefreshSampling() 补一次。
    void refreshSampling();
    void doRefreshSampling();
    // fit/fitWidth 的纯几何部分（resetTransform+scale+flags+centerOn+emit），
    // 与 fitToWindow()/fitToWidth() 共用；resizeEvent 拖动期只调这里不算采样。
    void fitToWindowGeom();
    void fitToWidthGeom();

    QGraphicsPixmapItem *m_item = nullptr;
    QGraphicsRectItem *m_rubber = nullptr;
    QLabel *m_logo = nullptr;
    QPoint m_lastPan;
    QPointF m_origin;
    bool m_panning = false;
    bool m_selecting = false;
    bool m_fitToWindow = true;
    bool m_fitWidth = false;
    bool m_cropMode = false;
    bool m_smooth = true;
    bool m_wheelZoom = true;
    bool m_middlePan = true;
    bool m_stretchSmall = false;
    bool m_resizeBurst = false;  // 窗口边界拖动进行中（resizeEvent 置位，收尾定时器复位）
    int m_interp = InterpBicubic;
    QPixmap m_sourcePixmap;      // 未经 Lanczos 预处理的原图，缩放变化时按它重算
    QSize m_samplingSize;        // 上次 Lanczos 重采样的目标像素尺寸（相同就跳过重算）
    qreal m_zoom = 1.0;
    // 交互收尾定时器：拖动/连续缩放停止 150ms 后补做一次重采样（单发）
    QTimer m_sampleFlush;
    QElapsedTimer m_lastSampleReq;   // 上一次 refreshSampling 请求时刻（判连续交互）
};
