#pragma once

#include <QHash>
#include <QMenu>
#include <QRect>
#include <QVector>

// 应用内右键菜单与顶栏三横菜单共用的自绘层。
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
    enum class Presentation { ContextMenu, OriginalActions };
    explicit CtxMenu(QWidget *parent = nullptr,
                     Presentation presentation = Presentation::ContextMenu);
    void setActionIcon(const QAction *action, const char *iconId);

    // QAction 动态属性键（组装方写入，paintEvent 读取）
    static constexpr const char *kIconProp     = "appmenu.icon";     // MenuIcons id
    static constexpr const char *kLabelProp    = "appmenu.label";    // 显示标签（覆盖 text 的 \t 前段）
    static constexpr const char *kShortcutProp = "appmenu.shortcut"; // 显示快捷键（覆盖 \t 后段/shortcut()）
    static constexpr const char *kDangerProp   = "appmenu.danger";   // 危险行
    static constexpr const char *kRadioProp    = "appmenu.radio";    // 单选（实心圆点）
    static constexpr const char *kLockProp     = "appmenu.lock";     // 标签后画固定锁（设置…）

    // 顶层最小宽 256 / 子菜单最小宽 238（规格）；更宽按最长标签+快捷键列扩。
    void setMinWidth(int w);

    // —— 顶部快捷区（解冻增量 2026-11-14）——
    // 与正文共用同一批 QAction 实例（快捷键/可用/勾选/回调全部同步，本类只
    // 提供图标按钮入口，不复制行为实现）。非空时在本菜单顶部保留 44px 布局
    // 空间（按钮区 2+32+5=39px + 1px 分隔 + 4px 间距），并自管悬停/按下/
    // 键盘焦点/触发/Tooltip；全部隐藏时不调用本函数，整行与下方分隔一并省略。
    void setQuickActions(const QList<QAction *> &acts);
    bool hasQuickArea() const { return !m_quick.isEmpty(); }
    // 探针支持：把键盘焦点放到快捷区第 idx 个可用按钮（-1 清除）。
    void setQuickFocus(int idx) { m_quickFocus = idx; }
    // 探针支持：快捷区按钮矩形（菜单坐标）与绑定动作。
    QRect quickButtonRect(int idx) const;
    QList<QAction *> quickActions() const { return m_quick; }
    // 与鼠标释放同一条触发路径（QAction::trigger + 关闭菜单）；公开给冒烟
    // 探针模拟「从顶部触发」，不绕过任何行为实现。
    void triggerQuick(int idx);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private:
    QString displayLabel(const QAction *action) const;
    QString displayShortcut(const QAction *action) const;
    bool hasShortcutColumn(const QList<QAction *> &actions) const;
    // 快捷区几何：按钮矩形（删除前短分隔占位）、短分隔 x、下方分隔线 y。
    // 锚点取首条正文行的 actionGeometry（QSS padding 不进 contentsMargins，
    // 以正文行布局为唯一真源，保证绘制/命中/正文三者严格一致）。
    void quickGeometry(QVector<QRect> *buttons, int *sepX, int *sepBelowY) const;
    int quickBodyTop() const;
    int quickIndexAt(const QPoint &pos) const;

    Presentation m_presentation;
    QHash<const QAction *, QString> m_actionIcons;
    int m_minWidth = 0;
    QList<QAction *> m_quick;        // 顶部快捷区绑定的真实动作（顺序=固定顺序）
    int m_quickHover = -1;           // 悬停按钮下标
    int m_quickPressed = -1;         // 按下按钮下标
    int m_quickFocus = -1;           // 键盘焦点按钮下标
};
