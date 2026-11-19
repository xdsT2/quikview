#include "OverviewPanel.h"

#include <QFileInfo>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>

#include "UiStyle.h"
#include "ImageCache.h"

namespace {
constexpr int kTileRadius = 8;
constexpr int kHaloRadius = 10;
constexpr int kMaxPending = 8;
}

OverviewPanel::OverviewPanel(ImageCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
{
    setObjectName(QStringLiteral("overviewPanel"));
    setAccessibleName(tr("图片总览"));
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    connect(m_cache, &ImageCache::thumbnailReady, this, &OverviewPanel::onThumbReady);
}

void OverviewPanel::setFiles(const QStringList &files, int currentIndex)
{
    if (m_files != files) {
        m_files = files;
        m_images.clear();
        for (int &index : m_pending)
            index = -1;
        m_failed.clear();
        m_hover = -1;
    }
    setCurrent(currentIndex);
    if (m_files.isEmpty())
        m_scroll = 0;
    update();
}

void OverviewPanel::setCurrent(int index)
{
    const int next = index >= 0 && index < m_files.size() ? index : -1;
    if (m_current == next)
        return;
    m_current = next;
    update();
}

void OverviewPanel::revealCurrent()
{
    ensureVisible(m_current);
    update();
}

void OverviewPanel::ensureVisible(int index)
{
    if (index < 0)
        return;
    const QRect cell = cellRect(index);
    const int visibleH = height() - metrics().top - metrics().bottomReserve;
    if (cell.top() < metrics().top)
        m_scroll -= metrics().top - cell.top();
    else if (cell.bottom() > metrics().top + visibleH)
        m_scroll += cell.bottom() - (metrics().top + visibleH);
    m_scroll = qBound(0, m_scroll, maxScroll());
}

int OverviewPanel::rowCount() const
{
    if (m_files.isEmpty())
        return 0;
    return (m_files.size() + metrics().cols - 1) / metrics().cols;
}

int OverviewPanel::contentHeight() const
{
    const Metrics m = metrics();
    const int rows = rowCount();
    if (rows <= 0)
        return 0;
    return m.top + rows * m.cellH + (rows - 1) * m.gapY + m.bottomReserve;
}

int OverviewPanel::maxScroll() const
{
    return qMax(0, contentHeight() - height());
}

OverviewPanel::Metrics OverviewPanel::metrics() const
{
    Metrics m;
    m.marginX = qBound(20, width() / 30, 40);
    m.gapX = 22;
    m.gapY = 26;
    // 目标单元格 176 宽；窄窗至少 3 列，宽窗至多 6 列。
    int cols = (width() - 2 * m.marginX + m.gapX) / (176 + m.gapX);
    m.cols = qBound(3, cols, 6);
    m.cellW = qMax(80, (width() - 2 * m.marginX - (m.cols - 1) * m.gapX) / m.cols);
    m.imageH = qBound(84, qRound(m.cellW * 0.70), 132);
    m.cellH = m.imageH + 30;   // 图片 + 文件名行
    m.top = 54;                // 顶栏 38 + 呼吸 16
    m.bottomReserve = 96;      // 底栏与底部状态行
    return m;
}

QRect OverviewPanel::cellRect(int index) const
{
    if (index < 0 || index >= m_files.size())
        return QRect();
    const Metrics m = metrics();
    const int row = index / m.cols;
    const int col = index % m.cols;
    return QRect(m.marginX + col * (m.cellW + m.gapX),
                 m.top + row * (m.cellH + m.gapY) - m_scroll,
                 m.cellW, m.cellH);
}

QRect OverviewPanel::imageRect(int index) const
{
    const QRect cell = cellRect(index);
    if (cell.isNull())
        return QRect();
    return QRect(cell.x(), cell.y(), cell.width(), metrics().imageH);
}

int OverviewPanel::indexAt(const QPoint &pos) const
{
    for (int i = 0; i < m_files.size(); ++i) {
        if (cellRect(i).contains(pos))
            return i;
    }
    return -1;
}

void OverviewPanel::schedulePreviews()
{
    if (!isVisible() || m_updateQueued)
        return;
    m_updateQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_updateQueued = false;
        if (isVisible())
            requestVisible();
    });
}

void OverviewPanel::requestVisible()
{
    if (m_files.isEmpty())
        return;
    const Metrics m = metrics();
    const int top = m_scroll;
    const int bottom = m_scroll + height();
    const QSize decode(qMin(420, m.cellW * 2), qMin(320, m.imageH * 2));
    for (int i = 0; i < m_files.size() && m_pending.size() < kMaxPending; ++i) {
        const QRect cell = cellRect(i);
        if (cell.bottom() < top || cell.top() > bottom)
            continue;
        const QString &path = m_files.at(i);
        if (m_images.contains(i) || m_failed.contains(i) || m_pending.contains(path))
            continue;
        m_pending.insert(path, i);
        m_cache->requestThumbnail(path, decode);
    }
}

void OverviewPanel::onThumbReady(const QString &path, const QImage &image)
{
    const auto it = m_pending.find(path);
    if (it == m_pending.end())
        return;
    const int index = it.value();
    m_pending.erase(it);
    if (index >= 0) {
        if (image.isNull())
            m_failed.insert(index);
        else
            m_images.insert(index, new QImage(image));
        update();
    }
    schedulePreviews();
}

void OverviewPanel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.fillRect(rect(), QColor(QString::fromLatin1(UiStyle::fluent::navBg())));

    const Metrics m = metrics();
    const QColor body = UiStyle::body();
    const QColor muted = UiStyle::muted();

    // 网格绘制裁到底栏上方，避免半排缩略图从悬浮栏底下穿出。
    const int clipBottom = height() - 64;
    p.save();
    p.setClipRect(QRect(0, 0, width(), qMax(0, clipBottom)));
    for (int i = 0; i < m_files.size(); ++i) {
        const QRect cell = cellRect(i);
        if (cell.bottom() < 0 || cell.top() > height() || cell.top() >= clipBottom)
            continue;
        const QRect area = imageRect(i);
        const bool selected = i == m_current;
        const bool hovered = i == m_hover;

        // 缩略图按原始宽高比等比放进单元格，不拉伸。
        QRect fitted = area.adjusted(6, 4, -6, -4);
        if (const QImage *image = m_images.object(i)) {
            const QSize scaled = image->size().scaled(fitted.size(), Qt::KeepAspectRatio);
            fitted = QRect(fitted.center().x() - scaled.width() / 2,
                           fitted.center().y() - scaled.height() / 2,
                           scaled.width(), scaled.height());
        }

        if (selected || hovered) {
            QColor halo = selected
                              ? QColor(QString::fromLatin1(UiStyle::pick("#2f3033", "#e4e5e7")))
                              : QColor(QString::fromLatin1(UiStyle::pick("#26272a", "#efeff0")));
            p.setPen(Qt::NoPen);
            p.setBrush(halo);
            const QRect h = fitted.adjusted(-8, -8, 8, 8)
                                .intersected(QRect(cell.x() - 2, cell.y() - 2,
                                                   cell.width() + 4, cell.height()));
            p.drawRoundedRect(h, kHaloRadius, kHaloRadius);
        }

        QPainterPath clip;
        clip.addRoundedRect(fitted, kTileRadius, kTileRadius);
        p.save();
        p.setClipPath(clip);
        if (const QImage *image = m_images.object(i)) {
            p.drawImage(fitted, *image);
        } else {
            p.fillPath(clip, QColor(QString::fromLatin1(UiStyle::pick("#232426", "#e6e7e9"))));
            if (m_failed.contains(i)) {
                p.setPen(muted);
                p.setFont(UiStyle::font(11));
                p.drawText(fitted, Qt::AlignCenter, tr("无法预览"));
            }
        }
        p.restore();

        if (selected) {
            p.setPen(QPen(UiStyle::accent(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(fitted).adjusted(-1, -1, 1, 1),
                              kTileRadius + 1, kTileRadius + 1);
            // 选中项显示文件名（默认省去元数据），居中在图片下方。
            const QRect nameRect = QRect(cell.x(), area.bottom() + 2, cell.width(), 24);
            p.setPen(body);
            p.setFont(UiStyle::font(11));
            const QString name = QFileInfo(m_files.at(i)).fileName();
            p.drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(name, Qt::ElideMiddle, nameRect.width() - 12));
        }
    }
    p.restore();

    // 左下状态行：图片总览 · N 张
    p.setPen(muted);
    p.setFont(UiStyle::font(11));
    p.drawText(QRect(m.marginX, height() - 30, width() - 2 * m.marginX, 20),
               Qt::AlignLeft | Qt::AlignVCenter,
               tr("图片总览 · %1 张").arg(m_files.size()));

    schedulePreviews();
}

void OverviewPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int index = indexAt(event->position().toPoint());
    if (index >= 0) {
        setCurrent(index);
        emit selected(index);
    }
    event->accept();
}

void OverviewPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && index >= 0) {
        setCurrent(index);
        emit activated(index);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OverviewPanel::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void OverviewPanel::leaveEvent(QEvent *event)
{
    m_hover = -1;
    update();
    QWidget::leaveEvent(event);
}

void OverviewPanel::wheelEvent(QWheelEvent *event)
{
    const QPoint pixels = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const int delta = !pixels.isNull() ? (pixels.y() ? pixels.y() : pixels.x())
                                       : (angle.y() ? angle.y() : angle.x()) / 2;
    m_scroll = qBound(0, m_scroll - delta, maxScroll());
    schedulePreviews();
    update();
    event->accept();
}

void OverviewPanel::moveSelection(int dx, int dy)
{
    if (m_files.isEmpty())
        return;
    const int cols = metrics().cols;
    int index = m_current < 0 ? 0 : m_current;
    if (dx != 0)
        index = qBound(0, index + dx, m_files.size() - 1);
    if (dy != 0)
        index = qBound(0, index + dy * cols, m_files.size() - 1);
    if (index != m_current) {
        setCurrent(index);
        emit selected(index);
    }
    ensureVisible(index);
    update();
}

void OverviewPanel::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        emit escapePressed();
        event->accept();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_current >= 0)
            emit activated(m_current);
        event->accept();
        return;
    case Qt::Key_Left:
        moveSelection(-1, 0);
        event->accept();
        return;
    case Qt::Key_Right:
        moveSelection(1, 0);
        event->accept();
        return;
    case Qt::Key_Up:
        moveSelection(0, -1);
        event->accept();
        return;
    case Qt::Key_Down:
        moveSelection(0, 1);
        event->accept();
        return;
    case Qt::Key_Home:
        if (!m_files.isEmpty()) {
            setCurrent(0);
            emit selected(0);
            ensureVisible(0);
        }
        event->accept();
        return;
    case Qt::Key_End:
        if (!m_files.isEmpty()) {
            setCurrent(m_files.size() - 1);
            emit selected(m_files.size() - 1);
            ensureVisible(m_files.size() - 1);
        }
        event->accept();
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}

void OverviewPanel::resizeEvent(QResizeEvent *event)
{
    m_scroll = qBound(0, m_scroll, maxScroll());
    QWidget::resizeEvent(event);
}

bool OverviewPanel::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const int index = indexAt(help->pos());
        if (index >= 0)
            QToolTip::showText(help->globalPos(), QFileInfo(m_files.at(index)).fileName(), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}
