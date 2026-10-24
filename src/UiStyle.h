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

namespace UiStyle {

// QMenu 圆角要干净，必须让窗口本身透明且无边框（否则四角露出黑底）。
inline void polishMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setWindowFlag(Qt::FramelessWindowHint, true);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
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
inline constexpr const char *Dim     = "#686e76";  // 弱化文字
inline constexpr const char *Accent  = "#55c0f0";  // 强调（青）
inline constexpr const char *Accent2 = "#1689bd";  // 强调深
inline constexpr const char *AccentBg = "rgba(85, 192, 240, 0.12)";
inline constexpr const char *Line    = "rgba(255, 255, 255, 0.085)";  // 细分隔
inline constexpr const char *Line2   = "rgba(255, 255, 255, 0.14)";   // 强分隔/描边
inline constexpr const char *Border  = "#4b4e54";  // 控件描边
inline constexpr const char *Danger  = "#c42b1c";  // 关闭/删除

// —— 语义色（绘制用）——
inline QColor color(const char *hex) { return QColor(QString::fromLatin1(hex)); }
inline QColor accent() { return QColor(0x55, 0xc0, 0xf0); }
inline QColor accentSoft() { return QColor(85, 192, 240, 31); }
inline QColor line() { return QColor(255, 255, 255, 22); }
inline QColor lineStrong() { return QColor(255, 255, 255, 36); }
inline QColor text() { return QColor(0xf2, 0xf2, 0xf2); }
inline QColor body() { return QColor(0xd5, 0xd7, 0xda); }
inline QColor muted() { return QColor(0x92, 0x97, 0x9e); }
inline QColor dim() { return QColor(0x68, 0x6e, 0x76); }
inline QColor field() { return QColor(0x2d, 0x2f, 0x34); }
inline QColor panel() { return QColor(0x24, 0x25, 0x28); }

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

// 12px ≈ 9pt 微软雅黑，与原型 12px 正文一致
inline QFont font(int px = 12)
{
    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPixelSize(px);
    return f;
}

// 应用级：菜单、提示、滚动条、对话框底座。由 Theme::apply(Dark) 安装。
inline QString appQss()
{
    return QStringLiteral(R"(
QDialog { background: #232427; color: #f2f2f2; }
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
QScrollBar::handle:vertical { background: rgba(255,255,255,0.16); border-radius: 4px; min-height: 28px; }
QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.26); }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: rgba(255,255,255,0.16); border-radius: 4px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: rgba(255,255,255,0.26); }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLineEdit, QPlainTextEdit, QTextEdit { background: #2d2f34; color: #e0e2e4;
    border: 1px solid #4b4e54; border-radius: 5px; padding: 0 8px; font-size: 12px;
    selection-background-color: #1689bd; }
QLineEdit { min-height: 26px; }
QLineEdit:hover, QPlainTextEdit:hover { border-color: #686d75; }
QLineEdit:focus, QPlainTextEdit:focus { border-color: #55c0f0; }
QLineEdit:disabled { color: #6b7078; }

QComboBox { background: #2d2f34; color: #e0e2e4; border: 1px solid #4b4e54;
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

QPushButton { background: #303237; color: #e1e3e5; border: 1px solid #4b4e54;
    border-radius: 6px; padding: 0 16px; min-height: 30px; font-size: 12px; }
QPushButton:hover { background: #3a3d43; border-color: #5e6269; }
QPushButton:pressed { background: #2a2c30; }
QPushButton:disabled { color: #6b7078; border-color: #3a3c40; background: #292b2f; }

QSpinBox, QDoubleSpinBox { background: #2d2f34; color: #e0e2e4; border: 1px solid #4b4e54;
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
QHeaderView::section { background: #292b2f; color: #9ca2aa; border: 0;
    border-bottom: 1px solid rgba(255,255,255,0.085); padding: 5px 8px; font-size: 11px; }
QTabWidget::pane { border: 0; }
QTabBar::tab { background: transparent; color: #92979e; padding: 7px 13px;
    border-bottom: 2px solid transparent; font-size: 12px; }
QTabBar::tab:selected { color: #55c0f0; border-bottom-color: #55c0f0; }
QTabBar::tab:hover { color: #ffffff; }
)");
}

// 对话框内部（设置 / 图片转换共用）
inline QString dialogQss()
{
    return QStringLiteral(R"(
QDialog { background: #232427; }
QWidget#dlgTitleBar { background: #25262a; border-bottom: 1px solid rgba(255,255,255,0.085); }
QLabel#dlgTitle { font-size: 13px; color: #f2f2f2; }
QWidget#dlgFooter { background: #222326; border-top: 1px solid rgba(255,255,255,0.085); }
QLabel#footerNote { color: #686e76; font-size: 11px; }

QWidget#navRail { background: #1e1f22; }
QLabel#navCaption { color: #686e76; font-size: 11px; letter-spacing: 1px; padding: 0 10px; }
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
)");
}

// 图片信息浮层
inline QString panelQss()
{
    return QStringLiteral(R"(
QWidget#infoPanel { background: rgba(35,36,39,0.98); border: 1px solid #46494f; border-radius: 9px; }
QWidget#infoHead { background: transparent; border-bottom: 1px solid rgba(255,255,255,0.085); }
QLabel#infoTitle { font-size: 13px; color: #f2f2f2; }
QPushButton#iconAction { background: transparent; border: 0; border-radius: 5px;
    color: #aeb3ba; min-width: 28px; min-height: 26px; padding: 0; }
QPushButton#iconAction:hover { background: #36393e; color: #ffffff; }
QLabel#fileName { font-size: 14px; color: #f2f2f2; }
QLabel#filePath { font-size: 11px; color: #92979e; }
QWidget#metaGroup > QLabel#groupTitle { font-size: 11px; color: #858b93; }
QLabel#metaKey { color: #8f959d; font-size: 11px; }
QLabel#metaValue { color: #d5d7da; font-size: 11px; }
QWidget#previewBox { background: #191a1d; border: 1px solid rgba(255,255,255,0.085);
    border-radius: 7px; }
)");
}

} // namespace UiStyle
