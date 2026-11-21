#include "OverviewPanel.h"

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetrics>
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
// 网格与卡片数值来自用户确认原型（图片总览-用户确认网格原型.html）：
// 列最小 145px、列间距 18px、行间距 26px、瓦片高 155px 档、缩略图最大高 113px、
// 卡片圆角 11px、缩略图圆角 5px。这里按 Qt 逻辑像素落地，间距/高度取整即可对齐。
constexpr int kMinCellW = 145;
constexpr int kTileRadius = 10;
constexpr int kThumbRadius = 5;
constexpr int kNameChipRadius = 5;
constexpr int kMaxPending = 8;

// 缩略图内边距（图片与卡片边缘之间的呼吸），与原型 .qv-tile 的 padding 同量级。
constexpr int kThumbPadX = 0;
constexpr int kThumbPadY = 2;
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

void OverviewPanel::scrollToEnd()
{
    m_scroll = maxScroll();
    update();
}

// 目标卡超出可视带就朝对应方向滚到「整卡可见」，仍在带内则一像素都不动
// ——拖浏览进度时既跟手，又不会每拖一格就把网格重新居中。
void OverviewPanel::ensureVisible(int index)
{
    if (index < 0)
        return;
    const Metrics m = metrics();
    const QRect cell = cellRect(index);
    const int bandTop = m.top;
    const int bandBottom = height() - m.bottomReserve;
    if (cell.bottom() > bandBottom)
        m_scroll += cell.bottom() - bandBottom;
    else if (cell.top() < bandTop)
        m_scroll += cell.top() - bandTop;
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
    // 原型：padding-inline: clamp(20px, 4.2%, 96px)；gap: 26px 18px；
    // grid-template-columns: repeat(auto-fill, minmax(145px, 1fr))。
    // 列数不再封顶 6 列——宽窗按 145px 最小格宽继续加列，避免把六列拉散留白。
    m.marginX = qBound(20, qRound(width() * 0.042), 96);
    m.gapX = 18;
    m.gapY = 26;
    const int avail = qMax(0, width() - 2 * m.marginX);
    int cols = (avail + m.gapX) / (kMinCellW + m.gapX);
    m.cols = qMax(1, cols);
    m.cellW = qMax(72, (avail - (m.cols - 1) * m.gapX) / m.cols);
    // 缩略图高度随格宽走（原型 146 宽对 110 高 ≈ 0.75），下限保住窄窗可读性，
    // 上限 146 防止超宽显示器把缩略图无限放大。
    m.imageH = qBound(80, qRound(m.cellW * 0.75), 146);
    m.cardPadX = 10;
    m.cardPadTop = 10;
    m.cardPadBottom = 6;
    m.nameH = 22;
    m.cellH = m.imageH + m.cardPadTop + m.nameH + m.cardPadBottom;
    // 除不尽的像素均分到左右留白，避免右侧多出一条缝。
    const int blockW = m.cols * m.cellW + (m.cols - 1) * m.gapX;
    m.gridX = m.marginX + qMax(0, (avail - blockW) / 2);
    m.top = 54;             // 顶栏 38 + 呼吸 16
    m.bottomReserve = 96;   // 底栏与底部状态行
    return m;
}

QRect OverviewPanel::cellRect(int index) const
{
    if (index < 0 || index >= m_files.size())
        return QRect();
    const Metrics m = metrics();
    const int row = index / m.cols;
    const int col = index % m.cols;
    return QRect(m.gridX + col * (m.cellW + m.gapX),
                 m.top + row * (m.cellH + m.gapY) - m_scroll,
                 m.cellW, m.cellH);
}

QRect OverviewPanel::imageRect(int index) const
{
    const QRect cell = cellRect(index);
    if (cell.isNull())
        return QRect();
    const Metrics m = metrics();
    return QRect(cell.x() + m.cardPadX,
                 cell.y() + m.cardPadTop,
                 qMax(1, cell.width() - 2 * m.cardPadX),
                 qMax(1, m.imageH));
}

int OverviewPanel::indexAt(const QPoint &pos) const
{
    const Metrics m = metrics();
    const int relX = pos.x() - m.gridX;
    const int relY = pos.y() - m.top + m_scroll;
    if (relX < 0 || relY < 0)
        return -1;
    const int hStride = m.cellW + m.gapX;
    const int vStride = m.cellH + m.gapY;
    const int col = relX / hStride;
    const int row = relY / vStride;
    if (relX - col * hStride >= m.cellW || relY - row * vStride >= m.cellH)
        return -1;   // 落在格间距里：不算命中，悬停不闪
    if (col >= m.cols)
        return -1;
    const int index = row * m.cols + col;
    return index >= 0 && index < m_files.size() ? index : -1;
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
    // 注意：cellRect() 返回的是「屏幕坐标」（已减去 m_scroll），
    // 这里必须拿 0..height() 的视口范围比较；早期用 m_scroll..m_scroll+height()
    // 当内容坐标去比，滚动后的行会被整段跳过，缩略图永远停在占位灰块。
    const int top = -160;              // 视口上方少量预取
    const int bottom = height() + 160; // 视口下方少量预取
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
    p.fillRect(rect(), UiStyle::overview::canvas());

    const Metrics m = metrics();
    const QColor body = UiStyle::body();
    const QColor muted = UiStyle::muted();
    const QFont nameFont = UiStyle::font(13);

    // 网格绘制裁到底栏上方，避免半排缩略图从悬浮栏底下穿出。
    const int clipBottom = height() - 64;
    p.save();
    p.setClipRect(QRect(0, 0, width(), qMax(0, clipBottom)));
    for (int i = 0; i < m_files.size(); ++i) {
        const QRect cell = cellRect(i);
        if (cell.bottom() < 0 || cell.top() >= clipBottom)
            continue;
        // 顶边被顶栏压住的部分照样要画（上滚时能看到半张卡），只跳过完全离屏的。
        const QRect area = imageRect(i);
        const bool selected = i == m_current;
        const bool hovered = i == m_hover;

        // 整卡底板：悬停轻底，选中实底——不是只沿缩略图像素边缘围一圈线。
        if (selected || hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? UiStyle::overview::cardSelected()
                                : UiStyle::overview::cardHover());
            p.drawRoundedRect(cell, kTileRadius, kTileRadius);
        }

        // 缩略图按原始宽高比等比放进卡片图片区，不拉伸。
        QRect fitted = area.adjusted(kThumbPadX, kThumbPadY, -kThumbPadX, -kThumbPadY);
        if (const QImage *image = m_images.object(i)) {
            const QSize scaled = image->size().scaled(fitted.size(), Qt::KeepAspectRatio);
            fitted = QRect(fitted.center().x() - scaled.width() / 2,
                           fitted.center().y() - scaled.height() / 2,
                           scaled.width(), scaled.height());
        }

        QPainterPath clip;
        clip.addRoundedRect(QRectF(fitted), kThumbRadius, kThumbRadius);
        p.save();
        p.setClipPath(clip);
        if (const QImage *image = m_images.object(i)) {
            p.drawImage(fitted, *image);
        } else {
            p.fillPath(clip, UiStyle::overview::thumbPlaceholder());
            if (m_failed.contains(i)) {
                p.setPen(muted);
                p.setFont(UiStyle::font(12));
                p.drawText(fitted, Qt::AlignCenter, tr("无法预览"));
            }
        }
        p.restore();

        // 文件名默认隐藏；悬停、选中与键盘聚焦（选中项在键盘焦点的这次会话里）才出现。
        const bool showName = selected || hovered;
        if (showName) {
            const QRect nameRect(cell.x() + m.cardPadX,
                                 cell.y() + m.cardPadTop + m.imageH + 1,
                                 qMax(1, cell.width() - 2 * m.cardPadX),
                                 m.nameH - 1);
            const QString name = QFileInfo(m_files.at(i)).fileName();
            p.setFont(nameFont);
            const QFontMetrics fm(nameFont);
            if (selected) {
                // 图二口径：选中文件名是蓝底白字的独立小底板，不是普通文字。
                const QString elided = fm.elidedText(name, Qt::ElideMiddle,
                                                     qMax(1, nameRect.width() - 16));
                const int textW = fm.horizontalAdvance(elided);
                const QRect chip(nameRect.center().x() - (textW + 16) / 2,
                                 nameRect.y() + (nameRect.height() - 20) / 2,
                                 textW + 16, 20);
                p.setPen(Qt::NoPen);
                p.setBrush(UiStyle::overview::nameChip());
                p.drawRoundedRect(chip, kNameChipRadius, kNameChipRadius);
                p.setPen(UiStyle::overview::nameChipText());
                p.drawText(chip, Qt::AlignCenter, elided);
            } else {
                p.setPen(UiStyle::overview::nameText());
                p.drawText(nameRect, Qt::AlignHCenter | Qt::AlignVCenter,
                           fm.elidedText(name, Qt::ElideMiddle, nameRect.width()));
            }
        }

        // 键盘聚焦环：只在「最近一次交互来自键盘」时出，与鼠标选中态分开
        // （选中底板本身已由整卡实底表达）。
        if (selected && m_keyboardFocus && hasFocus()) {
            p.setPen(QPen(UiStyle::interactive(), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(cell).adjusted(1, 1, -1, -1), kTileRadius, kTileRadius);
        }
    }
    p.restore();

    // 左下状态行：图片总览 · N 张
    p.setPen(muted);
    p.setFont(UiStyle::font(12));
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
    m_keyboardFocus = false;
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

void OverviewPanel::probeDoubleClick(int index)
{
    if (index < 0 || index >= m_files.size())
        return;
    ensureVisible(index);
    const QRect cell = cellRect(index);
    if (cell.isEmpty())
        return;
    const QPointF local = cell.center();
    const QPointF global = mapToGlobal(cell.center());
    QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(this, &press);
    QMouseEvent dbl(QEvent::MouseButtonDblClick, local, global, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(this, &dbl);
}

void OverviewPanel::probeFollow(int index)
{
    setCurrent(index);
    ensureVisible(index);
    update();
}

QString OverviewPanel::probeMetrics() const
{
    const Metrics m = metrics();
    const QRect cell = cellRect(m_current);
    return QStringLiteral("cols=%1 cellW=%2 cellH=%3 imageH=%4 gridX=%5 scroll=%6 max=%7 "
                          "current=%8 cell=%9,%10 %11x%12 height=%13 imgs=%14 pending=%15 failed=%16 "
                          "visible=%17")
        .arg(m.cols)
        .arg(m.cellW)
        .arg(m.cellH)
        .arg(m.imageH)
        .arg(m.gridX)
        .arg(m_scroll)
        .arg(maxScroll())
        .arg(m_current)
        .arg(cell.x())
        .arg(cell.y())
        .arg(cell.width())
        .arg(cell.height())
        .arg(height())
        .arg(m_images.size())
        .arg(m_pending.size())
        .arg(m_failed.size())
        .arg(isVisible() ? 1 : 0);
}

void OverviewPanel::mouseMoveEvent(QMouseEvent *event)
{
    const int index = indexAt(event->position().toPoint());
    if (index != m_hover) {
        m_hover = index;
        update();
    }
    // 卡片本身就是点击目标（整个格子，不是图片像素），悬停给手型。
    setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    QWidget::mouseMoveEvent(event);
}

void OverviewPanel::leaveEvent(QEvent *event)
{
    m_hover = -1;
    unsetCursor();
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
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
        if (!m_keyboardFocus) {
            m_keyboardFocus = true;
            update();
        }
        break;
    default:
        break;
    }
    switch (event->key()) {
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
    // 列数变了，选中格可能被挤出可视带：尺寸变化后把它拉回视野（不改滚动原点语义）。
    ensureVisible(m_current);
    QWidget::resizeEvent(event);
}

void OverviewPanel::focusInEvent(QFocusEvent *event)
{
    // 只有 Tab / Backtab 这类明显来自键盘的聚焦才点亮焦点环；
    // 点回窗口（ActiveWindowFocusReason）或鼠标点击不应让卡片多出一道描边。
    const Qt::FocusReason reason = event->reason();
    m_keyboardFocus = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason;
    update();
    QWidget::focusInEvent(event);
}

void OverviewPanel::focusOutEvent(QFocusEvent *event)
{
    m_keyboardFocus = false;
    update();
    QWidget::focusOutEvent(event);
}

bool OverviewPanel::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        // 文件名已在卡片内直接渲染（选中为蓝底白字，悬停为普通文字），
        // 再弹系统 tooltip 会与卡片文字重叠，这里统一吞掉。
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}
