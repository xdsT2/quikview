#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ImageCache;

// 图片总览（2026-11-19 定稿；2026-11-21 按用户确认原型返工）：
// 全窗自适应网格（列数随可用宽度增长，缩略图保持原始宽高比），默认省去元数据；
// 悬停=轻底、选中=整卡圆角底板 + 蓝底文件名，键盘聚焦另给焦点环。
// 单击选中，双击 / Enter 打开，Esc 返回看图；拖浏览进度时网格跟随目标索引。
class OverviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewPanel(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    int currentIndex() const { return m_current; }

    // 打开时把当前图片定位到可视区域（只滚到可见，不强行居中）。
    void revealCurrent();
    // 验收探针：滚到内容末端（验证末排缩略图与文件名可见）。
    void scrollToEnd();
    // 验收探针：对指定格发真实双击事件（走 mouseDoubleClickEvent，不直接 emit）。
    void probeDoubleClick(int index);
    // 验收探针：按目标索引跟随（与拖进度条同一路径）并把选中格滚到可见。
    void probeFollow(int index);
    // 验收探针：当前几何摘要（列数/格宽/滚动/选中格矩形），落盘供回执核对。
    QString probeMetrics() const;

signals:
    void selected(int index);
    void activated(int index);
    void escapePressed();

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

    Metrics metrics() const;
    int rowCount() const;
    int contentHeight() const;
    int maxScroll() const;
    QRect cellRect(int index) const;
    QRect imageRect(int index) const;
    int indexAt(const QPoint &pos) const;
    void ensureVisible(int index);
    void schedulePreviews();
    void requestVisible();
    void onThumbReady(const QString &path, const QImage &image);
    void moveSelection(int dx, int dy);

    ImageCache *m_cache = nullptr;
    QStringList m_files;
    QCache<int, QImage> m_images{64};
    QHash<QString, int> m_pending;
    QSet<int> m_failed;
    int m_current = -1;
    int m_hover = -1;
    int m_scroll = 0;
    bool m_updateQueued = false;
    bool m_keyboardFocus = false;   // 最近一次交互来自键盘（焦点环只在键盘可见时出）
};
