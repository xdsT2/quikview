#pragma once

#include <QGraphicsView>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QLabel;
class QWheelEvent;
class QMouseEvent;

// 图片画布：缩放 + 平移 + 适配窗口 + 裁剪选区。M0 用 QGraphicsView 实现；
// 后续 M1 接自绘高性能渲染（自定义 QWidget + 分块纹理）再替换。
class ImageCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    void setPixmap(const QPixmap &pixmap);
    void clear();
    void setSmooth(bool on);
    void setWheelZoomEnabled(bool on) { m_wheelZoom = on; }
    void setMiddlePanEnabled(bool on) { m_middlePan = on; }
    void fitToWindow();
    void fitToWidth();                    // 浏览模式 2：按图宽缩放，垂直可滚
    void zoomIn();
    void zoomOut();
    void zoomActual();
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    bool isFitToWindow() const { return m_fitToWindow; }
    bool isFitToWidth() const { return m_fitWidth; }

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
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
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
    qreal m_zoom = 1.0;
};
