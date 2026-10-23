#include "Chrome.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPropertyAnimation>
#include <QTimer>
#include <QWindow>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

namespace Chrome {

// ===========================================================================
// 皮肤贴图
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
    setFixedHeight(25);
}

void CaptionBar::setTitle(const QString &title)
{
    m_title = title;
    update();
}

QVector<QRect> CaptionBar::buttonRects() const
{
    const int w = width();
    const int closeW = 50;
    const int btnW = 24;
    QVector<QRect> r(4);
    r[3] = QRect(w - closeW, 2, closeW, 21);              // close
    r[2] = QRect(r[3].x() - btnW, 2, btnW, 21);           // max
    r[1] = QRect(r[2].x() - btnW, 2, btnW, 21);           // min
    r[0] = QRect(r[1].x() - btnW, 2, btnW, 21);           // full
    return r;
}

int CaptionBar::hitTest(const QPoint &pos) const
{
    const QVector<QRect> rects = buttonRects();
    for (int i = 0; i < rects.size(); ++i) {
        if (rects[i].contains(pos)) {
            return i;
        }
    }
    return -1;
}

void CaptionBar::startDrag()
{
    if (window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
    }
}

void CaptionBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // 标题栏底：#ababab（纯色填充，无贴图）
    p.fillRect(rect(), QColor("#ababab"));

    // 彩色 logo（btn_logo.png 帧0，缩到 18x16）
    QPixmap logo = skinFrame(QStringLiteral("btn_logo.png"), 0);
    if (!logo.isNull()) {
        p.drawPixmap(QRect(6, (height() - 16) / 2, 18, 16), logo,
                     QRect(0, 0, logo.width(), logo.height()));
    }
    // 标题
    QFont f(QStringLiteral("Segoe UI"), 9);
    p.setFont(f);
    p.setPen(QColor("#282828"));
    p.drawText(QRect(30, 0, width() - 220, height()),
               Qt::AlignVCenter | Qt::AlignLeft, m_title);

    // 右侧按钮
    const QVector<QRect> rects = buttonRects();
    const char *names[] = { "btn_full.png", "btn_minimize.png", "btn_maximize.png",
                            "btn_close.png" };
    for (int i = 0; i < rects.size(); ++i) {
        if (i == 3) {
            // btn_close.png 是 4 帧 x 50px（红底白X），与 skinFrame 的正方形帧
            // 假设不符（200/21=9 会切成 22px 碎片）——按真实帧宽 50 手动切。
            // 帧0 常态红底 / 帧1 悬停亮红 / 帧2 按下暗红 / 帧3 透明底X。
            QPixmap all = skinPixmap(QStringLiteral("btn_close.png"));
            if (!all.isNull()) {
                const int fw = all.width() / 4;
                const int f = (m_press == i) ? 2 : (m_hover == i ? 1 : 0);
                p.drawPixmap(rects[i].topLeft(), all,
                             QRect(f * fw, 0, fw, all.height()));
            } else {
                p.fillRect(rects[i], QColor("#c75050"));
            }
        } else {
            if (m_hover == i) {
                p.fillRect(rects[i], QColor(255, 255, 255, 60));
            }
            QPixmap pm = skinFrame(QLatin1String(names[i]), (m_hover == i) ? 1 : 0);
            if (!pm.isNull()) {
                p.drawPixmap(rects[i].topLeft(), pm);
            }
        }
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
        m_dragging = true;
        startDrag();
        return;
    }
    QWidget::mousePressEvent(e);
}

void CaptionBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    update();
    if (e->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    if (i == hitTest(e->pos())) {
        switch (i) {
        case 0: emit fullscreenClicked(); break;
        case 1: emit minimizeClicked(); break;
        case 2: emit maximizeClicked(); break;
        case 3: emit closeClicked(); break;
        default: break;
        }
    }
    m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}

void CaptionBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    update();
    QWidget::mouseMoveEvent(e);
}

void CaptionBar::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (hitTest(e->pos()) < 0) {
        emit maximizeClicked();
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CaptionBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ===========================================================================
// 工具栏
// ===========================================================================

ToolBar::ToolBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedHeight(31);
}

void ToolBar::setShowModeIcon(int index)
{
    m_showModeIcon = qBound(0, index, 4);
    update();
}

QVector<ToolBar::Btn> ToolBar::buttons() const
{
    // 右对齐，右缘距窗口右 22px（布局参数沿用 5.53 的可观察行为）
    QFont f(QStringLiteral("Microsoft YaHei UI"), 9);
    const QFontMetrics fm(f);
    struct Def { const char *text; bool isLock; bool isShowmode; };
    static const Def defs[] = {
        { "浏览",       false, true  },
        { "幻灯片放映", false, false },
        { "书签",       false, false },
        { "编辑",       false, false },
        { "复制到",     false, false },
        { "锁定",       true,  false },
    };
    QVector<Btn> out;
    int x = width() - 22;
    for (int i = 5; i >= 0; --i) {
        const Def &d = defs[i];
        const QString text = QString::fromUtf8(d.text);   // 源码 UTF-8，勿用 Latin-1
        int bw = fm.horizontalAdvance(text) + 26;
        if (d.isShowmode) {
            bw += 20;   // 模式图标
        }
        if (d.isLock) {
            bw += 16;   // 圆勾图标
        }
        out.append({ text, d.isLock, d.isShowmode, QRect(x - bw, 0, bw, height()) });
        x -= bw + 2;
    }
    std::reverse(out.begin(), out.end());   // 索引顺序 = 从左到右
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
    // 整条工具栏贴图平铺（含渐变与斜纹）
    QPixmap tile = skinPixmap(QStringLiteral("static_toolbar1.png"));
    if (!tile.isNull()) {
        for (int x = 0; x < width(); x += tile.width()) {
            p.drawPixmap(x, 0, tile);
        }
    } else {
        QLinearGradient g(0, 0, 0, height());
        g.setColorAt(0, QColor("#efefef"));
        g.setColorAt(1, QColor("#dedede"));
        p.fillRect(rect(), QBrush(g));
    }

    QFont f(QStringLiteral("Microsoft YaHei UI"), 9);
    p.setFont(f);
    const QVector<Btn> btns = buttons();
    for (int i = 0; i < btns.size(); ++i) {
        const Btn &b = btns[i];
        if (m_hover == i) {
            p.fillRect(b.rect, QColor("#d3ecfc"));
        }
        p.setPen(QColor("#333333"));
        const int cy = b.rect.center().y();
        if (b.isLock) {
            // 文字 + 深灰实心圆白勾（对照原版 ⊙✓，恒深灰非状态指示）
            p.drawText(QRect(b.rect.x(), b.rect.y(), b.rect.width() - 22, b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignRight, b.text);
            const QRect cr(b.rect.right() - 19, cy - 8, 16, 16);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#555555"));
            p.drawEllipse(cr);
            p.setPen(QPen(QColor("#ffffff"), 1.8, Qt::SolidLine, Qt::RoundCap,
                          Qt::RoundJoin));
            const qreal x0 = cr.left(), y0 = cr.top();
            p.drawLine(QPointF(x0 + 4.0, y0 + 8.5), QPointF(x0 + 6.8, y0 + 11.2));
            p.drawLine(QPointF(x0 + 6.8, y0 + 11.2), QPointF(x0 + 12.0, y0 + 5.0));
            p.setRenderHint(QPainter::Antialiasing, false);
        } else {
            int tx = b.rect.x() + 6;
            if (b.isShowmode) {
                QPixmap sm = skinPixmap(QStringLiteral("static_showmode_%1.png")
                                            .arg(m_showModeIcon));
                if (!sm.isNull()) {
                    const QPixmap scaled = sm.scaled(16, 18, Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
                    p.drawPixmap(tx, cy - scaled.height() / 2, scaled);
                    tx += scaled.width() + 5;
                }
            }
            p.drawText(QRect(tx, b.rect.y(), b.rect.width() - (tx - b.rect.x()) - 16,
                             b.rect.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, b.text);
            // 下拉小三角 ▼
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#555555"));
            const double ax = b.rect.right() - 11;
            const double ay = cy - 2;
            QPolygonF tri;
            tri << QPointF(ax, ay) << QPointF(ax + 7, ay) << QPointF(ax + 3.5, ay + 4.5);
            p.drawPolygon(tri);
            p.setRenderHint(QPainter::Antialiasing, false);
        }
    }
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
    const QPoint g(e->globalPosition().toPoint());
    switch (i) {
    case 0: emit browseMenuRequested(g); break;
    case 1: emit slideshowMenuRequested(g); break;
    case 2: emit bookmarkMenuRequested(g); break;
    case 3: emit editMenuRequested(g); break;
    case 4: emit copytoMenuRequested(g); break;
    case 5: emit lockToggled(); break;
    default: break;
    }
    QWidget::mouseReleaseEvent(e);
}

void ToolBar::mouseMoveEvent(QMouseEvent *e)
{
    m_hover = hitTest(e->pos());
    update();
    QWidget::mouseMoveEvent(e);
}

void ToolBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ===========================================================================
// 胶囊控制栏 —— 整图贴图方案（对照 quikview-controlbar-plan-2026-10-14.md）：
// 底图 pill_zoom.png 一张（缺图时回退 static_controlpanel.png），
// 按钮/滑轨/面板全部烘焙在图里，热区由 items() 给出；滑块视觉尚未实现（见 §4.2）。
// 禁止 QSS、禁止手绘任何本有贴图的元素。
// ===========================================================================

namespace {
// 原版引擎把 460x80 逻辑布局整体放大显示（真机实测 ~562x90）。
// 渲染期一次性缩放因子：逻辑坐标不变，热区坐标反算。
constexpr double kPillSX = 562.0 / 460.0;
constexpr double kPillSY = 90.0 / 80.0;
QPoint toLogical(const QPoint &physical)
{
    return QPoint(qRound(physical.x() / kPillSX), qRound(physical.y() / kPillSY));
}
} // namespace

ControlBar::ControlBar(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setFixedSize(562, 90);
}

void ControlBar::setZoomPercent(int percent)
{
    // 百分比 -> 滑轨值：v = 20*log2(pct/100)，值域 -100..100
    percent = qBound(3, percent, 3200);
    m_value = qRound(qBound(-100.0, 20.0 * std::log2(percent / 100.0), 100.0));
    update();
}

QRect ControlBar::sliderTrackRect() const
{
    // 底图自带滑轨凹槽（像素实测 x 132..327）与下排分隔竖线（x=194/266）
    return QRect(132, 11, 195, 24);
}

void ControlBar::applySliderValueFromX(int x)
{
    const QRect trackR = sliderTrackRect();
    m_value = qRound(qBound(-100.0,
                            (x - trackR.center().x()) / (trackR.width() / 2.0) * 100.0,
                            100.0));
    const double pct = 100.0 * std::pow(2.0, m_value / 20.0);
    emit zoomRequested(qRound(pct));
    update();
}

QVector<ControlBar::Item> ControlBar::items() const
{
    // 460x80 原生坐标系。锁死的值：open x=14、next x=400（446=460-14）。
    // 下排四钮与锁定由原版 hv_pill 像素实测反推（图形中心对齐，2026-10-15）：
    // + c125.5 / − c156.5 / ↺ c199 / ↻ c231 / "锁定"右缘 293 / 勾圆 301-316。
    return {
        { "open",    QRect(14, 8, 46, 46) },
        { "list",    QRect(64, 8, 46, 46) },
        { "slider",  sliderTrackRect() },
        { "zoomin",  QRect(112, 46, 28, 28) },
        { "zoomout", QRect(143, 46, 28, 28) },
        { "rotl",    QRect(186, 46, 28, 28) },
        { "rotr",    QRect(217, 46, 28, 28) },
        { "lock",    QRect(253, 46, 72, 28) },
        { "prev",    QRect(349, 8, 46, 46) },
        { "next",    QRect(400, 8, 46, 46) },
    };
}

int ControlBar::hitTest(const QPoint &pos) const
{
    const QVector<Item> list = items();
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].rect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ControlBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.scale(kPillSX, kPillSY);   // 460x80 逻辑坐标 -> 562x90 显示

    // 整图胶囊方案（pill_zoom.png，460x80 逻辑系的 ~3.65x 等比放大版）：
    // 全部按钮视觉已烘焙在图里，本版不画任何按下/悬停循环帧（z_btn_* 素材
    // 留待后续接入）。热区与信号照旧，点击功能不受影响。
    QPixmap pill = skinPixmap(QStringLiteral("pill_zoom.png"));
    if (!pill.isNull()) {
        p.drawPixmap(QRectF(0, 0, 460, 80), pill,
                     QRectF(0, 0, pill.width(), pill.height()));
        return;
    }
    // fallback：skin 目录无整图时退回旧底图
    QPixmap old = skinPixmap(QStringLiteral("static_controlpanel.png"));
    if (!old.isNull()) {
        p.drawPixmap(0, 0, old);
    }
}

void ControlBar::mousePressEvent(QMouseEvent *e)
{
    const QPoint lp = toLogical(e->pos());
    // 右键/中键点 ‹ ›：prev10/next10（CommandR）、prevFolder/nextFolder（CommandM），
    // 无需按下帧反馈
    if (e->button() == Qt::RightButton) {
        const int i = hitTest(lp);
        if (i == 8) { emit prev10Clicked(); return; }
        if (i == 9) { emit next10Clicked(); return; }
    }
    if (e->button() == Qt::MiddleButton) {
        const int i = hitTest(lp);
        if (i == 8) { emit prevFolderClicked(); return; }
        if (i == 9) { emit nextFolderClicked(); return; }
    }
    if (e->button() == Qt::LeftButton) {
        m_press = hitTest(lp);
        if (m_press == 2) {          // 滑轨：点击即跳值并进入拖动
            m_dragging = true;
            applySliderValueFromX(lp.x());
            return;
        }
        update();
    }
    QWidget::mousePressEvent(e);
}

void ControlBar::mouseReleaseEvent(QMouseEvent *e)
{
    const int i = m_press;
    m_press = -1;
    if (m_dragging) {
        m_dragging = false;
        update();
        QWidget::mouseReleaseEvent(e);
        return;
    }
    update();
    if (e->button() != Qt::LeftButton || i < 0 || i != hitTest(toLogical(e->pos()))) {
        QWidget::mouseReleaseEvent(e);
        return;
    }
    switch (i) {
    case 0: emit openClicked(); break;
    case 1: emit listClicked(); break;
    case 3: emit zoomInClicked(); break;
    case 4: emit zoomOutClicked(); break;
    case 5: emit rotateLeftClicked(); break;
    case 6: emit rotateRightClicked(); break;
    case 7: emit lockToggled(); break;
    case 8: emit prevClicked(); break;
    case 9: emit nextClicked(); break;
    default: break;
    }
    QWidget::mouseReleaseEvent(e);
}

void ControlBar::mouseMoveEvent(QMouseEvent *e)
{
    const QPoint lp = toLogical(e->pos());
    if (m_dragging && (e->buttons() & Qt::LeftButton)) {
        applySliderValueFromX(lp.x());
        return;
    }
    m_hover = hitTest(lp);
    update();
    QWidget::mouseMoveEvent(e);
}

void ControlBar::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ===========================================================================
// Toast
// ===========================================================================

Toast::Toast(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &QWidget::hide);
}

void Toast::showMessage(const QString &text)
{
    m_text = text;
    setGeometry(0, 12, parentWidget() ? parentWidget()->width() : 400, 56);
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
    p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    const QFontMetrics fm = p.fontMetrics();
    const int w = qMin(parentWidget() ? parentWidget()->width() - 40 : 360,
                       fm.horizontalAdvance(m_text) + 48);
    const QRectF r((width() - w) / 2.0, 0, w, 30);
    QColor c("#1e1e1e");
    c.setAlpha(215);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(r, 8, 8);
    p.setPen(QColor("#e8e8e8"));
    p.drawText(r, Qt::AlignCenter, m_text);
}

// ===========================================================================
// ZoomBadge（画布左下角缩放浮标）
// ===========================================================================

ZoomBadge::ZoomBadge(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
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
    const QFontMetrics fm(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    setFixedSize(fm.horizontalAdvance(text) + 28, 26);
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
    QColor c("#1e1e1e");
    c.setAlpha(215);
    p.setPen(Qt::NoPen);
    p.setBrush(c);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
    p.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 9));
    p.setPen(QColor("#e8e8e8"));
    p.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace Chrome
