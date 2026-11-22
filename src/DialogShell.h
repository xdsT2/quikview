#pragma once

// 二级界面统一外壳（Fluent 二级界面第 4 批 · 弹窗，2026-11-22）
// ---------------------------------------------------------------------------
// 图片转换 / Gamma 补偿 / 编辑书签三个自绘弹窗此前各自实现了一份
// 「无边框 + 圆角 + 描边 + 标题栏拖拽」，底色还各写各的裸色（Panel / Border /
// DialogBg），浅色主题下会露出一块深色板。这里收成一处：
//   · 窗口形态：Qt::Dialog + 无边框 + WA_TranslucentBackground；
//   · 外壳绘制：底色 UiStyle::fluent::contentBg()、描边 fluent::cardLine()，
//     与设置窗 paintEvent 同源，深浅主题自动跟随；
//   · 标题栏拖拽：统一在过滤器里按 objectName("dlgTitleBar") 识别，子控件
//     吞掉鼠标事件也不影响；
//   · 样式表：UiShell::shellQss() = UiStyle::dialogQss() + 透明底覆盖，
//     标题栏/底栏/按钮的观感全部来自 dialogQss 的 @Gen*@ 令牌。
// 只做骨架，不引入新控件形态；具体内容仍由各对话框自己 buildUi。
// ---------------------------------------------------------------------------

#include <QDialog>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWidget>

#include "UiStyle.h"

namespace UiShell {

class DialogShell : public QDialog
{
public:
    explicit DialogShell(QWidget *parent = nullptr)
        : QDialog(parent)
    {
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setStyleSheet(shellQss());
        setFont(UiStyle::font(12));
    }

    // 统一外壳样式：dialogQss() 的 QDialog 底色是给「有自绘外壳」的对话框用的，
    // 无边框窗口必须让 QDialog 本身透明，圆角才不会露出方角底色。
    static QString shellQss()
    {
        return UiStyle::dialogQss() + QStringLiteral("QDialog { background: transparent; }");
    }

    // 标题栏拖拽：把标题栏控件交给外壳的过滤器（子控件会先吃掉鼠标事件）。
    void attachTitleBar(QWidget *bar)
    {
        if (bar) {
            bar->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched && watched->objectName() == QLatin1String("dlgTitleBar")) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto *me = static_cast<QMouseEvent *>(event);
                if (me->button() == Qt::LeftButton) {
                    m_dragOffset = me->globalPosition().toPoint() - frameGeometry().topLeft();
                    return true;
                }
            } else if (event->type() == QEvent::MouseMove) {
                auto *me = static_cast<QMouseEvent *>(event);
                if (me->buttons() & Qt::LeftButton) {
                    move(me->globalPosition().toPoint() - m_dragOffset);
                    return true;
                }
            }
        }
        return QDialog::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                            UiStyle::RadiusDialog, UiStyle::RadiusDialog);
        p.fillPath(path, UiStyle::color(UiStyle::fluent::contentBg()));
        p.setPen(QPen(UiStyle::color(UiStyle::fluent::cardLine()), 1));
        p.drawPath(path);
    }

private:
    QPoint m_dragOffset;
};

} // namespace UiShell
