#include "ImageCanvas.h"

#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QWheelEvent>

#include <cmath>

namespace {

// 原版空态风车 logo：4 个分离的 45° 方块按对角风车排布（角度 k*90+45°，
// 方块中心半径 0.22S、边长 0.27S，对照实机采样）
QPixmap makePinwheel(int S, const QColor &color)
{
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const QPointF ctr(S / 2.0, S / 2.0);
    const double r = 0.27 * S;
    const double d = 0.26 * S;
    for (int k = 0; k < 4; ++k) {
        const double ang = qDegreesToRadians(k * 90.0 + 45.0);
        const QPointF c2 = ctr + QPointF(r * std::cos(ang), r * std::sin(ang));
        p.save();
        p.translate(c2);
        p.rotate(45);
        p.drawRect(QRectF(-d / 2, -d / 2, d, d));
        p.restore();
    }
    return pm;
}

} // namespace

ImageCanvas::ImageCanvas(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    // 原版无可见滚动条（平移走左键拖动/滚轮缩放）
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setBackgroundBrush(QColor(32, 32, 32));

    m_logo = new QLabel(viewport());
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_logo->setPixmap(makePinwheel(88, QColor("#626262")));
    m_logo->adjustSize();
    m_logo->move((viewport()->width() - m_logo->width()) / 2,
                 (viewport()->height() - m_logo->height()) / 2);
}

void ImageCanvas::setLogoVisible(bool on)
{
    if (m_logo) {
        m_logo->setVisible(on);
    }
}

void ImageCanvas::setPixmap(const QPixmap &pixmap)
{
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    // QGraphicsPixmapItem 默认 FastTransformation——painter 的
    // SmoothPixmapTransform 不覆盖 item 自身采样，缩放必须显式开平滑。
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    m_zoom = 1.0;
    resetTransform();
    fitToWindow();
    if (m_logo) {
        m_logo->hide();
    }
}

void ImageCanvas::clear()
{
    scene()->clear();
    m_item = nullptr;
    m_rubber = nullptr;
    if (m_logo) {
        m_logo->show();
    }
}

void ImageCanvas::setSmooth(bool on)
{
    m_smooth = on;
    QPainter::RenderHints hints = QPainter::Antialiasing;
    if (on) {
        hints |= QPainter::SmoothPixmapTransform;
    }
    setRenderHints(hints);
}

void ImageCanvas::setCropMode(bool on)
{
    m_cropMode = on;
    clearSelection();
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

QRect ImageCanvas::selectionRect() const
{
    if (!m_rubber) {
        return QRect();
    }
    const QRectF r = m_rubber->rect().normalized();
    return r.toAlignedRect().intersected(m_item->pixmap().rect());
}

void ImageCanvas::clearSelection()
{
    if (m_rubber) {
        scene()->removeItem(m_rubber);
        delete m_rubber;
        m_rubber = nullptr;
    }
}

void ImageCanvas::fitToWindow()
{
    if (!m_item) {
        return;
    }
    fitInView(m_item, Qt::KeepAspectRatio);
    m_fitToWindow = true;
    m_fitWidth = false;
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
}

// 浏览模式 2：宽度贴合 viewport（含滚动条边距），高度超出则顶部对齐可滚，
// 高度不足则垂直居中
void ImageCanvas::fitToWidth()
{
    if (!m_item) {
        return;
    }
    const qreal iw = m_item->pixmap().width();
    if (iw <= 0) {
        return;
    }
    resetTransform();
    // 宽度贴合要预留可能出现的垂直滚动条宽度，避免出现横向滚动条
    const int sbw = verticalScrollBar()->isVisible() || m_item->pixmap().height() > viewport()->height()
                        ? verticalScrollBar()->sizeHint().width() : 0;
    const qreal z = qMax(1.0, static_cast<qreal>(viewport()->width() - sbw)) / iw;
    scale(z, z);
    m_zoom = z;
    m_fitToWindow = false;
    m_fitWidth = true;
    if (m_item->pixmap().height() * z <= viewport()->height()) {
        centerOn(m_item);
    } else {
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
    }
    emit zoomChanged(m_zoom);
}

void ImageCanvas::zoomIn()
{
    scale(1.25, 1.25);
    m_fitToWindow = false;
    m_fitWidth = false;
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
}

void ImageCanvas::zoomOut()
{
    scale(0.8, 0.8);
    m_fitToWindow = false;
    m_fitWidth = false;
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
}

void ImageCanvas::zoomActual()
{
    if (m_item) {
        resetTransform();
        m_zoom = 1.0;
        m_fitToWindow = false;
        m_fitWidth = false;
        emit zoomChanged(m_zoom);
    }
}

void ImageCanvas::setZoom(qreal zoom)
{
    if (!m_item || zoom <= 0.0) {
        return;
    }
    resetTransform();
    scale(zoom, zoom);
    m_zoom = zoom;
    m_fitToWindow = false;
    m_fitWidth = false;
    emit zoomChanged(m_zoom);
}

void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_wheelZoom) {
        event->ignore();
        return;
    }
    const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    scale(factor, factor);
    m_fitToWindow = false;
    m_fitWidth = false;
    m_zoom = transform().m11();
    emit zoomChanged(m_zoom);
    event->accept();
}

void ImageCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_cropMode && event->button() == Qt::LeftButton && m_item) {
        m_selecting = true;
        m_origin = mapToScene(event->pos());
        clearSelection();
        m_rubber = scene()->addRect(QRectF(m_origin, m_origin),
                                    QPen(QColor(0, 180, 255), 1.5, Qt::DashLine),
                                    QBrush(QColor(0, 180, 255, 40)));
        event->accept();
        return;
    }
    if (m_middlePan && event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    // 左键平移由 QGraphicsView::ScrollHandDrag 原生处理，勿拦截基类事件。
    QGraphicsView::mousePressEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting && m_rubber) {
        const QPointF cur = mapToScene(event->pos());
        m_rubber->setRect(QRectF(m_origin, cur).normalized());
        event->accept();
        return;
    }
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPan;
        m_lastPan = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selecting && event->button() == Qt::LeftButton) {
        m_selecting = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fitToWindow) {
        fitToWindow();
    } else if (m_fitWidth) {
        fitToWidth();
    }
    if (m_logo) {
        m_logo->move((viewport()->width() - m_logo->width()) / 2,
                     (viewport()->height() - m_logo->height()) / 2);
    }
}
