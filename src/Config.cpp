#include "Config.h"

#include "CommandCatalog.h"
#include "MenuCatalog.h"
#include "quik_config_keys.h"

#include <new>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QStandardPaths>

Config::Config(const QString &path)
    : m_configPath(path)
    , m_settings(defaultConfigPath(), QSettings::IniFormat)
{
    const QString danger = QStringLiteral("config/appmenu/group_danger");
    const QString legacyDelete = QStringLiteral("config/appmenu/delete_file");
    if (!m_settings.contains(danger) && m_settings.contains(legacyDelete)) {
        m_settings.setValue(danger, m_settings.value(legacyDelete).toBool());
        m_settings.sync();
    }

    // 上下文菜单 6 个开关（2026-11-19）：旧版设置页把值写到 config/<leaf>，
    // 漏了 ctxmenu 层级，而壳扩展读的是 [config] ctxmenu\<leaf>，用户关掉的
    // 项从未生效。这里一次性把旧键搬到正确层级并清掉孤儿键；打标记，避免
    // 重复迁移覆盖用户此后在设置页做的选择。旧键读不删会让「误导性孤儿键」
    // 永远留在 ini 里，所以迁移时一律 remove。
    const QString ctxMigrated = QStringLiteral("config/etc/ctxmenu_keys_migrated");
    if (!m_settings.value(ctxMigrated, false).toBool()) {
        // 键名来自 shared/quik_config_keys.h（与壳扩展 prefs.cpp 同一份字面量），
        // 避免这边改名、那边忘改导致「设置改了右键不变」。
        for (int i = 0; i < int(quikcfg::CtxLeafCount); ++i) {
            const quikcfg::CtxLeaf leaf = quikcfg::CtxLeaf(i);
            const QString name = QString::fromWCharArray(quikcfg::CtxLeafName(leaf));
            const QString legacy = QStringLiteral("config/") + name;
            const QString target = QString::fromWCharArray(
                                       quikcfg::CtxSettingsKey(leaf).c_str());
            if (!m_settings.contains(target) && m_settings.contains(legacy)) {
                m_settings.setValue(target, m_settings.value(legacy));
            }
            m_settings.remove(legacy);
        }
        m_settings.setValue(ctxMigrated, true);
        m_settings.sync();
    }

    // 鼠标手势独立映射（翻页批次 2，2026-11-20）：旧版滚轮上下共用一个缩放
    // 开关（任一方向非对应档即双向停用），中键「拖拽平移」档捆绑单击全屏。
    // 升级按「用户此前真正得到的行为」一次性迁移：滚轮只有两档都在缩放位
    // 才真的能缩放，否则双向都停；中键只有 pan 档才有平移+单击，否则全停。
    // 迁移覆盖写入四个新键（旧键保留供降级回退），打标记避免重复迁移覆盖
    // 用户此后在设置页做的独立选择。
    const QString mouseMigrated = QStringLiteral("config/etc/mouse_gestures_migrated");
    if (!m_settings.value(mouseMigrated, false).toBool()) {
        const QString up = m_settings.value(QStringLiteral("config/mouse/wheel_up"),
                                            QStringLiteral("zoom_in")).toString();
        const QString down = m_settings.value(QStringLiteral("config/mouse/wheel_down"),
                                              QStringLiteral("zoom_out")).toString();
        const bool wheelZoomWorked = up == QLatin1String("zoom_in")
                                     && down == QLatin1String("zoom_out");
        const bool middleWorked =
            m_settings.value(QStringLiteral("config/mouse/btn_middle"),
                             QStringLiteral("pan")).toString() == QLatin1String("pan");
        m_settings.setValue(QStringLiteral("config/mouse/wheel_up"),
                            wheelZoomWorked ? QStringLiteral("zoom_in") : QStringLiteral("none"));
        m_settings.setValue(QStringLiteral("config/mouse/wheel_down"),
                            wheelZoomWorked ? QStringLiteral("zoom_out") : QStringLiteral("none"));
        m_settings.setValue(QStringLiteral("config/mouse/btn_middle_click"),
                            middleWorked ? QStringLiteral("fullscreen") : QStringLiteral("none"));
        m_settings.setValue(QStringLiteral("config/mouse/btn_middle_drag"),
                            middleWorked ? QStringLiteral("pan") : QStringLiteral("none"));
        m_settings.setValue(mouseMigrated, true);
        m_settings.sync();
    }

    migrateKeyboardToCommandIds();
}

// 键盘槽位 → 命令 ID 的一次性迁移（派单 2026-11-18 H1-7）。
// 旧语义（MainWindow::applyShortcutConfig 批次 D1）：
//   <槽位>_enabled 缺省 true；<槽位>_key 缺 = 出厂完整键位；_key 存在且等于出厂
//   主键且 _action==槽位 = 仍按出厂完整键位（副键 PageUp/Backspace/* 保留）；
//   其余 _key = 只装这一个键（清空串 = 显式不绑）；_action != 槽位 = 该键贡献给
//   目标动作。
// 迁移规则（逐槽，结果按「目标命令」聚合）：
//   1) 槽位既无 _enabled 又无 _key → 用户从未动过，不写新键（运行期仍取出厂表，
//      与「出厂行」语义一致，方向键滚动让位逻辑不受影响）；
//   2) 有 _key 且等值出厂主键且绑自身 → 写出厂完整键位表；
//   3) 其余有 _key → 写该单键（空串写成空表 = 显式不绑）；
//   4) 有 _enabled 但无 _key → 写出厂完整键位表，同时写 _enabled；
//   5) _action 指向别的槽位 → 键贡献给目标命令（目标命令的 _enabled = 任一贡献
//      槽位启用；旧版逐槽启用只影响该槽自己的贡献，这里做了等价折叠）。
// 迁移后旧键原样保留（供降级回退），打 config/etc/keyboard_cmd_ids_migrated 标记，
// 之后一律以命令 ID 键为准，不重复迁移。
void Config::migrateKeyboardToCommandIds()
{
    const QString marker = QStringLiteral("config/etc/keyboard_cmd_ids_migrated");
    if (m_settings.value(marker, false).toBool()) {
        return;
    }

    QHash<QString, QStringList> keysByCmd;
    QHash<QString, bool> enabledByCmd;   // 任一贡献槽位启用即为 true
    QStringList touched;

    for (const CommandCatalog::Entry &e : CommandCatalog::kEntries) {
        const QString slot = CommandCatalog::legacySlotOf(e);
        if (slot.isEmpty()) {
            continue;
        }
        const QString enabledKey = QStringLiteral("config/keyboard/%1_enabled").arg(slot);
        const QString keyKey = QStringLiteral("config/keyboard/%1_key").arg(slot);
        const QString actionKey = QStringLiteral("config/keyboard/%1_action").arg(slot);
        const bool hasEnabled = m_settings.contains(enabledKey);
        const bool hasKey = m_settings.contains(keyKey);
        if (!hasEnabled && !hasKey) {
            continue;   // 从未动过 → 出厂表，不写新键
        }

        const bool enabled = m_settings.value(enabledKey, true).toBool();
        const QString stored = m_settings.value(keyKey).toString();
        const QString binding = m_settings.value(actionKey, slot).toString();

        QString cmd = QString::fromLatin1(e.id);
        if (binding != slot) {
            const CommandCatalog::Entry *target = CommandCatalog::findLegacy(binding);
            if (target) {
                cmd = QString::fromLatin1(target->id);
            }
        }

        QStringList keys;
        const QStringList factory = CommandCatalog::factoryKeys(e);
        const QKeySequence storedSeq =
            QKeySequence::fromString(stored, QKeySequence::PortableText);
        const QKeySequence factoryPrimary =
            factory.isEmpty()
                ? QKeySequence()
                : QKeySequence::fromString(factory.first(), QKeySequence::PortableText);
        if (!hasKey) {
            keys = factory;
        } else if (binding == slot && !stored.isEmpty() && storedSeq == factoryPrimary
                   && !factoryPrimary.isEmpty()) {
            keys = factory;   // 配回出厂主键：恢复出厂完整表（含副键）
        } else if (!stored.isEmpty()) {
            keys.append(stored);
        }

        if (!touched.contains(cmd)) {
            touched.append(cmd);
            keysByCmd.insert(cmd, QStringList());
            enabledByCmd.insert(cmd, false);
        }
        if (enabled) {
            QStringList &dst = keysByCmd[cmd];
            for (const QString &k : keys) {
                if (!dst.contains(k)) {
                    dst.append(k);
                }
            }
            enabledByCmd[cmd] = true;
        }
    }

    if (!touched.isEmpty()) {
        for (const QString &cmd : touched) {
            const QString base = QStringLiteral("config/keyboard/%1").arg(cmd);
            m_settings.setValue(base + QStringLiteral("_enabled"), enabledByCmd.value(cmd));
            m_settings.setValue(base + QStringLiteral("_keys_set"), true);
            m_settings.setValue(base + QStringLiteral("_keys"), keysByCmd.value(cmd));
        }
    }
    m_settings.setValue(marker, true);
    m_settings.sync();
}

bool Config::commandEnabled(const QString &cmdId, bool def) const
{
    return m_settings
        .value(QStringLiteral("config/keyboard/%1_enabled").arg(cmdId), def)
        .toBool();
}

bool Config::commandKeysSaved(const QString &cmdId) const
{
    return m_settings
        .value(QStringLiteral("config/keyboard/%1_keys_set").arg(cmdId), false)
        .toBool();
}

QStringList Config::commandKeys(const QString &cmdId) const
{
    QStringList out;
    const QVariant v =
        m_settings.value(QStringLiteral("config/keyboard/%1_keys").arg(cmdId));
    for (const QString &raw : v.toStringList()) {   // 空串 = 该位无键，直接跳过
        const QString t = raw.trimmed();
        if (!t.isEmpty()) {
            out.append(t);
        }
    }
    return out;
}

void Config::setCommandKeys(const QString &cmdId, const QStringList &keys, bool enabled,
                            bool persist)
{
    const QString base = QStringLiteral("config/keyboard/%1").arg(cmdId);
    m_settings.setValue(base + QStringLiteral("_keys_set"), true);
    m_settings.setValue(base + QStringLiteral("_keys"), keys);
    m_settings.setValue(base + QStringLiteral("_enabled"), enabled);
    if (persist) {
        m_settings.sync();
    }
}

QString Config::defaultConfigPath()
{
    // 冒烟/截图隔离（M-07）：main() 检测到测试参数时用环境变量指定临时配置，
    // 这里的优先级最高——高于便携模式，保证真实 config 不被读写。
    const QByteArray testConfig = qgetenv(quikcfg::kConfigPathEnvVarA);
    if (!testConfig.isEmpty()) {
        return QString::fromLocal8Bit(testConfig);
    }

    // 便携模式：程序目录下存在 config.ini 则优先使用；否则用系统配置目录。
    // 文件名与目录层级都取自 shared/quik_config_keys.h，与壳扩展 prefs.cpp 一致。
    const QString configName =
        QString::fromWCharArray(quikcfg::kConfigFileName);
    const QString portable =
        QCoreApplication::applicationDirPath() + QLatin1Char('/') + configName;
    if (QFileInfo::exists(portable)) {
        return portable;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + configName;
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
    // 翻页批次 2（2026-11-20）：「询问」落地——手动翻到边界弹一次确认，
    // 确认后从另一端继续，取消留在当前图片（MainWindow::askEdgeWrap）。
    // 旧配置 "ask" 自本批起按新定义直接生效，不再折算为 stop；未知值回落 first。
    const QString v = m_settings.value(QStringLiteral("config/edge_action"),
                                        QStringLiteral("first")).toString();
    if (v == QLatin1String("stop") || v == QLatin1String("ask")) {
        return v;
    }
    return QStringLiteral("first");
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

bool Config::autoSaveRotate() const
{
    // 「旋转后自动保存」（A2）：默认关。开启后旋转/翻转立即原子写回原文件，
    // 消费点 MainWindow::maybeAutoSaveGeometry；每次操作实时读键，改开关即时生效。
    return m_settings.value(QStringLiteral("config/auto_save_rotate"), false).toBool();
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

bool Config::monitorIcc() const
{
    // 「使用显示器 ICC」（SDR 验证版，翻页批次 2）：ImageCanvas::toMonitorSpace
    // 在显示链出口把 sRGB 图转到当前屏幕关联 profile；ACM 开启的屏幕跳过
    // （系统合成已转换，避免双重转换）。默认关，验证达标后再启用设置入口。
    return m_settings.value(QStringLiteral("config/imageproc/monitor_icc"), false).toBool();
}

int Config::gifTransparent() const
{
    // GIF 透明背景铺底档（0 默认调色板 / 1 白 / 2 黑 / 3 中灰 / 4 自定义），
    // 消费点 MainWindow::applyGifBackdrop（仅 GIF 路径）。UI-001 §3.8.1 曾把
    // 该下拉移出设置页仅留内部读取；翻页批次 2（2026-11-20，派单-HDR入口与
    // 设置功能缺口）按单内范围重新暴露并补齐自定义取色。旧值 0..3 语义不变。
    return qBound(0, m_settings.value(QStringLiteral("config/imageproc/gif_transparent"), 0).toInt(), 4);
}

QColor Config::gifCustomColor() const
{
    // 自定义铺底色独立存键（imageproc/gif_custom_color），切回其他档再切回
    // 自定义不丢用户选色；非法值回落中灰 #808080。
    const QString v = m_settings.value(QStringLiteral("config/imageproc/gif_custom_color"),
                                       QStringLiteral("#808080")).toString();
    const QColor c(v);
    return c.isValid() ? c : QColor(128, 128, 128);
}

QString Config::canvasBackdrop() const
{
    // 全画布背景材质（UI-001 §3.8）："checker"（棋盘）| "solid"（纯色），
    // 颜色由主题令牌决定，用户不再单选黑白。旧值一次性迁移：
    // img32_transparent 0（网格）/3（深色网格）→ checker，1（白）/2（黑）→
    // solid，未知值回落 checker；迁移打标记，重复启动不二次覆盖用户后续
    // 选择（§3.8.4）。旧键保留不删，供降级回退旧版本使用。
    if (!m_settings.value(QStringLiteral("config/imageproc/canvas_bg_migrated"), false).toBool()) {
        // 迁移写在 const 读口里：QSettings 本就是可变缓存句柄，const_cast
        // 只为保持全部消费点签名不变（一次性，之后走普通读取）。
        auto &s = const_cast<QSettings &>(m_settings);
        const int legacy = s.value(QStringLiteral("config/imageproc/img32_transparent"), 0).toInt();
        const QString migrated = (legacy == 1 || legacy == 2)
                                     ? QStringLiteral("solid") : QStringLiteral("checker");
        s.setValue(QStringLiteral("config/imageproc/canvas_bg"), migrated);
        s.setValue(QStringLiteral("config/imageproc/canvas_bg_migrated"), true);
    }
    const QString v = m_settings.value(QStringLiteral("config/imageproc/canvas_bg"),
                                       QStringLiteral("checker")).toString();
    return v == QLatin1String("solid") ? QStringLiteral("solid") : QStringLiteral("checker");
}

bool Config::showTransparency() const
{
    // 「显示透明区域」（UI-001 §3.8 返工三控件之二）：默认开——纯色背景下
    // 透明像素在图片矩形内铺指示棋盘，帮用户看清透明范围；关闭时透明像素
    // 直接透出全画布背景。
    return m_settings.value(QStringLiteral("config/imageproc/show_transparency"), true).toBool();
}

bool Config::canvasShadow() const
{
    // 「图片阴影」（UI-001 §3.8.3）：旧用户与全新安装均默认 false，
    // 避免升级后突然改变图片观感。
    return m_settings.value(QStringLiteral("config/imageproc/canvas_shadow"), false).toBool();
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

// 滚轮支持档（翻页批次 2）：无操作 / 放大 / 缩小 / 翻下一张 / 翻上一张。
// 翻页走与点击动作同一个派发器（MainWindow::onMouseAction），连滚即连翻。
static bool wheelActionSupported(const QString &v)
{
    return v == QLatin1String("none") || v == QLatin1String("zoom_in")
           || v == QLatin1String("zoom_out") || v == QLatin1String("next")
           || v == QLatin1String("prev");
}

bool Config::twoPageCover() const
{
    // 双页阅览分页规则开关：默认 false = 现状（第 1+2、3+4… 两两拼页）。
    // 这一项每次拼页/翻页时读一次，手改 ini 无需重启。
    return m_settings.value(QStringLiteral("config/two_page_cover"), false).toBool();
}

bool Config::arrowKeysScroll() const
{
    // 只在启动装键盘时读一次：改 ini 需重启生效（不跟随设置页即时监听）。
    return m_settings.value(QStringLiteral("config/arrow_keys_scroll"), false).toBool();
}

QString Config::wheelUpAction() const
{
    // 上下独立：单向「无」只停用该方向。旧配置残留的不可执行档位在构造
    // 迁移里已按有效行为折算；这里再兜一次底，未知值按 none（不吞事件）。
    const QString v = m_settings.value(QStringLiteral("config/mouse/wheel_up"),
                                        QStringLiteral("zoom_in")).toString();
    return wheelActionSupported(v) ? v : QStringLiteral("none");
}

QString Config::wheelDownAction() const
{
    const QString v = m_settings.value(QStringLiteral("config/mouse/wheel_down"),
                                        QStringLiteral("zoom_out")).toString();
    return wheelActionSupported(v) ? v : QStringLiteral("none");
}

QString Config::middleClickAction() const
{
    // 中键单击与拖动分手：单击走点击动作表（默认全屏切换，与旧版一致），
    // 拖动单独由 middleDragAction 控制。「pan」不是点击动作，折算 none。
    const QString v = m_settings.value(QStringLiteral("config/mouse/btn_middle_click"),
                                        QStringLiteral("fullscreen")).toString();
    if (v.isEmpty() || v == QLatin1String("pan")) {
        return QStringLiteral("none");
    }
    return v;
}

QString Config::middleDragAction() const
{
    // 拖动手势只支持平移；其余值（含旧配置残留）按 none。
    const QString v = m_settings.value(QStringLiteral("config/mouse/btn_middle_drag"),
                                        QStringLiteral("pan")).toString();
    return v == QLatin1String("pan") ? QStringLiteral("pan") : QStringLiteral("none");
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

bool Config::groupRawJpg() const
{
    // 「RAW+JPG 分组显示」：消费点 DirectoryLoader::buildIndexDir 的列表构建层。
    // 默认关 —— 关闭时扫描/排序/索引与现状逐位一致，不给既有浏览加任何分支。
    return m_settings.value(QStringLiteral("config/group_raw_jpg"), false).toBool();
}

bool Config::groupShowRaw() const
{
    // 「显示原始数据」：分组条目上打开 RAW/PSD 而不是 JPG；仅在分组开启时生效。
    return m_settings.value(QStringLiteral("config/group_show_raw"), false).toBool();
}

bool Config::fitWindowToImage() const
{
    // 2026-11-12 用户口径：窗口尺寸默认不随打开的图片变化，保持初始 1080×815。
    // 设置「根据图片大小调节窗口大小」仍保留，用户显式开启后才按图调窗。
    return m_settings.value(QStringLiteral("config/etc/fit_window_to_image"), false).toBool();
}

bool Config::lockWindowSize() const
{
    // 「锁定窗口大小」：开启后窗口尺寸不可拖拽改变（画布缩放/平移不受影响）。
    // 默认关；持久化，重启后仍按启动尺寸锁定。与 config/etc/fit_window_to_image
    // 互斥（按图调窗会改窗口尺寸），规则见 MainWindow::applyWindowSizeLock。
    return m_settings.value(QStringLiteral("config/lock_window_size"), false).toBool();
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

bool Config::appMenuOwnVisible(const QString &id) const
{
    // 缺键（首次安装 / 旧配置 / 恢复默认）一律按 MenuCatalog 默认表出值；
    // 未知 id 视为不可见，避免拼错的键意外展开整组命令。
    const AppMenu::Node *node = AppMenu::find(id);
    if (!node) {
        return false;
    }
    const QString key = QStringLiteral("config/appmenu/") + id;
    if (m_settings.contains(key)) {
        return m_settings.value(key, node->defOn).toBool();
    }
    // 迁移（2026-11-16 开关层级修订）：危险操作由根组总开关统一控制，
    // 「删除」不再单列开关。旧配置里只有 appmenu/delete_file，
    // 新键缺位时回退读旧值，避免升级后危险操作整组凭空消失或重现。
    // 旧键不删、不改写，只在缺新键时作为一次来源。
    if (id == QLatin1String("group_danger")) {
        const QString legacy = QStringLiteral("config/appmenu/delete_file");
        if (m_settings.contains(legacy)) {
            return m_settings.value(legacy, true).toBool();
        }
    }
    return node->defOn;
}

bool Config::appMenuVisible(const QString &id) const
{
    // 生效可见性：本项（有开关时）与各级祖先总开关全部为开。
    const AppMenu::Node *node = AppMenu::find(id);
    if (!node) {
        return false;
    }
    if (AppMenu::ownsSwitch(*node) && !appMenuOwnVisible(id)) {
        return false;
    }
    for (QString p = AppMenu::parentOf(id); !p.isEmpty(); p = AppMenu::parentOf(p)) {
        if (!appMenuOwnVisible(p)) {
            return false;
        }
    }
    return true;
}

bool Config::resetAppMenuVisibility()
{
    if (m_settings.status() != QSettings::NoError) {
        reopenSettings();
    }
    const QString group = QStringLiteral("config/appmenu");
    m_settings.beginGroup(group);
    QVariantMap previous;
    for (const QString &key : m_settings.allKeys()) {
        previous.insert(key, m_settings.value(key));
    }
    m_settings.remove(QString());
    m_settings.sync();
    const bool saved = m_settings.status() == QSettings::NoError;
    if (!saved) {
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
            m_settings.setValue(it.key(), it.value());
        }
    }
    m_settings.endGroup();
    return saved;
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

// —— 窗口几何（整数键，不用 saveGeometry）——
// 键名带 config/ 前缀，与其余键同命名空间；便携模式随 config.ini 走。
bool Config::hasWindowGeometry() const
{
    return m_settings.contains(QStringLiteral("config/win_w"))
        && m_settings.contains(QStringLiteral("config/win_h"));
}

int Config::winX() const
{
    return m_settings.value(QStringLiteral("config/win_x"), 0).toInt();
}

int Config::winY() const
{
    return m_settings.value(QStringLiteral("config/win_y"), 0).toInt();
}

int Config::winW() const
{
    return m_settings.value(QStringLiteral("config/win_w"), 0).toInt();
}

int Config::winH() const
{
    return m_settings.value(QStringLiteral("config/win_h"), 0).toInt();
}

bool Config::winMaximized() const
{
    return m_settings.value(QStringLiteral("config/win_maximized"), false).toBool();
}

void Config::setWindowGeometry(int x, int y, int w, int h, bool maximized)
{
    m_settings.setValue(QStringLiteral("config/win_x"), x);
    m_settings.setValue(QStringLiteral("config/win_y"), y);
    m_settings.setValue(QStringLiteral("config/win_w"), w);
    m_settings.setValue(QStringLiteral("config/win_h"), h);
    m_settings.setValue(QStringLiteral("config/win_maximized"), maximized);
}

// —— 会话恢复 ——
QString Config::sessionLastFile() const
{
    return m_settings.value(QStringLiteral("config/session_last_file"), QString()).toString();
}

void Config::setSessionLastFile(const QString &path)
{
    m_settings.setValue(QStringLiteral("config/session_last_file"), path);
}
