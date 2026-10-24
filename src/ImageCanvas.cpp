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

namespace {

// 空态标识：同心圆环 + 右下短斜线，与 resources/quikview.ico 同一视觉语汇。
// 自有几何图形（不使用任何第三方 logo 的构形）。
QPixmap makePlaceholderMark(int S, const QColor &color)
{
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, qMax(1.0, S / 20.0)));
    const QPointF ctr(S / 2.0, S / 2.0);
    const double outer = 0.30 * S;
    p.drawEllipse(ctr, outer, outer);
    p.drawEllipse(ctr, outer * 0.52, outer * 0.52);
    p.drawLine(QPointF(ctr.x() + outer * 0.60, ctr.y() + outer * 0.60),
               QPointF(ctr.x() + outer * 0.98, ctr.y() + outer * 0.98));
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
    setFrameShape(QFrame::NoFrame);
    setBackgroundBrush(QColor(QStringLiteral("#161719")));
    setStyleSheet(QStringLiteral("QGraphicsView{background:#161719;border:0;}"));

    m_logo = new QLabel(viewport());
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_logo->setPixmap(makePlaceholderMark(88, QColor("#626262")));
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
    // QGraphicsScene 的 sceneRect 只涨不缩：换图后必须显式重置成新图尺寸，
    // 否则小于视口的图会按旧矩形的中心对齐，表现为「图偏在左边」。
    scene()->setSceneRect(m_item->boundingRect());
    fitToWindow();
    if (m_logo) {
        m_logo->hide();
    }
}

void ImageCanvas::setPixmapPreservingView(const QPixmap &pixmap, const QPointF &anchorNorm,
                                          qreal zoom, bool fitWindow, bool fitWidth)
{
    const bool hadItem = (m_item != nullptr);
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    resetTransform();
    scene()->setSceneRect(m_item->boundingRect());   // 同 setPixmap：防止沿用旧矩形
    if (!hadItem || fitWindow) {
        fitToWindow();
        return;
    }
    if (fitWidth) {
        fitToWidth();
        return;
    }
    const qreal z = zoom > 0.0 ? zoom : 1.0;
    scale(z, z);
    m_zoom = z;
    m_fitToWindow = false;
    m_fitWidth = false;
    const QRectF r = m_item->sceneBoundingRect();
    centerOn(r.left() + qBound(0.0, anchorNorm.x(), 1.0) * r.width(),
             r.top() + qBound(0.0, anchorNorm.y(), 1.0) * r.height());
    emit zoomChanged(m_zoom);
}

QPointF ImageCanvas::viewCenterNormalized() const
{
    if (!m_item) {
        return QPointF(0.5, 0.5);
    }
    const QRectF r = m_item->sceneBoundingRect();
    if (r.width() <= 0.0 || r.height() <= 0.0) {
        return QPointF(0.5, 0.5);
    }
    const QPointF c = mapToScene(viewport()->rect().center());
    return QPointF(qBound(0.0, (c.x() - r.left()) / r.width(), 1.0),
                   qBound(0.0, (c.y() - r.top()) / r.height(), 1.0));
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

void ImageCanvas::setContentMargins(const QMargins &margins)
{
    // 边距变化会改变可视区，自定义缩放下必须把原来的视图中心还原回去，
    // 否则展开缩略图栏后图片会被压到一边、拖动也对不上。
    const QPointF keep = m_item ? mapToScene(viewport()->rect().center()) : QPointF();
    setViewportMargins(margins.left(), margins.top(), margins.right(), margins.bottom());
    if (m_fitToWindow) {
        fitToWindow();
    } else if (m_fitWidth) {
        fitToWidth();
    } else if (m_item) {
        centerOn(keep);
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
    applyZoomAt(m_zoom * 1.25, viewport()->rect().center());
}

void ImageCanvas::zoomOut()
{
    applyZoomAt(m_zoom * 0.8, viewport()->rect().center());
}

void ImageCanvas::zoomActual()
{
    applyZoomAt(1.0, viewport()->rect().center());
}

void ImageCanvas::setZoom(qreal zoom)
{
    applyZoomAt(zoom, viewport()->rect().center());
}

// 缩放锚点还原：图片比视口小时 Qt 自己居中（滚动条无行程），此时不需要对齐；
// 超出视口时按「场景点当前落在哪」与「它应该落在哪」的像素差推滚动条。
void ImageCanvas::keepScenePointAt(const QPointF &scenePt, const QPoint &viewportPt)
{
    if (!m_item) {
        return;
    }
    const qreal z = transform().m11();
    const QRectF r = m_item->sceneBoundingRect();
    if (r.width() * z <= viewport()->width() && r.height() * z <= viewport()->height()) {
        return;
    }
    const QPointF now = mapFromScene(scenePt);
    const QPoint d(qRound(now.x()) - viewportPt.x(), qRound(now.y()) - viewportPt.y());
    if (d.isNull()) {
        return;
    }
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + d.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + d.y());
}

void ImageCanvas::applyZoomAt(qreal zoom, const QPoint &anchorViewportPt)
{
    if (!m_item || zoom <= 0.0) {
        return;
    }
    const QPointF anchorScene = mapToScene(anchorViewportPt);
    resetTransform();
    scale(zoom, zoom);
    m_zoom = zoom;
    m_fitToWindow = false;
    m_fitWidth = false;
    // 先发信号：主窗口会在这里同步把上下安全区归零（并 centerOn 保持中心），
    // 之后再按锚点做一次像素级对齐，避免「进入手动缩放」那一下跳图。
    emit zoomChanged(m_zoom);
    keepScenePointAt(anchorScene, anchorViewportPt);
}

void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_wheelZoom) {
        event->ignore();
        return;
    }
    const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    // 滚轮以鼠标位置为锚点（原型/交接单 §5）
    applyZoomAt(m_zoom * factor, event->position().toPoint());
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
