#pragma once

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QVector>

// 命令注册表（派单 2026-11-18 H1-7 / H2-3：公开命令行参数体系 + 快捷键命令 ID 化）。
// ---------------------------------------------------------------------------
// 唯一事实来源：一个可执行动作 = 一条命令（BandiView 风格 CMD_* 命名）。
// 快捷键、设置页键盘/鼠标页、冒烟探针、后续公开 CLI 全部按命令 ID 引用同一张表，
// 不允许各自再抄一份动作清单。
//
// 字段语义：
//   id            命令 ID，`CMD_` 前缀大写下划线，一旦发布不得改名（脚本/CLI 契约）。
//   group         分组，只用于 ID 全表展示与后续 CLI 帮助分组，不参与运行逻辑。
//   legacy        旧键盘槽位名（config/keyboard/<legacy>_*），空 = 该命令从未有槽位。
//                 旧 ini 的 _enabled/_key/_action 靠这一列映射到命令 ID（Config 迁移）。
//   alias         其余旧动作名（' ; ' 分隔）。鼠标页动作表的 close / context_menu /
//                 fullscreen 就靠这里兼容；改名后旧鼠标配置继续生效。
//   labelCtx/label 中文标签的译文上下文与源串。刻意复用既有上下文与既有源串
//                 （MainWindow / SettingsDialog / Chrome），这样 8 语 .ts 里已有的
//                译文直接命中，不新增未翻译条目（en_US 无汉字、unfinished=0）。
//   factoryKeys   出厂键位，' ; ' 分隔 QKeySequence::PortableText；空 = 出厂无键。
//   configurable  true = 设置页键盘页可改绑（历史 14 槽）；false = 键位固定，
//                 键盘页不列（改动范围最小化，也避免一次性把 70 条动作全暴露）。
//   takesParam    true = 需要参数执行（幻灯片秒数），CLI 侧支持 `-ss:N`。
//
// 盘点口径（2026-11-22 领单更新）：本表以 `MainWindow::createActions` 里 mk()/mkFn()
// 注册的动作逐个登记，另补四个「无独立 QAction 但用户可执行」的命令（全屏 / 上下文
// 菜单 / 编辑书签 / 不旋转，走 triggerCommand 直调）。i18n 自检脚本
// Temp/scripts/command-i18n-check.ps1，运行期对账是冒烟 `cmdcheck`。
// 不入表：底岛上的「图片总览 / 缩略图栏」入口（不是 mk() 动作，且这两条中文只在
// Chrome.cpp 里出现、.ts 尚未覆盖该上下文；二期做 CLI 帮助时随 i18n 一起补）。
namespace Cmd {

// —— 文件 ——
inline constexpr const char *FileOpen            = "CMD_FILE_OPEN";
inline constexpr const char *FileOpenFolder      = "CMD_FILE_OPEN_FOLDER";
inline constexpr const char *FileSaveAs          = "CMD_FILE_SAVE_AS";
inline constexpr const char *FilePrint           = "CMD_FILE_PRINT";
inline constexpr const char *FileBatchConvert    = "CMD_FILE_BATCH_CONVERT";
inline constexpr const char *FileClose           = "CMD_FILE_CLOSE";
inline constexpr const char *FileQuit            = "CMD_FILE_QUIT";
inline constexpr const char *FileDelete          = "CMD_FILE_DELETE";
inline constexpr const char *FileRename          = "CMD_FILE_RENAME";
inline constexpr const char *FileCopyToFolder    = "CMD_FILE_COPY_TO_FOLDER";
inline constexpr const char *FileMoveToFolder    = "CMD_FILE_MOVE_TO_FOLDER";
inline constexpr const char *PhotoFolderPanel    = "CMD_PHOTO_FOLDER_PANEL";
inline constexpr const char *FileOpenWithEditor  = "CMD_FILE_OPEN_WITH_EDITOR";
inline constexpr const char *FileWallpaper       = "CMD_FILE_SET_WALLPAPER";
inline constexpr const char *FileSelect          = "CMD_FILE_SELECT";
inline constexpr const char *FileLocate          = "CMD_FILE_LOCATE";

// —— 剪贴板 ——
inline constexpr const char *CopyClipboard       = "CMD_COPY_CLIPBOARD";
inline constexpr const char *CopyExif            = "CMD_COPY_EXIF";
inline constexpr const char *CopyPath            = "CMD_COPY_PATH";
inline constexpr const char *CopyVisible         = "CMD_COPY_VISIBLE";
inline constexpr const char *CopyImageFile     = "CMD_COPY_FILE";
inline constexpr const char *PasteClipboard      = "CMD_PASTE_CLIPBOARD";

// —— 浏览（翻页/跳转）——
inline constexpr const char *PageNext            = "CMD_PAGE_NEXT";
inline constexpr const char *PagePrev            = "CMD_PAGE_PREV";
inline constexpr const char *PageNext10          = "CMD_PAGE_NEXT_10";
inline constexpr const char *PagePrev10          = "CMD_PAGE_PREV_10";
inline constexpr const char *PageFirst           = "CMD_PAGE_FIRST";
inline constexpr const char *PageLast            = "CMD_PAGE_LAST";
inline constexpr const char *FolderNext          = "CMD_FOLDER_NEXT";
inline constexpr const char *FolderPrev          = "CMD_FOLDER_PREV";
inline constexpr const char *FrameNext           = "CMD_FRAME_NEXT";
inline constexpr const char *FramePrev           = "CMD_FRAME_PREV";
inline constexpr const char *PageRandomNext      = "CMD_PAGE_RANDOM_NEXT";
inline constexpr const char *PageRandomPrev      = "CMD_PAGE_RANDOM_PREV";

// —— 缩放与显示 ——
inline constexpr const char *ZoomIn              = "CMD_ZOOM_IN";
inline constexpr const char *ZoomOut             = "CMD_ZOOM_OUT";
inline constexpr const char *ZoomActual          = "CMD_ZOOM_ACTUAL";
inline constexpr const char *ZoomFit             = "CMD_ZOOM_FIT";
inline constexpr const char *ZoomFitWidth        = "CMD_ZOOM_FIT_WIDTH";
inline constexpr const char *ZoomStepIn          = "CMD_ZOOM_STEP_IN";
inline constexpr const char *ZoomStepOut         = "CMD_ZOOM_STEP_OUT";
inline constexpr const char *ZoomLock            = "CMD_ZOOM_LOCK";
inline constexpr const char *TwoPageL2R          = "CMD_VIEW_TWO_PAGE_L2R";
inline constexpr const char *TwoPageR2L          = "CMD_VIEW_TWO_PAGE_R2L";

// —— 图像处理 ——
inline constexpr const char *RotateLeft          = "CMD_IMAGE_ROTATE_LEFT";
inline constexpr const char *RotateRight         = "CMD_IMAGE_ROTATE_RIGHT";
inline constexpr const char *Rotate180           = "CMD_IMAGE_ROTATE_180";
inline constexpr const char *RotateNone          = "CMD_IMAGE_ROTATE_NONE";
inline constexpr const char *FlipH               = "CMD_IMAGE_FLIP_H";
inline constexpr const char *FlipV               = "CMD_IMAGE_FLIP_V";
inline constexpr const char *Resize              = "CMD_IMAGE_RESIZE";
inline constexpr const char *Negative            = "CMD_IMAGE_NEGATIVE";
inline constexpr const char *Gamma               = "CMD_IMAGE_GAMMA";
inline constexpr const char *StretchSmall        = "CMD_IMAGE_STRETCH_SMALL";
inline constexpr const char *EffectNone          = "CMD_IMAGE_EFFECT_NONE";
inline constexpr const char *EffectSoften        = "CMD_IMAGE_EFFECT_SOFTEN";
inline constexpr const char *EffectSharpen       = "CMD_IMAGE_EFFECT_SHARPEN";
inline constexpr const char *InterpBilinear      = "CMD_IMAGE_INTERP_BILINEAR";
inline constexpr const char *InterpBicubic       = "CMD_IMAGE_INTERP_BICUBIC";
inline constexpr const char *InterpLanczos       = "CMD_IMAGE_INTERP_LANCZOS";

// —— 幻灯片 ——
inline constexpr const char *SlideshowToggle     = "CMD_SLIDESHOW_TOGGLE";
inline constexpr const char *SlideshowStop       = "CMD_SLIDESHOW_STOP";
inline constexpr const char *SlideshowStart      = "CMD_SLIDESHOW_START";   // 参数：秒数 1..9

// —— 书签 ——
inline constexpr const char *BookmarkToggle      = "CMD_BOOKMARK_TOGGLE";
inline constexpr const char *BookmarkEdit        = "CMD_BOOKMARK_EDIT";

// —— 排序 ——
inline constexpr const char *SortName            = "CMD_SORT_NAME";
inline constexpr const char *SortTime            = "CMD_SORT_TIME";
inline constexpr const char *SortSize            = "CMD_SORT_SIZE";
inline constexpr const char *SortType            = "CMD_SORT_TYPE";
inline constexpr const char *SortDesc            = "CMD_SORT_DESC";

// —— 窗口与应用 ——
inline constexpr const char *Fullscreen          = "CMD_WINDOW_FULLSCREEN";
inline constexpr const char *OnTop               = "CMD_WINDOW_ON_TOP";
inline constexpr const char *InfoPanel           = "CMD_WINDOW_INFO_PANEL";
inline constexpr const char *ContextMenu         = "CMD_WINDOW_CONTEXT_MENU";
inline constexpr const char *DarkMode            = "CMD_WINDOW_DARK_MODE";
inline constexpr const char *Settings            = "CMD_APP_SETTINGS";
inline constexpr const char *About               = "CMD_APP_ABOUT";

} // namespace Cmd

namespace CommandCatalog {

enum Group {
    GroupFile,
    GroupClipboard,
    GroupPage,
    GroupZoom,
    GroupImage,
    GroupSlide,
    GroupBookmark,
    GroupSort,
    GroupWindow,
};

struct Entry {
    const char *id;
    Group group;
    const char *legacy;        // 旧键盘槽位名（config/keyboard/<legacy>_*）
    const char *alias;         // 额外旧动作名，';' 分隔
    const char *labelCtx;
    const char *label;
    const char *factoryKeys;   // ';' 分隔 PortableText
    bool configurable;
    bool takesParam;
};

// 分组顺序即 ID 全表展示顺序。注意：group 名只作内部/文档展示，不参与 UI 翻译
// （键盘页有自己的四个分组标题；CLI 帮助的分组文案在第二阶段定稿）。
inline const QString groupTitle(Group g)
{
    switch (g) {
    case GroupFile:      return QStringLiteral("文件");
    case GroupClipboard: return QStringLiteral("剪贴板");
    case GroupPage:      return QStringLiteral("浏览与翻页");
    case GroupZoom:      return QStringLiteral("缩放与显示");
    case GroupImage:     return QStringLiteral("图像处理");
    case GroupSlide:     return QStringLiteral("幻灯片");
    case GroupBookmark:  return QStringLiteral("书签");
    case GroupSort:      return QStringLiteral("排序");
    case GroupWindow:    return QStringLiteral("窗口与应用");
    }
    return QString();
}

// 逐条登记。configurable=true 的 14 条与历史键盘页 14 槽一一对应；
// factoryKeys 必须与 MainWindow::createActions 的出厂键位逐键一致。
inline const Entry kEntries[] = {
    // —— 文件 ——
    { Cmd::FileOpen,           GroupFile,      "open",       "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "打开(&O)..."),           "Ctrl+O;F2",     true,  false },
    { Cmd::FileOpenFolder,     GroupFile,      "openfolder", "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "打开文件夹..."),          "F",             true,  false },
    { Cmd::FileSaveAs,         GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "另存为…(&S)"),            "Ctrl+Shift+S",  false, false },
    { Cmd::FilePrint,          GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "打印"),                   "Ctrl+P",        false, false },
    { Cmd::FileBatchConvert,   GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "批量转换(&B)"),           "Ctrl+T",        false, false },
    { Cmd::FileClose,          GroupFile,      "",           "close", "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "关闭"),                   "F4",            false, false },
    { Cmd::FileQuit,           GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "退出(&Q)"),               "X;Ctrl+Q",      false, false },
    { Cmd::FileDelete,         GroupFile,      "del",        "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "删除"),                   "Del",           true,  false },
    { Cmd::FileRename,         GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "重命名…"),                "F6",            false, false },
    { Cmd::FileCopyToFolder,   GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制到 图片文件夹"),       "Ins",           false, false },
    { Cmd::FileMoveToFolder,   GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "移动到 图片文件夹 1"),     "Ctrl+Ins",      false, false },
    { Cmd::PhotoFolderPanel,   GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "照片文件夹面板…"),         "Alt+P",         false, false },
    { Cmd::FileOpenWithEditor, GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "用图像编辑器打开"),        "Ctrl+E",        false, false },
    { Cmd::FileWallpaper,      GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "设为桌面壁纸…"),           "",              false, false },
    { Cmd::FileSelect,         GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "选择图片"),               "Return;Enter",  false, false },
    { Cmd::FileLocate,         GroupFile,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "定位文件"),               "Ctrl+Return",   false, false },

    // —— 剪贴板 ——
    { Cmd::CopyClipboard,      GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制到剪贴板"),            "Ctrl+C",        false, false },
    { Cmd::CopyExif,           GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制 EXIF 数据"),          "Ctrl+Shift+C",  false, false },
    { Cmd::CopyPath,           GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制图片路径"),            "Ctrl+Alt+Shift+C", false, false },
    { Cmd::CopyVisible,        GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制当前可见图像"),        "Ctrl+Alt+C",    false, false },
    { Cmd::CopyImageFile,      GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "复制图像文件"),            "Alt+C",         false, false },
    { Cmd::PasteClipboard,     GroupClipboard, "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "从剪贴板粘贴图片"),        "Ctrl+V",        false, false },

    // —— 浏览与翻页 ——
    { Cmd::PageNext,           GroupPage,      "next",       "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "下一张图片"),             "Right;PageDown", true, false },
    { Cmd::PagePrev,           GroupPage,      "prev",       "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "上一张图片"),             "Left;PageUp;Backspace", true, false },
    { Cmd::PageNext10,         GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "向下10张图片"),            "Ctrl+PageDown", false, false },
    { Cmd::PagePrev10,         GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "向上10张图片"),            "Ctrl+PageUp",   false, false },
    { Cmd::PageFirst,          GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "首张图片"),               "Home",          false, false },
    { Cmd::PageLast,           GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "末尾图片"),               "End",           false, false },
    { Cmd::FolderNext,         GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "下一个文件夹"),            "]",             false, false },
    { Cmd::FolderPrev,         GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "上一个文件夹"),            "[",             false, false },
    { Cmd::FrameNext,          GroupPage,      "next_frame", "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "下一帧"),                 "Ctrl+Right",    true,  false },
    { Cmd::FramePrev,          GroupPage,      "prev_frame", "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "上一帧"),                 "Ctrl+Left",     true,  false },
    { Cmd::PageRandomNext,     GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "向后随机转到某张图片"),    "Ctrl+Alt+PageDown", false, false },
    { Cmd::PageRandomPrev,     GroupPage,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "向前随机转到某张图片"),    "Ctrl+Alt+PageUp",   false, false },

    // —— 缩放与显示 ——
    { Cmd::ZoomIn,             GroupZoom,      "zoom_in",    "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "放大(&+)"),               "Ctrl++;+",      true,  false },
    { Cmd::ZoomOut,            GroupZoom,      "zoom_out",   "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "缩小(&-)"),               "Ctrl+-;-",      true,  false },
    { Cmd::ZoomActual,         GroupZoom,      "actual_size","",   "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "原始大小 (100%)"),        "0;*",           true,  false },
    { Cmd::ZoomFit,            GroupZoom,      "fit",        "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "适合窗口"),               "9",             true,  false },
    { Cmd::ZoomFitWidth,       GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "适合宽度"),               "8",             false, false },
    { Cmd::ZoomStepIn,         GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "放大 (1%)"),              "Ctrl++",        false, false },
    { Cmd::ZoomStepOut,        GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "缩小 (1%)"),              "Ctrl+-",        false, false },
    { Cmd::ZoomLock,           GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "锁定缩放"),               "Ctrl+K",        false, false },
    { Cmd::TwoPageL2R,         GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "智能双页阅览 (左→右)"),    "7",             false, false },
    { Cmd::TwoPageR2L,         GroupZoom,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "智能双页阅览 (左←右)"),    "6",             false, false },

    // —— 图像处理 ——
    { Cmd::RotateLeft,         GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "逆时针旋转 90°"),         "Alt+Left",      false, false },
    { Cmd::RotateRight,        GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "顺时针旋转 90°"),         "Alt+Right",     false, false },
    { Cmd::Rotate180,          GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "旋转 180°"),              "Alt+Down",      false, false },
    { Cmd::RotateNone,         GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "不旋转"),                 "Alt+Up",        false, false },
    { Cmd::FlipH,              GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "水平翻转"),               "Ctrl+M",        false, false },
    { Cmd::FlipV,              GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "垂直翻转"),               "Ctrl+F",        false, false },
    { Cmd::Resize,             GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "调整大小(&R)"),           "",              false, false },
    { Cmd::Negative,           GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "反色"),                   "Ctrl+I",        false, false },
    { Cmd::Gamma,              GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "Gamma 补偿…"),            "Ctrl+G",        false, false },
    { Cmd::StretchSmall,       GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "拉伸小图"),               "Z",             false, false },
    { Cmd::EffectNone,         GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "无"),                     "U",             false, false },
    { Cmd::EffectSoften,       GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "减淡"),                   "I",             false, false },
    { Cmd::EffectSharpen,      GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "减淡 + 锐化"),            "S",             false, false },
    { Cmd::InterpBilinear,     GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "Bilinear 插值 (快)"),     "",              false, false },
    { Cmd::InterpBicubic,      GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "Bicubic 插值 (慢，高品质)"), "",           false, false },
    { Cmd::InterpLanczos,      GroupImage,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "Lanczos 插值 (慢，高品质)"), "",           false, false },

    // —— 幻灯片 ——
    { Cmd::SlideshowToggle,    GroupSlide,     "slideshow",  "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "幻灯片放映(&S)"),         "",              true,  false },
    { Cmd::SlideshowStop,      GroupSlide,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "停止放映"),               "Ctrl+0",        false, false },
    { Cmd::SlideshowStart,     GroupSlide,     "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "%1 秒放映"),              "Ctrl+1;Ctrl+2;Ctrl+3;Ctrl+4;Ctrl+5;Ctrl+6;Ctrl+7;Ctrl+8;Ctrl+9", false, true },

    // —— 书签 ——
    { Cmd::BookmarkToggle,     GroupBookmark,  "favorite",   "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "添加书签"),               "B",             true,  false },
    { Cmd::BookmarkEdit,       GroupBookmark,  "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "编辑书签"),               "F3",            false, false },

    // —— 排序 ——
    { Cmd::SortName,           GroupSort,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "文件名"),                 "",              false, false },
    { Cmd::SortTime,           GroupSort,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "修改日期"),               "",              false, false },
    { Cmd::SortSize,           GroupSort,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "大小"),                   "",              false, false },
    { Cmd::SortType,           GroupSort,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "类型"),                   "",              false, false },
    { Cmd::SortDesc,           GroupSort,      "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "降序"),                   "",              false, false },

    // —— 窗口与应用 ——
    { Cmd::Fullscreen,         GroupWindow,    "",           "fullscreen", "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "全屏"),                   "F11",           false, false },
    { Cmd::OnTop,              GroupWindow,    "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "窗口置顶"),               "Ctrl+A",        false, false },
    { Cmd::InfoPanel,          GroupWindow,    "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "显示/隐藏信息面板(&I)"),   "Tab",           false, false },
    { Cmd::ContextMenu,        GroupWindow,    "",           "context_menu", "SettingsDialog",
      QT_TRANSLATE_NOOP("SettingsDialog", "上下文菜单"),          "",              false, false },
    { Cmd::DarkMode,           GroupWindow,    "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "深色模式(&D)"),           "",              false, false },
    { Cmd::Settings,           GroupWindow,    "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "设置(&C)"),               "F5;F9",         false, false },
    { Cmd::About,              GroupWindow,    "",           "",    "MainWindow",
      QT_TRANSLATE_NOOP("MainWindow", "关于 SoleView"),          "F1",            false, false },
};

inline int count()
{
    return int(sizeof(kEntries) / sizeof(kEntries[0]));
}

inline const QVector<const Entry *> &all()
{
    static const QVector<const Entry *> list = [] {
        QVector<const Entry *> v;
        v.reserve(count());
        for (const Entry &e : kEntries)
            v.append(&e);
        return v;
    }();
    return list;
}

inline const Entry *find(const QString &cmdId)
{
    if (cmdId.isEmpty())
        return nullptr;
    for (const Entry &e : kEntries) {
        if (cmdId == QLatin1String(e.id))
            return &e;
    }
    return nullptr;
}

// 旧动作名（键盘槽位名 + 鼠标动作表别名）→ 命令。旧 ini / 旧鼠标配置兼容的唯一入口。
inline const Entry *findLegacy(const QString &legacyId)
{
    if (legacyId.isEmpty() || legacyId == QLatin1String("none"))
        return nullptr;
    for (const Entry &e : kEntries) {
        if (e.legacy && e.legacy[0] && legacyId == QLatin1String(e.legacy))
            return &e;
        if (e.alias && e.alias[0]) {
            for (const QString &a : QString::fromLatin1(e.alias).split(QLatin1Char(';'),
                                                                   Qt::SkipEmptyParts)) {
                if (legacyId == a.trimmed())
                    return &e;
            }
        }
    }
    return nullptr;
}

inline QString legacySlotOf(const Entry &e)
{
    return e.legacy ? QString::fromLatin1(e.legacy) : QString();
}

inline QStringList factoryKeys(const Entry &e)
{
    QStringList out;
    if (!e.factoryKeys || !e.factoryKeys[0])
        return out;
    for (const QString &k : QString::fromLatin1(e.factoryKeys).split(QLatin1Char(';'),
                                                                    Qt::SkipEmptyParts)) {
        const QString t = k.trimmed();
        if (!t.isEmpty())
            out.append(t);
    }
    return out;
}

inline QString label(const Entry &e)
{
    if (!e.label || !e.label[0])
        return QString::fromLatin1(e.id);
    return QCoreApplication::translate(e.labelCtx, e.label);
}

inline QString labelById(const QString &cmdId)
{
    const Entry *e = find(cmdId);
    return e ? label(*e) : cmdId;
}

// 键盘页可改绑的命令（历史 14 槽），顺序与设置页键盘页行顺序无关。
inline QVector<const Entry *> keyboardCommands()
{
    QVector<const Entry *> v;
    for (const Entry &e : kEntries) {
        if (e.configurable)
            v.append(&e);
    }
    return v;
}

} // namespace CommandCatalog
