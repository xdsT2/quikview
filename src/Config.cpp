#include "Config.h"

#include <new>

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
    // 冒烟/截图隔离（M-07）：main() 检测到测试参数时用环境变量指定临时配置，
    // 这里的优先级最高——高于便携模式，保证真实 config 不被读写。
    const QByteArray testConfig = qgetenv("QUIKVIEW_CONFIG_PATH");
    if (!testConfig.isEmpty()) {
        return QString::fromLocal8Bit(testConfig);
    }

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

void Config::reopenSettings()
{
    const QString fileName = m_settings.fileName();
    QSettings *p = &m_settings;
    p->~QSettings();
    new (p) QSettings(fileName, QSettings::IniFormat);
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
    // 第 18 期起三档："dark" | "light" | "system"（跟随 Windows 深浅色）。
    // 旧值只有 dark/light，天然兼容；未知值由 Theme::resolveMode 按深色处理。
    return m_settings.value(QStringLiteral("config/theme"), QStringLiteral("dark")).toString();
}

bool Config::blurMaterial() const
{
    // 第 18 期④：磨砂材质。默认关——只在右键菜单与信息浮层上尝试 DWM Acrylic，
    // 不支持的机器退回 QSS 半透明（polishMenu 会静默失败，不报错）。
    return m_settings.value(QStringLiteral("config/ui/blur_material"), false).toBool();
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

bool Config::rememberBrowsedFolders() const
{
    return m_settings.value(QStringLiteral("config/remember_browsed_folders"), true).toBool();
}

bool Config::allowMultipleInstances() const
{
    // 2026-11-12 定稿默认：默认允许多开（设置页「不允许多个实例」显示为关）。
    return m_settings.value(QStringLiteral("config/allow_multiple_instances"), true).toBool();
}

bool Config::deleteConfirm() const
{
    return m_settings.value(QStringLiteral("config/delete_confirm"), true).toBool();
}

bool Config::notifyCorner() const
{
    return m_settings.value(QStringLiteral("config/notify_corner"), true).toBool();
}

bool Config::showTooltip() const
{
    // 设置页「不显示鼠标悬浮工具提示」是反选（inverted），key 存正向语义：
    // true = 显示提示（默认）。消费点在 TooltipHint::hover 与主窗口 ToolTip 事件过滤。
    return m_settings.value(QStringLiteral("config/view/show_tooltip"), true).toBool();
}

bool Config::showAnimControl() const
{
    // 设置页「不显示动画信息控制」是反选（inverted），key 存正向语义：
    // true = 显示动图帧信息（默认）。消费点在 MainWindow::updateInfoOverlay。
    return m_settings.value(QStringLiteral("config/view/show_anim_control"), true).toBool();
}

bool Config::showZoomControl() const
{
    // 设置页「不显示缩放信息控制」是反选（inverted），key 存正向语义：
    // true = 显示左下角缩放浮标（默认）。消费点在 MainWindow 的 zoomChanged。
    return m_settings.value(QStringLiteral("config/view/show_zoom_control"), true).toBool();
}

bool Config::exifSmallFont() const
{
    // 设置页「用小字体显示 EXIF / 文件信息」：true = 10px，默认 12px。
    // 消费点：MetadataPanel::setSmallFont 与 StatusOverlay::setSmallFont。
    return m_settings.value(QStringLiteral("config/view/exif_small_font"), false).toBool();
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
    // 2026-11-12 定稿默认：到达末尾继续时回绕到首个文件。
    // 旧配置里的 "ask" 在「询问」实现落地（翻页批次 2）前按「停止」处理：
    // 目录加载器 next/prev 只认 stop，ask 会绕回另一端，与 advance 不一致，
    // 这里统一折算，配置原值保留，不回写。
    const QString v = m_settings.value(QStringLiteral("config/edge_action"),
                                        QStringLiteral("first")).toString();
    return v == QLatin1String("ask") ? QStringLiteral("stop") : v;
}

QString Config::thumbnailSize() const
{
    return m_settings.value(QStringLiteral("config/thumbnail_size"), QStringLiteral("medium")).toString();
}

// backgroundImage()/backgroundImagePath() 已随第 18 期「背景」段下线删除：
// 消费点 MainWindow::applyRuntimeSettings 改为固定画布外观，不再读这四个键。

bool Config::overlayFilename() const
{
    // 「顶端菜单隐藏时显示文件名 (Shift+TAB)」：MainWindow::keyPressEvent 在
    // Shift+TAB 且胶囊已隐藏时调用 showNameOverlay()，浮层 3s 后淡出。
    return m_settings.value(QStringLiteral("config/view/overlay_filename"), false).toBool();
}

QString Config::mouseLeftAction() const
{
    // 左键映射（C9）。none 保留内置行为：拖动平移 + 边缘单击翻页 + 双击最大化。
    return m_settings.value(QStringLiteral("config/mouse/btn_left"),
                            QStringLiteral("none")).toString();
}

namespace {
// 右键 / X / Y 没有 "pan" 的 onMouseAction 分支（设置页该档已灰显）：旧配置里
// 残留的 "pan" 按下只会吞掉事件——右键连内置菜单都不弹。读取时折算回内置
// 行为 "none"，让运行态与设置页显示的安全值一致；配置原值不回写不删除，
// 翻页批次 2 的独立映射迁移仍能看到用户存过的原始档位。
QString mouseBtnAction(const QSettings &s, const char *key)
{
    const QString v = s.value(QStringLiteral("config/mouse/") + key,
                              QStringLiteral("none")).toString();
    return v == QLatin1String("pan") ? QStringLiteral("none") : v;
}
} // namespace

QString Config::mouseRightAction() const
{
    // 右键映射（C10）。none 保留内置行为（弹出上下文菜单）。
    return mouseBtnAction(m_settings, "btn_right");
}

QString Config::mouseXAction() const
{
    // X 键（XButton1，浏览器后退键）映射（C11）；默认无操作。
    return mouseBtnAction(m_settings, "btn_x");
}

QString Config::mouseYAction() const
{
    // Y 键（XButton2，浏览器前进键）映射（C12）；默认无操作。
    return mouseBtnAction(m_settings, "btn_y");
}

bool Config::actionEnabled(const QString &action, bool def) const
{
    return m_settings.value(QStringLiteral("config/keyboard/%1_enabled").arg(action), def).toBool();
}

bool Config::hasActionKey(const QString &action) const
{
    return m_settings.contains(QStringLiteral("config/keyboard/%1_key").arg(action));
}

QString Config::actionKey(const QString &action, const QString &def) const
{
    return m_settings.value(QStringLiteral("config/keyboard/%1_key").arg(action), def).toString();
}

QString Config::actionBinding(const QString &action, const QString &def) const
{
    return m_settings.value(QStringLiteral("config/keyboard/%1_action").arg(action), def).toString();
}

bool Config::strongSharpen() const
{
    // 「使用强锐化滤镜」（C13）：消费点 ImageCanvas::doRefreshSampling，
    // lanczos 缩图后叠加固定强度 3×3 unsharp mask。
    return m_settings.value(QStringLiteral("config/imageproc/strong_sharpen"), false).toBool();
}

bool Config::previewMode() const
{
    // 「先显示低品质图片以加速渲染」（C14）：消费点 ImageCache::startFullDecode
    // 的快帧预览块——关闭时跳过低品质先出帧，直接等全图一次上屏。
    return m_settings.value(QStringLiteral("config/imageproc/preview_mode"), true).toBool();
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
    // 本版已移除 HDR 启用入口（右键「图片参数」项，批次 1）：色调映射只在部分
    // 高位深图上转 SDR，界面却提示「HDR 已开启」，与实际不符。此处一律返回 false，
    // 旧配置里残留的 config/hdr_tonemap=true 在本版也无法激活；该键不再被读取，
    // 也不回写不删除，后续真 HDR 输出落地时再按新语义接回。
    return false;
}

bool Config::imageIcc() const
{
    // 「使用图片文件的 ICC 设置」：MainWindow::showImage 在显示链入口把带
    // 非 sRGB 内嵌 ICC 的图转到 sRGB；无 ICC / 已是 sRGB 的图直通。
    return m_settings.value(QStringLiteral("config/imageproc/image_icc"), true).toBool();
}

int Config::gifTransparent() const
{
    // 「为 GIF 图片使用透明背景」：0 默认调色板（不铺底）/ 1 白 / 2 黑 / 3 自定义。
    // 消费点 MainWindow::applyGifBackdrop（仅 GIF 路径）。
    return qBound(0, m_settings.value(QStringLiteral("config/imageproc/gif_transparent"), 0).toInt(), 3);
}

int Config::img32Transparent() const
{
    // 「为32位图片使用透明背景」：0 网格 / 1 白 / 2 黑 / 3 深色网格。
    // 消费点 MainWindow::applyImage32Backdrop（非 GIF 的带 alpha 图）。
    return qBound(0, m_settings.value(QStringLiteral("config/imageproc/img32_transparent"), 0).toInt(), 3);
}

int Config::pageTurnFx() const
{
    // 「翻页效果」：MainWindow::showImage 在整图路径判断是否换页后调用
    // ImageCanvas::setPixmapAnimated；拖动滑块跟手预览（preview=true）不经过。
    return qBound(0, m_settings.value(QStringLiteral("config/imageproc/pageturn_fx"), 1).toInt(), 3);
}

bool Config::cacheNextImage() const
{
    return m_settings.value(QStringLiteral("config/imageproc/cache_image"), true).toBool();
}

bool Config::alignTopWhenLarge() const
{
    return m_settings.value(QStringLiteral("config/imageproc/align_top_when_large"), true).toBool();
}

int Config::cacheSizeMb() const
{
    // 原图缓存上限，单位 MB；与设置页输入框同范围（64–4096），这里再夹一次
    // 防止手改配置越界，旧配置越界值按边界生效并在设置页按边界值显示。
    return qBound(64, m_settings.value(QStringLiteral("config/imageproc/cache_size_mb"), 300).toInt(), 4096);
}

bool Config::smoothZoom() const
{
    return m_settings.value(QStringLiteral("config/smooth_zoom"), true).toBool();
}

bool Config::wheelZoom() const
{
    // 滚轮上下共用一个缩放开关：任一方向不是「放大/缩小」对应档（含旧配置里
    // 不可执行的档位）就整体停用，与设置页写回的兼容桥同一公式。这里直接按
    // mouse/wheel_up|down 推导而不是回读 config/wheel_zoom，避免手改或旧版
    // 残留的开关键与档位键不一致时，运行态和设置页显示各说各话。
    const QString up = m_settings.value(QStringLiteral("config/mouse/wheel_up"),
                                        QStringLiteral("zoom_in")).toString();
    const QString down = m_settings.value(QStringLiteral("config/mouse/wheel_down"),
                                          QStringLiteral("zoom_out")).toString();
    return up == QLatin1String("zoom_in") && down == QLatin1String("zoom_out");
}

bool Config::middlePan() const
{
    // 与 wheelZoom 同理按档位键推导：中键只有「拖拽平移」档生效（含单击全屏），
    // 其余档位（含旧配置残留的不可执行值）连单击全屏一起停用。
    return m_settings.value(QStringLiteral("config/mouse/btn_middle"),
                            QStringLiteral("pan")).toString() == QLatin1String("pan");
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

bool Config::noRawLoad() const
{
    // 「不打开 RAW 图片文件」：true 时 MainWindow::onLoadFailed 跳过 libraw 兜底，
    // 直接给出「已在设置中禁用 RAW 解码」提示。
    return m_settings.value(QStringLiteral("config/etc/no_raw_load"), false).toBool();
}

bool Config::skipRawWhenJpg() const
{
    // 「打开 JPEG 时不同时打开同名 RAW」：消费点 DirectoryLoader::open 的扫描过滤。
    return m_settings.value(QStringLiteral("config/etc/skip_raw_when_jpg"), true).toBool();
}

bool Config::rawThumbnail() const
{
    // 「打开 RAW 图片文件时，显示已有缩略图」：消费点 ImageCache::startFullDecode
    // 的后台解码线程（先 libraw unpack_thumb 上屏，再解全图）。
    return m_settings.value(QStringLiteral("config/etc/raw_thumbnail"), true).toBool();
}

bool Config::fitWindowToImage() const
{
    // 2026-11-12 用户口径：窗口尺寸默认不随打开的图片变化，保持初始 1080×815。
    // 设置「根据图片大小调节窗口大小」仍保留，用户显式开启后才按图调窗。
    return m_settings.value(QStringLiteral("config/etc/fit_window_to_image"), false).toBool();
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
    // 2026-11-12 用户定稿：启动时检查更新默认开。启动 5s 静默发起、24h 限频；
    // 联网行为仍由该开关控制，用户可在设置页随时关闭。
    return m_settings.value(QStringLiteral("config/check_updates"), true).toBool();
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

int Config::maxBookmarks() const
{
    // 设置页档位 10/20/30/50/100；clamp 一次防手改配置越界。
    return qBound(10, m_settings.value(QStringLiteral("config/bookmark/max_bookmarks"), 50).toInt(), 100);
}

bool Config::autoBookmark() const
{
    // 设置页「自动保存当前图片路径为书签」：只在 MainWindow::openFile 路径消费，
    // 翻页不触发，避免切图时刷满书签。
    return m_settings.value(QStringLiteral("config/bookmark/auto_bookmark"), false).toBool();
}

void Config::toggleFavorite(const QString &path)
{
    QStringList favs = favorites();
    if (favs.contains(path)) {
        favs.removeAll(path);
    } else {
        favs.prepend(path);
        // 最大书签数（键 config/bookmark/max_bookmarks）：超限从尾部淘汰 ——
        // favs 是新→旧序，尾部就是最早的书签，与「最近文件」同一淘汰方向。
        const int cap = maxBookmarks();
        while (favs.size() > cap) {
            favs.removeLast();
        }
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
