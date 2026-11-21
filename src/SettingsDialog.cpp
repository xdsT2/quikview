#include "PopupComboBox.h"
#include "SettingsDialog.h"

#include "AppMenuPreview.h"
#include "Chrome.h"
#include "DirectoryLoader.h"
#include "PanoExtension.h"

#include "quik_config_keys.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QTextStream>
#include <QToolTip>
#include <QCursor>
#include <QDateTime>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QElapsedTimer>

#include <cstdio>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QAbstractItemView>
#include <QFont>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QStylePainter>
#include <QTimer>
#include <QUrl>
#include <QVersionNumber>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <algorithm>
#include <memory>
#include <cmath>
#include <shobjidl.h>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

#include "Config.h"
#include "MainWindow.h"
#include "MenuCatalog.h"
#include "MenuIcons.h"
#include "PillToggle.h"
#include "Theme.h"
#include "UpdateChecker.h"
#include "UiStyle.h"
#include "AppIcon.h"

#include <QOperatingSystemVersion>

namespace {

// 2026-11-17 注释精简：把原本占第二行的说明搬到整行 tooltip。
// 传开关 / 下拉 / 行控件都可以：传行控件时 tooltip 就落在行上，传控件时落在它所在的行上。
// 行高仍由「有没有第二行」决定（applyPageRhythm 的 38 / 44），不改版式。
void descToRowTip(QWidget *control, const QString &text)
{
    if (!control)
        return;
    const bool isRow = control->objectName() == QLatin1String("settingRow");
    QWidget *target = isRow ? control : control->parentWidget();
    (target ? target : control)->setToolTip(text);
}

} // namespace

// ---------------------------------------------------------------------------
// 输入来源追踪（v2 焦点可见性，2026-11-11 修订规格）
// 鼠标点击控件时不得出现蓝色焦点框，再次点击已聚焦控件也要隐藏；只有
// Tab / Shift+Tab 等键盘导航显示 2px #1688F8 环并留 2px 外距。这里不改任何
// 控件的 FocusPolicy（保留 StrongFocus、Tab 顺序与高对比模式），只用一条应用级
// 过滤器记录最近一次输入来源，把结果写成每个控件的 kbFocus 动态属性；
// QSS 与 PillToggle 自绘都只在「有焦点 且 kbFocus」时画环。
// 菜单 Esc 返回是 KeyPress，自然延续键盘模式；程序性恢复焦点由 Qt 的
// focusChanged 触发 refresh，继承同一份模式状态。
// ---------------------------------------------------------------------------
class InputModeWatcher : public QObject
{
public:
    explicit InputModeWatcher(SettingsDialog *dialog)
        : QObject(dialog), m_dialog(dialog)
    {
    }

    bool keyboardMode() const { return m_keyboard; }

    // 焦点变化 / 切页 / 打开菜单后按当前焦点重刷一次属性。
    void refresh()
    {
        if (!m_dialog)
            return;
        QWidget *host = m_dialog->window();
        if (!host)
            return;
        QWidget *focus = host->focusWidget();
        // focusProxy 链：QKeySequenceEdit / 可编辑 QComboBox 的实际焦点在内层
        // QLineEdit 上，环要画在真正可见的那个控件上。
        QSet<QWidget *> ringTargets;
        for (QWidget *f = focus; f; f = f->focusProxy())
            ringTargets.insert(f);
        const QList<QWidget *> widgets = host->findChildren<QWidget *>();
        for (QWidget *w : widgets) {
            const bool on = m_keyboard && ringTargets.contains(w);
            if (w->property("kbFocus").toBool() != on) {
                w->setProperty("kbFocus", on);
                w->update();
            }
        }
        // focusWidget 可能尚未进入 findChildren（首次显示 / 刚切页）。
        for (QWidget *f : ringTargets) {
            if (f->property("kbFocus").toBool() != m_keyboard) {
                f->setProperty("kbFocus", m_keyboard);
                f->update();
            }
        }
    }

protected:
    bool eventFilter(QObject *obj, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::KeyPress: {
            auto *ke = static_cast<QKeyEvent *>(event);
            // 单独按修饰键不算导航，避免 Ctrl 起手的组合键先闪一下环。
            if (ke->key() != Qt::Key_Shift && ke->key() != Qt::Key_Control
                && ke->key() != Qt::Key_Alt && ke->key() != Qt::Key_Meta) {
                setKeyboardMode(true);
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::Wheel:
        case QEvent::TouchBegin:
            setKeyboardMode(false);
            break;
        default:
            break;
        }
        return QObject::eventFilter(obj, event);
    }

private:
    void setKeyboardMode(bool keyboard)
    {
        if (m_keyboard == keyboard)
            return;
        m_keyboard = keyboard;
        refresh();
    }

    SettingsDialog *m_dialog = nullptr;
    bool m_keyboard = false;
};

namespace {

// ---------------------------------------------------------------------------
// 设置窗口标准尺寸 1068 × 784，最小 900 × 660。
// 其余十页沿用 2026-11-11 的 kPage* 节奏，取值不变。
// ---------------------------------------------------------------------------
// 2026-11-12 用户纠偏：窗口随内容收紧（参考 macOS 设置窗口），默认逻辑宽
// 920（原 1068 让卡片两侧留出大片无用空白），侧栏 208，卡片最大 640，
// 超出继续拉宽时额外空间均分两侧、卡片不再变宽。实测：卡片 638、左右留白
// 36/37；125% 下物理 1150 × 980。
constexpr int kDlgW = 1068;   // 2026-11-16 开关层级修订：按设计稿恢复到 1068（覆盖 11-12 的 920 定档）
constexpr int kDlgH = 784;
constexpr int kDlgMinW = 900;
constexpr int kDlgMinH = 660;
constexpr int kTitleBarH = 41;     // v2：标题栏 41（窄窗 40）
constexpr int kNavW = 208;         // 收紧后左栏 208（窄窗 180），11 项不挤
constexpr int kFooterH = 44;       // v2：底栏 44（窄窗 43）
constexpr int kComboH = 30;
constexpr int kInputH = 30;
// 其余十页的设置行节奏（既有实现，不变）
constexpr int kRowMinH = 40;
constexpr int kRowPadX = 11;
constexpr int kRowPadY = 5;
constexpr int kGroupTop = 16;
// v2 修订（2026-11-11）：组间 14；行高不再统一加高，按行型由内容撑开——
// 单行设置 38、标题+说明 44（说明换行继续按文字增长），控件列 154。
constexpr int kPageFirstGap = 16;
constexpr int kPageGroupGap = 14;
constexpr int kPageRowH = 44;        // 标题+说明
constexpr int kPageRowHSingle = 38;  // 单行设置
constexpr int kPageCtlW = 190;

// —— 「设置 › 一般」v2 母版专用尺寸 ——
// 标准 920×784：卡片列居中最大 640（实测 638）、两侧留白 36/37；组间距 12、
// 行高按内容撑开、导航行 36 / 行距 4、图标底板 22、控件高 30、下拉宽 190。
// 窄窗 900×660：左栏 180、导航行 34 / 行距 2、组间距 8、行高约 38、
// 标题栏 40、底栏 43；卡片仍 638、左右各 40/41（实测）。
constexpr int kGenContentMaxW = 640;   // 卡片上限 640（620–640 区间取上沿）
constexpr int kGenColumnPadX = 28;
constexpr int kGenCompactPadX = 24;
constexpr int kGenPaneTop = 20;
constexpr int kGenFirstGap = 14;
constexpr int kGenGroupGap = 12;
constexpr int kGenRowH = 47;
constexpr int kGenRowPadX = 14;
constexpr int kGenTextGap = 16;
constexpr int kGenCtlW = 190;
constexpr int kGenNavRowH = 36;
constexpr int kGenCtrlH = 30;
constexpr int kGenNavSpacing = 4;
constexpr int kGenComboW = 190;
// 紧凑档（窗口宽 < 1000 时启用）
constexpr int kGenCompactNavW = 180;
constexpr int kGenCompactNavRowH = 34;
constexpr int kGenCompactNavSpacing = 2;
constexpr int kGenCompactTitleH = 40;
constexpr int kGenCompactFooterH = 43;
constexpr int kGenCompactGroupGap = 8;
constexpr int kGenCompactRowH = 38;
constexpr int kGenCompactFirstGap = 8;
constexpr int kGenCompactPaneTop = 12;
constexpr int kGenCompactHeadGap = 4;
constexpr int kGenCompactThreshold = 920;   // 小于默认宽度才进紧凑档

// ---------------------------------------------------------------------------
// QSettings 便捷读写（统一 config/ 前缀）
// ---------------------------------------------------------------------------
QString sGet(const char *key, const QString &def = QString())
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toString();
}
bool bGet(const char *key, bool def)
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toBool();
}
int iGet(const char *key, int def)
{
    return Config::instance().settings().value(QStringLiteral("config/") + key, def).toInt();
}

// ---------------------------------------------------------------------------
// 壳扩展注册探测（2026-11-13 修订，上下文菜单派单 §3）：
// 右键命令处理器与 WebP/PSD/PSB 缩略图分开检查；核对 InprocServer32 的完整
// DLL 路径（不再只比文件名），区分 未安装 / 未注册 / 指向其他安装 / 当前
// 安装已注册。右键命令核对 64 与 32 位两个视图（安装器把 quikthumb-<ver>.dll
// 写 64 位视图、quikthumb32-<ver>.dll 写 WOW6432Node；0.5.3 及以前是无版本号
// 的 quikthumb.dll / quikthumb32.dll，仍按回退分支认）。
// ---------------------------------------------------------------------------
// 壳扩展 DLL 定位。0.5.4 起安装的是带版本号的文件名（64 位 quikthumb-<ver>.dll、
// 32 位 quikthumb32-<ver>.dll）：升级换新文件名，旧文件即使被资源管理器占用
// 删不掉也会留在目录里，所以同目录可能同时存在多个版本 —— 按版本号取最高
// 的一份。只有一份带版本号的都没有时，才回退到 0.5.3 及以前的 quikthumb.dll。
QString qvShellDllInDir(const QString &appDir, bool is32)
{
    const QString prefix = is32 ? QStringLiteral("quikthumb32-")
                                : QStringLiteral("quikthumb-");
    QString bestName;
    QVersionNumber bestVer;
    const QFileInfoList entries = QDir(appDir).entryInfoList(QDir::Files);
    for (const QFileInfo &fi : entries) {
        const QString name = fi.fileName();
        if (!name.startsWith(prefix, Qt::CaseInsensitive)
            || !name.endsWith(QStringLiteral(".dll"), Qt::CaseInsensitive))
            continue;
        const QVersionNumber ver = QVersionNumber::fromString(
            name.mid(prefix.size(), name.size() - prefix.size() - 4));
        if (ver.isNull())
            continue;
        if (bestName.isEmpty() || QVersionNumber::compare(ver, bestVer) > 0) {
            bestName = name;
            bestVer = ver;
        }
    }
    if (!bestName.isEmpty())
        return appDir + QLatin1Char('/') + bestName;
    const QString legacy = appDir
        + (is32 ? QStringLiteral("/quikthumb32.dll")
                : QStringLiteral("/quikthumb.dll"));
    return QFileInfo::exists(legacy) ? legacy : QString();
}

bool shellExtDllPresent()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return !qvShellDllInDir(appDir, false).isEmpty()
        || !qvShellDllInDir(appDir, true).isEmpty();
}

namespace {

enum ShellRegState {
    ShellNotInstalled,   // 本机没有可用的注册记录，本安装也没有壳扩展文件
    ShellNotRegistered,  // 本安装带壳扩展文件，但注册表没有指向它的记录
    ShellOtherInstall,   // 注册表指向另一处安装的 quikthumb*.dll
    ShellRegistered      // 注册表指向本安装的 DLL（当前安装已注册）
};

struct ShellComponent {
    ShellRegState state = ShellNotInstalled;
    QString boundPath;   // 注册表当前指向的 DLL（「指向其他安装」详情展示用）
};

// 右键菜单 Handler 的注册位置。0.5.4 起逐扩展名一份；与 shell-ext/src/module.cpp
// 的 kSupportedExts 保持一致（菜单本来就只在这些格式上出现）。
const char *const kShellMenuExts[] = {".png", ".jpg",  ".jpeg", ".bmp", ".gif",
                                      ".webp", ".tif", ".tiff", ".psd", ".psb"};

// 归一化路径比较（分隔符与大小写不敏感）
bool qvSamePath(const QString &a, const QString &b)
{
    const QString na = QDir::fromNativeSeparators(a).trimmed();
    const QString nb = QDir::fromNativeSeparators(b).trimmed();
    return !na.isEmpty() && na.compare(nb, Qt::CaseInsensitive) == 0;
}

// 读注册表键的默认值。keyPath 形如
// HKEY_LOCAL_MACHINE\Software\Classes\...\InprocServer32；32 位视图用
// 字面 WOW6432Node 路径（64 位进程直接可读）。先查 HKLM 再查 HKCU
//（regsvr32 /i 按用户注册时写 HKCU）。
QString qvRegDefault(const QString &keyPath)
{
    const QString roots[] = {
        QStringLiteral("HKEY_LOCAL_MACHINE"),
        QStringLiteral("HKEY_CURRENT_USER"),
    };
    for (const QString &root : roots) {
        QSettings reg(root + QLatin1Char('\\') + keyPath,
                      QSettings::NativeFormat);
        const QString v = reg.value(QStringLiteral(".")).toString();
        if (!v.isEmpty())
            return v;
    }
    return QString();
}

// 由「注册表指向的 DLL 路径 + 本安装 DLL 是否在」推导四态。
// appDir 用来识别「指向本安装目录里的另一份 quikthumb*.dll」——那是同一套
// 安装的旧版本号文件（升级后才换成当前版本），仍算本安装已注册，不能误报成
// 「指向其他安装」。
ShellRegState qvStateFromPath(const QString &bound, const QString &ourDll,
                              const QString &appDir, bool dllFilePresent)
{
    if (bound.isEmpty())
        return dllFilePresent ? ShellNotRegistered : ShellNotInstalled;
    if (qvSamePath(bound, ourDll))
        return ShellRegistered;
    if (bound.contains(QStringLiteral("quikthumb"), Qt::CaseInsensitive)
        && qvSamePath(QFileInfo(QDir::fromNativeSeparators(bound)).absolutePath(),
                      appDir))
        return ShellRegistered;
    if (bound.contains(QStringLiteral("quikthumb"), Qt::CaseInsensitive))
        return ShellOtherInstall;
    return dllFilePresent ? ShellNotRegistered : ShellNotInstalled;
}

} // namespace

// 右键命令处理器注册态：handler 键 + 64/32 位 CLSID 的 InprocServer32 全部
// 指向本安装才算「已注册」；任一指向别处即为「指向其他安装」；位数不全为
// 「部分注册」。partial 返回 true 时 state 是 Registered 但位数不全。
static ShellComponent probeShellMenu()
{
    ShellComponent out;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dll64 = qvShellDllInDir(appDir, false);
    const QString dll32 = qvShellDllInDir(appDir, true);

    // handler 键：逐扩展名任一份指向本产品即算已注册；再看 0.5.3 及以前的
    // `*` 宽注册（升级时由安装器清掉，清掉前仍视为已注册）。
    bool handlerBound = false;
    for (const char *ext : kShellMenuExts) {
        const QString key =
            QStringLiteral("Software\\Classes\\%1\\shellex\\"
                           "ContextMenuHandlers\\QuikView")
                .arg(QLatin1String(ext));
        if (qvRegDefault(key).compare(
                QStringLiteral("{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}"),
                Qt::CaseInsensitive)
            == 0) {
            handlerBound = true;
            break;
        }
    }
    if (!handlerBound) {
        handlerBound =
            qvRegDefault(QStringLiteral("Software\\Classes\\*\\shellex\\"
                                        "ContextMenuHandlers\\QuikView"))
                .compare(
                    QStringLiteral("{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}"),
                    Qt::CaseInsensitive)
            == 0;
    }

    const QString clsid64 = qvRegDefault(QStringLiteral(
        "Software\\Classes\\CLSID\\{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}\\InprocServer32"));
    const QString clsid32 = qvRegDefault(QStringLiteral(
        "Software\\Classes\\WOW6432Node\\CLSID\\{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}\\InprocServer32"));

    const ShellRegState s64 =
        (handlerBound ? qvStateFromPath(clsid64, dll64, appDir, !dll64.isEmpty())
                      : (dll64.isEmpty() ? ShellNotInstalled : ShellNotRegistered));
    const ShellRegState s32 =
        (handlerBound ? qvStateFromPath(clsid32, dll32, appDir, !dll32.isEmpty())
                      : (dll32.isEmpty() ? ShellNotInstalled : ShellNotRegistered));

    // 综合位数：任一指向别处 → 其他安装；两者都已注册 → 已注册（位数全）；
    // 只有一边 → 部分注册（state=Registered + partial 标记由位数比较得出）
    if (s64 == ShellOtherInstall || s32 == ShellOtherInstall) {
        out.state = ShellOtherInstall;
        out.boundPath = s64 == ShellOtherInstall ? clsid64 : clsid32;
        return out;
    }
    const bool ok64 = s64 == ShellRegistered;
    const bool ok32 = s32 == ShellRegistered;
    if (ok64 && ok32) {
        out.state = ShellRegistered;
        out.boundPath = QStringLiteral("64+32");
    } else if (ok64 || ok32) {
        out.state = ShellRegistered;   // 部分注册：boundPath 记录已绑定位数
        out.boundPath = ok64 ? QStringLiteral("64") : QStringLiteral("32");
    } else {
        out.state = (dll64.isEmpty() && dll32.isEmpty()) ? ShellNotInstalled
                                                         : ShellNotRegistered;
    }
    return out;
}

// 文件缩略图注册态（.ext\ShellEx\{类别} → 缩略图 CLSID → 本安装 DLL）
static ShellComponent probeShellThumb(const QString &ext)
{
    ShellComponent out;
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString dll64 = qvShellDllInDir(appDir, false);
    const QString bound = qvRegDefault(QStringLiteral(
        "Software\\Classes\\%1\\ShellEx\\{e357fccd-a995-4576-b01f-234630154e96}")
                                           .arg(ext));
    const bool boundToUs =
        bound.compare(QStringLiteral("{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}"),
                      Qt::CaseInsensitive) == 0;
    const QString clsid = boundToUs
        ? qvRegDefault(QStringLiteral(
            "Software\\Classes\\CLSID\\{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}\\InprocServer32"))
        : bound;
    out.state = qvStateFromPath(clsid, dll64, appDir, !dll64.isEmpty());
    out.boundPath = clsid;
    return out;
}

// ---------------------------------------------------------------------------
// 键盘页行区：高度按整行吸附（行 32 + 分隔线 1），自己滚。
// 起因（2026-11-04 09:44 用户截图）：表格直接放页面里时，内容区底边会把最后
// 一行「删除」切成半行，看上去像被裁坏。改成整行数高度后，要么完整显示某一行，
// 要么整行藏起来等滚动，不会再出现半行。
// ---------------------------------------------------------------------------
class KeyRowsArea : public QScrollArea
{
public:
    explicit KeyRowsArea(QWidget *parent = nullptr) : QScrollArea(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setWidgetResizable(true);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        // sizeHint 跟随内容：行区不再吃 stretch（M-05），若仍用 QScrollArea 的默认
        // sizeHint（与内容无关的预置值），表格只按 360 高分配、键盘行会被逼出滚动条。
        setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    }

    // hint / head 是行区之外、同样占据纵向空间的固定部件。
    // extra：行区之外新增的部件（键盘页状态图例），也要从可用高度里扣掉，
    // 否则键盘行会算出过高、表格底部越出页面。其余页不传，行为不变。
    void snapTo(QWidget *hint, QWidget *head, int rowStep, QWidget *extra = nullptr,
                int fixedContent = 0)
    {
        m_hint = hint;
        m_head = head;
        m_extra = extra;
        m_step = rowStep;
        m_fixedContent = fixedContent;
        QWidget *w = parentWidget();
        while (w && !qobject_cast<QScrollArea *>(w)) {
            w = w->parentWidget();
        }
        if (auto *outer = qobject_cast<QScrollArea *>(w)) {
            m_outer = outer;
            m_page = outer->widget();
            outer->viewport()->installEventFilter(this);
            outer->installEventFilter(this);
            if (m_page)
                m_page->installEventFilter(this);
        }
        apply();
        // showEvent 时布局还没激活，viewport 高度常是旧值；等一轮事件循环再算一次。
        QTimer::singleShot(0, this, [this]() { apply(); });
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        // 页面在 QStackedWidget 里时，隐藏页的 geometry 会先被清空、显示时才重排；
        // 只监听 viewport 的 Resize 会读到旧高度。这里把外层滚动区、viewport、页面
        // 本体一起监听，统一走 queued 重算，等布局激活后再取最终几何。
        if (e->type() == QEvent::Resize || e->type() == QEvent::Show) {
            if (m_outer && (o == m_outer || o == m_outer->viewport() || o == m_page))
                scheduleApply();
        }
        return QScrollArea::eventFilter(o, e);
    }

    void showEvent(QShowEvent *e) override
    {
        QScrollArea::showEvent(e);
        scheduleApply();
    }

    // 高度直接由内容 sizeHint 决定：QScrollArea 默认 sizeHint 是固定值（360），
    // 与 AdjustToContents 无关——stretch=0 时布局按 sizeHint 给高度，所以必须自己重写。
    // NoFrame 时 frameWidth()=0，高度 = 内容 sizeHint 高度。宽度沿用父类（跟随外层表格）。
    // stretch 模式（键盘页 2026-11-11 起）：表格吃满页面剩余高度，行区可压缩，
    // sizeHint 必须给一个小值；否则 QBoxLayout 先按内容自然高分配，页面被撑出
    // 外层滚动条。滚动范围仍由 widget 内容决定，不受这里影响。
    QSize sizeHint() const override
    {
        return QSize(QScrollArea::sizeHint().width(), 120);
    }

private:
    void scheduleApply()
    {
        // 重算放到下一轮事件循环：等页面/视口布局激活完，读到的才是最终高度。
        if (m_applyQueued)
            return;
        m_applyQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_applyQueued = false;
            apply();
        });
    }

    void apply()
    {
        if (!m_hint || !m_head || !m_outer) {
            return;
        }
        // 内容自然高 = 13 行 + 12 条分隔线 + 底部 12px 安全留白（在 rowsBox margin 里）
        const int natural = widget() ? qMax(1, widget()->sizeHint().height())
                                     : m_step * 13;
        // 可用高 = 外层可视高 − 提示行 − 表头 − 表格上下 1px 边框。
        // 页面 layout 在这三段之外已无其它纵向占用（margins/spacing 均 0，
        // 表格外 spacing 已随 M-05 删除），不再重复扣减。
        constexpr int kTableBorder = 2;
        // 分组头等固定内容也占纵向空间：先扣掉，再按整行数封顶。
        const int avail = m_outer->viewport()->height() - m_hint->height()
                          - m_head->height() - (m_extra ? m_extra->height() : 0)
                          - kTableBorder - m_fixedContent;

        // 高度两步走：
        // 1) 窗口够高 → 就取内容自然高，表格不再吃满页面，剩余纵向空间由页面底部弹簧吸收；
        // 2) 窗口不够高 → 封顶到「k 整行」= k*33−1（k 行 + (k−1) 条线），仅行区内部滚动，
        //    页面不出现外层滚动条；k 下限 1，避免极矮窗口算出 0。
        // 统一用固定高度而不是只设 maximumHeight：表格已不再是 stretch 项，
        // 只设 max 时布局仍按 sizeHint 撑开，封顶不会真正生效。
        int target = natural;
        if (avail < natural) {
            const int k = qBound(1, avail / m_step, 14);
            target = k * m_step - 1;
        }
        if (maximumHeight() != target) {
            setFixedHeight(target);
            updateGeometry();
        }
    }

    QWidget *m_hint = nullptr;
    QWidget *m_head = nullptr;
    QWidget *m_extra = nullptr;
    QWidget *m_page = nullptr;
    QScrollArea *m_outer = nullptr;
    int m_step = 33;
    int m_fixedContent = 0;   // 分组头等不参与整行吸附的固定内容高
    bool m_applyQueued = false;
};

// ---------------------------------------------------------------------------
// v2 checkbox（2026-11-11 修订）：表格内逐项启用用它，16×16 外框、勾选为
// 中性填充 + 对比色勾（不是蓝色）。自绘而不用 QSS ::indicator，原因有二：
//   1) 深/浅主题的勾色需要跟随填充色反转，QSS 只能靠两张位图；
//   2) 焦点环只在键盘导航时出现（kbFocus），和 PillToggle 用同一套规则。
// 命中区 24×24：16 外框 + 2px 环 + 2px 外距，正好满足规格的外距要求。
// ---------------------------------------------------------------------------
class V2CheckBox : public QCheckBox
{
public:
    explicit V2CheckBox(QWidget *parent = nullptr) : QCheckBox(parent)
    {
        setText(QString());
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setFixedSize(24, 24);
    }

    QSize sizeHint() const override { return QSize(24, 24); }
    QSize minimumSizeHint() const override { return QSize(24, 24); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box((width() - 16) / 2.0, (height() - 16) / 2.0, 16, 16);
        const bool on = isChecked();
        QColor fill = on ? UiStyle::fluent::switchOffColor() : QColor(0, 0, 0, 0);
        QColor line = on ? UiStyle::fluent::switchOffColor()
                         : (underMouse() ? UiStyle::fluent::descColor()
                                         : UiStyle::fluent::borderColor());
        if (!isEnabled())
            p.setOpacity(0.45);

        p.setPen(QPen(line, 1));
        p.setBrush(fill);
        p.drawRoundedRect(box.adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        if (on) {
            const QColor mark(Qt::white);
            QPainterPath path;
            path.moveTo(box.left() + 3.6, box.center().y() + 0.2);
            path.lineTo(box.left() + 6.4, box.bottom() - 3.8);
            path.lineTo(box.right() - 3.2, box.top() + 4.0);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPath(path);
        }

        if (isEnabled())
            p.setOpacity(1.0);
        if (hasFocus() && property("kbFocus").toBool()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(UiStyle::fluent::accentColor(), 2));
            p.drawRoundedRect(box.adjusted(-2, -2, 2, 2), 5, 5);
        }
    }

    bool hitButton(const QPoint &pos) const override { return rect().contains(pos); }
};

// ---------------------------------------------------------------------------
// v2 pop-up（2026-11-11 修订）：持续显示互斥值的字段。
// 规格：行内左文右箭头、当前值、选中项有勾 + 中性底、禁用项灰显并把原因写在
// 次行、菜单宽度按最长值扩展（上限 290）、方向键跳过禁用项、Enter 提交、
// Esc 取消并恢复触发器焦点。QComboBox 本身已提供键盘语义与焦点恢复，
// 这里只替换弹出项绘制与几何。
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// 导航图标：语义 id 交给 AppIcon（图标统一派单 2026-11-18）。
// 历史实现是「不依赖 Qt6::Svg，用 QPainter 画 16px 线性几何图形」；方案 A 落地后
// 那条限制不成立，手绘分支随之删除，只保留语义映射，避免两套真源。
// 颜色仍由调用方给（底板上的白色 / 禁用态半透明），与文字间距保持 9px。
// ---------------------------------------------------------------------------
enum class NavGlyph { Gear, Eye, Image, Doc, Link, Keyboard, Mouse, Bookmark, ContextMenu, Menu, More, Globe, Info };

// 语义 → AppIcon id。顺序沿用历史枚举，避免调用点大改。
const char *navGlyphId(NavGlyph glyph)
{
    switch (glyph) {
    case NavGlyph::Gear:        return "settings";        // 一般
    case NavGlyph::Eye:         return "apps";            // 浏览
    case NavGlyph::Image:       return "color";           // 外观：配色/主题
    case NavGlyph::Doc:         return "image-edit";      // 图像处理
    case NavGlyph::Link:        return "link";            // 格式关联
    case NavGlyph::Keyboard:    return "keyboard";        // 键盘
    case NavGlyph::Mouse:       return "cursor";          // 鼠标
    case NavGlyph::Bookmark:    return "bookmark";        // 书签
    case NavGlyph::Menu:        return "text-bullet-list";// 上下文菜单
    case NavGlyph::ContextMenu: return "list-tree";       // 查看器右键菜单（大纲）
    case NavGlyph::More:        return "options";         // 杂项
    case NavGlyph::Globe:       return "language";        // 语言
    case NavGlyph::Info:        return "info";            // 关于
    }
    return "settings";
}

void paintNavGlyph(QPainter &p, NavGlyph glyph, const QRect &box, const QColor &color,
                   AppIcon::Style style)
{
    AppIcon::paint(p, QLatin1String(navGlyphId(glyph)), QRectF(box), color, style);
}

// navItem：QSS 负责背景/边框，图标与文字自绘（图标 16px + 间距 9px + 左内边距 11px）
class NavItemButton : public QPushButton
{
public:
    NavItemButton(const QString &text, NavGlyph glyph, QWidget *parent = nullptr,
                  bool muted = false, int categoryIndex = -1)
        : QPushButton(text, parent), m_glyph(glyph), m_muted(muted),
          m_category(categoryIndex)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QString label = opt.text;
        opt.text.clear();

        QStylePainter p(this);
        // 整行选中 / 悬停底色由 QSS 的 #navItem:checked / :hover 绘制。
        p.drawControl(QStyle::CE_PushButton, opt);

        // 定稿：20×20 实色圆角底板（半径 5）+ 16×16 网格白色线性图形。
        // 主题切换保留类别色，选中只用整行底色与 600 字重表达。
        const int plateSize = 22;   // v2：底板 22×22、圆角 6
        const int pad = 10;
        const QRect plate(pad, (height() - plateSize) / 2, plateSize, plateSize);
        QColor plateColor = m_muted ? UiStyle::color(UiStyle::fluent::neutralIcon())
                                    : UiStyle::fluent::categoryColor(m_category);
        if (!isEnabled())
            plateColor.setAlpha(140);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(plateColor);
        p.drawRoundedRect(plate, 6, 6);

        const int glyphSize = 16;
        // 设置导航：选中项 Filled，其余 Regular（2026-11-19 定稿状态规则）。
        paintNavGlyph(p, m_glyph,
                      QRect(plate.x() + (plateSize - glyphSize) / 2,
                            plate.y() + (plateSize - glyphSize) / 2,
                            glyphSize, glyphSize),
                      QColor(255, 255, 255),
                      isChecked() ? AppIcon::Style::Filled : AppIcon::Style::Regular);

        QFont f = font();
        if (isChecked())
            f.setWeight(QFont::DemiBold);
        p.setFont(f);
        QColor labelColor = UiStyle::fluent::textColor();
        if (!isEnabled())
            labelColor = UiStyle::fluent::descColor();
        else if (isChecked())
            labelColor = UiStyle::fluent::navSelTextColor();
        p.setPen(labelColor);
        const int gap = 9;
        const int textX = plate.right() + gap;
        p.drawText(QRect(textX, 0, width() - textX - 8, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    void enterEvent(QEnterEvent *e) override
    {
        QPushButton::enterEvent(e);
        update();
    }
    void leaveEvent(QEvent *e) override
    {
        QPushButton::leaveEvent(e);
        update();
    }

private:
    NavGlyph m_glyph;
    bool m_muted = false;
    int m_category = -1;
};

// 空热键占位修正：QKeySequenceEdit 内部的 QLineEdit 在构造后才挂上，
// 构造期 findChild 拿不到；在 Polish/Show 时再把占位文案换成中文「未设置」（Muted 色）。
class KeySeqPlaceholderFixer : public QObject
{
public:
    explicit KeySeqPlaceholderFixer(QKeySequenceEdit *edit) : QObject(edit)
    {
        edit->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (ev->type() == QEvent::Polish || ev->type() == QEvent::Show) {
            if (auto *le = static_cast<QKeySequenceEdit *>(parent())->findChild<QLineEdit *>()) {
                apply(le);
                // QKeySequenceEdit::clear()/resetState() 会把占位重置回英文
                // "Press shortcut"；文本重新变空时（含 Esc 清空）补回中文占位。
                if (!m_watchingEmpty) {
                    m_watchingEmpty = true;
                    // 延迟到事件循环：resetState() 里 setText 之后紧接着会写回英文占位，
                    // 必须等它写完再覆盖，否则又被冲掉。编辑结束（焦点离开也走
                    // finishEditing→resetState）同样要补一次。
                    auto *edit = static_cast<QKeySequenceEdit *>(parent());
                    auto reapply = [le]() {
                        QTimer::singleShot(0, le, [le]() {
                            if (le->text().isEmpty())
                                apply(le);
                        });
                    };
                    connect(le, &QLineEdit::textChanged, this, [reapply](const QString &t) {
                        if (t.isEmpty())
                            reapply();
                    });
                    connect(edit, &QKeySequenceEdit::editingFinished, this, reapply);
                }
            }
        }
        return false;
    }
private:
    bool m_watchingEmpty = false;
    static void apply(QLineEdit *le)
    {
        // QKeySequenceEdit 不会替内部编辑框保留完整高度；明确固定到键帽的
        // 26px 外形，避免全局 QLineEdit 最小高度把它压成一条矮槽。
        le->setFixedHeight(26);
        const bool editing = le->parentWidget()
            && le->parentWidget()->property("state").toString()
                   == QLatin1String("editing");
        le->setPlaceholderText(editing
            ? QCoreApplication::translate("SettingsDialog", "按下组合键…")
            : QCoreApplication::translate("SettingsDialog", "未设置"));
        QPalette pal = le->palette();
        pal.setBrush(QPalette::PlaceholderText, UiStyle::muted());
        le->setPalette(pal);
    }
};

// Qt 6.11 的 QKeySequenceEdit 会把 Esc 本身记成键位（只有「全选文本后按 Backspace」
// 才清空），而键盘页提示承诺「Esc 清空」。这里在编辑框上补一个过滤器：焦点在热键
// 编辑框内按 Esc → 清空为「未设置」，不再把 Esc 存成快捷键；其余情况 Esc 仍照常
// 交给对话框（关闭设置）。
class KeySeqEscClearFilter : public QObject
{
public:
    explicit KeySeqEscClearFilter(QKeySequenceEdit *edit) : QObject(edit), m_edit(edit)
    {
        edit->installEventFilter(this);
        hookInner();
        // 内部 QLineEdit 在构造后才由 QKeySequenceEdit 挂上；补一次延迟安装，
        // 否则 Esc 会漏到 QDialog::reject 把整个设置窗口关掉（2026-11-11 实测）。
        QTimer::singleShot(0, this, [this]() { hookInner(); });
    }

protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        if (ev->type() == QEvent::Show || ev->type() == QEvent::Polish)
            hookInner();
        if (ev->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(ev);
            if (ke->key() == Qt::Key_Escape && !ke->isAutoRepeat()) {
                m_edit->clear();
                return true;   // 吃掉 Esc：不再向上冒泡关闭对话框
            }
        }
        return false;
    }

private:
    void hookInner()
    {
        if (m_hooked)
            return;
        // 焦点实际落在内部 QLineEdit（QKeySequenceEdit 设了 focusProxy），键事件先到它。
        if (auto *le = m_edit->findChild<QLineEdit *>()) {
            le->installEventFilter(this);
            m_hooked = true;
        }
    }

    QKeySequenceEdit *m_edit;
    bool m_hooked = false;
};

// 键盘页热键框状态（2026-11-09 样板）：把「已设置 / 未设置 / 正在录入」写成动态
// 属性 state，交给 QSS 属性选择器；冲突态由整行 [conflict="true"] 级联，不在这里判。
// 属性变化后必须对容器和内部 QLineEdit 都重新 polish，否则后代选择器不会重算。
class KeySeqStateFilter : public QObject
{
public:
    explicit KeySeqStateFilter(QKeySequenceEdit *edit,
                               std::function<void()> onStateChanged = {})
        : QObject(edit), m_edit(edit), m_onStateChanged(onStateChanged)
    {
        edit->installEventFilter(this);
        if (auto *le = edit->findChild<QLineEdit *>())
            le->installEventFilter(this);
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this]() { schedule(); });
        schedule();
    }

    void schedule()
    {
        QTimer::singleShot(0, m_edit, [this]() { refresh(); });
    }

    void refresh()
    {
        const bool editing = m_edit->hasFocus()
            || (m_edit->focusProxy() && m_edit->focusProxy()->hasFocus());
        const QString state = editing ? QStringLiteral("editing")
            : (m_edit->keySequence().isEmpty() ? QStringLiteral("empty")
                                               : QStringLiteral("set"));
        if (m_edit->property("state").toString() != state) {
            m_edit->setProperty("state", state);
            m_edit->style()->unpolish(m_edit);
            m_edit->style()->polish(m_edit);
            if (auto *le = m_edit->findChild<QLineEdit *>()) {
                le->style()->unpolish(le);
                le->style()->polish(le);
            }
            m_edit->update();
        }
        // 录入时把旧值从显示上拿掉，键帽直接写「按下组合键…」（设计图 05）。
        // Qt 内部仍保留原序列：直接点走会走 finishEditing→resetState 恢复旧值，
        // 不会把「没按键就离开」误判成清空。
        if (auto *le = m_edit->findChild<QLineEdit *>()) {
            if (editing && !m_wasEditing && !le->text().isEmpty())
                le->clear();
            const QString want = editing ? tr("按下组合键…")
                : (m_edit->keySequence().isEmpty() ? tr("未设置") : QString());
            if (le->placeholderText() != want)
                le->setPlaceholderText(want);
            QPalette pal = le->palette();
            pal.setBrush(QPalette::PlaceholderText, UiStyle::muted());
            le->setPalette(pal);
            if (!editing && !m_edit->keySequence().isEmpty()) {
                QString shown = m_edit->keySequence().toString(QKeySequence::PortableText);
                shown.replace(QStringLiteral("Ctrl+"), QStringLiteral("Ctrl + "));
                shown.replace(QStringLiteral("Shift+"), QStringLiteral("Shift + "));
                shown.replace(QStringLiteral("Alt+"), QStringLiteral("Alt + "));
                shown.replace(QStringLiteral("Meta+"), QStringLiteral("Meta + "));
                shown.replace(QStringLiteral("Right"), QStringLiteral("→"));
                shown.replace(QStringLiteral("Left"), QStringLiteral("←"));
                if (le->text() != shown)
                    le->setText(shown);
            }
        }
        m_wasEditing = editing;

        // 行内「正在录入」与页首说明都靠这一次回调同步（含焦点进出）。
        if (m_onStateChanged)
            m_onStateChanged();
    }

protected:
    bool eventFilter(QObject *, QEvent *ev) override
    {
        const QEvent::Type t = ev->type();
        if (t == QEvent::FocusIn || t == QEvent::FocusOut
            || t == QEvent::Show || t == QEvent::Polish)
            schedule();
        return false;
    }

private:
    QKeySequenceEdit *m_edit;
    std::function<void()> m_onStateChanged;
    bool m_wasEditing = false;
};

// 9 个页签的图标（顺序必须与 addPage 调用顺序一致；漏项会让其后全部错一格）
// 通用 / 浏览 / 外观与显示 / 图像处理 / 键盘 / 鼠标与滚轮 / 查看器右键菜单 /
// Windows 集成 / 扩展。
NavGlyph navGlyphFor(int index)
{
    static const NavGlyph glyphs[] = { NavGlyph::Gear, NavGlyph::Eye, NavGlyph::Image,
                                       NavGlyph::Doc, NavGlyph::Keyboard, NavGlyph::Mouse,
                                       NavGlyph::ContextMenu, NavGlyph::Link, NavGlyph::More };
    return (index >= 0 && index < int(sizeof(glyphs) / sizeof(glyphs[0])))
               ? glyphs[index]
               : NavGlyph::Menu;
}

// ---------------------------------------------------------------------------
// 动作选项总表（键盘页 / 鼠标页共用）。value 存 config key。
// ---------------------------------------------------------------------------
QList<QPair<QString, QString>> actionOptions()
{
    return {
        { QStringLiteral("none"),          QObject::tr("无操作") },
        { QStringLiteral("next"),          QObject::tr("翻下一张") },
        { QStringLiteral("prev"),          QObject::tr("翻上一张") },
        { QStringLiteral("next_frame"),    QObject::tr("下一帧") },
        { QStringLiteral("prev_frame"),    QObject::tr("上一帧") },
        { QStringLiteral("zoom_in"),       QObject::tr("放大") },
        { QStringLiteral("zoom_out"),      QObject::tr("缩小") },
        { QStringLiteral("pan"),           QObject::tr("拖拽平移") },
        { QStringLiteral("actual_size"),   QObject::tr("实际大小") },
        { QStringLiteral("fit"),           QObject::tr("适配窗口") },
        { QStringLiteral("slideshow"),     QObject::tr("幻灯片") },
        { QStringLiteral("fullscreen"),    QObject::tr("全屏切换") },
        { QStringLiteral("close"),         QObject::tr("关闭窗口") },
        { QStringLiteral("context_menu"),  QObject::tr("上下文菜单") },
        { QStringLiteral("open"),          QObject::tr("打开") },
        { QStringLiteral("openfolder"),    QObject::tr("打开文件夹") },
        { QStringLiteral("favorite"),      QObject::tr("收藏") },
        { QStringLiteral("del"),           QObject::tr("删除") },
    };
}

// 到达首尾行为选项
QList<QPair<QString, QString>> lastFileOptions()
{
    return {
        { QStringLiteral("first"), QObject::tr("转到首个文件") },
        { QStringLiteral("stop"),  QObject::tr("停止") },
        { QStringLiteral("ask"),   QObject::tr("询问") },
    };
}

// 语言下拉：跟随系统 + 全部 8 种界面语言。
// 多语言补齐派单（2026-11-15）：三批补齐后全部语言完整可用
// （737 条界面串全覆盖，菜单项与 .qm 一一对应）。
void populateLanguageCombo(QComboBox *combo)
{
    combo->addItem(QObject::tr("跟随系统"), QStringLiteral("system"));
    combo->addItem(QStringLiteral("中文(简体)"), QStringLiteral("zh_CN"));
    combo->addItem(QStringLiteral("中文(繁體)"), QStringLiteral("zh_TW"));
    combo->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    combo->addItem(QStringLiteral("日本語"), QStringLiteral("ja_JP"));
    combo->addItem(QStringLiteral("한국어"), QStringLiteral("ko_KR"));
    combo->addItem(QStringLiteral("Français"), QStringLiteral("fr_FR"));
    combo->addItem(QStringLiteral("Deutsch"), QStringLiteral("de_DE"));
    combo->addItem(QStringLiteral("Español"), QStringLiteral("es_ES"));
    combo->addItem(QStringLiteral("Русский"), QStringLiteral("ru_RU"));
}

// 把 QSS 里的 @Token@ 占位符换成 UiStyle 常量：界面代码里不再出现裸色值。
QString withColorTokens(QString qss)
{
    // 令牌值按当前主题出深/浅两套（UiStyle::pick）；浅色值 2026-11-04 首版，
    // 以 Win11 浅色控件为基准，后续按实截图微调。
    static const struct {
        const char *key;
        const char *(*fn)();
    } tokens[] = {
        { "@DialogBg@",        []{ return UiStyle::pick("#232427", "#f2f3f5"); } },
        { "@GroupBg@",         []{ return UiStyle::pick("#222427", "#ffffff"); } },
        { "@Line@",            []{ return UiStyle::pick("rgba(255,255,255,0.085)", "rgba(0,0,0,0.085)"); } },
        { "@HeadBg@",          []{ return UiStyle::pick("#292b2f", "#e8e9eb"); } },
        { "@HeadText@",        []{ return UiStyle::pick("#9ca2aa", "#5f6368"); } },
        { "@Body@",            []{ return UiStyle::pick("#d5d7da", "#202124"); } },
        { "@Field@",           []{ return UiStyle::pick("#2d2f34", "#ffffff"); } },
        { "@FieldText@",       []{ return UiStyle::pick("#e0e2e4", "#1b1b1b"); } },
        { "@Border@",          []{ return UiStyle::pick("#787878", "#8a8a8a"); } },
        { "@BorderHover@",     []{ return UiStyle::pick("#686d75", "#6b6b6b"); } },
        { "@Accent@",          []{ return UiStyle::pick("#55c0f0", "#1689bd"); } },
        { "@DisabledTextSoft@",[]{ return UiStyle::pick("#6b7078", "#a0a0a0"); } },
        { "@DisabledBorder@",  []{ return UiStyle::pick("#3a3c40", "#d0d0d0"); } },
        { "@DisabledBg@",      []{ return UiStyle::pick("#26282b", "#f0f0f0"); } },
        { "@HintText@",        []{ return UiStyle::pick("#9aa0a6", "#6b7075"); } },
        { "@DisabledText@",    []{ return UiStyle::pick("#6f747a", "#a0a0a0"); } },
        { "@PureWhite@",       []{ return UiStyle::pick("#ffffff", "#101113"); } },
        { "@DangerBg@",        []{ return UiStyle::pick("rgba(196,43,28,0.20)", "rgba(196,43,28,0.12)"); } },
        { "@SetGroupBg@",      []{ return UiStyle::setGroupBg(); } },
        { "@SetGroupLine@",    []{ return UiStyle::setGroupLine(); } },
        { "@RowHover@",        []{ return UiStyle::setRowHover(); } },
        { "@RowTag@",          []{ return UiStyle::setRowTag(); } },
        // 「一般」v2 母版定稿的语义角色，全部映射到 UiStyle::fluent 令牌，
        // 设置页内部不另存色值。旧版蓝灰色值已全部作废。
        { "@GenTitleBg@",      []{ return UiStyle::fluent::titleBarBg(); } },
        { "@GenNavBg@",        []{ return UiStyle::fluent::navBg(); } },
        { "@OutlineBg@",       []{ return UiStyle::fluent::outlineBg(); } },
        { "@OutlineHeader@",   []{ return UiStyle::fluent::outlineAlt(); } },
        { "@OutlineDisabled@", []{ return UiStyle::fluent::outlineDisabled(); } },
        { "@GenContentBg@",    []{ return UiStyle::fluent::contentBg(); } },
        { "@GenCardBg@",       []{ return UiStyle::fluent::cardBg(); } },
        { "@GenCardLine@",     []{ return UiStyle::fluent::cardLine(); } },
        { "@GenRowLine@",      []{ return UiStyle::fluent::rowLine(); } },
        { "@GenFrameLine@",    []{ return UiStyle::fluent::frameLine(); } },
        { "@GenFooterLine@",   []{ return UiStyle::fluent::footerLine(); } },
        { "@GenText@",         []{ return UiStyle::fluent::text(); } },
        { "@GenDesc@",         []{ return UiStyle::fluent::desc(); } },
        { "@GenAccent@",       []{ return UiStyle::fluent::focus(); } },
        { "@GenOnAccent@",     []{ return UiStyle::fluent::navSelText(); } },
        { "@GenNavSel@",       []{ return UiStyle::fluent::navSelected(); } },
        { "@ComboPressed@",    []{ return UiStyle::fluent::controlPressed(); } },
        { "@GenHover@",        []{ return UiStyle::fluent::hover(); } },
        { "@GenCtrlBg@",       []{ return UiStyle::fluent::controlBg(); } },
        { "@GenCtrlLine@",     []{ return UiStyle::fluent::controlLine(); } },
        { "@GenDisabled@",     []{ return UiStyle::fluent::disabled(); } },
        { "@GenStatusOk@",     []{ return UiStyle::fluent::statusOk(); } },
        { "@GenStatusWarn@",   []{ return UiStyle::fluent::statusWarn(); } },
        { "@GenError@",        []{ return UiStyle::fluent::error(); } },
        { "@GenDangerBg@",     []{ return UiStyle::fluent::dangerBg(); } },
        { "@GenDangerLine@",   []{ return UiStyle::fluent::dangerLine(); } },
        { "@GenDangerIconText@", []{ return UiStyle::fluent::dangerIconText(); } },
    };
    for (const auto &t : tokens)
        qss.replace(QLatin1String(t.key), QLatin1String(t.fn()));
    return qss;
}

// 追加的几何/局部样式：只用 UiStyle 令牌，不引入新色板。
QString extraQss()
{
    return withColorTokens(QStringLiteral(R"(
QDialog { background: transparent; }
QFrame#dlgTitleBar { border-top-left-radius: 10px; border-top-right-radius: 10px; }
QFrame#dlgFooter { border-bottom-left-radius: 10px; border-bottom-right-radius: 10px; }

QFrame#formatGroup { background: @GroupBg@; border: 1px solid @Line@;
    border-radius: 7px; }
/* 设置页分组卡片（2026-11-07 排版统一）：低对比组底 + 1px 描边，行与行之间
   用同色 1px 分隔；整行悬停只提亮背景，不改控件尺寸。 */
QFrame#setGroup { background: @SetGroupBg@; border: 1px solid @SetGroupLine@;
    border-radius: 7px; }
QWidget#settingRow { background: transparent; }
QWidget#settingRow:hover { background: @RowHover@; border-radius: 6px; }
QWidget#rowControls { background: transparent; }
QFrame#rowLine { background: @SetGroupLine@; border: 0; max-height: 1px; }
QLabel#rowDesc { color: @HintText@; font-size: 11px; }
QLabel#rowTag { color: @RowTag@; font-size: 11px; }
QLabel#rowUnit { color: @HintText@; font-size: 11px; }
QLabel#setNote { color: @HintText@; font-size: 11px; }
QLabel#navEmpty { color: @HintText@; font-size: 11px; padding: 2px 2px 6px 2px; }
QFrame#keyTable { border: 1px solid @Line@; border-radius: 7px; }
QWidget#keyHead { background: @HeadBg@; }
QLabel#keyHeadCell { color: @HeadText@; font-size: 11px; }
QLabel#pageHint { color: @HintText@; font-size: 11px; }

QPushButton#miniBtn { min-height: 18px; max-height: 22px; padding: 0 9px; font-size: 10px;
    border-radius: 5px; }
/* 颜色按钮（ColorPickButton）已随第 18 期「背景段下线」删除，QSS 一并清掉 */
/* 键盘页「操作」列：只读动作名（原 flatCombo 可改绑，2026-11-04 锁死为标签） */
QLabel#keyAction { color: @Body@; font-size: 12px; }
/* 键盘页整行悬停（2026-11-09）：与一般页 settingRow 对齐，只提亮行底，
   不改行高/内边距。冲突行必须保持危险底色——`#keyRow:hover` 与
   `#keyRow[conflict="true"]` 特异性相同（id+伪类 / id+属性），冲突规则必须排在其后。 */
QWidget#keyRow { background: transparent; }
QWidget#keyRow:hover { background: @RowHover@; }
QWidget#keyRow[conflict="true"], QWidget#keyRow[conflict="true"]:hover {
    background: transparent; }

QLabel#valueText { color: @Body@; font-size: 11px; }

/* 键盘页热键编辑器（批次 D1 / 2026-11-06 下沿修复）：
   QKeySequenceEdit 是纯 QWidget 容器，自身不绘制 QSS 背景与边框——实际可见的
   底座/描边来自内部 QLineEdit。内部编辑框继承全局 QLineEdit 的 min-height:26px，
   在 26px 高的容器里被裁掉底部 1px 描边（用户图里从 Ctrl+O 到 Del 下边框全消失）。
   改法：容器透明无边框，底座/描边/悬停/焦点全部落到内部 QLineEdit，并解除
   min-height，让四边框完整落在容器内。焦点态由内部 QLineEdit:focus 提供。 */
QKeySequenceEdit { background: transparent; border: 0; padding: 0; }
QKeySequenceEdit QLineEdit { background: @Field@; color: @FieldText@;
    border: 1px solid @Border@; border-radius: 5px; font-size: 11px;
    padding: 0 6px; min-height: 0; }
QKeySequenceEdit QLineEdit:hover { border-color: @BorderHover@; }
QKeySequenceEdit QLineEdit:disabled { color: @DisabledTextSoft@;
    border-color: @DisabledBorder@; background: @DisabledBg@; }

/* 键盘页冲突态由 v2 段按键帽/文字表达，不再整行染色（2026-11-11 修订） */

/* 置灰（尚未实现）项的统一不可用态：文字压暗、底座与边框退到背景层，
   与可交互项一眼可分。UiStyle::dialogQss() 只给了 indicator:disabled，
   没有文字色，直接 setEnabled(false) 时文字仍与正常项同亮度、看不出差别，
   所以这里补一套。设置页底色由 paintEvent 固定为 DialogBg，DisabledText
   对比度约 3.3:1（满足禁用态 ≥3:1），可读且明确「不可点」。 */
QCheckBox:disabled { color: @DisabledText@; }
QLabel:disabled { color: @DisabledText@; }
/* 主题区「浅色模式」整行灰显：rowLabel / hint 是 id 选择器，特异性高于
   QLabel:disabled，必须显式补 id+pseudo，否则 setEnabled(false) 后文字亮度不变。 */
QLabel#rowLabel:disabled, QLabel#rowDesc:disabled,
QLabel#hint:disabled { color: @DisabledText@; }
QComboBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
/* 下拉六态（SET-04 阶段 C）：底座/悬停/按下/焦点/展开列表全部走主题令牌。
   QComboBox:pressed 在部分平台不触发，另用 :open 覆盖弹层打开时的按下观感；
   焦点态用强调色描边，键盘 Tab 进入时可见，不加额外光晕避免改变控件尺寸。 */
QComboBox { background: @Field@; color: @FieldText@; border: 1px solid @Border@; }
QComboBox:hover { border-color: @BorderHover@; }
QComboBox:pressed, QComboBox:open { background: @HeadBg@; border-color: @Accent@; }
QComboBox:focus { border-color: @Accent@; }
QComboBox QAbstractItemView { background: @GroupBg@; color: @FieldText@;
    border: 1px solid @Line@; border-radius: 6px; padding: 4px;
    outline: 0; selection-background-color: @HeadBg@; selection-color: @FieldText@; }
QComboBox QAbstractItemView::item { min-height: 24px; padding: 0 8px; }
QComboBox QAbstractItemView::item:hover { background: @HeadBg@; }
QComboBox QAbstractItemView::item:selected { background: @HeadBg@; color: @FieldText@; }
QComboBox QAbstractItemView::item:disabled { color: @DisabledText@; }
/* 数值框（2026-11-07）：与下拉框同一套深浅主题边界与焦点样式。浅色主题
   此前只有全局 appQss（无 QSpinBox 规则）导致「只剩一条底线」，这里补齐。 */
QSpinBox { background: @Field@; color: @FieldText@; border: 1px solid @Border@;
    border-radius: 5px; padding: 0 6px; font-size: 12px; min-height: 28px; }
QSpinBox:hover { border-color: @BorderHover@; }
QSpinBox:focus { border-color: @Accent@; }
QSpinBox:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QLineEdit:disabled { color: @DisabledText@; background: @DisabledBg@; border-color: @DisabledBorder@; }
QPushButton:disabled { color: @DisabledText@; border-color: @DisabledBorder@; background: @HeadBg@; }

/* ===== 一般 / 键盘 两页样板（2026-11-09）=================================
   用属性选择器与 #keyTable / #keyLegend 后代限定，只作用于这两页；
   其余九页、菜单和弹窗不套用。色值一律走上面的 @Token@，不在此另造。 */
/* 一般页：分组标题提级为独立层级（页标题 21px → 说明 12px → 分组标题 13px/600） */
QLabel#sectionTitle[variant="page"] { font-size: 13px; font-weight: 600;
    color: @Body@; padding-bottom: 2px; }
/* 键盘页：表头与行区分层；三列表头加重，下边线用组卡片同色 1px */
QWidget#keyHead { border-bottom: 1px solid @SetGroupLine@; }
QLabel#keyHeadCell { font-size: 11px; font-weight: 600; color: @HeadText@; }
/* 状态图例：默认（鼠标移开）画面里就能读出热键框四种状态的含义 */
QWidget#keyLegend { background: transparent; }
QLabel#keyLegendChip { font-size: 10px; padding: 2px 8px; border-radius: 4px;
    background: @Field@; color: @FieldText@; border: 1px solid @Border@; }
QLabel#keyLegendChip[state="empty"] { background: transparent;
    color: @HintText@; border-style: dashed; }
QLabel#keyLegendChip[state="editing"] { color: @Accent@; border-color: @Accent@; }
QLabel#keyLegendChip[state="conflict"] { background: @DangerBg@;
    border-color: @DangerBg@; color: @Body@; }
/* 热键输入框：未设置用虚线 + 提示色；正在录入用强调色文字（不改边框宽度，
   避免 26px 容器里再次出现裁边）；冲突由整行 [conflict] 级联，压过前两态。 */
QKeySequenceEdit[state="empty"] QLineEdit { border-style: dashed; color: @HintText@; }
QKeySequenceEdit[state="editing"] QLineEdit { color: @Accent@; border-color: @Accent@; }
QWidget#keyRow[conflict="true"] QKeySequenceEdit QLineEdit {
    background: @Field@; border-color: @DangerBg@; color: @FieldText@; }

/* ===== 设置 › 一般 v2 母版定稿（2026-11-11）============================
   唯一基准：QuikView-一般页-母版定稿/{dark,light,compact}-v2.png +
   颜色与尺寸标注.md。上一版蓝灰色板（#24262B 系 / #83BBFF / 蓝色开关）
   已作废，这里不保留任何旧值。外壳规则只覆盖设置对话框内具名对象；
   一般页规则一律带 [fluent="v2"]。色值全部走 @Gen*@ 令牌。 */
/* —— 外壳：中性石墨黑三档表面，禁止蓝灰 / 蓝紫 —— */
/* 2026-11-17 去线：标题栏不再压底部分隔线，靠 #1a1b1d / #1c1d1f / #252628 三档
   底色直接分区（dialogQss 里那条旧 border-bottom 由这里的 border-bottom: 0 显式压掉，
   其他对话框不受影响）。底栏只留一道极淡顶线，保住「正文滚动区 / 固定按钮栏」的边界。 */
QWidget#dlgTitleBar { background: @GenTitleBg@; border-bottom: 0; }
QLabel#dlgTitle { color: @GenDesc@; font-size: 11px; }
QWidget#navRail { background: @GenNavBg@; }
QFrame#navDivider { background: @GenCardLine@; border: 0; }
/* 导航/正文之间的 1px 占位：2026-11-17 起与导航栏同色，等于没有竖线。 */
QFrame#navRailSeam { background: @GenNavBg@; border: 0; }
QWidget#contentPane { background: @GenContentBg@; }
QWidget#dlgFooter { background: @GenTitleBg@; border-top: 1px solid @GenFooterLine@; }
QLabel#footerNote { color: @GenDesc@; font-size: 11px; }
QLabel#navCaption { color: @GenDesc@; font-size: 11px; }
QLineEdit#navSearch { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; padding: 0 10px; font-size: 11px; }
QLineEdit#navSearch[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }
QFrame#hLine { background: @GenCardLine@; }
QPushButton#closeBtn { color: @GenDesc@; }
QPushButton#closeBtn:hover { background: @GenHover@; color: @GenText@; }

/* 11 项导航：整行选中 #1688F8（圆角 8），彩色图标底板见自绘 */
QPushButton#navItem { background: transparent; border: 1px solid transparent;
    border-radius: 8px; color: @GenText@; font-size: 13px; text-align: left;
    padding: 0 10px; min-height: 36px; }
QPushButton#navItem:hover { background: @GenHover@; }
QPushButton#navItem:checked { background: @GenNavSel@; color: @GenOnAccent@;
    font-weight: 600; }
QPushButton#navItem[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }
QPushButton#navItem:disabled { color: @GenDisabled@; }

QLineEdit#appMenuFind {
    background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; padding: 0 7px; font-size: 12px;
}
QLineEdit#appMenuFind[kbFocus="true"]:focus {
    border: 2px solid @GenAccent@; padding: 0 6px; min-height: 28px; max-height: 28px;
}
QPushButton#btnResetAppMenu {
    background: @GenCtrlBg@; color: @GenText@; border: 1px solid @GenCtrlLine@;
    border-radius: 7px; padding: 0 10px; font-size: 12px;
}
QPushButton#btnResetAppMenu:hover { background: @GenHover@; }
QPushButton#btnResetAppMenu[kbFocus="true"]:focus { border: 2px solid @GenAccent@; }
QFrame#appMenuSurface {
    background: @OutlineBg@; border: 1px solid @GenCardLine@; border-radius: 9px;
}
QWidget#appMenuColumnHeader {
    background: @OutlineHeader@; border-bottom: 1px solid @GenCardLine@;
    min-height: 30px; max-height: 30px; border-top-left-radius: 8px; border-top-right-radius: 8px;
}
QWidget#appMenuColumnHeader QLabel { color: @GenDesc@; font-size: 12px; font-weight: 600; }
QScrollArea#appMenuScroll, QScrollArea#appMenuScroll QWidget#qt_scrollarea_viewport {
    background: transparent; border: 0;
}
QScrollArea#appMenuScroll QScrollBar:vertical { width: 10px; margin: 4px 2px; }
QScrollArea#appMenuScroll QScrollBar::handle:vertical {
    background: @OutlineDisabled@; border-radius: 2px; margin: 0 3px;
}
QScrollArea#appMenuScroll QScrollBar::handle:vertical:disabled { background: transparent; }
QLabel#appMenuName, QLabel#appMenuBranchName { color: @GenText@; font-size: 13px; }
QLabel#appMenuBranchName { font-weight: 600; }
QLabel#appMenuDescription, QLabel#appMenuMatch { color: @GenDesc@; font-size: 11px; }
QLabel#appMenuName:disabled, QLabel#appMenuBranchName:disabled,
QLabel#appMenuDescription:disabled { color: @OutlineDisabled@; }

/* 页标题 23/600，说明 11 */
QLabel#pageTitle { font-size: 23px; font-weight: 600; color: @GenText@; }
QLabel#pageDesc { font-size: 11px; color: @GenDesc@; }

/* 底栏全部中性：恢复默认 / 取消为文字钮，保存设置为中性描边钮 */
QPushButton#btnSubtle, QPushButton#genNeutral {
    background: transparent; color: @GenText@; border: 1px solid transparent;
    border-radius: 7px; min-height: 30px; padding: 0 12px; font-size: 13px; }
QPushButton#btnSubtle:hover, QPushButton#genNeutral:hover { background: @GenHover@; }
QPushButton#btnPrimary { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px; min-height: 30px;
    padding: 0 12px; font-size: 13px; font-weight: 600; }
QPushButton#btnPrimary:hover { background: @GenHover@; }
QPushButton#btnPrimary[kbFocus="true"]:focus, QPushButton#btnSubtle[kbFocus="true"]:focus,
QPushButton#genNeutral[kbFocus="true"]:focus { border: 2px solid @GenAccent@; padding: 0 9px; }

/* —— 一般页：中性灰卡片，极弱描边，靠明暗与间距分组 —— */
QFrame#setGroup[fluent="v2"] { background: @GenCardBg@;
    border: 1px solid @GenCardLine@; border-radius: 11px; }
/* 2026-11-17 去描边（先只落「一般」页）：卡片 #303133 压 #252628 内容底，
   本身已经分得开，外圈那条 1px 线取消；组内多行仍用 @GenRowLine@ 的淡分隔线，
   单行组（首尾翻页）只剩圆角底色。其他页由 genEdge 缺省值继续保留原描边。
   描边改成与卡片同色而不是 border: 0：帧宽仍占 1px，卡片矩形、行基线、
   分组间距与改前逐像素一致，切页也不会跳。 */
QFrame#setGroup[fluent="v2"][genEdge="none"] { background: @GenCardBg@;
    border: 1px solid @GenCardBg@; border-radius: 11px; }
QFrame#rowLine[fluent="v2"] { background: @GenRowLine@;
    margin-left: 14px; margin-right: 14px; }
QWidget#settingRow[fluent="v2"] { background: transparent; }
QWidget#settingRow[fluent="v2"]:hover { background: @GenHover@; border-radius: 10px; }
QWidget#settingRow[comboRow="true"]:hover,
QWidget#settingRow[popupSuppressHover="true"]:hover { background: transparent; }
QLabel#appMenuPreviewTitle { color: @GenText@; font-size: 15px; font-weight: 600; }
QLabel#appMenuPreviewHint { color: @GenDesc@; font-size: 11px; }
QLabel#sectionTitle[fluent="v2"] { font-size: 13px; font-weight: 600;
    color: @GenText@; }
QLabel#rowLabel[fluent="v2"] { font-size: 13px; color: @GenText@; }
QLabel#rowDesc[fluent="v2"] { font-size: 11px; color: @GenDesc@; }
QLabel#setNote[fluent="v2"] { font-size: 11px; color: @GenDesc@; }

QPushButton[fluent="v2"] { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; padding: 0 12px; font-size: 13px; }
QPushButton[fluent="v2"]:hover, QPushButton[fluent="v2"]:pressed {
    background: @GenHover@; }
QPushButton[fluent="v2"][kbFocus="true"]:focus { border: 2px solid @GenAccent@;
    padding: 0 9px; }
QPushButton[fluent="v2"]:disabled { color: @GenDisabled@;
    border-color: @GenCtrlLine@; background: @GenCtrlBg@; }

QComboBox[fluent="v2"] { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 7px;
    min-height: 30px; max-height: 30px; font-size: 13px;
    padding: 0 26px 0 9px; }
QComboBox[fluent="v2"]:focus { border-color: @GenCtrlLine@; }
QComboBox[fluent="v2"]:hover { background: @GenHover@; border-color: @GenDesc@; }
QComboBox[fluent="v2"]:pressed { background: @ComboPressed@; border-color: @GenDesc@; }
QComboBox[fluent="v2"][kbFocus="true"]:focus {
    border: 2px solid @GenAccent@; padding: 0 25px 0 8px; }
/* 菜单展开不画焦点环：字段保持普通描边，环只在键盘导航进入时出现。 */
QComboBox[fluent="v2"]:open { background: @GenCtrlBg@; border-color: @GenControlLine@; }
/* 箭头由 PopupComboBox::paintEvent 自绘，QSS 不再画一份 */
QComboBox[fluent="v2"]::drop-down { border: 0; width: 26px; }
QComboBox[fluent="v2"]::down-arrow { image: none; width: 0; height: 0; }
QComboBox[fluent="v2"]:disabled { color: @GenDisabled@;
    border-color: @GenCtrlLine@; background: @GenCtrlBg@; }
QComboBox[fluent="v2"] QAbstractItemView { background: @GenCardBg@;
    color: @GenText@; border: 1px solid @GenCardLine@; border-radius: 8px;
    padding: 4px; outline: 0; selection-background-color: @GenNavSel@;
    selection-color: @GenOnAccent@; }
QComboBox[fluent="v2"] QAbstractItemView::item { min-height: 26px;
    padding: 0 8px; }
QComboBox[fluent="v2"] QAbstractItemView::item:selected {
    background: @GenNavSel@; color: @GenOnAccent@; }
QComboBox[fluent="v2"] QAbstractItemView::item:disabled { color: @GenDisabled@; }

/* v2：普通勾选框（格式关联等）与键盘表格，同一套令牌，不退回蓝灰 */
QCheckBox { color: @GenText@; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid @GenCtrlLine@;
    border-radius: 4px; background: @GenCtrlBg@; }
QCheckBox::indicator:hover { border-color: @GenDesc@; }
QCheckBox::indicator:checked { background: @GenAccent@; border-color: @GenAccent@;
    image: url(:/ui/check-light.png); }
QCheckBox::indicator:disabled { border-color: @GenDisabled@; background: @GenCtrlBg@; }
QFrame#keyTable { background: @GenCardBg@; border: 1px solid @GenCardLine@;
    border-radius: 11px; }
/* 表头与行同底色，只靠下分隔线与 600 字重区分；分组头压到内容色，形成
   「表头 → 组 → 行」三级层次（设计图 04/07 实测 #303133 / #252628 / #303133）。 */
QWidget#keyHead { background: @GenCardBg@; border-bottom: 1px solid @GenRowLine@; }
QLabel#keyHeadCell { color: @GenDesc@; font-size: 11px; font-weight: 600; }
QLabel#keyAction { color: @GenText@; font-size: 13px; }
QWidget#keyRow { background: transparent; }
QWidget#keyRow:hover { background: @GenHover@; }
QWidget#keyRow[editing="true"]:hover,
QWidget#keyRow[conflict="true"], QWidget#keyRow[conflict="true"]:hover {
    background: transparent; }
QFrame#keyRowLine { background: @GenRowLine@; }
QWidget#keyGroupHead { background: @GenContentBg@; }
QLabel#keyGroupTitle { color: @GenDesc@; font-size: 11px; font-weight: 600; }
/* 行内状态：录入态蓝字、冲突态红字，和键帽边框一起表达 */
QLabel#keyRowState { font-size: 11px; }
QLabel#keyRowState[state="editing"] { color: @GenAccent@; }
QLabel#keyRowState[state="conflict"] { color: @GenError@; }

/* 冲突红条（设计图 06）：深色 #382C2D / 浅色 #FDECEC，文字用状态红 */
QWidget#keyBanner { background: @GenDangerBg@; border: 1px solid @GenDangerLine@;
    border-radius: 8px; }
QLabel#keyBannerIcon { background: @GenError@; color: @GenDangerIconText@;
    border-radius: 8px; font-size: 11px; font-weight: 600; }
QLabel#keyBannerText { color: @GenError@; font-size: 12px; }

/* 冲突行：键帽描边转红 + 行内「冲突」红字（设计图 06 行底不变，不做整行染色） */
QWidget#keyRow[conflict="true"] QKeySequenceEdit QLineEdit {
    background: @GenCtrlBg@; border: 1px solid @GenError@; color: @GenError@; }

QKeySequenceEdit QLineEdit { background: @GenCtrlBg@; color: @GenText@;
    border: 1px solid @GenCtrlLine@; border-radius: 6px; font-size: 12px;
    min-height: 0; max-height: 26px; padding: 0 8px; }
QKeySequenceEdit QLineEdit:hover { border-color: @GenDesc@; }
/* 全局 QLineEdit:focus 会把内部编辑框染成浅蓝；v2 只允许键盘导航画环，
   指针模式下焦点保持普通描边（2026-11-11 焦点规则）。 */
QKeySequenceEdit QLineEdit:focus { border-color: @GenCtrlLine@; }
QKeySequenceEdit QLineEdit[kbFocus="true"]:focus { border: 2px solid @GenAccent@;
    padding: 1px 7px; }
QKeySequenceEdit QLineEdit:disabled { color: @GenDisabled@; border-color: @GenCtrlLine@; }

/* 启用列 checkbox 由 V2CheckBox 自绘（16×16 中性填充 + 对比勾 + 键盘焦点环） */

/* 保存按钮在冲突时禁用：中性底 + 压暗文字，不保留强调描边 */
QPushButton#btnPrimary:disabled { color: @GenDisabled@; border-color: @GenCtrlLine@;
    background: @GenCtrlBg@; }
)"));
}

} // namespace

namespace {

// 设置窗构建分段计时（验收探针）：QUIKVIEW_SETTINGS_PROBE=<path> 时每次构建
// 追加一行 "stage,ms"。未设置环境变量时每个打点只做一次静态判断，零开销。
inline QElapsedTimer &probeClock()
{
    static QElapsedTimer t;
    static bool started = false;
    if (!started) {
        t.start();
        started = true;
    }
    return t;
}

void probeMark(const char *stage, qint64 us)
{
    static const QByteArray probePath = qgetenv("QUIKVIEW_SETTINGS_PROBE");
    if (probePath.isEmpty())
        return;
    if (FILE *f = std::fopen(probePath.constData(), "a")) {
        std::fprintf(f, "%s,%.1f\n", stage, us / 1000.0);
        std::fclose(f);
    }
}

} // namespace

// ===========================================================================
// 构造：深色三栏骨架 + 逐页构建
// ===========================================================================

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    const auto mark = [](const char *stage) {
        probeMark(stage, probeClock().nsecsElapsed() / 1000);
    };
    mark("ctor-start");
    setWindowTitle(tr("设置"));
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    // 非模态（2026-11-09）：原来 setModal(true) + exec() 会把主窗口在 Win32 层
    // disable，主窗口收不到鼠标消息、标题栏拖不动——需求池「设置打开时主窗口
    // 无法拖动」的成因。设置页是即时写盘，不需要模态来保证一致性；关闭收尾由
    // MainWindow::finishSettings() 负责。设置窗口自身拖动见 mousePressEvent。
    setModal(false);
    resize(kDlgW, kDlgH);
    setMinimumSize(kDlgMinW, kDlgMinH);
    setFont(UiStyle::font(12));
    setStyleSheet(UiStyle::dialogQss() + extraQss());

    // Qt's Windows geometry check consults height-for-width even with SetNoConstraint.
    // Scrollable pages can fit in the configured minimum without growing the window.
    class SettingsLayout : public QVBoxLayout
    {
    public:
        using QVBoxLayout::QVBoxLayout;
        int minimumHeightForWidth(int) const override { return kDlgMinH; }
    };
    auto *root = new SettingsLayout(this);
    root->setContentsMargins(1, 1, 1, 1);
    root->setSpacing(0);
    // 允许窗口缩到 900x660：布局最小高（约 716）不得反向撑住窗口，
    // 超出部分由右侧内容区滚动承担（v2 窄窗口径）。
    root->setSizeConstraint(QLayout::SetNoConstraint);

    // —— 标题栏：⚙ 设置 … × ——
    auto *titleBar = new QFrame(this);
    titleBar->setObjectName(QStringLiteral("dlgTitleBar"));
    m_titleBar = titleBar;
    titleBar->setFixedHeight(kTitleBarH);
    auto *tl = new QHBoxLayout(titleBar);
    tl->setContentsMargins(18, 0, 14, 0);
    tl->setSpacing(9);
    auto *title = new QLabel(tr("QuikView — 设置"), titleBar);
    title->setObjectName(QStringLiteral("dlgTitle"));
    tl->addWidget(title);
    tl->addStretch(1);
    auto *btnClose = new QPushButton(QStringLiteral("\u2715"), titleBar);
    btnClose->setObjectName(QStringLiteral("closeBtn"));
    btnClose->setCursor(Qt::PointingHandCursor);
    btnClose->setFixedSize(32, 30);
    tl->addWidget(btnClose);
    connect(btnClose, &QPushButton::clicked, this, [this]() { reject(); });
    root->addWidget(titleBar);

    // —— 主体：导航 | 竖分隔 | 内容 ——
    auto *body = new QWidget(this);
    auto *bl = new QHBoxLayout(body);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(0);

    auto *nav = new QFrame(body);
    nav->setObjectName(QStringLiteral("navRail"));
    nav->setFixedWidth(kNavW);
    m_navRail = nav;
    m_navLayout = new QVBoxLayout(nav);
    m_navLayout->setContentsMargins(10, 16, 10, 16);
    m_navLayout->setSpacing(kGenNavSpacing);   // v2：行距 4（窄窗 2）
    // 侧栏搜索（2026-11-07 排版统一）：按设置名匹配，命中页签保留并跳转；
    // 清空恢复全部导航，无匹配给明确反馈。
    m_search = new QLineEdit(nav);
    m_search->setObjectName(QStringLiteral("navSearch"));
    m_search->setPlaceholderText(tr("搜索设置…"));
    m_search->setClearButtonEnabled(true);
    m_search->setFixedHeight(kGenCtrlH);
    m_search->setAccessibleName(tr("搜索设置"));
    m_navLayout->addWidget(m_search);
    m_navLayout->addSpacing(10);
    auto *caption = new QLabel(tr("偏好设置"), nav);
    caption->setObjectName(QStringLiteral("navCaption"));
    m_navLayout->addWidget(caption);
    m_navLayout->addSpacing(10);
    m_navLayout->addWidget(m_searchEmpty = new QLabel(tr("没有匹配的设置"), nav));
    m_searchEmpty->setObjectName(QStringLiteral("navEmpty"));
    m_searchEmpty->setVisible(false);
    bl->addWidget(nav);

    // 2026-11-17 去线：导航栏与正文之间不再画 1px 竖线。宽度 1px 的占位仍然留着
    // （updateContentColumnWidth() 的列宽算式按它扣了 1px，删掉会连带改列宽），
    // 只把它的底色改成导航底色 → 线与导航栏同色，视觉上不再有分隔线。
    auto *rail = new QFrame(body);
    rail->setObjectName(QStringLiteral("navRailSeam"));
    rail->setFixedWidth(1);
    bl->addWidget(rail);

    auto *content = new QFrame(body);
    content->setObjectName(QStringLiteral("contentPane"));
    auto *cl = new QVBoxLayout(content);
    // 外壳：内容列由 updateContentColumnWidth() 居中限宽到 kGenContentMaxW
    // （2026-11-12 收窗：默认 920 下卡片 638、两侧各留 36/37）。
    cl->setContentsMargins(0, 25, 0, 20);
    cl->setSpacing(0);
    m_contentLay = cl;
    auto *column = new QWidget(content);
    column->setObjectName(QStringLiteral("contentColumn"));
    column->setMaximumWidth(kGenContentMaxW);
    auto *cvl = new QVBoxLayout(column);
    // 左右留白由 updateContentColumnWidth() 统一算（含 pane 内边距），
    // 这里不再叠一层，否则卡片会被吃掉 56px（2026-11-12 收窗实测）。
    cvl->setContentsMargins(0, 0, 0, 0);
    cvl->setSpacing(0);
    m_contentColumn = column;

    m_pageTitle = new QLabel(tr("浏览"), column);
    m_pageTitle->setObjectName(QStringLiteral("pageTitle"));
    m_pageDesc = new QLabel(column);
    m_pageDesc->setObjectName(QStringLiteral("pageDesc"));
    m_pageDesc->setWordWrap(true);
    // 页标题 / 分组标题 / 行文案共享同一条左边线：卡片内行有 11px 内边距，
    // 标题同样左缩进 11px。一般页由 selectPage 把缩进归零，让标题与卡片左缘对齐。
    m_pageTitle->setContentsMargins(11, 0, 0, 0);
    m_pageDesc->setContentsMargins(11, 0, 0, 0);
    auto *headBox = new QWidget(column);
    auto *hb = new QHBoxLayout(headBox);
    hb->setContentsMargins(0, 0, 0, 0);
    hb->setSpacing(8);
    // 标题与副标题纵向排；右侧留一个页专属动作位（「恢复默认菜单」）。
    // 动作位在其它页隐藏，页头排版与既有页面一致。
    auto *headText = new QWidget(headBox);
    auto *ht = new QVBoxLayout(headText);
    ht->setContentsMargins(0, 0, 0, 0);
    ht->setSpacing(5);
    ht->addWidget(m_pageTitle);
    ht->addWidget(m_pageDesc);
    hb->addWidget(headText, 1);
    m_pageActionHost = new QWidget(headBox);
    auto *ha = new QHBoxLayout(m_pageActionHost);
    ha->setContentsMargins(0, 0, 0, 0);
    ha->setSpacing(0);
    m_pageActionHost->setVisible(false);
    hb->addWidget(m_pageActionHost, 0, Qt::AlignTop | Qt::AlignRight);
    cvl->addWidget(headBox);
    // 页头分隔线（原型 .qv-page-head 的 border-bottom）。定稿的一般页无此线，
    // 由 selectPage 只在该页隐藏；其余页保持原样。
    auto *headLine = new QFrame(column);
    headLine->setObjectName(QStringLiteral("hLine"));
    headLine->setFixedHeight(1);
    m_headLine = headLine;
    m_headGapTop = new QSpacerItem(0, 13);
    m_headGapBottom = new QSpacerItem(0, 15);
    cvl->addSpacerItem(m_headGapTop);
    cvl->addWidget(headLine);
    cvl->addSpacerItem(m_headGapBottom);

    m_stack = new QStackedWidget(column);
    cvl->addWidget(m_stack, 1);
    // 内容列居中：宽由 updateContentColumnWidth() 以 min=max 固定到
    // kGenContentMaxW，AlignHCenter 把剩余空间均分两侧。窗口继续拉宽时
    // 卡片不变宽，只有两侧留白变大（2026-11-12 收窗口径）。
    cl->addWidget(column, 1, Qt::AlignHCenter);
    bl->addWidget(content, 1);

    root->addWidget(body, 1);

    // —— 页面（每页一个 QScrollArea，内容 widget 存 m_pages）——
    // 九页重组（2026-11-21）：通用 / 浏览 / 外观与显示 / 图像处理 / 键盘 /
    // 鼠标与滚轮 / 查看器右键菜单 / Windows 集成 / 扩展。
    // 首屏只建「通用」页：其余页登记构建器，首次进入（或首次搜索）时才构建。
    // 2026-11-16 实测：旧 12 页同步构建占开窗 169ms 中约 160ms，延后后首屏回到个位数毫秒；
    // 九页重组（2026-11-21）沿用同一策略，只建第 0 页。
    buildGeneralPage(addPage(tr("通用"), QString()));
    m_pageBuilt[0] = true;
    addLazyPage(tr("浏览"), QString(), &SettingsDialog::buildViewPage);
    addLazyPage(tr("外观与显示"), QString(), &SettingsDialog::buildAppearancePage);
    addLazyPage(tr("图像处理"), QString(), &SettingsDialog::buildImageProcPage);
    // 键盘页不显示 pageDesc：页首说明由 m_keyHint 承担（默认/录入两态切换），
    // 避免标题下出现两条近似文案（设计图 04/05 只有一行）。
    addLazyPage(tr("键盘"), QString(), &SettingsDialog::buildKeyboardPage);
    addLazyPage(tr("鼠标与滚轮"), QString(), &SettingsDialog::buildMousePage);
    // 查看器右键菜单：QuikView 内的右键菜单，独立一页，与 Windows 侧集成分开。
    m_appMenuPage = addLazyPage(tr("查看器右键菜单"), QString(),
                                &SettingsDialog::buildAppMenuSections);
    // 索引在构造期就定位：列宽/页头动作位等判断不依赖该页是否已构建。
    m_appMenuPageIndex = m_pages.indexOf(m_appMenuPage);
    // Windows 集成页要查资源管理器注册状态（四次注册表探测），放首屏最不划算。
    addLazyPage(tr("Windows 集成"), QString(), &SettingsDialog::buildWindowsPage);
    addLazyPage(tr("扩展"), QString(), &SettingsDialog::buildExtensionsPage);

    // 页面内容不足一屏时，多余空间落在末尾弹簧上（否则各 section 会被均匀拉开）。
    // 弹簧必须在页面内容构建之后加（延后构建的页在 ensurePageBuilt 里补），
    // 否则先加的弹簧会把后建的内容顶到底部。首屏只有「通用」页需要。
    finishPageLayout(0);
    mark("pages");

    // 导航底部：分隔 + 关于
    m_navLayout->addStretch(1);
    auto *navDivider = new QFrame(nav);
    navDivider->setObjectName(QStringLiteral("navDivider"));
    navDivider->setFixedHeight(1);
    m_navLayout->addSpacing(9);
    m_navLayout->addWidget(navDivider);
    m_navLayout->addSpacing(9);
    auto *btnAbout = new NavItemButton(tr("关于 QuikView"), NavGlyph::Info, nav, true);
    m_btnAbout = btnAbout;
    btnAbout->setObjectName(QStringLiteral("navItem"));
    btnAbout->setCursor(Qt::PointingHandCursor);
    btnAbout->setFixedHeight(kGenNavRowH);
    m_navLayout->addWidget(btnAbout);
    connect(btnAbout, &QPushButton::clicked, this, [this]() {
        QMessageBox::about(this, tr("关于 QuikView"),
                           tr("<b>QuikView</b> %1<br>轻量图片浏览器。")
                               .arg(QCoreApplication::applicationVersion()));
    });

    // —— footer：提示 … 恢复默认 / 取消 / 保存设置 ——
    auto *footer = new QFrame(this);
    footer->setObjectName(QStringLiteral("dlgFooter"));
    footer->setFixedHeight(kFooterH);
    m_footer = footer;
    auto *fl = new QHBoxLayout(footer);
    // 定稿页脚：恢复默认贴左栏下方、提示与内容左缘对齐、取消/保存右对齐，
    // 按钮间距 8。148 的间隔按 980 评审画布算（10 + 恢复默认约 76 + 8 + 148
    // ≈ 内容列左缘 242）；窗口变化时提示仍在右栏内、不压按钮。
    fl->setContentsMargins(10, 0, 20, 0);
    fl->setSpacing(8);

    m_btnReset = new QPushButton(tr("恢复默认"), footer);
    m_btnReset->setObjectName(QStringLiteral("btnSubtle"));
    m_btnReset->setCursor(Qt::PointingHandCursor);
    m_btnReset->setFixedHeight(kGenCtrlH);
    connect(m_btnReset, &QPushButton::clicked, this, &SettingsDialog::onReset);
    fl->addWidget(m_btnReset);
    // 提示与卡片左缘对齐：间隔由 updateContentColumnWidth() 按内容列实测左缘
    // 逐帧计算（窗口收窄到 920 后固定 240 会把提示留在 1068 时代的位置）。
    m_footerGap = new QSpacerItem(240, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
    fl->addSpacerItem(m_footerGap);

    // 2026-11-17 注释精简：删除「普通设置即时生效…」这条常驻页脚；页脚平时不说话，
    // 只在有未保存改动时就地提示（updatePendingNote 保留原语气与位置）。
    auto *note = new QLabel(QString(), footer);
    note->setObjectName(QStringLiteral("footerNote"));
    note->hide();
    m_footerNote = note;
    fl->addWidget(note);
    fl->addStretch(1);

    auto *btnCancel = new QPushButton(tr("取消"), footer);
    btnCancel->setObjectName(QStringLiteral("genNeutral"));
    btnCancel->setCursor(Qt::PointingHandCursor);
    btnCancel->setFixedHeight(kGenCtrlH);
    connect(btnCancel, &QPushButton::clicked, this, [this]() { reject(); });
    fl->addWidget(btnCancel);

    m_btnOk = new QPushButton(tr("保存设置"), footer);
    m_btnOk->setObjectName(QStringLiteral("btnPrimary"));
    m_btnOk->setCursor(Qt::PointingHandCursor);
    m_btnOk->setFixedHeight(kGenCtrlH);
    m_btnOk->setMinimumWidth(92);
    m_btnOk->setDefault(true);
    // 保存按钮只走 accept()：校验与写盘都在 accept()/onAccepted() 里统一处理，
    // 冲突时不关闭窗口（P0-1）。
    connect(m_btnOk, &QPushButton::clicked, this, [this]() { accept(); });
    fl->addWidget(m_btnOk);

    root->addWidget(footer);

    // 同页下拉统一宽度（按最长选项）+ 搜索索引；再回填配置与待保存基线。
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pageBuilt.value(i))
            levelComboWidths(m_pages.at(i));
    }
    buildSearchIndex();
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) {
        // 搜索要匹配全部页文案；未构建的页没有控件可匹配，首次搜索时一次性补齐。
        // 这是用户主动操作，付一次构建耗时换「搜得到」。
        if (!text.trimmed().isEmpty())
            ensureAllPagesBuilt();
        filterNavigation(text);
    });

    loadFromConfig();
    mark("load-config");
    selectPage(0);
    mark("select-page0");
    capturePendingBaseline();
    updatePendingNote();
    // 即时项在加载完控件后再接信号：避免开窗时的 setChecked 触发一次写盘。
    connectLiveChecks();
    connectInstantSignals();
    // 全部控件建完后再放开一次：QDialog 默认会把布局最小高写成窗口最小高，
    // 导致 900×660 窄窗被撑到 716。放最后确保覆盖默认约束。
    layout()->setSizeConstraint(QLayout::SetNoConstraint);
    mark("ctor-end");

    // v2 焦点可见性：应用级追踪最近一次输入来源。窗口首次打开没有主动键盘
    // 输入，全部控件保持无环；Tab 之后才由 kbFocus 属性点亮当前焦点控件。
    m_inputMode = new InputModeWatcher(this);
    qApp->installEventFilter(m_inputMode);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *) {
        if (m_inputMode)
            m_inputMode->refresh();
    });

    // 设置窗内的轻提示：直接复用 Chrome::Toast（与主窗同一套外观/图标/时长），
    // 只是把父控件换成设置窗，这样导出/导入结果能贴着当前窗口显示。
    m_toast = new Chrome::Toast(this);
    m_toast->setCorner(true);
}

SettingsDialog::~SettingsDialog()
{
    if (m_inputMode) {
        qApp->removeEventFilter(m_inputMode);
        // 关闭时清理追踪状态，避免下一次打开沿用上一次的键盘模式。
        m_inputMode = nullptr;
    }
}

bool SettingsDialog::keyboardInputMode() const
{
    return m_inputMode && m_inputMode->keyboardMode();
}

void SettingsDialog::refreshFocusVisibility()
{
    if (m_inputMode)
        m_inputMode->refresh();
}

// ===========================================================================
// 页面骨架
// ===========================================================================

QWidget *SettingsDialog::addPage(const QString &title, const QString &desc)
{
    const int index = m_pages.size();

    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *page = new QWidget;
    // 所有页面共用 v2 外壳：卡片 / 行 / 字号 / 控件列都由这枚标记命中。
    page->setProperty("fluent", QStringLiteral("v2"));
    auto *v = new QVBoxLayout(page);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    scroll->setWidget(page);
    m_stack->addWidget(scroll);

    m_pages.append(page);
    m_pageTitles.append(title);
    m_pageDescs.append(desc);
    m_pageBuilders.append(std::function<void()>());
    m_pageBuilt.append(false);

    auto *btn = new NavItemButton(title, navGlyphFor(index), m_navLayout->parentWidget(),
                                  false, index);
    btn->setObjectName(QStringLiteral("navItem"));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(kGenNavRowH);
    connect(btn, &QPushButton::clicked, this, [this, index]() { selectPage(index); });
    m_navLayout->addWidget(btn);
    m_tabs.append(btn);

    return page;
}

QWidget *SettingsDialog::addLazyPage(const QString &title, const QString &desc,
                                    void (SettingsDialog::*builder)(QWidget *))
{
    QWidget *page = addPage(title, desc);
    const int index = m_pages.size() - 1;
    m_pageBuilders[index] = [this, builder, page]() { (this->*builder)(page); };
    return page;
}

void SettingsDialog::finishPageLayout(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    if (index == 4)
        return;
    // 查看器右键菜单页同理：大纲列表自己是一个滚动区，要 stretch 吃满高度；
    // 尾部再加弹簧会与它平分剩余空间，列表只占半屏、下面空一截（2026-11-16）。
    if (index == m_appMenuPageIndex)
        return;
    if (auto *pv = qobject_cast<QVBoxLayout *>(m_pages.at(index)->layout()))
        pv->addStretch(1);
}

void SettingsDialog::ensurePageBuilt(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    if (m_pageBuilt.value(index, true))   // 已建或越界：直接返回
        return;
    m_pageBuilt[index] = true;

    // 记录本次新增绑定的起点：构建后只回填/连接这一段，避免覆盖其它页的
    // 在编草稿（热键 / 语言是待保存项，重放会把手上的改动抹掉）。
    const int chkFrom = m_chkBinds.size();
    const int comboFrom = m_comboBinds.size();
    const int keyFrom = m_keyRows.size();
    const int mouseFrom = m_mouseRows.size();
    const int liveFrom = m_liveChecks.size();

    QElapsedTimer pageTimer;
    pageTimer.start();
    if (m_pageBuilders.at(index))
        m_pageBuilders.at(index)();

    QWidget *page = m_pages.at(index);
    loadBindingsFromConfig(chkFrom, comboFrom, keyFrom, mouseFrom);
    loadPageWidgets(page);
    levelComboWidths(page);
    finishPageLayout(index);
    rebuildPageSearchIndex(index);
    connectLiveChecks(liveFrom);
    connectInstantSignals(chkFrom, comboFrom, keyFrom, mouseFrom);
    connectPageWidgets(page);
    capturePendingBaseline(keyFrom);
    if (m_compact)
        applyCompactMetricsToPage(page);   // 窄窗后建的页也要落到紧凑档
    updatePendingNote();
    updateContentColumnWidth();
    probeMark((QByteArrayLiteral("page") + QByteArray::number(index)).constData(),
              pageTimer.nsecsElapsed() / 1000);
}

void SettingsDialog::ensureAllPagesBuilt()
{
    if (m_allPagesBuilt)
        return;
    m_allPagesBuilt = true;
    for (int i = 0; i < m_pages.size(); ++i)
        ensurePageBuilt(i);
    buildSearchIndex();
}

void SettingsDialog::selectPage(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    ensurePageBuilt(index);   // 延后构建：首次进入该页时才建内容
    m_stack->setCurrentIndex(index);
    m_pageTitle->setText(m_pageTitles.at(index));
    const QString desc = m_pageDescs.at(index);
    m_pageDesc->setText(desc);
    m_pageDesc->setVisible(!desc.isEmpty());
    for (int i = 0; i < m_tabs.size(); ++i)
        m_tabs.at(i)->setChecked(i == index);

    // v2 母版：所有页统一无页头分隔线、标题与卡片左缘对齐、内容列 640 居中。
    if (m_headLine)
        m_headLine->setVisible(false);
    updateGeneralHeaderSpacing();
    m_pageTitle->setContentsMargins(0, 0, 0, 0);
    m_pageDesc->setContentsMargins(0, 0, 0, 0);
    updateContentColumnWidth();

    // 页专属动作位（「恢复默认菜单」）只在查看器右键菜单页出现
    if (m_pageActionHost) {
        m_pageActionHost->setVisible(m_appMenuPage && m_pages.at(index) == m_appMenuPage);
    }
}

void SettingsDialog::updateGeneralHeaderSpacing()
{
    // v2：所有页都无页头分隔线；标准档 8+8，紧凑档 6+6。
    const bool outline = m_appMenuPageIndex >= 0 && m_stack->currentIndex() == m_appMenuPageIndex;
    const int gap = outline && !m_compact ? 9 : m_compact ? kGenCompactHeadGap : 8;
    if (m_headGapTop)
        m_headGapTop->changeSize(0, gap);
    if (m_headGapBottom)
        m_headGapBottom->changeSize(0, gap);
    if (m_contentColumn && m_contentColumn->layout())
        m_contentColumn->layout()->invalidate();
}

void SettingsDialog::updateContentColumnWidth()
{
    if (!m_contentLay)
        return;
    // 内容列：宽度封顶到 kGenContentMaxW，靠 QBoxLayout 的居中把多出来的
    // 空间均分到两侧（align=AlignHCenter 已在外壳上设好）。窗口继续拉宽时
    // 卡片不变宽，只有两侧留白变大；窗口变窄时列跟着收，不再叠第二层内边距。
    const bool outline = m_appMenuPageIndex >= 0 && m_stack->currentIndex() == m_appMenuPageIndex;
    const int extraWidth = qBound(0, width() - kDlgMinW, kDlgW - kDlgMinW);
    const int navW = outline
        ? 188 + (kNavW - 188) * extraWidth / (kDlgW - kDlgMinW)
        : m_compact ? kGenCompactNavW : kNavW;
    m_navRail->setFixedWidth(navW);
    m_titleBar->setFixedHeight(outline || m_compact ? kGenCompactTitleH : kTitleBarH);
    m_footer->setFixedHeight(outline || m_compact ? kGenCompactFooterH : kFooterH);
    const bool shortNav = m_compact || height() < 720;
    const int navRowH = shortNav ? kGenCompactNavRowH : kGenNavRowH;
    m_navLayout->setSpacing(shortNav ? kGenCompactNavSpacing : kGenNavSpacing);
    for (QAbstractButton *button : m_tabs) {
        button->setFixedHeight(navRowH);
    }
    m_btnAbout->setFixedHeight(navRowH);
    const int padX = m_compact ? kGenCompactPadX : kGenColumnPadX;
    int colW = qMin(kGenContentMaxW, qMax(0, width() - navW - 1 - 2 * padX));
    if (outline) {
        // 在最小窗保留可用列宽，过渡到常规窗的 33px 两侧留白。
        // 三栏（大纲 + 间隙 24 + 常驻一级预览）放宽到 1160，与图 01 的
        // 「大纲约 880 / 预览 256」对齐；窄窗先压大纲，预览最后才收窄。
        const int sideMargins = 42 + 24 * extraWidth / (kDlgW - kDlgMinW);
        colW = qMin(1160, width() - navW - 2 - sideMargins);
        constexpr int kPreviewTableGap = 24;
        constexpr int kTableMinW = 420;
        int previewW = 256;
        if (colW - kPreviewTableGap - previewW < kTableMinW) {
            previewW = qMax(200, colW - kPreviewTableGap - kTableMinW);
        }
        if (auto *scroll = m_appMenuPage
                               ? m_appMenuPage->findChild<QScrollArea *>(
                                     QStringLiteral("appMenuPreviewScroll"))
                               : nullptr) {
            scroll->setFixedWidth(previewW);
        }
        if (m_appMenuPreview) {
            m_appMenuPreview->setMinimumWidth(160);
        }
        if (auto *cap = m_appMenuPage ? m_appMenuPage->findChild<QWidget *>(
                               QStringLiteral("appMenuPreviewCaption"))
                                      : nullptr) {
            cap->setVisible(height() >= 720);
        }
        m_layoutAppMenuOutline(qMax(240, colW - kPreviewTableGap - previewW));
    }
    m_contentColumn->setFixedWidth(colW);
    m_contentLay->setContentsMargins(0, m_compact ? kGenCompactPaneTop : kGenPaneTop,
                                    0, outline ? 16 : 12);

    // 底栏提示对齐到内容列左缘：footer 左边距 + 恢复默认按钮宽 + 布局间距
    // 是固定量（实测合计 20），其余交给弹性间隔，提示文字起点 = 卡片左缘。
    if (m_footerGap && m_btnReset && m_footer && m_footer->layout()) {
        const int columnLeft = navW + 1 + qMax(0, width() - navW - 1 - colW) / 2;
        const int resetW = m_btnReset->width() > 0 ? m_btnReset->width()
                                                   : m_btnReset->sizeHint().width();
        const int gap = qMax(0, columnLeft - 20 - resetW);
        m_footerGap->changeSize(gap, 0, QSizePolicy::Fixed, QSizePolicy::Minimum);
        m_footer->layout()->invalidate();
    }
}

void SettingsDialog::applyCompactLayout(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;

    // 所有已构建页：组间距与行高按档位收放（字号与控件尺寸不变）。
    // 未构建的页在 ensurePageBuilt() 里按 m_compact 补一次。
    for (int i = 0; i < m_pages.size(); ++i) {
        if (m_pageBuilt.value(i))
            applyCompactMetricsToPage(m_pages.at(i));
    }
    updateGeneralHeaderSpacing();
    updateContentColumnWidth();
}

void SettingsDialog::applyCompactMetricsToPage(QWidget *page)
{
    if (!page)
        return;
    const bool compact = m_compact;
    {
        int index = 0;
        if (auto *lay = page->layout()) {
            for (int i = 0; i < lay->count(); ++i) {
                QWidget *sec = lay->itemAt(i) ? lay->itemAt(i)->widget() : nullptr;
                if (!sec || sec->objectName() != QLatin1String("pageSection"))
                    continue;
                if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout())) {
                    const int gap = index == 0
                        ? (compact ? kGenCompactFirstGap : kGenFirstGap)
                        : (compact ? kGenCompactGroupGap : kGenGroupGap);
                    v->setContentsMargins(0, gap, 0, 0);
                    for (int k = 0; k < v->count(); ++k) {
                        if (auto *sp = v->itemAt(k)->spacerItem())
                            sp->changeSize(0, compact ? 4 : 6);
                    }
                }
                ++index;
            }
        }
        for (QWidget *row : page->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
            row->setMinimumHeight(compact ? kGenCompactRowH : kGenRowH);
            if (auto *h = qobject_cast<QHBoxLayout *>(row->layout())) {
                const int padY = compact ? 2 : kRowPadY;
                h->setContentsMargins(kGenRowPadX, padY, kGenRowPadX, padY);
                if (auto *left = qobject_cast<QVBoxLayout *>(h->itemAt(0)->layout()))
                    left->setSpacing(compact ? 1 : 2);
            }
        }
    }
}

QSize SettingsDialog::minimumSizeHint() const
{
    return QSize(kDlgMinW, kDlgMinH);
}

void SettingsDialog::resizeEvent(QResizeEvent *event)
{
    const auto restore = m_saveAppMenuScroll && m_appMenuPage->isVisible()
                             ? m_saveAppMenuScroll() : std::function<void()>();
    QDialog::resizeEvent(event);
    // 窗口大小变化时即时重排：先切标准/紧凑档，再算内容列宽度。
    applyCompactLayout(width() < kGenCompactThreshold);
    updateContentColumnWidth();
    if (restore) {
        restore();
        QTimer::singleShot(0, this, restore);
    }
    if (m_toast)
        m_toast->reposition();
}

// ===========================================================================
// 控件工厂
// ===========================================================================

// ---------------------------------------------------------------------------
// 分组头：分组图标 + 名称 + 状态摘要。
// 折叠交互已按用户实测反馈停用（点标题会误收起），卡片始终展开，这里只保留
// 标题行本身——不响应鼠标、不给焦点、不画 chevron。将来若要恢复折叠，在
// mouseReleaseEvent 里接回切换即可。标题沿用 sectionTitle 的样式与左边线，
// 图标落在同一 11px 起始位。
// ---------------------------------------------------------------------------
class SectionHeader : public QWidget
{
public:
    SectionHeader(const QString &title, const QString &iconName, QWidget *parent)
        : QWidget(parent)
    {
        setMinimumHeight(26);

        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(kRowPadX, 0, kRowPadX, 0);
        h->setSpacing(8);

        if (!iconName.isEmpty()) {
            auto *icon = new QLabel(this);
            icon->setFixedSize(16, 16);
            icon->setPixmap(MenuIcons::pixmap(iconName, UiStyle::fluent::textColor(), 16));
            h->addWidget(icon, 0, Qt::AlignVCenter);
        }

        auto *t = new QLabel(title, this);
        t->setObjectName(QStringLiteral("sectionTitle"));
        // 左边线由 header 的 11px 内边距承担，applyPageRhythm 不再二次缩进。
        t->setProperty("collapseHead", true);
        h->addWidget(t, 0, Qt::AlignVCenter);
        h->addStretch(1);

        m_summary = new QLabel(this);
        m_summary->setVisible(false);
        h->addWidget(m_summary, 0, Qt::AlignVCenter);
    }

    // 状态摘要：已开启 / 总数；全开绿、全关灰、部分橙（与开关态同源）。
    void setSummary(int on, int total)
    {
        if (total <= 0) {
            m_summary->setVisible(false);
            return;
        }
        m_summary->setVisible(true);
        m_summary->setText(QStringLiteral("%1/%2").arg(on).arg(total));
        const QColor c = (on == total) ? UiStyle::fluent::statusOkColor()
                        : (on == 0)    ? UiStyle::fluent::descColor()
                                       : UiStyle::fluent::statusWarnColor();
        m_summary->setStyleSheet(QStringLiteral("font-size:11px;color:%1;")
                                     .arg(c.name(QColor::HexRgb)));
    }

private:
    QLabel *m_summary = nullptr;
};

// ---------------------------------------------------------------------------
// 父项行末端的展开箭头：指向右 = 子项已收起，指向下 = 已展开。
// 纯绘制、不接收鼠标（点击交给整行判断），颜色每次重绘现取，深浅色切换不残留。
// ---------------------------------------------------------------------------
class RowChevron : public QWidget
{
public:
    explicit RowChevron(QWidget *parent) : QWidget(parent)
    {
        setFixedSize(16, 16);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
    void setExpanded(bool on)
    {
        if (m_expanded == on)
            return;
        m_expanded = on;
        setProperty("expanded", on);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(UiStyle::fluent::descColor(), 1.4, Qt::SolidLine,
                      Qt::RoundCap, Qt::RoundJoin));
        p.translate(width() / 2.0, height() / 2.0);
        if (m_expanded)
            p.rotate(90);
        p.drawLine(QPointF(-1.8, -3.0), QPointF(1.6, 0.0));
        p.drawLine(QPointF(1.6, 0.0), QPointF(-1.8, 3.0));
    }

private:
    bool m_expanded = true;
};

// ---------------------------------------------------------------------------
// 行点击转发：父项行本身、标题、说明、图标都不吃鼠标事件，这里统一收左键
// 单击（按下与抬起位移 ≤4px 才算点击），交给回调切换子项显隐。
// 开关是子控件且自己接收事件，点开关不会走到这里。
// ---------------------------------------------------------------------------
class RowClickRelay : public QObject
{
public:
    RowClickRelay(std::function<void()> onClick, QObject *parent)
        : QObject(parent), m_onClick(std::move(onClick))
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (me->button() == Qt::LeftButton) {
                m_pressPos = me->position().toPoint();
                m_pressed = true;
            }
            return false;   // 不拦截，行照常收到 hover/重绘
        }
        if (e->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(e);
            if (m_pressed && me->button() == Qt::LeftButton) {
                m_pressed = false;
                if ((me->position().toPoint() - m_pressPos).manhattanLength() <= 4) {
                    if (m_onClick)
                        m_onClick();
                    return true;
                }
            }
        }
        return false;
    }

private:
    std::function<void()> m_onClick;
    QPoint m_pressPos;
    bool m_pressed = false;
};

QWidget *SettingsDialog::beginCollapsibleSection(QWidget *page, const QString &title,
                                                 const QString &iconName, int topGap)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return page;

    auto *sec = new QWidget(page);
    sec->setObjectName(QStringLiteral("pageSection"));
    auto *v = new QVBoxLayout(sec);
    v->setContentsMargins(0, topGap >= 0 ? topGap : kGroupTop, 0, 0);
    v->setSpacing(0);

    auto *card = new QFrame(sec);
    card->setObjectName(QStringLiteral("setGroup"));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);

    auto *head = new SectionHeader(title, iconName, sec);
    head->setObjectName(QStringLiteral("sectionHeader"));
    v->addWidget(head);
    v->addSpacing(6);
    v->addWidget(card);
    // 卡片常显：折叠交互已停用，标题行只承载图标与摘要，点它不会收起内容。

    pageLay->addWidget(sec);
    return sec;
}

QWidget *SettingsDialog::beginSection(QWidget *page, const QString &title, int topGap)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return page;

    auto *sec = new QWidget(page);
    // 标记分组壳：applyPageRhythm() 按页面顺序统一首组 / 组间距，不再逐页传参。
    sec->setObjectName(QStringLiteral("pageSection"));
    auto *v = new QVBoxLayout(sec);
    v->setContentsMargins(0, topGap >= 0 ? topGap : kGroupTop, 0, 0);
    v->setSpacing(0);

    if (!title.isEmpty()) {
        auto *t = new QLabel(title, sec);
        t->setObjectName(QStringLiteral("sectionTitle"));
        // 与行文案共用左边线（卡片内行左内边距 11px）。
        t->setContentsMargins(kRowPadX, 0, 0, 0);
        v->addWidget(t);
        v->addSpacing(6);
    }

    auto *card = new QFrame(sec);
    card->setObjectName(QStringLiteral("setGroup"));
    auto *cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(0, 0, 0, 0);
    cardLay->setSpacing(0);
    v->addWidget(card);

    pageLay->addWidget(sec);
    return sec;
}

QWidget *SettingsDialog::groupCard(QWidget *section) const
{
    return section ? section->findChild<QFrame *>(QStringLiteral("setGroup")) : nullptr;
}

void SettingsDialog::applyPageRhythm(QWidget *page, int firstGap, int groupGap,
                                    int rowH, int ctlW)
{
    if (!page)
        return;

    // v2 母版：所有设置页共用同一套卡片 / 行 / 字号 / 控件列。每个页面在
    // addPage 时已打上 fluent=v2，这里再把本页生成的控件逐类打标，供
    // extraQss 的属性选择器命中。参数留空即用 v2 默认 16/12/47/154。
    const int fg = firstGap >= 0 ? firstGap : kPageFirstGap;
    const int gg = groupGap >= 0 ? groupGap : kPageGroupGap;
    const int rh = rowH >= 0 ? rowH : kPageRowH;
    const int rowWithDesc = rowH >= 0 ? rowH : kPageRowH;
    const int rowSingle = qMax(28, rowWithDesc - 6);
    const int cw = ctlW >= 0 ? ctlW : kPageCtlW;
    const bool isFirstPage = !m_pages.isEmpty() && m_pages.at(0) == page;

    const char *marks[] = { "setGroup", "rowLine", "settingRow", "rowLabel",
                            "rowDesc", "sectionTitle", "setNote" };
    for (const char *name : marks) {
        for (QWidget *w : page->findChildren<QWidget *>(QLatin1String(name)))
            w->setProperty("fluent", QStringLiteral("v2"));
    }
    // 2026-11-17 去描边先只落「一般」页（首块验收页），其他页等内容样式确认后再推广：
    // 本页卡片单独打 genEdge=none，QSS 用属性选择器压掉外圈描边，其余页保持原样。
    if (isFirstPage) {
        for (QWidget *w : page->findChildren<QWidget *>(QStringLiteral("setGroup")))
            w->setProperty("genEdge", QStringLiteral("none"));
    }
    for (QPushButton *b : page->findChildren<QPushButton *>()) {
        if (b->objectName() == QLatin1String("miniBtn"))
            continue;   // 迷你按钮保持 22 高，不套 30 高控件规格
        b->setProperty("fluent", QStringLiteral("v2"));
    }
    for (QComboBox *c : page->findChildren<QComboBox *>())
        c->setProperty("fluent", QStringLiteral("v2"));
    for (QCheckBox *cb : page->findChildren<QCheckBox *>())
        cb->setProperty("fluent", QStringLiteral("v2"));

    // 分组顶部间距：首组 fg，其余 gg。
    int index = 0;
    if (auto *lay = page->layout()) {
        for (int i = 0; i < lay->count(); ++i) {
            QLayoutItem *item = lay->itemAt(i);
            QWidget *sec = item ? item->widget() : nullptr;
            if (!sec || sec->objectName() != QLatin1String("pageSection"))
                continue;
            if (auto *v = qobject_cast<QVBoxLayout *>(sec->layout()))
                v->setContentsMargins(0, index == 0 ? fg : gg, 0, 0);
            ++index;
        }
    }

    // 行高、卡片内边距 14、文案与控件相隔 16；控件列整页同宽并右对齐。
    // 一般页（首块验收页）控件列固定 154；其他页先量后设，避免杂项路径输入
    // 或上下文菜单长状态被 154 挤扁。
    QList<QWidget *> rows;
    int ctlWidth = cw;
    for (QWidget *row : page->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
        rows.append(row);
        if (isFirstPage)
            continue;
        row->ensurePolished();
        if (auto *ctl = row->findChild<QWidget *>(QStringLiteral("rowControls"))) {
            ctl->ensurePolished();
            ctlWidth = qMax(ctlWidth, ctl->sizeHint().width() + 8);
        }
    }
    for (QWidget *row : rows) {
        // 三类行型（规格 §布局与文字）：单行设置 38 / 标题+说明 44 /
        // 表格与映射行走自己的密度（不在这一支）。minimumHeight 只是下限，
        // 说明换行或多行内容仍由文字自然撑高，不做封顶、不删说明。
        const bool hasDesc = row->findChild<QLabel *>(QStringLiteral("rowDesc")) != nullptr;
        row->setMinimumHeight(hasDesc ? rowWithDesc : rowSingle);
        if (auto *h = qobject_cast<QHBoxLayout *>(row->layout())) {
            h->setContentsMargins(kGenRowPadX, kRowPadY, kGenRowPadX, kRowPadY);
            h->setSpacing(kGenTextGap);
        }
        if (auto *ctl = row->findChild<QWidget *>(QStringLiteral("rowControls"))) {
            ctl->setFixedWidth(ctlWidth);
            if (auto *hl = qobject_cast<QHBoxLayout *>(ctl->layout()))
                hl->insertStretch(0, 1);
        }
    }

    for (QLabel *t : page->findChildren<QLabel *>(QStringLiteral("sectionTitle"))) {
        t->setProperty("variant", QStringLiteral("page"));
        // 折叠头的标题已经落在 header 的 11px 内边距上（前面还有分组图标），
        // 再套一次左边距会和图标错位。
        if (!t->property("collapseHead").toBool())
            t->setContentsMargins(kGenRowPadX, 0, 0, 0);
    }
    for (QLabel *n : page->findChildren<QLabel *>(QStringLiteral("setNote")))
        n->setContentsMargins(kGenRowPadX, 8, kGenRowPadX, 9);
}

SettingsDialog::SettingRowParts SettingsDialog::addSettingRow(QWidget *section,
                                                              const QString &label,
                                                              const QString &desc)
{
    SettingRowParts out;
    auto *card = groupCard(section);
    if (!card)
        return out;

    // 行间 1px 分隔：组卡片里已有内容才插，避免出现首行顶线。
    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout())) {
        if (cardLay->count() > 0) {
            auto *line = new QFrame(card);
            line->setObjectName(QStringLiteral("rowLine"));
            line->setFixedHeight(1);
            cardLay->addWidget(line);
        }
    }

    auto *row = new QWidget(card);
    row->setObjectName(QStringLiteral("settingRow"));
    // 整行悬停：WA_Hover 让行（含子控件的祖先链）收到 HoverEnter/Leave，
    // QSS `#settingRow:hover` 只提亮行底，不改变任何控件尺寸。
    row->setAttribute(Qt::WA_Hover, true);
    row->setMinimumHeight(kRowMinH);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(kGenRowPadX, kRowPadY, kGenRowPadX, kRowPadY);
    h->setSpacing(kGenTextGap);

    auto *left = new QVBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    // 规格：第一行 18px，第二行 15px，间隔 1px。
    left->setSpacing(1);
    out.label = new QLabel(label, row);
    out.label->setObjectName(QStringLiteral("rowLabel"));
    out.label->setWordWrap(true);
    left->addStretch(1);
    left->addWidget(out.label);
    if (!desc.isEmpty()) {
        out.desc = new QLabel(desc, row);
        out.desc->setObjectName(QStringLiteral("rowDesc"));
        out.desc->setWordWrap(true);
        left->addWidget(out.desc);
    }
    left->addStretch(1);
    h->addLayout(left, 1);

    auto *ctlHost = new QWidget(row);
    ctlHost->setObjectName(QStringLiteral("rowControls"));
    auto *ctl = new QHBoxLayout(ctlHost);
    ctl->setContentsMargins(0, 0, 0, 0);
    ctl->setSpacing(8);
    out.controls = ctl;
    h->addWidget(ctlHost, 0, Qt::AlignVCenter);

    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout()))
        cardLay->addWidget(row);
    out.row = row;
    return out;
}

QHBoxLayout *SettingsDialog::addRow(QWidget *section, const QString &label,
                                    const QString &desc)
{
    return addSettingRow(section, label, desc).controls;
}

QCheckBox *SettingsDialog::addCheck(QWidget *section, const QString &text, const char *key,
                                    bool inverted, bool def)
{
    return addSwitchRow(section, text, QString(), key, inverted, def);
}

PillToggle *SettingsDialog::addSwitchRow(QWidget *section, const QString &label,
                                         const QString &desc, const char *key,
                                         bool inverted, bool def)
{
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return nullptr;

    auto *cb = new PillToggle(parts.row);
    cb->setAccessibleName(label);
    if (!desc.isEmpty())
        cb->setToolTip(desc);
    parts.controls->addWidget(cb, 0, Qt::AlignVCenter);
    if (key) {
        cb->setProperty("cfgKey", QString::fromLatin1(key));   // 即时化取证探针按配置键定位开关
        m_chkBinds.append({ cb, key, inverted, def });
    }
    return cb;
}

QLabel *SettingsDialog::makeToggleLabel(const QString &text, const QString &objectName)
{
    auto *lab = new QLabel(text);
    lab->setObjectName(objectName);
    lab->setWordWrap(true);
    return lab;
}

namespace {

// 主题来源值 → 运行态实际配色（"system" 按 Windows 当前深浅解析）。
QString themeMode(const QString &setting)
{
    return Theme::resolveMode(setting) == Theme::Mode::Light ? QStringLiteral("light")
                                                            : QStringLiteral("dark");
}

// 整行命中：标题 / 说明 / 行内空白都走同一次开关激活。轨道本身由 PillToggle
// 自己处理，不装过滤器，避免一次点击激活两次。按下后移出再松开按取消处理；
// 禁用行只吞事件、不激活。
class ToggleRowHitFilter final : public QObject
{
public:
    ToggleRowHitFilter(PillToggle *toggle, QWidget *row, QObject *parent)
        : QObject(parent)
        , m_toggle(toggle)
        , m_row(row)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton)
                return false;
            m_pressed = m_toggle->isEnabled();
            return true;   // 吃掉按下：标签不抢焦点，松手仍按整行判定
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() != Qt::LeftButton)
                return false;
            const bool pressed = m_pressed;
            m_pressed = false;
            if (!pressed || !m_toggle->isEnabled())
                return true;   // 禁用行点任何位置都无效
            const QPoint globalPos = me->globalPosition().toPoint();
            if (!QRect(m_row->mapToGlobal(QPoint(0, 0)), m_row->size()).contains(globalPos))
                return true;   // 按下后移出再松开：取消激活
            m_toggle->click();
            return true;
        }
        return false;
    }

private:
    PillToggle *m_toggle = nullptr;
    QWidget *m_row = nullptr;
    bool m_pressed = false;
};

} // namespace

SettingsDialog::ToggleRow SettingsDialog::addToggleRow(QWidget *section, const QString &label,
                                                       const QString &desc,
                                                       const QString &accessibleName)
{
    ToggleRow out;
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return out;

    out.label = parts.label;
    out.desc = parts.desc;
    out.row = parts.row;
    out.toggle = new PillToggle(parts.row);
    out.toggle->setAccessibleName(accessibleName);
    out.toggle->setToolTip(desc);
    parts.controls->addWidget(out.toggle, 0, Qt::AlignVCenter);

    // 启用行的标题、说明和行内空白都是开关命中区；焦点仍只在轨道上。
    out.label->setCursor(Qt::PointingHandCursor);
    if (out.desc)
        out.desc->setCursor(Qt::PointingHandCursor);
    parts.row->setCursor(Qt::PointingHandCursor);
    auto *hit = new ToggleRowHitFilter(out.toggle, parts.row, parts.row);
    out.label->installEventFilter(hit);
    if (out.desc)
        out.desc->installEventFilter(hit);
    parts.row->installEventFilter(hit);
    return out;
}

PillToggle *SettingsDialog::addPillCheck(QWidget *section, const QString &label,
                                         const char *key, bool inverted, bool def,
                                         const QString &timing)
{
    ToggleRow row = addToggleRow(section, label, timing, label);
    // 与 addCheck 同一张绑定表：loadFromConfig 回填、onAccepted 写盘都复用，
    // 保证胶囊只是换了外壳，配置键与保存路径不变。
    if (row.toggle)
        m_chkBinds.append({ row.toggle, key, inverted, def });
    return row.toggle;
}

QComboBox *SettingsDialog::makePopupCombo(QWidget *parent)
{
    auto *combo = new PopupComboBox(parent);
    if (parent && parent->objectName() == QLatin1String("settingRow"))
        parent->setProperty("comboRow", true);
    combo->setFixedHeight(kComboH);
    return combo;
}

QComboBox *SettingsDialog::addComboRow(QWidget *section, const QString &label,
                                       const QString &desc, const char *key,
                                       const QString &def,
                                       const QList<QPair<QString, QString>> &options,
                                       int minWidth)
{
    SettingRowParts parts = addSettingRow(section, label, desc);
    if (!parts.row)
        return nullptr;

    auto *combo = makePopupCombo(parts.row);
    for (const auto &opt : options)
        combo->addItem(opt.second, opt.first);
    combo->setCursor(Qt::PointingHandCursor);
    combo->setFixedHeight(kComboH);
    if (!desc.isEmpty())
        combo->setToolTip(desc);

    // 宽度按最长选项计算（含箭头与左右内边距），同页再由 levelComboWidths
    // 统一到最大需求；上下限避免超长选项把控件列撑爆。
    int req = minWidth;
    if (req <= 0) {
        const QFontMetrics fm(combo->font());
        for (const auto &opt : options)
            req = qMax(req, fm.horizontalAdvance(opt.second));
        req += 44;
        req = qBound(160, req, 260);
    }
    combo->setProperty("reqW", req);
    combo->setFixedWidth(req);
    combo->setMinimumWidth(req);
    combo->setMaximumWidth(req);
    parts.controls->addWidget(combo, 0, Qt::AlignVCenter);

    if (key)
        m_comboBinds.append({ combo, key, def });
    // 把禁用项的 tooltip 复制到 UserRole+1：pop-up 委托在次行显示禁用原因，
    // 不用悬停才看得到（规格 §Pop-up button）。
    for (int i = 0; i < combo->count(); ++i) {
        const QString tip = combo->itemData(i, Qt::ToolTipRole).toString();
        if (!tip.isEmpty())
            combo->setItemData(i, tip, Qt::UserRole + 1);
    }
    return combo;
}

void SettingsDialog::levelComboWidths(QWidget *page)
{
    if (!page)
        return;
    int widest = 0;
    for (QComboBox *combo : page->findChildren<QComboBox *>()) {
        // 键盘页没有下拉；语言页单行自然保持自身需求。
        widest = qMax(widest, combo->property("reqW").toInt());
    }
    if (widest <= 0)
        return;
    for (QComboBox *combo : page->findChildren<QComboBox *>()) {
        if (combo->property("reqW").toInt() <= 0)
            continue;   // 专项控件（书签上限等）保留既有宽度
        combo->setFixedWidth(widest);
        combo->setMinimumWidth(widest);
        combo->setMaximumWidth(widest);
    }
}

QLabel *SettingsDialog::addHint(QWidget *section, const QString &text)
{
    auto *card = groupCard(section);
    if (!card)
        return nullptr;
    auto *lab = new QLabel(text, card);
    lab->setObjectName(QStringLiteral("setNote"));
    lab->setWordWrap(true);
    lab->setContentsMargins(kRowPadX, 8, kRowPadX, 9);
    if (auto *cardLay = qobject_cast<QVBoxLayout *>(card->layout()))
        cardLay->addWidget(lab);
    return lab;
}

QPushButton *SettingsDialog::addButton(QWidget *parent, const QString &text)
{
    auto *btn = new QPushButton(text, parent);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(kComboH);
    return btn;
}

QPushButton *SettingsDialog::addMiniButton(QWidget *parent, const QString &text)
{
    auto *btn = new QPushButton(text, parent);
    btn->setObjectName(QStringLiteral("miniBtn"));
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedHeight(22);
    return btn;
}

// ===========================================================================
// 诚实化：零消费项灰显
// ===========================================================================

namespace {

// 单一文案：所有"尚未实现"项共用一句。文案散落在十几处，将来改动/翻译必走样。
QString unavailableTip()
{
    return QObject::tr("该选项暂未实现，将在后续版本提供。");
}

// 设置导出/导入的新文案：显式写 context，和 SettingsDialog 的裸 tr() 同一个
// context（"SettingsDialog"），但按派单硬约束一律走 QCoreApplication::translate。
QString setioTr(const char *text)
{
    return QCoreApplication::translate("SettingsDialog", text);
}

} // namespace

void SettingsDialog::markUnavailable(QWidget *w)
{
    if (!w)
        return;
    w->setEnabled(false);           // 不可交互：点击 / Tab / 键盘都不响应
    w->setCursor(Qt::ArrowCursor);  // 收掉手型，别"看起来能点"
    w->setToolTip(unavailableTip());
}

QCheckBox *SettingsDialog::addCheckUnavailable(QWidget *section, const QString &text,
                                               const char *key, bool inverted, bool def)
{
    QCheckBox *cb = addCheck(section, text, key, inverted, def);
    markUnavailable(cb);
    return cb;
}

QComboBox *SettingsDialog::addComboRowUnavailable(QWidget *section, const QString &label,
                                                  const QString &desc, const char *key,
                                                  const QString &def,
                                                  const QList<QPair<QString, QString>> &options,
                                                  int minWidth)
{
    QComboBox *combo = addComboRow(section, label, desc, key, def, options, minWidth);
    markUnavailable(combo->parentWidget()->parentWidget());   // 整行（行标签 + 下拉）一起灰
    return combo;
}

// ===========================================================================
// 1. 一般
// ===========================================================================

void SettingsDialog::buildGeneralPage(QWidget *page)
{
    // 九页重组（2026-11-21）：只改页面归属，不改配置键、默认值、正反开关语义与
    // 生效时机。原「一般」页保留窗口与应用级行为，吸收杂项页的「按图调窗 /
    // 禁用屏保」、语言整页与设置备份。
    QWidget *sec = beginSection(page, tr("窗口行为"));
    // 「图像编辑器」一行的控件列要单独放宽：输入框 + 浏览… + 预设 三个控件
    // 在 154/160 的统一控件列里放不下（英文下按钮文字会被截成 "owse"）。
    QWidget *editorControls = nullptr;
    addSwitchRow(sec, tr("不允许多个实例"), tr("重启 QuikView 后生效"),
                 "allow_multiple_instances", true, true);
    auto *alwaysOnTop = addSwitchRow(sec, tr("总在最前 (Ctrl+A)"), QString(),
                                     "always_on_top", false, false);
    m_liveChecks.append({ alwaysOnTop, "always_on_top", false, false });
    auto *fitWin = addSwitchRow(sec, tr("根据图片大小调节窗口大小"), QString(),
                                "etc/fit_window_to_image", false, false);
    m_liveChecks.append({ fitWin, "etc/fit_window_to_image", false, false });
    auto *noSaver = addSwitchRow(sec, tr("运行 QuikView 时禁用 Windows 屏幕保护程序"),
                                 tr("退出 QuikView 后恢复系统屏保设置"),
                                 "etc/disable_screensaver", false, false);
    m_liveChecks.append({ noSaver, "etc/disable_screensaver", false, false });

    sec = beginSection(page, tr("操作反馈"));
    auto *deleteConfirm = addSwitchRow(sec, tr("删除文件时确认"), QString(),
                                       "delete_confirm", false, true);
    auto *notifyCorner = addSwitchRow(sec, tr("在边角显示通知信息，而非中心"), QString(),
                                      "notify_corner", false, true);
    // 通知位置有可下发的运行态（Toast 的角位）：切换点立刻换位并弹一条示例，
    // 会话内可见；另外两项同样是消费即读，一起登记保证「切换即写盘」。
    m_liveChecks.append({ deleteConfirm, "delete_confirm", false, false });
    m_liveChecks.append({ notifyCorner, "notify_corner", false, false });

    // 图像编辑器：原杂项页「常用路径」组里唯一仍留在这里的路径项——它是
    // 「编辑」动作的目标程序，属应用级设置；两个图片文件夹归「浏览」。
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("图像编辑器程序路径"), tr("留空时使用系统默认程序"));
        m_editorPath = new QLineEdit(parts.row);
        m_editorPath->setFixedHeight(kInputH);
        m_editorPath->setFixedWidth(210);
        m_editorPath->setAccessibleName(tr("图像编辑器程序路径"));
        parts.controls->addWidget(m_editorPath, 0, Qt::AlignVCenter);
        editorControls =
            parts.row->findChild<QWidget *>(QStringLiteral("rowControls"));
        auto *btnBrowse = addButton(nullptr, tr("浏览…"));
        auto *btnPreset = addButton(nullptr, tr("预设"));
        parts.controls->addWidget(btnBrowse, 0, Qt::AlignVCenter);
        parts.controls->addWidget(btnPreset, 0, Qt::AlignVCenter);
        connect(btnBrowse, &QPushButton::clicked, this, [this]() {
            const QString f = QFileDialog::getOpenFileName(
                this, tr("选择图像编辑器"), m_editorPath->text(),
                tr("程序 (*.exe);;所有文件 (*)"));
            if (!f.isEmpty()) {
                m_editorPath->setText(f);
                applyInstantValue(QStringLiteral("etc/image_editor"), f);
            }
        });
        connect(btnPreset, &QPushButton::clicked, this, [this]() {
            const QString preset = QStringLiteral("C:/Windows/System32/mspaint.exe");
            m_editorPath->setText(preset);
            applyInstantValue(QStringLiteral("etc/image_editor"), preset);
        });
    }

    // —— 检查更新 ——
    // 手动检查不受频控；自动检查默认开（2026-11-12 用户定稿），启动 5s 静默查一次、24h 限频。
    sec = beginSection(page, tr("检查更新"));
    QHBoxLayout *updateRow = addRow(sec, tr("手动检查"), QString());
    m_checkUpdateBtn = addButton(updateRow->parentWidget(), tr("检查更新"));
    updateRow->addWidget(m_checkUpdateBtn, 0, Qt::AlignVCenter);
    addSwitchRow(sec, tr("启动时检查更新"), tr("重启后生效；每 24 小时最多检查一次"),
                 "check_updates", false, true);
    sec->setToolTip(tr("手动检查不受频率限制；检查更新会联网访问发布源，"
                       "失败时提示「检查失败，请检查网络」。"));
    connect(m_checkUpdateBtn, &QPushButton::clicked, this, [this]() {
        m_checkUpdateBtn->setEnabled(false);
        m_checkUpdateBtn->setText(tr("检查中…"));
        if (!UpdateChecker::instance().check(UpdateChecker::Mode::Manual)) {
            // 已有请求在飞（比如刚触发的自动检查）→ 立即恢复，不排队、不重试。
            m_checkUpdateBtn->setEnabled(true);
            m_checkUpdateBtn->setText(tr("检查更新"));
        }
    });
    connect(&UpdateChecker::instance(), &UpdateChecker::finished, this,
            [this](bool manual) {
                if (manual)
                    m_checkUpdateBtn->setEnabled(true);
                m_checkUpdateBtn->setText(tr("检查更新"));
            });

    // —— 界面语言（原「语言」整页并入）——
    sec = beginSection(page, tr("界面语言"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("界面语言"),
            tr("重启 QuikView 后生效"));
        m_language = makePopupCombo(parts.row);
        populateLanguageCombo(m_language);
        m_language->setCursor(Qt::PointingHandCursor);
        m_language->setFixedHeight(kComboH);
        m_language->setFixedWidth(kGenCtlW);
        m_language->setAccessibleName(tr("界面语言"));
        parts.controls->addWidget(m_language, 0, Qt::AlignVCenter);
    }
    {
        SettingRowParts parts = addSettingRow(sec, tr("打开语言文件夹"), QString());
        descToRowTip(parts.row, tr("从 translations 目录加载 .qm 语言文件"));
        auto *btn = addButton(nullptr, tr("打开文件夹"));
        parts.controls->addWidget(btn, 0, Qt::AlignVCenter);
        connect(btn, &QPushButton::clicked, this, [this]() {
            const QString dir = QCoreApplication::applicationDirPath()
                                + QStringLiteral("/translations");
            QDir().mkpath(dir);
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
                QMessageBox::warning(this, tr("打开语言文件夹"),
                                     tr("无法调用系统文件管理器打开：\n%1").arg(dir));
            }
        });
    }
    {
        SettingRowParts parts = addSettingRow(sec, tr("下载更多语言"), QString());
        auto *btn = addButton(nullptr, tr("下载更多语言"));
        parts.controls->addWidget(btn, 0, Qt::AlignVCenter);
        connect(btn, &QPushButton::clicked, this, [this]() {
            // 外部网页：只报告系统是否接受了打开请求；网络可达性不做验证。
            if (!QDesktopServices::openUrl(
                    QUrl(QStringLiteral("https://github.com/xdsT2/quikview")))) {
                QMessageBox::warning(this, tr("下载更多语言"),
                                     tr("无法打开浏览器。请手动访问：\n"
                                        "https://github.com/xdsT2/quikview"));
            }
        });
    }
    addHint(sec, tr("两个入口都会离开 QuikView，应用内只报告启动是否成功。"));

    // —— 设置导出 / 导入（需求池 H1-3）——
    sec = beginSection(page, setioTr("设置备份"));
    {
        SettingRowParts parts = addSettingRow(
            sec, setioTr("导出设置"),
            setioTr("把当前设置写入 .ini，便于备份或换机"));
        m_btnExportSettings = addButton(nullptr, setioTr("导出设置"));
        parts.controls->addWidget(m_btnExportSettings, 0, Qt::AlignVCenter);
        connect(m_btnExportSettings, &QPushButton::clicked,
                this, &SettingsDialog::onExportSettings);
    }
    {
        SettingRowParts parts = addSettingRow(
            sec, setioTr("导入设置"),
            setioTr("从 .ini 文件恢复设置，重启 QuikView 后生效"));
        m_btnImportSettings = addButton(nullptr, setioTr("导入设置"));
        parts.controls->addWidget(m_btnImportSettings, 0, Qt::AlignVCenter);
        connect(m_btnImportSettings, &QPushButton::clicked,
                this, &SettingsDialog::onImportSettings);
    }

    // —— v2 母版：只改视觉，不动任何业务 ——
    m_checkUpdateBtn->setFixedHeight(kGenCtrlH);
    m_checkUpdateBtn->setProperty("fluent", QStringLiteral("v2"));
    for (QCheckBox *cb : page->findChildren<QCheckBox *>())
        cb->setProperty("fluent", QStringLiteral("v2"));
    const char *marks[] = { "setGroup", "rowLine", "settingRow", "rowLabel",
                            "rowDesc", "sectionTitle", "setNote" };
    for (const char *name : marks) {
        for (QWidget *w : page->findChildren<QWidget *>(QLatin1String(name)))
            w->setProperty("fluent", QStringLiteral("v2"));
    }
    applyPageRhythm(page, kGenFirstGap, kGenGroupGap, kGenRowH, kGenCtlW);
    // 统一节奏把每行控件列都固定到 160；编辑器路径这一行按自身 sizeHint 放宽，
    // 避免英文「Browse… / Preset」被截断（其余行不受影响）。
    if (editorControls)
        editorControls->setFixedWidth(qMax(editorControls->sizeHint().width(), kGenCtlW));
}

// ===========================================================================
// 2. 浏览
// ===========================================================================

void SettingsDialog::buildViewPage(QWidget *page)
{
    // 九页重组（2026-11-21）：翻页 / 缩放与记忆 / 图片文件夹 / 书签四组。
    // 原「浏览」页的视觉类开关（侧边箭头、浮层、帧号、缩放浮标、悬浮提示、
    // EXIF 字号）已移到「外观与显示」；这里只留翻页与定位行为。
    QWidget *sec = beginSection(page, tr("翻页"));
    const auto opts = lastFileOptions();
    auto *edge = addComboRow(sec, tr("到达首尾图片时执行"), QString(),
                             "edge_action", QStringLiteral("first"), opts, kGenComboW);
    descToRowTip(edge, tr("「询问」在翻到边界时弹确认；单图目录直接停住，不弹框"));
    sec->setToolTip(tr("「转到首个文件」在末尾继续时回绕到首个；「停止」停在当前图片；"
                       "「询问」弹确认框，确认后从另一端继续，取消留在当前图片。"));
    // 翻页效果（原「图像处理」页）：「无 / 淡入淡出 / Alpha 滑动 / 滑动」四档不变。
    addComboRow(sec, tr("翻页效果"), QString(),
                "imageproc/pageturn_fx", QStringLiteral("1"),
                { { "0", tr("无") }, { "1", tr("淡入淡出") },
                  { "2", tr("Alpha 滑动") }, { "3", tr("滑动") } });
    // 翻页音效（原「杂项」页）：键与默认值不变，消费点仍在每次翻页。
    // 2026-11-21：行名收成「翻页音效」，让搜索「翻页音效」直接命中本页。
    auto *pageturnWav = addSwitchRow(sec, tr("翻页音效"),
                                     tr("翻页时播放音效；关闭后静音"),
                                     "etc/pageturn_wav", false, false);
    descToRowTip(pageturnWav, tr("翻页音效仅在单张翻页与幻灯片切页时播放"));

    sec = beginSection(page, tr("缩放与记忆"));
    auto *saveZoom = addSwitchRow(sec, tr("记住缩放状态"),
                                  tr("下次打开这张图时恢复；当前画面不变"),
                                  "view/save_zoom_state", false, false);
    // 大图对齐（原「图像处理 · 渲染」）：显示位置属浏览行为，随本页走。
    addSwitchRow(sec, tr("图片大于窗口时显示图片顶部"),
                 tr("适配宽度／实际大小下、图片高于窗口时生效"),
                 "imageproc/align_top_when_large", false, true);
    auto *remember = addSwitchRow(sec, tr("不保存浏览过的文件夹路径"), QString(),
                                  "remember_browsed_folders", true, true);
    m_liveChecks.append({ saveZoom, "view/save_zoom_state", false, false });
    m_liveChecks.append({ remember, "remember_browsed_folders", true, false });

    // 图片文件夹（原「杂项 · 常用路径」）：两个快捷入口路径。
    sec = beginSection(page, tr("图片文件夹"));
    auto makeFolderRow = [this, sec](const QString &label, const QString &cfgKey,
                                     QLineEdit **edit) {
        SettingRowParts parts = addSettingRow(sec, label, tr("留空表示不使用该文件夹"));
        *edit = new QLineEdit(parts.row);
        (*edit)->setFixedHeight(kInputH);
        (*edit)->setFixedWidth(210);
        (*edit)->setAccessibleName(label);
        parts.controls->addWidget(*edit, 0, Qt::AlignVCenter);
        auto *btnBrowse = addButton(nullptr, tr("浏览…"));
        auto *btnOpen = addButton(nullptr, tr("打开"));
        parts.controls->addWidget(btnBrowse, 0, Qt::AlignVCenter);
        parts.controls->addWidget(btnOpen, 0, Qt::AlignVCenter);
        connect(btnBrowse, &QPushButton::clicked, this, [this, edit, cfgKey]() {
            const QString d = QFileDialog::getExistingDirectory(
                this, tr("选择一个文件夹."), (*edit)->text());
            if (!d.isEmpty()) {
                (*edit)->setText(d);
                applyInstantValue(cfgKey, d);
            }
        });
        connect(btnOpen, &QPushButton::clicked, this, [this, edit]() {
            const QString d = (*edit)->text().trimmed();
            if (d.isEmpty()) {
                QMessageBox::warning(this, tr("打开文件夹"), tr("请先填写文件夹路径。"));
                return;
            }
            if (!QFileInfo::exists(d)) {
                QMessageBox::warning(this, tr("打开文件夹"),
                                     tr("文件夹不存在：\n%1").arg(d));
                return;
            }
            if (!QDesktopServices::openUrl(QUrl::fromLocalFile(d))) {
                QMessageBox::warning(this, tr("打开文件夹"),
                                     tr("无法调用系统文件管理器打开：\n%1").arg(d));
            }
        });
    };
    makeFolderRow(tr("图片文件夹 1"), QStringLiteral("etc/photo_folder_1"), &m_photoFolder1);
    makeFolderRow(tr("图片文件夹 2"), QStringLiteral("etc/photo_folder_2"), &m_photoFolder2);

    // 书签（原「书签」整页并入）：只在浏览时产生与消费，跟翻页放同一页。
    sec = beginSection(page, tr("书签"));
    addSwitchRow(sec, tr("自动保存当前图片路径为书签"),
                 tr("翻页不追加"),
                 "bookmark/auto_bookmark", false, false);
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("最大书签数"),
            tr("新增书签超过上限时，从最早的书签开始淘汰"));
        m_maxBookmarks = makePopupCombo(parts.row);
        for (int v : { 10, 20, 30, 50, 100 })
            m_maxBookmarks->addItem(QString::number(v), v);
        m_maxBookmarks->setCursor(Qt::PointingHandCursor);
        m_maxBookmarks->setFixedHeight(kComboH);
        m_maxBookmarks->setFixedWidth(110);
        m_maxBookmarks->setAccessibleName(tr("最大书签数"));
        parts.controls->addWidget(m_maxBookmarks, 0, Qt::AlignVCenter);
    }
    addHint(sec, tr("文件移动或重命名后原条目失效；书签只存本机配置，不随安装包或账户同步。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 2.5 外观（第 18 期：主题三档 + 磨砂材质）
// ===========================================================================

void SettingsDialog::buildAppearancePage(QWidget *page)
{
    // 九页重组（2026-11-21）：主题与材质（原样）+ 画面信息（原浏览页视觉项）
    // + 画布与透明背景（原图像处理画布组）。键与即时下发链路全部不变。
    // —— 主题 ——
    QWidget *sec = beginSection(page, tr("主题"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("界面主题"),
            tr("跟随系统时由 Windows 深浅色设置决定"));
        m_themeCombo = makePopupCombo(parts.row);
        m_themeCombo->addItem(tr("跟随系统"), QStringLiteral("system"));
        m_themeCombo->addItem(tr("浅色"), QStringLiteral("light"));
        m_themeCombo->addItem(tr("深色"), QStringLiteral("dark"));
        m_themeCombo->setAccessibleName(tr("界面主题"));
        m_themeCombo->setFixedWidth(kGenComboW);
        parts.controls->addWidget(m_themeCombo, 0, Qt::AlignVCenter);
        connect(m_themeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
            if (!m_themeApplying)
                applyThemePreview();
        });
    }

    const QString theme = sGet("theme", QStringLiteral("dark"));
    syncThemeControls(theme);
    m_themeAppliedSetting = theme;
    m_themeAppliedMode = themeMode(theme);
    updateThemeControlStates();

    // 跟随系统时，Windows 深浅色变化要刷新设置页自身 QSS 与主窗口表面；
    // 字段显示值保持「跟随系统」，不跟着系统色来回跳。
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme cs) {
                Q_UNUSED(cs);
                if (m_themeApplying || m_themePendingActive || !m_themeCombo)
                    return;
                if (m_themeCombo->currentData().toString() != QLatin1String("system"))
                    return;
                Theme::currentSetting() = QStringLiteral("system");
                refreshDialogStyle();
                if (auto *host = qobject_cast<MainWindow *>(parentWidget()))
                    host->refreshThemeSurfaces();
            });

    // —— 材质 ——
    sec = beginSection(page, tr("材质"));
    ToggleRow blurRow = addToggleRow(
        sec, tr("磨砂材质（右键菜单与信息浮层）"),
        tr("仅作用于右键菜单与信息浮层；Windows 11 效果最佳"),
        tr("磨砂材质"));
    m_blurMaterial = blurRow.toggle;
    m_blurMaterial->setChecked(bGet("ui/blur_material", false));
#if defined(Q_OS_WIN)
    if (QOperatingSystemVersion::current() < QOperatingSystemVersion::Windows11) {
        m_blurMaterial->setEnabled(false);   // 不写回、不落盘，保留用户已有取值
        if (blurRow.desc)
            blurRow.desc->setText(tr("当前系统不支持系统级磨砂，已停用"));
    }
#endif
    addHint(sec, tr("磨砂材质在 Windows 10 上降级为半透明。"));

    // —— 画面信息（原浏览页视觉项整体搬入）——
    sec = beginSection(page, tr("画面信息"));
    auto *sideArrows = addSwitchRow(sec, tr("显示侧边箭头"), QString(),
                                    "view/show_side_arrow", false, true);
    auto *overlayName = addSwitchRow(sec, tr("显示文件名浮层"),
                                     tr("隐藏顶端菜单时按 Shift+Tab 显隐"),
                                     "view/overlay_filename", false, false);
    auto *animInfo = addSwitchRow(sec, tr("显示动画帧号"), QString(),
                                  "view/show_anim_control", false, true);
    descToRowTip(animInfo, tr("GIF / MNG 在底部信息行显示「第 n/N 帧」"));
    auto *zoomInfo = addSwitchRow(sec, tr("显示缩放浮标"), QString(),
                                  "view/show_zoom_control", false, true);
    auto *tooltip = addSwitchRow(sec, tr("显示鼠标悬浮提示"), QString(),
                                 "view/show_tooltip", false, true);
    auto *smallFont = addSwitchRow(sec, tr("用小字体显示 EXIF / 文件信息"), QString(),
                                   "view/exif_small_font", false, false);
    m_liveChecks.append({ smallFont, "view/exif_small_font", false, false });
    m_liveChecks.append({ sideArrows, "view/show_side_arrow", false, false });
    m_liveChecks.append({ zoomInfo, "view/show_zoom_control", false, false });
    m_liveChecks.append({ animInfo, "view/show_anim_control", false, false });

    m_liveChecks.append({ overlayName, "view/overlay_filename", false, false });
    m_liveChecks.append({ tooltip, "view/show_tooltip", false, false });

    // —— 画布与透明背景（原「图像处理 · 透明背景」组）——
    sec = beginSection(page, tr("画布与透明背景"));
    // UI-001 §3.8：画布背景只有「棋盘 / 纯色」二选一，颜色自动跟随应用深浅
    // 主题；背景由绘制层铺满整个画布视口，不烘焙进图像。
    auto *canvasBgCombo = addComboRow(sec, tr("画布背景"), QString(),
                "imageproc/canvas_bg", QStringLiteral("checker"),
                { { "checker", tr("棋盘") }, { "solid", tr("纯色") } }, kGenComboW);
    descToRowTip(canvasBgCombo, tr("铺满整个画布视口，颜色随深浅主题自动变化，切换主题即时生效"));
    auto *transparency = addSwitchRow(sec, tr("显示透明区域"),
                 tr("关闭后透明像素直接透出画布背景"),
                 "imageproc/show_transparency", false, true);
    descToRowTip(transparency, tr("棋盘格固定在屏幕坐标，不随图片缩放和平移"));
    addSwitchRow(sec, tr("图片阴影"),
                 tr("在图片边缘加一圈阴影，浅色背景下的浅色图片也能看清边界；"
                    "仅屏幕预览，不进入复制、打印、批量转换与另存为"),
                 "imageproc/canvas_shadow", false, false);

    // GIF 透明背景（翻页批次 2，2026-11-20 重新暴露 + 自定义取色）：
    // 只影响 GIF 显示铺底，源文件/复制/导出保持原透明信息。旧 0..3 档语义
    // 不变；新增第 5 档「自定义颜色」，选色独立存 imageproc/gif_custom_color。
    m_gifBgCombo = addComboRow(sec, tr("GIF 透明背景"), QString(),
                               "imageproc/gif_transparent", QStringLiteral("0"),
                               {{QStringLiteral("0"), tr("默认调色板")},
                                {QStringLiteral("1"), tr("白色")},
                                {QStringLiteral("2"), tr("黑色")},
                                {QStringLiteral("3"), tr("中灰背景")},
                                {QStringLiteral("4"), tr("自定义颜色")}},
                               kGenComboW);
    descToRowTip(m_gifBgCombo, tr("只影响 GIF 显示铺底；源文件、复制与导出保持原透明信息"));
    auto gifColorParts = addSettingRow(sec, tr("自定义颜色"), QString());
    m_gifColorBtn = new QPushButton(gifColorParts.row);
    m_gifColorBtn->setFixedSize(56, 26);
    m_gifColorBtn->setCursor(Qt::PointingHandCursor);
    gifColorParts.controls->addWidget(m_gifColorBtn, 0, Qt::AlignVCenter);
    descToRowTip(m_gifColorBtn, tr("点击选择 GIF 透明区域的铺底颜色，选色即时重绘当前 GIF"));
    // 三控件都即时预览：写盘由 comboBind / chkBind 的即时路径统一走，这里只
    // 补运行态下发（applyLiveSetting 直达画布，模态设置页开着也能看到效果）。
    for (const ComboBind &b : m_comboBinds) {
        if (qstrcmp(b.key, "imageproc/canvas_bg") != 0)
            continue;
        connect(b.combo, &QComboBox::currentIndexChanged, this, [this, b]() {
            if (m_loading)
                return;
            if (MainWindow *w = qobject_cast<MainWindow *>(parentWidget()))
                w->applyLiveSetting(QStringLiteral("imageproc/canvas_bg"),
                                    b.combo->currentData().toString(), nullptr);
        });
    }
    for (const ChkBind &b : m_chkBinds) {
        if (qstrcmp(b.key, "imageproc/show_transparency") != 0
            && qstrcmp(b.key, "imageproc/canvas_shadow") != 0)
            continue;
        const QString key = QString::fromLatin1(b.key);
        connect(b.cb, &QCheckBox::toggled, this, [this, key](bool on) {
            if (m_loading)
                return;
            if (MainWindow *w = qobject_cast<MainWindow *>(parentWidget()))
                w->applyLiveSetting(key, on, nullptr);
        });
    }

    // 内联状态行：模态设置页看不到主窗口时，用这行就地反馈「刚才那一下已经生效」，
    // 并说明取消回滚口径（派单 §3 要求可观察，不得只换胶囊）。
    m_browseLiveNote = new QLabel(page);
    m_browseLiveNote->setObjectName(QStringLiteral("noteLabel"));
    m_browseLiveNote->setWordWrap(true);
    m_browseLiveNote->hide();
    if (page->layout())
        page->layout()->addWidget(m_browseLiveNote);

    applyPageRhythm(page);
}

QString SettingsDialog::themeFromControls() const
{
    if (m_themeCombo) {
        const QString v = m_themeCombo->currentData().toString();
        if (v == QLatin1String("system") || v == QLatin1String("light")
            || v == QLatin1String("dark"))
            return v;
    }
    return QStringLiteral("dark");
}

void SettingsDialog::syncThemeControls(const QString &theme)
{
    if (!m_themeCombo)
        return;
    const QString v = (theme == QLatin1String("system") || theme == QLatin1String("light"))
        ? theme : QStringLiteral("dark");
    const int idx = m_themeCombo->findData(v);
    const QSignalBlocker blocker(m_themeCombo);
    m_themeCombo->setCurrentIndex(idx >= 0 ? idx : 2);
    m_themeLast = theme;
}

void SettingsDialog::updateThemeControlStates()
{
    // 单一 pop-up 字段没有互斥禁用项；三个值始终都可选。
    // 保留该入口供既有调用点（加载配置 / 失败回退）复用。
}

void SettingsDialog::syncAppliedTheme(const QString &theme)
{
    if (m_themeApplying)
        return;
    if (m_themeDefer)
        m_themeDefer->stop();
    ++m_themeSerial;
    m_themePendingActive = false;
    m_themePending.clear();
    syncThemeControls(theme);
    m_themeAppliedSetting = theme;
    m_themeAppliedMode = themeMode(theme);
    refreshDialogStyle();
}

void SettingsDialog::refreshDialogStyle()
{
    // 设置页构造时把 dialogQss() 编译进了局部表；主题切换后必须按新令牌重装，
    // 否则设置页（用户正在看的这一面）会停在旧主题。
    setStyleSheet(UiStyle::dialogQss() + extraQss());
    if (m_refreshAppMenuStyle) {
        m_refreshAppMenuStyle();
    }
    update();
}

void SettingsDialog::applyThemePreview()
{
    if (!m_themeCombo)
        return;

    const QString want = themeFromControls();
    ThemeProbe::begin("settings-select", want.toUtf8().constData());
    // 来源状态立即生效：跟随系统的监听条件与禁用态不依赖是否换肤。
    Theme::currentSetting() = want;
    updateThemeControlStates();

    if (themeMode(want) == m_themeAppliedMode) {
        // 等色：手选深色 ↔ 跟随系统(系统当前为深色) 等场景，运行态配色没变。
        // 只同步来源状态并作废在途请求，不重装全局调色板 / 全局样式。
        ++m_themeSerial;
        m_themePendingActive = false;
        m_themePending.clear();
        m_themeLast = want;
        m_themeAppliedSetting = want;
        // 等色也写盘：来源从「手选深色」变成「跟随系统」，配色虽未变，
        // 但配置必须保存，否则关窗后来源又跳回去。
        applyInstantValue(QStringLiteral("theme"), want);
        ThemeProbe::mark(ThemeProbe::currentSeq(), "settings-same-mode-done",
                         want.toUtf8().constData());
        return;
    }

    // 真正变色：点击回调只排队，重任务压到下一次事件循环执行（不在信号栈内
    // 同步重抛光）。2026-11-16：原来是等 PillToggle 旧胶囊动画 120ms+30ms，
    // 但主题字段早已改成下拉框、没有圆点动画，这段固定等待是纯延迟；
    // 快速连续选择仍由 m_themeSerial 保证只落最后一次。
    m_themePending = want;
    m_themePendingActive = true;
    ++m_themeSerial;
    ThemeProbe::mark(ThemeProbe::currentSeq(), "settings-defer-start",
                     want.toUtf8().constData());
    if (!m_themeDefer) {
        m_themeDefer = new QTimer(this);
        m_themeDefer->setSingleShot(true);
        connect(m_themeDefer, &QTimer::timeout, this, [this]() {
            ThemeProbe::mark(ThemeProbe::currentSeq(), "settings-defer-fire", "");
            applyPendingTheme(m_themeSerial);
        });
    }
    m_themeDefer->start(0);   // 仅排到下一事件循环，不再等旧开关动画 150ms
}

void SettingsDialog::applyPendingTheme(int serial)
{
    if (!m_themePendingActive || serial != m_themeSerial)
        return;   // 已被更新的点击或回滚取代，不能覆盖最终选择
    const QString want = m_themePending;
    m_themePendingActive = false;
    m_themePending.clear();
    if (!m_themeCombo)
        return;
    auto *host = qobject_cast<MainWindow *>(parentWidget());
    if (!host)
        return;

    m_themeApplying = true;
    QString error;
    const bool ok = host->applyLiveSetting(QStringLiteral("theme"), want, &error);
    if (ok) {
        m_themeAppliedSetting = want;
        m_themeAppliedMode = themeMode(want);
        m_themeLast = want;
        ThemeProbe::mark(ThemeProbe::currentSeq(), "dlg-style-start", "");
        refreshDialogStyle();
        ThemeProbe::mark(ThemeProbe::currentSeq(), "dlg-style-done", "");
        // 主题是即时项：切换成功即写盘，关闭窗口（含「不保存」）也保留。
        applyInstantValue(QStringLiteral("theme"), want);
        ThemeProbe::mark(ThemeProbe::currentSeq(), "settings-done",
                         want.toUtf8().constData());
    } else {
        // 运行态没变（失败发生在下发入口）；回退到运行态实际值，而不是
        // 用户最后一次点选值，避免「控件显示浅色、界面还是深色」。
        Theme::currentSetting() = m_themeAppliedSetting;
        syncThemeControls(m_themeAppliedSetting);
        updateThemeControlStates();
        QMessageBox::warning(this, tr("主题未应用"),
                             error.isEmpty() ? tr("本次主题切换失败，已恢复原设置。")
                                             : error);
    }
    m_themeApplying = false;
}

void SettingsDialog::flushPendingTheme()
{
    // 保存前把排队中的真实变色同步做完，避免「配置写浅色、运行态还没换」。
    if (m_themeDefer)
        m_themeDefer->stop();
    if (m_themePendingActive)
        applyPendingTheme(m_themeSerial);
}

// ===========================================================================
// 3. 图像处理
// ===========================================================================

void SettingsDialog::buildImageProcPage(QWidget *page)
{
    // 九页重组（2026-11-21）：渲染 / 自动保存 / 缓存与载入 / RAW / 色彩管理。
    // 画布与 GIF 背景移入「外观与显示」，翻页效果与大图对齐移入「浏览」；
    // 键、默认值、即时链路保持原样。
    QWidget *sec = beginSection(page, tr("渲染"));
    auto *interpCombo = addComboRow(sec, tr("插值算法"), QString(),
                "imageproc/interpolate", QStringLiteral("0"),
                { { "0", tr("最近邻 (快)") }, { "1", tr("平滑 (双线性)") },
                  { "2", tr("Lanczos (锐利)") } });
    descToRowTip(interpCombo, tr("缩放重采样时使用；最近邻最快，Lanczos 最锐"));
    addSwitchRow(sec, tr("使用强锐化滤镜"),
                 tr("Lanczos 且缩小到 100% 以下时生效"),
                 "imageproc/strong_sharpen", false, false);

    // 旋转后自动保存（A2）：标准开关行，键 config/auto_save_rotate 默认关。
    sec = beginSection(page, tr("自动保存"));
    addSwitchRow(sec, tr("旋转后自动保存"),
                 tr("旋转/翻转立即写回原文件；RAW 与动图不受影响"),
                 "auto_save_rotate", false, false);

    sec = beginSection(page, tr("缓存与载入"));
    addSwitchRow(sec, tr("缓存下一张图片"), QString(),
                 "imageproc/cache_image", false, true);
    {
        SettingRowParts parts = addSettingRow(sec, tr("缓存大小"),
                                              tr("全尺寸解码缓存的占用上限，范围 64–4096 MB，越界按边界值生效"));
        m_cacheSize = new QSpinBox(parts.row);
        m_cacheSize->setRange(64, 4096);
        m_cacheSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
        m_cacheSize->setFixedHeight(kComboH);
        m_cacheSize->setFixedWidth(88);
        m_cacheSize->setAlignment(Qt::AlignCenter);
        m_cacheSize->setAccessibleName(tr("缓存大小"));
        parts.controls->addWidget(m_cacheSize, 0, Qt::AlignVCenter);
        auto *mb = new QLabel(tr("MB"), parts.row);
        mb->setObjectName(QStringLiteral("rowUnit"));
        parts.controls->addWidget(mb, 0, Qt::AlignVCenter);
    }
    addSwitchRow(sec, tr("先显示低品质图片以加速渲染"), QString(),
                 "imageproc/preview_mode", false, true);

    // RAW（原「杂项 · RAW 图片」组）：三条开关的消费点都在解码链路。
    sec = beginSection(page, tr("RAW 图片"));
    addSwitchRow(sec, tr("不打开 RAW 图片文件"),
                 QString(), "etc/no_raw_load", false, false);
    addSwitchRow(sec, tr("打开 JPEG 文件时，不打开同名 RAW 文件"),
                 QString(),
                 "etc/skip_raw_when_jpg", false, true);
    // #84 RAW 缩略图：批次22 起为即时项，注册进 m_liveChecks 走「下发运行态 →
    // 写盘 → 失败回退」链路；与 #82 noRawLoad() 的 && 短路联动由消费点保留。
    auto *rawThumb = addSwitchRow(sec, tr("打开 RAW 图片文件时，显示已有缩略图"),
                 QString(),
                 "etc/raw_thumbnail", false, true);
    m_liveChecks.append({ rawThumb, "etc/raw_thumbnail", false, false });

    sec = beginSection(page, tr("色彩管理"));
    auto *iccFile = addSwitchRow(sec, tr("使用图片文件的 ICC 设置"), QString(),
                 "imageproc/image_icc", false, true);
    descToRowTip(iccFile, tr("打开带非 sRGB ICC 的图片时按配置转换上屏"));
    auto *iccMonitor = addSwitchRow(sec, tr("使用监视器的 ICC 设置"),
                                    tr("SDR 验证版：显示出口把 sRGB 转到当前屏幕关联配置；"
                                       "已开 Windows 自动颜色管理的屏幕跳过，跨屏自动重估"),
                                    "imageproc/monitor_icc", false, false);
    descToRowTip(iccMonitor, tr("无有效配置或转换失败时保持 sRGB 显示，原因见临时目录日志"));
    applyPageRhythm(page);
}

// ===========================================================================
// 4. 格式关联：说明 + 两个系统按钮 + 四组格式勾选网格

// ===========================================================================
// 5. 键盘：勾选 | 热键 | 操作 表格（13 行）
// ===========================================================================

void SettingsDialog::buildKeyboardPage(QWidget *page)
{
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    if (!pageLay)
        return;

    // 退出行为（2026-11-21 九页重组）：原「一般」页的 Esc 退出挪到键盘页，
    // 与热键表同处一页；键名、默认值、即时下发链路均不变。
    {
        QWidget *escSec = beginSection(page, tr("退出行为"));
        escSec->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        auto *escExit = addSwitchRow(escSec, tr("按 ESC 退出"), QString(),
                                     "esc_exit", false, true);
        m_liveChecks.append({ escExit, "esc_exit", false, false });
    }

    // 页首说明：默认态 / 录入态两种文案（规格 §焦点与键盘状态）。冲突时由
    // 红条顶替，不叠两条提示（设计图 06）。
    m_keyHint = new QLabel(page);
    m_keyHint->setObjectName(QStringLiteral("pageHint"));
    m_keyHint->setWordWrap(true);
    // 退出行为组与热键说明之间留一段呼吸：说明属于整张键盘表，不是上一组的尾部。
    m_keyHint->setContentsMargins(kGenRowPadX, 16, kGenRowPadX, 6);
    m_keyHint->setText(tr("双击热键录入，Esc 清空。勾选即时生效，改绑需保存。"));
    pageLay->addWidget(m_keyHint);

    // 冲突红条：指出涉及的动作与键位，修正后自动消失（可被辅助技术读到）。
    m_keyBanner = new QWidget(page);
    m_keyBanner->setObjectName(QStringLiteral("keyBanner"));
    m_keyBanner->setAttribute(Qt::WA_StyledBackground, true);
    auto *bannerLay = new QHBoxLayout(m_keyBanner);
    bannerLay->setContentsMargins(10, 7, 10, 7);
    bannerLay->setSpacing(8);
    auto *bannerIcon = new QLabel(QStringLiteral("!"), m_keyBanner);
    bannerIcon->setObjectName(QStringLiteral("keyBannerIcon"));
    bannerIcon->setAlignment(Qt::AlignCenter);
    bannerIcon->setFixedSize(16, 16);
    bannerLay->addWidget(bannerIcon, 0, Qt::AlignVCenter);
    m_keyBannerText = new QLabel(m_keyBanner);
    m_keyBannerText->setObjectName(QStringLiteral("keyBannerText"));
    m_keyBannerText->setWordWrap(true);
    m_keyBannerText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bannerLay->addWidget(m_keyBannerText, 1);
    m_keyBanner->hide();
    pageLay->addWidget(m_keyBanner);

    auto *table = new QFrame(page);
    table->setObjectName(QStringLiteral("keyTable"));
    auto *tl = new QVBoxLayout(table);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(0);

    // 表头 30px：操作 / 热键 / 启用（规格 §布局与文字）。
    auto *head = new QWidget(table);
    head->setObjectName(QStringLiteral("keyHead"));
    head->setFixedHeight(30);
    auto *hl = new QHBoxLayout(head);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    auto makeHeadCell = [head](const QString &text, int width, int leftPad = 8) {
        auto *lab = new QLabel(text, head);
        lab->setObjectName(QStringLiteral("keyHeadCell"));
        lab->setContentsMargins(leftPad, 0, 8, 0);
        if (width > 0)
            lab->setFixedWidth(width);
        return lab;
    };
    hl->addWidget(makeHeadCell(tr("操作"), 0, kGenRowPadX), 1);
    hl->addWidget(makeHeadCell(tr("热键"), 160, 0));
    hl->addWidget(makeHeadCell(tr("启用"), 52, 0));
    hl->addSpacing(8);
    tl->addWidget(head);

    // 单一滚动区：表头固定，分组头与行一起滚（窄窗 900×660 用同一支）。
    auto *rowsArea = new KeyRowsArea(table);
    rowsArea->setObjectName(QStringLiteral("keyRowsArea"));
    auto *rowsBox = new QWidget;
    rowsBox->setObjectName(QStringLiteral("keyRowsBox"));
    auto *rl0 = new QVBoxLayout(rowsBox);
    rl0->setContentsMargins(0, 0, 0, 6);
    rl0->setSpacing(0);

    // 默认键位 = MainWindow 出厂硬编码值（PortableText；幻灯片出厂无单键）。
    // 四个分组按文件、浏览、显示与缩放、编辑与其他排列（规格 §焦点与键盘状态）。
    struct KeyDef {
        const char *group;
        const char *action;
        const char *hotkey;
        const char *label;
    };
    static const KeyDef defs[] = {
        { "file", "open",        "Ctrl+O",     QT_TR_NOOP("打开") },
        { "file", "openfolder",  "F",          QT_TR_NOOP("打开文件夹") },
        { "nav",  "next",        "Right",      QT_TR_NOOP("翻下一张") },
        { "nav",  "prev",        "Left",       QT_TR_NOOP("翻上一张") },
        { "nav",  "next_frame",  "Ctrl+Right", QT_TR_NOOP("下一帧") },
        { "nav",  "prev_frame",  "Ctrl+Left",  QT_TR_NOOP("上一帧") },
        { "nav",  "slideshow",   "",           QT_TR_NOOP("幻灯片") },
        { "view", "zoom_in",     "Ctrl++",     QT_TR_NOOP("放大") },
        { "view", "zoom_out",    "Ctrl+-",     QT_TR_NOOP("缩小") },
        { "view", "actual_size", "0",          QT_TR_NOOP("实际大小") },
        { "view", "fit",         "9",          QT_TR_NOOP("适配窗口") },
        { "edit", "favorite",    "B",          QT_TR_NOOP("收藏") },
        { "edit", "del",         "Del",        QT_TR_NOOP("删除") },
    };
    static const struct { const char *id; const char *title; } groups[] = {
        { "file", QT_TR_NOOP("文件") },
        { "nav",  QT_TR_NOOP("浏览") },
        { "view", QT_TR_NOOP("显示与缩放") },
        { "edit", QT_TR_NOOP("编辑与其他") },
    };

    QString currentGroup;
    int groupCount = 0;
    int groupHeadTotal = 0;   // 交给行区：分组头也占纵向空间
    constexpr int kKeyRowH = 36;
    constexpr int kKeyGroupHeadH = 22;

    for (int i = 0; i < int(sizeof(defs) / sizeof(defs[0])); ++i) {
        const KeyDef &def = defs[i];
        const QString groupId = QString::fromLatin1(def.group);
        if (groupId != currentGroup) {
            currentGroup = groupId;
            auto *gh = new QWidget(table);
            gh->setObjectName(QStringLiteral("keyGroupHead"));
            gh->setAttribute(Qt::WA_StyledBackground, true);
            gh->setFixedHeight(kKeyGroupHeadH);
            auto *gl = new QHBoxLayout(gh);
            gl->setContentsMargins(kGenRowPadX, 0, 8, 0);
            auto *gt = new QLabel(gh);
            gt->setObjectName(QStringLiteral("keyGroupTitle"));
            for (const auto &g : groups) {
                if (groupId == QLatin1String(g.id)) {
                    gt->setText(tr(g.title));
                    break;
                }
            }
            gl->addWidget(gt);
            gl->addStretch(1);
            rl0->addWidget(gh);
            groupHeadTotal += kKeyGroupHeadH;
            ++groupCount;
        } else {
            auto *line = new QFrame(table);
            line->setObjectName(QStringLiteral("keyRowLine"));
            line->setFixedHeight(1);
            rl0->addWidget(line);
        }

        auto *row = new QWidget(table);
        row->setObjectName(QStringLiteral("keyRow"));
        row->setFixedHeight(kKeyRowH - 1);
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setAttribute(Qt::WA_Hover, true);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        rl->setSpacing(0);

        auto *actionLab = new QLabel(tr(def.label), row);
        actionLab->setObjectName(QStringLiteral("keyAction"));
        actionLab->setContentsMargins(kGenRowPadX, 0, 8, 0);
        rl->addWidget(actionLab, 1);

        // 热键列：键帽固定 88 宽、左对齐；右侧留出行内状态文字
        // （「正在录入」/「冲突」），不挤压键帽本身。
        auto *editHost = new QWidget(row);
        editHost->setFixedWidth(160);
        auto *el = new QHBoxLayout(editHost);
        el->setContentsMargins(0, 0, 0, 0);
        el->setSpacing(6);
        auto *edit = new QKeySequenceEdit(editHost);
        edit->setFixedSize(88, 26);
        new KeySeqPlaceholderFixer(edit);
        new KeySeqEscClearFilter(edit);
        new KeySeqStateFilter(edit, [this]() { refreshKeyEditingState(); });
        el->addWidget(edit, 0, Qt::AlignVCenter);
        auto *stateLab = new QLabel(editHost);
        stateLab->setObjectName(QStringLiteral("keyRowState"));
        stateLab->hide();
        el->addWidget(stateLab, 0, Qt::AlignVCenter);
        el->addStretch(1);
        rl->addWidget(editHost);

        auto *chkHost = new QWidget(row);
        chkHost->setFixedWidth(52);
        auto *cl2 = new QHBoxLayout(chkHost);
        cl2->setContentsMargins(0, 0, 0, 0);
        // 启用列统一用 checkbox（表格内逐项启用），不再是 mini switch。
        auto *chk = new V2CheckBox(chkHost);
        chk->setObjectName(QStringLiteral("keyEnable"));
        chk->setAccessibleName(tr("%1 启用").arg(tr(def.label)));
        cl2->addWidget(chk, 0, Qt::AlignCenter);
        rl->addWidget(chkHost);
        rl->addSpacing(8);

        // 任一字段改动即重算冲突：红条、行内标记与保存按钮一起变。
        connect(chk, &QCheckBox::toggled, this, [this]() { refreshKeyConflictBanner(); });
        connect(edit, &QKeySequenceEdit::keySequenceChanged, this,
                [this]() { refreshKeyConflictBanner(); });

        KeyRow kr;
        kr.cb = chk;
        kr.edit = edit;
        kr.actionLab = actionLab;
        kr.row = row;
        kr.action = QString::fromUtf8(def.action);
        kr.defHotkey = QString::fromUtf8(def.hotkey);
        kr.label = tr(def.label);
        kr.stateLab = stateLab;
        m_keyRows.append(kr);
        rl0->addWidget(row);
    }

    // 视口高于表格内容时，把多余空间留在最后一行下方。
    rl0->addStretch(1);
    rowsArea->setWidget(rowsBox);
    // 单一表格滚动区（规格 §审图索引）：表格吃满页面剩余高度，表头在滚动区
    // 之外固定，分组头与 13 行一起滚；900×660 与 1068×822 用同一支布局，
    // 只由可用高度决定露出多少行。不再做整行吸附——设计图 08 本身就允许
    // 末行被视口边缘自然截断。
    tl->addWidget(rowsArea, 1);
    table->ensurePolished();
    table->setMaximumHeight(head->height() + rowsBox->sizeHint().height()
                            + 2 * table->frameWidth());
    pageLay->addWidget(table, 1);
    pageLay->addStretch();

    applyPageRhythm(page);
}

// ===========================================================================
// 6. 鼠标：两条设定 + 两列「按键 → 动作」
// ===========================================================================

void SettingsDialog::buildMousePage(QWidget *page)
{
    QWidget *sec = beginSection(page, tr("鼠标设定"));
    // 两项均已接线：双击最大化/还原走 ImageCanvas::doubleClickRequested →
    // MainWindow 切换窗口；两侧单击翻页走 edgeTurnRequested，且老配置默认值
    // 做过一次性迁移（main.cpp 的 etc/migrated_side_click）。
    addSwitchRow(sec, tr("双击最大化 / 还原"), QString(),
                 "mouse/dblclick_maximize", false, true);
    addSwitchRow(sec, tr("单击屏幕两侧时翻页"), QString(),
                 "mouse/click_side_pageturn", false, false);

    sec = beginSection(page, tr("按键映射"));

    // 翻页批次 2（2026-11-20）：按键与手势独立映射。中键拆成「单击 / 拖动」
    // 两行，滚轮上下各自一行、单向「无」只停用该方向。左/右/X/Y 键由
    // ImageCanvas 判定后发 mouseActionRequested → MainWindow::onMouseAction
    // 派发；中键单击与滚轮翻页走同一派发器；none 保留内置行为（左键拖拽、
    // 右键菜单等）。支持矩阵外的档位灰显并说明原因，不做假开关。
    struct BtnDef { const char *key; const char *label; const char *def; bool configurable; };
    static const BtnDef btns[] = {
        { "mouse/btn_left",         QT_TR_NOOP("鼠标左键"),   "none",       true },
        { "mouse/btn_middle_click", QT_TR_NOOP("中键单击"),   "fullscreen", true },
        { "mouse/btn_middle_drag",  QT_TR_NOOP("中键拖动"),   "pan",        true },
        { "mouse/btn_right",        QT_TR_NOOP("鼠标右键"),   "none",       true },
        { "mouse/btn_x",            QT_TR_NOOP("X 键"),       "none",       true },
        { "mouse/btn_y",            QT_TR_NOOP("Y 键"),       "none",       true },
        { "mouse/wheel_up",         QT_TR_NOOP("滚轮向上"),   "zoom_in",    true },
        { "mouse/wheel_down",       QT_TR_NOOP("滚轮向下"),   "zoom_out",   true },
    };

    const auto opts = actionOptions();
    for (int i = 0; i < 8; ++i) {
        const BtnDef &b = btns[i];
        SettingRowParts parts = addSettingRow(sec, tr(b.label), QString());
        auto *combo = makePopupCombo(parts.row);
        for (const auto &opt : opts)
            combo->addItem(opt.second, opt.first);
        const QString key = QString::fromUtf8(b.key);
        // 支持矩阵：右键 / X / Y 没有拖拽平移实现（按下即派发，无拖动阶段）；
        // 中键单击是点击手势，pan 灰显；中键拖动只支持 none/pan；
        // 滚轮支持 无/放大/缩小/翻下一张/翻上一张（连滚即连翻）。
        QStringList allowed;
        QString denyTip;
        if (key == QLatin1String("mouse/btn_right")
            || key == QLatin1String("mouse/btn_x")
            || key == QLatin1String("mouse/btn_y")) {
            for (int j = 0; j < opts.size(); ++j) {
                if (opts[j].first == QLatin1String("pan")) {
                    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                        if (auto *item = model->item(j)) {
                            const QString why = tr("该按键没有拖拽平移实现，灰显不可选");
                            item->setEnabled(false);
                            item->setToolTip(why);
                            item->setData(why, Qt::UserRole + 1);
                        }
                    }
                }
            }
        } else if (key == QLatin1String("mouse/btn_middle_click")) {
            for (int j = 0; j < opts.size(); ++j) {
                if (opts[j].first == QLatin1String("pan")) {
                    if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                        if (auto *item = model->item(j)) {
                            const QString why = tr("单击手势不支持拖拽平移；拖动请在「中键拖动」单独设置");
                            item->setEnabled(false);
                            item->setToolTip(why);
                            item->setData(why, Qt::UserRole + 1);
                        }
                    }
                }
            }
        } else if (key == QLatin1String("mouse/btn_middle_drag")) {
            allowed << QStringLiteral("none") << QStringLiteral("pan");
            denyTip = tr("拖动手势仅支持「无操作 / 拖拽平移」；点击动作请在「中键单击」单独设置");
        } else if (key == QLatin1String("mouse/wheel_up")
                   || key == QLatin1String("mouse/wheel_down")) {
            allowed << QStringLiteral("none") << QStringLiteral("zoom_in")
                    << QStringLiteral("zoom_out") << QStringLiteral("next")
                    << QStringLiteral("prev");
            denyTip = tr("滚轮仅支持「无操作 / 放大 / 缩小 / 翻下一张 / 翻上一张」；"
                         "单向选「无」只停用该方向");
        }
        if (!allowed.isEmpty()) {
            for (int j = 0; j < opts.size(); ++j) {
                if (allowed.contains(opts[j].first))
                    continue;
                if (auto *model = qobject_cast<QStandardItemModel *>(combo->model())) {
                    if (auto *item = model->item(j)) {
                        item->setEnabled(false);
                        item->setToolTip(denyTip);
                        item->setData(denyTip, Qt::UserRole + 1);
                    }
                }
            }
        }
        // 2026-11-17：与分组页脚同一句话，不再在每个下拉上重复一遍。
        combo->setCursor(Qt::PointingHandCursor);
        combo->setFixedHeight(kComboH);
        combo->setProperty("reqW", 170);
        combo->setFixedWidth(170);
        parts.controls->addWidget(combo, 0, Qt::AlignVCenter);

        if (!b.configurable)
            markUnavailable(parts.row);   // 整行（行标签 + 下拉）一起灰

        m_mouseRows.append({ combo, QString::fromUtf8(b.key), QString::fromUtf8(b.def) });
    }

    addHint(sec, tr("「无」保留内置行为：左键拖拽平移、边缘单击翻页、右键菜单。"
                    "中键单击与拖动分别设置（默认单击全屏、拖动平移）；滚轮上下独立生效，"
                    "单向选「无」只停用该方向。映射在下次触发该按键时生效。"));
    applyPageRhythm(page);
}

// ===========================================================================
// 7. 书签

// ===========================================================================
// 8. 上下文菜单
// ===========================================================================

// 验收探针（--smoke settingsctx:<leaf>）：切到上下文菜单页并点击指定开关。
// 点击走 PillToggle::toggled → writeValues 真实写盘链路，等价鼠标点击。
// 导航/点击都延迟到事件循环稳定后（与 settingspage: 探针同节奏）。
void SettingsDialog::probeCtxToggle(const QString &leaf)
{
    QTimer::singleShot(400, this, [this, leaf]() {
        const auto navs = findChildren<QPushButton *>(QStringLiteral("navItem"));
        for (QPushButton *b : navs) {
            if (b->text().contains(tr("Windows 集成"))) {
                b->click();
                break;
            }
        }
        QTimer::singleShot(400, this, [this, leaf]() {
            const QString want = QStringLiteral("ctxmenu.") + leaf;
            const auto toggles = findChildren<PillToggle *>();
            for (PillToggle *t : toggles) {
                if (t->accessibleName() == want) {
                    t->click();
                    return;
                }
            }
        });
    });
}

// 本任务验收探针（--smoke appmenu:<id>[,<id>…]）：切到上下文菜单页后依次点击
// 应用内右键菜单可见性开关（appMenuId），驱动「父关子灰 /
// 组关整树灰 / 父开全量」等状态；--smoke appmenureset 点「恢复默认菜单」。
// 同样走真实 PillToggle::toggled / clicked → 写盘链路。
void SettingsDialog::probeAppMenuToggle(const QString &ids)
{
    QTimer::singleShot(400, this, [this, ids]() {
        selectPage(m_appMenuPageIndex);
        QTimer::singleShot(400, this, [this, ids]() {
            const QStringList list = ids.split(u',');
            for (const QString &raw : list) {
                const QString want = raw.trimmed();
                for (PillToggle *t : findChildren<PillToggle *>()) {
                    if (t->property("appMenuId").toString() == want) {
                        t->click();
                        break;
                    }
                }
            }
        });
    });
}

void SettingsDialog::probeAppMenuReset()
{
    QTimer::singleShot(400, this, [this]() {
        selectPage(m_appMenuPageIndex);
        QTimer::singleShot(400, this, [this]() {
            if (auto *btn = findChild<QPushButton *>(QStringLiteral("btnResetAppMenu"))) {
                btn->click();
            }
        });
    });
}

// probeAppMenuPageGrab（--smoke ctxsettings:<out>）：切到上下文菜单页后把
// 整页内容 grab 落盘（长图，不受滚动视口裁剪）。页面按本任务的
// btnResetAppMenu 标记定位，不依赖页序号。
void SettingsDialog::probeAppMenuPageGrab(const QString &out)
{
    QTimer::singleShot(400, this, [this, out]() {
        selectPage(m_appMenuPageIndex);
        QTimer::singleShot(400, this, [this, out]() {
            auto *scroll = qobject_cast<QScrollArea *>(m_appMenuOutline);
            if (scroll && scroll->widget()) {
                scroll->widget()->grab().save(out);
            }
            QCoreApplication::quit();
        });
    });
}

// probeAppMenuWindow（--smoke appmenushot:<out>[:<W>x<H>]）：切到「查看器右键
// 菜单」页，可选改窗口尺寸，再抓整个设置窗（侧栏 + 页头 + 大纲 + 页脚）。
void SettingsDialog::probeAppMenuWindow(const QString &out, int w, int h, const QString &scrollTo,
                                       const QString &steps)
{
    QTimer::singleShot(350, this, [this, out, w, h, scrollTo, steps]() {
        selectPage(m_appMenuPageIndex);
        if (w > 0 && h > 0) {
            resize(w, h);
        }
        QTimer::singleShot(600, this, [this, out, scrollTo, steps]() {
            auto *sc = qobject_cast<QScrollArea *>(m_appMenuOutline);
            const auto rows = m_appMenuPage->findChildren<QWidget *>(QStringLiteral("appMenuRow"));
            auto rowById = [&rows](const QString &id) -> QWidget * {
                for (QWidget *row : rows) {
                    if (row->property("appMenuId").toString() == id)
                        return row;
                }
                return nullptr;
            };
            auto *find = m_appMenuPage->findChild<QLineEdit *>(QStringLiteral("appMenuFind"));
            QStringList states;
            auto snapshot = [&, this](const QString &label) {
                const int top = sc->verticalScrollBar()->value();
                const QWidget *focus = QApplication::focusWidget();
                QString focused;
                QString anchor;
                for (QWidget *row : rows) {
                    if (row->isVisible() && anchor.isEmpty() && row->y() + row->height() > top) {
                        anchor = QStringLiteral("%1@%2")
                            .arg(row->property("appMenuId").toString()).arg(row->y() - top);
                    }
                    if (focus && (focus == row || row->isAncestorOf(focus))) {
                        focused = row->property("appMenuId").toString();
                        if (qobject_cast<const PillToggle *>(focus))
                            focused += QStringLiteral(".switch");
                        else if (focus->objectName() == QLatin1String("outlineChevron"))
                            focused += QStringLiteral(".chevron");
                    }
                }
                states << QStringLiteral("%1 size=%2x%3 scroll=%4 anchor=%5 focus=%6 search=%7 query=%8")
                              .arg(label).arg(width()).arg(height()).arg(top).arg(anchor, focused)
                              .arg(find->hasFocus() ? 1 : 0).arg(find->text());
                states << QStringLiteral("  tableWidth=%1 navWidth=%2")
                              .arg(sc->parentWidget()->width()).arg(m_navRail->width());
                for (QWidget *row : rows) {
                    if (!row->isVisible())
                        continue;
                    auto *chev = row->findChild<QAbstractButton *>(QStringLiteral("outlineChevron"));
                    auto *toggle = row->findChild<PillToggle *>();
                    states << QStringLiteral("  %1 y=%2 h=%3 expanded=%4 switch=%5 enabled=%6 %7")
                                  .arg(row->property("appMenuId").toString()).arg(row->y()).arg(row->height())
                                  .arg(chev ? int(chev->isChecked()) : -1)
                                  .arg(toggle ? int(toggle->isChecked()) : -1)
                                  .arg(toggle ? int(toggle->isEnabled()) : -1)
                                  .arg(row->accessibleDescription());
                }
            };
            if (!steps.isEmpty()) {
                for (const QString &step : steps.split(QLatin1Char(';'))) {
                    QApplication::processEvents();
                    if (step.startsWith(QLatin1String("find="))) {
                        find->setFocus(Qt::ShortcutFocusReason);
                        find->setText(step.mid(5));
                    } else if (step == QLatin1String("clear")) {
                        if (auto *clear = find->findChild<QAction *>(QStringLiteral("appMenuClearSearch")))
                            clear->trigger();
                    } else if (step.startsWith(QLatin1String("state="))) {
                        snapshot(step.mid(6));
                    } else if (step.startsWith(QLatin1String("size="))) {
                        const QStringList size = step.mid(5).split(u'x');
                        resize(size.value(0).toInt(), size.value(1).toInt());
                    } else if (step.startsWith(QLatin1String("page="))) {
                        selectPage(step.mid(5).toInt());
                    } else if (step.startsWith(QLatin1String("theme=")) && m_themeCombo) {
                        m_themeCombo->setCurrentIndex(m_themeCombo->findData(step.mid(6)));
                    } else if (step.startsWith(QLatin1String("scroll="))) {
                        if (QWidget *row = rowById(step.mid(7)))
                            sc->verticalScrollBar()->setValue(row->y() + 3);
                    } else if (step.startsWith(QLatin1String("toggle="))) {
                        const QString id = step.mid(7);
                        const auto boxes = findChildren<QCheckBox *>();
                        for (QCheckBox *cb : boxes) {
                            if (cb->property("appMenuId").toString() == id) {
                                cb->click();
                                break;
                            }
                        }
                    } else if (step.startsWith(QLatin1String("expand="))) {
                        // 展开指定节点（只点三角），用于取证「六个默认关闭项」
                        const QString id = step.mid(7);
                        for (QWidget *w : findChildren<QWidget *>()) {
                            if (w->property("appMenuId").toString() == id && w->objectName() == QLatin1String("appMenuRow")) {
                                const auto chevs = w->findChildren<QAbstractButton *>();
                                for (QAbstractButton *b : chevs) {
                                    // 只点三角，别落到同一行里的胶囊开关上
                                    // （OutlineChevron 无 Q_OBJECT，metaObject 是基类的，
                                    //  只能按 objectName 认）
                                    if (b->objectName() == QLatin1String("outlineChevron")
                                        && b->isVisible()) {
                                        b->click();
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    } else if (step.startsWith(QLatin1String("focus="))) {
                        const QStringList target = step.mid(6).split(u'.');
                        if (target.first() == QLatin1String("search")) {
                            find->setFocus(Qt::ShortcutFocusReason);
                        } else if (QWidget *row = rowById(target.first())) {
                            QWidget *control = row;
                            if (target.value(1) == QLatin1String("chevron"))
                                control = row->findChild<QAbstractButton *>(QStringLiteral("outlineChevron"));
                            else if (target.value(1) == QLatin1String("switch"))
                                control = row->findChild<PillToggle *>();
                            if (control)
                                control->setFocus(Qt::ShortcutFocusReason);
                        }
                    } else if (step == QLatin1String("expandall")) {
                        // 全量节点核对：把所有折叠的三角点开
                        const auto chevs = findChildren<QAbstractButton *>();
                        for (QAbstractButton *b : chevs) {
                            if (b->objectName() == QLatin1String("outlineChevron")
                                && b->isVisible()
                                && !b->property("expanded").toBool()) {
                                b->click();
                                QApplication::processEvents();
                            }
                        }
                    } else if (step == QLatin1String("save")) {
                        m_btnOk->click();
                        QTimer::singleShot(80, qApp, &QCoreApplication::quit);
                        return;
                    } else if (step == QLatin1String("reset")) {
                        for (QPushButton *b : findChildren<QPushButton *>()) {
                            if (b->objectName() == QLatin1String("btnResetAppMenu")) {
                                b->click();
                                break;
                            }
                        }
                    } else if (step.startsWith(QLatin1String("preview="))) {
                        // 打开常驻预览的二级浮层（取证：两级菜单与浮动位置）
                        if (auto *pv = qobject_cast<AppMenuPreview *>(m_appMenuPreview)) {
                            pv->setExpanded(step.mid(8));
                            QApplication::processEvents();
                        }
                    } else if (step.startsWith(QLatin1String("wait="))) {
                        // 等动画/布局落位（开关滑块过渡 120ms）
                        QElapsedTimer t;
                        t.start();
                        const int ms = step.mid(5).toInt();
                        while (t.elapsed() < ms) {
                            QApplication::processEvents(QEventLoop::AllEvents, 10);
                        }
                    } else if (step.startsWith(QLatin1String("keys="))) {
                        const auto names = step.mid(5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        for (const QString &n : names) {
                            int key = 0;
                            if (n == QLatin1String("Down")) key = Qt::Key_Down;
                            else if (n == QLatin1String("Up")) key = Qt::Key_Up;
                            else if (n == QLatin1String("Left")) key = Qt::Key_Left;
                            else if (n == QLatin1String("Right")) key = Qt::Key_Right;
                            else if (n == QLatin1String("Enter")) key = Qt::Key_Return;
                            else if (n == QLatin1String("Escape")) key = Qt::Key_Escape;
                            else if (n == QLatin1String("Space")) key = Qt::Key_Space;
                            if (key && QApplication::focusWidget()) {
                                QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
                                QApplication::sendEvent(QApplication::focusWidget(), &ev);
                                QApplication::processEvents();
                            }
                        }
                    }
                }
                QApplication::processEvents();
            }
            // 可选：把指定节点滚到可视区（验收「应用」等屏外分组用）
            if (!scrollTo.isEmpty() && sc) {
                if (scrollTo == QLatin1String("max")) {
                    sc->verticalScrollBar()->setValue(sc->verticalScrollBar()->maximum());
                } else if (scrollTo == QLatin1String("full")) {
                    // 长图取证：整张大纲表面（不受视口裁剪），供全量节点核对
                    if (QWidget *surface = sc->widget()) {
                        QApplication::processEvents();
                        const QPixmap pm = surface->grab();
                        QImage img(pm.size(), QImage::Format_ARGB32);
                        img.fill(UiStyle::color(UiStyle::fluent::cardBg()));
                        QPainter pp(&img);
                        pp.drawPixmap(0, 0, pm);
                        pp.end();
                        img.save(out);
                        QTimer::singleShot(60, qApp, &QCoreApplication::quit);
                        return;
                    }
                } else {
                    for (QWidget *w : findChildren<QWidget *>()) {
                        if (w->property("appMenuId").toString() == scrollTo && w->objectName() == QLatin1String("appMenuRow")) {
                            const int y = w->mapTo(sc->widget(), QPoint(0, 0)).y();
                            sc->verticalScrollBar()->setValue(y);
                            break;
                        }
                    }
                }
            }
            snapshot(QStringLiteral("final"));
            QFile report(out + QStringLiteral(".txt"));
            if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
                report.write(states.join(u'\n').toUtf8());
            }
            QPixmap base = grab();
            // 二级浮层是独立顶层窗口，grab() 拍不到：按屏幕坐标叠回同一张图
            for (QWidget *tl : QApplication::topLevelWidgets()) {
                if (tl->objectName() != QLatin1String("appMenuPreviewPopup") || !tl->isVisible()) {
                    continue;
                }
                const QPoint off = tl->mapToGlobal(QPoint(0, 0)) - mapToGlobal(QPoint(0, 0));
                QPainter pp(&base);
                pp.drawPixmap(off, tl->grab());
            }
            base.save(out);
            QTimer::singleShot(80, qApp, &QCoreApplication::quit);
        });
    });
}

// ---------------------------------------------------------------------------
// 查看器右键菜单页 · 大纲视图与开关层级修订（2026-11-16）
// 设计基准：管理/QuikView-右键菜单设计-20261114/全量配置修订/大纲视图方案-开关层级修订
//
// 结构：页内查找栏 → 固定列头 → 单一大纲表面（圆角 9，内部独立纵向滚动）。
// 层级：五个根组各有总开关；三角只切设置页展开，开关只切右键菜单可见性。
// 开关层级（规格 §开关判定）：某父节点「直接子级里可独立配置的入口数」为 0 或 1
//   时子行不再放开关，只保留父组总开关（危险操作 → 删除即此例）；≥2 时父组总开关
//   加子项各自开关。枚举值 / 执行参数行恒为只展示。
// 可见性仍唯一写 config/appmenu/<id>（目录见 MenuCatalog.h）；父关子灰但保原值，
// 重新开启按原值恢复；展开状态写 ui/appmenu_expanded/<id>，与开关值分开保存。
// ---------------------------------------------------------------------------

namespace {

constexpr int kOutlineIndent = 18;
constexpr int kOutlineChevronSize = 24;
constexpr int kOutlineLeft = 4;
constexpr int kOutlineRight = 2;
constexpr int kOutlineScrollWidth = 10;
constexpr int kOutlineGap = 6;

class OutlineChevron : public QAbstractButton
{
public:
    explicit OutlineChevron(QWidget *parent) : QAbstractButton(parent)
    {
        setObjectName(QStringLiteral("outlineChevron"));
        setFixedSize(kOutlineChevronSize, kOutlineChevronSize);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setCheckable(true);
    }

    void setExpanded(bool expanded, const QString &label, bool filtering)
    {
        const QSignalBlocker blocker(this);
        setChecked(expanded);
        setProperty("expanded", expanded);
        setAccessibleName(label);
        setAccessibleDescription(filtering
            ? SettingsDialog::tr("查找期间沿匹配路径展开")
            : expanded ? SettingsDialog::tr("已展开") : SettingsDialog::tr("已收起"));
        setToolTip(accessibleDescription());
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF target = QRectF(rect()).adjusted(2, 2, -2, -2);
        if (underMouse() && isEnabled()) {
            p.setPen(Qt::NoPen);
            p.setBrush(UiStyle::fluent::hoverColor());
            p.drawRoundedRect(target, 4, 4);
        }
        if (hasFocus() && property("kbFocus").toBool()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(UiStyle::fluent::focus()), 2));
            p.drawRoundedRect(target, 4, 4);
        }
        p.translate(width() / 2.0, height() / 2.0);
        if (isChecked()) {
            p.rotate(90);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(UiStyle::fluent::descColor());
        p.drawPolygon(QPolygonF({{-2.5, -4}, {3, 0}, {-2.5, 4}}));
    }

    // The saved or filtered tree is the source of the checked state.
    void nextCheckState() override {}
};

class OutlineRowWidget : public QWidget
{
public:
    explicit OutlineRowWidget(bool root, QWidget *parent) : QWidget(parent), m_root(root)
    {
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumHeight(root ? 33 : 32);
    }

    void setMatchHit(bool hit)
    {
        m_hit = hit;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        if (m_root) {
            p.fillRect(rect(), QColor(UiStyle::fluent::outlineAlt()));
        }
        if (m_hit || underMouse()) {
            p.fillRect(rect(), m_hit ? QColor(UiStyle::fluent::searchHit())
                                     : UiStyle::fluent::hoverColor());
        }
        p.setPen(QColor(UiStyle::fluent::outlineLine()));
        p.drawLine(0, height() - 1, width(), height() - 1);
        if (hasFocus() && property("kbFocus").toBool()) {
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor(UiStyle::fluent::focus()), 2));
            p.drawRoundedRect(QRectF(rect()).adjusted(3, 2, -3, -2), 5, 5);
        }
    }

    void enterEvent(QEnterEvent *e) override { QWidget::enterEvent(e); update(); }
    void leaveEvent(QEvent *e) override { QWidget::leaveEvent(e); update(); }
    void focusInEvent(QFocusEvent *e) override { QWidget::focusInEvent(e); update(); }
    void focusOutEvent(QFocusEvent *e) override { QWidget::focusOutEvent(e); update(); }

private:
    bool m_root;
    bool m_hit = false;
};

class OutlineScroll : public QScrollArea
{
public:
    explicit OutlineScroll(QWidget *parent) : QScrollArea(parent)
    {
        setObjectName(QStringLiteral("appMenuScroll"));
        setWidgetResizable(true);
        setFrameShape(QFrame::NoFrame);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setFocusPolicy(Qt::NoFocus);
        viewport()->setAutoFillBackground(false);
    }

    QSize sizeHint() const override { return {0, 120}; }
};

struct OutlineRow {
    QString id;
    QString parentId;
    QString label;
    QString iconId;
    QString summary;
    bool group = false;
    int depth = 0;
    OutlineRowWidget *row = nullptr;
    QWidget *nameCell = nullptr;
    OutlineChevron *chev = nullptr;
    QLabel *icon = nullptr;
    QLabel *name = nullptr;
    QLabel *desc = nullptr;
    QWidget *fixedStatus = nullptr;
    PillToggle *toggle = nullptr;

    QWidget *primaryControl() const
    {
        if (toggle && toggle->isEnabled()) {
            return toggle;
        }
        if (chev && chev->isEnabled()) {
            return chev;
        }
        return row;
    }
};

struct OutlineAnchor {
    QWidget *row = nullptr;
    int offset = 0;
};

QString highlightedOutlineLabel(const QString &label, const QString &query)
{
    QString html;
    qsizetype start = 0;
    for (qsizetype match = label.indexOf(query, 0, Qt::CaseInsensitive); match >= 0;
         match = label.indexOf(query, start, Qt::CaseInsensitive)) {
        html += label.mid(start, match - start).toHtmlEscaped();
        html += QStringLiteral("<span style=\"background-color:%1;font-weight:600\">%2</span>")
                    .arg(QLatin1String(UiStyle::fluent::searchWord()),
                         label.mid(match, query.size()).toHtmlEscaped());
        start = match + query.size();
    }
    return html + label.mid(start).toHtmlEscaped();
}

struct OutlineState {
    QVector<OutlineRow> rows;
    QHash<QString, bool> expanded;
    QSet<QString> searchPath;
    QSet<QString> searchAncestors;
    QString query;
    QString priorFocusId;
    QPointer<QWidget> priorFocus;
    OutlineAnchor priorAnchor;
    QFrame *table = nullptr;
    QWidget *header = nullptr;
    OutlineScroll *scroll = nullptr;
    QLineEdit *find = nullptr;
    QLabel *count = nullptr;
    QLabel *empty = nullptr;
    QLabel *nameHead = nullptr;
    QAction *searchIcon = nullptr;
    QAction *clearSearch = nullptr;

    const OutlineRow *rowFor(const QWidget *widget) const
    {
        for (const OutlineRow &item : rows) {
            if (item.row == widget || (widget && item.row->isAncestorOf(widget))) {
                return &item;
            }
        }
        return nullptr;
    }

    bool isExpanded(const QString &id) const
    {
        return query.isEmpty() ? expanded.value(id) : searchAncestors.contains(id);
    }

    OutlineAnchor anchor() const
    {
        const int top = scroll->verticalScrollBar()->value();
        for (const OutlineRow &item : rows) {
            if (!item.row->isHidden() && item.row->y() + item.row->height() > top) {
                return {item.row, item.row->y() - top};
            }
        }
        return {};
    }

    void restoreAnchor(const OutlineAnchor &anchor) const
    {
        scroll->widget()->layout()->activate();
        if (anchor.row && !anchor.row->isHidden()) {
            scroll->verticalScrollBar()->setValue(anchor.row->y() - anchor.offset);
        }
    }

    void updateColumns(int tableWidth) const
    {
        const int nameWidth = (tableWidth - 2) * 48 / 100;
        nameHead->setFixedWidth(nameWidth - 9);
        for (const OutlineRow &item : rows) {
            item.nameCell->setFixedWidth(nameWidth - item.depth * kOutlineIndent);
        }
    }

    void refresh() const
    {
        const Config &config = Config::instance();
        for (const OutlineRow &item : rows) {
            bool visible = query.isEmpty() || searchPath.contains(item.id);
            QString disabledBy;
            for (QString parent = item.parentId; !parent.isEmpty(); parent = AppMenu::parentOf(parent)) {
                visible = visible && isExpanded(parent);
                if (disabledBy.isEmpty() && !config.appMenuOwnVisible(parent)) {
                    disabledBy = AppMenu::label(parent);
                }
            }
            const bool inactive = !disabledBy.isEmpty();
            const bool groupOff = item.group && !config.appMenuOwnVisible(item.id);
            item.desc->setText(groupOff ? SettingsDialog::tr("总开关已关闭，可展开查看子项。")
                                        : item.summary);
            const bool hit = !query.isEmpty() && item.label.contains(query, Qt::CaseInsensitive);
            item.row->setVisible(visible);
            item.row->setMatchHit(hit);
            item.name->setTextFormat(hit ? Qt::RichText : Qt::PlainText);
            item.name->setText(hit ? highlightedOutlineLabel(item.label, query) : item.label);
            item.name->setEnabled(!inactive);
            item.desc->setEnabled(!inactive);
            if (item.fixedStatus) {
                item.fixedStatus->setEnabled(!inactive);
                auto *lock = item.fixedStatus->findChild<QLabel *>(QStringLiteral("appMenuFixedLock"));
                lock->setPixmap(MenuIcons::pixmap(QStringLiteral("lock"),
                    inactive ? QColor(UiStyle::fluent::outlineDisabled())
                             : UiStyle::fluent::descColor(), 12));
            }

            const bool danger = item.id == QLatin1String("delete_file")
                                || item.id == QLatin1String("group_danger");
            const QColor iconColor = inactive ? QColor(UiStyle::fluent::outlineDisabled())
                                     : danger ? QColor(UiStyle::fluent::outlineDanger())
                                              : UiStyle::fluent::textColor();
            item.icon->setPixmap(MenuIcons::pixmap(item.iconId, iconColor, 16));
            const QString level = SettingsDialog::tr("第 %1 级").arg(item.depth + 1);
            const QString reason = inactive ? SettingsDialog::tr("「%1」已关闭").arg(disabledBy)
                                            : item.desc->text();
            item.row->setAccessibleDescription(reason.isEmpty() ? level : level + u'，' + reason);
            if (item.chev) {
                item.chev->setExpanded(isExpanded(item.id),
                    SettingsDialog::tr("%1，%2，展开或收起").arg(item.label, level), !query.isEmpty());
                item.chev->setEnabled(query.isEmpty());
            }
            if (item.toggle) {
                item.toggle->setEnabled(!inactive);
                item.toggle->setAccessibleDescription(item.row->accessibleDescription());
                item.toggle->setToolTip(inactive ? reason : QString());
            }
            item.row->setFocusPolicy(item.primaryControl() == item.row
                                         ? Qt::StrongFocus : Qt::ClickFocus);
        }
        const QMargins margins = table->layout()->contentsMargins();
        const int contentHeight = scroll->widget()->layout()->sizeHint().height();
        table->setMaximumHeight(header->height() + margins.top()
                                + contentHeight + margins.bottom());
    }
};

} // namespace

void SettingsDialog::buildExtensionsPage(QWidget *page)
{
    // 扩展页（派单 2026-11-20）：首版只有「360° 全景查看器」一个可选组件。
    // 左侧组件名与用途，右侧状态 + 当前可用操作；组件状态只认文件事实
    // （与安装向导勾选安装共用），因此两条安装路径不会各说各话。
    QWidget *sec = beginSection(page, tr("可选组件"));
    SettingRowParts row = addSettingRow(
        sec, tr("360° 全景查看器"),
        tr("把等距柱状全景图渲染成可拖动的球形视图，独立进程运行；组件缺失时普通看图不受影响。"));
    m_extRow = row.row;

    // 状态与进度竖排成一列，动作按钮单独一列：整页控件列宽度按 sizeHint 统一，
    // 横排三个控件会把文字列压成窄条（实测 503px 控件列 vs 91px 文字列）。
    auto *statusBox = new QWidget(row.row);
    statusBox->setObjectName(QStringLiteral("extStatusBox"));
    // 进度条平时隐藏，但控件列宽是建页时按 sizeHint 定死的：这里留最小宽度，
    // 保证「安装中」进度条出现时不会把文字列再挤一次（布局不跳）。
    statusBox->setMinimumWidth(100);
    auto *sv = new QVBoxLayout(statusBox);
    sv->setContentsMargins(0, 0, 0, 0);
    sv->setSpacing(4);

    m_extStatus = new QLabel(statusBox);
    m_extStatus->setObjectName(QStringLiteral("rowTag"));
    m_extStatus->setWordWrap(false);
    m_extStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    sv->addWidget(m_extStatus, 0, Qt::AlignRight);

    m_extProgress = new QProgressBar(statusBox);
    m_extProgress->setRange(0, 100);
    m_extProgress->setValue(0);
    m_extProgress->setTextVisible(false);
    m_extProgress->setFixedWidth(96);
    m_extProgress->setFixedHeight(6);
    m_extProgress->setAccessibleName(tr("组件下载进度"));
    sv->addWidget(m_extProgress, 0, Qt::AlignRight);

    row.controls->addWidget(statusBox, 0, Qt::AlignVCenter);

    m_extAction = addButton(row.row, tr("安装"));
    m_extAction->setAccessibleName(tr("360° 全景查看器：安装"));
    row.controls->addWidget(m_extAction, 0, Qt::AlignVCenter);
    connect(m_extAction, &QPushButton::clicked, this, &SettingsDialog::onExtensionsAction);

    addHint(sec, tr("首版范围为本地已拼好的 360°×180° 等距柱状 JPG："
                    "带完整 GPano 标记的图在主窗给出「360° 查看」入口，"
                    "2:1 宽图只在右键提供「尝试以 360° 查看」。"
                    "局部裁切、其它投影与相片拼接不在本版。"));

    // —— 组件信息（安装位置 / 下载源，只读）——
    // 长路径与长地址放「说明行」而不是控件列：控件列整页同宽，塞长路径会把
    // 同页其它行的文字列一起挤窄。
    QWidget *info = beginSection(page, tr("组件信息"));
    addSettingRow(info, tr("安装位置"),
                  QDir::toNativeSeparators(PanoExt::Manager::exePath()));
    m_extWhereHint = addHint(info, QString());

    QWidget *secSource = beginSection(page, tr("下载源"));
    addSettingRow(secSource, tr("组件包地址"),
                  [this]() {
                      const QString source = PanoExt::Manager::instance().sourceUrl();
                      return source.isEmpty() ? tr("未配置") : source;
                  }());
    addHint(secSource,
            tr("受控下载入口：用户不需要访问代码托管站点。安装包自带组件时无需下载；"
               "未配置地址可运行安装包勾选该组件，或在 config.ini 写入 "
               "config/ext/pano_source（支持 http(s) 与本地 file:// 测试源）。"));
    addHint(secSource,
            tr("下载包须带同址 .sha256 摘要，校验通过才落盘；取消、断网、摘要不符"
               "都保留原有可用状态。卸载只删组件自身文件，不碰主程序与设置。"));

    refreshExtensionsPage();

    // 安装/取消/卸载/失败都从组件管理器广播，页面开着时即时刷新。
    auto &mgr = PanoExt::Manager::instance();
    connect(&mgr, &PanoExt::Manager::changed, this,
            [this]() { refreshExtensionsPage(); });
    connect(&mgr, &PanoExt::Manager::progressChanged, this,
            [this](int) { refreshExtensionsPage(); });

    applyPageRhythm(page);
}

void SettingsDialog::refreshExtensionsPage()
{
    if (!m_extStatus || !m_extAction || !m_extProgress) {
        return;   // 页面还没建（懒构建）
    }
    const PanoExt::Status st = PanoExt::Manager::instance().status();
    QString stateText;
    const char *color = UiStyle::fluent::desc();
    QString actionText = tr("安装");
    bool actionEnabled = true;

    switch (st.state) {
    case PanoExt::State::Installing:
        // 还没收到第一段数据（percent=0）时说的是「连接中」，不是「安装中 0%」——
        // 后者看着像卡住了。
        stateText = st.percent > 0 ? tr("安装中 %1%").arg(st.percent)
                                   : tr("正在连接下载源");
        actionText = tr("取消");
        color = UiStyle::fluent::statusWarn();
        m_extProgress->setVisible(true);
        m_extProgress->setValue(st.percent);
        break;
    case PanoExt::State::Installed:
        stateText = st.compatible ? tr("已安装 %1").arg(st.version)
                                  : tr("已安装 %1（版本不兼容）").arg(st.version);
        actionText = tr("卸载");
        color = st.compatible ? UiStyle::fluent::statusOk() : UiStyle::fluent::statusWarn();
        m_extProgress->setVisible(false);
        break;
    case PanoExt::State::Failed:
        stateText = tr("安装失败");
        actionText = tr("重试");
        color = UiStyle::Danger;
        actionEnabled = true;
        m_extProgress->setVisible(false);
        break;
    case PanoExt::State::NotInstalled:
    default:
        stateText = tr("未安装");
        actionText = tr("安装");
        color = UiStyle::fluent::desc();
        m_extProgress->setVisible(false);
        break;
    }

    if (!m_extProgress->isVisible() && st.state == PanoExt::State::Installing)
        m_extProgress->setVisible(true);   // 兜底：安装中必须看得到进度

    m_extStatus->setText(stateText);
    m_extStatus->setStyleSheet(
        QStringLiteral("color:%1;font-size:11px;").arg(QLatin1String(color)));
    m_extStatus->setToolTip(st.state == PanoExt::State::Failed ? st.error : stateText);
    m_extAction->setText(actionText);
    m_extAction->setEnabled(actionEnabled);
    m_extLastStatusText = stateText;

    if (m_extWhereHint) {
        const QString dir = QCoreApplication::applicationDirPath();
        QString text;
        if (st.state == PanoExt::State::Failed)
            text = tr("上次操作没有完成：%1").arg(st.error);
        else if (st.state == PanoExt::State::Installed)
            text = tr("组件文件位于 %1，与安装向导勾选安装共用同一份状态；"
                      "主程序升级保留已安装的兼容组件，不会替未安装的用户安装。")
                       .arg(QDir::toNativeSeparators(dir));
        else if (st.state == PanoExt::State::Installing)
            text = tr("正在获取组件包：校验通过后才写入 %1。")
                       .arg(QDir::toNativeSeparators(dir));
        else
            text = tr("尚未安装。安装向导与这里使用同一组件版本；"
                      "安装目录需可写（%1）。")
                       .arg(QDir::toNativeSeparators(dir));
        m_extWhereHint->setText(text);
    }
}

void SettingsDialog::onExtensionsAction()
{
    auto &mgr = PanoExt::Manager::instance();
    const PanoExt::Status st = mgr.status();
    switch (st.state) {
    case PanoExt::State::Installing:
        mgr.cancelInstall();
        return;
    case PanoExt::State::Installed: {
        QString error;
        if (!mgr.uninstall(&error))
            showSettingsToast(error, false);
        else
            showSettingsToast(tr("已卸载 360° 全景查看器"), true);
        return;
    }
    case PanoExt::State::Failed:
    case PanoExt::State::NotInstalled:
    default:
        mgr.startInstall();
        refreshExtensionsPage();
        return;
    }
}

void SettingsDialog::focusExtensionsPage()
{
    // 扩展页在九页里固定是最后一页（构造期 addLazyPage 顺序即导航顺序）。
    const int idx = m_pages.size() - 1;
    selectPage(idx);
    if (m_extRow) {
        m_extRow->setFocusPolicy(Qt::StrongFocus);
        if (auto *scroll = qobject_cast<QScrollArea *>(m_pages.at(idx)->parentWidget()))
            scroll->ensureWidgetVisible(m_extRow, 0, 24);
        m_extRow->setFocus(Qt::OtherFocusReason);
    }
    if (m_extAction)
        m_extAction->setFocus(Qt::OtherFocusReason);
    if (PanoExt::Manager::instance().status().state == PanoExt::State::NotInstalled)
        showSettingsToast(tr("360° 全景查看器尚未安装：点右侧「安装」后即可用原图打开"), false);
}

void SettingsDialog::buildAppMenuSections(QWidget *page)
{
    m_appMenuPage = page;
    m_appMenuPageIndex = m_pages.indexOf(page);
    auto *pageLay = qobject_cast<QVBoxLayout *>(page->layout());
    const auto st = std::make_shared<OutlineState>();
    QSettings &settings = Config::instance().settings();

    // 2026-11-17 注释精简：分组只数个数、父组只罗列子项、叶子只换说法的一律不写说明；
    // 常驻说明只留三类——父组关闭后的影响、固定项不能关的原因、随某总开关显隐的依赖。
    auto descFor = [this](const QString &id, AppMenu::Kind kind) -> QString {
        if (kind == AppMenu::Section || kind == AppMenu::Group)
            return {};
        if (kind == AppMenu::Fixed)
            return tr("固定项不提供开关；关闭所属分组时一并隐藏。");
        if (id == QLatin1String("copy_clipboard")) {
            return tr("同时遵循已有的剪贴板入口设置。");
        }
        if (id == QLatin1String("parent_nav")) {
            return tr("关闭后仍可展开查看命令。");
        }
        if (id == QLatin1String("delete_file")) {
            return tr("随「危险操作」总开关显示。");
        }
        if (id == QLatin1String("copy_folder")) {
            return tr("执行参数，随「复制更多」显隐。");
        }
        if (id == QLatin1String("open_editor")) {
            return tr("执行参数，随「打开方式」显隐。");
        }
        return {};
    };

    auto *reset = new QPushButton(tr("恢复默认菜单"), m_pageActionHost);
    reset->setObjectName(QStringLiteral("btnResetAppMenu"));
    reset->setFixedHeight(32);
    reset->setMinimumWidth(118);
    m_pageActionHost->layout()->addWidget(reset);

    auto *findHost = new QWidget(page);
    auto *findLay = new QHBoxLayout(findHost);
    findLay->setContentsMargins(0, 0, 0, 14);
    findLay->setSpacing(16);
    st->find = new QLineEdit(findHost);
    st->find->setObjectName(QStringLiteral("appMenuFind"));
    st->find->setPlaceholderText(tr("查找菜单项…"));
    st->find->setFixedSize(242, 32);
    st->find->setAccessibleName(tr("查找菜单项"));
    st->searchIcon = st->find->addAction(QIcon(), QLineEdit::LeadingPosition);
    connect(st->searchIcon, &QAction::triggered, st->find,
            [find = st->find]() { find->setFocus(Qt::MouseFocusReason); });
    st->clearSearch = st->find->addAction(QIcon(), QLineEdit::TrailingPosition);
    st->clearSearch->setObjectName(QStringLiteral("appMenuClearSearch"));
    st->clearSearch->setText(tr("清除查找"));
    st->clearSearch->setVisible(false);
    connect(st->clearSearch, &QAction::triggered, st->find, &QLineEdit::clear);
    st->count = new QLabel(findHost);
    st->count->setObjectName(QStringLiteral("appMenuMatch"));
    findLay->addWidget(st->find);
    findLay->addWidget(st->count);
    findLay->addStretch();

    // 常驻一级预览列（实装派单 2026-11-16）：大纲表与一级预览左右并排。
    // 1440 时大纲宽由表悬停分配、预览固定 256；1068 收窄时仍保留 256；
    // 900 最小窗按「限制可用空间」压缩预览宽度（见 refreshPreviewGeometry）。
    auto *bodySplit = new QWidget(page);
    auto *bodySplitLay = new QHBoxLayout(bodySplit);
    bodySplitLay->setContentsMargins(0, 0, 0, 0);
    bodySplitLay->setSpacing(24);

    auto *leftCol = new QWidget(bodySplit);
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(0);
    leftLay->addWidget(findHost);
    bodySplitLay->addWidget(leftCol, 1);

    auto *table = new QFrame(leftCol);
    table->setObjectName(QStringLiteral("appMenuSurface"));
    st->table = table;
    auto *tableLay = new QVBoxLayout(table);
    tableLay->setContentsMargins(1, 1, 1, 1);
    tableLay->setSpacing(0);
    leftLay->addWidget(table, 1);
    leftLay->addStretch();

    auto *head = new QWidget(table);
    head->setObjectName(QStringLiteral("appMenuColumnHeader"));
    st->header = head;
    head->setFixedHeight(31);
    tableLay->addWidget(head);
    auto *headLay = new QHBoxLayout(head);
    headLay->setContentsMargins(13, 0, kOutlineRight + kOutlineScrollWidth, 1);
    headLay->setSpacing(kOutlineGap);
    st->nameHead = new QLabel(tr("菜单项"), head);
    headLay->addWidget(st->nameHead);
    headLay->addWidget(new QLabel(tr("说明 / 状态"), head), 1);
    auto *visibility = new QLabel(tr("可见性"), head);
    const int controlsWidth = qMax(72, visibility->sizeHint().width());
    visibility->setFixedWidth(controlsWidth);
    visibility->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    headLay->addWidget(visibility);

    st->scroll = new OutlineScroll(table);
    m_appMenuOutline = st->scroll;
    auto *body = new QWidget;
    auto *bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    bodyLay->setSpacing(0);
    st->scroll->setWidget(body);
    tableLay->addWidget(st->scroll, 1);

    // —— 右侧常驻一级预览列 ——
    auto *previewHost = new QWidget(bodySplit);
    auto *previewHostLay = new QVBoxLayout(previewHost);
    previewHostLay->setContentsMargins(0, 0, 0, 0);
    previewHostLay->setSpacing(6);
    {
        // 右列头（规格 §3.1 / 图 01）：标题 + 一句用法 + 菜单小标题
        auto *title = new QLabel(tr("实时预览"), previewHost);
        title->setObjectName(QStringLiteral("appMenuPreviewTitle"));
        previewHostLay->addWidget(title);
        auto *hint = new QLabel(tr("点击菜单项可定位设置。"), previewHost);
        hint->setObjectName(QStringLiteral("appMenuPreviewHint"));
        hint->setWordWrap(true);
        previewHostLay->addWidget(hint);
        // 小标题在短窗口隐藏（900/1068 的高度不足以同时容纳 15 行菜单）
        auto *captionHost = new QWidget(previewHost);
        captionHost->setObjectName(QStringLiteral("appMenuPreviewCaption"));
        auto *captionLay = new QVBoxLayout(captionHost);
        captionLay->setContentsMargins(0, 0, 0, 0);
        captionLay->setSpacing(0);
        captionLay->addSpacing(10);
        auto *caption = new QLabel(tr("查看器右键菜单"), captionHost);
        caption->setObjectName(QStringLiteral("appMenuPreviewHint"));
        captionLay->addWidget(caption);
        previewHostLay->addWidget(captionHost);
    }
    m_appMenuPreview = new AppMenuPreview(previewHost);
    m_appMenuPreview->setObjectName(QStringLiteral("appMenuPreview"));
    // 短窗口（900×660）时预览自身滚动，菜单行高与命中区不变
    auto *previewScroll = new QScrollArea(previewHost);
    previewScroll->setObjectName(QStringLiteral("appMenuPreviewScroll"));
    previewScroll->setFrameShape(QFrame::NoFrame);
    previewScroll->setWidgetResizable(true);
    previewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    previewScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    previewScroll->viewport()->setAutoFillBackground(false);
    previewScroll->setStyleSheet(
        QStringLiteral("QScrollArea#appMenuPreviewScroll{background:transparent;border:0;}"));
    previewScroll->setWidget(m_appMenuPreview);
    previewHostLay->addWidget(previewScroll, 1);
    bodySplitLay->addWidget(previewHost);

    pageLay->addWidget(bodySplit, 1);

    auto addRows = [&](auto &&walk, const QString &parentId, int depth) -> void {
        for (const AppMenu::Node *node : AppMenu::childrenOf(parentId)) {
            OutlineRow item;
            item.id = QLatin1String(node->id);
            item.parentId = parentId;
            item.label = AppMenu::label(item.id);
            item.iconId = QLatin1String(node->icon);
            item.depth = depth;
            item.group = node->kind == AppMenu::Section || node->kind == AppMenu::Group;
            item.summary = descFor(item.id, node->kind);
            item.row = new OutlineRowWidget(depth == 0, body);
            item.row->setObjectName(QStringLiteral("appMenuRow"));
            item.row->setProperty("appMenuId", item.id);
            item.row->setAccessibleName(item.label);
            auto *layout = new QHBoxLayout(item.row);
            layout->setContentsMargins(kOutlineLeft + depth * kOutlineIndent, 2, kOutlineRight, 2);
            layout->setSpacing(kOutlineGap);

            item.nameCell = new QWidget(item.row);
            auto *nameLay = new QHBoxLayout(item.nameCell);
            nameLay->setContentsMargins(0, 0, 0, 0);
            nameLay->setSpacing(4);
            if (AppMenu::childrenOf(item.id).isEmpty()) {
                nameLay->addSpacing(kOutlineChevronSize);
            } else {
                item.chev = new OutlineChevron(item.nameCell);
                nameLay->addWidget(item.chev);
                st->expanded.insert(item.id, settings.value(
                    QStringLiteral("ui/appmenu_expanded/") + item.id, depth == 0).toBool());
                connect(item.chev, &QAbstractButton::clicked, this, [st, id = item.id]() {
                    if (!st->query.isEmpty()) {
                        return;
                    }
                    const bool expanded = !st->isExpanded(id);
                    st->expanded.insert(id, expanded);
                    QSettings &settings = Config::instance().settings();
                    settings.setValue(QStringLiteral("ui/appmenu_expanded/") + id, expanded);
                    settings.sync();
                    st->refresh();
                });
            }
            item.icon = new QLabel(item.nameCell);
            item.icon->setFixedSize(16, 16);
            nameLay->addWidget(item.icon);
            item.name = new QLabel(item.label, item.nameCell);
            item.name->setObjectName(depth == 0 || node->kind == AppMenu::Group
                                         ? QStringLiteral("appMenuBranchName")
                                         : QStringLiteral("appMenuName"));
            item.name->setWordWrap(true);
            nameLay->addWidget(item.name, 1);
            layout->addWidget(item.nameCell);

            item.desc = new QLabel(item.summary, item.row);
            item.desc->setObjectName(QStringLiteral("appMenuDescription"));
            item.desc->setWordWrap(true);
            layout->addWidget(item.desc, 1);

            // Reserve the same column on every row, including fixed commands.
            auto *control = new QWidget(item.row);
            auto *controlLay = new QVBoxLayout(control);
            controlLay->setContentsMargins(0, 0, 0, 0);
            layout->addWidget(control, 0, Qt::AlignVCenter);
            if (AppMenu::ownsSwitch(*node)) {
                item.toggle = new PillToggle(control);
                item.toggle->setFluent(true);
                item.toggle->setLargeTrack(true);
                item.toggle->setProperty("appMenuId", item.id);
                item.toggle->setAccessibleName(tr("%1，可见性").arg(item.label));
                item.toggle->setChecked(Config::instance().appMenuOwnVisible(item.id));
                controlLay->addWidget(item.toggle, 0, Qt::AlignRight);
                connect(item.toggle, &PillToggle::toggled, this,
                        [this, st, id = item.id, toggle = item.toggle](bool on) {
                    QString error;
                    if (!writeValues({{QStringLiteral("appmenu/") + id, on}}, false, &error)) {
                        const QSignalBlocker blocker(toggle);
                        toggle->setChecked(Config::instance().appMenuOwnVisible(id));
                        QMessageBox::warning(this, tr("查看器右键菜单"), error);
                    }
                    st->refresh();
                    refreshAppMenuPreview(false);
                });
            } else if (item.id == QLatin1String("settings")) {
                auto *status = new QWidget(control);
                auto *statusLay = new QHBoxLayout(status);
                statusLay->setContentsMargins(0, 0, 0, 0);
                statusLay->setSpacing(4);
                auto *lock = new QLabel(status);
                lock->setObjectName(QStringLiteral("appMenuFixedLock"));
                lock->setFixedSize(12, 12);
                statusLay->addWidget(lock);
                auto *label = new QLabel(tr("随组显示"), status);
                label->setObjectName(QStringLiteral("appMenuDescription"));
                label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
                label->setWordWrap(true);
                statusLay->addWidget(label, 1);
                controlLay->addWidget(status);
                item.fixedStatus = status;
            }

            control->setFixedWidth(controlsWidth);
            bodyLay->addWidget(item.row);
            st->rows.append(item);
            walk(walk, item.id, depth + 1);
        }
    };
    addRows(addRows, QString(), 0);

    st->empty = new QLabel(tr("没有匹配的菜单项"), body);
    st->empty->setObjectName(QStringLiteral("appMenuDescription"));
    st->empty->setAlignment(Qt::AlignCenter);
    st->empty->setMinimumHeight(70);
    st->empty->hide();
    bodyLay->addWidget(st->empty);
    bodyLay->addStretch();

    m_layoutAppMenuOutline = [st](int width) { st->updateColumns(width); };
    m_saveAppMenuScroll = [st]() -> std::function<void()> {
        const OutlineAnchor anchor = st->anchor();
        return [st, anchor]() { st->restoreAnchor(anchor); };
    };
    m_refreshAppMenuStyle = [st]() {
        st->searchIcon->setIcon(QIcon(MenuIcons::pixmap(QStringLiteral("search"),
                                                       UiStyle::fluent::descColor(), 14)));
        st->clearSearch->setIcon(QIcon(MenuIcons::pixmap(QStringLiteral("x"),
                                                        UiStyle::fluent::descColor(), 12)));
        st->refresh();
    };
    m_refreshAppMenuStyle();
    // 预览 → 大纲：展开祖先、滚入视口并聚焦对应行（只定位，不执行命令）
    if (auto *preview = qobject_cast<AppMenuPreview *>(m_appMenuPreview)) {
        preview->setLocalizeOutlineFn([st](const QString &id) {
            // 临时展开祖先以带出目标行：只改内存，不写 INI，不覆盖用户的展开状态
            for (QString p = AppMenu::parentOf(id); !p.isEmpty(); p = AppMenu::parentOf(p)) {
                if (!st->expanded.value(p)) {
                    st->expanded.insert(p, true);
                }
            }
            st->refresh();
            for (const OutlineRow &item : std::as_const(st->rows)) {
                if (item.id == id) {
                    st->scroll->ensureWidgetVisible(item.row, 0, 2);
                    item.primaryControl()->setFocus(Qt::OtherFocusReason);
                    break;
                }
            }
        });
    }
    refreshAppMenuPreview();

    // Record the exact control before focus moves into the query, even for mouse entry.
    connect(qApp, &QApplication::focusChanged, page, [this, st](QWidget *old, QWidget *now) {
        if (st->query.isEmpty() && now == st->find) {
            if (const OutlineRow *row = st->rowFor(old)) {
                st->priorFocusId = row->id;
                st->priorFocus = old;
            } else {
                st->priorFocusId.clear();
                st->priorFocus.clear();
            }
        }
        if (const OutlineRow *row = st->rowFor(now); row) {
            if (keyboardInputMode()) {
                st->scroll->ensureWidgetVisible(row->row, 0, 2);
            }
            // 大纲选中 → 预览对应行轻量定位高亮（分组标题定位其首个可见入口）
            if (auto *preview = qobject_cast<AppMenuPreview *>(m_appMenuPreview)) {
                preview->highlightFromOutline(row->id);
            }
        }
    });

    connect(st->find, &QLineEdit::textChanged, this, [st](const QString &text) {
        st->clearSearch->setVisible(!text.isEmpty());
        const QString query = text.trimmed();
        if (query == st->query) {
            return;
        }
        if (st->query.isEmpty()) {
            st->priorAnchor = st->anchor();
            if (const OutlineRow *row = st->rowFor(QApplication::focusWidget())) {
                st->priorFocusId = row->id;
                st->priorFocus = QApplication::focusWidget();
            }
        }
        st->query = query;
        st->searchPath.clear();
        st->searchAncestors.clear();
        int hits = 0;
        if (!query.isEmpty()) {
            for (const OutlineRow &item : std::as_const(st->rows)) {
                if (!item.label.contains(query, Qt::CaseInsensitive)) {
                    continue;
                }
                ++hits;
                st->searchPath.insert(item.id);
                for (QString parent = item.parentId; !parent.isEmpty(); parent = AppMenu::parentOf(parent)) {
                    st->searchPath.insert(parent);
                    st->searchAncestors.insert(parent);
                }
            }
        }
        st->count->setText(query.isEmpty() ? QString() : tr("%1 项匹配").arg(hits));
        st->empty->setVisible(!query.isEmpty() && hits == 0);
        st->refresh();
        if (!query.isEmpty()) {
            st->scroll->verticalScrollBar()->setValue(0);
            return;
        }

        const QString priorId = std::exchange(st->priorFocusId, QString());
        const QPointer<QWidget> prior = std::exchange(st->priorFocus, {});
        const OutlineAnchor anchor = st->priorAnchor;
        // Expanding the original tree changes the scroll range after this input event.
        QTimer::singleShot(0, st->scroll, [st, priorId, prior, anchor]() {
            if (!st->query.isEmpty()) {
                return;
            }
            for (const OutlineRow &item : std::as_const(st->rows)) {
                if (item.id == priorId && item.row->isVisible()) {
                    QWidget *target = prior && prior->isEnabled() ? prior.data() : item.primaryControl();
                    target->setFocus(Qt::OtherFocusReason);
                    break;
                }
            }
            st->restoreAnchor(anchor);
        });
    });

    connect(reset, &QPushButton::clicked, this, [this, st]() {
        if (!Config::instance().resetAppMenuVisibility()) {
            QMessageBox::warning(this, tr("查看器右键菜单"), tr("无法恢复默认菜单，配置未写入。"));
            return;
        }
        for (const OutlineRow &item : std::as_const(st->rows)) {
            if (item.toggle) {
                const QSignalBlocker blocker(item.toggle);
                item.toggle->setChecked(AppMenu::defaultOn(item.id));
            }
        }
        st->refresh();
        refreshAppMenuPreview(false);
    });

    struct KeyRelay : QObject {
        std::function<bool(QWidget *, QKeyEvent *)> handle;
        bool eventFilter(QObject *object, QEvent *event) override
        {
            return event->type() == QEvent::KeyPress
                   && handle(static_cast<QWidget *>(object), static_cast<QKeyEvent *>(event));
        }
    };
    auto *relay = new KeyRelay;
    relay->setParent(page);
    relay->handle = [st](QWidget *widget, QKeyEvent *event) {
        if (event->key() == Qt::Key_Escape && !st->find->text().isEmpty()) {
            st->find->clear();
            return true;
        }

        QVector<const OutlineRow *> visible;
        for (const OutlineRow &item : std::as_const(st->rows)) {
            if (item.row->isVisible()) {
                visible.append(&item);
            }
        }
        const auto focusRow = [st, &visible](qsizetype index) {
            if (index >= 0 && index < visible.size()) {
                const OutlineRow *item = visible.at(index);
                st->scroll->ensureWidgetVisible(item->row, 0, 2);
                item->primaryControl()->setFocus(Qt::OtherFocusReason);
            }
        };
        if (widget == st->find) {
            if (event->key() == Qt::Key_Down && !visible.isEmpty()) {
                focusRow(0);
                return true;
            }
            return false;
        }
        const OutlineRow *current = st->rowFor(widget);
        const qsizetype index = visible.indexOf(current);
        if (index < 0) {
            return false;
        }
        switch (event->key()) {
        case Qt::Key_Up:
            focusRow(index - 1);
            return true;
        case Qt::Key_Down:
            focusRow(index + 1);
            return true;
        case Qt::Key_Right:
            if (current->chev && current->chev->isEnabled() && !st->isExpanded(current->id)) {
                current->chev->click();
            } else if (index + 1 < visible.size() && visible.at(index + 1)->parentId == current->id) {
                focusRow(index + 1);
            }
            return true;
        case Qt::Key_Left:
            if (current->chev && current->chev->isEnabled() && st->isExpanded(current->id)) {
                current->chev->click();
            } else {
                for (qsizetype parent = index - 1; parent >= 0; --parent) {
                    if (visible.at(parent)->id == current->parentId) {
                        focusRow(parent);
                        break;
                    }
                }
            }
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            // Display-only rows consume Enter as well, so it cannot save the dialog.
            if (!event->isAutoRepeat()) {
                QWidget *target = widget == current->row ? current->primaryControl() : widget;
                if (auto *button = qobject_cast<QAbstractButton *>(target); button && button->isEnabled()) {
                    button->click();
                }
            }
            return true;
        default:
            return false;
        }
    };
    st->find->installEventFilter(relay);
    for (const OutlineRow &item : std::as_const(st->rows)) {
        item.row->installEventFilter(relay);
        if (item.chev) {
            item.chev->installEventFilter(relay);
        }
        if (item.toggle) {
            item.toggle->installEventFilter(relay);
        }
    }
}

void SettingsDialog::setAppMenuRunState(const AppMenuRunState &state)
{
    m_appMenuRunState = state;
    refreshAppMenuPreview();
}

// 由 Config::appMenuVisible（生效可见性 = 本项 × 各级祖先总开关）生成两级模型，
// 顺序与 MainWindow::buildContextMenu 一致：五段之间插分段线，空段整段省略。
void SettingsDialog::probeVisitAllPages()
{
    // 延后构建的验收探针：按导航顺序逐页切一次，日志里每页一条
    // "visit<下标>,<毫秒>"（含构建 + 回填 + 布局），供外部脚本算首次打开耗时。
    const QString shotDir = qEnvironmentVariable("QUIKVIEW_SETTINGS_PAGES_DIR");
    for (int i = 0; i < m_pages.size(); ++i) {
        QElapsedTimer t;
        t.start();
        selectPage(i);
        probeMark((QByteArrayLiteral("visit") + QByteArray::number(i)).constData(),
                  t.nsecsElapsed() / 1000);
        if (!shotDir.isEmpty()) {
            // 自渲染取证（QWidget::grab，不经屏幕合成）：每页一张整窗图。
            grab().save(shotDir + QStringLiteral("/page%1.png").arg(i));
        }
    }
}

void SettingsDialog::probeSearch(const QString &text)
{
    // 走真实链路：setText → textChanged → ensureAllPagesBuilt + filterNavigation。
    // 首次搜索是「一次性补建其余页 + 建索引」的最坏路径，这里记一段耗时供回归对比。
    if (m_search) {
        QElapsedTimer t;
        t.start();
        m_search->setText(text);
        probeMark("search-first", t.nsecsElapsed() / 1000);
    }
}

void SettingsDialog::refreshAppMenuPreview(bool relayout)
{
    Q_UNUSED(relayout);
    auto *preview = qobject_cast<AppMenuPreview *>(m_appMenuPreview);
    if (!preview) {
        return;
    }
    const Config &cfg = Config::instance();
    const AppMenuRunState &s = m_appMenuRunState;

    auto checkedOf = [&s](const QString &id) {
        return s.checked.value(id, false);
    };
    auto enabledOf = [&s](const QString &id) {
        return s.enabled.value(id, true);
    };
    // 无图时与真实菜单一致：依赖当前图片的命令禁用（预览照常显示，供对照）
    auto needsImage = [](const QString &id) {
        static const QSet<QString> kImageCmds = {
            QStringLiteral("nav_next"), QStringLiteral("nav_prev"), QStringLiteral("nav_first"),
            QStringLiteral("nav_last"), QStringLiteral("nav_next10"), QStringLiteral("nav_prev10"),
            QStringLiteral("nav_rand_back"), QStringLiteral("nav_rand_fwd"),
            QStringLiteral("nav_folder_next"), QStringLiteral("nav_folder_prev"),
            QStringLiteral("parent_nav"), QStringLiteral("parent_sort"), QStringLiteral("parent_view"),
            QStringLiteral("parent_zoom"), QStringLiteral("zoom_in"), QStringLiteral("zoom_out"),
            QStringLiteral("zoom_100"), QStringLiteral("zoom_in1"), QStringLiteral("zoom_out1"),
            QStringLiteral("parent_interp"), QStringLiteral("parent_fx"), QStringLiteral("parent_rotate"),
            QStringLiteral("parent_transition"), QStringLiteral("image_info"),
            QStringLiteral("copy_clipboard"), QStringLiteral("copy_folder"),
            QStringLiteral("copy_exif"), QStringLiteral("copy_path"),
            QStringLiteral("copy_visible"), QStringLiteral("copy_file"),
            QStringLiteral("open_editor"), QStringLiteral("delete_file"),
            QStringLiteral("wallpaper"), QStringLiteral("print"),
        };
        return kImageCmds.contains(id);
    };

    // 运行态：排序 / 浏览方式 / 插值 / 过渡 / 旋转为互斥单选，其余为勾选
    auto markOf = [&](const QString &id, bool *radio) {
        *radio = false;
        const QString view = s.currentViewMode;
        if (id == QLatin1String("sort_name") || id == QLatin1String("sort_time")
            || id == QLatin1String("sort_size") || id == QLatin1String("sort_type")) {
            *radio = true;
            const int idx = id == QLatin1String("sort_name") ? 0
                          : id == QLatin1String("sort_time") ? 1
                          : id == QLatin1String("sort_size") ? 2 : 3;
            return s.sortMode >= 0 && s.sortMode == idx;
        }
        if (id == QLatin1String("view_actual")) { *radio = true; return view == QLatin1String("actual"); }
        if (id == QLatin1String("view_fit")) { *radio = true; return view == QLatin1String("fit"); }
        if (id == QLatin1String("view_fitwidth")) { *radio = true; return view == QLatin1String("fitwidth"); }
        if (id == QLatin1String("view_two_l2r")) { *radio = true; return view == QLatin1String("spread_l2r"); }
        if (id == QLatin1String("view_two_r2l")) { *radio = true; return view == QLatin1String("spread_r2l"); }
        if (id == QLatin1String("interp_bilinear")) { *radio = true; return s.currentInterp == QLatin1String("fast"); }
        if (id == QLatin1String("interp_bicubic")) { *radio = true; return s.currentInterp == QLatin1String("smooth"); }
        if (id == QLatin1String("interp_lanczos")) { *radio = true; return s.currentInterp == QLatin1String("lanczos"); }
        if (id == QLatin1String("fx_none")) { *radio = true; return s.currentEffect == QLatin1String("none"); }
        if (id == QLatin1String("fx_soften")) { *radio = true; return s.currentEffect == QLatin1String("soften"); }
        if (id == QLatin1String("fx_sharpen")) { *radio = true; return s.currentEffect == QLatin1String("sharpen"); }
        if (id == QLatin1String("rot_none")) { *radio = true; return s.netRotation == 0 && !s.flippedH && !s.flippedV; }
        if (id == QLatin1String("rot_left")) { *radio = true; return s.netRotation == 90; }
        if (id == QLatin1String("rot_right")) { *radio = true; return s.netRotation == 270; }
        if (id == QLatin1String("rot_180")) { *radio = true; return s.netRotation == 180; }
        if (id == QLatin1String("sort_desc")) return s.sortDescending;
        if (id == QLatin1String("always_on_top")) return s.alwaysOnTop;
        if (id == QLatin1String("zoom_lock")) return s.zoomLocked;
        if (id == QLatin1String("fx_negative")) return s.negative;
        if (id == QLatin1String("fx_flipv")) return s.flippedV;
        if (id == QLatin1String("fx_fliph")) return s.flippedH;
        return checkedOf(id);
    };

    auto makeRow = [&](const AppMenu::Node &n, bool group) {
        AppMenuPreview::Row r;
        r.id = QLatin1String(n.id);
        r.label = AppMenu::label(r.id);
        r.icon = QLatin1String(n.icon);
        r.shortcut = AppMenu::shortcut(r.id);
        r.isGroup = group;
        r.danger = r.id == QLatin1String("delete_file");
        bool radio = false;
        const bool on = markOf(r.id, &radio);
        r.checked = on && !radio;
        r.radioOn = on && radio;
        // 互斥组里非当前项画空圈（排序 / 浏览方式 / 插值 / 过渡 / 旋转）
        r.radioOff = radio && !on;
        r.enabled = enabledOf(r.id)
                    && (s.hasImage || !needsImage(r.id));
        return r;
    };
    auto visible = [&cfg](const AppMenu::Node &n) {
        return AppMenu::canConfigure(n)
                   ? cfg.appMenuVisible(QLatin1String(n.id))
                   : cfg.appMenuVisible(AppMenu::parentOf(QLatin1String(n.id)));
    };

    QVector<AppMenuPreview::Row> level1;
    QHash<QString, QVector<AppMenuPreview::Row>> children;
    bool hasContent = false;      // 已有可见入口（决定要不要画分段线）
    bool segmentStarted = true;   // 当前分段是否已经开始

    auto beginSegment = [&]() {
        if (hasContent && !segmentStarted) {
            AppMenuPreview::Row sep;
            sep.id = QStringLiteral("__sep__");
            sep.separator = true;
            sep.enabled = false;
            level1.append(sep);
        }
        segmentStarted = true;
    };
    auto addLeaf = [&](const AppMenu::Node &n) {
        if (!visible(n)) {
            return;
        }
        beginSegment();
        level1.append(makeRow(n, false));
        hasContent = true;
    };
    auto addMenu = [&](const AppMenu::Node &n) {
        if (!visible(n)) {
            return;
        }
        QVector<AppMenuPreview::Row> kids;
        for (const AppMenu::Node *c : AppMenu::childrenOf(QLatin1String(n.id))) {
            if (!visible(*c)) {
                continue;
            }
            if (c->kind == AppMenu::Parent) {
                for (const AppMenu::Node *gc : AppMenu::childrenOf(QLatin1String(c->id))) {
                    if (visible(*gc)) {
                        kids.append(makeRow(*gc, false));
                    }
                }
            } else {
                kids.append(makeRow(*c, false));
            }
        }
        if (kids.isEmpty()) {
            return;
        }
        beginSegment();
        const AppMenuPreview::Row row = makeRow(n, true);
        level1.append(row);
        children.insert(row.id, kids);
        hasContent = true;
    };

    for (const AppMenu::Node *root : AppMenu::childrenOf(QString())) {
        if (!cfg.appMenuOwnVisible(QLatin1String(root->id))) {
            continue;
        }
        segmentStarted = false;   // 新的一段：首个可见入口前插分段线
        for (const AppMenu::Node *n : AppMenu::childrenOf(QLatin1String(root->id))) {
            if (n->kind == AppMenu::Parent) {
                addMenu(*n);
            } else if (n->kind == AppMenu::Group) {
                if (QLatin1String(n->id) == QLatin1String("group_file")) {
                    addMenu(*n);   // 文件操作：一级一个入口，二级四命令直达
                } else {
                    for (const AppMenu::Node *p : AppMenu::childrenOf(QLatin1String(n->id))) {
                        if (p->kind == AppMenu::Parent) {
                            addMenu(*p);
                        } else {
                            addLeaf(*p);
                        }
                    }
                }
            } else {
                addLeaf(*n);
            }
        }
    }

    preview->setModel(level1, children, preview->expandedId());
}

void SettingsDialog::buildWindowsPage(QWidget *page)
{
    // 九页重组（2026-11-21）：Windows 默认应用 + 资源管理器右键菜单（注册状态 /
    // 开关 / 缩略图）+ 可展开的支持格式。本页只处理「Windows 侧的集成」。
    // 查看器自己的右键菜单在「查看器右键菜单」页，两套菜单不混。

    // —— Windows 默认应用（原「格式关联」页）——
    QWidget *secWin = beginSection(page, tr("Windows 默认应用"));
    auto *btnDefault = addButton(nullptr, tr("打开默认应用…"));
    auto partsDefault = addSettingRow(secWin, tr("在 Windows 中设置文件关联"),
                                      tr("打开系统设置，再按扩展名选择默认应用"));
    partsDefault.controls->addWidget(btnDefault, 0, Qt::AlignVCenter);
    connect(btnDefault, &QPushButton::clicked, this, [this]() {
        const HINSTANCE r = ShellExecuteW(nullptr, L"open",
                                          L"ms-settings:defaultapps?registeredAppMachine=QuikView",
                                          nullptr, nullptr, SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(r) <= 32) {
            QMessageBox::warning(this, tr("格式关联"),
                                 tr("无法打开 Windows 默认应用，请从 Windows 设置手动打开。"));
        }
    });
    addHint(secWin, tr("默认打开方式由 Windows 管理，QuikView 不能直接改写；"
                       "找不到 QuikView 时先运行安装包注册。"));

    // —— 资源管理器右键菜单（注册状态 / 开关 / 缩略图三段）——
    // 原页首的「资源管理器集成」空壳分组已删：它只留标题、没有任何行。
    // 上下文菜单派单（2026-11-13）：注册探测四态 + 把设置页接到真实右键行为。
    // 偏好写 config/ctxmenu/*（与壳扩展读同一份按用户 config.ini），下次右键
    // 弹出时生效；写入失败显示原因并恢复实际值。
    auto addStatusRow = [this](QWidget *section, const QString &label, const QString &visible,
                               const QString &state, const char *color, const QString &tip) {
        SettingRowParts parts = addSettingRow(section, label, visible);
        auto *value = new QLabel(state, parts.row);
        value->setObjectName(QStringLiteral("rowTag"));
        value->setWordWrap(false);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                 .arg(QLatin1String(color)));
        const QString hover = tip.isEmpty() ? (visible.isEmpty() ? state : visible) : tip;
        value->setToolTip(hover);
        parts.row->setToolTip(hover);
        parts.controls->addWidget(value, 0, Qt::AlignVCenter);
    };

    // —— 注册探测：右键命令（64+32 位）与 WebP/PSD/PSB 缩略图分开 ——
    const ShellComponent menu = probeShellMenu();
    const ShellComponent thumbWebp = probeShellThumb(QStringLiteral(".webp"));
    const ShellComponent thumbPsd = probeShellThumb(QStringLiteral(".psd"));
    const ShellComponent thumbPsb = probeShellThumb(QStringLiteral(".psb"));

    auto stateView = [](ShellRegState s, bool partial32) -> QPair<QString, const char *> {
        switch (s) {
        case ShellRegistered:
            return { partial32 ? QCoreApplication::translate("SettingsDialog", "已注册（仅 64 位，32 位视图未绑定）")
                               : QCoreApplication::translate("SettingsDialog", "当前安装已注册"),
                     partial32 ? UiStyle::fluent::statusWarn() : UiStyle::fluent::statusOk() };
        case ShellOtherInstall:
            return { QCoreApplication::translate("SettingsDialog", "指向其他安装"),
                     UiStyle::fluent::statusWarn() };
        case ShellNotRegistered:
            return { QCoreApplication::translate("SettingsDialog", "未注册"),
                     UiStyle::fluent::desc() };
        case ShellNotInstalled:
        default:
            return { QCoreApplication::translate("SettingsDialog", "未安装"),
                     UiStyle::fluent::desc() };
        }
    };

    QWidget *sec = beginSection(page, tr("注册状态"));
    {
        const bool partial64 = menu.state == ShellRegistered
                               && menu.boundPath == QStringLiteral("64");
        auto view = stateView(menu.state, partial64);
        addStatusRow(sec, tr("右键菜单处理器"),
                     menu.state == ShellOtherInstall
                         ? tr("当前指向：%1").arg(menu.boundPath)
                         : QString(),
                     view.first, view.second,
                     tr("64/32 位扩展的 CLSID 与处理器绑定"));
    }
    {
        auto view = stateView(thumbWebp.state, false);
        addStatusRow(sec, tr("WebP 缩略图"),
                     thumbWebp.state == ShellOtherInstall
                         ? tr("当前指向：%1").arg(thumbWebp.boundPath)
                         : QString(),
                     view.first, view.second, QString());
    }
    {
        auto view = stateView(thumbPsd.state, false);
        addStatusRow(sec, tr("PSD 缩略图"),
                     thumbPsd.state == ShellOtherInstall
                         ? tr("当前指向：%1").arg(thumbPsd.boundPath)
                         : QString(),
                     view.first, view.second,
                     tr("缺少内嵌 JPEG 时回退默认图标"));
    }
    {
        auto view = stateView(thumbPsb.state, false);
        addStatusRow(sec, tr("PSB 缩略图"),
                     thumbPsb.state == ShellOtherInstall
                         ? tr("当前指向：%1").arg(thumbPsb.boundPath)
                         : QString(),
                     view.first, view.second,
                     tr("缺少内嵌 JPEG 时回退默认图标"));
    }

    // —— 开关区：仅当右键菜单处理器注册到本安装时可操作 ——
    const bool menuUsable = menu.state == ShellRegistered;
    sec = beginSection(page, tr("右键菜单开关"));
    struct CtxToggleRow {
        SettingsDialog::ToggleRow row;
        quikcfg::CtxLeaf leaf;
    };
    auto ctxToggle = [this](QWidget *section, const QString &label,
                            const QString &desc, quikcfg::CtxLeaf leaf) -> CtxToggleRow {
        SettingsDialog::ToggleRow row =
            addToggleRow(section, label, desc, label);
        if (row.toggle) {
            // 键名与壳扩展共用 shared/quik_config_keys.h（唯一来源），
            // 这边拼 "config/ctxmenu/<leaf>"，壳扩展读 [config] ctxmenu\<leaf>。
            const QString key =
                QString::fromWCharArray(quikcfg::CtxSettingsKey(leaf).c_str());
            row.toggle->setChecked(
                Config::instance().settings().value(key, true).toBool());
            // 验收探针锚点：smoke settingsctx:<leaf> 按这个名字点开关
            row.toggle->setAccessibleName(
                QStringLiteral("ctxmenu.")
                + QString::fromWCharArray(quikcfg::CtxLeafName(leaf)));
            connect(row.toggle, &PillToggle::toggled, this,
                    [this, row, key, leaf](bool on) mutable {
                QString err;
                // writeValues() 自己拼 "config/" 前缀，所以这里只给相对键
                // ctxmenu/<leaf>；拼出来的位置必须等于壳扩展读的
                // [config] ctxmenu\<leaf>，否则开关写到一个没人读的键上。
                const QList<QPair<QString, QVariant>> vals = {
                    { QString::fromWCharArray(
                          quikcfg::CtxSettingsSubKey(leaf).c_str()), on }
                };
                if (writeValues(vals, false, &err))
                    return;
                // 写入失败：显示原因并恢复到实际值（内存已由 writeValues 回滚）
                const bool actual =
                    Config::instance().settings().value(key, true).toBool();
                if (row.toggle) {
                    QSignalBlocker blocker(row.toggle);
                    row.toggle->setChecked(actual);
                }
                QMessageBox::warning(this, tr("上下文菜单"),
                                     tr("写入失败：%1\n已恢复到当前实际值。").arg(err));
            });
        }
        return { row, leaf };
    };

    const CtxToggleRow masterRow = ctxToggle(
        sec, tr("在资源管理器右键菜单中显示 QuikView"),
        tr("关闭后不再插入菜单项；各细项的选择会保留"),
        quikcfg::CtxUseShellMenu);
    const CtxToggleRow browseRow = ctxToggle(
        sec, tr("用 QuikView 浏览图片"), QString(),
        quikcfg::CtxShowBrowse);
    if (browseRow.row.row)
        browseRow.row.row->setToolTip(tr("命令行：quikview.exe \"文件\""));
    const CtxToggleRow convertRow = ctxToggle(
        sec, tr("用 QuikView 转换图片"), QString(),
        quikcfg::CtxShowConvert);
    if (convertRow.row.row)
        convertRow.row.row->setToolTip(tr("命令行：quikview.exe --batch \"文件\""));
    const CtxToggleRow previewRow = ctxToggle(
        sec, tr("菜单内预览"), QString(),
        quikcfg::CtxShowPreview);
    if (previewRow.row.row)
        previewRow.row.row->setToolTip(
            tr("右键菜单顶部显示 ≤256px 缩略图，解码超时自动省略"));
    const CtxToggleRow infoRow = ctxToggle(
        sec, tr("图片信息"), QString(),
        quikcfg::CtxShowInfo);
    const CtxToggleRow iconRow = ctxToggle(
        sec, tr("软件图标"), QString(),
        quikcfg::CtxShowIcon);

    // 联动与整行灰显：注册态不可用 → 整行灰显；总开关关闭 → 细项行灰显
    // （勾选状态保留，恢复后各自生效）
    const auto applyUsable = [menuUsable, masterRow, browseRow, convertRow,
                              previewRow, infoRow, iconRow]() {
        const bool masterOn = masterRow.row.toggle && masterRow.row.toggle->isChecked();
        const bool subUsable = menuUsable && masterOn;
        auto applyRow = [](const CtxToggleRow &r, bool usable) {
            if (!r.row.toggle)
                return;
            r.row.toggle->setEnabled(usable);
            r.row.row->setEnabled(true);   // 命中过滤器自身判 isEnabled，行不禁用
            // QSS 的 rowLabel 颜色会盖掉禁用调色板：灰显必须显式写样式
            const QString grey = QStringLiteral(
                "color:%1;").arg(QLatin1String(UiStyle::fluent::disabled()));
            if (r.row.label)
                r.row.label->setStyleSheet(usable ? QString() : grey);
            if (r.row.desc)
                r.row.desc->setStyleSheet(usable ? QString() : grey);
        };
        applyRow(masterRow, menuUsable);
        applyRow(browseRow, subUsable);
        applyRow(convertRow, subUsable);
        applyRow(previewRow, subUsable);
        applyRow(infoRow, subUsable);
        applyRow(iconRow, subUsable);
    };
    applyUsable();
    if (masterRow.row.toggle) {
        connect(masterRow.row.toggle, &PillToggle::toggled, this,
                [applyUsable]() { applyUsable(); });
    }

    // 不可操作的原因说明（诚实化：灰显必须配原因）
    if (menu.state == ShellOtherInstall) {
        addHint(sec, tr("本机的右键菜单处理器指向另一处安装（%1）。"
                        "运行安装包覆盖安装到该目录，或卸载该安装后重装，"
                        "才能在这里控制右键菜单。")
                    .arg(menu.boundPath));
    } else if (!shellExtDllPresent()) {
        addHint(sec, tr("未检测到壳扩展文件（quikthumb-*.dll）。"
                        "运行安装包可修复；便携版不带安装器注册，右键菜单不可用。"));
    } else if (!menuUsable) {
        addHint(sec, tr("壳扩展文件存在，但右键菜单处理器未注册到本安装。"
                        "运行安装包覆盖安装即可完成注册。"));
    }
    addHint(sec, tr("改动即时写入，下次右键时生效，无需重装或管理员权限；"
                    "菜单未变化时重启资源管理器。"));

    // —— 资源管理器缩略图状态（按实际注册探测，不再写死）——
    sec = beginSection(page, tr("资源管理器缩略图"));
    {
        auto view = stateView(thumbWebp.state, false);
        addStatusRow(sec, tr("WebP 缩略图"), QString(), view.first, view.second, QString());
        auto psdView = stateView(thumbPsd.state, false);
        addStatusRow(sec, tr("PSD 缩略图"), QString(), psdView.first, psdView.second,
                     tr("缺少内嵌 JPEG 时回退默认图标"));
        auto psbView = stateView(thumbPsb.state, false);
        addStatusRow(sec, tr("PSB 缩略图"), QString(), psbView.first, psbView.second,
                     tr("缺少内嵌 JPEG 时回退默认图标"));
    }
    addHint(sec, tr("这些入口由壳扩展在资源管理器中提供，安装／卸载安装包时"
                    "自动注册／移除；与文件默认打开方式无关。"));
    addHint(sec, tr("资源管理器独立预览窗格为后续交付项：旧 shell/ 有实现，"
                    "尚未接入当前安装包。"));

    // —— 支持的格式（默认收起；显式按钮展开/收起，不点标题不误收起）——
    sec = beginSection(page, tr("支持的格式"));
    {
        SettingRowParts parts = addSettingRow(
            sec, tr("全部支持格式"),
            tr("按扩展名分组，列表只读；新增格式随程序版本更新"));
        m_formatsToggleBtn = addButton(nullptr, tr("展开"));
        m_formatsToggleBtn->setAccessibleName(tr("展开支持的格式"));
        parts.controls->addWidget(m_formatsToggleBtn, 0, Qt::AlignVCenter);
        connect(m_formatsToggleBtn, &QPushButton::clicked, this, [this]() {
            if (!m_formatsFoldBody)
                return;
            const bool show = !m_formatsFoldBody->isVisible();
            m_formatsFoldBody->setVisible(show);
            m_formatsToggleBtn->setText(show ? tr("收起") : tr("展开"));
        });
    }
    // 折叠体单独一张卡：addSettingRow 需要 section 的 setGroup 卡片，
    // 这里用一个独立 section 承载，整段显示/隐藏，不嵌在上一张卡里。
    m_formatsFoldBody = beginSection(page, QString());
    for (const DirectoryLoader::Group &g : DirectoryLoader::imageGroups()) {
        QStringList exts;
        exts.reserve(g.exts.size());
        for (const QString &e : g.exts)
            exts.append(e.toUpper());
        addSettingRow(m_formatsFoldBody, g.label, exts.join(QStringLiteral(" · ")));
    }
    addHint(m_formatsFoldBody, tr("列表只读；新增格式随程序版本更新。"));
    m_formatsFoldBody->hide();

    applyPageRhythm(page);
}

// ===========================================================================
// 9. 杂项
// ===========================================================================


// ===========================================================================
// 9.1 设置导出 / 导入（需求池 H1-3）
// ===========================================================================

// 当前生效的那份 ini：QUIKVIEW_CONFIG_PATH → 程序目录 config.ini → 系统配置目录，
// 判定顺序与 Config::defaultConfigPath 完全一致，这里只读 QSettings 已解析出的
// 实际文件名，天然跟随便携模式与冒烟隔离路径，绝不写死 %APPDATA%。
QString SettingsDialog::settingsIniPath() const
{
    return QFileInfo(Config::instance().settings().fileName()).absoluteFilePath();
}

void SettingsDialog::showSettingsToast(const QString &text, bool ok)
{
    if (!m_toast)
        return;
    m_toast->showMessage(text, ok ? Chrome::Toast::Tone::Success
                                  : Chrome::Toast::Tone::Warning);
}

bool SettingsDialog::isValidSettingsFile(const QString &path, QString *error) const
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("无法读取文件"));
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    // ① QSettings 能解析（手改坏的 ini 会在 status 上暴露）：空文件也拒绝。
    {
        QSettings probe(path, QSettings::IniFormat);
        if (probe.status() != QSettings::NoError)
            return fail(QStringLiteral("QSettings 解析失败"));
        if (probe.allKeys().isEmpty())
            return fail(QStringLiteral("没有可读键"));
    }

    // ② 必须含 [config] 段（QuikView 的业务键全在这一段下）。
    bool hasConfigSection = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (line.trimmed().compare(QStringLiteral("[config]"), Qt::CaseInsensitive) == 0) {
            hasConfigSection = true;
            break;
        }
    }
    if (!hasConfigSection)
        return fail(QStringLiteral("缺少 [config] 段"));

    // ③ 至少 5 个本项目已知键：防止把别的软件的 ini 当设置吃进来。
    //    只做「键名出现过」的粗判——这里的目的不是逐键校值，而是来源识别。
    static const char *const kKnownKeys[] = {
        "two_page_cover", "arrow_keys_scroll", "image_editor", "photo_folder_1",
        "photo_folder_2", "click_side_pageturn", "canvas_bg", "interpolation",
        "edge_action", "default_zoom", "show_side_arrow", "fit_window_to_image",
        "disable_screensaver", "pageturn_wav", "raw_thumbnail", "language",
        "theme", "cache_size_mb", "blur_material", "slideshow_interval_ms",
    };
    int hits = 0;
    for (const char *key : kKnownKeys) {
        if (text.contains(QString::fromLatin1(key)))
            ++hits;
    }
    if (hits < 5)
        return fail(QStringLiteral("已知键不足 5 个（命中 %1）").arg(hits));
    return true;
}

bool SettingsDialog::exportSettingsTo(const QString &path, QString *error)
{
    // 导出前先把内存里的即时项落盘，导出的才是「界面上看到的当前配置」。
    Config::instance().settings().sync();
    const QString src = settingsIniPath();
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = in.readAll();
    in.close();

    // QSaveFile：先写临时文件再原子替换，导出到已存在的旧文件也不会写一半。
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = out.errorString();
        return false;
    }
    if (out.write(data) != data.size() || !out.commit()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    return true;
}

bool SettingsDialog::applyImportedSettings(const QString &path, QString *error)
{
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        if (error)
            *error = in.errorString();
        return false;
    }
    const QByteArray data = in.readAll();
    in.close();

    const QString dest = settingsIniPath();
    // 备份：导入前的原配置整份留存，导入失败/回滚都有据可查。
    if (QFileInfo::exists(dest)) {
        const QString bak = dest + QStringLiteral(".bak-")
                            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddHHmm"));
        QFile::remove(bak);
        if (!QFile::copy(dest, bak)) {
            if (error)
                *error = QStringLiteral("备份原配置失败");
            return false;
        }
    }
    // 覆盖写入同样走 QSaveFile：失败时原文件仍在，不会出现半个 ini。
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error)
            *error = out.errorString();
        return false;
    }
    if (out.write(data) != data.size() || !out.commit()) {
        if (error)
            *error = out.errorString();
        return false;
    }
    // 让运行中的 Config 立刻改为读新文件：不重启也能回读正确；
    // 真正「生效」仍需重启（本单明确不做立即生效）。
    Config::instance().reopenSettings();
    return true;
}

void SettingsDialog::exportSettingsPath(const QString &path)
{
    QString err;
    if (!exportSettingsTo(path, &err)) {
        showSettingsToast(setioTr("导出失败"), false);
        return;
    }
    showSettingsToast(setioTr("设置已导出"), true);
}

bool SettingsDialog::importSettingsPath(const QString &path)
{
    // 先把队列里的即时项写完，再动文件：否则导入后旧内存值会被后续 sync 带回去。
    flushPendingTheme();
    QString err;
    if (!flushPendingWrites(&err)) {
        showSettingsToast(setioTr("导入失败，原配置未改动"), false);
        return false;
    }
    if (!isValidSettingsFile(path, &err)) {
        showSettingsToast(setioTr("不是有效的 QuikView 设置文件"), false);
        return false;
    }
    if (!applyImportedSettings(path, &err)) {
        showSettingsToast(setioTr("导入失败，原配置未改动"), false);
        return false;
    }
    // 界面回填成导入后的值，并把待保存基线重置——避免关闭设置时把旧值又写回去。
    loadFromConfig();
    capturePendingBaseline(0);
    updatePendingNote();
    showSettingsToast(setioTr("导入成功，重启 QuikView 后生效"), true);
    return true;
}

void SettingsDialog::onExportSettings()
{
    const QString defName = QStringLiteral("quikview-settings-%1.ini")
                                .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
    const QString defDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString path = QFileDialog::getSaveFileName(
        this, setioTr("导出设置"), defDir.isEmpty() ? defName : defDir + QLatin1Char('/') + defName,
        setioTr("QuikView 设置 (*.ini)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".ini"), Qt::CaseInsensitive))
        path += QStringLiteral(".ini");
    exportSettingsPath(path);
}

void SettingsDialog::onImportSettings()
{
    QString path = QFileDialog::getOpenFileName(
        this, setioTr("导入设置"), QFileInfo(settingsIniPath()).absolutePath(),
        setioTr("QuikView 设置 (*.ini);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    importSettingsPath(path);
}

void SettingsDialog::probeSettingsIO(const QString &steps)
{
    QFile log(QStringLiteral("Temp/setio-probe.txt"));
    if (!log.open(QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&log);
    const QStringList parts = steps.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    for (const QString &rawPart : parts) {
        const QString step = rawPart.trimmed();
        QString result;
        if (step == QLatin1String("page")) {
            // 按文案定位「通用」页签再点击：页面索引会随新增页漂移，写死数字不可靠。
            // 九页重组后「设置备份」在「通用」页末尾。
            for (QAbstractButton *b : m_tabs) {
                if (b && b->text().contains(setioTr("通用"))) {
                    b->click();
                    break;
                }
            }
            result = QStringLiteral("ok=1 ini=%1 portable=%2")
                         .arg(settingsIniPath())
                         .arg(Config::instance().isPortable() ? 1 : 0);
        } else if (step == QLatin1String("bottom")) {
            // 滚到当前页底部：通用页末尾才是「设置备份」，不滚下去截图看不全两个按钮。
            // 当前页本身就是一个 QScrollArea（页内容挂在它的 widget() 上）。
            if (m_stack) {
                if (auto *sa = qobject_cast<QScrollArea *>(m_stack->currentWidget()))
                    sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
            }
            result = QStringLiteral("ok=1");
        } else if (step.startsWith(QLatin1String("export|"))) {
            const QString path = step.mid(7);
            QString err;
            const bool ok = exportSettingsTo(path, &err);
            showSettingsToast(ok ? setioTr("设置已导出") : setioTr("导出失败"), ok);
            result = QStringLiteral("ok=%1 path=%2 err=%3").arg(ok ? 1 : 0).arg(path, err);
        } else if (step.startsWith(QLatin1String("import|"))) {
            const QString path = step.mid(7);
            const bool ok = importSettingsPath(path);
            result = QStringLiteral("ok=%1 path=%2").arg(ok ? 1 : 0).arg(path);
        } else if (step.startsWith(QLatin1String("grab|"))) {
            const bool ok = grab().save(step.mid(5));
            result = QStringLiteral("ok=%1 out=%2").arg(ok ? 1 : 0).arg(step.mid(5));
        } else {
            result = QStringLiteral("ok=0 err=未知步骤");
        }
        ts << "step=" << step << ' ' << result << '\n';
        ts.flush();
    }
}

// ===========================================================================
// 10. 语言

// ===========================================================================
// 配置读写
// ===========================================================================

void SettingsDialog::loadFromConfig()
{
    loadBindingsFromConfig(0, 0, 0, 0);
    loadPageWidgets(nullptr);
    // 批次03：记住进入设置时的四项状态，取消 / Esc / ✕ 时回到这里。
    captureBrowseLiveBaseline();
}

void SettingsDialog::loadBindingsFromConfig(int chkFrom, int comboFrom, int keyFrom,
                                            int mouseFrom)
{
    m_loading = true;
    for (int i = qMax(0, chkFrom); i < m_chkBinds.size(); ++i) {
        const ChkBind &b = m_chkBinds.at(i);
        const bool v = bGet(b.key, b.def);
        b.cb->setChecked(b.inverted ? !v : v);
    }
    for (int i = qMax(0, comboFrom); i < m_comboBinds.size(); ++i) {
        const ComboBind &b = m_comboBinds.at(i);
        const int idx = b.combo->findData(sGet(b.key, b.def));
        b.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    // 翻页批次 2（2026-11-20）：「询问」已实现并启用，旧配置 edge_action="ask"
    // 直接按原值显示与生效，不再折算为「停止」（批次 1 的临时折算已移除）。
    for (int i = qMax(0, keyFrom); i < m_keyRows.size(); ++i) {
        const KeyRow &r = m_keyRows.at(i);
        // 出厂无单键的动作（幻灯片）默认不启用，避免「勾着但热键为空」；
        // 已有配置仍以用户落盘值为准（P2 / D4）。
        const bool enabledByDefault = !r.defHotkey.isEmpty();
        r.cb->setChecked(bGet(QStringLiteral("keyboard/%1_enabled").arg(r.action).toLatin1().constData(),
                              enabledByDefault));
        const QString key = sGet(QStringLiteral("keyboard/%1_key").arg(r.action).toLatin1().constData(),
                                 r.defHotkey);
        r.edit->setKeySequence(QKeySequence::fromString(key, QKeySequence::PortableText));
        // 「操作」列只读：不再回读 keyboard/<action>_action（历史上被改绑过的配置
        // 会在下次保存时按固定动作覆写回去）。
    }
    for (int i = qMax(0, mouseFrom); i < m_mouseRows.size(); ++i) {
        const MouseRow &r = m_mouseRows.at(i);
        const QString stored = sGet(r.key.toLatin1().constData(), r.def);
        int idx = r.combo->findData(stored);
        // 旧配置里的不可执行档位（灰显项）按运行态安全值「无操作」显示：
        // 右键 / X / Y 的 pan 在 Config 读取时折算回内置行为；滚轮与中键的
        // 旧共用开关语义已由 Config 构造迁移按有效行为折算到独立映射键。
        // 配置原值不回写。
        if (idx >= 0) {
            if (auto *model = qobject_cast<QStandardItemModel *>(r.combo->model())) {
                if (auto *item = model->item(idx); item && !item->isEnabled())
                    idx = r.combo->findData(QStringLiteral("none"));
            }
        }
        r.combo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    m_loading = false;
}

void SettingsDialog::loadPageWidgets(QWidget *page)
{
    // 页专属控件不进绑定表；page 非空时只回填这一页拥有的那几个。
    const auto owns = [page](QWidget *w) {
        return w && (!page || page == w || page->isAncestorOf(w));
    };
    m_loading = true;
    if (owns(m_cacheSize))
        m_cacheSize->setValue(iGet("imageproc/cache_size_mb", 300));
    if (owns(m_maxBookmarks)) {
        const int idx = m_maxBookmarks->findData(iGet("bookmark/max_bookmarks", 50));
        m_maxBookmarks->setCurrentIndex(idx >= 0 ? idx : 3);
    }
    if (owns(m_photoFolder1))
        m_photoFolder1->setText(sGet("etc/photo_folder_1"));
    if (owns(m_photoFolder2))
        m_photoFolder2->setText(sGet("etc/photo_folder_2"));
    if (owns(m_editorPath))
        m_editorPath->setText(sGet("etc/image_editor"));
    if (owns(m_language)) {
        const int idx = m_language->findData(sGet("language", QStringLiteral("system")));
        m_language->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    // 外观页两件套不进绑定表，单独回读（历史 theme 三值 → 跟随 + 浅色两开关）。
    if (owns(m_themeCombo))
        syncThemeControls(sGet("theme", QStringLiteral("dark")));
    if (owns(m_blurMaterial))
        m_blurMaterial->setChecked(bGet("ui/blur_material", false));
    if (owns(m_gifColorBtn))
        syncGifColorRow();   // 色样与可点态按回填后的档位/颜色刷新
    m_loading = false;
}

// GIF 自定义色样（翻页批次 2）：色样恒取 Config 现值；仅「自定义颜色」档
// 可点，其余档灰显并说明原因，不做假按钮。
void SettingsDialog::syncGifColorRow()
{
    if (!m_gifColorBtn || !m_gifBgCombo)
        return;
    const QColor c = Config::instance().gifCustomColor();
    m_gifColorBtn->setStyleSheet(QStringLiteral(
        "QPushButton{background:%1;border:1px solid rgba(128,128,128,0.65);"
        "border-radius:6px;}")
        .arg(c.name()));
    const bool custom = m_gifBgCombo->currentData().toString() == QLatin1String("4");
    m_gifColorBtn->setEnabled(custom);
    m_gifColorBtn->setToolTip(custom
        ? tr("点击选择 GIF 透明区域的铺底颜色")
        : tr("先在上方「GIF 透明背景」选择「自定义颜色」"));
}

// ---------------------------------------------------------------------------
// 即时写盘（普通项默认路径）+ 待保存项（热键 / 语言）
// ---------------------------------------------------------------------------

void SettingsDialog::refreshSettingsInstanceIfError()
{
    QSettings &s = Config::instance().settings();
    if (s.status() == QSettings::NoError)
        return;
    // QSettings::status() 会被首个错误粘住：重建实例并重放当前内存键，
    // 让「留在设置页稍后重试」拿到干净状态；同时不丢主窗口刚写入的键。
    QHash<QString, QVariant> snapshot;
    for (const QString &k : s.allKeys())
        snapshot.insert(k, s.value(k));
    Config::instance().reopenSettings();
    QSettings &retry = Config::instance().settings();
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it)
        retry.setValue(it.key(), it.value());
}

bool SettingsDialog::writeValues(const QList<QPair<QString, QVariant>> &values, bool retryable,
                                 QString *error)
{
    refreshSettingsInstanceIfError();
    QSettings &s = Config::instance().settings();

    // 记录旧值：草稿写失败时要按旧值回滚内存，不能把用户放弃的键留在
    // QSettings 里（否则「不保存」路径的 sync 会把草稿刷到磁盘）。
    QHash<QString, QVariant> previous;
    QSet<QString> previousPresent;
    for (const auto &kv : values) {
        const QString full = QStringLiteral("config/") + kv.first;
        if (s.contains(full))
            previousPresent.insert(full);
        previous.insert(full, s.value(full));
        s.setValue(full, kv.second);
        if (retryable)
            m_pendingWrites.insert(kv.first, kv.second);
    }
    s.sync();
    if (s.status() == QSettings::NoError) {
        if (retryable) {
            for (const auto &kv : values)
                m_pendingWrites.remove(kv.first);
        }
        return true;
    }

    if (!retryable) {
        for (const auto &kv : values) {
            const QString full = QStringLiteral("config/") + kv.first;
            if (previousPresent.contains(full))
                s.setValue(full, previous.value(full));
            else
                s.remove(full);
        }
    }
    if (error)
        *error = tr("配置文件写入失败（%1）")
                     .arg(QString::number(static_cast<int>(s.status())));
    return false;
}

bool SettingsDialog::commitValues(const QList<QPair<QString, QVariant>> &values, QString *error)
{
    // 热键改绑 / 语言草稿：失败即回滚内存，草稿只存在于界面上。
    return writeValues(values, false, error);
}

bool SettingsDialog::flushPendingWrites(QString *error)
{
    if (m_pendingWrites.isEmpty()) {
        if (!m_forceSync)
            return true;
        refreshSettingsInstanceIfError();
        QSettings &s = Config::instance().settings();
        s.sync();
        if (s.status() != QSettings::NoError) {
            if (error)
                *error = tr("配置文件写入失败（%1）")
                             .arg(QString::number(static_cast<int>(s.status())));
            return false;
        }
        m_forceSync = false;
        return true;
    }
    QList<QPair<QString, QVariant>> values;
    for (auto it = m_pendingWrites.cbegin(); it != m_pendingWrites.cend(); ++it)
        values.append({ it.key(), it.value() });
    if (!writeValues(values, true, error))
        return false;
    m_forceSync = false;
    return true;
}

void SettingsDialog::warnSaveFailure(const QString &error)
{
    QMessageBox::warning(this, tr("设置"),
                         tr("保存失败：%1。\n"
                            "改动仍保留在界面上，可稍后重试。")
                             .arg(error));
}

bool SettingsDialog::applyInstantValue(const QString &key, const QVariant &value)
{
    if (m_loading)
        return true;
    // 运行态由设置页在可下发项上即时调用 MainWindow::applyLiveSetting；
    // 本函数只保证「不点保存也落盘」，并与关闭提交共用同一个写盘入口。
    return writeValues({{ key, value }}, true, nullptr);
}

void SettingsDialog::writeBridgeKeys()
{
    // —— 兼容桥：运行逻辑沿用的旧 key 由新设置项推导 ——
    // 三档必须一一对应；Config::interpolation() 会把 fast→bilinear、
    // smooth→bicubic 翻译成画布认识的模式，lanczos 原样透传。
    // 只有推导值与现有配置不同才写，未变更键不重写。
    QSettings &s = Config::instance().settings();
    const auto writeIfChanged = [&s](const QString &key, const QVariant &want) {
        if (!s.value(QStringLiteral("config/") + key).isValid()
            || s.value(QStringLiteral("config/") + key) != want)
            return QPair<QString, QVariant>(key, want);
        return QPair<QString, QVariant>();
    };

    const QString interp = sGet("imageproc/interpolate", QStringLiteral("0"));
    const QPair<QString, QVariant> interpPair = writeIfChanged(
        QStringLiteral("interpolation"),
        interp == QStringLiteral("0") ? QStringLiteral("fast")
            : interp == QStringLiteral("1") ? QStringLiteral("smooth")
                                           : QStringLiteral("lanczos"));
    // 兼容桥（翻页批次 2 更新）：旧开关键仅供降级回退旧版本时读取，运行态
    // 已改读四个独立映射键。wheel_zoom 按「两方向都在缩放位」推导，
    // middle_pan 按「中键拖动=pan」推导，与旧版语义一致。
    const QPair<QString, QVariant> wheelPair = writeIfChanged(
        QStringLiteral("wheel_zoom"),
        sGet("mouse/wheel_up", QStringLiteral("zoom_in")) == QStringLiteral("zoom_in")
            && sGet("mouse/wheel_down", QStringLiteral("zoom_out")) == QStringLiteral("zoom_out"));
    const QPair<QString, QVariant> middlePair = writeIfChanged(
        QStringLiteral("middle_pan"),
        sGet("mouse/btn_middle_drag", QStringLiteral("pan")) == QStringLiteral("pan"));

    QList<QPair<QString, QVariant>> changes;
    for (const QPair<QString, QVariant> &p : { interpPair, wheelPair, middlePair }) {
        if (!p.first.isEmpty())
            changes.append(p);
    }
    if (!changes.isEmpty())
        writeValues(changes, true, nullptr);   // 兼容桥随即时项一起重试，不丢推导值
}

void SettingsDialog::connectInstantSignals(int chkFrom, int comboFrom, int keyFrom,
                                           int mouseFrom)
{
    const auto isLive = [this](QCheckBox *cb) {
        for (const LiveCheck &item : m_liveChecks) {
            if (item.cb == cb)
                return true;
        }
        return false;
    };
    for (int i = qMax(0, chkFrom); i < m_chkBinds.size(); ++i) {
        const ChkBind &b = m_chkBinds.at(i);
        if (isLive(b.cb))
            continue;   // 即时预览项由 applyLiveCheck 统一下发 + 写盘
        connect(b.cb, &QCheckBox::toggled, this, [this, b](bool on) {
            applyInstantValue(QString::fromLatin1(b.key),
                              b.inverted ? !on : on);
            updatePendingNote();
        });
    }
    for (int i = qMax(0, comboFrom); i < m_comboBinds.size(); ++i) {
        const ComboBind &b = m_comboBinds.at(i);
        connect(b.combo, &QComboBox::currentIndexChanged, this, [this, b]() {
            applyInstantValue(QString::fromLatin1(b.key),
                              b.combo->currentData().toString());
            writeBridgeKeys();
        });
    }
    for (int i = qMax(0, mouseFrom); i < m_mouseRows.size(); ++i) {
        const MouseRow &r = m_mouseRows.at(i);
        auto *combo = r.combo;
        const QString key = r.key;
        connect(combo, &QComboBox::currentIndexChanged, this, [this, key, combo]() {
            applyInstantValue(key, combo->currentData().toString());
            writeBridgeKeys();
        });
    }
    // 热键改绑 / 语言只更新待保存计数，不写盘；「启用」是即时项：
    // 先写配置，再在切换点让 MainWindow 重装热键表（不能等对话框关闭）。
    for (int i = qMax(0, keyFrom); i < m_keyRows.size(); ++i) {
        const KeyRow &r = m_keyRows.at(i);
        const QString action = r.action;
        QCheckBox *cb = r.cb;
        connect(cb, &QCheckBox::toggled, this, [this, action, cb](bool on) {
            if (m_loading)
                return;
            const QString key = QStringLiteral("keyboard/%1_enabled").arg(action);
            applyInstantValue(key, on);
            // 运行态即时生效：失败时控件回退，避免「看是关、实际还能按」。
            if (MainWindow *host = qobject_cast<MainWindow *>(parentWidget())) {
                QString error;
                if (!host->applyLiveSetting(key, on, &error)) {
                    {
                        const QSignalBlocker b(cb);
                        cb->setChecked(!on);
                    }
                    applyInstantValue(key, !on);
                    QMessageBox::warning(this, tr("设置"),
                                         tr("「%1」未能即时生效：%2\n已恢复原值。")
                                             .arg(cb->text(), error));
                }
            }
        });
        connect(r.edit, &QKeySequenceEdit::keySequenceChanged, this, [this]() {
            updatePendingNote();
        });
    }
}

void SettingsDialog::connectPageWidgets(QWidget *page)
{
    // 页专属控件不进绑定表：在该页首次构建时连一次，只连这一页拥有的。
    const auto owns = [page](QWidget *w) {
        return w && (!page || page == w || page->isAncestorOf(w));
    };
    if (owns(m_cacheSize)) {
        connect(m_cacheSize, &QSpinBox::valueChanged, this, [this](int v) {
            applyInstantValue(QStringLiteral("imageproc/cache_size_mb"), v);
        });
    }
    if (owns(m_maxBookmarks)) {
        connect(m_maxBookmarks, &QComboBox::currentIndexChanged, this, [this]() {
            applyInstantValue(QStringLiteral("bookmark/max_bookmarks"),
                              m_maxBookmarks->currentData().toInt());
        });
    }
    if (owns(m_photoFolder1)) {
        connect(m_photoFolder1, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/photo_folder_1"), m_photoFolder1->text());
        });
    }
    if (owns(m_photoFolder2)) {
        connect(m_photoFolder2, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/photo_folder_2"), m_photoFolder2->text());
        });
    }
    if (owns(m_editorPath)) {
        connect(m_editorPath, &QLineEdit::editingFinished, this, [this]() {
            applyInstantValue(QStringLiteral("etc/image_editor"), m_editorPath->text());
        });
    }
    if (owns(m_blurMaterial)) {
        connect(m_blurMaterial, &QCheckBox::toggled, this, [this](bool on) {
            if (m_loading)
                return;
            applyInstantValue(QStringLiteral("ui/blur_material"), on);
            UiStyle::setBlurMaterial(on);
            refreshDialogStyle();
        });
    }
    if (owns(m_language)) {
        connect(m_language, &QComboBox::currentIndexChanged, this, [this]() {
            updatePendingNote();
        });
    }
    if (owns(m_gifColorBtn)) {
        syncGifColorRow();
        connect(m_gifBgCombo, &QComboBox::currentIndexChanged, this, [this]() {
            syncGifColorRow();
        });
        connect(m_gifColorBtn, &QPushButton::clicked, this, [this]() {
            const QColor c = QColorDialog::getColor(Config::instance().gifCustomColor(),
                                                    this, tr("选择 GIF 背景色"));
            if (!c.isValid())
                return;   // 取消保持原色，不写盘
            applyInstantValue(QStringLiteral("imageproc/gif_custom_color"), c.name());
            syncGifColorRow();
        });
    }
}

void SettingsDialog::capturePendingBaseline(int keyFrom)
{
    if (keyFrom <= 0)
        m_keyBaseline.clear();
    for (int i = qMax(0, keyFrom); i < m_keyRows.size(); ++i) {
        const KeyRow &r = m_keyRows.at(i);
        m_keyBaseline.insert(r.action,
                             r.edit->keySequence().toString(QKeySequence::PortableText));
    }
    if (m_language)
        m_langBaseline = m_language->currentData().toString();
}

QStringList SettingsDialog::pendingChangeNames() const
{
    QStringList names;
    for (const KeyRow &r : m_keyRows) {
        const QString now = r.edit->keySequence().toString(QKeySequence::PortableText);
        if (now != m_keyBaseline.value(r.action))
            names << tr("热键：%1").arg(r.label);
    }
    if (m_language && m_language->currentData().toString() != m_langBaseline)
        names << tr("界面语言");
    return names;
}

void SettingsDialog::updatePendingNote()
{
    if (!m_footerNote)
        return;
    const QStringList names = pendingChangeNames();
    if (names.isEmpty()) {
        // 2026-11-17：没有待保存项时页脚保持空，不再常驻「即时生效」提示。
        m_footerNote->clear();
        m_footerNote->hide();
        return;
    }
    m_footerNote->setText(tr("有 %1 项待保存（%2）；关闭时会询问")
                              .arg(names.size())
                              .arg(names.join(QStringLiteral("、"))));
    m_footerNote->show();
}

// ---------------------------------------------------------------------------
// 侧栏搜索：按设置名 / 分组名 / 选项文字匹配，命中页签保留并跳转
// ---------------------------------------------------------------------------

void SettingsDialog::buildSearchIndex()
{
    m_pageSearchIndex.clear();
    m_pageSearchIndex.resize(m_pages.size());
    for (int i = 0; i < m_pages.size(); ++i)
        rebuildPageSearchIndex(i);
}

void SettingsDialog::rebuildPageSearchIndex(int index)
{
    if (index < 0 || index >= m_pages.size())
        return;
    if (m_pageSearchIndex.size() < m_pages.size())
        m_pageSearchIndex.resize(m_pages.size());
    {
        QWidget *page = m_pages.at(index);
        QStringList parts;
        for (QLabel *lab : page->findChildren<QLabel *>()) {
            const QString name = lab->objectName();
            if (name == QLatin1String("rowLabel")
                || name == QLatin1String("rowDesc")
                || name == QLatin1String("sectionTitle")
                || name == QLatin1String("pageTitle")
                || name == QLatin1String("pageDesc")
                || name == QLatin1String("pageHint")) {
                parts << lab->text();
            }
        }
        for (QComboBox *combo : page->findChildren<QComboBox *>()) {
            for (int i = 0; i < combo->count(); ++i)
                parts << combo->itemText(i);
        }
        for (QPushButton *btn : page->findChildren<QPushButton *>())
            parts << btn->text();
        // 2026-11-17 注释精简：说明搬进 tooltip 后关键词仍要被搜到，所以把设置行
        // 及其子控件的 tooltip 一并入索引（页脚 setNote 本就不在索引内）。
        for (QWidget *row : page->findChildren<QWidget *>(QStringLiteral("settingRow"))) {
            parts << row->toolTip();
            for (QWidget *w : row->findChildren<QWidget *>())
                parts << w->toolTip();
        }
        m_pageSearchIndex[index] = parts.join(QStringLiteral(" ")).toLower();
    }
}

void SettingsDialog::selectFirstVisiblePage()
{
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (!m_tabs[i]->isHidden()) {
            selectPage(i);
            return;
        }
    }
}

void SettingsDialog::filterNavigation(const QString &text)
{
    const QString term = text.trimmed().toLower();
    bool any = false;
    for (int i = 0; i < m_tabs.size(); ++i) {
        const bool hit = term.isEmpty() || m_pageSearchIndex.value(i).contains(term);
        m_tabs[i]->setVisible(hit);
        any = any || hit;
    }
    if (m_searchEmpty)
        m_searchEmpty->setVisible(!term.isEmpty() && !any);
    // 当前页被过滤掉时跳到第一个命中页，避免右侧停在「无匹配」的旧页。
    if (!term.isEmpty() && any) {
        const int cur = m_stack ? m_stack->currentIndex() : 0;
        if (cur >= 0 && cur < m_tabs.size() && m_tabs[cur]->isHidden())
            selectFirstVisiblePage();
    }
}

// ---------------------------------------------------------------------------
// SET-02 即时预览会话
// ---------------------------------------------------------------------------

void SettingsDialog::connectLiveChecks(int from)
{
    for (int i = qMax(0, from); i < m_liveChecks.size(); ++i) {
        LiveCheck &item = m_liveChecks[i];
        item.lastValue = item.inverted ? !item.cb->isChecked() : item.cb->isChecked();
        connect(item.cb, &QCheckBox::toggled, this, [this, i](bool on) {
            applyLiveCheck(i, on);
        });
    }
}

void SettingsDialog::applyLiveCheck(int index, bool checked)
{
    if (m_loading || index < 0 || index >= m_liveChecks.size())
        return;
    LiveCheck &item = m_liveChecks[index];
    const bool want = item.inverted ? !checked : checked;
    if (want == item.lastValue)   // 反复切换/程序化回设：幂等，不重复下发或写盘
        return;

    const QString key = QString::fromLatin1(item.key);
    MainWindow *host = qobject_cast<MainWindow *>(parentWidget());
    QString error;
    if (!host)
        error = tr("未找到主窗口，无法下发运行态。");
    if (!host || !host->applyLiveSetting(key, want, &error)) {
        // 运行态没变 → 控件必须退回上一次成功值，避免 UI 与实际状态分叉；
        // 配置未写入，不会留下半套状态。
        const bool previousChecked = item.inverted ? !item.lastValue : item.lastValue;
        {
            const QSignalBlocker b(item.cb);
            item.cb->setChecked(previousChecked);
        }
        if (host)
            host->applyLiveSetting(key, item.lastValue, nullptr);   // 兜底：运行态回到上次成功值
        // PillToggle 的可见文字在内部标签而非 QAbstractButton::text()，用
        // accessibleName（addSwitchRow 里设为同一 label）才拿得到项名。
        const QString itemName = item.cb->accessibleName().isEmpty()
                                     ? item.cb->text() : item.cb->accessibleName();
        QMessageBox::warning(this, tr("设置"),
                             tr("「%1」未能即时生效：%2\n已恢复原值，未写入配置。")
                                 .arg(itemName, error));
        return;
    }

    // 即时生效 + 立刻写盘：关闭窗口（含「不保存」）也保留该值。
    item.lastValue = want;
    applyInstantValue(key, want);
    updateBrowseLiveNote(key, want);
}

// ---------------------------------------------------------------------------
// 批次03：浏览页四项（保存缩放状态 / 文件名浮层 / 动画帧号 / 悬浮提示）
// 进入设置时快照，取消（含 Esc、✕）回滚；保存才保留。
// ---------------------------------------------------------------------------

void SettingsDialog::updateBrowseLiveNote(const QString &key, bool value)
{
    if (!m_browseLiveNote)
        return;
    // 只在浏览页这四项（以及同页的即时项）上说话；其它页不显示这行。
    QString itemName;
    for (const LiveCheck &item : m_liveChecks) {
        if (key == QString::fromLatin1(item.key)) {
            itemName = item.cb->accessibleName().isEmpty() ? item.cb->text()
                                                          : item.cb->accessibleName();
            break;
        }
    }
    if (itemName.isEmpty())
        return;
    m_browseLiveNote->setText(
        tr("「%1」已即时生效：%2 · 无需保存；按「取消」/ Esc 会回到进入设置时的状态。")
            .arg(itemName, value ? tr("开") : tr("关")));
    m_browseLiveNote->show();
}

void SettingsDialog::captureBrowseLiveBaseline()
{
    // 取值一律走 Config getter：默认值（缩放记忆 false / 浮层 false / 动画帧号 true /
    // 悬浮提示 true）只在 getter 里维护一份，不在这里再抄一遍，避免回滚写错口径。
    // 只认配置值，不依赖浏览页是否已构建（该页是延迟构建页）。
    Config &cfg = Config::instance();
    m_browseLiveBaseline.clear();
    m_browseLiveBaseline.insert(QStringLiteral("view/save_zoom_state"), cfg.saveZoomState());
    m_browseLiveBaseline.insert(QStringLiteral("view/overlay_filename"), cfg.overlayFilename());
    m_browseLiveBaseline.insert(QStringLiteral("view/show_anim_control"), cfg.showAnimControl());
    m_browseLiveBaseline.insert(QStringLiteral("view/show_tooltip"), cfg.showTooltip());
}

void SettingsDialog::rollbackBrowseLiveBaseline()
{
    if (m_browseLiveBaseline.isEmpty())
        return;
    MainWindow *host = qobject_cast<MainWindow *>(parentWidget());
    Config &cfg = Config::instance();
    // 「当前值」必须走 getter（带默认值）：ini 里没这个键时 QSettings 返回无效值，
    // 直接 toBool() 会得到 false，把「默认 true 且没写过」误判成改动、反而写一次盘。
    const auto currentOf = [&cfg](const QString &key) {
        if (key == QLatin1String("view/save_zoom_state"))
            return cfg.saveZoomState();
        if (key == QLatin1String("view/overlay_filename"))
            return cfg.overlayFilename();
        if (key == QLatin1String("view/show_anim_control"))
            return cfg.showAnimControl();
        if (key == QLatin1String("view/show_tooltip"))
            return cfg.showTooltip();
        return false;
    };
    QList<QPair<QString, QVariant>> revert;
    bool anyChanged = false;
    if (host)
        host->setLivePreviewQuiet(true);   // 回滚不应在画布上弹预览
    for (auto it = m_browseLiveBaseline.cbegin(); it != m_browseLiveBaseline.cend(); ++it) {
        const QString key = it.key();
        const bool base = it.value();
        if (currentOf(key) == base)
            continue;
        anyChanged = true;
        revert.append({ key, base });
        // 运行态与配置同源：配置先回，运行态跟着回到基线（applyLiveSetting 只读
        // 运行时状态、不写盘，两边不会二次分叉）。
        if (host)
            host->applyLiveSetting(key, base, nullptr);
        // 页已构建过才需要同步控件与幂等值（未构建的页下次构建时按配置读取）。
        for (LiveCheck &item : m_liveChecks) {
            if (key != QString::fromLatin1(item.key))
                continue;
            item.lastValue = base;
            const QSignalBlocker b(item.cb);
            item.cb->setChecked(item.inverted ? !base : base);
            break;
        }
    }
    if (host)
        host->setLivePreviewQuiet(false);
    if (!revert.isEmpty())
        commitValues(revert);   // 配置回到进入设置时的值（不弹窗、不整表重写）
    if (m_browseLiveNote) {
        m_browseLiveNote->setText(
            anyChanged ? tr("已放弃本次改动：浏览页即时项已回到进入设置时的状态。")
                       : tr("已关闭设置，未保留即时改动。"));
        m_browseLiveNote->show();
    }
}

bool SettingsDialog::onAccepted()
{
    // —— 键盘冲突检测先于一切写盘：同一键位被两个启用的动作占用（或撞上
    // 固定动作、同一动作被重复指派）→ 高亮冲突行、提示占用者、整次保存拒绝 ——
    clearKeyConflicts();
    const QString conflictMsg = keyboardConflicts();
    if (!conflictMsg.isEmpty()) {
        // 不写盘、不关闭：用户的改动留在界面上，改完可再次保存成功（P0-1）。
        QMessageBox::warning(this, tr("设置"), conflictMsg);
        return false;
    }

    // 排队中的主题切换先落运行态（写盘经 applyInstantValue 同一入口）。
    flushPendingTheme();

    // —— 只提交实际变更的键：普通即时项在变更点已经落盘，这里不再整表重写。
    // 待保存项只有热键改绑与界面语言；「启用」是即时项，不在这里重复写。 ——
    QList<QPair<QString, QVariant>> changes;
    QSettings &s = Config::instance().settings();
    for (const KeyRow &r : m_keyRows) {
        // _key 存 PortableText（"Ctrl+P"、"Right"…），MainWindow 按
        // QKeySequence::fromString(…, PortableText) 读回。
        const QString now = r.edit->keySequence().toString(QKeySequence::PortableText);
        if (now != m_keyBaseline.value(r.action)) {
            changes.append({ QStringLiteral("keyboard/%1_key").arg(r.action), now });
        }
        // 动作固定；仅当历史改绑残留的旧值与目标动作不一致时才清理一次。
        const QString actionKey = QStringLiteral("keyboard/%1_action").arg(r.action);
        const QVariant storedAction = s.value(QStringLiteral("config/") + actionKey);
        if (storedAction.isValid() && storedAction.toString() != r.action)
            changes.append({ actionKey, r.action });
    }
    if (m_language) {
        const QString now = m_language->currentData().toString();
        if (now != m_langBaseline)
            changes.append({ QStringLiteral("language"), now });
    }

    QString error;
    // 先重试此前 sync 失败的即时项：失败则不动草稿，避免「草稿已落盘、窗口还开着」。
    if (!flushPendingWrites(&error)) {
        warnSaveFailure(error);
        return false;
    }
    // 只提交实际变更的键：普通即时项在变更点已经落盘，这里不再整表重写。
    if (!changes.isEmpty() && !commitValues(changes, &error)) {
        warnSaveFailure(error);
        return false;
    }
    return true;
}

void SettingsDialog::onReset()
{
    const int r = QMessageBox::question(this, tr("设置"), tr("是否要初始化设置？"),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (r != QMessageBox::Yes)
        return;
    // 恢复默认要覆盖所有页：延后构建的页尚未建控件，先补齐再回填，
    // 否则「重置后进某个没打开过的页，还是旧值」。
    ensureAllPagesBuilt();
    QSettings &s = Config::instance().settings();
    s.beginGroup(QStringLiteral("config"));
    s.remove(QStringLiteral(""));
    s.endGroup();
    m_forceSync = true;   // 直接改内存的批量变更，由关闭/后续写盘统一 sync
    loadFromConfig();

    // 恢复默认同样是「即时项立即生效」：配置已清成默认值，运行态按默认值重放；
    // 热键 / 语言若与进入设置时不同，进入待保存计数（关闭时会询问）。
    for (int i = 0; i < m_liveChecks.size(); ++i)
        applyLiveCheck(i, m_liveChecks[i].cb->isChecked());
    applyThemePreview();
    if (m_blurMaterial) {
        UiStyle::setBlurMaterial(m_blurMaterial->isChecked());
        refreshDialogStyle();
    }
    updatePendingNote();

    // 「背景」段的两个颜色钮已随第 18 期下线；其余直接读写 config 的控件
    // 均为绑定表/专项控件，loadFromConfig() 已覆盖。
}

// ===========================================================================
// 键盘自定义（批次 D1）：冲突检测 + 验收探针
// ===========================================================================

// 高亮以整行为单位（QSS QWidget[conflict="true"]，行 widget 已开 WA_StyledBackground）
void SettingsDialog::setKeyRowConflict(int index, bool on)
{
    if (index < 0 || index >= m_keyRows.size())
        return;
    QWidget *row = m_keyRows[index].row;
    if (row->property("conflict").toBool() == on)
        return;
    row->setProperty("conflict", on);
    row->style()->unpolish(row);
    row->style()->polish(row);
    row->update();
}

void SettingsDialog::clearKeyConflicts()
{
    for (int i = 0; i < m_keyRows.size(); ++i)
        setKeyRowConflict(i, false);
}

// 实时冲突反馈：与保存前的检测同一套判定，但每次键位/勾选变化都重算一次
// （规格 §焦点与键盘状态：冲突态要指出动作与键位、键帽与行内文字标红、
// 保存按钮不可用；修正后自动清除并恢复保存）。
void SettingsDialog::refreshKeyConflictBanner()
{
    if (m_keyRows.isEmpty())
        return;
    const QString msg = keyboardConflicts();   // 内部会逐行设置 conflict 属性
    const bool has = !msg.isEmpty();

    if (m_keyBanner && m_keyBannerText) {
        m_keyBannerText->setText(msg.split(QLatin1Char('\n')).join(QStringLiteral("  ")));
        m_keyBanner->setVisible(has);
        m_keyBanner->setAccessibleName(msg);
        m_keyBanner->setAccessibleDescription(msg);
    }
    // 冲突时用红条顶替页首说明，避免两条提示叠在一起（设计图 06）。
    if (m_keyHint)
        m_keyHint->setVisible(!has);
    if (m_btnOk) {
        // 冲突时保存不可用：enabled=false 负责拦截点击/回车，颜色用控件级声明
        // 覆盖（Qt 的 QStyleSheetStyle 对运行期 :disabled 伪状态不重解析，实测
        // 对话框级 `#btnPrimary:disabled` 不生效；控件级声明优先级最高，稳）。
        m_btnOk->setEnabled(!has);
        if (has) {
            m_btnOk->setStyleSheet(QStringLiteral("color:%1;")
                                       .arg(QLatin1String(UiStyle::fluent::disabled())));
        } else {
            m_btnOk->setStyleSheet(QString());
        }
        m_btnOk->style()->unpolish(m_btnOk);
        m_btnOk->style()->polish(m_btnOk);
        m_btnOk->update();
    }
    if (m_footerNote) {
        if (has)
            m_footerNote->setText(tr("解决键位冲突后可保存。"));
        else
            updatePendingNote();
    }

    for (const KeyRow &r : m_keyRows) {
        if (!r.stateLab)
            continue;
        const bool bad = r.row->property("conflict").toBool();
        if (bad) {
            r.stateLab->setText(tr("冲突"));
            r.stateLab->setProperty("state", QStringLiteral("conflict"));
            r.stateLab->show();
        } else if (!r.stateLab->property("state").toString()
                       .compare(QLatin1String("conflict"))) {
            // 冲突解除后交还给录入态判定
            r.stateLab->setProperty("state", QString());
            r.stateLab->hide();
        }
        r.stateLab->style()->unpolish(r.stateLab);
        r.stateLab->style()->polish(r.stateLab);
    }
    refreshKeyEditingState();
}

// 录入态：只有真正持有键盘焦点的热键框显示「正在录入」，页首说明同步切换。
void SettingsDialog::refreshKeyEditingState()
{
    if (m_keyRows.isEmpty())
        return;
    bool anyEditing = false;
    for (const KeyRow &r : m_keyRows) {
        const bool editing = r.edit
            && (r.edit->hasFocus() || (r.edit->focusProxy() && r.edit->focusProxy()->hasFocus()));
        if (r.row->property("editing").toBool() != editing) {
            r.row->setProperty("editing", editing);
            r.row->style()->unpolish(r.row);
            r.row->style()->polish(r.row);
            r.row->update();
        }
        if (editing)
            anyEditing = true;
        if (!r.stateLab)
            continue;
        if (r.row->property("conflict").toBool())
            continue;   // 冲突优先，红字不被「正在录入」覆盖
        if (editing) {
            r.stateLab->setText(tr("正在录入"));
            r.stateLab->setProperty("state", QStringLiteral("editing"));
            r.stateLab->show();
        } else {
            r.stateLab->setText(QString());
            r.stateLab->setProperty("state", QString());
            r.stateLab->hide();
        }
        r.stateLab->style()->unpolish(r.stateLab);
        r.stateLab->style()->polish(r.stateLab);
    }
    setKeyHintMode(anyEditing);
}

void SettingsDialog::setKeyHintMode(bool editing)
{
    if (!m_keyHint)
        return;
    const QString text = editing
        ? tr("按下新的组合键。Esc 清空当前热键。")
        : tr("双击热键录入，Esc 清空。勾选即时生效，改绑需保存。");
    if (m_keyHint->text() != text)
        m_keyHint->setText(text);
}

// 键盘页冲突检测（保存前调用）。生效语义与 MainWindow::applyShortcutConfig
// 一一对应，判定基准是「保存后每个动作实际会装什么键」：
//   出厂态行（启用 + 绑自身 + 键为空或等于出厂主键）= 基线，自身不参与冲突，
//   其动作按出厂完整键位表生效；其余为自定义行（将顶掉出厂键位）。
// 多个启用行指向同一动作是合法用法（第 15 期：open 行可改绑 openfolder，
// 两个槽位的键聚合到同一动作）；同一动作内部重复的键只算一次，不报冲突。
// 两类冲突（只由自定义行触发，且目标动作不同）：
//   1) 两个自定义行键位相同；
//   2) 自定义行键位撞上「仍处于出厂态的其他动作」的任何键（含 PageUp/Backspace
//      等副键）或固定动作（打印 Ctrl+P 等 13 个之外的动作）的任何键。
// 有冲突：高亮全部冲突行并返回提示文案（每条冲突一行，指出键位与冲突的两个动作）；
// 无冲突返回空串。
QString SettingsDialog::keyboardConflicts()
{
    struct RowState {
        bool enabled = false;
        QString target;
        QKeySequence key;
        bool factory = false;
    };
    QList<RowState> st;
    for (const KeyRow &r : m_keyRows) {
        RowState s;
        s.enabled = r.cb->isChecked();
        s.target = r.action;   // 动作只读，目标恒等于行自身动作
        s.key = r.edit->keySequence();
        const QKeySequence defSeq =
            QKeySequence::fromString(r.defHotkey, QKeySequence::PortableText);
        s.factory = s.enabled && s.target == r.action
                    && (s.key.isEmpty() || s.key == defSeq);
        st.append(s);
    }

    // 动作 id → 显示名（「操作」列已改只读标签，直接从行里取）
    QHash<QString, QString> labelOf;
    for (const KeyRow &r : m_keyRows) {
        labelOf.insert(r.action, r.label);
    }
    auto label = [&labelOf](const QString &id) { return labelOf.value(id, id); };
    auto rowIndexOfAction = [this](const QString &action) -> int {
        for (int i = 0; i < m_keyRows.size(); ++i) {
            if (m_keyRows[i].action == action)
                return i;
        }
        return -1;
    };

    // 占用表（显示名 → 键位列表）：仍处于出厂态的可配置动作（含副键）+ 固定动作
    QList<QPair<QString, QList<QKeySequence>>> occupied;
    QList<QString> occupiedIds;   // 与 occupied 平行；空串 = 固定动作
    if (MainWindow *mw = qobject_cast<MainWindow *>(parent())) {
        const auto factory = mw->factoryShortcutLists();
        for (auto it = factory.constBegin(); it != factory.constEnd(); ++it) {
            // 只有当该动作自己的行处于出厂态时，它的出厂键位才仍然在岗
            const int own = rowIndexOfAction(it.key());
            if (own >= 0 && st[own].factory) {
                occupied.append({ label(it.key()), it.value() });
                occupiedIds.append(it.key());
            }
        }
        QHash<QString, QList<QKeySequence>> merged;
        const auto fixedEntries = mw->fixedShortcutActions();
        for (const auto &f : fixedEntries)
            merged[f.first].append(f.second);
        for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
            occupied.append({ it.key(), it.value() });
            occupiedIds.append(QString());
        }
    }

    QSet<int> bad;
    QStringList msgs;
    auto markBad = [&bad, &msgs](QList<int> rows, const QString &m) {
        for (int r : rows)
            bad.insert(r);
        if (!msgs.contains(m))
            msgs.append(m);
    };
    auto keyText = [](const QKeySequence &s) {
        return s.toString(QKeySequence::NativeText);
    };

    // 1) 两个已启用且已改绑的行键位相同（同目标动作内部重复由 MainWindow 聚合去重，不算冲突）
    for (int i = 0; i < st.size(); ++i) {
        if (!st[i].enabled || st[i].factory || st[i].key.isEmpty())
            continue;
        for (int j = i + 1; j < st.size(); ++j) {
            if (!st[j].enabled || st[j].factory || st[j].key.isEmpty())
                continue;
            if (st[i].target == st[j].target)
                continue;
            if (st[i].key == st[j].key)
                markBad({ i, j },
                        tr("按键「%1」冲突：『%2』与『%3』被绑到同一组合键。")
                            .arg(keyText(st[i].key), label(st[i].target),
                                 label(st[j].target)));
        }
    }
    // 2) 已启用的改绑行 vs 出厂/固定键位（停用行不占键；目标动作自己的出厂表不参与）
    for (int i = 0; i < st.size(); ++i) {
        if (!st[i].enabled || st[i].factory || st[i].key.isEmpty())
            continue;
        for (int k = 0; k < occupied.size(); ++k) {
            if (!occupiedIds[k].isEmpty() && occupiedIds[k] == st[i].target)
                continue;
            if (occupied[k].second.contains(st[i].key)) {
                // 规格：冲突的两行都要标红（键帽 + 行内文字）。占用的若是
                // 可配置动作，把它对应的行一起标记；固定动作没有行，只标自己。
                QList<int> rows{i};
                if (!occupiedIds[k].isEmpty()) {
                    const int other = rowIndexOfAction(occupiedIds[k]);
                    if (other >= 0 && other != i)
                        rows.append(other);
                }
                markBad(rows,
                        tr("按键「%1」冲突：『%2』与『%3』被绑到同一组合键。")
                            .arg(keyText(st[i].key), label(st[i].target),
                                 occupied[k].first));
            }
        }
    }

    for (int i = 0; i < st.size(); ++i)
        setKeyRowConflict(i, bad.contains(i));
    return msgs.join(QStringLiteral("\n"));
}

// —— 主题切换验收探针（--smoke settheme:<mode>）——
void SettingsDialog::probeTheme(const QString &theme)
{
    // 外观页自 2026-11-16 起改为延后构建（首次进入才建），而设置页默认停在「通用」。
    // 探针若直接读 m_themeCombo 会是空指针、静默不执行；这里先按用户真实路径把外观页
    // 建出来（序号 2 见构造期 addPage 顺序），也让探针只承担「一般 + 外观」两页的树，
    // 与真实使用者打开设置页后切主题时的现场一致。
    if (!m_themeCombo)
        ensurePageBuilt(2);
    if (!m_themeCombo)
        return;
    const int idx = m_themeCombo->findData(theme);
    if (idx >= 0 && idx != m_themeCombo->currentIndex())
        m_themeCombo->setCurrentIndex(idx);   // → currentIndexChanged → applyThemePreview
}

// —— 验收探针（--smoke keyconflict）——
void SettingsDialog::probeKeyConflict()
{
    // 切到键盘页（导航第 5 项，0=通用），让冲突行高亮进截图
    selectPage(4);
    // 把「删除」行热键改成 Ctrl+P（固定动作「打印」的主键），走真实保存链路：
    // 应弹「Ctrl+P 已被『打印』占用」、冲突行高亮、config 不落盘。
    for (const KeyRow &r : m_keyRows) {
        if (r.action == QLatin1String("del")) {
            r.edit->setKeySequence(QKeySequence(QStringLiteral("Ctrl+P")));
            break;
        }
    }
    // 冲突提示框是 exec 阻塞的：先定时在嵌套循环里截提示框证据再关掉它
    QTimer::singleShot(600, this, [this]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            mb->grab().save(QStringLiteral("Temp/keys-conflict-msg.png"));
            mb->close();   // exec 随之返回，保存已被阻止
        }
    });
    if (!onAccepted()) {
        // 冲突行高亮态：窗口此时未被关闭（P0-1 之前这里已被 accept() 关掉）。
        grab().save(QStringLiteral("Temp/keys-conflict-row.png"));
    }
    close();
}

// —— 批次22 即时化验收探针（--smoke etcprobe:<spec>）——
// 走真实 PillToggle::click → applyLiveCheck（下发运行态 + 写盘 + 失败回退），
// 不绕过任何链路。dump 同一会话内「控件态 / Config getter / QSettings 原始值」
// 证明写盘即被运行态读到（QSettings 单实例），且页面内控件态随切换即时更新。
void SettingsDialog::probeEtcLive(const QString &spec)
{
    const auto dump = [this](const QString &tag) {
        Config &cfg = Config::instance();
        QSettings &s = cfg.settings();
        const auto b = [&s](const char *full) { return s.value(QString::fromLatin1(full)).toBool(); };
        QString out;
        QTextStream ts(&out);
        ts << "[" << tag << "]\n";
        const struct { const char *key; const char *full; bool getter; bool checked; } rows[] = {
            { "etc/raw_thumbnail",       "config/etc/raw_thumbnail",       cfg.rawThumbnail(),       false },
            { "etc/fit_window_to_image", "config/etc/fit_window_to_image", cfg.fitWindowToImage(),   false },
            { "etc/disable_screensaver", "config/etc/disable_screensaver", cfg.disableScreensaver(), false },
        };
        for (auto &r : rows) {
            bool checked = false;
            for (PillToggle *t : findChildren<PillToggle *>())
                if (t->property("cfgKey").toString() == QLatin1String(r.key)) { checked = t->isChecked(); break; }
            ts << r.key
               << " checked=" << (checked ? "true" : "false")
               << " getter=" << (r.getter ? "true" : "false")
               << " stored=" << (b(r.full) ? "true" : "false")
               << " present=" << (s.contains(QString::fromLatin1(r.full)) ? "1" : "0") << "\n";
        }
        // 联动项 #82 noRawLoad 不被误伤：单独 dump
        ts << "etc/no_raw_load getter=" << (cfg.noRawLoad() ? "true" : "false") << "\n";
        QFile f(QStringLiteral("Temp/etc-live-probe.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            f.write(out.toUtf8());
        }
    };

    QTimer::singleShot(400, this, [this, spec, dump]() {
        // 九页重组后三个探针开关分居两页：先建「通用」（按图调窗 / 禁用屏保），
        // 再进「图像处理」（RAW 缩略图）；延后构建页首次进入才建控件。
        const auto navs = findChildren<QPushButton *>(QStringLiteral("navItem"));
        for (QPushButton *b : navs) {
            if (b->text().contains(tr("通用"))) { b->click(); break; }
        }
        QTimer::singleShot(350, this, [this, spec, dump]() {
        const auto navs2 = findChildren<QPushButton *>(QStringLiteral("navItem"));
        for (QPushButton *b : navs2) {
            if (b->text().contains(tr("图像处理"))) { b->click(); break; }
        }
        QTimer::singleShot(500, this, [this, spec, dump]() {
            QFile::remove(QStringLiteral("Temp/etc-live-probe.txt"));
            dump(QStringLiteral("before"));
            // spec: "key=on|off+key=on|off"（+ 分隔：--smoke 用逗号拆命令）
            const QStringList items = spec.split(u'+');
            for (const QString &item : items) {
                const int eq = item.indexOf(u'=');
                if (eq < 0) continue;
                const QString key = item.left(eq).trimmed();
                const bool want = item.mid(eq + 1).trimmed() == QLatin1String("on");
                for (PillToggle *t : findChildren<PillToggle *>()) {
                    if (t->property("cfgKey").toString() != key) continue;
                    if (t->isChecked() != want)
                        t->click();   // 真实链路：applyLiveCheck → applyLiveSetting → 写盘
                    break;
                }
            }
            QTimer::singleShot(500, this, [this, dump]() {
                dump(QStringLiteral("after"));
                // 设置页自身 grab（--screenshot 抓的是主窗口，抓不到模态设置页）
                grab().save(QStringLiteral("Temp/etc-live-settings.png"));
            });
        });
        });
    });
}

// —— 验收探针（--smoke viewprobe / viewcancel / vievesc / viewclose / viewsave，
//    见 MainWindow::runSmokeCommand）——
// spec: "key=on+key=off"（+ 分隔：--smoke 用逗号拆命令）。每切换一项立刻 dump
// 一次（tag = step:<key>=<on|off>），最后 dump after / 截图 / 按 closeMode 走
// 真实关闭链路，再 dump after-close（用于三链路回滚取证）。
void SettingsDialog::probeViewLive(const QString &spec, int closeMode, bool thenSave)
{
    // 「失败注入」时 applyLiveCheck 会弹模态 warning，探针要先挂一个自动点掉它的
    // 定时器，否则 exec 卡住、后面的 dump 永远不出。
    auto *autoOk = new QTimer(this);
    autoOk->setInterval(120);
    connect(autoOk, &QTimer::timeout, this, [this]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            // 失败注入的取证：把「未能即时生效」提示原样存一张，再点掉。
            mb->grab().save(QStringLiteral("Temp/view-live-fail.png"));
            if (auto *ok = mb->button(QMessageBox::Ok))
                ok->click();
        }
    });
    autoOk->start();
    // reject / accept 会关窗；探针要在关窗之后继续 dump 回滚结果，所以去掉
    // WA_DeleteOnClose（MainWindow 建这个探针对话框时设的），对象留到进程退出。
    setAttribute(Qt::WA_DeleteOnClose, false);

    const auto dump = [this](const QString &tag) {
        Config &cfg = Config::instance();
        QSettings &s = cfg.settings();
        const auto b = [&s](const char *full) { return s.value(QString::fromLatin1(full)).toBool(); };
        const auto present = [&s](const char *full) { return s.contains(QString::fromLatin1(full)) ? 1 : 0; };
        QString out;
        QTextStream ts(&out);
        ts << "[" << tag << "]\n";
        const struct { const char *key; const char *full; bool getter; } rows[] = {
            { "view/save_zoom_state", "config/view/save_zoom_state", cfg.saveZoomState() },
            { "view/overlay_filename", "config/view/overlay_filename", cfg.overlayFilename() },
            { "view/show_anim_control", "config/view/show_anim_control", cfg.showAnimControl() },
            { "view/show_tooltip", "config/view/show_tooltip", cfg.showTooltip() },
        };
        for (const auto &r : rows) {
            bool checked = false;
            for (PillToggle *t : findChildren<PillToggle *>())
                if (t->property("cfgKey").toString() == QLatin1String(r.key)) {
                    checked = t->isChecked();
                    break;
                }
            ts << r.key
               << " checked=" << (checked ? "true" : "false")
               << " getter=" << (r.getter ? "true" : "false")
               << " stored=" << (b(r.full) ? "true" : "false")
               << " present=" << present(r.full);
            if (MainWindow *host = qobject_cast<MainWindow *>(parentWidget()))
                ts << " runtime=" << host->liveSettingValue(QString::fromLatin1(r.key)).toBool();
            ts << "\n";
        }
        if (MainWindow *host = qobject_cast<MainWindow *>(parentWidget())) {
            ts << "mainwin " << host->viewLiveStateDump() << "\n";
            // 同页回归项 #12/#14/#16：只 dump 落盘值与运行态（getter 在 Config 里
            // 各有实现，这里统一走 liveSettingValue，与设置页显示同一个来源）。
            for (const char *k : { "view/show_side_arrow", "view/show_zoom_control",
                                   "view/exif_small_font" }) {
                const QString key = QString::fromLatin1(k);
                ts << "regress " << key
                   << " stored=" << (b(("config/" + key).toLatin1().constData()) ? "true" : "false")
                   << " present=" << (s.contains(QStringLiteral("config/") + key) ? 1 : 0)
                   << " runtime=" << host->liveSettingValue(key).toBool() << "\n";
            }
        }
        ts << "dialogVisible=" << (isVisible() ? 1 : 0)
           << " note=\""
           << (m_browseLiveNote && m_browseLiveNote->isVisible()
                   ? m_browseLiveNote->text() : QStringLiteral("(hidden)"))
           << "\"\n";
        QFile f(QStringLiteral("Temp/view-live-probe.txt"));
        if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write(out.toUtf8());
    };

    const auto clickToggle = [this](const QString &key, bool want) {
        for (PillToggle *t : findChildren<PillToggle *>()) {
            if (t->property("cfgKey").toString() != key)
                continue;
            if (t->isChecked() != want)
                t->click();   // 真实链路：applyLiveCheck → applyLiveSetting → 写盘
            return true;
        }
        return false;
    };

    QTimer::singleShot(400, this, [this, spec, closeMode, thenSave, dump, clickToggle, autoOk]() {
        // 「外观与显示」是延后构建页：先进页把视觉开关建出来（与用户点导航同一路径）。
        const auto navs = findChildren<QPushButton *>(QStringLiteral("navItem"));
        for (QPushButton *b : navs) {
            if (b->text().contains(tr("外观与显示"))) { b->click(); break; }
        }
        QTimer::singleShot(500, this, [this, spec, closeMode, thenSave, dump, clickToggle, autoOk]() {
            QFile::remove(QStringLiteral("Temp/view-live-probe.txt"));
            dump(QStringLiteral("before"));
            const QStringList items = spec.split(u'+');
            for (const QString &item : items) {
                const int eq = item.indexOf(u'=');
                if (eq < 0)
                    continue;
                const QString key = item.left(eq).trimmed();
                const bool want = item.mid(eq + 1).trimmed() == QLatin1String("on");
                // 悬浮提示的真实行为取证：切到「关」之前先点亮一个系统工具提示，
                // 切换后 dump 出的 tipvis 应为 0（隐藏发生在 applyLiveSetting 里）。
                if (key == QLatin1String("view/show_tooltip") && !want
                    && !QToolTip::isVisible()) {
                    QToolTip::showText(QCursor::pos(), QStringLiteral("probe-tip"), this);
                    dump(QStringLiteral("tip-primed"));
                }
                if (!clickToggle(key, want))
                    dump(QStringLiteral("step-missing:") + item);
                dump(QStringLiteral("step:") + item);
            }
            QTimer::singleShot(600, this, [this, closeMode, thenSave, dump, autoOk]() {
                dump(QStringLiteral("after"));
                grab().save(QStringLiteral("Temp/view-live-settings.png"));
                // 真实关闭链路：1 = footer「取消」钮；2 = Esc 键；3 = 标题栏 ✕（close()）；
                // 4 = footer「保存设置」（accept → 保留）。都落在同一个 reject() / accept()。
                if (closeMode == 1) {
                    for (QPushButton *b : findChildren<QPushButton *>())
                        if (b->text() == tr("取消")) { b->click(); break; }
                } else if (closeMode == 2) {
                    QKeyEvent ev(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                    QApplication::sendEvent(this, &ev);
                } else if (closeMode == 3) {
                    close();
                } else if (closeMode == 4 || thenSave) {
                    if (m_btnOk)
                        m_btnOk->click();
                }
                if (closeMode != 0 || thenSave) {
                    dump(QStringLiteral("after-close"));
                    autoOk->stop();
                } else {
                    autoOk->stop();
                }
            });
        });
    });
}

// —— 验收探针（--smoke setcfglive:<cfgKey>=on|off）——
// 只走真实控件点击链路（PillToggle::click → chkBind → applyLiveSetting → 写盘），
// 不直接改 QSettings，确保验的是「改开关即时生效」本身。
void SettingsDialog::probeCfgLive(const QString &spec)
{
    const int eq = spec.indexOf(u'=');
    const QString key = eq < 0 ? spec : spec.left(eq).trimmed();
    const bool want = eq >= 0 && spec.mid(eq + 1).trimmed() == QLatin1String("on");
    QTimer::singleShot(400, this, [this, key, want]() {
        const auto navs = findChildren<QPushButton *>(QStringLiteral("navItem"));
        for (QPushButton *b : navs) {
            if (b->text().contains(tr("图像处理"))) {
                b->click();
                break;
            }
        }
        QTimer::singleShot(700, this, [this, key, want]() {
            const auto dump = [this, key](const char *tag) {
                bool checked = false;
                for (PillToggle *t : findChildren<PillToggle *>())
                    if (t->property("cfgKey").toString() == key) { checked = t->isChecked(); break; }
                Config &cfg = Config::instance();
                const QString out = QStringLiteral("[%1] key=%2 checked=%3 getter=%4 stored=%5 present=%6\n")
                    .arg(QString::fromLatin1(tag), key,
                         checked ? QStringLiteral("true") : QStringLiteral("false"),
                         cfg.autoSaveRotate() ? QStringLiteral("true") : QStringLiteral("false"),
                         cfg.settings().value(QStringLiteral("config/auto_save_rotate"), false).toBool()
                             ? QStringLiteral("true") : QStringLiteral("false"),
                         cfg.settings().contains(QStringLiteral("config/auto_save_rotate")) ? QStringLiteral("1")
                                                                                            : QStringLiteral("0"));
                QFile f(QStringLiteral("Temp/cfg-live-probe.txt"));
                if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                    f.write(out.toUtf8());
            };
            QFile::remove(QStringLiteral("Temp/cfg-live-probe.txt"));
            dump("before");
            for (PillToggle *t : findChildren<PillToggle *>()) {
                if (t->property("cfgKey").toString() != key) continue;
                if (t->isChecked() != want) t->click();   // 真实链路
                break;
            }
            QTimer::singleShot(500, this, [this, dump]() {
                dump("after");
                grab().save(QStringLiteral("Temp/cfg-live-settings.png"));
            });
        });
    });
}

// —— 验收探针（--smoke keyreset）——
void SettingsDialog::probeResetDefaults()
{
    // footer「恢复默认」→「是否要初始化设置？」自动答「是」→ 回默认值；
    // 再按真实「确定」链路写盘并 accept（accept 不走快照回滚）。
    QTimer::singleShot(400, this, [this]() {
        if (auto *mb = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            if (auto *yes = mb->button(QMessageBox::Yes))
                yes->click();
        }
    });
    if (m_btnReset)
        m_btnReset->click();
    onAccepted();
    accept();
}

// —— 验收探针（--smoke settingsgeo:<page>:<W>x<H>[:bottom][:fold]）——
// 最小窗口验收：先把窗口摆到指定尺寸，再切页，保证页面在紧凑档布局下重建/重排；
// 「:bottom」再加一步滚到页底，「:fold」点开「支持的格式」折叠体（Windows 集成页）。
void SettingsDialog::probePageAtSize(int page, int w, int h, bool toBottom,
                                     bool expandFormats)
{
    if (w > 0 && h > 0)
        resize(w, h);
    QTimer::singleShot(300, this, [this, page, toBottom, expandFormats]() {
        selectPage(page);
        if (expandFormats && m_formatsToggleBtn && m_formatsFoldBody
            && !m_formatsFoldBody->isVisible())
            m_formatsToggleBtn->click();
        if (!toBottom || !m_stack)
            return;
        QTimer::singleShot(250, this, [this]() {
            if (auto *sa = qobject_cast<QScrollArea *>(m_stack->currentWidget()))
                sa->verticalScrollBar()->setValue(sa->verticalScrollBar()->maximum());
        });
    });
}

// —— 验收探针（--smoke settingscombo，见 MainWindow::runSmokeCommand）——
void SettingsDialog::probeOpenCombo(int page, int index)
{
    selectPage(page);
    QTimer::singleShot(500, this, [this, index]() {
        QList<QComboBox *> combos;
        for (QComboBox *c : findChildren<QComboBox *>()) {
            if (c->isVisible()) {
                combos << c;
            }
        }
        // 清单落盘：索引是「可见下拉」的创建顺序，换顺序时应以这份清单为准
        if (QFile f(QStringLiteral("Temp/settings-combos.txt")); f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            for (int i = 0; i < combos.size(); ++i) {
                ts << i << ": current=\"" << combos.at(i)->currentText()
                   << "\" items=" << combos.at(i)->count() << "\n";
            }
        }
        if (index >= 0 && index < combos.size()) {
            combos.at(index)->showPopup();
        }
    });
}

// ===========================================================================
// 窗口铬件：圆角底 + 边框；标题栏拖拽
// ===========================================================================

void SettingsDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (m_centered)
        return;
    probeMark("shown", probeClock().nsecsElapsed() / 1000);
    m_centered = true;
    QWidget *host = parentWidget() ? parentWidget()->window() : nullptr;
    if (!host)
        return;
    const QRect g = host->frameGeometry();
    move(g.center() - QPoint(width() / 2, height() / 2));
}

void SettingsDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(r, UiStyle::RadiusDialog, UiStyle::RadiusDialog);
    // 设置框外壳走 Fluent 定稿的「内容背景」，1px 边框也用定稿描边色；
    // 转换对话框有自己的 paintEvent，不受影响。
    p.fillPath(path, UiStyle::color(UiStyle::fluent::contentBg()));
    p.setPen(QPen(UiStyle::color(UiStyle::fluent::cardLine()), 1));
    p.drawPath(path);
}

void SettingsDialog::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->position().y() < kTitleBarH) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
        return;
    }
    QDialog::mousePressEvent(event);
}

void SettingsDialog::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - m_dragOffset);
    QDialog::mouseMoveEvent(event);
}

void SettingsDialog::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QDialog::mouseReleaseEvent(event);
}

bool SettingsDialog::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    // 无边框窗口没有系统边框，靠 WM_NCHITTEST 把四边/四角交回系统处理：
    // 光标形状与拖动改大小都由 Windows 原生完成，Qt 只接收 WM_SIZE 重排布局。
    // 6px 以内算边框（含四角），其余仍走客户区，标题栏拖动不受影响。
    if (eventType == QByteArrayLiteral("windows_generic_MSG")) {
        auto *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_GETMINMAXINFO) {
            MONITORINFO monitor{};
            monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfo(MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                auto *bounds = reinterpret_cast<MINMAXINFO *>(msg->lParam);
                const RECT area = monitor.rcWork;
                const qreal scale = devicePixelRatioF();
                const int minW = qRound(minimumWidth() * scale);
                const int minH = qRound(minimumHeight() * scale);
                bounds->ptMinTrackSize = {minW, minH};
                bounds->ptMaxTrackSize = {qMax(minW, int(area.right - area.left)),
                                          qMax(minH, int(area.bottom - area.top))};
                *result = 0;
                return true;
            }
        }
        if (msg && msg->message == WM_NCHITTEST) {
            const int gx = static_cast<short>(msg->lParam & 0xffff);
            const int gy = static_cast<short>((msg->lParam >> 16) & 0xffff);
            POINT point{gx, gy};
            ScreenToClient(msg->hwnd, &point);
            const QPointF local = QPointF(point.x, point.y) / devicePixelRatioF();
            constexpr int kEdge = 6;
            const bool left = local.x() < kEdge;
            const bool right = local.x() >= width() - kEdge;
            const bool top = local.y() < kEdge;
            const bool bottom = local.y() >= height() - kEdge;
            int ht = 0;
            if (top && left)
                ht = HTTOPLEFT;
            else if (top && right)
                ht = HTTOPRIGHT;
            else if (bottom && left)
                ht = HTBOTTOMLEFT;
            else if (bottom && right)
                ht = HTBOTTOMRIGHT;
            else if (left)
                ht = HTLEFT;
            else if (right)
                ht = HTRIGHT;
            else if (top)
                ht = HTTOP;
            else if (bottom)
                ht = HTBOTTOM;
            if (ht != 0) {
                *result = ht;
                return true;
            }
        }
    }
    return QDialog::nativeEvent(eventType, message, result);
}

void SettingsDialog::keyPressEvent(QKeyEvent *event)
{
    // 搜索框内 Esc 先清搜索；Enter 不做「保存设置」。
    if (event->key() == Qt::Key_Escape && m_appMenuPage && m_appMenuPage->isVisible()) {
        auto *find = m_appMenuPage->findChild<QLineEdit *>(QStringLiteral("appMenuFind"));
        if (find && !find->text().isEmpty()) {
            find->clear();
            event->accept();
            return;
        }
    }
    if (m_search && m_search->hasFocus()
        && event->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
        m_search->clear();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QWidget *f = focusWidget();
        // 输入类控件里回车属于控件自身（选择下拉项 / 结束键位录制 / 编辑框提交），
        // 不能顺带保存并关窗。
        if (qobject_cast<QLineEdit *>(f) || qobject_cast<QAbstractSpinBox *>(f)
            || qobject_cast<QComboBox *>(f) || qobject_cast<QKeySequenceEdit *>(f)
            || (f && qobject_cast<QKeySequenceEdit *>(f->parentWidget()))) {
            return;
        }
        accept();   // 与保存按钮同一条校验路径，冲突时不关闭窗口（P0-1）
        return;
    }
    QDialog::keyPressEvent(event); // ESC -> reject() -> 待保存询问
}

void SettingsDialog::accept()
{
    // 唯一的「保存并关闭」入口：按钮、Enter/Return、默认按钮都汇到这里。
    // 校验（键盘冲突）或写盘未通过时不调基类，窗口保持打开。
    if (!onAccepted())
        return;
    QDialog::accept();
}

void SettingsDialog::reject()
{
    // 同一次退出里，MainWindow::closeEvent 会显式调一次 reject()，Qt 关主窗口时
    // 也可能向非模态子对话框再发一次关闭。没有这层守卫会连弹两次「保存更改？」。
    if (m_prompting)
        return;
    // 关闭语义（2026-11-07，2026-11-20 批次03 修订）：普通设置已即时写盘，关闭即保留；
    // 例外是浏览页四项（缩放记忆 / 文件名浮层 / 动画帧号 / 悬浮提示）——它们按
    // 派单 §2.4 要求取消即回滚，配置与运行态一起回到进入设置时的状态。
    // 只有热键改绑 / 界面语言是待保存项。有草稿时弹三选一。
    flushPendingTheme();   // 排队中的主题切换先落运行态并写盘，再决定关闭方式
    const QStringList names = pendingChangeNames();
    if (names.isEmpty()) {
        // 直接关闭：同样要确认写盘成功，不能「看着关了、其实没落盘」。
        QString error;
        if (!flushPendingWrites(&error)) {
            warnSaveFailure(error);
            return;   // 留在设置页，允许稍后重试
        }
        rollbackBrowseLiveBaseline();   // 批次03：取消 / Esc / ✕ 三链路同口径回滚
        QDialog::reject();
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(tr("保存更改？"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("有 %1 项更改尚未保存。").arg(names.size()));
    QStringList shown = names.mid(0, 4);
    if (names.size() > shown.size())
        shown << tr("等 %1 项").arg(names.size());
    box.setInformativeText(tr("待保存：%1\n\n"
                             "「不保存」只放弃这些草稿；浏览页的缩放记忆 / 文件名浮层 / "
                             "动画帧号 / 悬浮提示会回到进入设置时的状态，其余即时设置保留。")
                               .arg(shown.join(QStringLiteral("、"))));
    auto *saveBtn = box.addButton(tr("保存并关闭"), QMessageBox::AcceptRole);
    box.addButton(tr("不保存"), QMessageBox::DestructiveRole);
    auto *keepBtn = box.addButton(tr("继续编辑"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.setEscapeButton(keepBtn);
    m_prompting = true;
    box.exec();
    m_prompting = false;

    if (box.clickedButton() == saveBtn) {
        accept();   // 冲突 / 写盘失败时 accept 内部已拦截，窗口保持打开
        return;
    }
    if (box.clickedButton() && box.clickedButton() != keepBtn) {
        // 不保存：丢弃热键 / 语言草稿，浏览页四项回滚；其余已即时项仍须确认落盘。
        QString error;
        if (!flushPendingWrites(&error)) {
            warnSaveFailure(error);
            return;   // 留在设置页，允许稍后重试
        }
        rollbackBrowseLiveBaseline();   // 批次03：与 Esc / ✕ 同一口径
        QDialog::reject();   // 丢弃待保存草稿
    }
}
