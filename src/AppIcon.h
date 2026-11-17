#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

class QPainter;
class QRectF;

// 应用图标唯一入口（图标派单 2026-11-18）。
// ---------------------------------------------------------------------------
// 语义 id → Microsoft Fluent UI System Icons 资源，集中在这张表里；页面、菜单、
// 控件一律用 id 取图，禁止在别处拼 :/icons/fluent/... 路径（派单 §3.4）。
// 渲染走 QSvgRenderer（派单 §4 方案 A）：SVG 保持矢量，按 16/20/24 三档取最接近
// 的原始资源，再等比缩放到目标尺寸；着色用 SourceIn 蒙版，深浅主题共用同一份
// 文件，不为主题复制资源。
// 颜色由调用方传入，本层不读主题、不读配置，只认 id。
namespace AppIcon {

// Regular = 默认/悬停；Filled = 选中、持续激活、锁定（派单 §5）。
enum class Style { Regular, Filled };

// 资源树是否随程序打包（缺 QtSvg 插件或漏打包时为 false，界面回退无图标）。
bool available();

// 该 id 是否有映射（无映射直接返回空图，不猜图）。
bool has(const QString &id);

// 该 id 实际命中的资源路径（验收/诊断用，不要拿它去拼别处的图标）。
QString sourcePath(const QString &id, Style style = Style::Regular);

// 生成位图；px 为逻辑像素，dpr 为设备像素比（2.0 = @2x）。
QPixmap pixmap(const QString &id, int px, const QColor &ink, Style style = Style::Regular,
               qreal dpr = 2.0);

// 生成 QIcon（同一 id 同一色同一尺寸缓存复用）。
QIcon icon(const QString &id, const QColor &ink, int px = 16, Style style = Style::Regular);

// 在 rect 内等比绘制并着色；返回 false 表示无此语义或无资源（调用方回退）。
bool paint(QPainter &p, const QString &id, const QRectF &rect, const QColor &ink,
           Style style = Style::Regular);

} // namespace AppIcon
