#pragma once

#include <QString>

// 应用内右键菜单目录（右键菜单派单 2026-11-14「全量配置修订」专用）。
// ---------------------------------------------------------------------------
// 唯一事实来源：MainWindow::buildContextMenu 组装菜单、SettingsDialog 上下文
// 菜单页列开关、Config 默认值判定，三处都从这张表取，不允许各自再抄一份。
//
// 可见性只保存「父级总开关 + 可配置的一级单命令开关」（键 config/appmenu/<id>）：
//   - Group  三个外层收纳分组（浏览与显示 / 图像处理 / 文件操作），有总开关；
//   - Parent 组内具体子菜单，仅父项有总开关，末级子命令随父项整体显隐；
//   - Single 一级单命令，各有可见性开关；
//   - Fixed  固定命令（设置… / 设为桌面壁纸… / 打印…），无开关，随所在组显隐；
//   - Leaf   子菜单里的末级子命令，无自己的开关，列在设置页（名称+图标）。
// 缺键（首次安装 / 旧配置 / 恢复默认）一律按 defOn 出值。
// 顺序即本表顺序，第一版无拖拽排序。
namespace AppMenu {

enum Kind { Single, Group, Parent, Fixed, Leaf };

struct Node {
    const char *id;      // 配置键 config/appmenu/<id>
    const char *icon;    // MenuIcons 图标名（菜单与设置页同一套线性图标）
    Kind kind;
    bool defOn;          // 默认可见（缺键时的取值）
    const char *parent;  // 父节点 id；"" = 顶层
};

inline const Node kNodes[] = {
    // —— 打开与定位（一级单命令）——
    {"open_image",     "image",        Single, true,  ""},
    {"open_folder",    "folder-open",  Single, true,  ""},
    {"select_image",   "images",       Single, true,  ""},
    {"locate_file",    "folder-target",Single, true,  ""},
    // —— 常用动作（一级单命令）——
    {"image_info",     "info",         Single, true,  ""},
    {"copy_clipboard", "copy",         Single, true,  ""},
    {"always_on_top",  "pin",          Single, true,  ""},
    // —— 浏览与显示（外层分组，总开关）——
    {"group_browse",   "browse",       Group,  true,  ""},
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
    {"group_image",    "imageproc",    Group,  true,  ""},
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
    {"group_file",     "folder",       Group,  true,  ""},
    {"wallpaper",      "monitor",      Fixed,  true,  "group_file"},
    {"parent_copymore","copy-more",    Parent, true,  "group_file"},
    {"copy_folder",    "folder-copy",  Leaf,   true,  "parent_copymore"},
    {"parent_openwith","open-with",    Parent, true,  "group_file"},
    {"open_editor",    "pencil",       Leaf,   true,  "parent_openwith"},
    {"print",          "printer",      Fixed,  true,  "group_file"},
    // —— 危险操作 / 应用（一级单命令；设置… 固定显示）——
    {"delete_file",    "trash",        Single, true,  ""},
    {"settings",       "sliders",      Fixed,  true,  ""},
    {"about",          "info",         Single, true,  ""},
    {"close_window",   "x",            Single, true,  ""},
    {"quit",           "logout",       Single, true,  ""},
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

// 节点显示名：末级子命令按设计图文案；父项/分组/一级命令由组装方给定
// （菜单/设置页都用同一份，避免两处文案漂移）。
inline QString label(const QString &id)
{
    // 末级子命令（Leaf）+ 固定命令的文案集中在这里，菜单与设置页共用。
    if (id == QLatin1String("nav_next"))          return QStringLiteral("下一张");
    if (id == QLatin1String("nav_prev"))          return QStringLiteral("上一张");
    if (id == QLatin1String("nav_first"))         return QStringLiteral("首张图片");
    if (id == QLatin1String("nav_last"))          return QStringLiteral("末尾图片");
    if (id == QLatin1String("nav_next10"))        return QStringLiteral("向下 10 张图片");
    if (id == QLatin1String("nav_prev10"))        return QStringLiteral("向上 10 张图片");
    if (id == QLatin1String("nav_rand_back"))     return QStringLiteral("向后随机转到某张图片");
    if (id == QLatin1String("nav_rand_fwd"))      return QStringLiteral("向前随机转到某张图片");
    if (id == QLatin1String("nav_folder_next"))   return QStringLiteral("下一个文件夹");
    if (id == QLatin1String("nav_folder_prev"))   return QStringLiteral("上一个文件夹");
    if (id == QLatin1String("sort_name"))         return QStringLiteral("按名称");
    if (id == QLatin1String("sort_time"))         return QStringLiteral("按时间");
    if (id == QLatin1String("sort_size"))         return QStringLiteral("按大小");
    if (id == QLatin1String("sort_type"))         return QStringLiteral("按类型");
    if (id == QLatin1String("sort_desc"))         return QStringLiteral("降序");
    if (id == QLatin1String("view_actual"))       return QStringLiteral("实际大小");
    if (id == QLatin1String("view_fit"))          return QStringLiteral("适合窗口");
    if (id == QLatin1String("view_fitwidth"))     return QStringLiteral("适合宽度");
    if (id == QLatin1String("view_two_l2r"))      return QStringLiteral("智能双页 (左至右)");
    if (id == QLatin1String("view_two_r2l"))      return QStringLiteral("智能双页 (右至左)");
    if (id == QLatin1String("zoom_in"))           return QStringLiteral("放大");
    if (id == QLatin1String("zoom_out"))          return QStringLiteral("缩小");
    if (id == QLatin1String("zoom_lock"))         return QStringLiteral("锁定缩放");
    if (id == QLatin1String("zoom_100"))          return QStringLiteral("100%");
    if (id == QLatin1String("zoom_in1"))          return QStringLiteral("放大 (1%)");
    if (id == QLatin1String("zoom_out1"))         return QStringLiteral("缩小 (1%)");
    if (id == QLatin1String("interp_bilinear"))   return QStringLiteral("Bilinear 插值 (快)");
    if (id == QLatin1String("interp_bicubic"))    return QStringLiteral("Bicubic 插值 (慢，高品质)");
    if (id == QLatin1String("interp_lanczos"))    return QStringLiteral("Lanczos 插值 (慢，高品质)");
    if (id == QLatin1String("fx_negative"))       return QStringLiteral("反色");
    if (id == QLatin1String("fx_gamma"))          return QStringLiteral("Gamma");
    if (id == QLatin1String("fx_flipv"))          return QStringLiteral("垂直翻转");
    if (id == QLatin1String("fx_fliph"))          return QStringLiteral("水平翻转");
    if (id == QLatin1String("rot_none"))          return QStringLiteral("不旋转");
    if (id == QLatin1String("rot_left"))          return QStringLiteral("逆时针 90°");
    if (id == QLatin1String("rot_right"))         return QStringLiteral("顺时针 90°");
    if (id == QLatin1String("rot_180"))           return QStringLiteral("旋转 180°");
    if (id == QLatin1String("fx_none"))           return QStringLiteral("无");
    if (id == QLatin1String("fx_soften"))         return QStringLiteral("减淡");
    if (id == QLatin1String("fx_sharpen"))        return QStringLiteral("减淡 + 锐化");
    if (id == QLatin1String("copy_folder"))       return QStringLiteral("复制到图片文件夹");
    if (id == QLatin1String("open_editor"))       return QStringLiteral("用图像编辑器打开");
    // 父项 / 分组 / 一级命令
    if (id == QLatin1String("open_image"))        return QStringLiteral("打开图片…");
    if (id == QLatin1String("open_folder"))       return QStringLiteral("打开文件夹…");
    if (id == QLatin1String("select_image"))      return QStringLiteral("选择图片…");
    if (id == QLatin1String("locate_file"))       return QStringLiteral("定位文件…");
    if (id == QLatin1String("image_info"))        return QStringLiteral("图片信息");
    if (id == QLatin1String("copy_clipboard"))    return QStringLiteral("复制到剪贴板");
    if (id == QLatin1String("always_on_top"))     return QStringLiteral("窗口置顶");
    if (id == QLatin1String("group_browse"))      return QStringLiteral("浏览与显示");
    if (id == QLatin1String("parent_nav"))        return QStringLiteral("浏览导航");
    if (id == QLatin1String("parent_sort"))       return QStringLiteral("排序方式");
    if (id == QLatin1String("parent_view"))       return QStringLiteral("浏览方式");
    if (id == QLatin1String("parent_zoom"))       return QStringLiteral("缩放工具");
    if (id == QLatin1String("group_image"))       return QStringLiteral("图像处理");
    if (id == QLatin1String("parent_interp"))     return QStringLiteral("插值算法");
    if (id == QLatin1String("parent_fx"))         return QStringLiteral("图片效果");
    if (id == QLatin1String("parent_rotate"))     return QStringLiteral("图像旋转");
    if (id == QLatin1String("parent_transition")) return QStringLiteral("过渡效果");
    if (id == QLatin1String("group_file"))        return QStringLiteral("文件操作");
    if (id == QLatin1String("wallpaper"))         return QStringLiteral("设为桌面壁纸…");
    if (id == QLatin1String("parent_copymore"))   return QStringLiteral("复制更多");
    if (id == QLatin1String("parent_openwith"))   return QStringLiteral("打开方式");
    if (id == QLatin1String("print"))             return QStringLiteral("打印…");
    if (id == QLatin1String("delete_file"))       return QStringLiteral("删除");
    if (id == QLatin1String("settings"))          return QStringLiteral("设置…");
    if (id == QLatin1String("about"))             return QStringLiteral("关于 QuikView");
    if (id == QLatin1String("close_window"))      return QStringLiteral("关闭窗口");
    if (id == QLatin1String("quit"))              return QStringLiteral("退出");
    return id;
}

// 设置页分组标题（按顶层节点顺序分段；仅设置页用）。
inline QString sectionTitle(const QString &sectionId)
{
    if (sectionId == QLatin1String("open"))   return QStringLiteral("打开与定位");
    if (sectionId == QLatin1String("common")) return QStringLiteral("常用动作");
    if (sectionId == QLatin1String("task"))   return QStringLiteral("任务菜单");
    if (sectionId == QLatin1String("danger")) return QStringLiteral("危险操作");
    if (sectionId == QLatin1String("app"))    return QStringLiteral("应用");
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

} // namespace AppMenu
