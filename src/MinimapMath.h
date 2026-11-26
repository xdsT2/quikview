#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QtGlobal>

// 长图/放大图鸟瞰导航器（B2）纯几何计算，不依赖 Qt Widgets。
namespace MinimapMath {

struct MinimapLayout {
    bool visible = false;
    QSizeF thumbSize;
    QRectF highlightRect;   // 相对缩略图左上角 (0,0) 的可视区域框
};

// 仅当图片在任一轴实际溢出视口超过 2px 时显示鸟瞰图；
// 长图（高/宽 >= 2.5）保底宽度 36px，超宽图保底高度 32px，避免缩略图细成一根线。
inline MinimapLayout computeLayout(const QSizeF &imageSize,
                                   const QRectF &visibleSceneRect,
                                   qreal zoom,
                                   const QSizeF &viewportSize)
{
    MinimapLayout out;
    if (imageSize.width() <= 0.0 || imageSize.height() <= 0.0
        || viewportSize.width() <= 0.0 || viewportSize.height() <= 0.0
        || zoom <= 0.0) {
        return out;
    }

    const qreal dispW = imageSize.width() * zoom;
    const qreal dispH = imageSize.height() * zoom;
    if (dispW <= viewportSize.width() + 2.0 && dispH <= viewportSize.height() + 2.0) {
        return out;
    }

    const qreal aspect = imageSize.height() / imageSize.width();
    qreal maxW = 136.0;
    qreal maxH = 136.0;
    if (aspect >= 2.5) {
        maxH = 168.0;
        maxW = 96.0;
    } else if (aspect <= 0.4) {
        maxW = 156.0;
        maxH = 96.0;
    }

    const qreal scale = qMin(maxW / imageSize.width(), maxH / imageSize.height());
    qreal tw = qMax(aspect >= 2.5 ? 36.0 : 20.0, imageSize.width() * scale);
    qreal th = qMax(aspect <= 0.4 ? 32.0 : 20.0, imageSize.height() * scale);
    tw = qMin(maxW, tw);
    th = qMin(maxH, th);

    out.visible = true;
    out.thumbSize = QSizeF(tw, th);

    const QRectF imgBounds(0.0, 0.0, imageSize.width(), imageSize.height());
    QRectF inter = visibleSceneRect.intersected(imgBounds);
    if (inter.isEmpty()) {
        // 图片被拖出视口时把指示框钉在靠近视口的边缘，让用户一眼能点回来。
        const qreal cx = qBound(0.0, visibleSceneRect.center().x(), imageSize.width());
        const qreal cy = qBound(0.0, visibleSceneRect.center().y(), imageSize.height());
        const qreal hw = qBound(6.0, tw * (visibleSceneRect.width() / imageSize.width()), tw);
        const qreal hh = qBound(6.0, th * (visibleSceneRect.height() / imageSize.height()), th);
        const qreal hx = qBound(0.0, (cx / imageSize.width()) * tw - hw * 0.5, tw - hw);
        const qreal hy = qBound(0.0, (cy / imageSize.height()) * th - hh * 0.5, th - hh);
        out.highlightRect = QRectF(hx, hy, hw, hh);
        return out;
    }

    qreal rx = (inter.left() / imageSize.width()) * tw;
    qreal ry = (inter.top() / imageSize.height()) * th;
    qreal rw = (inter.width() / imageSize.width()) * tw;
    qreal rh = (inter.height() / imageSize.height()) * th;

    if (rw < 6.0) {
        const qreal cx = rx + rw * 0.5;
        rw = qMin(tw, 6.0);
        rx = qBound(0.0, cx - rw * 0.5, tw - rw);
    }
    if (rh < 6.0) {
        const qreal cy = ry + rh * 0.5;
        rh = qMin(th, 6.0);
        ry = qBound(0.0, cy - rh * 0.5, th - rh);
    }
    out.highlightRect = QRectF(rx, ry, rw, rh);
    return out;
}

inline QPointF thumbPointToScene(const QPointF &ptInThumb,
                                 const QSizeF &thumbSize,
                                 const QSizeF &imageSize)
{
    if (thumbSize.width() <= 0.0 || thumbSize.height() <= 0.0
        || imageSize.width() <= 0.0 || imageSize.height() <= 0.0) {
        return QPointF(0.0, 0.0);
    }
    const qreal nx = qBound(0.0, ptInThumb.x() / thumbSize.width(), 1.0);
    const qreal ny = qBound(0.0, ptInThumb.y() / thumbSize.height(), 1.0);
    return QPointF(nx * imageSize.width(), ny * imageSize.height());
}

} // namespace MinimapMath
