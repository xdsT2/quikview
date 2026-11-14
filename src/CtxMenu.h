#pragma once

#include <QMenu>

// 应用内右键菜单的自绘层（右键菜单派单 2026-11-14「全量配置修订」）。
// ---------------------------------------------------------------------------
// 只接管绘制与尺寸，导航/级联/快捷键/Esc 仍走 QMenu 原生行为：
//   - 行高 30px、菜单圆角 10px、行圆角 6px、分隔线上下各 4px（几何仍由
//     全局 QSS 的 QMenu 规则给出，绘制在 actionGeometry 的行矩形内完成）；
//   - 16px 线性图标（MenuIcons）+ 13px 标签 + 11px 右对齐快捷键列（最小 76px）；
//   - 勾选用勾、单选用实心圆点，画在快捷键列左侧的独立位置；
//   - 键盘焦点 = 2px 蓝色内框（UiStyle::interactive），悬停 = 行底色；
//   - 危险行（删除）：图标危险色、悬停低饱和危险底，文字保持中性；
//   - 禁用行灰色并跳过键盘导航（QMenu 原生已跳过）。
// 显示标签/图标/快捷键通过 QAction 动态属性给定，不改动动作本体文字，
// 因此同一 QAction 挂到其他菜单（顶栏/编辑菜单）不受本设计影响。
class CtxMenu : public QMenu
{
    Q_OBJECT

public:
    explicit CtxMenu(QWidget *parent = nullptr);

    // QAction 动态属性键（组装方写入，paintEvent 读取）
    static constexpr const char *kIconProp     = "appmenu.icon";     // MenuIcons id
    static constexpr const char *kLabelProp    = "appmenu.label";    // 显示标签（覆盖 text 的 \t 前段）
    static constexpr const char *kShortcutProp = "appmenu.shortcut"; // 显示快捷键（覆盖 \t 后段/shortcut()）
    static constexpr const char *kDangerProp   = "appmenu.danger";   // 危险行
    static constexpr const char *kRadioProp    = "appmenu.radio";    // 单选（实心圆点）
    static constexpr const char *kLockProp     = "appmenu.lock";     // 标签后画固定锁（设置…）

    // 顶层最小宽 256 / 子菜单最小宽 238（规格）；更宽按最长标签+快捷键列扩。
    void setMinWidth(int w);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_minWidth = 0;
};
