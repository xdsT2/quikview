#pragma once

#include <QSettings>
#include <QColor>
#include <QStringList>

// 应用配置。设计对照逆向得到的 Honeyview config.ini 配置面，
// 但刻意做三处改进（见 ARCHITECTURE.md「取舍」）：
//  1. 遥测默认关闭，且不设任何上报端点；后续若要遥测做成显式 opt-in。
//  2. 便携模式 is_portable 默认跟随「配置是否与程序同目录」自动判定。
//  3. 无任何第三方推广组件。
class Config
{
public:
    explicit Config(const QString &path = QString());

    static Config &instance();

    // 便携模式：配置落在程序同目录即视为便携，优先读本地 config.ini
    bool isPortable() const;

    QString language() const;
    QString theme() const;   // "dark" | "light" | "system"
    QString dockStyle() const;   // "floating" | "edge"（默认 floating，升级沿用悬浮）
    // 底栏锁定常显（2026-11-28）：底栏锁图标状态跨重启保留，默认关。
    bool dockLocked() const;
    void setDockLocked(bool locked);
    bool blurMaterial() const;   // 磨砂材质（右键菜单与信息浮层，默认关）
    int slideshowIntervalMs() const;  // 默认 3000
    bool slideshowRandom() const;     // 幻灯片随机播放（默认 false）
    QString defaultZoom() const;      // "fit" | "actual"
    bool saveZoomState() const;       // 打开图片时恢复上次缩放（默认 false）

    // 按路径记忆的视图状态（键 config/zoom/<pathHash>，仅 save_zoom_state 开启时读写）。
    // 值："fit" / "width" / 纯数字缩放比；空串 = 该路径没有记录。
    QString savedViewState(const QString &path) const;
    void setSavedViewState(const QString &path, const QString &state);

    // 一般
    bool rememberBrowsedFolders() const;  // 记住浏览过的文件夹（默认 true）
    bool allowMultipleInstances() const;  // 允许多个实例（默认 true：默认多开）
    bool deleteConfirm() const;        // 删除文件时确认（默认 true）
    bool notifyCorner() const;         // 在边角显示通知信息
    bool showTooltip() const;          // 悬浮工具提示总开关（默认显示）
    bool showAnimControl() const;      // 动图播放控制栏 + 信息行帧号（第 x/y 帧）是否显示（默认显示，语义为超集）
    bool showZoomControl() const;      // 缩放百分比浮标是否显示（默认显示）
    bool showMinimap() const;          // 长图/放大图右下角鸟瞰导航器（默认显示）
    bool exifSmallFont() const;        // EXIF / 文件信息用小字体（默认 12px，开则 10px）
    bool alwaysOnTop() const;          // 总在最前
    bool escExit() const;              // 按 ESC 退出
    QString edgeAction() const;        // "first" | "stop" | "ask"（默认 "first"；ask=边界弹确认，翻页批次 2 已实现）

    // 浏览
    // 背景/边框颜色与背景图访问器已随第 18 期「背景」段下线删除；
    // 四个键 view/bkgnd_* 、view/border_color 不再读写（存量 ini 留着无害）。
    bool overlayFilename() const;      // Shift+TAB 在画布顶部显示文件名（默认 false）
    // 双页阅览「首图视为封面」（键 config/two_page_cover，默认 false）：
    // 开启后目录第一张单独占一页，从第二张起两两拼页。仅影响双页阅览的分页，
    // 单页模式与「上一张/下一张」以外的跳转不受影响。
    bool twoPageCover() const;
    // 「仅方向键滚动」（键 config/arrow_keys_scroll，默认 false）：开启后 ←/→ 只做
    // 水平滚动、不再翻页；无滚动行程时不动也不翻页。启动时读取，改 ini 需重启。
    bool arrowKeysScroll() const;

    // 鼠标按键映射（键 config/mouse/btn_*）：值取 SettingsDialog 动作表，
    // "none" = 保留内置行为（左键平移/右键菜单/X、Y 无操作）。
    QString mouseLeftAction() const;
    QString mouseRightAction() const;
    QString mouseXAction() const;
    QString mouseYAction() const;

    // 键盘快捷键自定义（批次 D1）：13 个可配置动作各占三键
    // config/keyboard/<action>_enabled / _key / _action。_key 存 QKeySequence
    // 的 PortableText（"Ctrl+P"、"Right"…）。键存在且为空串 = 显式未绑定
    // （设置页显示「未设置」，运行期不装键）；键不存在 = 从未保存过，用出厂
    // 键位。_action 是该键位槽绑定到的动作 id（默认 = 槽位自身）。动作清单与
    // 下发语义见 MainWindow::applyShortcutConfig，设置页见键盘页。
    bool actionEnabled(const QString &action, bool def = true) const;
    bool hasActionKey(const QString &action) const;
    QString actionKey(const QString &action, const QString &def = QString()) const;
    QString actionBinding(const QString &action, const QString &def = QString()) const;

    // 命令 ID 化的键位（派单 2026-11-18 H1-7）：config/keyboard/<CMD_ID>_enabled 与
    // /<CMD_ID>_keys（QStringList，PortableText，支持一个命令多个键）。
    // 显式语义另有一枚 /<CMD_ID>_keys_set：
    //   缺 _keys_set = 从未保存过 → 运行期按 CommandCatalog 的出厂键位；
    //   _keys_set=true    = 用户已确认过绑定 → 以 _keys 为准（空表 = 显式不绑键）。
    // 旧键 config/keyboard/<槽位>_enabled/_key/_action 由构造函数一次性迁移
    // （见 Config::migrateKeyboardToCommandIds），迁移规则写进回执。
    bool commandEnabled(const QString &cmdId, bool def = true) const;
    bool commandKeysSaved(const QString &cmdId) const;
    QStringList commandKeys(const QString &cmdId) const;
    // 设置页/迁移写盘点：persist=true 时立即 sync（迁移需要落盘）。
    void setCommandKeys(const QString &cmdId, const QStringList &keys, bool enabled,
                        bool persist);

    // 「使用强锐化滤镜」：lanczosResample 缩图后叠加一层固定强度 3×3
    // unsharp mask（ImageCanvas::doRefreshSampling）。默认关。
    bool strongSharpen() const;
    // 「先显示低品质图片以加速渲染」：ImageCache::startFullDecode 的快帧
    // 预览（DCT 缩放解码先上屏，全图随后替换）。默认开。
    bool previewMode() const;

    // 图像处理
    QString interpolation() const;     // "bilinear" | "bicubic" | "lanczos"（默认 "bicubic"）
    bool stretchSmall() const;         // 适合窗口时把小于窗口的图放大填满（默认 false）
    bool hdrTonemap() const;           // HDR 色调映射（本版恒 false：启用入口已移除，旧配置不激活）
    // 顶栏 HDR 浮层（H1-8）：模式 0 自动检测 / 1 开启 / 2 关闭；曝光补偿单位档。
    // 与 hdrTonemap() 的旧布尔分开：旧键不再读取，这两项是新语义（用户选择）。
    int hdrMode() const;
    void setHdrMode(int mode);
    float hdrExposureEv() const;
    void setHdrExposureEv(float ev);
    bool imageIcc() const;             // 使用图片内嵌 ICC（非 sRGB 图转 sRGB，默认 true）
    bool monitorIcc() const;           // 显示器 ICC（SDR 验证版：ACM 关闭的屏幕做应用侧目标转换，默认 false）
    int gifTransparent() const;        // GIF 透明背景 0=默认调色板 1=白 2=黑 3=中灰 4=自定义（默认 0；批次 2 重新暴露并加自定义取色）
    QColor gifCustomColor() const;     // GIF 自定义背景色（默认 #808080，仅档位 4 消费）
    QString canvasBackdrop() const;    // 全画布背景材质 "checker"|"solid"（旧值一次性迁移，默认随主题着色）
    bool showTransparency() const;     // 「显示透明区域」（默认开：纯色背景下透明像素铺指示棋盘）
    bool canvasShadow() const;         // 图片阴影开关（默认 false，仅 32 位 alpha 非 GIF 图）
    int pageTurnFx() const;            // 翻页效果 0 无 / 1 淡入淡出 / 2 Alpha 滑动 / 3 滑动（默认 1）
    bool cacheNextImage() const;       // 缓存下一张图片（翻页前预读，默认 true）
    int cacheSizeMb() const;           // 原图缓存上限 MB（默认 300，clamp 64..4096）
    bool alignTopWhenLarge() const;    // 图片大于窗口时显示图片顶部（默认开）
    bool autoSaveRotate() const;       // 旋转后自动保存（默认关：旋转/翻转立即原子写回原文件）

    // 鼠标（翻页批次 2：按键与手势独立映射，2026-11-20）
    // 滚轮上下独立设置，单向「无」只停用该方向；中键单击与拖动分别设置。
    // 旧配置（滚轮共用缩放开关、中键 pan 捆绑单击全屏）在构造函数里按
    // 「用户此前真正得到的行为」一次性迁移到下面四个键，旧键保留供降级。
    QString wheelUpAction() const;     // 滚轮向上：none/zoom_in/zoom_out/next/prev（默认 zoom_in）
    QString wheelDownAction() const;   // 滚轮向下：同上（默认 zoom_out）
    QString middleClickAction() const; // 中键单击：点击动作表（默认 fullscreen）
    QString middleDragAction() const;  // 中键拖动：none/pan（默认 pan）
    bool sideClickPageturn() const;    // 单击屏幕两侧翻页（默认 true）
    bool dblclickMaximize() const;     // 双击最大化 / 还原（默认 true）

    // 杂项
    bool disableScreensaver() const;   // 运行期间禁用屏幕保护（默认 false）
    bool noRawLoad() const;            // 不打开 RAW 图片文件（默认 false）
    bool skipRawWhenJpg() const;       // JPEG 存在时跳过同名 RAW（默认 true）
    bool rawThumbnail() const;         // RAW 先显示内嵌缩略图（默认 true）
    bool groupRawJpg() const;
    bool groupShowRaw() const;
    // RAW+JPG 分组显示（派单 派单-RAW与JPG分组显示-20261118）：
    // 键 config/group_raw_jpg —— 同目录同主名的 RAW/PSD 与 JPG 合并为一条（默认关，
    // 关闭时目录列表与现状逐位一致）；键 config/group_show_raw —— 分组条目默认打开
    // 原始数据（RAW/PSD）而不是 JPG（默认关 = 显示 JPG，浏览更快）。
    bool fitWindowToImage() const;     // 打开图片时按图片尺寸调节窗口（默认 false，需用户开启）
    bool lockWindowSize() const;       // 锁定窗口大小（键 config/lock_window_size，默认 false，持久化）
    bool pageturnWav() const;          // 翻页时播放音效（默认 false）

    // 排序（浏览菜单）：键 0名称/1修改时间/2大小/3类型 + 降序开关（默认升序）
    int sortMode() const;
    bool sortDescending() const;

    // 上下文菜单
    bool contextCopy() const;          // 复制到剪贴板启用

    // 应用内右键菜单可见性（键 config/appmenu/<id>；目录与默认值唯一来源是
    // MenuCatalog.h）。缺键 = 首次安装 / 旧配置 / 恢复默认，一律按目录默认表出值。
    // appMenuOwnVisible 只读本节点自己那份键；appMenuVisible 返回**生效**可见性
    // = 本项开关 × 各级祖先总开关（无开关的 Fixed / Leaf 完全由祖先决定）。
    bool appMenuOwnVisible(const QString &id) const;
    bool appMenuVisible(const QString &id) const;
    // 汉堡菜单与右键菜单显隐统一开关（键 config/appmenu/unified_visibility，默认关）。
    // 关 = 两边各自独立（现状不变）；开 = 右键隐藏的共有命令在汉堡菜单同步隐藏。
    bool appMenuUnifiedVisibility() const;
    // 启动自愈（J-2 防呆）：关键入口（设置…）若被配置隐藏，回落为可见并写日志。
    void repairCriticalMenuEntries();
    // 本次启动自愈回落的关键入口 id（空 = 无需回落）；供启动后落盘取证。
    QStringList repairedCriticalEntries() const;
    // 「恢复默认菜单」：清掉全部 config/appmenu/* 键（只重置上下文菜单可见性，
    // 不动浏览模式、快捷键、图像效果等其他设置）。
    bool resetAppMenuVisibility();

    // 收藏与最近打开
    QStringList favorites() const;
    QStringList recentFiles() const;
    void toggleFavorite(const QString &path);
    int maxBookmarks() const;         // 最大书签数（默认 50，clamp 10..100）
    bool autoBookmark() const;        // 打开图片时自动加入书签（默认 false）
    void addRecentFile(const QString &path);

    // 照片文件夹（H1-1）：1–20 个可配目标文件夹；兼容旧两格 config/etc/photo_folder_1/2。
    static constexpr int kMinPhotoFolders = 1;
    static constexpr int kMaxPhotoFolders = 20;
    int photoFolderCount() const;
    QString photoFolder(int index1Based) const;
    QStringList photoFolders() const;
    void setPhotoFolders(const QStringList &folders);

    // 检查更新
    bool checkUpdatesOnStartup() const;      // 启动时自动检查（默认开，5s 延迟 / 24h 限频）
    qint64 lastUpdateCheck() const;          // 上次检查时间（epoch 秒，0 = 从未）
    void setLastUpdateCheck(qint64 secsSinceEpoch);

    // 窗口几何：五个整数键，不用 saveGeometry（base64 不可读、跨版本脆弱）。
    // 关闭时落盘、启动时读回并做离屏 clamp；x/y 允许负值（副屏在主屏左侧）。
    // hasWindowGeometry() 为 false（空 ini）时保持构造默认，等同无记录=现行为不变。
    bool hasWindowGeometry() const;
    int winX() const;
    int winY() const;
    int winW() const;
    int winH() const;
    bool winMaximized() const;
    void setWindowGeometry(int x, int y, int w, int h, bool maximized);

    // 会话恢复：上次查看文件的绝对路径（空串=无会话）。
    // 记录时机与开关无关（每次成功打开即写），是否在启动时恢复由下面的偏好决定：
    // 「设置 → 通用 → 启动 → 启动时恢复上次查看的图片」，键 config/restore_last_file，
    // 默认关闭（全新配置与缺此键的旧配置都按关闭处理）。与「记住浏览过的文件夹」
    // 「窗口位置」三份偏好各自独立，互不推导。
    QString sessionLastFile() const;
    void setSessionLastFile(const QString &path);
    bool restoreLastFileOnStart() const;

    // 遥测：恒 false（M0 直接禁死；未来若做，走 opt-in 并保留本开关）
    bool telemetryEnabled() const { return false; }

    QSettings &settings() { return m_settings; }

    // 写盘失败后 QSettings::status() 会被首个错误粘住（后续 sync 即使成功也仍
    // 返回 AccessError）。重建同一路径的 QSettings 实例可清掉该状态，供设置页
    // 在权限恢复后真正重试落盘；内存中的待写值由调用方按 m_pendingWrites 重放。
    void reopenSettings();

private:
    static QString defaultConfigPath();
    // 旧键盘槽位键 → 命令 ID 键的一次性迁移（构造函数调用，幂等）。
    void migrateKeyboardToCommandIds();

    QString m_configPath;
    QSettings m_settings;
    QStringList m_repairedCritical;
};
