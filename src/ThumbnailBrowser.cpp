#include "ThumbnailBrowser.h"

#include <QFileInfo>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <algorithm>

#include "Chrome.h"
#include "ImageCache.h"
#include "UiStyle.h"

namespace {
constexpr int kSide = 14;
constexpr int kVertical = 9;
constexpr int kGap = 9;
constexpr int kRing = 3;
constexpr int kRadius = 9;
constexpr int kTrayRadius = 18;
constexpr int kMaxPending = 8;
constexpr int kRevealOpenMs = 160;    // 展开：淡入 + 遮罩由中间向两端
constexpr int kRevealCloseMs = 115;   // 收起：内容先减淡，托盘再收
constexpr int kRevealReducedMs = 80;  // 减少动效：纯淡入淡出
constexpr qreal kRevealExpand = 20.0; // 展开时左右两侧各让出的起始内收量
constexpr int kTileFadeMs = 80;       // 异步加载完成的单张淡入
constexpr int kRingSwapMs = 100;      // 选中描边交叉淡化
constexpr int kCenterMs = 180;        // 切图自动居中（快速减速、无回弹）
}

ThumbnailBrowser::ThumbnailBrowser(ImageCache *cache, QWidget *parent)
    : QWidget(parent)
    , m_cache(cache)
    , m_reveal(new QVariantAnimation(this))
{
    setObjectName(QStringLiteral("thumbnailBrowser"));
    setAccessibleName(tr("缩略图栏"));
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    m_reveal->setStartValue(0.0);
    m_reveal->setEndValue(1.0);
    m_reveal->setDuration(kRevealOpenMs);
    m_reveal->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_reveal, &QVariantAnimation::valueChanged, this, [this] { update(); });
    connect(m_reveal, &QVariantAnimation::finished, this, [this] {
        if (!m_expanded)
            hide();
    });
    // 自动居中：只改滚动值，目标可被滚轮/点击/目录重建随时打断改写。
    m_center = new QVariantAnimation(this);
    m_center->setDuration(kCenterMs);
    m_center->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_center, &QVariantAnimation::valueChanged, this, [this] {
        m_scroll = m_center->currentValue().toInt();
        m_centerCurrent = false;
        schedulePreviews();
        update();
    });
    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setInterval(16);
    connect(m_fadeTimer, &QTimer::timeout, this, [this] {
        const bool ringBusy = m_prevCurrent >= 0
                              && m_paintClock.elapsed() - m_ringSwapMs < kRingSwapMs;
        if (!ringBusy && m_prevCurrent >= 0)
            m_prevCurrent = -1;
        if (m_tileFade.isEmpty() && !ringBusy) {
            m_fadeTimer->stop();
        }
        update();
    });
    m_paintClock.start();
    connect(m_cache, &ImageCache::thumbnailReady, this, &ThumbnailBrowser::onThumbReady);
}

void ThumbnailBrowser::setFiles(const QStringList &files, int currentIndex)
{
    if (m_files != files) {
        m_files = files;
        m_sizes = QVector<QSize>(files.size());
        m_images.clear();
        // Keep in-flight work bounded across directory switches. A stale result
        // releases its slot without touching the new directory.
        for (int &index : m_pending)
            index = -1;
        m_failed.clear();
        m_tileFade.clear();
        m_prevCurrent = -1;
        m_center->stop();
        m_pressed = m_hover = -1;
        rebuildPositions();
    }
    setCurrent(currentIndex);
    if (files.isEmpty() && m_expanded)
        setExpanded(false);
}

void ThumbnailBrowser::setCurrent(int index)
{
    const int previous = m_current;
    m_current = index >= 0 && index < m_files.size() ? index : -1;
    m_centerCurrent = true;
    if (m_expanded && m_current != previous && previous >= 0 && !Chrome::reducedMotion()) {
        // 选中描边交叉淡化：旧描边淡出、新描边淡入。
        m_prevCurrent = previous;
        m_ringSwapMs = m_paintClock.elapsed();
        if (!m_fadeTimer->isActive())
            m_fadeTimer->start();
    }
    if (m_expanded) {
        if (Chrome::reducedMotion()) {
            updateGeometryAndScroll();
            return;
        }
        // 几何/位置先按当前滚动更新（不吸附居中），滚动用 180ms 动画补齐，
        // 途中被滚轮/点击/目录重建打断都安全。
        m_centerCurrent = false;
        updateGeometryAndScroll();
        m_centerCurrent = true;
        startCenterAnimation();
    }
}

void ThumbnailBrowser::setExpanded(bool expanded)
{
    if (expanded == m_expanded || (expanded && m_files.isEmpty()))
        return;
    m_expanded = expanded;
    setAttribute(Qt::WA_TransparentForMouseEvents, !expanded);
    m_pressed = m_hover = -1;
    if (expanded) {
        m_centerCurrent = true;
        updateGeometryAndScroll();
        show();
        raise();
    }
    m_reveal->setDirection(expanded ? QAbstractAnimation::Forward : QAbstractAnimation::Backward);
    // 展开慢、收起快（退出比进入快约 25%）；减少动效只留短淡入淡出。
    m_reveal->setDuration(Chrome::reducedMotion()
                              ? kRevealReducedMs
                              : (expanded ? kRevealOpenMs : kRevealCloseMs));
    if (m_reveal->state() != QAbstractAnimation::Running)
        m_reveal->start();
}

void ThumbnailBrowser::place(int availableWidth, int bottom)
{
    if (m_availableWidth != availableWidth) {
        m_centerCurrent = true;
        m_availableWidth = availableWidth;
        const int preview = qBound(54, 72 + (availableWidth - 860) / 40, 84);
        if (preview != m_previewHeight) {
            m_previewHeight = preview;
            m_positionsDirty = true;
        }
    }
    m_bottom = bottom;
    if (isVisible() || m_expanded)
        updateGeometryAndScroll();
}

void ThumbnailBrowser::rebuildPositions()
{
    m_positions.resize(m_sizes.size() + 1);
    m_positions[0] = 0;
    for (int i = 0; i < m_sizes.size(); ++i) {
        const QSize size = m_sizes.at(i);
        const int previewWidth = size.isEmpty() ? m_previewHeight * 4 / 3
            : qMax(24, qRound(qMin(2.4, qreal(size.width()) / size.height()) * m_previewHeight));
        m_positions[i + 1] = m_positions.at(i) + previewWidth + 2 * kRing + kGap;
    }
    m_positionsDirty = false;
}

void ThumbnailBrowser::updateGeometryAndScroll()
{
    if (m_positionsDirty) {
        const int anchor = indexAt(m_scroll);
        const int offset = anchor >= 0 ? m_scroll - m_positions.at(anchor) : 0;
        rebuildPositions();
        if (anchor >= 0)
            m_scroll = m_positions.at(anchor) + qMin(offset,
                m_positions.at(anchor + 1) - m_positions.at(anchor) - 1);
    }
    const int content = m_positions.isEmpty() ? 0 : qMax(0, m_positions.last() - kGap);
    // The window gutter is independent of the padding inside the filmstrip.
    const int safeInset = qBound(32, qRound(m_availableWidth * 0.042), 96);
    const int left = qMax(safeInset, (m_availableWidth - content - 2 * kSide) / 2);
    const int trayWidth = qMax(1, m_availableWidth - 2 * left);
    m_images.setMaxCost(qMax(48, trayWidth / (24 + 2 * kRing + kGap) + 12));
    const int trayHeight = m_previewHeight + 2 * (kRing + kVertical);
    setGeometry(left, m_bottom - trayHeight, trayWidth, trayHeight);
    if (m_center->state() == QAbstractAnimation::Running) {
        // 居中动画进行中：几何照常更新，滚动交给动画去追（终点实时重算）。
        startCenterAnimation();
    } else if (m_centerCurrent && m_current >= 0) {
        m_scroll = (m_positions.at(m_current) + m_positions.at(m_current + 1) - kGap) / 2
                   - (width() - 2 * kSide) / 2;
    }
    m_scroll = qBound(0, m_scroll, maxScroll());
    update();
    schedulePreviews();
}

int ThumbnailBrowser::centerScrollTarget() const
{
    if (m_current < 0 || m_positions.size() < m_current + 2)
        return qBound(0, m_scroll, maxScroll());
    return qBound(0, (m_positions.at(m_current) + m_positions.at(m_current + 1) - kGap) / 2
                       - (width() - 2 * kSide) / 2,
                  maxScroll());
}

void ThumbnailBrowser::startCenterAnimation()
{
    // 目标值实时重算：缩略图尺寸异步补齐会让目标移动，动画跟着改写终点，
    // 但起点永远是当前滚动位置——被打断就反向，不跳回、不排队。
    const int target = centerScrollTarget();
    if (m_center->state() == QAbstractAnimation::Running
        && m_center->endValue().toInt() == target)
        return;
    m_center->stop();
    m_center->setStartValue(m_scroll);
    m_center->setEndValue(target);
    m_center->start();
}

int ThumbnailBrowser::maxScroll() const
{
    return m_positions.isEmpty() ? 0
        : qMax(0, m_positions.last() - kGap - width() + 2 * kSide);
}

int ThumbnailBrowser::indexAt(int contentX) const
{
    if (m_positions.size() < 2)
        return -1;
    const auto it = std::upper_bound(m_positions.cbegin(), m_positions.cend(), contentX);
    return qBound(0, int(it - m_positions.cbegin()) - 1, int(m_files.size()) - 1);
}

QRect ThumbnailBrowser::tileRect(int index) const
{
    return QRect(kSide + m_positions.at(index) - m_scroll, kVertical,
                 m_positions.at(index + 1) - m_positions.at(index) - kGap,
                 m_previewHeight + 2 * kRing);
}

bool ThumbnailBrowser::currentInView() const
{
    if (!m_expanded || m_current < 0)
        return false;
    const QRect tile = tileRect(m_current);
    return tile.left() >= kSide && tile.right() < width() - kSide;
}

void ThumbnailBrowser::schedulePreviews()
{
    if (!m_expanded || m_updateQueued)
        return;
    m_updateQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_updateQueued = false;
        if (!m_expanded)
            return;
        if (m_positionsDirty)
            updateGeometryAndScroll();
        requestVisible();
    });
}

void ThumbnailBrowser::requestVisible()
{
    const int first = qMax(0, indexAt(m_scroll) - 1);
    const int last = qMin(int(m_files.size()) - 1, indexAt(m_scroll + width() - 2 * kSide) + 1);
    // Source previews cover the largest tray at 150% DPI. Only the visible
    // interval and one tile on either side are decoded, with bounded concurrency.
    const QSize decodeSize(308, 126);
    for (int index = first; index <= last && m_pending.size() < kMaxPending; ++index) {
        const QString &path = m_files.at(index);
        if (m_images.contains(index) || m_failed.contains(index) || m_pending.contains(path))
            continue;
        m_pending.insert(path, index);
        m_cache->requestThumbnail(path, decodeSize);
    }
}

void ThumbnailBrowser::onThumbReady(const QString &path, const QImage &image)
{
    const auto it = m_pending.find(path);
    if (it == m_pending.end())
        return;
    const int index = it.value();
    m_pending.erase(it);
    if (index >= 0) {
        if (image.isNull()) {
            m_failed.insert(index);
        } else {
            const QSize previous = m_sizes.at(index);
            m_sizes[index] = image.size();
            m_images.insert(index, new QImage(image));
            m_positionsDirty |= previous != image.size();
            // 异步补齐的单张缩略图 80ms 淡入（缓存命中不经过这里，保持立即出现）。
            if (!Chrome::reducedMotion()) {
                m_tileFade.insert(index, m_paintClock.elapsed());
                if (!m_fadeTimer->isActive())
                    m_fadeTimer->start();
            }
        }
        update();
    }
    schedulePreviews();
}

void ThumbnailBrowser::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    const qreal progress = m_reveal->currentValue().toReal();
    const bool reduced = Chrome::reducedMotion();
    const bool closing = m_reveal->direction() == QAbstractAnimation::Backward;
    // 收起两段式：内容先减淡（进度前半段），托盘遮罩随后再收。
    const qreal contentFade = closing ? qBound(0.0, (progress - 0.55) / 0.45, 1.0) : 1.0;
    // 展开：遮罩由中间向两端展开 + 高度 96%→100%（底边锚点固定，肉眼无上抬）；
    // 减少动效时取消位移类展开，只保留淡入淡出。
    const qreal growX = reduced ? 0.0 : kRevealExpand * (1.0 - progress);
    const qreal growY = reduced ? 0.0 : 6.0 * (1.0 - progress);
    painter.setOpacity(progress);
    const QRectF tray = QRectF(rect()).adjusted(.5 + growX, .5 + growY, -.5 - growX, -.5);
    QPainterPath shape;
    shape.addRoundedRect(tray, kTrayRadius, kTrayRadius);
    painter.setPen(UiStyle::filmstripEdge());
    painter.setBrush(UiStyle::filmstripSurface());
    painter.drawPath(shape);
    if (m_files.isEmpty() || m_positions.size() < 2)
        return;

    painter.setClipPath(shape);
    // 选中描边交叉淡化进度：旧描边淡出、新描边淡入（100ms，可被下一次切换打断）。
    const qreal ringT = reduced ? 1.0
        : qBound(0.0, qreal(m_paintClock.elapsed() - m_ringSwapMs) / kRingSwapMs, 1.0);
    // Include peeking tiles in both equally wide edge gutters before fading.
    const int first = indexAt(m_scroll - kSide);
    const int last = indexAt(m_scroll + width() - kSide - 1);
    for (int index = first; index <= last; ++index) {
        const QRectF tile = tileRect(index);
        const QRectF preview = tile.adjusted(kRing, kRing, -kRing, -kRing);
        const bool isCur = index == m_current;
        const bool isPrev = !isCur && index == m_prevCurrent && (1.0 - ringT) > 0.004;
        const bool isHover = index == m_hover && !isCur && !isPrev;
        if (isCur || isPrev || isHover) {
            painter.setOpacity(progress * (isCur ? ringT : (isPrev ? 1.0 - ringT : 1.0)));
            painter.setBrush(UiStyle::filmstripInset());
            painter.setPen(QPen(UiStyle::filmstripOutline(), (isCur || isPrev) ? 2 : 1));
            painter.drawRoundedRect(tile.adjusted(1, 1, -1, -1), kRadius + 1, kRadius + 1);
        }
        // 异步补齐的单张 80ms 淡入；缓存命中与失败占位不参与，立即出现。
        qreal tileAlpha = 1.0;
        if (!reduced) {
            auto fit = m_tileFade.find(index);
            if (fit != m_tileFade.end()) {
                const qint64 age = m_paintClock.elapsed() - fit.value();
                if (age >= kTileFadeMs)
                    m_tileFade.erase(fit);
                else
                    tileAlpha = qreal(age) / kTileFadeMs;
            }
        }
        painter.setOpacity(progress * contentFade * tileAlpha);
        QPainterPath clip;
        clip.addRoundedRect(preview, kRadius, kRadius);
        painter.save();
        painter.setClipPath(clip, Qt::IntersectClip);
        painter.fillPath(clip, UiStyle::filmstripInset());
        if (const QImage *image = m_images.object(index)) {
            const QSizeF size = QSizeF(image->size()).scaled(preview.size(), Qt::KeepAspectRatio);
            painter.drawImage(QRectF(preview.center() - QPointF(size.width(), size.height()) / 2, size),
                              *image);
        } else if (m_failed.contains(index)) {
            painter.setPen(UiStyle::filmstripOutline());
            painter.drawText(preview, Qt::AlignCenter, tr("无法预览"));
        }
        painter.restore();
    }

    painter.setOpacity(progress);
    auto fade = [&](int edge, int direction) {
        const int extent = kSide + 5;
        QLinearGradient gradient(edge, 0, edge + direction * extent, 0);
        QColor color = UiStyle::filmstripSurface();
        gradient.setColorAt(0, color);
        color.setAlpha(0);
        gradient.setColorAt(1, color);
        painter.fillRect(QRect(qMin(edge, edge + direction * extent), 1, extent, height() - 2),
                         gradient);
    };
    if (m_scroll > 0)
        fade(1, 1);
    if (m_scroll < maxScroll())
        fade(width() - 1, -1);
}

void ThumbnailBrowser::mousePressEvent(QMouseEvent *event)
{
    const QPoint point = event->position().toPoint();
    const int index = indexAt(point.x() - kSide + m_scroll);
    if (event->button() == Qt::LeftButton && index >= 0 && tileRect(index).contains(point))
        m_pressed = index;
    m_center->stop();
    event->accept();
}

void ThumbnailBrowser::mouseReleaseEvent(QMouseEvent *event)
{
    const int pressed = m_pressed;
    m_pressed = -1;
    if (event->button() == Qt::LeftButton && pressed >= 0
        && tileRect(pressed).contains(event->position().toPoint()))
        emit activateImage(pressed);
    event->accept();
}

void ThumbnailBrowser::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint point = event->position().toPoint();
    const int index = indexAt(point.x() - kSide + m_scroll);
    m_hover = index >= 0 && tileRect(index).contains(point) ? index : -1;
    update();
}

void ThumbnailBrowser::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

void ThumbnailBrowser::wheelEvent(QWheelEvent *event)
{
    const QPoint pixels = event->pixelDelta();
    const QPoint angle = event->angleDelta();
    const int delta = !pixels.isNull() ? (pixels.x() ? pixels.x() : pixels.y())
        : (angle.x() ? angle.x() : angle.y()) / 2;
    m_center->stop(); // 滚轮立即打断自动居中，滑动手感优先
    m_centerCurrent = false;
    m_hover = -1;
    m_pressed = -1;
    m_scroll = qBound(0, m_scroll - delta, maxScroll());
    schedulePreviews();
    update();
    event->accept();
}

bool ThumbnailBrowser::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        const auto *help = static_cast<QHelpEvent *>(event);
        const int index = indexAt(help->pos().x() - kSide + m_scroll);
        if (index >= 0 && tileRect(index).contains(help->pos()))
            QToolTip::showText(help->globalPos(), QFileInfo(m_files.at(index)).fileName(), this);
        else
            QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}
