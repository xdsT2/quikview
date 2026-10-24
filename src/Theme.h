#pragma once

#include <QApplication>
#include <QPalette>
#include <QString>

#include "UiStyle.h"

// 主题：深色 / 浅色。
// 深色已改为 QuikView 自有 Win11 视觉（令牌与样式全部来自 UiStyle.h），
// 浅色保留 Honeyview 风格的旧样式，仅作兜底。
namespace Theme {

enum class Mode { Light, Dark };

inline void applyDark(QApplication &app)
{
    // 原生绘制的部件（勾选符、滚动条箭头、菜单边框）跟随调色板
    QPalette p;
    p.setColor(QPalette::Window, QColor(0x23, 0x24, 0x27));
    p.setColor(QPalette::WindowText, UiStyle::text());
    p.setColor(QPalette::Base, QColor(0x1e, 0x20, 0x23));
    p.setColor(QPalette::AlternateBase, QColor(0x22, 0x24, 0x27));
    p.setColor(QPalette::Text, UiStyle::body());
    p.setColor(QPalette::Button, QColor(0x30, 0x32, 0x37));
    p.setColor(QPalette::ButtonText, UiStyle::body());
    p.setColor(QPalette::Highlight, UiStyle::accent());
    p.setColor(QPalette::HighlightedText, QColor(0x09, 0x21, 0x2c));
    p.setColor(QPalette::ToolTipBase, QColor(0x2a, 0x2c, 0x30));
    p.setColor(QPalette::ToolTipText, QColor(0xe6, 0xe8, 0xea));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x6b, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x6b, 0x70, 0x78));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x6b, 0x70, 0x78));
    app.setPalette(p);

    app.setStyleSheet(UiStyle::appQss());
}

inline void apply(QApplication &app, Mode mode)
{
    if (mode == Mode::Dark) {
        applyDark(app);
    } else {
        app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background: #f5f5f5; }
QMenuBar { background: #ececec; color: #202020; }
QMenuBar::item:selected { background: #e0e0e0; }
QMenu { background: #ffffff; color: #202020; border: 1px solid #cccccc; }
QMenu::item:selected { background: #cde3f7; }
QStatusBar { background: #ececec; color: #505050; }
QDockWidget { color: #202020; }
QDockWidget::title { background: #ececec; padding: 4px; }
QTreeWidget { background: #ffffff; color: #202020; border: none; }
QTreeWidget::item:selected { background: #cde3f7; }
QPushButton { background: #ececec; color: #202020; border: 1px solid #c4c4c4; padding: 4px 12px; }
QPushButton:hover { background: #e0e0e0; }
)"));
    }
}

} // namespace Theme
