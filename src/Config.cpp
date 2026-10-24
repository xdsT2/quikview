#include "Config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

Config::Config(const QString &path)
    : m_configPath(path)
    , m_settings(defaultConfigPath(), QSettings::IniFormat)
{
}

QString Config::defaultConfigPath()
{
    // 便携模式：程序目录下存在 config.ini 则优先使用；否则用系统配置目录。
    const QString portable = QCoreApplication::applicationDirPath() + QStringLiteral("/config.ini");
    if (QFileInfo::exists(portable)) {
        return portable;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/config.ini");
}

Config &Config::instance()
{
    static Config inst;
    return inst;
}

bool Config::isPortable() const
{
    return QFileInfo(m_settings.fileName()).absolutePath()
            == QCoreApplication::applicationDirPath();
}

QString Config::language() const
{
    return m_settings.value(QStringLiteral("config/language"), QStringLiteral("system")).toString();
}

bool Config::showSelectLangDialog() const
{
    return m_settings.value(QStringLiteral("config/show_select_lang_dialog"), true).toBool();
}

bool Config::checkUpdates() const
{
    return m_settings.value(QStringLiteral("config/check_updates"), false).toBool();
}

QString Config::theme() const
{
    return m_settings.value(QStringLiteral("config/theme"), QStringLiteral("dark")).toString();
}

int Config::slideshowIntervalMs() const
{
    return m_settings.value(QStringLiteral("config/slideshow_interval_ms"), 3000).toInt();
}

bool Config::slideshowRandom() const
{
    return m_settings.value(QStringLiteral("config/slideshow_random"), false).toBool();
}

QString Config::defaultZoom() const
{
    return m_settings.value(QStringLiteral("config/default_zoom"), QStringLiteral("fit")).toString();
}

bool Config::captureWindow() const
{
    return m_settings.value(QStringLiteral("config/capture_window"), true).toBool();
}

bool Config::rememberBrowsedFolders() const
{
    return m_settings.value(QStringLiteral("config/remember_browsed_folders"), true).toBool();
}

bool Config::allowMultipleInstances() const
{
    return m_settings.value(QStringLiteral("config/allow_multiple_instances"), false).toBool();
}

bool Config::deleteConfirm() const
{
    return m_settings.value(QStringLiteral("config/delete_confirm"), true).toBool();
}

bool Config::notifyCorner() const
{
    return m_settings.value(QStringLiteral("config/notify_corner"), true).toBool();
}

bool Config::alwaysOnTop() const
{
    return m_settings.value(QStringLiteral("config/always_on_top"), false).toBool();
}

bool Config::escExit() const
{
    return m_settings.value(QStringLiteral("config/esc_exit"), true).toBool();
}

bool Config::checkMinorUpdates() const
{
    return m_settings.value(QStringLiteral("config/check_minor_updates"), false).toBool();
}

QString Config::edgeAction() const
{
    return m_settings.value(QStringLiteral("config/edge_action"), QStringLiteral("stop")).toString();
}

QString Config::thumbnailSize() const
{
    return m_settings.value(QStringLiteral("config/thumbnail_size"), QStringLiteral("medium")).toString();
}

QString Config::interpolation() const
{
    const QString v = m_settings.value(QStringLiteral("config/interpolation"),
                                       QStringLiteral("bicubic")).toString();
    // 兼容旧配置：fast → bilinear，smooth → bicubic（老版本只存这两档）
    if (v == QLatin1String("fast")) {
        return QStringLiteral("bilinear");
    }
    if (v == QLatin1String("smooth")) {
        return QStringLiteral("bicubic");
    }
    return v;
}

bool Config::stretchSmall() const
{
    return m_settings.value(QStringLiteral("config/stretch_small"), false).toBool();
}

bool Config::smoothZoom() const
{
    return m_settings.value(QStringLiteral("config/smooth_zoom"), true).toBool();
}

bool Config::wheelZoom() const
{
    return m_settings.value(QStringLiteral("config/wheel_zoom"), true).toBool();
}

bool Config::middlePan() const
{
    return m_settings.value(QStringLiteral("config/middle_pan"), true).toBool();
}

int Config::sortMode() const
{
    return m_settings.value(QStringLiteral("config/sort_mode"), 0).toInt();
}

bool Config::sortDescending() const
{
    return m_settings.value(QStringLiteral("config/sort_desc"), false).toBool();
}

bool Config::contextCopy() const
{
    return m_settings.value(QStringLiteral("config/context_copy"), true).toBool();
}

QStringList Config::favorites() const
{
    return m_settings.value(QStringLiteral("favorites")).toStringList();
}

QStringList Config::recentFiles() const
{
    return m_settings.value(QStringLiteral("recent_files")).toStringList();
}

void Config::toggleFavorite(const QString &path)
{
    QStringList favs = favorites();
    if (favs.contains(path)) {
        favs.removeAll(path);
    } else {
        favs.prepend(path);
    }
    m_settings.setValue(QStringLiteral("favorites"), favs);
}

void Config::addRecentFile(const QString &path)
{
    QStringList recent = recentFiles();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) {
        recent.removeLast();
    }
    m_settings.setValue(QStringLiteral("recent_files"), recent);
}
