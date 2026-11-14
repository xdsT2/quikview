#pragma once

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRectF>
#include <QString>

// 中性线性图标（右键菜单派单 2026-11-14）。
// ---------------------------------------------------------------------------
// 菜单与设置页使用同一套图标：16px 网格、约 1.45px 线宽、圆角端点，
// 颜色由调用方传入（正常/禁用/危险三态各自取色），不内嵌具体主题色。
// 只负责按 id 画线，不知道任何目录/配置——目录见 MenuCatalog.h。
namespace MenuIcons {

// 在 rect 内按 16px 网格画图标；返回 false 表示未知 id（调用方可回退无图标）。
bool paint(QPainter &p, const QString &id, const QRectF &rect, const QColor &color);

// 生成位图（默认 16 逻辑像素 @2x，供设置页 QLabel 使用）。
QPixmap pixmap(const QString &id, const QColor &color, int px = 16, qreal dpr = 2.0);

// 生成 QIcon（同一 id 同一色缓存复用；设置页行图标用）。
QIcon icon(const QString &id, const QColor &color);

} // namespace MenuIcons
