#include "OverviewPanel.h"

#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMap>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>

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

// 分组标题：小号文字 + 组间留白（派单 2026-11-22 §两种分组）。
// 首组贴顶栏呼吸位，不再额外加一段上留白，避免总览顶部空一大块。
constexpr int kHeadH = 20;
constexpr int kHeadGapTop = 18;
constexpr int kHeadGapBottom = 8;

// 大小三档（派单 §按图片大小）：小于 2 MP / 2 至不足 8 MP / 8 MP 及以上。
// MP 按 100 万像素计（与相机/系统口径一致），不是 2^20。
constexpr qint64 kTwoMp = 2000000LL;
constexpr qint64 kEightMp = 8000000LL;
constexpr int kSizeBucketCount = 3;

QString sizeBucketTitle(int bucket)
{
    switch (bucket) {
    case 0: return QCoreApplication::translate("OverviewPanel", "小于 2 MP");
    case 1: return QCoreApplication::translate("OverviewPanel", "2 至不足 8 MP");
    default: return QCoreApplication::translate("OverviewPanel", "8 MP 及以上");
    }
}

// 未知大小返回 -1：调用方单独收一桶并始终置底。
int sizeBucketOf(const ImageFacts &facts)
{
    if (!facts.hasSize()) {
        return -1;
    }
    const qint64 px = facts.pixelCount();
    if (px < kTwoMp) {
        return 0;
    }
    if (px < kEightMp) {
        return 1;
    }
    return 2;
}

const char *groupModeName(OverviewPanel::GroupMode mode)
{
    switch (mode) {
    case OverviewPanel::GroupMode::Date: return "date";
    case OverviewPanel::GroupMode::Size: return "size";
    default: return "none";
    }
}
} // namespace

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
        m_pending.clear();   // 旧目录在飞的缩略图回来后按路径找不到落点，直接丢弃
        m_failed.clear();
        m_hover = -1;
        rebuildOrder();
    }
    setCurrent(currentIndex);
    if (m_files.isEmpty()) {
        m_scroll = 0;
    }
    m_scroll = qBound(0, m_scroll, maxScroll());
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

// —— 分组状态（2026-11-22 派单）————————————————————————————
QString OverviewPanel::groupTitle(GroupMode mode)
{
    switch (mode) {
    case GroupMode::Date: return tr("按日期");
    case GroupMode::Size: return tr("按图片大小");
    default: return tr("不分组");
    }
}

QString OverviewPanel::groupDirectionText() const
{
    switch (m_mode) {
    case GroupMode::Date: return m_ascending ? tr("从旧到新") : tr("从新到旧");
    case GroupMode::Size: return m_ascending ? tr("从小到大") : tr("从大到小");
    default: return QString();
    }
}

void OverviewPanel::selectGroupMode(GroupMode mode)
{
    if (mode == m_mode) {
        // 再点当前项：反转方向；「不分组」没有方向可翻，保持原位。
        if (mode == GroupMode::None) {
            return;
        }
        m_ascending = !m_ascending;
    } else {
        // 切到另一项用该项的初始方向（日期从新到旧、大小从大到小）。
        m_mode = mode;
        m_ascending = false;
    }
    rebuildOrder();
    invalidateLayout();
    // 切换方式或反转时保留同一文件的选中状态，并滚到它仍可见的位置。
    ensureVisiblePosition(currentPosition());
    update();
    emit orderChanged();
}

void OverviewPanel::setGroupMode(GroupMode mode, bool ascending)
{
    m_mode = mode;
    m_ascending = mode == GroupMode::None ? false : ascending;
    rebuildOrder();
    invalidateLayout();
    ensureVisiblePosition(currentPosition());
    update();
    emit orderChanged();
}

void OverviewPanel::resetGrouping()
{
    m_mode = GroupMode::None;
    m_ascending = false;
    rebuildOrder();
    invalidateLayout();
    update();
    emit orderChanged();
}

// 只重排「显示顺序」：m_images / m_failed 仍按文件下标保存，换分组不会重新解码缩略图。
void OverviewPanel::rebuildOrder()
{
    const int n = m_files.size();
    m_layoutDirty = true;
    m_hover = -1;
    m_order.resize(n);
    m_posOf.fill(-1, n);
    m_groupTitles.clear();
    m_groupCounts.clear();
    if (n == 0) {
        m_order.clear();
        return;
    }

    if (m_mode == GroupMode::None) {
        for (int i = 0; i < n; ++i) {
            m_order[i] = i;
            m_posOf[i] = i;
        }
        m_groupTitles.append(QString());   // 空标题 = 不画分组标题
        m_groupCounts.append(n);
        return;
    }

    // 头部事实按路径缓存：切到另一种分组、来回反转都不必重新读文件。
    QVector<ImageFacts> facts(n);
    for (int i = 0; i < n; ++i) {
        const QString &path = m_files.at(i);
        auto it = m_facts.find(path);
        if (it == m_facts.end()) {
            it = m_facts.insert(path, readImageFacts(path));
        }
        facts[i] = it.value();
    }

    QVector<int> order;
    order.reserve(n);

    if (m_mode == GroupMode::Date) {
        // 按拍摄日期的日历日聚组；同一天保持目录相对顺序（按目录顺序 append）。
        QMap<QDate, QVector<int>> byDay;
        QVector<int> undated;
        for (int i = 0; i < n; ++i) {
            if (facts.at(i).hasDate()) {
                byDay[facts.at(i).shotDate].append(i);
            } else {
                undated.append(i);
            }
        }
        const QList<QDate> days = byDay.keys();   // QMap 键已按日期升序
        for (int k = 0; k < days.size(); ++k) {
            const QDate day = days.at(m_ascending ? k : days.size() - 1 - k);
            const QVector<int> &bucket = byDay[day];
            m_groupTitles.append(day.toString(QStringLiteral("yyyy年M月d日")));
            m_groupCounts.append(bucket.size());
            order += bucket;
        }
        // 缺日期单列「无拍摄日期」，两个方向都置底。
        if (!undated.isEmpty()) {
            m_groupTitles.append(tr("无拍摄日期"));
            m_groupCounts.append(undated.size());
            order += undated;
        }
    } else {
        QVector<QVector<int>> buckets(kSizeBucketCount);
        QVector<int> unknown;
        for (int i = 0; i < n; ++i) {
            const int bucket = sizeBucketOf(facts.at(i));
            if (bucket < 0) {
                unknown.append(i);
            } else {
                buckets[bucket].append(i);
            }
        }
        for (int k = 0; k < kSizeBucketCount; ++k) {
            const int bucket = m_ascending ? k : kSizeBucketCount - 1 - k;
            QVector<int> files = buckets.at(bucket);
            if (files.isEmpty()) {
                continue;
            }
            // 大小组内按实际总像素数排序（方向与档位一致）；同值保持目录相对顺序。
            std::stable_sort(files.begin(), files.end(), [&facts, this](int a, int b) {
                const qint64 pa = facts.at(a).pixelCount();
                const qint64 pb = facts.at(b).pixelCount();
                return m_ascending ? pa < pb : pa > pb;
            });
            m_groupTitles.append(sizeBucketTitle(bucket));
            m_groupCounts.append(files.size());
            order += files;
        }
        // 未知大小单列置底。
        if (!unknown.isEmpty()) {
            m_groupTitles.append(tr("未知大小"));
            m_groupCounts.append(unknown.size());
            order += unknown;
        }
    }

    m_order = order;
    for (int pos = 0; pos < m_order.size(); ++pos) {
        m_posOf[m_order.at(pos)] = pos;
    }
}

int OverviewPanel::positionOf(int fileIndex) const
{
    if (fileIndex < 0 || fileIndex >= m_posOf.size()) {
        return -1;
    }
    return m_posOf.at(fileIndex);
}

int OverviewPanel::fileAtPosition(int position) const
{
    if (position < 0 || position >= m_order.size()) {
        return -1;
    }
    return m_order.at(position);
}

bool OverviewPanel::stepPosition(int delta)
{
    if (m_order.isEmpty() || delta == 0) {
        return false;
    }
    const int pos = currentPosition();
    const int next = qBound(0, (pos < 0 ? 0 : pos) + delta, m_order.size() - 1);
    if (next == pos) {
        return false;   // 已在当前顺序的首/末张
    }
    setCurrent(m_order.at(next));
    emit selected(m_current);
    ensureVisiblePosition(next);
    update();
    return true;
}

const OverviewPanel::Layout &OverviewPanel::layout() const
{
    if (m_layoutDirty) {
        rebuildLayout();
    }
    return m_layout;
}

// 布局：按组依次"标题行 + 若干行卡片"，全部用内容坐标（未减滚动量）。
// 单元格矩形在这里一次算好，绘制 / 命中 / 缩略图预取都只读这份缓存。
void OverviewPanel::rebuildLayout() const
{
    m_layout.cells.clear();
    m_layout.groups.clear();
    m_layout.contentH = 0;
    m_layoutDirty = false;

    const Metrics m = metrics();
    const int n = m_order.size();
    m_layout.cells.resize(n);
    int y = m.top;
    int pos = 0;
    for (int g = 0; g < m_groupCounts.size(); ++g) {
        const int count = m_groupCounts.at(g);
        if (count <= 0) {
            continue;
        }
        GroupRun run;
        run.title = m_groupTitles.value(g);
        run.firstPos = pos;
        run.count = count;
        if (!run.title.isEmpty()) {
            const int headTop = y + (pos == 0 ? 0 : kHeadGapTop);
            run.head = QRect(m.gridX, headTop, qMax(1, width() - 2 * m.gridX), kHeadH);
            y = headTop + kHeadH + kHeadGapBottom;
        }
        run.gridTop = y;
        const int rows = (count + m.cols - 1) / m.cols;
        for (int i = 0; i < count; ++i) {
            const int row = i / m.cols;
            const int col = i % m.cols;
            m_layout.cells[pos + i] = QRect(m.gridX + col * (m.cellW + m.gapX),
                                            y + row * (m.cellH + m.gapY),
                                            m.cellW, m.cellH);
        }
        y += rows * m.cellH + (rows - 1) * m.gapY;
        run.gridBottom = y;
        m_layout.groups.append(run);
        pos += count;
    }
    m_layout.contentH = n == 0 ? 0 : y + m.bottomReserve;
}

void OverviewPanel::revealCurrent()
{
    ensureVisiblePosition(currentPosition());
    update();
}

void OverviewPanel::scrollToEnd()
{
    m_scroll = maxScroll();
    update();
}

// 目标卡超出可视带就朝对应方向滚到「整卡可见」，仍在带内则一像素都不动
// ——拖浏览进度时既跟手，又不会每拖一格就把网格重新居中。
void OverviewPanel::ensureVisiblePosition(int position)
{
    if (position < 0) {
        return;
    }
    const Metrics m = metrics();
    const QRect cell = cellRect(position);
    if (cell.isNull()) {
        return;
    }
    const int bandTop = m.top;
    const int bandBottom = height() - m.bottomReserve;
    if (cell.bottom() > bandBottom) {
        m_scroll += cell.bottom() - bandBottom;
    } else if (cell.top() < bandTop) {
        m_scroll += cell.top() - bandTop;
    }
    m_scroll = qBound(0, m_scroll, maxScroll());
}

void OverviewPanel::ensureVisible(int fileIndex)
{
    ensureVisiblePosition(positionOf(fileIndex));
}

int OverviewPanel::contentHeight() const
{
    return layout().contentH;
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

QRect OverviewPanel::cellRect(int position) const
{
    const Layout &l = layout();
    if (position < 0 || position >= l.cells.size()) {
        return QRect();
    }
    const QRect cell = l.cells.at(position);
    return QRect(cell.x(), cell.y() - m_scroll, cell.width(), cell.height());
}

QRect OverviewPanel::imageRect(int position) const
{
    const QRect cell = cellRect(position);
    if (cell.isNull()) {
        return QRect();
    }
    const Metrics m = metrics();
    return QRect(cell.x() + m.cardPadX,
                 cell.y() + m.cardPadTop,
                 qMax(1, cell.width() - 2 * m.cardPadX),
                 qMax(1, m.imageH));
}

// 命中：先按纵向落到某一组，再在组内按格阵换算——组标题与组间留白不算命中。
int OverviewPanel::positionAt(const QPoint &pos) const
{
    const Metrics m = metrics();
    const int relX = pos.x() - m.gridX;
    if (relX < 0) {
        return -1;
    }
    const int hStride = m.cellW + m.gapX;
    const int col = relX / hStride;
    if (col >= m.cols || relX - col * hStride >= m.cellW) {
        return -1;   // 落在列间距里：不算命中，悬停不闪
    }
    const int vStride = m.cellH + m.gapY;
    const int contentY = pos.y() + m_scroll;
    for (const GroupRun &run : layout().groups) {
        if (contentY < run.gridTop || contentY >= run.gridBottom) {
            continue;
        }
        const int dy = contentY - run.gridTop;
        const int row = dy / vStride;
        if (dy - row * vStride >= m.cellH) {
            return -1;   // 行间距
        }
        const int inGroup = row * m.cols + col;
        if (inGroup < 0 || inGroup >= run.count) {
            return -1;   // 末行右侧的空位
        }
        return run.firstPos + inGroup;
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
    // 注意：cellRect() 返回的是「屏幕坐标」（已减去 m_scroll），
    // 这里必须拿 0..height() 的视口范围比较；早期用 m_scroll..m_scroll+height()
    // 当内容坐标去比，滚动后的行会被整段跳过，缩略图永远停在占位灰块。
    const int top = -160;              // 视口上方少量预取
    const int bottom = height() + 160; // 视口下方少量预取
    const QSize decode(qMin(420, m.cellW * 2), qMin(320, m.imageH * 2));
    const Layout &l = layout();
    for (int pos = 0; pos < m_order.size() && m_pending.size() < kMaxPending; ++pos) {
        if (pos >= l.cells.size()) {
            break;
        }
        const QRect cell = cellRect(pos);
        if (cell.bottom() < top || cell.top() > bottom) {
            continue;
        }
        const int index = m_order.at(pos);
        const QString &path = m_files.at(index);
        if (m_images.contains(index) || m_failed.contains(index) || m_pending.contains(path)) {
            continue;
        }
        m_pending.insert(path, index);
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
        if (image.isNull()) {
            m_failed.insert(index);
        } else {
            m_images.insert(index, new QImage(image));
        }
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
    const Layout &l = layout();
    const QColor body = UiStyle::body();
    const QColor muted = UiStyle::muted();
    const QFont nameFont = UiStyle::font(13);

    // 网格绘制裁到底栏上方，避免半排缩略图从悬浮栏底下穿出。
    const int clipBottom = height() - 64;
    p.save();
    p.setClipRect(QRect(0, 0, width(), qMax(0, clipBottom)));

    // 组标题：小号文字 + 组间留白；不分组时没有标题，这一段自然空转。
    p.setFont(UiStyle::font(12));
    for (const GroupRun &run : l.groups) {
        if (run.title.isEmpty()) {
            continue;
        }
        QRect head = run.head;
        head.moveTop(head.top() - m_scroll);
        if (head.bottom() < m.top || head.top() >= clipBottom) {
            continue;
        }
        p.setPen(muted);
        p.drawText(head, Qt::AlignLeft | Qt::AlignVCenter, run.title);
    }

    for (int pos = 0; pos < l.cells.size(); ++pos) {
        const QRect cell = cellRect(pos);
        if (cell.bottom() < 0 || cell.top() >= clipBottom) {
            continue;
        }
        // 顶边被顶栏压住的部分照样要画（上滚时能看到半张卡），只跳过完全离屏的。
        const QRect area = imageRect(pos);
        const int index = m_order.at(pos);
        const bool selected = index == m_current;
        const bool hovered = pos == m_hover;

        // 整卡底板：悬停轻底，选中实底——不是只沿缩略图像素边缘围一圈线。
        if (selected || hovered) {
            p.setPen(Qt::NoPen);
            p.setBrush(selected ? UiStyle::overview::cardSelected()
                                : UiStyle::overview::cardHover());
            p.drawRoundedRect(cell, kTileRadius, kTileRadius);
        }

        // 缩略图按原始宽高比等比放进卡片图片区，不拉伸。
        QRect fitted = area.adjusted(kThumbPadX, kThumbPadY, -kThumbPadX, -kThumbPadY);
        if (const QImage *image = m_images.object(index)) {
            const QSize scaled = image->size().scaled(fitted.size(), Qt::KeepAspectRatio);
            fitted = QRect(fitted.center().x() - scaled.width() / 2,
                           fitted.center().y() - scaled.height() / 2,
                           scaled.width(), scaled.height());
        }

        QPainterPath clip;
        clip.addRoundedRect(QRectF(fitted), kThumbRadius, kThumbRadius);
        p.save();
        p.setClipPath(clip);
        if (const QImage *image = m_images.object(index)) {
            p.drawImage(fitted, *image);
        } else {
            p.fillPath(clip, UiStyle::overview::thumbPlaceholder());
            if (m_failed.contains(index)) {
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
            const QString name = QFileInfo(m_files.at(index)).fileName();
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
    const int pos = positionAt(event->position().toPoint());
    if (pos >= 0) {
        setCurrent(m_order.at(pos));
        emit selected(m_current);
    }
    event->accept();
}

void OverviewPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    const int pos = positionAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && pos >= 0) {
        setCurrent(m_order.at(pos));
        emit activated(m_current);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OverviewPanel::probeDoubleClick(int index)
{
    const int pos = positionOf(index);
    if (pos < 0) {
        return;
    }
    ensureVisiblePosition(pos);
    const QRect cell = cellRect(pos);
    if (cell.isEmpty()) {
        return;
    }
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
    const int pos = positionOf(index);
    if (pos < 0) {
        return;
    }
    setCurrent(index);
    ensureVisiblePosition(pos);
    update();
}

QString OverviewPanel::probeMetrics() const
{
    const Metrics m = metrics();
    const int pos = currentPosition();
    const QRect cell = cellRect(pos);
    const Layout &l = layout();
    return QStringLiteral("cols=%1 cellW=%2 cellH=%3 imageH=%4 gridX=%5 scroll=%6 max=%7 "
                          "current=%8 pos=%9 cell=%10,%11 %12x%13 height=%14 imgs=%15 pending=%16 failed=%17 "
                          "visible=%18 group=%19 asc=%20 groups=%21 content=%22")
        .arg(m.cols)
        .arg(m.cellW)
        .arg(m.cellH)
        .arg(m.imageH)
        .arg(m.gridX)
        .arg(m_scroll)
        .arg(maxScroll())
        .arg(m_current)
        .arg(pos)
        .arg(cell.x())
        .arg(cell.y())
        .arg(cell.width())
        .arg(cell.height())
        .arg(height())
        .arg(m_images.size())
        .arg(m_pending.size())
        .arg(m_failed.size())
        .arg(isVisible() ? 1 : 0)
        .arg(QString::fromLatin1(groupModeName(m_mode)))
        .arg(m_ascending ? 1 : 0)
        .arg(l.groups.size())
        .arg(l.contentH);
}

void OverviewPanel::mouseMoveEvent(QMouseEvent *event)
{
    const int pos = positionAt(event->position().toPoint());
    if (pos != m_hover) {
        m_hover = pos;
        update();
    }
    // 卡片本身就是点击目标（整个格子，不是图片像素），悬停给手型。
    setCursor(pos >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
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

// 键盘方向键按「当前可见顺序」走位：左右 = 顺序前后一格，上下 = 跨一行。
void OverviewPanel::moveSelection(int dx, int dy)
{
    if (m_order.isEmpty()) {
        return;
    }
    const int cols = metrics().cols;
    const int before = currentPosition();
    int pos = before < 0 ? 0 : before;
    if (dx != 0) {
        pos = qBound(0, pos + dx, m_order.size() - 1);
    }
    if (dy != 0) {
        pos = qBound(0, pos + dy * cols, m_order.size() - 1);
    }
    if (pos != before) {
        setCurrent(m_order.at(pos));
        emit selected(m_current);
    }
    ensureVisiblePosition(pos);
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
        if (!m_order.isEmpty()) {
            setCurrent(m_order.first());
            emit selected(m_current);
            ensureVisiblePosition(0);
        }
        event->accept();
        return;
    case Qt::Key_End:
        if (!m_order.isEmpty()) {
            setCurrent(m_order.last());
            emit selected(m_current);
            ensureVisiblePosition(m_order.size() - 1);
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
    invalidateLayout();   // 列数变了，单元格坐标要重排
    m_scroll = qBound(0, m_scroll, maxScroll());
    // 列数变了，选中格可能被挤出可视带：尺寸变化后把它拉回视野（不改滚动原点语义）。
    ensureVisiblePosition(currentPosition());
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
