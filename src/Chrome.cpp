#include "Chrome.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPropertyAnimation>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>
#include <QWindow>
#include <algorithm>
#include <cmath>

namespace Chrome {
namespace {

enum class Icon {
    Folder, Grid, Play, Bookmark, Edit, Copy, Lock, More, ChevronDown,
    Minus, Plus, Left, Right, Fit, RotateLeft, RotateRight, Picture,
    MinWindow, MaxWindow, Close
};

QFont uiFont(int pixelSize, int weight = QFont::Normal)
{
    QFont f(QStringLiteral("Segoe UI"));
    f.setPixelSize(pixelSize);
    f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

QColor rgba(int r, int g, int b, int a)
{
    return QColor(r, g, b, a);
}

void drawIcon(QPainter &p, Icon icon, const QRectF &r, const QColor &color,
              qreal strokeWidth = 1.55)
{
    if (r.isEmpty()) {
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(r.x(), r.y());
    const qreal sx = r.width() / 24.0;
    const qreal sy = r.height() / 24.0;
    p.scale(sx, sy);
    QPen pen(color, strokeWidth / qMax(0.01, sx), Qt::SolidLine,
             Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (icon) {
    case Icon::Folder: {
        QPainterPath path;
        path.moveTo(3, 7.5);
        path.lineTo(3, 18);
        path.quadTo(3, 20, 5, 20);
        path.lineTo(19, 20);
        path.quadTo(21, 20, 21, 18);
        path.lineTo(21, 8);
        path.quadTo(21, 6, 19, 6);
        path.lineTo(12, 6);
        path.lineTo(9, 4);
        path.lineTo(5, 4);
        path.quadTo(3, 4, 3, 6);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Grid:
        p.drawRoundedRect(QRectF(4, 4, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(14, 4, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(4, 14, 6, 6), 1, 1);
        p.drawRoundedRect(QRectF(14, 14, 6, 6), 1, 1);
        break;
    case Icon::Play: {
        QPainterPath path;
        path.moveTo(8, 5);
        path.lineTo(19, 12);
        path.lineTo(8, 19);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Bookmark: {
        QPainterPath path;
        path.moveTo(6, 4);
        path.quadTo(6, 2, 8, 2);
        path.lineTo(16, 2);
        path.quadTo(18, 2, 18, 4);
        path.lineTo(18, 22);
        path.lineTo(12, 18);
        path.lineTo(6, 22);
        path.closeSubpath();
        p.drawPath(path);
        break;
    }
    case Icon::Edit:
        p.drawLine(QPointF(14, 4), QPointF(20, 10));
        p.drawLine(QPointF(20, 10), QPointF(9, 21));
        p.drawLine(QPointF(9, 21), QPointF(3, 21));
        p.drawLine(QPointF(3, 21), QPointF(3, 15));
        p.drawLine(QPointF(3, 15), QPointF(14, 4));
        p.drawLine(QPointF(16, 2), QPointF(22, 8));
        break;
    case Icon::Copy:
        p.drawRoundedRect(QRectF(8, 8, 13, 13), 2, 2);
        p.drawLine(QPointF(16, 8), QPointF(16, 5));
        p.drawLine(QPointF(16, 5), QPointF(14, 3));
        p.drawLine(QPointF(14, 3), QPointF(5, 3));
        p.drawLine(QPointF(5, 3), QPointF(3, 5));
        p.drawLine(QPointF(3, 5), QPointF(3, 14));
        p.drawLine(QPointF(3, 14), QPointF(5, 16));
        p.drawLine(QPointF(5, 16), QPointF(8, 16));
        break;
    case Icon::Lock:
        p.drawRoundedRect(QRectF(4, 10, 16, 11), 2, 2);
        p.drawLine(QPointF(8, 10), QPointF(8, 7));
        p.drawArc(QRectF(8, 3, 8, 8), 0, 180 * 16);
        p.drawLine(QPointF(16, 7), QPointF(16, 10));
        break;
    case Icon::More:
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPointF(5, 12), 1.1, 1.1);
        p.drawEllipse(QPointF(12, 12), 1.1, 1.1);
        p.drawEllipse(QPointF(19, 12), 1.1, 1.1);
        break;
    case Icon::ChevronDown:
        p.drawLine(QPointF(7, 10), QPointF(12, 15));
        p.drawLine(QPointF(12, 15), QPointF(17, 10));
        break;
    case Icon::Minus:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::Plus:
        p.drawLine(QPointF(12, 5), QPointF(12, 19));
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::Left:
        p.drawLine(QPointF(15, 5), QPointF(8, 12));
        p.drawLine(QPointF(8, 12), QPointF(15, 19));
        break;
    case Icon::Right:
        p.drawLine(QPointF(9, 5), QPointF(16, 12));
        p.drawLine(QPointF(16, 12), QPointF(9, 19));
        break;
    case Icon::Fit:
        p.drawLine(QPointF(8, 3), QPointF(3, 3));
        p.drawLine(QPointF(3, 3), QPointF(3, 8));
        p.drawLine(QPointF(21, 3), QPointF(16, 3));
        p.drawLine(QPointF(21, 3), QPointF(21, 8));
        p.drawLine(QPointF(3, 16), QPointF(3, 21));
        p.drawLine(QPointF(3, 21), QPointF(8, 21));
        p.drawLine(QPointF(21, 16), QPointF(21, 21));
        p.drawLine(QPointF(21, 21), QPointF(16, 21));
        break;
    case Icon::RotateLeft:
        p.drawArc(QRectF(4, 4, 16, 16), 70 * 16, 260 * 16);
        p.drawLine(QPointF(4, 4), QPointF(4, 9));
        p.drawLine(QPointF(4, 4), QPointF(9, 4));
        break;
    case Icon::RotateRight:
        p.drawArc(QRectF(4, 4, 16, 16), 30 * 16, 260 * 16);
        p.drawLine(QPointF(20, 4), QPointF(20, 9));
        p.drawLine(QPointF(20, 4), QPointF(15, 4));
        break;
    case Icon::Picture:
        p.drawRoundedRect(QRectF(3, 3, 18, 18), 3, 3);
        p.drawEllipse(QPointF(8.5, 8.5), 1.5, 1.5);
        p.drawLine(QPointF(4, 17), QPointF(9, 12));
        p.drawLine(QPointF(9, 12), QPointF(13, 16));
        p.drawLine(QPointF(13, 16), QPointF(16, 13));
        p.drawLine(QPointF(16, 13), QPointF(21, 18));
        break;
    case Icon::MinWindow:
        p.drawLine(QPointF(5, 12), QPointF(19, 12));
        break;
    case Icon::MaxWindow:
        p.drawRoundedRect(QRectF(5, 5, 14, 14), 1, 1);
        break;
    case Icon::Close:
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;
    }
    p.restore();
}

void setPointerCursor(QWidget *w, bool pointer)
{
    if (!w) {
        return;
    }
    if (pointer) {
        w->setCursor(Qt::PointingHandCursor);
    } else {
        w->unsetCursor();
    }
}

} // namespace

// ===========================================================================
// 皮肤贴图兼容接口（旧布局代码仍可能引用）
// ===========================================================================

QString skinDir()
{
    const QString base = QCoreApplication::applicationDirPath();
    const QString dirs[] = {
        base + QStringLiteral("/../resources/skin"),
        base + QStringLiteral("/../../resources/skin"),
        base + QStringLiteral("/resources/skin"),
        QStringLiteral("resources/skin"),
    };
    for (const QString &d : dirs) {
        if (QFileInfo::exists(d + QStringLiteral("/static_controlpanel.png"))) {
            return d;
        }
    }
    return QStringLiteral("resources/skin");
}

QPixmap skinPixmap(const QString &name)
{
    return QPixmap(skinDir() + QLatin1Char('/') + name);
}

QPixmap skinFrame(const QString &name, int idx)
{
    const QPixmap all = skinPixmap(name);
    if (all.isNull()) {
        return QPixmap();
    }
    const int frames = qMax(1, all.width() / qMax(1, all.height()));
    const int fw = all.width() / frames;
    return all.copy(idx * fw, 0, fw, all.height());
}

// ===========================================================================
// 标题栏
// ===========================================================================

CaptionBar::CaptionBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(28);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void CaptionBar::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QRect CaptionBar::buttonRect(int index) const
{
    const int bw = 44;
    return QRect(width() - (3 - index) * bw, 0, bw, height());
}

int CaptionBar::hitTest(const QPoint &pos) const
{
    for (int i = 0; i < 3; ++i) {
        if (buttonRect(i).contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CaptionBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QStringLiteral("#1d1e20")));
    p.fillRect(QRect(0, height() - 1, width(), 1), rgba(255, 255, 255, 10));

    // 应用标识
    const QRectF logoRect(11, (height() - 16) / 2.0, 16, 16);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#0b84c6")));
    p.drawRoundedRect(logoRect, 4, 4);
    p.setFont(uiFont(10, QFont::DemiBold));
    p.setPen(QColor(QStringLiteral("#ffffff")));
    p.drawText(logoRect, Qt::AlignCenter, QStringLiteral("Q"));

    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#d7d7d7")));
    p.drawText(QRect(34, 0, 78, height()), Qt::AlignVCenter | Qt::AlignLeft,
               QStringLiteral("QuikView"));

    const int dividerX = 103;
    p.fillRect(QRect(dividerX, 9, 1, height() - 17), rgba(255, 255, 255, 19));

    // 文件名
    const int controlsLeft = width() - 3 * 44;
    const int titleLeft = dividerX + 12;
    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#96999d")));
    const QRect titleRect(titleLeft, 0, qMax(0, controlsLeft - titleLeft - 12), height());
    p.drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
               p.fontMetrics().elidedText(m_title, Qt::ElideMiddle, titleRect.width()));

    // 窗口按钮
    for (int i = 0; i < 3; ++i) {
        const QRect r = buttonRect(i);
        const bool hover = (m_hover == i);
        if (i == 2 && hover) {
            p.fillRect(r, QColor(QStringLiteral("#c42b1c")));
        } else if (hover) {
            p.fillRect(r, QColor(QStringLiteral("#343539")));
        }
        QColor ink = (i == 2 && hover) ? QColor(Qt::white)
                                       : QColor(QStringLiteral("#c9c9c9"));
        const Icon icon = i == 0 ? Icon::MinWindow
                                 : (i == 1 ? Icon::MaxWindow : Icon::Close);
        drawIcon(p, icon, QRectF(r.center().x() - 6, r.center().y() - 6, 12, 12), ink, 1.4);
    }
}

void CaptionBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_press = hitTest(e->pos());
        if (m_press >= 0) {
            update();
            return;
        }
        if (window() && window()->windowHandle()) {
            window()->windowHandle()->startSystemMove();
        }
        return;
    }
    QWidget::mousePressEvent(e);
}

void CaptionBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    update();
    if (e->button() == Qt::LeftButton && i >= 0 && i == hitTest(e->pos())) {
        switch (i) {
        case 0: emit minimizeClicked(); break;
        case 1: emit maximizeClicked(); break;
        case 2: emit closeClicked(); break;
        default: break;
        }
    }
    QWidget::mouseReleaseEvent(e);
}

void CaptionBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void CaptionBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && hitTest(e->pos()) < 0) {
        emit maximizeClicked();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CaptionBar::contextMenuEvent(QContextMenuEvent *e)
{
    emit systemMenuRequested(e->globalPos());
    e->accept();
}

void CaptionBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    setPointerCursor(this, false);
    update();
}

// ===========================================================================
// 命令栏
// ===========================================================================

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(34);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ToolBar::setShowModeIcon(int index)
{
    m_showModeIcon = qBound(0, index, 4);
}

void ToolBar::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    update();
}

QVector<ToolBar::Btn> ToolBar::buttons() const
{
    QVector<Btn> out;
    const int y = 4;
    const int h = 26;
    out.append({ 0, QStringLiteral("打开"), QRect(8, y, 68, h), true, false });
    out.append({ 1, QStringLiteral("浏览"), QRect(79, y, 72, h), false, true });
    out.append({ 2, QStringLiteral("幻灯片放映"), QRect(154, y, 96, h), false, false });

    int x = 262;
    out.append({ 3, QString(), QRect(x, y, 30, h), false, false });
    x += 33;
    out.append({ 4, QString(), QRect(x, y, 30, h), false, false });
    x += 33;
    out.append({ 5, QString(), QRect(x, y, 30, h), false, false });

    out.append({ 6, QString(), QRect(width() - 68, y, 30, h), false, false });
    out.append({ 7, QString(), QRect(width() - 38, y, 30, h), false, false });
    return out;
}

int ToolBar::hitTest(const QPoint &pos) const
{
    const QVector<Btn> btns = buttons();
    for (int i = 0; i < btns.size(); ++i) {
        if (btns[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ToolBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QStringLiteral("#232426")));
    p.fillRect(QRect(0, height() - 1, width(), 1), rgba(0, 0, 0, 82));
    p.fillRect(QRect(0, height() - 2, width(), 1), rgba(255, 255, 255, 5));

    const QVector<Btn> btns = buttons();
    for (int i = 0; i < btns.size(); ++i) {
        const Btn &b = btns[i];
        const bool hovered = (m_hover == i);
        const bool activeLock = b.id == 6 && m_locked;

        if (b.primary) {
            const QColor bg = hovered ? QColor(QStringLiteral("#0e7fb5"))
                                      : QColor(QStringLiteral("#0b6f9f"));
            p.setPen(QPen(QColor(QStringLiteral("#2485b3")), 1));
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(b.rect).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
            drawIcon(p, Icon::Folder, QRectF(b.rect.x() + 10, b.rect.center().y() - 8, 16, 16),
                     QColor(QStringLiteral("#ffffff")));
            p.setFont(uiFont(12, QFont::DemiBold));
            p.setPen(QColor(QStringLiteral("#ffffff")));
            p.drawText(QRect(b.rect.x() + 31, b.rect.y(), b.rect.width() - 35, b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, b.text);
            continue;
        }

        if (hovered || activeLock) {
            QColor bg = activeLock ? rgba(85, 192, 240, 31)
                                   : QColor(QStringLiteral("#383a3e"));
            QColor border = activeLock ? rgba(85, 192, 240, 64)
                                       : rgba(255, 255, 255, 19);
            p.setPen(QPen(border, 1));
            p.setBrush(bg);
            p.drawRoundedRect(QRectF(b.rect).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        }

        const QColor ink = activeLock ? QColor(QStringLiteral("#55c0f0"))
                                      : QColor(QStringLiteral("#d9dadd"));
        const QRectF iconRect(b.rect.x() + 9, b.rect.center().y() - 8, 16, 16);
        switch (b.id) {
        case 1: drawIcon(p, Icon::Grid, iconRect, ink); break;
        case 2: drawIcon(p, Icon::Play, iconRect, ink); break;
        case 3: drawIcon(p, Icon::Bookmark,
                         QRectF(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16), ink);
            break;
        case 4: drawIcon(p, Icon::Edit,
                         QRectF(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16), ink);
            break;
        case 5: drawIcon(p, Icon::Copy,
                         QRectF(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16), ink);
            break;
        case 6: drawIcon(p, Icon::Lock,
                         QRectF(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16), ink);
            break;
        case 7: drawIcon(p, Icon::More,
                         QRectF(b.rect.center().x() - 8, b.rect.center().y() - 8, 16, 16), ink);
            break;
        default: break;
        }

        if (b.id == 1 || b.id == 2) {
            p.setFont(uiFont(12));
            p.setPen(ink);
            p.drawText(QRect(b.rect.x() + 30, b.rect.y(),
                             b.rect.width() - (b.id == 1 ? 47 : 34), b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, b.text);
            if (b.caret) {
                drawIcon(p, Icon::ChevronDown,
                         QRectF(b.rect.right() - 18, b.rect.center().y() - 5, 10, 10),
                         QColor(QStringLiteral("#8e9297")), 1.4);
            }
        }
    }

    // 工具组前的分隔线
    const QRect slideRect = btns.size() > 2 ? btns[2].rect : QRect();
    const int sepX = slideRect.right() + 8;
    p.fillRect(QRect(sepX, 8, 1, height() - 17), rgba(255, 255, 255, 19));
}

void ToolBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_press = hitTest(e->pos());
        update();
    }
    QWidget::mousePressEvent(e);
}

void ToolBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    update();
    if (e->button() != Qt::LeftButton || i < 0 || i != hitTest(e->pos())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    const QPoint g = e->globalPosition().toPoint();
    switch (i) {
    case 0: emit openClicked(); break;
    case 1: emit browseMenuRequested(g); break;
    case 2: emit slideshowMenuRequested(g); break;
    case 3: emit bookmarkMenuRequested(g); break;
    case 4: emit editMenuRequested(g); break;
    case 5: emit copytoMenuRequested(g); break;
    case 6: emit lockToggled(); break;
    case 7: emit moreMenuRequested(g); break;
    default: break;
    }
    QWidget::mouseReleaseEvent(e);
}

void ToolBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void ToolBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    setPointerCursor(this, false);
    update();
}

// ===========================================================================
// 底部胶囊控制栏
// ===========================================================================

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(408, 72);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
}

void ControlBar::setZoomPercent(int percent)
{
    m_percent = qBound(1, percent, 3200);
    update();
}

void ControlBar::setLocked(bool locked)
{
    if (m_locked == locked) {
        return;
    }
    m_locked = locked;
    update();
}

QVector<ControlBar::Item> ControlBar::items() const
{
    return {
        { Action::Open,        QRect(41, 35, 32, 30) },
        { Action::List,        QRect(76, 35, 32, 30) },
        { Action::Fit,         QRect(124, 35, 32, 30) },
        { Action::RotateLeft,  QRect(172, 35, 32, 30) },
        { Action::RotateRight, QRect(207, 35, 32, 30) },
        { Action::Prev,        QRect(255, 35, 32, 30) },
        { Action::Next,        QRect(290, 35, 32, 30) },
        { Action::Lock,        QRect(338, 35, 32, 30) },
        { Action::ZoomOut,     QRect(12, 6, 32, 26) },
        { Action::Slider,      QRect(50, 9, 260, 20) },
        { Action::ZoomIn,      QRect(316, 6, 32, 26) },
        { Action::ZoomValue,   QRect(354, 6, 42, 26) },
    };
}

ControlBar::Action ControlBar::hitTest(const QPoint &pos) const
{
    const QVector<Item> list = items();
    for (const Item &item : list) {
        if (item.rect.contains(pos)) {
            return item.action;
        }
    }
    return Action::None;
}

int ControlBar::currentSliderPercent() const
{
    return qBound(10, m_percent, 200);
}

void ControlBar::applySliderValueFromX(int x)
{
    const qreal ratio = qBound(0.0, (x - 50.0) / 260.0, 1.0);
    const int pct = qRound(10.0 + ratio * 190.0);
    if (pct == m_percent) {
        return;
    }
    m_percent = pct;
    emit zoomRequested(pct);
    update();
}

void ControlBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    p.setPen(QPen(rgba(255, 255, 255, 33), 1));
    p.setBrush(rgba(40, 42, 45, 242));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);

    const QVector<Item> list = items();
    for (int i = 0; i < list.size(); ++i) {
        const Item &item = list[i];
        if (item.action == Action::Slider) {
            continue;
        }
        const bool hovered = (m_hover == i);
        const bool activeLock = item.action == Action::Lock && m_locked;
        if (hovered || activeLock) {
            if (activeLock) {
                p.setPen(QPen(rgba(85, 192, 240, 64), 1));
                p.setBrush(rgba(85, 192, 240, 31));
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(QColor(QStringLiteral("#45484d")));
            }
            p.drawRoundedRect(QRectF(item.rect).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        }
    }

    // 分隔线
    const int separators[] = { 115, 160, 243, 326 };
    for (int x : separators) {
        p.fillRect(QRect(x, 42, 1, 16), rgba(255, 255, 255, 23));
    }

    const QColor ink(QStringLiteral("#d7d9dc"));
    const QColor muted(QStringLiteral("#b7bbc0"));
    const QColor accent(QStringLiteral("#55c0f0"));

    for (int i = 0; i < list.size(); ++i) {
        const Item &item = list[i];
        const QColor color = (item.action == Action::Lock && m_locked) ? accent : ink;
        const QRect r = item.rect;
        switch (item.action) {
        case Action::Open:
            drawIcon(p, Icon::Folder, QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18),
                     color, 1.55);
            break;
        case Action::List:
            drawIcon(p, Icon::Grid, QRectF(r.center().x() - 8.5, r.center().y() - 8.5, 17, 17),
                     color, 1.5);
            break;
        case Action::Fit:
            drawIcon(p, Icon::Fit, QRectF(r.center().x() - 8.5, r.center().y() - 8.5, 17, 17),
                     color, 1.5);
            break;
        case Action::RotateLeft:
            drawIcon(p, Icon::RotateLeft,
                     QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18), color, 1.5);
            break;
        case Action::RotateRight:
            drawIcon(p, Icon::RotateRight,
                     QRectF(r.center().x() - 9, r.center().y() - 9, 18, 18), color, 1.5);
            break;
        case Action::Prev:
            drawIcon(p, Icon::Left, QRectF(r.center().x() - 9.5, r.center().y() - 9.5, 19, 19),
                     QColor(QStringLiteral("#f0f0f0")), 1.8);
            break;
        case Action::Next:
            drawIcon(p, Icon::Right, QRectF(r.center().x() - 9.5, r.center().y() - 9.5, 19, 19),
                     QColor(QStringLiteral("#f0f0f0")), 1.8);
            break;
        case Action::Lock:
            drawIcon(p, Icon::Lock, QRectF(r.center().x() - 8.5, r.center().y() - 8.5, 17, 17),
                     color, 1.5);
            break;
        case Action::ZoomOut:
            drawIcon(p, Icon::Minus, QRectF(r.center().x() - 8, r.center().y() - 8, 16, 16),
                     muted, 1.5);
            break;
        case Action::ZoomIn:
            drawIcon(p, Icon::Plus, QRectF(r.center().x() - 8, r.center().y() - 8, 16, 16),
                     muted, 1.5);
            break;
        case Action::ZoomValue:
            p.setFont(uiFont(11));
            p.setPen(muted);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("%1%").arg(m_percent));
            break;
        case Action::Slider: {
            const qreal trackY = r.center().y();
            p.setPen(QPen(QColor(QStringLiteral("#5d6166")), 3, Qt::SolidLine,
                          Qt::RoundCap));
            p.drawLine(QPointF(r.left(), trackY), QPointF(r.right(), trackY));
            const qreal ratio = (currentSliderPercent() - 10.0) / 190.0;
            const qreal thumbX = r.left() + ratio * r.width();
            p.setPen(QPen(accent, 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(QPointF(r.left(), trackY), QPointF(thumbX, trackY));
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(QStringLiteral("#f3f5f6")));
            p.drawEllipse(QPointF(thumbX, trackY), 5.5, 5.5);
            break;
        }
        default:
            break;
        }
    }
}

void ControlBar::mousePressEvent(QMouseEvent *e)
{
    const Action action = hitTest(e->pos());
    if (e->button() == Qt::RightButton) {
        if (action == Action::Prev) { emit prev10Clicked(); return; }
        if (action == Action::Next) { emit next10Clicked(); return; }
    }
    if (e->button() == Qt::MiddleButton) {
        if (action == Action::Prev) { emit prevFolderClicked(); return; }
        if (action == Action::Next) { emit nextFolderClicked(); return; }
    }
    if (e->button() == Qt::LeftButton) {
        m_press = static_cast<int>(action);
        if (action == Action::Slider) {
            m_dragging = true;
            applySliderValueFromX(e->pos().x());
            return;
        }
        update();
    }
    QWidget::mousePressEvent(e);
}

void ControlBar::mouseReleaseEvent(QMouseEvent *e)
{
    const Action pressed = static_cast<Action>(m_press);
    m_press = -1;
    if (m_dragging) {
        m_dragging = false;
        update();
        QWidget::mouseReleaseEvent(e);
        return;
    }
    update();
    if (e->button() != Qt::LeftButton || pressed == Action::None
        || pressed != hitTest(e->pos())) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    switch (pressed) {
    case Action::Open: emit openClicked(); break;
    case Action::List: emit listClicked(); break;
    case Action::Fit: emit fitClicked(); break;
    case Action::RotateLeft: emit rotateLeftClicked(); break;
    case Action::RotateRight: emit rotateRightClicked(); break;
    case Action::Prev: emit prevClicked(); break;
    case Action::Next: emit nextClicked(); break;
    case Action::Lock: emit lockToggled(); break;
    case Action::ZoomOut: emit zoomOutClicked(); break;
    case Action::ZoomIn: emit zoomInClicked(); break;
    case Action::ZoomValue: emit fitClicked(); break;
    default: break;
    }
    QWidget::mouseReleaseEvent(e);
}

void ControlBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        applySliderValueFromX(e->pos().x());
        return;
    }
    const QVector<Item> list = items();
    m_hover = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(e->pos())) {
            m_hover = i;
            break;
        }
    }
    if (m_hover >= 0 && list[m_hover].action == Action::Slider) {
        setCursor(Qt::SizeHorCursor);
    } else {
        setPointerCursor(this, m_hover >= 0);
    }
    update();
    QWidget::mouseMoveEvent(e);
}

void ControlBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    setPointerCursor(this, false);
    update();
}

void ControlBar::wheelEvent(QWheelEvent *e)
{
    const int step = e->angleDelta().y() > 0 ? 10 : -10;
    emit zoomRequested(qBound(10, m_percent + step, 200));
    e->accept();
}

// ===========================================================================
// 状态浮层
// ===========================================================================

StatusOverlay::StatusOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
}

void StatusOverlay::setImageInfo(const QString &text)
{
    m_info = text;
    update();
}

void StatusOverlay::setCounter(const QString &text)
{
    m_counter = text;
    update();
}

void StatusOverlay::clear()
{
    m_info.clear();
    m_counter.clear();
    update();
}

void StatusOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(uiFont(11));
    const int baselineTop = height() - 25;
    p.setPen(QColor(QStringLiteral("#797d82")));
    p.drawText(QRect(16, baselineTop, width() / 2, 16),
               Qt::AlignVCenter | Qt::AlignLeft, m_info);
    p.setPen(QColor(QStringLiteral("#85898e")));
    p.drawText(QRect(width() / 2, baselineTop, width() / 2 - 17, 16),
               Qt::AlignVCenter | Qt::AlignRight, m_counter);
}

// ===========================================================================
// 空状态
// ===========================================================================

EmptyState::EmptyState(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_NoSystemBackground);
}

QRect EmptyState::primaryRect() const
{
    const int y = (height() - 340) / 2;
    return QRect(width() / 2 - 123, y + 197, 120, 36);
}

QRect EmptyState::secondaryRect() const
{
    const int y = (height() - 340) / 2;
    return QRect(width() / 2 + 7, y + 197, 116, 36);
}

void EmptyState::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int top = (height() - 340) / 2;
    const QRectF iconRect(width() / 2.0 - 38, top, 76, 76);
    p.setPen(QPen(rgba(255, 255, 255, 31), 1));
    p.setBrush(QColor(QStringLiteral("#232323")));
    p.drawRoundedRect(iconRect, 18, 18);
    drawIcon(p, Icon::Picture,
             QRectF(iconRect.center().x() - 17, iconRect.center().y() - 17, 34, 34),
             QColor(QStringLiteral("#bdbdbd")), 1.35);

    p.setFont(uiFont(22, QFont::Medium));
    p.setPen(QColor(QStringLiteral("#f3f3f3")));
    p.drawText(QRect(0, top + 94, width(), 30), Qt::AlignCenter,
               QStringLiteral("开始查看图片"));

    p.setFont(uiFont(13));
    p.setPen(QColor(QStringLiteral("#999999")));
    p.drawText(QRect(0, top + 132, width(), 22), Qt::AlignCenter,
               QStringLiteral("把图片拖到这里，或从电脑中选择一张图片"));
    p.drawText(QRect(0, top + 154, width(), 22), Qt::AlignCenter,
               QStringLiteral("也可以直接打开整个文件夹"));

    const QRect primary = primaryRect();
    const QRect secondary = secondaryRect();

    p.setPen(QPen(QColor(QStringLiteral("#2485b3")), 1));
    p.setBrush(m_hover == 0 ? QColor(QStringLiteral("#0e7fb5"))
                            : QColor(QStringLiteral("#0b6f9f")));
    p.drawRoundedRect(QRectF(primary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    drawIcon(p, Icon::Folder,
             QRectF(primary.x() + 18, primary.center().y() - 8, 16, 16),
             QColor(Qt::white));
    p.setFont(uiFont(12, QFont::DemiBold));
    p.setPen(QColor(Qt::white));
    p.drawText(QRect(primary.x() + 40, primary.y(), primary.width() - 44, primary.height()),
               Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("打开图片"));

    p.setPen(QPen(QColor(QStringLiteral("#4a4c50")), 1));
    if (m_hover == 1) {
        p.setBrush(QColor(QStringLiteral("#383a3e")));
    } else {
        p.setBrush(Qt::NoBrush);
    }
    p.drawRoundedRect(QRectF(secondary).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(12));
    p.setPen(QColor(QStringLiteral("#d9dadd")));
    p.drawText(secondary, Qt::AlignCenter, QStringLiteral("选择文件夹"));

    // 快捷键提示
    struct KbdGroup {
        QStringList keys;
        QString label;
    };
    const QVector<KbdGroup> groups = {
        { { QStringLiteral("Ctrl"), QStringLiteral("O") }, QStringLiteral("打开图片") },
        { { QStringLiteral("F11") }, QStringLiteral("全屏查看") },
        { { QStringLiteral("F9") }, QStringLiteral("设置") },
    };
    const QFont keyFont = uiFont(11);
    const QFont labelFont = uiFont(12);
    auto groupWidth = [&](const KbdGroup &g) {
        QFontMetrics kfm(keyFont);
        QFontMetrics lfm(labelFont);
        int w = 0;
        for (const QString &key : g.keys) {
            w += kfm.horizontalAdvance(key) + 12 + 5;
        }
        return w + lfm.horizontalAdvance(g.label);
    };
    int total = 0;
    for (const KbdGroup &g : groups) {
        total += groupWidth(g);
    }
    total += 18 * (groups.size() - 1);
    int x = (width() - total) / 2;
    const int sy = top + 259;
    for (const KbdGroup &g : groups) {
        for (const QString &key : g.keys) {
            QFontMetrics kfm(keyFont);
            const int kw = kfm.horizontalAdvance(key) + 12;
            const QRect kr(x, sy, kw, 20);
            p.setPen(QPen(QColor(QStringLiteral("#444444")), 1));
            p.setBrush(QColor(QStringLiteral("#292929")));
            p.drawRoundedRect(QRectF(kr).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
            p.setFont(keyFont);
            p.setPen(QColor(QStringLiteral("#aaaaaa")));
            p.drawText(kr, Qt::AlignCenter, key);
            x += kw + 5;
        }
        p.setFont(labelFont);
        p.setPen(QColor(QStringLiteral("#737373")));
        QFontMetrics lfm(labelFont);
        const int lw = lfm.horizontalAdvance(g.label);
        p.drawText(QRect(x, sy, lw, 20), Qt::AlignVCenter | Qt::AlignLeft, g.label);
        x += lw + 18;
    }
}

void EmptyState::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (primaryRect().contains(e->pos())) m_press = 0;
        else if (secondaryRect().contains(e->pos())) m_press = 1;
        update();
    }
    QWidget::mousePressEvent(e);
}

void EmptyState::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    update();
    if (e->button() == Qt::LeftButton) {
        if (i == 0 && primaryRect().contains(e->pos())) emit openClicked();
        if (i == 1 && secondaryRect().contains(e->pos())) emit folderClicked();
    }
    QWidget::mouseReleaseEvent(e);
}

void EmptyState::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = primaryRect().contains(e->pos()) ? 0
              : (secondaryRect().contains(e->pos()) ? 1 : -1);
    setPointerCursor(this, m_hover >= 0);
    update();
    QWidget::mouseMoveEvent(e);
}

void EmptyState::leaveEvent(QEvent *)
{
    m_hover = -1;
    setPointerCursor(this, false);
    update();
}

// ===========================================================================
// Toast
// ===========================================================================

Toast::Toast(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::showMessage(const QString &text)
{
    m_text = text;
    if (parentWidget()) {
        setGeometry(0, y(), parentWidget()->width(), 56);
    }
    show();
    raise();
    m_timer->start(2000);
    update();
}

void Toast::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setFont(uiFont(12));
    const QFontMetrics fm = p.fontMetrics();
    const int w = qMin(parentWidget() ? parentWidget()->width() - 40 : 360,
                       fm.horizontalAdvance(m_text) + 32);
    const QRectF r((width() - w) / 2.0, 0, w, 30);
    p.setPen(QPen(QColor(QStringLiteral("#555555")), 1));
    p.setBrush(QColor(QStringLiteral("#383838")));
    p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 7, 7);
    p.setPen(QColor(QStringLiteral("#eeeeee")));
    p.drawText(r, Qt::AlignCenter, m_text);
}

// ===========================================================================
// ZoomBadge
// ===========================================================================

ZoomBadge::ZoomBadge(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        m_fade->start();
    });
    m_opacity = new QGraphicsOpacityEffect(this);
    m_opacity->setOpacity(1.0);
    setGraphicsEffect(m_opacity);
    m_fade = new QPropertyAnimation(m_opacity, "opacity", this);
    m_fade->setDuration(320);
    m_fade->setStartValue(1.0);
    m_fade->setEndValue(0.0);
    connect(m_fade, &QPropertyAnimation::finished, this, &QWidget::hide);
}

void ZoomBadge::showText(const QString &text)
{
    m_text = text;
    m_fade->stop();
    m_opacity->setOpacity(1.0);
    const QFontMetrics fm(uiFont(11));
    setFixedSize(fm.horizontalAdvance(text) + 24, 24);
    show();
    raise();
    m_timer->start(1500);
    update();
}

void ZoomBadge::paintEvent(QPaintEvent *)
{
    if (m_text.isEmpty()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(rgba(255, 255, 255, 26), 1));
    p.setBrush(QColor(QStringLiteral("#282a2d")));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
    p.setFont(uiFont(11));
    p.setPen(QColor(QStringLiteral("#d7d9dc")));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace Chrome
