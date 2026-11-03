#pragma once

#include <QSettings>

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
    bool showSelectLangDialog() const;
    QString theme() const;   // "dark" | "light"
    int slideshowIntervalMs() const;  // 默认 3000
    bool slideshowRandom() const;     // 幻灯片随机播放（默认 false）
    QString defaultZoom() const;      // "fit" | "actual"
    bool saveZoomState() const;       // 打开图片时恢复上次缩放（默认 false）

    // 按路径记忆的视图状态（键 config/zoom/<pathHash>，仅 save_zoom_state 开启时读写）。
    // 值："fit" / "width" / 纯数字缩放比；空串 = 该路径没有记录。
    QString savedViewState(const QString &path) const;
    void setSavedViewState(const QString &path, const QString &state);

    // 一般
    bool captureWindow() const;        // 捕捉窗口
    bool rememberBrowsedFolders() const;  // 记住浏览过的文件夹（默认 true）
    bool allowMultipleInstances() const;  // 允许多个实例（默认 false）
    bool deleteConfirm() const;        // 删除文件时确认（默认 true）
    bool notifyCorner() const;         // 在边角显示通知信息
    bool showTooltip() const;          // 悬浮工具提示总开关（默认显示）
    bool showAnimControl() const;      // 动图帧信息（第 x/y 帧）是否显示（默认显示）
    bool showZoomControl() const;      // 缩放百分比浮标是否显示（默认显示）
    bool exifSmallFont() const;        // EXIF / 文件信息用小字体（默认 12px，开则 10px）
    bool alwaysOnTop() const;          // 总在最前
    bool escExit() const;              // 按 ESC 退出
    QString edgeAction() const;        // "first" | "stop" | "ask"（默认 "stop"）

    // 浏览
    QString thumbnailSize() const;     // "small" | "medium" | "large"（默认 "medium"）

    // 图像处理
    QString interpolation() const;     // "bilinear" | "bicubic" | "lanczos"（默认 "bicubic"）
    bool smoothZoom() const;           // 默认平滑缩放
    bool stretchSmall() const;         // 适合窗口时把小于窗口的图放大填满（默认 false）
    bool hdrTonemap() const;           // HDR 色调映射（高位深→SDR，命令栏 HDR 钮，默认 true）
    bool imageIcc() const;             // 使用图片内嵌 ICC（非 sRGB 图转 sRGB，默认 true）
    int gifTransparent() const;        // GIF 透明区域底色档位 0..3（默认 0）
    int img32Transparent() const;      // 32 位图（带 alpha）底色档位 0..3（默认 0）
    bool cacheNextImage() const;       // 缓存下一张图片（翻页前预读，默认 true）
    int cacheSizeMb() const;           // 原图缓存上限 MB（默认 300，clamp 64..4096）
    bool alignTopWhenLarge() const;    // 图片大于窗口时显示图片顶部（默认开）

    // 鼠标
    bool wheelZoom() const;            // 滚轮缩放
    bool middlePan() const;            // 中键拖拽平移
    bool sideClickPageturn() const;    // 单击屏幕两侧翻页（默认 true）
    bool dblclickMaximize() const;     // 双击最大化 / 还原（默认 true）

    // 杂项
    bool disableScreensaver() const;   // 运行期间禁用屏幕保护（默认 false）
    bool noRawLoad() const;            // 不打开 RAW 图片文件（默认 false）
    bool skipRawWhenJpg() const;       // JPEG 存在时跳过同名 RAW（默认 true）
    bool rawThumbnail() const;         // RAW 先显示内嵌缩略图（默认 true）
    bool fitWindowToImage() const;     // 打开图片时按图片尺寸调节窗口（默认 true）
    bool pageturnWav() const;          // 翻页时播放音效（默认 false）

    // 排序（浏览菜单）：键 0名称/1修改时间/2大小/3类型 + 降序开关（默认升序）
    int sortMode() const;
    bool sortDescending() const;

    // 上下文菜单
    bool contextCopy() const;          // 复制到剪贴板启用

    // 收藏与最近打开
    QStringList favorites() const;
    QStringList recentFiles() const;
    void toggleFavorite(const QString &path);
    int maxBookmarks() const;         // 最大书签数（默认 50，clamp 10..100）
    bool autoBookmark() const;        // 打开图片时自动加入书签（默认 false）
    void addRecentFile(const QString &path);

    // 检查更新
    bool checkUpdatesOnStartup() const;      // 启动时自动检查（默认关：不擅自联网）
    qint64 lastUpdateCheck() const;          // 上次检查时间（epoch 秒，0 = 从未）
    void setLastUpdateCheck(qint64 secsSinceEpoch);

    // 遥测：恒 false（M0 直接禁死；未来若做，走 opt-in 并保留本开关）
    bool telemetryEnabled() const { return false; }

    QSettings &settings() { return m_settings; }

private:
    static QString defaultConfigPath();

    QString m_configPath;
    QSettings m_settings;
};
