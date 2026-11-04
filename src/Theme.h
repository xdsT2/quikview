#pragma once

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QString>
#include <QStyleHints>
#include <QWidget>

#include "UiStyle.h"

// 主题：深色 / 浅色 / 跟随系统（config/theme = "dark" | "light" | "system"）。
// 深色已改为 QuikView 自有 Win11 视觉（令牌与样式全部来自 UiStyle.h），
// 浅色保留 Honeyview 风格的旧样式，仅作兜底。
// 注意：本头文件不 include Config.h（Config 属持久层），当前取值经参数传入。
namespace Theme {

enum class Mode { Light, Dark };

// 最近一次应用的原始取值（dark/light/system），供系统深浅色监听判断是否跟随。
inline QString &currentSetting()
{
    static QString s = QStringLiteral("dark");
    return s;
}

// "light" → 浅色；"system" → 按 Windows 深浅色；"dark"/未知值 → 深色（默认）。
inline Mode resolveMode(const QString &theme)
{
    if (theme == QLatin1String("light"))
        return Mode::Light;
    if (theme == QLatin1String("system"))
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light
                   ? Mode::Light
                   : Mode::Dark;
    return Mode::Dark;
}

inline void applyDark(QApplication &app)
{
    UiStyle::setScheme(UiStyle::Scheme::Dark);
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

// 浅色：2026-11-04 起为真实现。调色板 + UiStyle::appQss()（内部按 Scheme 出浅色 QSS）。
inline void applyLight(QApplication &app)
{
    UiStyle::setScheme(UiStyle::Scheme::Light);
    QPalette p;
    p.setColor(QPalette::Window, QColor(0xf2, 0xf3, 0xf5));
    p.setColor(QPalette::WindowText, QColor(0x1b, 0x1b, 0x1b));
    p.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf8, 0xf9));
    p.setColor(QPalette::Text, QColor(0x1b, 0x1b, 0x1b));
    p.setColor(QPalette::Button, QColor(0xfb, 0xfb, 0xfb));
    p.setColor(QPalette::ButtonText, QColor(0x1b, 0x1b, 0x1b));
    p.setColor(QPalette::Highlight, QColor(0x16, 0x89, 0xbd));
    p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xff));
    p.setColor(QPalette::ToolTipText, QColor(0x1b, 0x1b, 0x1b));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xa0, 0xa0, 0xa0));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0xa0, 0xa0, 0xa0));
    app.setPalette(p);

    app.setStyleSheet(UiStyle::appQss());
}

inline void apply(QApplication &app, Mode mode)
{
    // 换肤前收起全部弹出层（菜单、组合框下拉等）：QSS 换肤不会重绘已经弹出
    // 的窗口，留着会半新半旧；代价是弹出层被关闭，属可接受的一次性打断。
    // Qt 6.11 已移除 QApplication::closeAllPopups，用 activePopupWidget 循环
    // 关闭兜底（上限防异常死循环）。
    for (int guard = 0; guard < 16; ++guard) {
        QWidget *popup = app.activePopupWidget();
        if (!popup)
            break;
        popup->close();
    }

    if (mode == Mode::Dark) {
        applyDark(app);
    } else {
        applyLight(app);
    }
}

// 便捷入口：按 config/theme 的字符串解析并应用。所有调用点统一走这里，
// 同时记录取值，供 installSystemWatcher 判断「跟随系统」是否启用。
inline void applySetting(QApplication &app, const QString &theme)
{
    currentSetting() = theme;
    apply(app, resolveMode(theme));
}

// 「跟随系统」：监听 Windows 深浅色切换（Qt 6.5+ colorSchemeChanged）。
// 全程只装一次；仅当当前取值是 system 时才重新应用——手动选了深色/浅色
// 的用户不被系统切换打扰。上下文对象是 app，随应用析构自动断连。
inline void installSystemWatcher(QApplication &app)
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app,
                     [&app](Qt::ColorScheme) {
                         if (currentSetting() == QLatin1String("system"))
                             apply(app, resolveMode(QStringLiteral("system")));
                     });
}

} // namespace Theme
