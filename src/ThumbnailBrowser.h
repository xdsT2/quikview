#pragma once

#include <QCache>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QSet>
#include <QStringList>
#include <QVector>
#include <QWidget>

class ImageCache;
class QTimer;
class QVariantAnimation;

class ThumbnailBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ThumbnailBrowser(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    void setExpanded(bool expanded);
    bool isExpanded() const { return m_expanded; }
    void place(int availableWidth, int bottom);
    int currentIndex() const { return m_current; }
    bool currentInView() const;

signals:
    void activateImage(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool event(QEvent *event) override;

private:
    void rebuildPositions();
    void updateGeometryAndScroll();
    void schedulePreviews();
    void requestVisible();
    void onThumbReady(const QString &path, const QImage &image);
    void startCenterAnimation();
    int centerScrollTarget() const;
    int indexAt(int contentX) const;
    int maxScroll() const;
    QRect tileRect(int index) const;

    ImageCache *m_cache;
    QVariantAnimation *m_reveal;
    QVariantAnimation *m_center = nullptr;   // 切图自动居中（180ms，滚轮/点击/键入即断）
    QTimer *m_fadeTimer = nullptr;           // 逐格淡入 / 描边交叉淡化期间推进重绘
    QElapsedTimer m_paintClock;
    QHash<int, qint64> m_tileFade;           // index → 异步缩略图淡入起始时刻
    QStringList m_files;
    QVector<QSize> m_sizes;
    QVector<int> m_positions;
    QCache<int, QImage> m_images{48};
    QHash<QString, int> m_pending;
    QSet<int> m_failed;
    int m_availableWidth = 0;
    int m_bottom = 0;
    int m_previewHeight = 72;
    int m_scroll = 0;
    int m_current = -1;
    int m_prevCurrent = -1;                  // 描边交叉淡化的旧选中
    qint64 m_ringSwapMs = 0;                 // 选中切换时刻（m_paintClock 时基）
    int m_pressed = -1;
    int m_hover = -1;
    bool m_expanded = false;
    bool m_updateQueued = false;
    bool m_positionsDirty = false;
    bool m_centerCurrent = false;
};
