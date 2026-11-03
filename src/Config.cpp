#include "Config.h"

#include <QCoreApplication>
#include <QCryptographicHash>
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

bool Config::saveZoomState() const
{
    return m_settings.value(QStringLiteral("config/view/save_zoom_state"), false).toBool();
}

// 路径 → QSettings 键名：INI 键里的 '/' 会分组、中文/空格要转义，
// 直接拿路径当键名会写散；用 MD5 十六进制最省事且与平台无关。
namespace {
QString zoomStateKey(const QString &path)
{
    const QByteArray md5 = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5);
    return QStringLiteral("config/zoom/") + QString::fromLatin1(md5.toHex());
}
} // namespace

QString Config::savedViewState(const QString &path) const
{
    if (path.isEmpty()) {
        return QString();
    }
    return m_settings.value(zoomStateKey(path)).toString();
}

void Config::setSavedViewState(const QString &path, const QString &state)
{
    if (path.isEmpty()) {
        return;
    }
    m_settings.setValue(zoomStateKey(path), state);
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

bool Config::hdrTonemap() const
{
    // 出厂默认关闭：SDR 屏上默认开色调映射会改变画质，只有 HDR 屏/明确的用户才需要。
    // 只影响没有写过该键的用户，已有配置里的值优先。
    return m_settings.value(QStringLiteral("config/hdr_tonemap"), false).toBool();
}

bool Config::cacheNextImage() const
{
    return m_settings.value(QStringLiteral("config/imageproc/cache_image"), true).toBool();
}

int Config::cacheSizeMb() const
{
    // 原图缓存上限，单位 MB；设置页范围比这宽，这里再夹一次防止手改配置越界。
    return qBound(64, m_settings.value(QStringLiteral("config/imageproc/cache_size_mb"), 300).toInt(), 4096);
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

bool Config::sideClickPageturn() const
{
    return m_settings.value(QStringLiteral("config/mouse/click_side_pageturn"), true).toBool();
}

bool Config::dblclickMaximize() const
{
    return m_settings.value(QStringLiteral("config/mouse/dblclick_maximize"), true).toBool();
}

bool Config::disableScreensaver() const
{
    return m_settings.value(QStringLiteral("config/etc/disable_screensaver"), false).toBool();
}

bool Config::fitWindowToImage() const
{
    return m_settings.value(QStringLiteral("config/etc/fit_window_to_image"), true).toBool();
}

bool Config::pageturnWav() const
{
    return m_settings.value(QStringLiteral("config/etc/pageturn_wav"), false).toBool();
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

bool Config::checkUpdatesOnStartup() const
{
    // 默认关：未经用户明确同意不联网。设置页「启动时检查更新」勾上才为 true。
    return m_settings.value(QStringLiteral("config/check_updates"), false).toBool();
}

qint64 Config::lastUpdateCheck() const
{
    // 每次检查（手动/自动）发起时都会写这个时间戳；自动检查按它做 ≥24h 频控。
    return m_settings.value(QStringLiteral("etc/last_update_check"), 0).toLongLong();
}

void Config::setLastUpdateCheck(qint64 secsSinceEpoch)
{
    m_settings.setValue(QStringLiteral("etc/last_update_check"), secsSinceEpoch);
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
