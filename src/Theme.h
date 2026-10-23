#pragma once

#include <QApplication>
#include <QString>

// 主题：深色 / 浅色（QSS 样式表）。对照 Honeyview 的 skin 能力做轻量实现。
namespace Theme {

enum class Mode { Light, Dark };

inline void apply(QApplication &app, Mode mode)
{
    if (mode == Mode::Dark) {
        app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background: #232323; }
QMenuBar { background: #2b2b2b; color: #e0e0e0; }
QMenuBar::item:selected { background: #3c3c3c; }
QMenu { background: #2b2b2b; color: #e0e0e0; border: 1px solid #3c3c3c; }
QMenu::item:selected { background: #3c6e9e; }
QStatusBar { background: #2b2b2b; color: #a0a0a0; }
QDockWidget { color: #e0e0e0; }
QDockWidget::title { background: #2b2b2b; padding: 4px; }
QTreeWidget { background: #232323; color: #e0e0e0; border: none; }
QTreeWidget::item:selected { background: #3c6e9e; }
QPushButton { background: #3c3c3c; color: #e0e0e0; border: 1px solid #4a4a4a; padding: 4px 12px; }
QPushButton:hover { background: #4a4a4a; }
)"));
    } else {
        app.setStyleSheet(QStringLiteral(R"(
QMainWindow, QDialog { background: #f5f5f5; }
QMenuBar { background: #ececec; color: #202020; }
QMenu { background: #ffffff; color: #202020; border: 1px solid #cccccc; }
QMenu::item:selected { background: #cde3f7; }
QStatusBar { background: #ececec; color: #505050; }
QDockWidget::title { background: #ececec; padding: 4px; }
QTreeWidget { background: #ffffff; color: #202020; border: none; }
QTreeWidget::item:selected { background: #cde3f7; }
)"));
    }
}

} // namespace Theme
