// 应用图标唯一入口（图标派单 2026-11-18）。
// ---------------------------------------------------------------------------
// 这张表是「语义 id → Fluent 资源家族」的唯一事实来源：菜单、工具栏、底岛、
// 设置页、图层面板都只认 id。资源文件名由 AppIcon 自己拼，别处不许拼路径。
// 资源落位：resources/icons/fluent/<16|20|24>/<regular|filled>/ic_fluent_<family>_<size>_<style>.svg
// 着色与缓存见文件末尾；这里只管映射。

#include "AppIcon.h"

#include <QDir>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <QSvgRenderer>
#include <QtGlobal>

namespace {

struct Entry {
    const char *id;      // 应用语义 id
    const char *family;  // Fluent 图标家族名（ic_fluent_<family>_<size>_<style>.svg）
    const char *filled;  // 有 filled 变体时的家族名；空 = 复用 regular
    const char *note;    // 选择理由 / 回退说明（只给人看，不参与渲染）
};

// 顺序按使用面分组：菜单/设置 → 工具栏与底岛 → 图层面板 → 状态与扩展。
// note 里写清「为什么是这个家族」和「包内缺件时的回退」，缺件规则见
// 管理/SoleView-图标系统-20261118/missing-and-custom.csv。
const Entry kEntries[] = {
    // —— 打开与定位 ——
    {"image", "image", nullptr, "打开图片 / 图片状态"},
    {"images", "image_multiple", nullptr, "选择图片（多图）"},
    {"folder", "folder", nullptr, "普通文件夹"},
    {"folder-open", "folder_open", "folder_open", "打开文件夹、顶栏「打开」；按下短暂填充"},
    {"folder-target", "folder_search", nullptr, "定位文件（在资源管理器中显示）"},
    {"folder-next", "folder_arrow_right", nullptr, "下一个文件夹"},
    {"folder-prev", "folder_arrow_left", nullptr, "上一个文件夹"},
    {"folder-copy", "folder_arrow_right", nullptr, "复制到图片文件夹"},
    {"apps", "apps", "apps", "浏览/宫格入口；总览打开或按下时填充"},
    {"browse", "apps", "apps", "浏览（MainWindow 历史 id，与 apps 同一家族）"},
    {"grid", "list", nullptr, "任务菜单根组：包内 grid 已用于浏览，根组改用清单语义"},
    // —— 常用动作 ——
    {"info", "info", "info", "图片信息 / 关于；设置导航选中时填充"},
    {"copy", "copy", "copy", "复制渲染后的图片；按下短暂填充"},
    {"copy-more", "copy", nullptr, "复制更多（父入口）"},
    {"pin", "pin", "pin", "图片置顶；置顶态用 filled（派单 §5 激活态）"},
    {"bookmark", "bookmark", "bookmark", "书签；已收藏用 filled"},
    {"save", "save", nullptr, "另存为"},
    {"print", "print", nullptr, "打印"},
    {"printer", "print", nullptr, "打印（MenuCatalog 历史 id）"},
    {"open-with", "open", nullptr, "用其它程序打开"},
    {"pencil", "edit", nullptr, "用图像编辑器打开"},
    {"trash", "delete", nullptr, "删除（危险操作）"},
    // —— 浏览与导航 ——
    {"nav", "navigation", nullptr, "浏览导航父项"},
    {"chev-left", "chevron_left", nullptr, "上一张 / 返回（历史别名）"},
    {"chev-right", "chevron_right", nullptr, "下一张 / 子菜单箭头（历史别名）"},
    {"chevron-left", "chevron_left", "chevron_left", "上一张 / 返回；按下短暂填充"},
    {"chevron-right", "chevron_right", "chevron_right", "下一张 / 子菜单箭头；按下短暂填充"},
    {"chevron-down", "chevron_down", "chevron_down", "展开 / 下拉箭头；展开态填充"},
    {"first", "arrow_left", nullptr, "首张图片（与「上一张」区分，用长箭头）"},
    {"last", "arrow_right", nullptr, "末尾图片（与「下一张」区分，用长箭头）"},
    {"shuffle", "arrow_swap", nullptr, "随机跳转"},
    // —— 排序 ——
    {"sort", "arrow_sort", nullptr, "排序方式父项"},
    {"sort-desc", "text_sort_descending", nullptr, "降序"},
    {"text", "text_sort_ascending", nullptr, "按名称"},
    {"clock", "history", nullptr, "按时间"},
    {"size", "ruler", nullptr, "按大小"},
    {"tag", "filter", nullptr, "按类型"},
    // —— 浏览方式与缩放 ——
    {"view", "window_multiple", nullptr, "浏览方式父项"},
    {"book", "window_multiple", nullptr, "智能双页"},
    {"frame", "frame", nullptr, "实际大小 / 边框"},
    {"fit-window", "zoom_fit", nullptr, "适合窗口"},
    {"fit-width", "zoom_fit", nullptr, "适合宽度（同一家族，靠文案区分动作）"},
    {"zoom", "zoom_in", nullptr, "缩放父项"},
    {"zoom-in", "zoom_in", nullptr, "放大"},
    {"zoom-out", "subtract", nullptr, "缩小：包内无 Zoom Out，回退 Subtract（已记缺口）"},
    {"add", "add", "add", "底岛放大；按下短暂填充"},
    {"subtract", "subtract", "subtract", "底岛缩小；按下短暂填充"},
    {"minus", "subtract", nullptr, "无过渡效果"},
    // —— 图像处理 ——
    {"imageproc", "image_edit", nullptr, "图像处理分组"},
    {"image-edit", "image_edit", "image_edit", "顶栏「编辑」；按下短暂填充"},
    {"interp", "arrow_swap", nullptr, "插值算法"},
    {"sparkle", "paint_brush", nullptr, "图片效果 / 锐化"},
    {"invert", "brightness_high", nullptr, "反色"},
    {"eq", "options", nullptr, "Gamma"},
    {"flip-v", "arrow_swap", nullptr, "垂直翻转"},
    {"flip-h", "arrow_swap", nullptr, "水平翻转"},
    {"rotate", "arrow_rotate_counterclockwise", nullptr, "逆时针 90°"},
    {"rotate-cw", "arrow_rotate_clockwise", nullptr, "顺时针 90°"},
    {"transition", "arrow_sync", nullptr, "过渡效果 / 减淡"},
    {"crop", "crop", nullptr, "裁剪"},
    {"color", "color", "color", "颜色管理；设置导航选中时填充"},
    // —— 顶栏与底岛 ——
    {"play", "play", "play", "幻灯片播放；按下短暂填充"},
    {"stop", "pause", nullptr, "停止幻灯片：包内无 Stop，暂停即同一动作"},
    {"list", "image_multiple", "image_multiple", "底岛缩略图栏；按下短暂填充"},
    {"full-screen-maximize", "full_screen_maximize", "full_screen_maximize", "进入全屏；按下短暂填充"},
    {"arrow-rotate-counterclockwise", "arrow_rotate_counterclockwise",
     "arrow_rotate_counterclockwise", "底岛逆时针旋转；按下短暂填充"},
    {"dots", "more_horizontal", nullptr, "常用动作根组（历史 id）"},
    {"x", "dismiss", nullptr, "关闭窗口 / 清除搜索（历史 id）"},
    {"logout", "arrow_exit", nullptr, "退出（历史 id）"},
    // —— 锁 ——
    {"lock", "lock_closed", "lock_closed", "顶部锁/底栏锁：锁定态用 filled"},
    {"lock-closed", "lock_closed", "lock_closed", "锁定"},
    {"lock-open", "lock_open", "lock_open", "解锁；按下短暂填充"},
    // —— 面板与图层 ——
    {"layers", "stack", "stack", "PSD 图层面板入口；面板打开时填充"},
    {"stack", "stack", "stack", "PSD 图层；面板打开时填充"},
    {"data-histogram", "data_histogram", nullptr, "图片信息面板的直方图折叠入口"},
    {"eye", "eye", "eye", "图层可见：可见态用 filled，隐藏切 eye-off"},
    {"eye-off", "eye_off", nullptr, "图层隐藏"},
    {"list-tree", "text_bullet_list_tree", "text_bullet_list_tree", "大纲/树；设置导航选中时填充"},
    // —— 设置与状态 ——
    {"sliders", "options", nullptr, "设置 / 偏好"},
    {"options", "options", "options", "杂项；设置导航选中时填充"},
    {"settings", "settings", "settings", "设置；设置导航选中时填充"},
    {"search", "search", nullptr, "搜索"},
    {"filter", "filter", nullptr, "筛选"},
    {"edit", "edit", nullptr, "编辑"},
    {"monitor", "desktop", nullptr, "设为桌面壁纸"},
    {"moon", "weather_moon", nullptr, "深色主题"},
    // —— 通知与静态提示（Toast / 空状态；2026-11-19 定稿语义）——
    {"checkmark-circle", "checkmark_circle", nullptr, "成功通知"},
    {"warning", "warning", nullptr, "警告提示"},
    {"error-circle", "error_circle", nullptr, "失败通知 / 空状态错误行"},
    {"keyboard", "keyboard", "keyboard", "键盘快捷键；设置导航选中时填充"},
    {"cursor", "cursor", "cursor", "鼠标；设置导航选中时填充"},
    {"text-bullet-list", "text_bullet_list", "text_bullet_list", "右键菜单列表；设置导航选中时填充"},
    {"language", "local_language", "local_language", "语言；设置导航选中时填充"},
    {"link", "link", "link", "格式关联；设置导航选中时填充"},
};

const QHash<QString, const Entry *> &index()
{
    static QHash<QString, const Entry *> table;
    if (table.isEmpty()) {
        for (const Entry &e : kEntries) {
            table.insert(QString::fromLatin1(e.id), &e);
        }
    }
    return table;
}

// Fluent 只出 16/20/24 三档；按目标尺寸取最接近的一档，交给矢量等比缩放。
int assetSize(int px)
{
    if (px <= 18) {
        return 16;
    }
    if (px <= 22) {
        return 20;
    }
    return 24;
}

QString buildPath(const Entry &e, int size, AppIcon::Style style)
{
    const bool filled = style == AppIcon::Style::Filled && e.filled && e.filled[0];
    const char *family = filled ? e.filled : e.family;
    const char *styleName = filled ? "filled" : "regular";
    return QStringLiteral(":/icons/fluent/%1/%2/ic_fluent_%3_%1_%2.svg")
        .arg(size).arg(QString::fromLatin1(styleName)).arg(QString::fromLatin1(family));
}

// QSvgRenderer 按路径缓存：同一文件只解析一次。图标只在 GUI 线程绘制，
// 故不加锁；后续若在后台线程取图，需要在这里补同步。
QSvgRenderer *rendererFor(const QString &path)
{
    static QHash<QString, QSvgRenderer *> cache;
    auto it = cache.constFind(path);
    if (it != cache.constEnd()) {
        return it.value();
    }
    QSvgRenderer *r = new QSvgRenderer(path);
    cache.insert(path, r);
    return r->isValid() ? r : nullptr;
}

// 位图缓存：id + 尺寸 + 样式 + dpr + 前景色。菜单每帧重绘，命中缓存才不会
// 反复解析 SVG。上限 512 条，超出整体清空（图标组合有限，不会频繁触发）。
QPixmap renderPixmap(const QString &id, int px, const QColor &ink, AppIcon::Style style,
                     qreal dpr)
{
    static QHash<QString, QPixmap> cache;
    const QString key = QStringLiteral("%1|%2|%3|%4|%5")
                            .arg(id).arg(px).arg(int(style))
                            .arg(QString::number(dpr, 'f', 2)).arg(ink.rgba());
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) {
        return it.value();
    }

    const Entry *e = index().value(id, nullptr);
    if (!e || !AppIcon::available()) {
        return {};
    }
    QSvgRenderer *r = rendererFor(buildPath(*e, assetSize(px), style));
    if (!r) {
        return {};
    }

    QPixmap pm(qMax(1, int(px * dpr)), qMax(1, int(px * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        r->render(&p, QRectF(0, 0, px, px));
        // 单色蒙版着色：Fluent 图标是纯 fill 的单一色路径，SourceIn 保留边缘 alpha，
        // 不会出现硬边或半透明泛白；深浅主题共用同一份文件（派单 §5）。
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(QRectF(0, 0, px, px), ink);
    }

    if (cache.size() > 512) {
        cache.clear();
    }
    cache.insert(key, pm);
    return pm;
}

} // namespace

namespace AppIcon {

bool available()
{
    // 验收开关：SOLEVIEW_NO_ICONS=1（兼容 QUIKVIEW_NO_ICONS=1）时按「图标资源缺失」处理，
    // 用来确认回退路径——图标整体缺席，但按钮、文字、交互全部照常，不黑块也不崩。正常使用不设。
    if (!qEnvironmentVariableIsEmpty("SOLEVIEW_NO_ICONS")
        || !qEnvironmentVariableIsEmpty("QUIKVIEW_NO_ICONS"))
        return false;
    return QDir(QStringLiteral(":/icons/fluent")).exists();
}

bool has(const QString &id)
{
    return index().contains(id);
}

QString sourcePath(const QString &id, Style style)
{
    const Entry *e = index().value(id, nullptr);
    if (!e) {
        return {};
    }
    // 取 20px 档作为代表（三档只有光学修正的差别，家族与样式一致）。
    return buildPath(*e, 20, style);
}

QPixmap pixmap(const QString &id, int px, const QColor &ink, Style style, qreal dpr)
{
    return renderPixmap(id, px, ink, style, dpr);
}

QIcon icon(const QString &id, const QColor &ink, int px, Style style)
{
    QIcon ic;
    if (!has(id)) {
        return ic;
    }
    ic.addPixmap(pixmap(id, px, ink, style, 1.0));
    ic.addPixmap(pixmap(id, px, ink, style, 2.0));
    return ic;
}

bool paint(QPainter &p, const QString &id, const QRectF &rect, const QColor &ink, Style style)
{
    if (rect.isEmpty() || !has(id)) {
        return false;
    }
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : 2.0;
    // 取最长边作为逻辑尺寸，再按原始纵横比等比放进 rect（派单 §5 禁止拉伸）。
    const int px = int(qRound(qMin(rect.width(), rect.height())));
    const QPixmap pm = pixmap(id, px, ink, style, dpr > 0 ? dpr : 2.0);
    if (pm.isNull()) {
        return false;
    }
    const QSizeF logical = pm.deviceIndependentSize();
    const QPointF topLeft(rect.center().x() - logical.width() / 2.0,
                          rect.center().y() - logical.height() / 2.0);
    p.drawPixmap(topLeft, pm);
    return true;
}

} // namespace AppIcon
