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

    // 一般
    bool captureWindow() const;        // 捕捉窗口
    bool rememberBrowsedFolders() const;  // 记住浏览过的文件夹（默认 true）
    bool allowMultipleInstances() const;  // 允许多个实例（默认 false）
    bool deleteConfirm() const;        // 删除文件时确认（默认 true）
    bool notifyCorner() const;         // 在边角显示通知信息
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

    // 鼠标
    bool wheelZoom() const;            // 滚轮缩放
    bool middlePan() const;            // 中键拖拽平移

    // 排序（浏览菜单）：键 0名称/1修改时间/2大小/3类型 + 降序开关（默认升序）
    int sortMode() const;
    bool sortDescending() const;

    // 上下文菜单
    bool contextCopy() const;          // 复制到剪贴板启用

    // 收藏与最近打开
    QStringList favorites() const;
    QStringList recentFiles() const;
    void toggleFavorite(const QString &path);
    void addRecentFile(const QString &path);

    // 遥测：恒 false（M0 直接禁死；未来若做，走 opt-in 并保留本开关）
    bool telemetryEnabled() const { return false; }

    QSettings &settings() { return m_settings; }

private:
    static QString defaultConfigPath();

    QString m_configPath;
    QSettings m_settings;
};
