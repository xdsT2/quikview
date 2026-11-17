// 菜单/设置页图标入口（右键菜单派单 2026-11-14；图标统一派单 2026-11-18 改实现）。
// ---------------------------------------------------------------------------
// 历史实现是 QPainter 在 16px 网格上逐条画线；图标派单冻结方案 A 后，这里只剩
// 转发：语义 id 交给 AppIcon（Fluent SVG + QSvgRenderer），不再保留手绘路径，
// 避免出现「手绘版 + SVG 版」两套真源（派单 §4 末段）。
// 调用方 API 不变：菜单、设置页、预览面板都仍走 MenuIcons::paint/pixmap/icon。

#include "MenuIcons.h"
#include "AppIcon.h"

namespace MenuIcons {

bool paint(QPainter &p, const QString &id, const QRectF &rect, const QColor &color)
{
    // 未知 id 直接失败，调用方回退（宁可缺图标也不画错图）。
    return AppIcon::paint(p, id, rect, color);
}

QPixmap pixmap(const QString &id, const QColor &color, int px, qreal dpr)
{
    return AppIcon::pixmap(id, px, color, AppIcon::Style::Regular, dpr);
}

QIcon icon(const QString &id, const QColor &color)
{
    return AppIcon::icon(id, color, 16, AppIcon::Style::Regular);
}

} // namespace MenuIcons
