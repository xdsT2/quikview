#pragma once

#include <QFrame>
#include <QHash>
#include <QVector>
#include <QString>
#include "MenuCatalog.h"

class QTimer;
class QLabel;
class QPaintEvent;
class QMouseEvent;
class QKeyEvent;
class QShowEvent;
class QHideEvent;
class QFocusEvent;
class QResizeEvent;

// 查看器右键菜单 · 设置页常驻一级预览 + 浮动二级（实装派单 2026-11-16）。
// ---------------------------------------------------------------------------
// 职责与边界：
//  · 只做「当前可见两级菜单」的可视呈现与两级内的交互，不改动任何配置；
//  · 树结构与可见性全部来自 Config::appMenuVisible + MenuCatalog，开关改动后
//    由 SettingsDialog 调用 setModel() 重建；运行态（勾选/互斥单选/禁用/有无图片）
//    来自 MainWindow 推送的 AppMenuRunState 快照（见 SettingsDialog.h）；
//  · 一级列本 widget 常驻；二级以独立顶层浮层弹出（不受设置内容裁剪），
//    打开方向/翻转/上移按屏幕可用空间自适应，见 refreshPopupGeometry()；
//  · 预览只做定位，永不执行命令：叶子点击/Enter/Space 仅把大纲对应行带进视野，
//    分组行点击/→ 打开二级浮层；Esc 先关二级再退出焦点；点击外部关二级留一级。
//  · 预览省略顶部快捷区（打开/文件夹/复制/信息/删除），只显示正文两级。
//  · 一级行空态显示「当前没有可显示的菜单项」。
class AppMenuPreview : public QFrame {
    Q_OBJECT
public:
    // 一级行数据（由 SettingsDialog 按当前可见性生成；结构 / 文案/图标/快捷键
    // 来自 MenuCatalog，运行态并入）。isGroup = 有可展开的子项。
    struct Row {
        QString id;            // stable 菜单 id（定位 / 运行态查表用）
        QString label;         // 显示名（已按 labelSource 档位解析）
        QString icon;          // MenuIcons id
        QString shortcut;      // 显示快捷键（AppMenu::shortcut），可空
        bool danger = false;   // 危险操作（红字）
        bool isGroup = false;  // 此行为父级（→ 展开二级）
        bool separator = false; // 在行顶画 1px 分段线（一段的首行）
        bool checked = false;  // 勾选（布尔）
        bool radioOn = false;  // 互斥单选点亮（iconCheck 点）
        bool radioOff = false; // 互斥单选熄灭（空圆圈）
        bool enabled = true;   // 命令禁用（无图等）
    };

    explicit AppMenuPreview(QWidget *parent = nullptr);

    // 重建一级模型并重绘。children 为「分组 id → 二级行」，与一级同批生成，
    // 保证预览与实际菜单同源；expandedId 为当前打开的二级组 id（仍可见则保留）。
    void setModel(const QVector<Row> &level1,
                  const QHash<QString, QVector<Row>> &children,
                  const QString &expandedId = QString());

    // 大纲 → 预览 双向定位：大纲选中某行时调用，把对应一级行/已开集高亮。
    // expand 为 true 时自动打开该 id 所在分组（供大纲叶子 → 预览定位）。
    void highlightFromOutline(const QString &id, bool expand = false);

    // 由 SettingDialog 在组件创建后注入：把某个菜单 id 带进大纲视野（滚动+聚焦）。
    void setLocalizeOutlineFn(std::function<void(const QString &)> fn);

    // 让外部程序化打开/关闭二级（测试/键盘共用）；expand 空串关闭。
    void setExpanded(const QString &groupId);

    QString expandedId() const { return m_expandedId; }

    // 键盘：给出当前焦点（预览 / 二级路由用）。返回 true 表示已消费。
    bool handleKey(int key, Qt::KeyboardModifiers mods);

    struct ProbeInfo { QString activeId; QString expandedId; };  // 探针：当前定位/分组
    ProbeInfo probeInfo() const { return ProbeInfo{ m_activeId, m_expandedId }; }

    QSize sizeHint() const override;

signals:
    // 一级分组打开变化（用于“打开二级时滚动大纲到对应分组”的依附提示）。
    void expandedChanged(const QString &groupId);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool event(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Geom {
        int topPad = 6;
        int bottomPad = 6;
        int rowH = 30;
        QRect menuSurface;              // 含圆角的整块表面区
        QVector<QRectF> rowRects;      // 与 m_rows 对齐；越界 = 空态行
    };
    void rebuildGeometry();
    int rowAt(const QPoint &pos) const;
    void repaintActive();               // 只重绘画到激活行的区域
    void openPopupAt(int row);          // 由分组行打开二级浮层（先算好位置）
    void closePopupKeepActive();        // 关二级，保留一级焦点
    void closePopup();                  // 关二级并让出焦点
    void moveActiveUpDown(int delta);
    void activeToChild();               // →：光标分组开二级，叶子则定位
    void activeToParent();              // ←：二级缩回 / 定位父分组
    void activateCurrent();             // Enter/Space：分组开二级，叶子定位
    void relocalize(const QString &id); // 定位：滚动大纲+（关二级）
    QRect popupAnchorRect() const;      // 二级锚点（当前分组行的右侧）

    // —— 内部二级浮层（独立顶层座位，menuSurface 同款外观）——
    class Popup;
    Popup *m_popup = nullptr;

    QVector<Row> m_rows;
    QHash<QString, QVector<Row>> m_children;   // 一级分组 id → 二级行
    QString m_expandedId;               // 当前打开的二级组 id
    QString m_activeId;                 // 一级当前定位行（聚焦/键盘）
    QString m_hoverId;                  // 鼠标悬停行
    bool m_popupClosing = false;        // 主动关浮层时抑制 Hide 回调
    QFont m_labelFont;
    QFont m_shortcutFont;
    Geom m_geom;
    bool m_needRebuild = true;
    std::function<void(const QString &)> m_localizeOutline;
};

// 二级浮层：Qt::Tool 顶层、无边框、menuSurface 配色，按各分组实时构建。
// 只负责二级行呈现（同款图标/快捷键/勾选/单选/禁用/危险/分组内分隔），回调
// 定位一级/大纲；由 AppMenuPreview 持有并在几何变化时移动。关闭语义在
// AppMenuPreview::event() 统一处理（点击外部 / Esc）。
class AppMenuPreview::Popup : public QWidget {
    Q_OBJECT
public:
    using Row = AppMenuPreview::Row;   // 与一级行同构，直接复用
    explicit Popup(AppMenuPreview *owner);
    void setRows(const QVector<Row> &rows);
    const QVector<Row> &rows() const { return m_rows; }
    int rowAt(const QPoint &pos) const;
    QString activeId() const { return m_activeId; }
    void setActiveId(const QString &id);
    bool handleKey(int key, Qt::KeyboardModifiers mods);
    QRect rowRect(int i) const;
    qsizetype rowCount() const { return m_rows.size(); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void rebuildGeom();
    void repaintRow(int i);
    AppMenuPreview *m_owner;
    QVector<Row> m_rows;
    QString m_activeId;
    QRect m_surface;      // 含圆角表面
    QVector<QRect> m_rowsRect;
    QFont m_labelFont, m_shortcutFont;
};