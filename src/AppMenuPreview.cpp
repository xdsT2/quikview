#include "AppMenuPreview.h"

#include <QApplication>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>

#include "MenuIcons.h"
#include "UiStyle.h"

// 查看器右键菜单 · 设置页常驻一级预览 + 浮动二级（实装派单 2026-11-16）。
// 排版常量与 CtxMenu 完全同源（行高 30 / 图标 16 / 标签 13 / 快捷键 11 / 圆角），
// 只做两层内的呈现与定位，不执行任何命令。

namespace {
constexpr int kRowH         = 30;
constexpr int kIconSize     = 16;
constexpr int kIconPadX     = 8;
constexpr int kIconTextGap  = 8;
constexpr int kMinShortcutW = 76;
constexpr int kArrowW       = 12;
constexpr int kMenuRadius   = 10;
constexpr int kRowRadius    = 6;
constexpr int kPad          = 5;    // 描边内边距（与 QMenu 的 4+1 对齐）
constexpr int kMarkW        = 18;   // 勾 / 单选点列
constexpr int kPopupGap     = 4;    // 两层之间
constexpr int kSepH         = 9;    // 分段线占位高度
constexpr int kMinPreviewW  = 160;  // 900 最低窗压缩下限
constexpr int kPreviewW     = 256;  // 一级固定宽度（1440 / 1068 同值）
constexpr int kPopupW       = 238;  // 二级起宽

QColor shortcutColor()   { return UiStyle::color(UiStyle::pick("#a7a9ad", "#606369")); }
QColor dangerIconColor() { return UiStyle::color(UiStyle::pick("#e15a50", "#c4382e")); }
QColor dangerRowBg()     { return UiStyle::color(UiStyle::fluent::dangerBg()); }
QColor colText()         { return QColor(UiStyle::menuText()); }
QColor colDisabled()     { return QColor(UiStyle::menuDisabled()); }
QColor colHover()        { return QColor(UiStyle::menuHover()); }

QColor menuSurface()
{
    if (UiStyle::blurMaterial()) {
        return QColor(UiStyle::isLight() ? QColor(255, 255, 255, 209) : QColor(39, 41, 45, 209));
    }
    return QColor(UiStyle::surfaceMenu());
}

void drawCheck(QPainter &p, const QPointF &center, const QColor &c, bool radio)
{
    if (radio) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawEllipse(center, 2.6, 2.6);
        return;
    }
    QPen pen(c, 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(center.x() - 3.6, center.y() + 0.2);
    path.lineTo(center.x() - 1.0, center.y() + 2.8);
    path.lineTo(center.x() + 4.0, center.y() - 2.8);
    p.drawPath(path);
}

void drawRadioOff(QPainter &p, const QPointF &center)
{
    QPen pen(shortcutColor(), 1.3);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(center, 2.7, 2.7);
}

void drawArrow(QPainter &p, const QPointF &rightCenter, bool enabled)
{
    const QPen pen(enabled ? shortcutColor() : colDisabled(), 1.5);
    QPen cap = pen;
    cap.setCapStyle(Qt::RoundCap);
    cap.setJoinStyle(Qt::RoundJoin);
    p.setPen(cap);
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(rightCenter.x() - 3.6, rightCenter.y() - 4.2);
    path.lineTo(rightCenter.x() + 1.2, rightCenter.y());
    path.lineTo(rightCenter.x() - 3.6, rightCenter.y() + 4.2);
    p.drawPath(path);
}

// 菜单表面：圆角 10 + 1px 描边，与 CtxMenu 一致（投影由 DWM 负责）。
void paintSurface(QPainter &p, const QRectF &surface)
{
    QPainterPath bg;
    bg.addRoundedRect(surface.adjusted(0.5, 0.5, -0.5, -0.5), kMenuRadius, kMenuRadius);
    p.fillPath(bg, menuSurface());
    p.setPen(QPen(QColor(UiStyle::menuLine()), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawPath(bg);
}

// 一行菜单的列布局（与 CtxMenu::paintEvent 的正文行同一套）：
// 图标 16 → 8px → 标签 → 勾/点列 → 快捷键列(≥76) → 箭头 12。
template <typename RowT>
void paintRows(QPainter &p, const QVector<RowT> &rows, const QVector<QRectF> &rowRects,
               const QString &activeId, const QString &hoverId, const QFont &fLabel,
               const QFont &fShort, bool drawFocusRing)
{
    const QFontMetrics fmL(fLabel);
    const QFontMetrics fmS(fShort);

    bool shortcutCol = false;
    bool arrowCol = false;
    bool markCol = false;
    for (const RowT &r : rows) {
        shortcutCol = shortcutCol || !r.shortcut.isEmpty();
        arrowCol = arrowCol || r.isGroup;
        markCol = markCol || r.checked || r.radioOn || r.radioOff;
    }

    for (int i = 0; i < rows.size() && i < rowRects.size(); ++i) {
        const RowT &r = rows.at(i);
        const QRectF row = rowRects.at(i);
        if (r.separator) {
            // 分段线：左右内缩 7px，与 CtxMenu 的分隔线同规格
            p.setPen(QPen(QColor(UiStyle::menuLine()), 1.0));
            const qreal y = row.top() + 0.5;
            p.drawLine(QPointF(row.left() + 7, y), QPointF(row.right() - 7, y));
            continue;
        }
        const bool enabled = r.enabled;
        const bool isActive = !activeId.isEmpty() && r.id == activeId;
        const bool isHover = !hoverId.isEmpty() && r.id == hoverId;
        const bool dangerHover = r.danger && (isHover || isActive);

        // —— 行底：悬停/定位同一档中性底；危险行只在悬停时给低饱和危险底 ——
        QColor bg;
        if (dangerHover) {
            bg = dangerRowBg();
        } else if (isHover || isActive) {
            bg = colHover();
        }
        if (bg.isValid()) {
            QPainterPath path;
            path.addRoundedRect(row.adjusted(1, 1, -1, -1), kRowRadius, kRowRadius);
            p.fillPath(path, bg);
        }

        const qreal iconX = row.left() + kIconPadX;
        const qreal iconY = row.center().y() - kIconSize / 2.0;
        qreal x = iconX + kIconSize + kIconTextGap;

        qreal arrowRight = 0;
        if (arrowCol && r.isGroup) {
            arrowRight = row.right() - kIconPadX;
        }
        qreal shortcutRight = arrowRight > 0 ? arrowRight - kArrowW - 6 : row.right() - kIconPadX;
        qreal shortcutLeft = 0;
        if (shortcutCol) {
            const int sw = qMax(kMinShortcutW, fmS.horizontalAdvance(r.shortcut) + 10);
            shortcutLeft = shortcutRight - sw;
        }

        if (!r.icon.isEmpty()) {
            const QColor ic = !enabled ? colDisabled()
                            : r.danger ? dangerIconColor()
                                       : colText();
            MenuIcons::paint(p, r.icon, QRectF(iconX, iconY, kIconSize, kIconSize), ic);
        }

        p.setFont(fLabel);
        p.setPen(enabled ? colText() : colDisabled());
        qreal labelRight = shortcutCol ? shortcutLeft - 6 : shortcutRight - 6;
        if (markCol && !shortcutCol) {
            labelRight -= kMarkW;
        }
        const int labelAvail = qMax(0, int(labelRight - x));
        const QString label = fmL.elidedText(r.label, Qt::ElideRight, labelAvail);
        p.drawText(QRectF(x, row.top(), labelAvail, row.height()),
                   Qt::AlignVCenter | Qt::AlignLeft, label);

        const QPointF markCenter(shortcutCol ? shortcutLeft - 8 : shortcutRight - 10,
                                 row.center().y());
        if (r.checked) {
            drawCheck(p, markCenter, enabled ? colText() : colDisabled(), false);
        } else if (r.radioOn) {
            drawCheck(p, markCenter, enabled ? colText() : colDisabled(), true);
        } else if (r.radioOff) {
            drawRadioOff(p, markCenter);
        }

        if (shortcutCol && !r.shortcut.isEmpty()) {
            p.setFont(fShort);
            p.setPen(enabled ? shortcutColor() : colDisabled());
            p.drawText(QRectF(shortcutLeft, row.top(), shortcutRight - shortcutLeft, row.height()),
                       Qt::AlignVCenter | Qt::AlignRight, r.shortcut);
        }

        if (arrowRight > 0) {
            drawArrow(p, QPointF(arrowRight - kArrowW + 3.0, row.center().y()), enabled);
        }

        if (drawFocusRing && isActive) {
            p.setPen(QPen(QColor(QStringLiteral("#1688f8")), 2));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(row.adjusted(1, 1, -1, -1), kRowRadius, kRowRadius);
        }
    }
}

template <typename RowT>
int menuContentWidth(const QVector<RowT> &rows, const QFont &fLabel, const QFont &fShort)
{
    const QFontMetrics fmL(fLabel);
    const QFontMetrics fmS(fShort);
    bool shortcutCol = false;
    bool arrowCol = false;
    bool markCol = false;
    for (const RowT &r : rows) {
        shortcutCol = shortcutCol || !r.shortcut.isEmpty();
        arrowCol = arrowCol || r.isGroup;
        markCol = markCol || r.checked || r.radioOn || r.radioOff;
    }
    int content = 0;
    for (const RowT &r : rows) {
        int w = kIconPadX + kIconSize + kIconTextGap + fmL.horizontalAdvance(r.label);
        if (markCol) {
            w += kMarkW;
        }
        if (shortcutCol) {
            w += qMax(kMinShortcutW, fmS.horizontalAdvance(r.shortcut) + 10);
        }
        if (arrowCol && r.isGroup) {
            w += kArrowW + 6;
        }
        w += kIconPadX;
        content = qMax(content, w);
    }
    return content + kPad * 2 + 2;
}

QRect screenRectFor(const QPoint &globalPoint)
{
    QScreen *screen = QGuiApplication::screenAt(globalPoint);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    return screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
}
} // namespace

// ===========================================================================
// AppMenuPreview
// ===========================================================================

AppMenuPreview::AppMenuPreview(QWidget *parent) : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumWidth(kMinPreviewW);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_labelFont = UiStyle::font(13);
    m_shortcutFont = UiStyle::font(11);
    m_popup = new Popup(this);
    m_popup->installEventFilter(this);
}

QSize AppMenuPreview::sizeHint() const
{
    int h = kPad * 2;
    for (const Row &r : m_rows) {
        h += r.separator ? kSepH : kRowH;
    }
    return QSize(kPreviewW, qMax(h, kRowH + kPad * 2));
}

void AppMenuPreview::setModel(const QVector<Row> &level1,
                              const QHash<QString, QVector<Row>> &children,
                              const QString &expandedId)
{
    m_rows = level1;
    m_children = children;

    const QString want = expandedId.isEmpty() ? m_expandedId : expandedId;
    bool exists = false;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.id == want && r.isGroup && m_children.contains(r.id)) {
            exists = true;
        }
    }
    if (!exists) {
        closePopup();
        m_expandedId.clear();
    } else {
        m_expandedId = want;
    }

    bool activeOk = false;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.id == m_activeId) {
            activeOk = true;
        }
    }
    if (!activeOk) {
        m_activeId.clear();   // 默认不高亮任何行：只有键盘/大纲定位才点亮
    }

    rebuildGeometry();
    update();
    if (!m_expandedId.isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == m_expandedId) {
                openPopupAt(i);
                break;
            }
        }
    }
    // 面板高度贴合内容：菜单末行之后不留空面板；超高时由外层滚动区接管
    setFixedHeight(sizeHint().height());
    updateGeometry();
}

void AppMenuPreview::highlightFromOutline(const QString &id, bool expand)
{
    if (id.isEmpty()) {
        return;
    }
    int hit = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == id) {
            hit = i;
            break;
        }
    }
    // 叶子 / 分组下的节点：向上找最近的一级入口
    for (QString p = AppMenu::parentOf(id); hit < 0 && !p.isEmpty(); p = AppMenu::parentOf(p)) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == p) {
                hit = i;
                break;
            }
        }
    }
    // 根组标题：定位该段首个可见入口
    if (hit < 0) {
        for (int i = 0; i < m_rows.size(); ++i) {
            for (QString p = AppMenu::parentOf(m_rows.at(i).id); !p.isEmpty();
                 p = AppMenu::parentOf(p)) {
                if (p == id) {
                    hit = i;
                    break;
                }
            }
            if (hit >= 0) {
                break;
            }
        }
    }
    if (hit < 0) {
        // 目标行不在预览里（例如被关掉的入口）：清掉旧高亮，避免停在无关行上
        if (!m_activeId.isEmpty()) {
            m_activeId.clear();
            update();
        }
        return;
    }
    m_activeId = m_rows.at(hit).id;
    update();
    if (expand && m_rows.at(hit).isGroup) {
        openPopupAt(hit);
    }
}

void AppMenuPreview::setLocalizeOutlineFn(std::function<void(const QString &)> fn)
{
    m_localizeOutline = std::move(fn);
}

QRect AppMenuPreview::probeRowRect(const QString &id)
{
    rebuildGeometry();
    for (int i = 0; i < m_rows.size() && i < m_geom.rowRects.size(); ++i) {
        if (m_rows.at(i).id == id) {
            return m_geom.rowRects.at(i).toRect();
        }
    }
    return QRect();
}

void AppMenuPreview::setExpanded(const QString &groupId)
{
    if (groupId.isEmpty()) {
        closePopupKeepActive();
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == groupId && m_rows.at(i).isGroup) {
            openPopupAt(i);
            return;
        }
    }
}

void AppMenuPreview::rebuildGeometry()
{
    m_geom.menuSurface = rect();
    m_geom.rowRects.clear();
    const int width = qMax(0, rect().width() - kPad * 2);
    qreal y = kPad;
    for (const Row &r : std::as_const(m_rows)) {
        const qreal h = r.separator ? kSepH : kRowH;
        m_geom.rowRects.append(QRectF(kPad, y, width, h));
        y += h;
    }
}

int AppMenuPreview::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < m_geom.rowRects.size(); ++i) {
        if (m_geom.rowRects.at(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void AppMenuPreview::repaintActive()
{
    update();
}

void AppMenuPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintSurface(p, QRectF(rect()));

    if (m_rows.isEmpty()) {
        p.setFont(m_labelFont);
        p.setPen(colDisabled());
        p.drawText(rect().adjusted(kPad, kPad, -kPad, -kPad),
                   Qt::AlignCenter | Qt::TextWordWrap, tr("当前没有可显示的菜单项"));
        return;
    }
    paintRows(p, m_rows, m_geom.rowRects, m_activeId, m_hoverId, m_labelFont, m_shortcutFont,
              hasFocus());
}

void AppMenuPreview::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QFrame::mousePressEvent(event);
        return;
    }
    setFocus(Qt::MouseFocusReason);
    const int row = rowAt(event->position().toPoint());
    if (row < 0) {
        return;
    }
    if (m_rows.at(row).separator) {
        return;
    }
    m_activeId = m_rows.at(row).id;
    update();
    if (m_rows.at(row).isGroup) {
        if (m_expandedId == m_activeId) {
            closePopupKeepActive();
        } else {
            openPopupAt(row);
        }
        return;
    }
    relocalize(m_activeId);
    event->accept();
}

void AppMenuPreview::mouseMoveEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    const QString hover = (row >= 0) ? m_rows.at(row).id : QString();
    if (hover != m_hoverId) {
        m_hoverId = hover;
        update();
    }
    QFrame::mouseMoveEvent(event);
}

void AppMenuPreview::leaveEvent(QEvent *event)
{
    if (!m_hoverId.isEmpty()) {
        m_hoverId.clear();
        update();
    }
    QFrame::leaveEvent(event);
}

bool AppMenuPreview::event(QEvent *event)
{
    if (event->type() == QEvent::Hide && m_popup && m_popup->isVisible()) {
        closePopup();
    }
    return QFrame::event(event);
}

bool AppMenuPreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_popup && event->type() == QEvent::Hide && !m_popupClosing) {
        // 点击外部 / Qt 主动收起：二级消失，一级常驻并保留当前定位
        m_expandedId.clear();
        update();
    }
    return QFrame::eventFilter(watched, event);
}

void AppMenuPreview::focusInEvent(QFocusEvent *event)
{
    update();
    QFrame::focusInEvent(event);
}

void AppMenuPreview::focusOutEvent(QFocusEvent *event)
{
    // 焦点去二级浮层时保持一级高亮；真正离开预览区才清悬停
    m_hoverId.clear();
    update();
    QFrame::focusOutEvent(event);
}

void AppMenuPreview::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    rebuildGeometry();
    if (!m_expandedId.isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == m_expandedId) {
                openPopupAt(i);
                break;
            }
        }
    }
}

void AppMenuPreview::keyPressEvent(QKeyEvent *event)
{
    if (handleKey(event->key(), event->modifiers())) {
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

bool AppMenuPreview::handleKey(int key, Qt::KeyboardModifiers)
{
    const bool popupOpen = m_popup && m_popup->isVisible();

    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (popupOpen) {
            return m_popup->handleKey(key, Qt::NoModifier);
        }
        moveActiveUpDown(key == Qt::Key_Down ? 1 : -1);
        return true;
    }
    case Qt::Key_Right:
        if (popupOpen) {
            return m_popup->handleKey(key, Qt::NoModifier);
        }
        activeToChild();
        return true;
    case Qt::Key_Left:
        if (popupOpen) {
            closePopupKeepActive();
            return true;
        }
        activeToParent();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        if (popupOpen) {
            return m_popup->handleKey(key, Qt::NoModifier);
        }
        activateCurrent();
        return true;
    case Qt::Key_Escape:
        if (popupOpen) {
            closePopupKeepActive();
            return true;
        }
        // 二级已关：只释放预览焦点，不能让 Esc 冒泡去关掉整个设置页
        clearFocus();
        m_activeId.clear();
        update();
        return true;
    default:
        return false;
    }
}

void AppMenuPreview::moveActiveUpDown(int delta)
{
    if (m_rows.isEmpty()) {
        return;
    }
    int index = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == m_activeId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        index = 0;
        while (index < m_rows.size() && m_rows.at(index).separator) {
            ++index;
        }
        if (index >= m_rows.size()) {
            return;
        }
    } else {
        for (int step = 0; step < m_rows.size(); ++step) {
            int next = index + delta;
            if (next < 0 || next >= m_rows.size()) {
                break;
            }
            index = next;
            if (m_rows.at(index).enabled && !m_rows.at(index).separator) {
                break;
            }
        }
    }
    m_activeId = m_rows.at(index).id;
    if (!m_expandedId.isEmpty()) {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == m_expandedId) {
                openPopupAt(i);
                break;
            }
        }
    }
    update();
}

void AppMenuPreview::activeToChild()
{
    if (m_rows.isEmpty()) {
        return;
    }
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id != m_activeId) {
            continue;
        }
        if (m_rows.at(i).isGroup) {
            openPopupAt(i);
        } else {
            relocalize(m_activeId);
        }
        return;
    }
}

void AppMenuPreview::activeToParent()
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == m_activeId) {
            relocalize(m_activeId);
            return;
        }
    }
}

void AppMenuPreview::activateCurrent()
{
    activeToChild();
}

void AppMenuPreview::relocalize(const QString &id)
{
    if (m_localizeOutline) {
        m_localizeOutline(id);
    }
    closePopupKeepActive();
}

QRect AppMenuPreview::popupAnchorRect() const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows.at(i).id == m_expandedId) {
            return m_geom.rowRects.at(i).toRect();
        }
    }
    return {};
}

void AppMenuPreview::openPopupAt(int row)
{
    if (row < 0 || row >= m_rows.size()) {
        return;
    }
    const Row &r = m_rows.at(row);
    const auto it = m_children.constFind(r.id);
    if (!r.isGroup || it == m_children.constEnd() || it->isEmpty()) {
        return;
    }

    m_expandedId = r.id;
    m_popup->setRows(*it);

    const QRect anchor = m_geom.rowRects.at(row).toRect();
    const QPoint anchorGlobal = mapToGlobal(QPoint(anchor.left(), anchor.top()));
    const QPoint anchorRight = mapToGlobal(QPoint(anchor.right(), anchor.top()));
    const QRect screen = screenRectFor(anchorGlobal);

    const int w = m_popup->width();
    const int h = m_popup->height();
    // 右侧优先，但不越过设置窗右缘（规格 §3.2：屏幕或设置窗右侧不足时向左）
    QWidget *win = window();
    const QRect winRect = win ? QRect(win->mapToGlobal(QPoint(0, 0)), win->size()) : screen;
    const int boundRight = qMin(screen.right(), winRect.right());
    int x = anchorRight.x() + kPopupGap;
    if (x + w > boundRight) {
        const int flipped = anchorGlobal.x() - w - kPopupGap;
        if (flipped >= screen.left()) {
            x = flipped;
        } else {
            x = qMax(screen.left(), boundRight - w);
        }
    }
    int y = anchorGlobal.y();
    if (y + h > screen.bottom()) {
        y = qMax(screen.top(), screen.bottom() - h);
    }
    m_popup->move(x, y);
    m_popup->show();
    m_popup->setFocus(Qt::PopupFocusReason);
    update();
    emit expandedChanged(r.id);
}

void AppMenuPreview::closePopupKeepActive()
{
    if (!m_popup || !m_popup->isVisible()) {
        m_expandedId.clear();
        return;
    }
    m_popupClosing = true;
    m_popup->hide();
    m_popupClosing = false;
    m_expandedId.clear();
    update();
}

void AppMenuPreview::closePopup()
{
    closePopupKeepActive();
}

// ===========================================================================
// 二级浮层
// ===========================================================================

AppMenuPreview::Popup::Popup(AppMenuPreview *owner)
    : QWidget(nullptr, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint),
      m_owner(owner)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setObjectName(QStringLiteral("appMenuPreviewPopup"));
    m_labelFont = UiStyle::font(13);
    m_shortcutFont = UiStyle::font(11);
}

void AppMenuPreview::Popup::setRows(const QVector<Row> &rows)
{
    m_rows = rows;
    bool activeOk = false;
    for (const Row &r : std::as_const(m_rows)) {
        if (r.id == m_activeId) {
            activeOk = true;
        }
    }
    if (!activeOk) {
        m_activeId.clear();
    }
    const QRect screen = screenRectFor(m_owner ? m_owner->mapToGlobal(QPoint(0, 0))
                                               : QPoint(0, 0));
    int width = qMax(kPopupW, menuContentWidth(m_rows, m_labelFont, m_shortcutFont));
    width = qMin(width, qMax(kPopupW, screen.width() - 16));
    rebuildGeom();
    setFixedSize(width, int(m_rows.size()) * kRowH + kPad * 2);
    rebuildGeom();
    update();
}

void AppMenuPreview::Popup::rebuildGeom()
{
    m_surface = rect();
    m_rowsRect.clear();
    const int width = qMax(0, rect().width() - kPad * 2);
    for (int i = 0; i < m_rows.size(); ++i) {
        m_rowsRect.append(QRect(kPad, kPad + i * kRowH, width, kRowH));
    }
}

int AppMenuPreview::Popup::rowAt(const QPoint &pos) const
{
    for (int i = 0; i < m_rowsRect.size(); ++i) {
        if (m_rowsRect.at(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

QRect AppMenuPreview::Popup::rowRect(int i) const
{
    return (i >= 0 && i < m_rowsRect.size()) ? m_rowsRect.at(i) : QRect();
}

void AppMenuPreview::Popup::setActiveId(const QString &id)
{
    m_activeId = id;
    update();
}

void AppMenuPreview::Popup::repaintRow(int)
{
    update();
}

bool AppMenuPreview::Popup::handleKey(int key, Qt::KeyboardModifiers)
{
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (m_rows.isEmpty()) {
            return true;
        }
        int index = -1;
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows.at(i).id == m_activeId) {
                index = i;
                break;
            }
        }
        const int delta = (key == Qt::Key_Down) ? 1 : -1;
        if (index < 0) {
            index = delta > 0 ? 0 : m_rows.size() - 1;
        } else {
            for (int step = 0; step < m_rows.size(); ++step) {
                const int next = index + delta;
                if (next < 0 || next >= m_rows.size()) {
                    break;
                }
                index = next;
                if (m_rows.at(index).enabled) {
                    break;
                }
            }
        }
        m_activeId = m_rows.at(index).id;
        update();
        return true;
    }
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
    case Qt::Key_Right:
        if (m_owner && !m_activeId.isEmpty()) {
            m_owner->relocalize(m_activeId);
        }
        return true;
    case Qt::Key_Left:
    case Qt::Key_Escape:
        if (m_owner) {
            m_owner->closePopupKeepActive();
            m_owner->setFocus(Qt::OtherFocusReason);
        }
        return true;
    default:
        return false;
    }
}

void AppMenuPreview::Popup::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    paintSurface(p, QRectF(rect()));

    QVector<QRectF> rects;
    rects.reserve(m_rowsRect.size());
    for (const QRect &r : std::as_const(m_rowsRect)) {
        rects.append(QRectF(r));
    }
    paintRows(p, m_rows, rects, m_activeId, QString(), m_labelFont, m_shortcutFont, false);
}

void AppMenuPreview::Popup::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const int row = rowAt(event->position().toPoint());
    if (row < 0) {
        if (m_owner) {
            m_owner->closePopupKeepActive();
        }
        return;
    }
    m_activeId = m_rows.at(row).id;
    update();
    if (m_owner) {
        m_owner->relocalize(m_activeId);
        m_owner->setFocus(Qt::OtherFocusReason);
    }
    event->accept();
}

void AppMenuPreview::Popup::mouseMoveEvent(QMouseEvent *event)
{
    const int row = rowAt(event->position().toPoint());
    if (row >= 0 && m_rows.at(row).id != m_activeId) {
        m_activeId = m_rows.at(row).id;
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void AppMenuPreview::Popup::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
}

void AppMenuPreview::Popup::keyPressEvent(QKeyEvent *event)
{
    if (handleKey(event->key(), event->modifiers())) {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
