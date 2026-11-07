#pragma once

// QuikView 深色视觉层（Win11 原型对齐）
// ---------------------------------------------------------------------------
// 颜色/圆角/字号全部取自二级界面原型 Temp/quikview-secondary-prototype.html 的 :root，
// 这里是唯一真源：任何界面都用这里的令牌，不要再各自硬编码颜色。
//
// 用法：
//   Theme::apply() 已把 appQss() 装到 qApp 上（菜单/提示/滚动条/对话框底座）；
//   对话框内部用 dialogQss()，浮层面板用 panelQss()，各自 setStyleSheet 即可。
// ---------------------------------------------------------------------------

#include <QColor>
#include <QFont>
#include <QMenu>
#include <QString>

#if defined(Q_OS_WIN)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#endif

namespace UiStyle {

// —— 主题模式（2026-11-04 起支持浅色）——
// Theme::apply 切换；色板函数与三套 QSS 按它出深/浅两套值。
// 原则：深色值与历史常量逐字节一致（保证深色零回归），浅色值另起一套。
enum class Scheme { Dark, Light };
inline Scheme &activeScheme() { static Scheme s = Scheme::Dark; return s; }
inline void setScheme(Scheme s) { activeScheme() = s; }
inline bool isLight() { return activeScheme() == Scheme::Light; }
inline const char *pick(const char *dark, const char *light)
{
    return isLight() ? light : dark;
}

// 磨砂材质开关（config/ui/blur_material）。只影响右键菜单与信息浮层：
// 开 → 菜单半透明 + 尝试 DWM Acrylic；关 → 纯色不透明。
// 由 MainWindow::applyRuntimeSettings 在启动/保存设置后写入，appQss() 与
// polishMenu() 都读这里，避免各处重复拷配置。
inline bool &blurMaterial()
{
    static bool on = false;
    return on;
}
inline void setBlurMaterial(bool on) { blurMaterial() = on; }

// QMenu 圆角要干净，必须让窗口本身透明且无边框（否则四角露出黑底）。
inline void polishMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setWindowFlag(Qt::FramelessWindowHint, true);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
#if defined(Q_OS_WIN)
    if (blurMaterial()) {
        // 真正的亚克力必须由 DWM 合成，QSS 只能给半透明底。窗口句柄在首次
        // winId() 时创建；失败（Win10 / 旧版 Win11）就退回纯半透明，不报错。
        HWND hwnd = reinterpret_cast<HWND>(menu->winId());
        if (hwnd) {
            const DWORD kBackdropAttr = 38;   // DWMWA_SYSTEMBACKDROP_TYPE
            int type = 3;                     // DWMSBT_TRANSIENTWINDOW (Acrylic)
            DwmSetWindowAttribute(hwnd, kBackdropAttr, &type, sizeof(type));
        }
    }
#endif
}

// —— 原型 :root 色板 ——
inline constexpr const char *Bg      = "#101113";  // 应用底
inline constexpr const char *Window  = "#17181b";  // 窗口
inline constexpr const char *Bar     = "#202124";  // 命令栏
inline constexpr const char *Panel   = "#242528";  // 内容面板
inline constexpr const char *Panel2  = "#2a2c30";  // 次级面板
inline constexpr const char *Field   = "#2d2f34";  // 输入框底
inline constexpr const char *Hover   = "#35383e";  // 悬停
inline constexpr const char *Text    = "#f2f2f2";  // 主文字
inline constexpr const char *Body    = "#d5d7da";  // 正文
inline constexpr const char *Muted   = "#92979e";  // 次要文字
inline constexpr const char *Dim     = "#686e76";  // 弱化文字（只限装饰）
inline constexpr const char *Accent  = "#55c0f0";  // 强调（青）
inline constexpr const char *Accent2 = "#1689bd";  // 强调深
inline constexpr const char *AccentBg = "rgba(85, 192, 240, 0.12)";
inline constexpr const char *Line    = "rgba(255, 255, 255, 0.085)";  // 细分隔
inline constexpr const char *Line2   = "rgba(255, 255, 255, 0.14)";   // 强分隔/描边
inline constexpr const char *Border  = "#787878";  // 控件描边（对 Field 3.05 / Panel 3.47）
inline constexpr const char *Danger  = "#c42b1c";  // 关闭/删除

// —— 对话框 / 无边框窗口底板（第 19 期从 QSS 裸值提为令牌）——
inline constexpr const char *DialogBg     = "#232427";  // 设置/转换对话框底
inline constexpr const char *FooterBg     = "#222326";  // 对话框底栏
inline constexpr const char *NavBg        = "#1e1f22";  // 导航栏
inline constexpr const char *TitleBg      = "#25262a";  // 对话框标题栏
inline constexpr const char *CanvasBg     = "#202020";  // 画布纯色底
inline constexpr const char *ChromeBg     = "#161719";  // 无边框窗口底
inline constexpr const char *ChromeBorder = "#3a3c40";  // 无边框窗口描边
inline constexpr const char *DialogBorder = "#4a4c51";  // 对话框外描边

// —— 控件 / 文字辅助角色（同上，避免界面代码再出现裸色值）——
inline constexpr const char *NavText          = "#bfc3c8";  // 导航项常态文字
inline constexpr const char *HeadText         = "#9ca2aa";  // 表头文字
inline constexpr const char *HintText         = "#9aa0a6";  // 页内提示文字
inline constexpr const char *SubtleText       = "#b9bec5";  // 弱正文
inline constexpr const char *FieldText        = "#e0e2e4";  // 输入控件文字
inline constexpr const char *PureWhite        = "#ffffff";  // 悬停/选中纯白
inline constexpr const char *DisabledText     = "#6f747a";  // 禁用项文字（设置页）
inline constexpr const char *DisabledTextSoft = "#6b7078";  // 禁用项文字（全局 QSS）
inline constexpr const char *DisabledBorder   = "#3a3c40";  // 禁用项描边
inline constexpr const char *DisabledBg       = "#26282b";  // 禁用项底座
inline constexpr const char *BorderHover      = "#686d75";  // 控件悬停描边
inline constexpr const char *HeadBg           = "#292b2f";  // 表头/分组条底
inline constexpr const char *GroupBg          = "#222427";  // 分组卡片底
inline constexpr const char *WellBg           = "#1e2023";  // 只读文本井底
inline constexpr const char *StatusOk         = "#7fc47f";  // 状态：已注册
inline constexpr const char *StatusWarn       = "#d6a04a";  // 状态：缺文件
inline constexpr const char *OverlayText      = "#e8eaec";  // 文件名浮层文字
inline constexpr const char *OverlayBg        = "rgba(0,0,0,0.62)";          // 文件名浮层底
inline constexpr const char *DangerBg         = "rgba(196, 43, 28, 0.20)";   // 冲突行高亮

// —— 语义色（绘制用；全部按当前主题出深/浅值）——
inline QColor color(const char *hex) { return QColor(QString::fromLatin1(hex)); }
inline QColor accent()      { return color(pick("#55c0f0", "#1689bd")); }
inline QColor accentSoft()  { return isLight() ? QColor(22, 137, 189, 31)
                                               : QColor(85, 192, 240, 31); }
inline QColor line()        { return isLight() ? QColor(0, 0, 0, 22)
                                               : QColor(255, 255, 255, 22); }
inline QColor lineStrong()  { return isLight() ? QColor(0, 0, 0, 36)
                                               : QColor(255, 255, 255, 36); }
inline QColor text()        { return color(pick("#f2f2f2", "#1b1b1b")); }
inline QColor body()        { return color(pick("#d5d7da", "#202124")); }
inline QColor muted()       { return color(pick("#92979e", "#6b7075")); }
inline QColor dim()         { return color(pick("#686e76", "#8a8f94")); }
inline QColor field()       { return color(pick("#2d2f34", "#ffffff")); }
inline QColor panel()       { return color(pick("#242528", "#ffffff")); }
inline QColor dialogBg()    { return color(pick("#232427", "#f2f3f5")); }
inline QColor footerBg()    { return color(pick("#222326", "#ececee")); }
inline QColor navBg()       { return color(pick("#1e1f22", "#e9eaec")); }
inline QColor titleBg()     { return color(pick("#25262a", "#f0f1f3")); }
inline QColor chromeBg()    { return color(pick("#161719", "#f3f4f6")); }
inline QColor canvasBg()  { return color(pick("#202020", "#f0f0f0")); }
// 导航项常态文字/图标色：深色 #bfc3c8；浅色必须压到 #4a4d51（对导航底 6.9:1），
// 直接用深色常量会在浅色下掉到 1.47:1（实测）。
inline QColor navText()    { return color(pick("#bfc3c8", "#4a4d51")); }
// 链接文字：浅色下 #55c0f0 对白只有 1.99:1，必须压到 #0b6f9f（5.55:1）。
// 与浮层 mapLink 同一取值，两处别各改各的。
inline QColor linkText()   { return color(pick("#55c0f0", "#0b6f9f")); }

// —— 设置页排版统一（2026-11-07）——
// 分组卡片底/描边、整行悬停底、只读状态标签文字。浅色分组底比内容白面板
// 低一档（#f5f7f8），靠 1px 描边勾出组边界；悬停只提亮行，不改变控件尺寸。
inline const char *setGroupBg()   { return pick("#292b2f", "#f5f7f8"); }
inline const char *setGroupLine() { return pick("#3c4046", "#dce1e6"); }
inline const char *setRowHover()  { return pick("#33373e", "#e6edf2"); }
inline const char *setRowTag()    { return pick("#aab0b7", "#545e69"); }

// —— 尺寸令牌 ——
inline constexpr int RadiusDialog = 10;
inline constexpr int RadiusCard   = 9;
inline constexpr int RadiusButton = 6;
inline constexpr int RadiusInput  = 5;
inline constexpr int RadiusCheck  = 4;
inline constexpr int TitleBarH    = 43;
inline constexpr int NavWidth     = 204;
inline constexpr int FooterH      = 62;
inline constexpr int InfoPanelW   = 344;
// 胶囊拨动开关（外观页主题区/材质区）：轨道 40×20、圆角 10（复用最大档位），
// 滑块 16、内边距 2、焦点环外扩 2。已在设计规范 §4.2 登记。
inline constexpr int SwitchTrackW = 40;
inline constexpr int SwitchTrackH = 20;
inline constexpr int SwitchRadius = 10;
inline constexpr int SwitchKnob   = 16;
inline constexpr int SwitchInset  = 2;
inline constexpr int SwitchPad    = 2;

// 12px ≈ 9pt 微软雅黑，与原型 12px 正文一致
inline QFont font(int px = 12)
{
    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPixelSize(px);
    return f;
}

// 应用级：菜单、提示、滚动条、对话框底座。由 Theme::apply 安装；按主题出两套。
inline QString darkAppQss()
{
    QString qss = QStringLiteral(R"(
QDialog { background: %1; color: #f2f2f2; }
QWidget { color: #f2f2f2; }

QMenu { background: #27292d; color: #e0e2e5; border: 1px solid #50535a;
        border-radius: 8px; padding: 6px; }
QMenu::item { height: 25px; padding: 0 18px 0 24px; border-radius: 5px; font-size: 12px; }
QMenu::item:selected { background: #3c4046; }
QMenu::item:disabled { color: #6b7078; }
QMenu::separator { height: 1px; background: rgba(255,255,255,0.085); margin: 4px 7px; }
QMenu::icon { left: 8px; }
/* 勾选列：Qt 原生对勾偏小且颜色不受控，改用自带浅色对勾（深色菜单底） */
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
QMenu::indicator:checked { image: url(:/ui/check-light.png); }
QMenu::right-arrow { width: 12px; height: 12px; right: 8px;
                     image: url(:/ui/chevron-right.png); }

QToolTip { background: #2a2c30; color: #e6e8ea; border: 1px solid rgba(255,255,255,0.14);
           border-radius: 6px; padding: 6px 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.34); border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.46); }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: rgba(255,255,255,0.34); border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: rgba(255,255,255,0.46); }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLineEdit, QPlainTextEdit, QTextEdit { background: #2d2f34; color: #e0e2e4;
    border: 1px solid #787878; border-radius: 5px; padding: 0 8px; font-size: 12px;
    selection-background-color: #1689bd; }
QLineEdit { min-height: 26px; }
QLineEdit:hover, QPlainTextEdit:hover { border-color: #686d75; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: #55c0f0; }
QLineEdit:disabled { color: #6b7078; }

QComboBox { background: #2d2f34; color: #e0e2e4; border: 1px solid #787878;
    border-radius: 5px; padding: 0 26px 0 9px; font-size: 12px; min-height: 26px; }
QComboBox:hover { border-color: #686d75; }
QComboBox:focus { border-color: #55c0f0; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: #27292d; color: #e0e2e5; border: 1px solid #50535a;
    border-radius: 6px; padding: 4px; outline: 0; selection-background-color: #3c4046;
    selection-color: #ffffff; }

QCheckBox { color: #d0d2d5; font-size: 12px; spacing: 9px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #696e75;
    border-radius: 4px; background: #2d2f33; }
QCheckBox::indicator:hover { border-color: #9ba1a8; }
QCheckBox::indicator:checked { background: #55c0f0; border-color: #55c0f0;
    image: url(:/ui/check.png); }
QCheckBox::indicator:checked:hover { background: #72caf1; }
QCheckBox::indicator:disabled { border-color: #4a4d52; background: #26282b; }

QRadioButton { color: #c9cdd2; font-size: 12px; spacing: 7px; }
QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px;
    border: 1px solid #696e75; background: #2d2f33; }
QRadioButton::indicator:checked { border: 4px solid #55c0f0; background: #ffffff; }
QRadioButton::indicator:hover { border-color: #9ba1a8; }

QPushButton { background: #303237; color: #e1e3e5; border: 1px solid #787878;
    border-radius: 6px; padding: 0 16px; min-height: 30px; font-size: 12px; }
QPushButton:hover { background: #3a3d43; border-color: #5e6269; }
QPushButton:pressed { background: #2a2c30; }
QPushButton:disabled { color: #6b7078; border-color: #3a3c40; background: #292b2f; }

QSpinBox, QDoubleSpinBox { background: #2d2f34; color: #e0e2e4; border: 1px solid #787878;
    border-radius: 5px; padding: 0 4px 0 8px; font-size: 12px; min-height: 26px; }
QSpinBox::up-button, QSpinBox::down-button { width: 15px; border: 0; background: transparent; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 9px; height: 9px; }

QSlider::groove:horizontal { height: 3px; background: #5d6166; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #55c0f0; border-radius: 3px; }
QSlider::handle:horizontal { width: 11px; height: 11px; margin: -4px 0;
    border-radius: 6px; background: #f2f4f5; }

QProgressBar { background: #383b40; border: 0; border-radius: 4px; height: 5px;
    text-align: center; color: transparent; }
QProgressBar::chunk { background: #55c0f0; border-radius: 4px; }

QGroupBox { border: 1px solid rgba(255,255,255,0.085); border-radius: 7px;
    margin-top: 14px; padding: 12px 10px 10px; font-size: 12px; color: #eceeef; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }

QLabel { color: #d5d7da; font-size: 12px; }
QTreeWidget, QTreeView, QListView, QTableWidget { background: #1e2023; color: #d5d7da;
    border: 1px solid rgba(255,255,255,0.085); border-radius: 6px;
    alternate-background-color: #222427; selection-background-color: rgba(85,192,240,0.18);
    selection-color: #ffffff; }
/* 列表/树的悬停只在行上给浅底，不做整体缩放（交互状态统一规范 P0-3） */
QTreeView::item:hover, QListView::item:hover, QTableWidget::item:hover { background: #33363b; }
QTreeView::item:selected, QListView::item:selected, QTableWidget::item:selected {
    background: rgba(85,192,240,0.18); color: #ffffff; }
QHeaderView::section { background: #292b2f; color: #9ca2aa; border: 0;
    border-bottom: 1px solid rgba(255,255,255,0.085); padding: 5px 8px; font-size: 11px; }
QTabWidget::pane { border: 0; }
QTabBar::tab { background: transparent; color: #92979e; padding: 7px 13px;
    border-bottom: 2px solid transparent; font-size: 12px; }
QTabBar::tab:selected { color: #55c0f0; border-bottom-color: #55c0f0; }
QTabBar::tab:hover { color: #ffffff; }
)").arg(QLatin1String(DialogBg));
    // 磨砂材质开：菜单给半透明底，剩下的交给 DWM 合成（polishMenu 里设 Acrylic）；
    // 关：保持纯色，避免「半透明但没模糊」的糊字。
    if (blurMaterial()) {
        qss.replace(QStringLiteral("QMenu { background: #27292d;"),
                    QStringLiteral("QMenu { background: rgba(39,41,45,0.82);"));
    }
    return qss;
}

// 浅色应用级：与深色同构（选择器一一对应），只换色值与对勾图标。
// 菜单白底 → 深色对勾 check.png；勾选框蓝底 → 白勾 check-light.png。
inline QString lightAppQss()
{
    QString qss = QStringLiteral(R"(
QDialog { background: #f2f3f5; color: #1b1b1b; }
QWidget { color: #1b1b1b; }

QMenu { background: #ffffff; color: #1b1b1b; border: 1px solid #d0d0d0;
        border-radius: 8px; padding: 6px; }
QMenu::item { height: 25px; padding: 0 18px 0 24px; border-radius: 5px; font-size: 12px; }
QMenu::item:selected { background: #cde8f7; }
QMenu::item:disabled { color: #a0a0a0; }
QMenu::separator { height: 1px; background: rgba(0,0,0,0.085); margin: 4px 7px; }
QMenu::icon { left: 8px; }
QMenu::indicator { width: 14px; height: 14px; left: 6px; }
QMenu::indicator:checked { image: url(:/ui/check.png); }
QMenu::right-arrow { width: 12px; height: 12px; right: 8px;
                     image: url(:/ui/chevron-right.png); }

QToolTip { background: #ffffff; color: #1b1b1b; border: 1px solid rgba(0,0,0,0.14);
           border-radius: 6px; padding: 6px 8px; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: rgba(0,0,0,0.28); border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.42); }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: rgba(0,0,0,0.28); border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: rgba(0,0,0,0.42); }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLineEdit, QPlainTextEdit, QTextEdit { background: #ffffff; color: #1b1b1b;
    border: 1px solid #8a8a8a; border-radius: 5px; padding: 0 8px; font-size: 12px;
    selection-background-color: #1689bd; }
QLineEdit { min-height: 26px; }
QLineEdit:hover, QPlainTextEdit:hover { border-color: #6b6b6b; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: #1689bd; }
QLineEdit:disabled { color: #a0a0a0; }

QComboBox { background: #ffffff; color: #1b1b1b; border: 1px solid #8a8a8a;
    border-radius: 5px; padding: 0 26px 0 9px; font-size: 12px; min-height: 26px; }
QComboBox:hover { border-color: #6b6b6b; }
QComboBox:focus { border-color: #1689bd; }
QComboBox::drop-down { border: 0; width: 22px; }
QComboBox::down-arrow { image: url(:/ui/chevron-down.png); width: 12px; height: 12px; }
QComboBox QAbstractItemView { background: #ffffff; color: #1b1b1b; border: 1px solid #d0d0d0;
    border-radius: 6px; padding: 4px; outline: 0; selection-background-color: #cde8f7;
    selection-color: #1b1b1b; }

QCheckBox { color: #202124; font-size: 12px; spacing: 9px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #8a8a8a;
    border-radius: 4px; background: #ffffff; }
QCheckBox::indicator:hover { border-color: #5f5f5f; }
QCheckBox::indicator:checked { background: #1689bd; border-color: #1689bd;
    image: url(:/ui/check-light.png); }
QCheckBox::indicator:checked:hover { background: #1b9bca; }
QCheckBox::indicator:disabled { border-color: #d0d0d0; background: #f0f0f0; }

QRadioButton { color: #202124; font-size: 12px; spacing: 7px; }
QRadioButton::indicator { width: 14px; height: 14px; border-radius: 7px;
    border: 1px solid #8a8a8a; background: #ffffff; }
QRadioButton::indicator:checked { border: 4px solid #1689bd; background: #ffffff; }
QRadioButton::indicator:hover { border-color: #5f5f5f; }

QPushButton { background: #fbfbfb; color: #1b1b1b; border: 1px solid #8a8a8a;
    border-radius: 6px; padding: 0 16px; min-height: 30px; font-size: 12px; }
QPushButton:hover { background: #f0f0f0; border-color: #6b6b6b; }
QPushButton:pressed { background: #e6e6e6; }
QPushButton:disabled { color: #a0a0a0; border-color: #d0d0d0; background: #f5f5f5; }
)");
    // 磨砂开：白菜单半透明，DWM 合成交给 polishMenu。
    if (blurMaterial()) {
        qss.replace(QStringLiteral("QMenu { background: #ffffff;"),
                    QStringLiteral("QMenu { background: rgba(255,255,255,0.82);"));
    }
    return qss;
}

inline QString appQss() { return isLight() ? lightAppQss() : darkAppQss(); }

// 对话框内部（设置 / 图片转换共用）；按主题出深/浅两套。
inline QString dialogQss()
{
    if (isLight()) {
        return QStringLiteral(R"(
QDialog { background: #f4f5f7; }
QWidget#dlgTitleBar { background: #f2f4f6; border-bottom: 1px solid rgba(0,0,0,0.085); }
QLabel#dlgTitle { font-size: 13px; color: #1b1b1b; }
QWidget#dlgFooter { background: #eceef1; border-top: 1px solid rgba(0,0,0,0.085); }
QLabel#footerNote { color: #5c5f63; font-size: 11px; }

QWidget#navRail { background: #e6e8eb; }
QLabel#navCaption { color: #5c5f63; font-size: 11px; letter-spacing: 1px; padding: 0 10px; }
QPushButton#navItem { background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: #3b3f44; text-align: left; padding: 0 11px; min-height: 33px; font-size: 12px; }
QPushButton#navItem:hover { background: #d8dade; color: #101214; }
QPushButton#navItem:checked { background: rgba(22,137,189,0.12); border-color: rgba(22,137,189,0.16);
    color: #1689bd; }
QFrame#navDivider { background: rgba(0,0,0,0.085); border: 0; }

QWidget#contentPane { background: #ffffff; }
QLabel#pageTitle { font-size: 21px; color: #1b1b1b; }
QLabel#pageDesc { font-size: 12px; color: #5f6368; }
QLabel#sectionTitle { font-size: 12px; font-weight: 600; color: #2b2e33; }
QLabel#rowLabel { color: #303236; font-size: 12px; }
QLabel#hint { color: #5f6368; font-size: 11px; }
QFrame#hLine { background: rgba(0,0,0,0.085); border: 0; max-height: 1px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }

QPushButton#btnPrimary { background: #1689bd; border-color: #37a5cf; color: #ffffff; }
QPushButton#btnPrimary:hover { background: #1b9bca; }
QPushButton#btnSubtle { background: transparent; border-color: transparent; color: #4a4e53; }
QPushButton#btnSubtle:hover { background: #d8dade; color: #101214; }
QPushButton#cardButton { background: #ffffff; border: 1px solid #999999; border-radius: 7px;
    color: #303236; min-height: 54px; }
QPushButton#cardButton:hover { background: #f0f2f4; }
QPushButton#cardButton:checked { background: rgba(22,137,189,0.12); border-color: #1689bd;
    color: #1689bd; }
QPushButton#closeBtn { background: transparent; border: 0; color: #4a4e53; min-width: 30px;
    min-height: 28px; padding: 0; border-radius: 5px; font-size: 14px; }
QPushButton#closeBtn:hover { background: #c42b1c; color: #ffffff; }
)");
    }
    return QStringLiteral(R"(
QDialog { background: %1; }
QWidget#dlgTitleBar { background: %2; border-bottom: 1px solid rgba(255,255,255,0.085); }
QLabel#dlgTitle { font-size: 13px; color: #f2f2f2; }
QWidget#dlgFooter { background: %3; border-top: 1px solid rgba(255,255,255,0.085); }
QLabel#footerNote { color: %4; font-size: 11px; }

QWidget#navRail { background: %5; }
QLabel#navCaption { color: %4; font-size: 11px; letter-spacing: 1px; padding: 0 10px; }
QPushButton#navItem { background: transparent; border: 1px solid transparent; border-radius: 6px;
    color: #bfc3c8; text-align: left; padding: 0 11px; min-height: 33px; font-size: 12px; }
QPushButton#navItem:hover { background: #2d2f34; color: #ffffff; }
QPushButton#navItem:checked { background: rgba(85,192,240,0.12); border-color: rgba(85,192,240,0.16);
    color: #55c0f0; }
QFrame#navDivider { background: rgba(255,255,255,0.085); border: 0; }

QWidget#contentPane { background: #242528; }
QLabel#pageTitle { font-size: 21px; color: #f2f2f2; }
QLabel#pageDesc { font-size: 12px; color: #92979e; }
QLabel#sectionTitle { font-size: 12px; font-weight: 600; color: #eceeef; }
QLabel#rowLabel { color: #c5c8cc; font-size: 12px; }
QLabel#hint { color: #92979e; font-size: 11px; }
QFrame#hLine { background: rgba(255,255,255,0.085); border: 0; max-height: 1px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }

QPushButton#btnPrimary { background: #138ab9; border-color: #37a5cf; color: #ffffff; }
QPushButton#btnPrimary:hover { background: #1b9bca; }
QPushButton#btnSubtle { background: transparent; border-color: transparent; color: #b9bdc2; }
QPushButton#btnSubtle:hover { background: #33363b; color: #ffffff; }
QPushButton#cardButton { background: #2b2d31; border: 1px solid #45484e; border-radius: 7px;
    color: #c6cacf; min-height: 54px; }
QPushButton#cardButton:hover { background: #34373c; }
QPushButton#cardButton:checked { background: rgba(85,192,240,0.12); border-color: #55c0f0;
    color: #55c0f0; }
QPushButton#closeBtn { background: transparent; border: 0; color: #b9bdc2; min-width: 30px;
    min-height: 28px; padding: 0; border-radius: 5px; font-size: 14px; }
QPushButton#closeBtn:hover { background: #c42b1c; color: #ffffff; }
)").arg(QLatin1String(DialogBg))
   .arg(QLatin1String(TitleBg))
   .arg(QLatin1String(FooterBg))
   .arg(QLatin1String(Muted))
   .arg(QLatin1String(NavBg));
}

// 图片信息浮层（左上覆盖层，对齐原型 .info-overlay）
// 2026-11-03 深夜修订：定稿「深色半透明底 + 内容自适应 + 有值才显示 + 长值完整换行」。
// 22:05 那版「完全透明无边框」在白图/黑图上文字不可读，已废止——底板必须回来。
inline QString panelQss()
{
    if (isLight()) {
        return QStringLiteral(R"(
/* 浅色浮层：不透明纯白卡片。alpha 0.92 的白压在饱和色照片上会被染色
   （实测压红条 (246,226,227)、压绿条 (225,247,227) = 彩色玻璃感），
   浅色下必须完全不透明，靠 1px 描边立边界（2026-11-05）。 */
QWidget#infoPanel { background: #ffffff;
    border: 1px solid #dfe0e2; border-radius: 8px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
/* 全局 QLabel 的 12px 会覆盖 setFont()，小字体档位改用动态属性选择器
   （MetadataPanel 重建行时写入 smallFont 属性）。 */
QLabel#infoKey { color: #5f6368; }
QLabel#infoVal { color: #1b1b1b; }
QLabel#infoKey[smallFont="true"], QLabel#infoVal[smallFont="true"] { font-size: 10px; }
QPushButton#infoRoundBtn { min-height: 0; max-height: 22px; min-width: 22px;
    max-width: 22px; padding: 0; background: transparent; border: 0; }
QPushButton#mapLink { background: transparent; border: 0; padding: 0;
    text-align: left; font-size: 12px; color: #0b6f9f; }
QPushButton#mapLink:hover { color: #0a608c; }
QPushButton#histToggle { background: transparent; border: 0; padding: 0;
    text-align: left; font-size: 12px; color: #5f6368; }
QPushButton#histToggle:hover { color: #1b1b1b; }
)");
    }
    return QStringLiteral(R"(
/* 深色半透明底板：白图/黑图/复杂纹理上都可读；1px 低对比细边勾出边界，
   8px 圆角对齐原型 .info-overlay；不加投影（叠加在图片上会显脏）。 */
QWidget#infoPanel { background: rgba(28,29,31,0.88);
    border: 1px solid rgba(255,255,255,0.10); border-radius: 8px; }
QScrollArea, QScrollArea > QWidget > QWidget { background: transparent; border: 0; }
QLabel#infoKey { color: #9ba0a5; }
QLabel#infoVal { color: #e9eaec; }
QLabel#infoKey[smallFont="true"], QLabel#infoVal[smallFont="true"] { font-size: 10px; }
/* 浮层左上角的关闭/复制圆钮：paintEvent 自绘圆形，QSS 只负责抵消全局
   QPushButton 的 min-height/padding（否则 22x22 被撑成 22x32 纵椭圆） */
QPushButton#infoRoundBtn { min-height: 0; max-height: 22px; min-width: 22px;
    max-width: 22px; padding: 0; background: transparent; border: 0; }
QPushButton#mapLink { background: transparent; border: 0; padding: 0;
    text-align: left; font-size: 12px; color: #55c0f0; }
QPushButton#mapLink:hover { color: #72caf1; }
QPushButton#histToggle { background: transparent; border: 0; padding: 0;
    text-align: left; font-size: 12px; color: #9ba0a5; }
QPushButton#histToggle:hover { color: #e9eaec; }
)");
}

} // namespace UiStyle
