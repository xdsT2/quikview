#pragma once

#include <QCoreApplication>
#include <QString>
#include <QVector>

// 应用内右键菜单目录（右键菜单派单 2026-11-14「全量配置修订」专用）。
// ---------------------------------------------------------------------------
// 唯一事实来源：MainWindow::buildContextMenu 组装菜单、SettingsDialog 上下文
// 菜单页列开关、Config 默认值判定，三处都从这张表取，不允许各自再抄一份。
//
// 可见性只保存「有开关的节点」的值（键 config/appmenu/<id>），生效可见性 =
// 本项开关 × 各级祖先总开关（Config::appMenuVisible 逐级上溯）：
//   - Section 五个根组（打开与定位 / 常用动作 / 任务菜单 / 危险操作 / 应用），
//             有总开关；只用于设置页的层级收纳，菜单里不产生额外节点；
//   - Group   三个外层收纳分组（浏览与显示 / 图像处理 / 文件操作），有总开关；
//   - Parent  组内具体子菜单，仅父项有总开关，末级子命令随父项整体显隐；
//   - Single  可独立配置的单命令；
//   - Fixed   固定命令（设置… / 设为桌面壁纸… / 打印…），无开关，随所在组显隐；
//   - Leaf    只展示随父入口显示的子菜单命令或执行参数。
// 单个可配置子项由父开关代表（如「危险操作 → 删除」）。
// 缺键（首次安装 / 旧配置 / 恢复默认）一律按 defOn 出值。
// 顺序即本表顺序，第一版无拖拽排序。
namespace AppMenu {

enum Kind { Single, Group, Parent, Fixed, Leaf, Section };

struct Node {
    const char *id;      // 配置键 config/appmenu/<id>
    const char *icon;    // MenuIcons 图标名（菜单与设置页同一套线性图标）
    Kind kind;
    bool defOn;          // 默认可见（缺键时的取值）
    const char *parent;  // 父节点 id；"" = 顶层
};

inline const Node kNodes[] = {
    // —— 打开与定位（根组 + 4 个一级单命令）——
    {"group_open",     "folder-open",  Section,true,  ""},
    {"open_image",     "image",        Single, true,  "group_open"},
    {"open_folder",    "folder-open",  Single, true,  "group_open"},
    {"select_image",   "images",       Single, true,  "group_open"},
    {"locate_file",    "folder-target",Single, true,  "group_open"},
    // —— 常用动作（根组 + 3 个一级单命令）——
    {"group_common",   "dots",         Section,true,  ""},
    {"image_info",     "info",         Single, true,  "group_common"},
    {"copy_clipboard", "copy",         Single, true,  "group_common"},
    {"always_on_top",  "pin",          Single, true,  "group_common"},
    // —— 任务菜单（根组；下辖三个收纳分组）——
    {"group_task",     "grid",         Section,true,  ""},
    {"group_browse",   "browse",       Group,  true,  "group_task"},
    {"parent_nav",     "nav",          Parent, false, "group_browse"},
    {"nav_next",       "chev-right",   Leaf,   true,  "parent_nav"},
    {"nav_prev",       "chev-left",    Leaf,   true,  "parent_nav"},
    {"nav_first",      "first",        Leaf,   true,  "parent_nav"},
    {"nav_last",       "last",         Leaf,   true,  "parent_nav"},
    {"nav_next10",     "chev-right",   Leaf,   true,  "parent_nav"},
    {"nav_prev10",     "chev-left",    Leaf,   true,  "parent_nav"},
    {"nav_rand_back",  "shuffle",      Leaf,   true,  "parent_nav"},
    {"nav_rand_fwd",   "shuffle",      Leaf,   true,  "parent_nav"},
    {"nav_folder_next","folder-next",  Leaf,   true,  "parent_nav"},
    {"nav_folder_prev","folder-prev",  Leaf,   true,  "parent_nav"},
    {"parent_sort",    "sort",         Parent, true,  "group_browse"},
    {"sort_name",      "text",         Leaf,   true,  "parent_sort"},
    {"sort_time",      "clock",        Leaf,   true,  "parent_sort"},
    {"sort_size",      "size",         Leaf,   true,  "parent_sort"},
    {"sort_type",      "tag",          Leaf,   true,  "parent_sort"},
    {"sort_desc",      "sort-desc",    Leaf,   true,  "parent_sort"},
    {"parent_view",    "view",         Parent, true,  "group_browse"},
    {"view_actual",    "frame",        Leaf,   true,  "parent_view"},
    {"view_fit",       "fit-window",   Leaf,   true,  "parent_view"},
    {"view_fitwidth",  "fit-width",    Leaf,   true,  "parent_view"},
    {"view_two_l2r",   "book",         Leaf,   true,  "parent_view"},
    {"view_two_r2l",   "book",         Leaf,   true,  "parent_view"},
    {"parent_zoom",    "zoom",         Parent, false, "group_browse"},
    {"zoom_in",        "zoom-in",      Leaf,   true,  "parent_zoom"},
    {"zoom_out",       "zoom-out",     Leaf,   true,  "parent_zoom"},
    {"zoom_lock",      "lock",         Leaf,   true,  "parent_zoom"},
    {"zoom_100",       "frame",        Leaf,   true,  "parent_zoom"},
    {"zoom_in1",       "zoom-in",      Leaf,   true,  "parent_zoom"},
    {"zoom_out1",      "zoom-out",     Leaf,   true,  "parent_zoom"},
    // —— 图像处理（外层分组，总开关；默认四个子项全关，整组不出现父入口）——
    {"group_image",    "imageproc",    Group,  true,  "group_task"},
    {"parent_interp",  "interp",       Parent, false, "group_image"},
    {"interp_bilinear","interp",       Leaf,   true,  "parent_interp"},
    {"interp_bicubic", "interp",       Leaf,   true,  "parent_interp"},
    {"interp_lanczos", "interp",       Leaf,   true,  "parent_interp"},
    {"parent_fx",      "sparkle",      Parent, false, "group_image"},
    {"fx_negative",    "invert",       Leaf,   true,  "parent_fx"},
    {"fx_gamma",       "eq",           Leaf,   true,  "parent_fx"},
    {"fx_flipv",       "flip-v",       Leaf,   true,  "parent_fx"},
    {"fx_fliph",       "flip-h",       Leaf,   true,  "parent_fx"},
    {"parent_rotate",  "rotate",       Parent, false, "group_image"},
    {"rot_none",       "image",        Leaf,   true,  "parent_rotate"},
    {"rot_left",       "rotate",       Leaf,   true,  "parent_rotate"},
    {"rot_right",      "rotate-cw",    Leaf,   true,  "parent_rotate"},
    {"rot_180",        "rotate",       Leaf,   true,  "parent_rotate"},
    {"parent_transition","transition", Parent, false, "group_image"},
    {"fx_none",        "minus",        Leaf,   true,  "parent_transition"},
    {"fx_soften",      "transition",   Leaf,   true,  "parent_transition"},
    {"fx_sharpen",     "sparkle",      Leaf,   true,  "parent_transition"},
    // —— 文件操作（外层分组，总开关；壁纸/打印为固定行，无开关）——
    {"group_file",     "folder",       Group,  true,  "group_task"},
    {"wallpaper",      "monitor",      Fixed,  true,  "group_file"},
    {"parent_copymore","copy-more",    Parent, true,  "group_file"},
    {"copy_folder",    "folder-copy",  Leaf,   true,  "parent_copymore"},
    {"parent_openwith","open-with",    Parent, true,  "group_file"},
    {"open_editor",    "pencil",       Leaf,   true,  "parent_openwith"},
    {"print",          "printer",      Fixed,  true,  "group_file"},
    // —— 危险操作（唯一可配置入口继承父开关）——
    {"group_danger",   "trash",        Section,true,  ""},
    {"delete_file",    "trash",        Single,   true,  "group_danger"},
    // —— 应用（根组 4 项：设置… 固定显示，其余三个各有开关）——
    {"group_app",      "sliders",      Section,true,  ""},
    {"settings",       "sliders",      Fixed,  true,  "group_app"},
    {"about",          "info",         Single, true,  "group_app"},
    {"close_window",   "x",            Single, true,  "group_app"},
    {"quit",           "logout",       Single, true,  "group_app"},
};

inline const Node *find(const QString &id)
{
    for (const Node &n : kNodes) {
        if (QLatin1String(n.id) == id) {
            return &n;
        }
    }
    return nullptr;
}

inline bool defaultOn(const QString &id)
{
    const Node *n = find(id);
    return n ? n->defOn : false;
}

// 目录语义：固定项和执行参数不参与可见性配置。
inline bool canConfigure(const Node &n)
{
    return n.kind == Single || n.kind == Group || n.kind == Parent || n.kind == Section;
}

inline QString parentOf(const QString &id)
{
    const Node *n = find(id);
    return n ? QLatin1String(n->parent) : QString();
}

// 直接子级（按表内顺序）
inline QVector<const Node *> childrenOf(const QString &id)
{
    QVector<const Node *> out;
    for (const Node &n : kNodes) {
        if (id.isEmpty()) {
            if (!n.parent[0]) {
                out.append(&n);
            }
        } else if (QLatin1String(n.parent) == id) {
            out.append(&n);
        }
    }
    return out;
}

// 可独立配置的直接子级数量（规格 §开关判定：0 / 1 / ≥2 决定开关层级）。
// 固定项与末级展示行不计入。
inline int configurableChildCount(const QString &id)
{
    int n = 0;
    for (const Node *c : childrenOf(id)) {
        if (canConfigure(*c)) {
            ++n;
        }
    }
    return n;
}

inline bool ownsSwitch(const Node &n)
{
    return canConfigure(n)
           && (!n.parent[0] || configurableChildCount(QLatin1String(n.parent)) >= 2);
}

// 节点显示名：末级子命令按设计图文案；父项/分组/一级命令由组装方给定
// （菜单/设置页都用同一份，避免两处文案漂移）。
// 多语言派单（2026-11-15）：原为硬编码 QStringLiteral，界面切语言后右键菜单
// 仍是中文。改为 QT_TRANSLATE_NOOP 表 + QCoreApplication::translate 查表，
// 简中行为不变（translate 无译文时原样返回），菜单/设置页共用同一份文案。
inline const char *labelSource(const QString &id)
{
    if (id == QLatin1String("nav_next"))          return QT_TRANSLATE_NOOP("AppMenu", "下一张");
    if (id == QLatin1String("nav_prev"))          return QT_TRANSLATE_NOOP("AppMenu", "上一张");
    if (id == QLatin1String("nav_first"))         return QT_TRANSLATE_NOOP("AppMenu", "首张图片");
    if (id == QLatin1String("nav_last"))          return QT_TRANSLATE_NOOP("AppMenu", "末尾图片");
    if (id == QLatin1String("nav_next10"))        return QT_TRANSLATE_NOOP("AppMenu", "向下 10 张图片");
    if (id == QLatin1String("nav_prev10"))        return QT_TRANSLATE_NOOP("AppMenu", "向上 10 张图片");
    if (id == QLatin1String("nav_rand_back"))     return QT_TRANSLATE_NOOP("AppMenu", "向后随机转到某张图片");
    if (id == QLatin1String("nav_rand_fwd"))      return QT_TRANSLATE_NOOP("AppMenu", "向前随机转到某张图片");
    if (id == QLatin1String("nav_folder_next"))   return QT_TRANSLATE_NOOP("AppMenu", "下一个文件夹");
    if (id == QLatin1String("nav_folder_prev"))   return QT_TRANSLATE_NOOP("AppMenu", "上一个文件夹");
    if (id == QLatin1String("sort_name"))         return QT_TRANSLATE_NOOP("AppMenu", "按名称");
    if (id == QLatin1String("sort_time"))         return QT_TRANSLATE_NOOP("AppMenu", "按时间");
    if (id == QLatin1String("sort_size"))         return QT_TRANSLATE_NOOP("AppMenu", "按大小");
    if (id == QLatin1String("sort_type"))         return QT_TRANSLATE_NOOP("AppMenu", "按类型");
    if (id == QLatin1String("sort_desc"))         return QT_TRANSLATE_NOOP("AppMenu", "降序");
    if (id == QLatin1String("view_actual"))       return QT_TRANSLATE_NOOP("AppMenu", "实际大小");
    if (id == QLatin1String("view_fit"))          return QT_TRANSLATE_NOOP("AppMenu", "适合窗口");
    if (id == QLatin1String("view_fitwidth"))     return QT_TRANSLATE_NOOP("AppMenu", "适合宽度");
    if (id == QLatin1String("view_two_l2r"))      return QT_TRANSLATE_NOOP("AppMenu", "智能双页 (左至右)");
    if (id == QLatin1String("view_two_r2l"))      return QT_TRANSLATE_NOOP("AppMenu", "智能双页 (右至左)");
    if (id == QLatin1String("zoom_in"))           return QT_TRANSLATE_NOOP("AppMenu", "放大");
    if (id == QLatin1String("zoom_out"))          return QT_TRANSLATE_NOOP("AppMenu", "缩小");
    if (id == QLatin1String("zoom_lock"))         return QT_TRANSLATE_NOOP("AppMenu", "锁定缩放");
    if (id == QLatin1String("zoom_100"))          return QT_TRANSLATE_NOOP("AppMenu", "100%");
    if (id == QLatin1String("zoom_in1"))          return QT_TRANSLATE_NOOP("AppMenu", "放大 (1%)");
    if (id == QLatin1String("zoom_out1"))         return QT_TRANSLATE_NOOP("AppMenu", "缩小 (1%)");
    if (id == QLatin1String("interp_bilinear"))   return QT_TRANSLATE_NOOP("AppMenu", "Bilinear 插值 (快)");
    if (id == QLatin1String("interp_bicubic"))    return QT_TRANSLATE_NOOP("AppMenu", "Bicubic 插值 (慢，高品质)");
    if (id == QLatin1String("interp_lanczos"))    return QT_TRANSLATE_NOOP("AppMenu", "Lanczos 插值 (慢，高品质)");
    if (id == QLatin1String("fx_negative"))       return QT_TRANSLATE_NOOP("AppMenu", "反色");
    if (id == QLatin1String("fx_gamma"))          return QT_TRANSLATE_NOOP("AppMenu", "Gamma");
    if (id == QLatin1String("fx_flipv"))          return QT_TRANSLATE_NOOP("AppMenu", "垂直翻转");
    if (id == QLatin1String("fx_fliph"))          return QT_TRANSLATE_NOOP("AppMenu", "水平翻转");
    if (id == QLatin1String("rot_none"))          return QT_TRANSLATE_NOOP("AppMenu", "不旋转");
    if (id == QLatin1String("rot_left"))          return QT_TRANSLATE_NOOP("AppMenu", "逆时针 90°");
    if (id == QLatin1String("rot_right"))         return QT_TRANSLATE_NOOP("AppMenu", "顺时针 90°");
    if (id == QLatin1String("rot_180"))           return QT_TRANSLATE_NOOP("AppMenu", "旋转 180°");
    if (id == QLatin1String("fx_none"))           return QT_TRANSLATE_NOOP("AppMenu", "无");
    if (id == QLatin1String("fx_soften"))         return QT_TRANSLATE_NOOP("AppMenu", "减淡");
    if (id == QLatin1String("fx_sharpen"))        return QT_TRANSLATE_NOOP("AppMenu", "减淡 + 锐化");
    if (id == QLatin1String("copy_folder"))       return QT_TRANSLATE_NOOP("AppMenu", "复制到图片文件夹");
    if (id == QLatin1String("open_editor"))       return QT_TRANSLATE_NOOP("AppMenu", "用图像编辑器打开");
    // 父项 / 分组 / 一级命令
    if (id == QLatin1String("group_open"))        return QT_TRANSLATE_NOOP("AppMenu", "打开与定位");
    if (id == QLatin1String("group_common"))      return QT_TRANSLATE_NOOP("AppMenu", "常用动作");
    if (id == QLatin1String("group_task"))        return QT_TRANSLATE_NOOP("AppMenu", "任务菜单");
    if (id == QLatin1String("group_danger"))      return QT_TRANSLATE_NOOP("AppMenu", "危险操作");
    if (id == QLatin1String("group_app"))         return QT_TRANSLATE_NOOP("AppMenu", "应用");
    if (id == QLatin1String("open_image"))        return QT_TRANSLATE_NOOP("AppMenu", "打开图片…");
    if (id == QLatin1String("open_folder"))       return QT_TRANSLATE_NOOP("AppMenu", "打开文件夹…");
    if (id == QLatin1String("select_image"))      return QT_TRANSLATE_NOOP("AppMenu", "选择图片…");
    if (id == QLatin1String("locate_file"))       return QT_TRANSLATE_NOOP("AppMenu", "定位文件…");
    if (id == QLatin1String("image_info"))        return QT_TRANSLATE_NOOP("AppMenu", "图片信息");
    if (id == QLatin1String("copy_clipboard"))    return QT_TRANSLATE_NOOP("AppMenu", "复制到剪贴板");
    if (id == QLatin1String("always_on_top"))     return QT_TRANSLATE_NOOP("AppMenu", "窗口置顶");
    if (id == QLatin1String("group_browse"))      return QT_TRANSLATE_NOOP("AppMenu", "浏览与显示");
    if (id == QLatin1String("parent_nav"))        return QT_TRANSLATE_NOOP("AppMenu", "浏览导航");
    if (id == QLatin1String("parent_sort"))       return QT_TRANSLATE_NOOP("AppMenu", "排序方式");
    if (id == QLatin1String("parent_view"))       return QT_TRANSLATE_NOOP("AppMenu", "浏览方式");
    if (id == QLatin1String("parent_zoom"))       return QT_TRANSLATE_NOOP("AppMenu", "缩放工具");
    if (id == QLatin1String("group_image"))       return QT_TRANSLATE_NOOP("AppMenu", "图像处理");
    if (id == QLatin1String("parent_interp"))     return QT_TRANSLATE_NOOP("AppMenu", "插值算法");
    if (id == QLatin1String("parent_fx"))         return QT_TRANSLATE_NOOP("AppMenu", "图片效果");
    if (id == QLatin1String("parent_rotate"))     return QT_TRANSLATE_NOOP("AppMenu", "图像旋转");
    if (id == QLatin1String("parent_transition")) return QT_TRANSLATE_NOOP("AppMenu", "过渡效果");
    if (id == QLatin1String("group_file"))        return QT_TRANSLATE_NOOP("AppMenu", "文件操作");
    if (id == QLatin1String("wallpaper"))         return QT_TRANSLATE_NOOP("AppMenu", "设为桌面壁纸…");
    if (id == QLatin1String("parent_copymore"))   return QT_TRANSLATE_NOOP("AppMenu", "复制更多");
    if (id == QLatin1String("parent_openwith"))   return QT_TRANSLATE_NOOP("AppMenu", "打开方式");
    if (id == QLatin1String("print"))             return QT_TRANSLATE_NOOP("AppMenu", "打印…");
    if (id == QLatin1String("delete_file"))       return QT_TRANSLATE_NOOP("AppMenu", "删除");
    if (id == QLatin1String("settings"))          return QT_TRANSLATE_NOOP("AppMenu", "设置…");
    if (id == QLatin1String("about"))             return QT_TRANSLATE_NOOP("AppMenu", "关于 QuikView");
    if (id == QLatin1String("close_window"))      return QT_TRANSLATE_NOOP("AppMenu", "关闭窗口");
    if (id == QLatin1String("quit"))              return QT_TRANSLATE_NOOP("AppMenu", "退出");
    return nullptr;
}

inline QString label(const QString &id)
{
    if (const char *src = labelSource(id)) {
        return QCoreApplication::translate("AppMenu", src);
    }
    return id;
}

// 设置页分组标题（按顶层节点顺序分段；仅设置页用）。
inline QString sectionTitle(const QString &sectionId)
{
    if (sectionId == QLatin1String("open"))   return QCoreApplication::translate("AppMenu", QT_TRANSLATE_NOOP("AppMenu", "打开与定位"));
    if (sectionId == QLatin1String("common")) return QCoreApplication::translate("AppMenu", QT_TRANSLATE_NOOP("AppMenu", "常用动作"));
    if (sectionId == QLatin1String("task"))   return QCoreApplication::translate("AppMenu", QT_TRANSLATE_NOOP("AppMenu", "任务菜单"));
    if (sectionId == QLatin1String("danger")) return QCoreApplication::translate("AppMenu", QT_TRANSLATE_NOOP("AppMenu", "危险操作"));
    if (sectionId == QLatin1String("app"))    return QCoreApplication::translate("AppMenu", QT_TRANSLATE_NOOP("AppMenu", "应用"));
    return sectionId;
}

// 顶层节点的设置页归属（分组行归 task 段）。
inline const char *sectionOf(const Node &n)
{
    const QLatin1String id(n.id);
    if (id == QLatin1String("open_image") || id == QLatin1String("open_folder") ||
        id == QLatin1String("select_image") || id == QLatin1String("locate_file"))
        return "open";
    if (id == QLatin1String("image_info") || id == QLatin1String("copy_clipboard") ||
        id == QLatin1String("always_on_top"))
        return "common";
    if (id == QLatin1String("group_browse") || id == QLatin1String("group_image") ||
        id == QLatin1String("group_file"))
        return "task";
    if (id == QLatin1String("delete_file"))
        return "danger";
    return "app";
}

// 显示用快捷键（查看器右键菜单与设置页预览共用同一份；多语言派单 2026-11-15）。
// 只读、只作显示；真实键位与可用性仍由动作本体/键盘自定义决定。
// 一级命令的快捷键沿用既有固定槽位语义（open/folder 走出厂主键）。
inline const char *shortcutSource(const QString &id)
{
    if (id == QLatin1String("open_image"))   return "F2";
    if (id == QLatin1String("open_folder"))  return "F";
    if (id == QLatin1String("select_image")) return "Enter";
    if (id == QLatin1String("locate_file"))  return "Ctrl+Enter";
    if (id == QLatin1String("image_info"))   return "Tab";
    if (id == QLatin1String("copy_clipboard"))return "Ctrl+C";
    if (id == QLatin1String("always_on_top"))return "Ctrl+A";
    if (id == QLatin1String("nav_next"))     return "→";
    if (id == QLatin1String("nav_prev"))     return "←";
    if (id == QLatin1String("nav_first"))    return "Home";
    if (id == QLatin1String("nav_last"))     return "End";
    if (id == QLatin1String("nav_next10"))   return "Ctrl+PageDown";
    if (id == QLatin1String("nav_prev10"))   return "Ctrl+PageUp";
    if (id == QLatin1String("nav_rand_back"))return "Ctrl+Alt+PageDown";
    if (id == QLatin1String("nav_rand_fwd")) return "Ctrl+Alt+PageUp";
    if (id == QLatin1String("nav_folder_next")) return "]";
    if (id == QLatin1String("nav_folder_prev")) return "[";
    if (id == QLatin1String("zoom_in"))      return "+";
    if (id == QLatin1String("zoom_out"))     return "−";
    if (id == QLatin1String("zoom_lock"))    return "Ctrl+K";
    if (id == QLatin1String("zoom_in1"))     return "Ctrl++";
    if (id == QLatin1String("zoom_out1"))    return "Ctrl+−";
    if (id == QLatin1String("fx_gamma"))     return "Ctrl+G";
    if (id == QLatin1String("fx_flipv"))     return "Ctrl+F";
    if (id == QLatin1String("fx_fliph"))     return "Ctrl+M";
    if (id == QLatin1String("fx_negative"))  return "Ctrl+I";
    if (id == QLatin1String("rot_none"))     return "Alt+Up";
    if (id == QLatin1String("rot_left"))     return "Alt+Left";
    if (id == QLatin1String("rot_right"))    return "Alt+Right";
    if (id == QLatin1String("rot_180"))      return "Alt+Down";
    if (id == QLatin1String("fx_none"))      return "U";
    if (id == QLatin1String("fx_soften"))    return "I";
    if (id == QLatin1String("fx_sharpen"))   return "S";
    if (id == QLatin1String("copy_folder"))  return "Ins";
    if (id == QLatin1String("open_editor"))  return "Ctrl+E";
    if (id == QLatin1String("delete_file"))  return "Delete";
    return nullptr;
}

// 预览分组标题（设置页预览列顶部，随内容段生成的接收段名）。
inline QString shortcut(const QString &id)
{
    if (const char *src = shortcutSource(id)) {
        return QCoreApplication::translate("AppMenu", src);
    }
    return {};
}

} // namespace AppMenu
