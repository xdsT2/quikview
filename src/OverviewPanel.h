#pragma once

#include <QCache>
#include <QDate>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "ImageFacts.h"

class ImageCache;

// 图片总览（2026-11-19 定稿；2026-11-21 按用户确认原型返工；2026-11-22 增加分组）：
// 全窗自适应网格（列数随可用宽度增长，缩略图保持原始宽高比），默认省去元数据；
// 悬停=轻底、选中=整卡圆角底板 + 蓝底文件名，键盘聚焦另给焦点环。
// 单击选中，双击 / Enter 打开，Esc 返回看图；拖浏览进度时网格跟随目标索引。
// 分组：不分组（目录浏览顺序）/ 按日期（拍摄日历日）/ 按图片大小（原始总像素），
// 组标题用小字 + 组间留白；组内同值保持目录相对顺序，缺日期 / 未知大小始终置底。
class OverviewPanel : public QWidget
{
    Q_OBJECT
public:
    // 菜单三项；None 用目录原有浏览顺序。
    enum class GroupMode { None, Date, Size };

    explicit OverviewPanel(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    int currentIndex() const { return m_current; }

    // —— 分组（2026-11-22 派单）——
    // 点菜单语义：点当前分组项 = 反转方向；点另一项 = 切过去并用该项初始方向
    // （日期从新到旧、大小从大到小）。不分组没有方向可翻。
    void selectGroupMode(GroupMode mode);
    void setGroupMode(GroupMode mode, bool ascending);   // 直设（探针 / 复位用）
    void resetGrouping();                               // 每次打开总览的默认「不分组」
    GroupMode groupMode() const { return m_mode; }
    bool groupAscending() const { return m_ascending; }
    QString groupDirectionText() const;                 // 菜单右侧的「从新到旧」等
    static QString groupTitle(GroupMode mode);          // 菜单行的名称（已翻译）

    // 显示顺序映射：总览内页码 / 滑条 / 左右翻页全部按可见位置走，
    // 打开图片仍按真实文件下标（currentIndex / fileAtPosition）。
    int positionOf(int fileIndex) const;
    int fileAtPosition(int position) const;
    int positionCount() const { return m_order.size(); }
    int currentPosition() const { return positionOf(m_current); }
    // 按可见顺序平移选中（底栏左右翻页）；到边界返回 false，调用方给提示。
    bool stepPosition(int delta);

    // 打开时把当前图片定位到可视区域（只滚到可见，不强行居中）。
    void revealCurrent();
    // 验收探针：滚到内容末端（验证末排缩略图与文件名可见）。
    void scrollToEnd();
    // 验收探针：对指定格发真实双击事件（走 mouseDoubleClickEvent，不直接 emit）。
    void probeDoubleClick(int index);
    // 验收探针：按目标索引跟随（与拖进度条同一路径）并把选中格滚到可见。
    void probeFollow(int index);
    // 验收探针：当前几何摘要（列数/格宽/滚动/选中格矩形/分组），落盘供回执核对。
    QString probeMetrics() const;

signals:
    void selected(int index);
    void activated(int index);
    void escapePressed();
    // 显示顺序改变（切换分组 / 反转方向 / 换目录 / 首末变化）：底栏页码需重算。
    void orderChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    bool event(QEvent *event) override;

private:
    struct Metrics {
        int cols = 5;
        int cellW = 176;
        int cellH = 154;
        int imageH = 116;       // 单元格内图片最大高度（原型 113 档）
        int marginX = 32;       // 内容左右内边距（原型 clamp(20, 4.2%, 96)）
        int gridX = 32;         // 网格左边界（余量居中，左右留白一致）
        int gapX = 18;
        int gapY = 26;
        int top = 54;           // 顶栏 38 + 呼吸 16
        int nameH = 22;         // 文件名行（默认隐藏，但位置长驻，悬停不跳格）
        int cardPadX = 10;
        int cardPadTop = 10;
        int cardPadBottom = 6;
        int bottomReserve = 96; // 底栏与状态行
    };

    // 一组连续位置：分组时带标题（小字），不分组时标题为空、只占一段网格。
    struct GroupRun {
        QString title;
        QRect head;         // 内容坐标
        int firstPos = 0;
        int count = 0;
        int gridTop = 0;    // 首行单元格顶边（内容坐标）
        int gridBottom = 0; // 末行单元格底边
    };
    // 布局只有一份缓存：位置→单元格（内容坐标）+ 组标题，宽度/顺序/分组变化才重建。
    struct Layout {
        QVector<QRect> cells;       // 按位置索引
        QVector<GroupRun> groups;
        int contentH = 0;
    };

    Metrics metrics() const;
    const Layout &layout() const;
    void rebuildLayout() const;
    void rebuildOrder();
    void invalidateLayout() { m_layoutDirty = true; }
    int contentHeight() const;
    int maxScroll() const;
    QRect cellRect(int position) const;      // 屏幕坐标（已减 m_scroll）
    QRect imageRect(int position) const;
    int positionAt(const QPoint &pos) const;
    void ensureVisiblePosition(int position);
    void ensureVisible(int fileIndex);
    void schedulePreviews();
    void requestVisible();
    void onThumbReady(const QString &path, const QImage &image);
    void moveSelection(int dx, int dy);

    ImageCache *m_cache = nullptr;
    QStringList m_files;
    QCache<int, QImage> m_images{64};
    QHash<QString, int> m_pending;
    QSet<int> m_failed;
    int m_current = -1;        // 文件下标（与目录一致，打开图片沿用它）
    int m_hover = -1;          // 可见位置
    int m_scroll = 0;
    bool m_updateQueued = false;
    bool m_keyboardFocus = false;   // 最近一次交互来自键盘（焦点环只在键盘可见时出）

    // 分组状态：方向按组保存——切到另一组时回到该组初始方向，再点当前项才反转。
    GroupMode m_mode = GroupMode::None;
    bool m_ascending = false;       // false = 日期从新到旧 / 大小从大到小
    QVector<int> m_order;           // 位置 → 文件下标
    QVector<int> m_posOf;           // 文件下标 → 位置
    QVector<QString> m_groupTitles; // 与 m_groupCounts 一一对应
    QVector<int> m_groupCounts;
    mutable Layout m_layout;
    mutable bool m_layoutDirty = true;
    mutable QHash<QString, ImageFacts> m_facts;   // 跨分组切换复用，避免重复读头部
};
