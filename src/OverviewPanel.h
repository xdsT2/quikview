#pragma once

#include <QCache>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ImageCache;

// 图片总览（2026-11-19 定稿）：全窗网格、保持原始宽高比、默认省去元数据，
// 选中项在缩略图下方显示文件名。单击选中，双击 / Enter 打开，Esc 返回看图。
class OverviewPanel : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewPanel(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    int currentIndex() const { return m_current; }

    // 打开时把当前图片定位到可视区域（选中并居中）。
    void revealCurrent();
    // 验收探针：滚到内容末端（验证末排缩略图与文件名可见）。
    void scrollToEnd();
    // 验收探针：对指定格发真实双击事件（走 mouseDoubleClickEvent，不直接 emit）。
    void probeDoubleClick(int index);

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
    bool event(QEvent *event) override;

private:
    struct Metrics {
        int cols = 5;
        int cellW = 176;
        int cellH = 148;
        int marginX = 32;
        int gapX = 24;
        int gapY = 30;
        int top = 54;        // 顶栏 38 + 呼吸 16
        int imageH = 118;    // 单元格内图片最大高度
        int bottomReserve = 96;   // 底栏与状态行
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
};
