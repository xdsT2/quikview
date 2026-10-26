#include "ImageCanvas.h"

#include <QGraphicsOpacityEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QCursor>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTextStream>
#include <QWheelEvent>

#include <cmath>
#include <vector>

namespace {

// 分离式 Lanczos3 重采样：先水平后垂直，两个方向各 3-lobe 窗口。
// 只用于「缩小显示」——原版把 Lanczos 标成「慢，高品质」，正是因为它比
// GPU 的双线性/双三次慢一个量级，换来的是缩略时更少的振铃/摩尔纹。
//
// 权重只取决于「tap 相对采样中心的偏移」，与行/列无关：进卷积前按输出
// 列/行各预算一张 tap 表（clamp 后的源索引 + double 权重 + 归一化倒数），
// 内层循环只剩 ≤6 次乘加，无 sin()/pow()/浮点除法。权重沿用与旧实现
// 逐位相同的表达式和求和顺序，输出与旧实现逐字节一致。
// 不用 Q12 定点：权重会被量化，破坏逐字节一致这一验收线。
struct ResampleTaps {
    int count = 0;      // 非零权重 tap 数（≤6；零权重 tap 与旧实现一样跳过）
    int idx[6];         // clamp 后的源索引
    double w[6];        // Lanczos3 权重
    double inv = 1.0;   // 1/wsum，预算好，像素循环里零除法
};

double lanczos3Kernel(double x)
{
    if (qFuzzyIsNull(x)) {
        return 1.0;
    }
    if (x <= -3.0 || x >= 3.0) {
        return 0.0;
    }
    const double px = M_PI * x;
    return (3.0 * std::sin(px) * std::sin(px / 3.0)) / (px * px);
}

ResampleTaps buildResampleTaps(double center, int srcLen)
{
    ResampleTaps t;
    const int start = static_cast<int>(std::floor(center)) - 2;
    double wsum = 0.0;
    for (int i = 0; i < 6; ++i) {
        const int xx = start + i;
        const double wgt = lanczos3Kernel(xx - center);
        if (wgt == 0.0) {
            continue;
        }
        t.idx[t.count] = qBound(0, xx, srcLen - 1);
        t.w[t.count] = wgt;
        wsum += wgt;
        ++t.count;
    }
    t.inv = wsum == 0.0 ? 1.0 : 1.0 / wsum;
    return t;
}

QImage lanczosResample(const QImage &src, const QSize &dst)
{
    if (src.isNull() || dst.isEmpty() || dst == src.size()) {
        return src;
    }
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    const int sw = s.width();
    const int sh = s.height();
    const int dw = dst.width();
    const int dh = dst.height();

    // —— 预算权重表（每个输出列/行一次，与像素量无关） ——
    const double rx = static_cast<double>(dw) / sw;
    std::vector<ResampleTaps> colTaps;
    colTaps.reserve(dw);
    for (int x = 0; x < dw; ++x) {
        colTaps.push_back(buildResampleTaps((x + 0.5) / rx - 0.5, sw));
    }
    const double ry = static_cast<double>(dh) / sh;
    std::vector<ResampleTaps> rowTaps;
    rowTaps.reserve(dh);
    for (int y = 0; y < dh; ++y) {
        rowTaps.push_back(buildResampleTaps((y + 0.5) / ry - 0.5, sh));
    }

    // —— 水平 ——
    QImage tmp(dw, sh, QImage::Format_ARGB32);
    for (int y = 0; y < sh; ++y) {
        const QRgb *lrow = reinterpret_cast<const QRgb *>(s.constScanLine(y));
        QRgb *orow = reinterpret_cast<QRgb *>(tmp.scanLine(y));
        for (int x = 0; x < dw; ++x) {
            const ResampleTaps &t = colTaps[x];
            double a = 0.0, r = 0.0, g = 0.0, b = 0.0;
            for (int i = 0; i < t.count; ++i) {
                const QRgb p = lrow[t.idx[i]];
                const double wgt = t.w[i];
                a += qAlpha(p) * wgt;
                r += qRed(p) * wgt;
                g += qGreen(p) * wgt;
                b += qBlue(p) * wgt;
            }
            orow[x] = qRgba(qBound(0, qRound(r * t.inv), 255), qBound(0, qRound(g * t.inv), 255),
                            qBound(0, qRound(b * t.inv), 255), qBound(0, qRound(a * t.inv), 255));
        }
    }

    // —— 垂直 ——
    // 按累加器重排：每个输出像素的 4 个通道累加器仍按 tap 顺序累加（与旧实现
    // 逐位一致），但把 tap 循环提到外层、x 连续访存，且源行指针逐行预取，
    // 避免在像素循环里反复 constScanLine 与跨行随机访问。
    QImage out(dw, dh, QImage::Format_ARGB32);
    std::vector<double> accA(dw), accR(dw), accG(dw), accB(dw);
    for (int y = 0; y < dh; ++y) {
        const ResampleTaps &t = rowTaps[y];
        const QRgb *srcrows[6];
        for (int i = 0; i < t.count; ++i) {
            srcrows[i] = reinterpret_cast<const QRgb *>(tmp.constScanLine(t.idx[i]));
        }
        std::fill(accA.begin(), accA.end(), 0.0);
        std::fill(accR.begin(), accR.end(), 0.0);
        std::fill(accG.begin(), accG.end(), 0.0);
        std::fill(accB.begin(), accB.end(), 0.0);
        for (int i = 0; i < t.count; ++i) {
            const QRgb *src = srcrows[i];
            const double wgt = t.w[i];
            for (int x = 0; x < dw; ++x) {
                const QRgb p = src[x];
                accA[x] += qAlpha(p) * wgt;
                accR[x] += qRed(p) * wgt;
                accG[x] += qGreen(p) * wgt;
                accB[x] += qBlue(p) * wgt;
            }
        }
        QRgb *orow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < dw; ++x) {
            orow[x] = qRgba(qBound(0, qRound(accR[x] * t.inv), 255),
                            qBound(0, qRound(accG[x] * t.inv), 255),
                            qBound(0, qRound(accB[x] * t.inv), 255),
                            qBound(0, qRound(accA[x] * t.inv), 255));
        }
    }
    return out;
}

// 空态标识：同心圆环 + 右下短斜线，与 resources/quikview.ico 同一视觉语汇。
// 自有几何图形（不使用任何第三方 logo 的构形）。
QPixmap makePlaceholderMark(int S, const QColor &color)
{
    QPixmap pm(S, S);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(color, qMax(1.0, S / 20.0)));
    const QPointF ctr(S / 2.0, S / 2.0);
    const double outer = 0.30 * S;
    p.drawEllipse(ctr, outer, outer);
    p.drawEllipse(ctr, outer * 0.52, outer * 0.52);
    p.drawLine(QPointF(ctr.x() + outer * 0.60, ctr.y() + outer * 0.60),
               QPointF(ctr.x() + outer * 0.98, ctr.y() + outer * 0.98));
    return pm;
}

} // namespace

// ---------------------------------------------------------------------------
// 侧边翻页箭头（原型 .side-nav：48x88 命中区视觉、30px 白色雪佛龙、贴边 6px、
// 垂直居中）。纯视觉：WA_TransparentForMouseEvents，命中判定在画布上；
// 显隐带 160ms 淡入淡出（对齐铬件 180ms 量级）。
// ---------------------------------------------------------------------------
class ImageCanvas::SideArrow : public QWidget
{
public:
    enum Dir { Prev, Next };

    SideArrow(Dir dir, QWidget *parent)
        : QWidget(parent), m_dir(dir)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFixedSize(48, 88);
        m_effect = new QGraphicsOpacityEffect(this);
        m_effect->setOpacity(0.0);
        setGraphicsEffect(m_effect);
        m_fade = new QPropertyAnimation(m_effect, "opacity", this);
        m_fade->setDuration(160);
        connect(m_fade, &QPropertyAnimation::finished, this, [this]() {
            if (!m_shown) {
                hide();
            }
        });
    }

    void setShown(bool on)
    {
        if (on == m_shown) {
            return;
        }
        m_shown = on;
        m_fade->stop();
        m_fade->setStartValue(m_effect->opacity());
        m_fade->setEndValue(on ? 1.0 : 0.0);
        if (on) {
            show();
        }
        m_fade->start();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        // 24 单位图标放大到 30px；先压一层深色投影（亮图上保底可读），再画主色
        const qreal s = 30.0 / 24.0;
        const QPointF c = rect().center();
        const QPointF a((m_dir == Prev ? 15 : 9) - 12, 5 - 12);
        const QPointF b((m_dir == Prev ? 8 : 15) - 12, 12 - 12);
        const QPointF d((m_dir == Prev ? 15 : 9) - 12, 19 - 12);
        struct Pass {
            QColor color;
            qreal width;
            QPointF offset;
        };
        const Pass passes[] = {
            { QColor(0, 0, 0, 120), 2.6 * s, QPointF(0, 1.2) },
            { QColor(240, 243, 245, 185), 1.8 * s, QPointF(0, 0) },
        };
        for (const Pass &pass : passes) {
            QPen pen(pass.color, pass.width);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawLine(c + a * s + pass.offset, c + b * s + pass.offset);
            p.drawLine(c + b * s + pass.offset, c + d * s + pass.offset);
        }
    }

private:
    Dir m_dir;
    QGraphicsOpacityEffect *m_effect = nullptr;
    QPropertyAnimation *m_fade = nullptr;
    bool m_shown = false;
};

ImageCanvas::ImageCanvas(QWidget *parent)
    : QGraphicsView(parent)
{
    setScene(new QGraphicsScene(this));
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    // 原版无可见滚动条（平移走左键拖动/滚轮缩放）
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setBackgroundColor(QColor(QStringLiteral("#161719")));

    m_logo = new QLabel(viewport());
    m_logo->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_logo->setPixmap(makePlaceholderMark(88, QColor("#626262")));
    m_logo->adjustSize();
    m_logo->move((viewport()->width() - m_logo->width()) / 2,
                 (viewport()->height() - m_logo->height()) / 2);

    // 侧边翻页箭头（原版 bDoNotShowSideArrow=0 的视觉）：纯视觉不接鼠标，
    // 命中判定始终在画布上；悬停无按键 move 事件依赖 viewport 的 mouse tracking
    viewport()->setMouseTracking(true);
    m_arrowPrev = new SideArrow(SideArrow::Prev, viewport());
    m_arrowNext = new SideArrow(SideArrow::Next, viewport());
    m_arrowPrev->hide();
    m_arrowNext->hide();

    // 交互收尾：拖窗口边界 / 连续缩放停止 150ms 后补做一次重采样（只补当前 m_zoom）
    m_sampleFlush.setSingleShot(true);
    m_sampleFlush.setInterval(150);
    connect(&m_sampleFlush, &QTimer::timeout, this, [this]() {
        m_resizeBurst = false;
        doRefreshSampling();
    });
}

void ImageCanvas::setLogoVisible(bool on)
{
    if (m_logo) {
        m_logo->setVisible(on);
    }
}

// 画布底色：brush + stylesheet 必须一起改。置了 stylesheet 的 QGraphicsView 用
// 样式里的 background 画视口，backgroundBrush 会被盖掉——只改 brush 底色不变，
// 只改 stylesheet 又会让 palette 与 brush 不同步（其它按 brush 取色的地方会错）。
void ImageCanvas::setBackgroundColor(const QColor &color)
{
    if (!color.isValid() || color == m_backgroundColor) {
        return;
    }
    m_backgroundColor = color;
    setBackgroundBrush(color);
    setStyleSheet(QStringLiteral("QGraphicsView{background:%1;border:0;}")
                      .arg(color.name()));
    viewport()->update();
}

void ImageCanvas::setBorderColor(const QColor &color)
{
    if (m_borderColor == color) {
        return;
    }
    m_borderColor = color;
    viewport()->update();   // 边框只在 paintEvent 里现画，重绘即生效
}

// 侧边箭头开关：只关「视觉」，不关命中——edgeTurnDelta 走箭头矩形，矩形仍由
// placeEdgeArrows 摆在原位，所以两侧点击照旧翻页（光标也仍是手型，否则开关一关
// 用户就再也找不到翻页热区了）。显示箭头的唯一路径是 updateEdgeHover()，
// 因此那里判一次即可覆盖 resize/scroll/show/crop 等全部刷新时机。
void ImageCanvas::setSideArrowsEnabled(bool on)
{
    if (m_sideArrowsEnabled == on) {
        return;
    }
    m_sideArrowsEnabled = on;
    if (!m_arrowPrev || !m_arrowNext) {
        return;
    }
    if (!on) {
        // 关：可能正淡入/显示着，立即收掉（淡出动画由 SideArrow 自己收尾）
        m_arrowPrev->setShown(false);
        m_arrowNext->setShown(false);
        return;
    }
    refreshEdgeHover();   // 开：按当前真实光标位置补一次，光标停在带内就立刻显示
}

void ImageCanvas::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);
    // 边框画在视口 1px 内沿：位置只和视口尺寸有关（与 scene 的缩放/平移无关），
    // 所以每次重绘现算，不缓存任何几何。m_borderColor 无效时等于「不画边框」。
    if (!m_borderColor.isValid()) {
        return;
    }
    QPainter p(viewport());
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(m_borderColor, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(viewport()->rect().adjusted(0, 0, -1, -1));
}

void ImageCanvas::setPixmap(const QPixmap &pixmap, bool fitAfter)
{
    m_sourcePixmap = pixmap;
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    m_samplingSize = QSize();   // 换图后旧的目标尺寸命中必是假命中（源都换了）
    // QGraphicsPixmapItem 默认 FastTransformation——painter 的
    // SmoothPixmapTransform 不覆盖 item 自身采样，缩放必须显式开平滑。
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    m_zoom = 1.0;
    resetTransform();
    // QGraphicsScene 的 sceneRect 只涨不缩：换图后必须显式重置成新图尺寸，
    // 否则小于视口的图会按旧矩形的中心对齐，表现为「图偏在左边」。
    scene()->setSceneRect(m_item->boundingRect());
    if (fitAfter) {
        fitToWindow();
    }
    if (m_logo) {
        m_logo->hide();
    }
    refreshEdgeHover();   // 空态→有图等切换后，按当前光标位置重算箭头
}

void ImageCanvas::setPixmapPreservingView(const QPixmap &pixmap, const QPointF &anchorNorm,
                                          qreal zoom, bool fitWindow, bool fitWidth)
{
    const bool hadItem = (m_item != nullptr);
    m_sourcePixmap = pixmap;
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    m_samplingSize = QSize();   // 换图后旧的目标尺寸命中必是假命中（源都换了）
    resetTransform();
    scene()->setSceneRect(m_item->boundingRect());   // 同 setPixmap：防止沿用旧矩形
    if (!hadItem || fitWindow) {
        fitToWindow();
        refreshEdgeHover();   // 空态→有图：按当前光标位置重算箭头
        return;
    }
    if (fitWidth) {
        fitToWidth();
        refreshEdgeHover();
        return;
    }
    const qreal z = zoom > 0.0 ? zoom : 1.0;
    scale(z, z);
    m_zoom = z;
    m_fitToWindow = false;
    m_fitWidth = false;
    const QRectF r = m_item->sceneBoundingRect();
    centerOn(r.left() + qBound(0.0, anchorNorm.x(), 1.0) * r.width(),
             r.top() + qBound(0.0, anchorNorm.y(), 1.0) * r.height());
    emit zoomChanged(m_zoom);
    refreshEdgeHover();
}

QPointF ImageCanvas::viewCenterNormalized() const
{
    if (!m_item) {
        return QPointF(0.5, 0.5);
    }
    const QRectF r = m_item->sceneBoundingRect();
    if (r.width() <= 0.0 || r.height() <= 0.0) {
        return QPointF(0.5, 0.5);
    }
    const QPointF c = mapToScene(viewport()->rect().center());
    return QPointF(qBound(0.0, (c.x() - r.left()) / r.width(), 1.0),
                   qBound(0.0, (c.y() - r.top()) / r.height(), 1.0));
}

void ImageCanvas::clear()
{
    scene()->clear();
    m_item = nullptr;
    m_rubber = nullptr;
    if (m_logo) {
        m_logo->show();
    }
}

void ImageCanvas::setContentMargins(const QMargins &margins)
{
    // 边距变化会改变可视区，自定义缩放下必须把原来的视图中心还原回去，
    // 否则展开缩略图栏后图片会被压到一边、拖动也对不上。
    const QPointF keep = m_item ? mapToScene(viewport()->rect().center()) : QPointF();
    setViewportMargins(margins.left(), margins.top(), margins.right(), margins.bottom());
    placeEdgeArrows();   // 左带/箭头随视口右移，不压在缩略图面板上
    refreshEdgeHover();  // 视口几何变了，按真实光标位置重算显隐
    if (m_fitToWindow) {
        fitToWindow();
    } else if (m_fitWidth) {
        fitToWidth();
    } else if (m_item) {
        centerOn(keep);
    }
}

void ImageCanvas::setSmooth(bool on)
{
    m_smooth = on;
    QPainter::RenderHints hints = QPainter::Antialiasing;
    if (on) {
        hints |= QPainter::SmoothPixmapTransform;
    }
    setRenderHints(hints);
}

void ImageCanvas::setInterpolationMode(int mode)
{
    m_interp = mode;
    // bilinear 关掉平滑；bicubic / lanczos 都要 GPU 平滑兜底（放大路径仍靠它）。
    setSmooth(mode != InterpBilinear);
    if (m_item) {
        m_item->setTransformationMode(mode == InterpBilinear ? Qt::FastTransformation
                                                             : Qt::SmoothTransformation);
    }
    m_samplingSize = QSize();
    refreshSampling();
}

void ImageCanvas::setStretchSmall(bool on)
{
    if (m_stretchSmall == on) {
        return;
    }
    m_stretchSmall = on;
    if (m_fitToWindow) {
        fitToWindow();
    } else if (m_fitWidth) {
        fitToWidth();
    }
}

// Lanczos 只处理「缩小显示」这一种情况：按目标屏幕像素重采样一次，再把结果
// 塞回 item 并用 devicePixelRatio 把逻辑尺寸还原成原图大小。scene 几何因此完全
// 不变，fit / centerOn / viewCenterNormalized 全都不用改，只是采样更干净。
//
// 交互态闸门：拖窗口边界（m_resizeBurst）或 150ms 内连续缩放时不做同步重采样，
// m_item 的 SmoothTransformation 顶住画面；交互停止 150ms 后由 m_sampleFlush
// 调 doRefreshSampling() 补做一次，且只对当前 m_zoom 补一次。
void ImageCanvas::refreshSampling()
{
    if (!m_item || m_sourcePixmap.isNull()) {
        return;
    }
    const bool rapid = m_lastSampleReq.isValid() && m_lastSampleReq.elapsed() < 150;
    m_lastSampleReq.restart();
    if (rapid || m_resizeBurst) {
        m_sampleFlush.start();   // 反复 restart：交互停止后 150ms 才会真正触发
        return;
    }
    doRefreshSampling();
}

void ImageCanvas::doRefreshSampling()
{
    if (!m_item || m_sourcePixmap.isNull()) {
        return;
    }
    const QSizeF isz = m_sourcePixmap.deviceIndependentSize();
    if (isz.width() <= 0.0 || isz.height() <= 0.0) {
        return;
    }
    if (m_interp != InterpLanczos || m_zoom >= 0.999 || m_zoom <= 0.0) {
        if (m_item->pixmap().cacheKey() != m_sourcePixmap.cacheKey()) {
            m_item->setPixmap(m_sourcePixmap);
            m_samplingSize = QSize();
        }
        return;
    }
    const int tw = qMax(1, qRound(isz.width() * m_zoom));
    const int th = qMax(1, qRound(isz.height() * m_zoom));
    if (m_samplingSize == QSize(tw, th)) {
        return;   // 目标尺寸没变，复用上一次的重采样结果
    }
    // 目标像素数过大时直接放弃：单次重采样的耗时与目标面积成正比，
    // 超过 12MP 已经不是「看大图」而是拖 UI 了，交给 GPU 平滑即可。
    if (static_cast<qint64>(tw) * th > 12000000) {
        if (m_item->pixmap().cacheKey() != m_sourcePixmap.cacheKey()) {
            m_item->setPixmap(m_sourcePixmap);
        }
        m_samplingSize = QSize(tw, th);
        return;
    }
    const QImage small = lanczosResample(m_sourcePixmap.toImage(), QSize(tw, th));
    QPixmap pm = QPixmap::fromImage(small);
    pm.setDevicePixelRatio(static_cast<qreal>(tw) / isz.width());
    m_item->setPixmap(pm);
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_samplingSize = QSize(tw, th);
}

void ImageCanvas::setCropMode(bool on)
{
    m_cropMode = on;
    clearSelection();
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

QRect ImageCanvas::selectionRect() const
{
    if (!m_rubber) {
        return QRect();
    }
    const QRectF r = m_rubber->rect().normalized();
    return r.toAlignedRect().intersected(m_item->pixmap().rect());
}

void ImageCanvas::clearSelection()
{
    if (m_rubber) {
        scene()->removeItem(m_rubber);
        delete m_rubber;
        m_rubber = nullptr;
    }
}

void ImageCanvas::fitToWindow()
{
    fitToWindowGeom();
    if (m_item) {
        refreshSampling();
    }
}

void ImageCanvas::fitToWindowGeom()
{
    if (!m_item) {
        return;
    }
    const QSizeF isz = m_item->pixmap().deviceIndependentSize();
    const qreal iw = isz.width();
    const qreal ih = isz.height();
    if (iw <= 0.0 || ih <= 0.0) {
        return;
    }
    resetTransform();
    qreal s = qMin(viewport()->width() / iw, viewport()->height() / ih);
    // 原版「拉伸小图像」默认关闭：小于窗口的图保持 100%，点 Z 开启后才等比放大填满。
    if (!m_stretchSmall) {
        s = qMin<qreal>(1.0, s);
    }
    scale(s, s);
    m_fitToWindow = true;
    m_fitWidth = false;
    m_zoom = s;
    centerOn(m_item);
    emit zoomChanged(m_zoom);
}

// 浏览模式 2：宽度贴合 viewport（含滚动条边距），高度超出则顶部对齐可滚，
// 高度不足则垂直居中
void ImageCanvas::fitToWidth()
{
    fitToWidthGeom();
    if (m_item) {
        refreshSampling();
    }
}

void ImageCanvas::fitToWidthGeom()
{
    if (!m_item) {
        return;
    }
    const QSizeF isz = m_item->pixmap().deviceIndependentSize();
    const qreal iw = isz.width();
    const qreal ih = isz.height();
    if (iw <= 0) {
        return;
    }
    resetTransform();
    // 宽度贴合要预留可能出现的垂直滚动条宽度，避免出现横向滚动条
    const int sbw = verticalScrollBar()->isVisible() || ih > viewport()->height()
                        ? verticalScrollBar()->sizeHint().width() : 0;
    const qreal z = qMax(1.0, static_cast<qreal>(viewport()->width() - sbw)) / iw;
    scale(z, z);
    m_zoom = z;
    m_fitToWindow = false;
    m_fitWidth = true;
    if (ih * z <= viewport()->height()) {
        centerOn(m_item);
    } else {
        horizontalScrollBar()->setValue(0);
        verticalScrollBar()->setValue(0);
    }
    emit zoomChanged(m_zoom);
}

void ImageCanvas::zoomIn()
{
    applyZoomAt(m_zoom * 1.25, viewport()->rect().center());
}

void ImageCanvas::zoomOut()
{
    applyZoomAt(m_zoom * 0.8, viewport()->rect().center());
}

void ImageCanvas::zoomActual()
{
    applyZoomAt(1.0, viewport()->rect().center());
}

void ImageCanvas::setZoom(qreal zoom)
{
    applyZoomAt(zoom, viewport()->rect().center());
}

// 缩放锚点还原：图片比视口小时 Qt 自己居中（滚动条无行程），此时不需要对齐；
// 超出视口时按「场景点当前落在哪」与「它应该落在哪」的像素差推滚动条。
void ImageCanvas::keepScenePointAt(const QPointF &scenePt, const QPoint &viewportPt)
{
    if (!m_item) {
        return;
    }
    const qreal z = transform().m11();
    const QRectF r = m_item->sceneBoundingRect();
    if (r.width() * z <= viewport()->width() && r.height() * z <= viewport()->height()) {
        return;
    }
    const QPointF now = mapFromScene(scenePt);
    const QPoint d(qRound(now.x()) - viewportPt.x(), qRound(now.y()) - viewportPt.y());
    if (d.isNull()) {
        return;
    }
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + d.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + d.y());
}

void ImageCanvas::applyZoomAt(qreal zoom, const QPoint &anchorViewportPt)
{
    if (!m_item || zoom <= 0.0) {
        return;
    }
    const QPointF anchorScene = mapToScene(anchorViewportPt);
    // 锚点同时记一份**窗口坐标系**的副本：进入手动缩放时主窗口会把顶部 76px 安全区
    // 收掉，视口在窗口里整体上移，若仍按视口坐标对齐，图片会跟着往上窜 30~40px。
    const QPoint anchorWindowPt = viewport()->mapTo(this, anchorViewportPt);
    resetTransform();
    scale(zoom, zoom);
    m_zoom = zoom;
    m_fitToWindow = false;
    m_fitWidth = false;
    // 先发信号：主窗口会在这里同步把上下安全区归零（并 centerOn 保持中心），
    // 之后再按锚点做一次像素级对齐，避免「进入手动缩放」那一下跳图。
    emit zoomChanged(m_zoom);
    keepScenePointAt(anchorScene, viewport()->mapFrom(this, anchorWindowPt));
    refreshSampling();
}

void ImageCanvas::wheelEvent(QWheelEvent *event)
{
    if (!m_wheelZoom) {
        event->ignore();
        return;
    }
    const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    // 滚轮以鼠标位置为锚点（原型/交接单 §5）
    applyZoomAt(m_zoom * factor, event->position().toPoint());
    event->accept();
}

void ImageCanvas::keyPressEvent(QKeyEvent *event)
{
    // QAbstractScrollArea 默认把方向键/翻页键消费成滚动，画布只要拿到焦点，
    // 事件就再也到不了 MainWindow。滚动条是 AlwaysOff，于是表现为「按了没反应」。
    // 统一 ignore() 让事件冒泡；不要用 NoFocus 一刀切，裁剪模式还要收 Esc。
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        event->ignore();
        return;
    default:
        break;
    }
    QGraphicsView::keyPressEvent(event);
}

void ImageCanvas::scrollVerticallyBy(qreal fraction)
{
    // ↑/↓ = 上移/下移（原版语义）。按视口高度比例滚，setValue 自带边界钳制，
    // 到顶/到底自然停住；适应窗口时没有可滚动范围，直接不动。
    QScrollBar *bar = verticalScrollBar();
    if (!bar) {
        return;
    }
    const int span = bar->maximum() - bar->minimum();
    if (span <= 0) {
        return;
    }
    bar->setValue(bar->value() + qRound(viewport()->height() * fraction));
}

// ---------------------------------------------------------------------------
// 侧边翻页箭头（原版：悬停才出现的 Chevron，点击即翻页）：
//   命中口径与显示口径统一为同一个 SideArrow 48x88 矩形——箭头在哪，
//   点击就在哪。视口坐标，与图片缩放无关；缩略图面板展开时
//   setViewportMargins 已把视口右移，左箭头天然落在面板右侧。
//   无图 / 裁剪模式不响应。旧的「40px 点击带 / 120px 悬停带」两套口径已废止。
// ---------------------------------------------------------------------------
int ImageCanvas::edgeTurnDelta(const QPoint &pos) const
{
    if (!m_item || m_sourcePixmap.isNull() || m_cropMode) {
        return 0;
    }
    // 注意不能拿 isVisible() 当命中条件：显隐本身就由「光标在不在矩形内」
    // 推出来，拿结果当条件会自锁（箭头永远点不亮）。矩形即口径，二者恒一致。
    if (m_arrowPrev && m_arrowPrev->geometry().contains(pos)) {
        return -1;
    }
    if (m_arrowNext && m_arrowNext->geometry().contains(pos)) {
        return +1;
    }
    return 0;
}

// 悬停反馈：光标落在箭头矩形内 → 箭头淡入 + 手掌光标；离开 → 淡出。
// 显隐判定只看箭头自身矩形（与点击同一口径），不再有更宽的悬停带。
// 这里是**唯一的显箭头入口**（其余时机都是 setShown(false)），所以侧边箭头
// 总开关只需在这里与一下，就能同时挡住 resize/scroll/show/换图等所有刷新路径。
void ImageCanvas::updateEdgeHover(const QPoint &pos)
{
    if (!m_arrowPrev || !m_arrowNext) {
        return;
    }
    const int delta = edgeTurnDelta(pos);
    m_arrowPrev->setShown(m_sideArrowsEnabled && delta < 0);
    m_arrowNext->setShown(m_sideArrowsEnabled && delta > 0);
    if (!m_cropMode) {
        // 关掉箭头也保留手型：命中带还在（点击仍翻页），没有光标提示就成暗坑了
        viewport()->setCursor(delta != 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
}

// 按**真实光标**当前位置重算悬停态。供换图 / Resize / 缩略图开合 / 全屏进出 /
// 裁剪切换 / show / enter 等「画面变了但鼠标没动」的时机调用。
void ImageCanvas::refreshEdgeHover()
{
    if (!m_arrowPrev || !m_arrowNext) {
        return;
    }
    if (m_cropMode) {
        m_arrowPrev->setShown(false);
        m_arrowNext->setShown(false);
        viewport()->unsetCursor();   // 让 setCropMode 设置的十字光标透出来
        return;
    }
    const QPoint vp = viewport()->mapFromGlobal(QCursor::pos());
    if (!viewport()->rect().contains(vp)) {
        // 光标已不在画布上（窗口变化后常见）：箭头必须收掉，不能挂在原地
        m_arrowPrev->setShown(false);
        m_arrowNext->setShown(false);
        viewport()->unsetCursor();
        return;
    }
    updateEdgeHover(vp);
}

void ImageCanvas::placeEdgeArrows()
{
    if (!m_arrowPrev) {
        return;
    }
    // 不判 m_sideArrowsEnabled：箭头矩形同时是点击命中带（edgeTurnDelta 读它），
    // 哪怕箭头被开关藏起来，也必须每帧摆回原位，否则关掉箭头就连点击也一起丢了。
    const QRect vp = viewport()->rect();
    const QSize sz = m_arrowPrev->size();
    m_arrowPrev->move(6, (vp.height() - sz.height()) / 2);
    m_arrowNext->move(vp.width() - 6 - sz.width(), (vp.height() - sz.height()) / 2);
}

void ImageCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_cropMode && event->button() == Qt::LeftButton && m_item) {
        m_selecting = true;
        m_origin = mapToScene(event->pos());
        clearSelection();
        m_rubber = scene()->addRect(QRectF(m_origin, m_origin),
                                    QPen(QColor(0, 180, 255), 1.5, Qt::DashLine),
                                    QBrush(QColor(0, 180, 255, 40)));
        event->accept();
        return;
    }
    if (m_middlePan && event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = event->pos();
        // 中键也可能只是「点一下」：按下先记账，松手时再按位移/时长判定单击。
        // 裁剪模式与未打开图片不参与（保持原行为，中键只做平移）。
        m_midPressPos = event->pos();
        m_midPressClock.restart();
        m_midPressTracked = m_item != nullptr && !m_cropMode;
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    // 左键按下先记账：松手时才判断「点击」还是「拖拽平移」（见 mouseReleaseEvent）
    if (event->button() == Qt::LeftButton) {
        m_pressTracked = m_item != nullptr && !m_sourcePixmap.isNull() && !m_cropMode;
        m_pressPos = event->pos();
        m_pressClock.restart();
    }
    // 左键平移由 QGraphicsView::ScrollHandDrag 原生处理，勿拦截基类事件。
    QGraphicsView::mousePressEvent(event);
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selecting && m_rubber) {
        const QPointF cur = mapToScene(event->pos());
        m_rubber->setRect(QRectF(m_origin, cur).normalized());
        event->accept();
        return;
    }
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPan;
        m_lastPan = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    // 悬停（无按键）：箭头光标 + 箭头显隐（viewport 开了 mouse tracking）。
    // 统一走真实光标口径，鼠标不动时的其它刷新也走同一条路。
    if (event->buttons() == Qt::NoButton) {
        refreshEdgeHover();
    } else if (m_pressTracked && (event->buttons() & Qt::LeftButton)
               && (event->pos() - m_pressPos).manhattanLength() >= 3) {
        // 位移到 3px 即判定为拖拽平移，松手不再翻页（原逻辑不受影响）
        m_pressTracked = false;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selecting && event->button() == Qt::LeftButton) {
        m_selecting = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        unsetCursor();
        // 单击判定与左键箭头带同一套阈值：<3px、<300ms。拖动超过 3px 仍只是平移。
        const bool isClick = m_midPressTracked
                             && m_midPressClock.isValid()
                             && m_midPressClock.elapsed() < 300
                             && (event->pos() - m_midPressPos).manhattanLength() < 3;
        m_midPressTracked = false;
        event->accept();
        if (isClick) {
            emit middleClickRequested();
        }
        return;
    }
    // 箭头带内的「点击」翻页：左键 + 位移 <3px + 按压 <300ms。先让基类把
    // ScrollHandDrag 收干净，再发信号（loadAsync 异步，不会在事件里重入画布）。
    if (event->button() == Qt::LeftButton && m_pressTracked) {
        m_pressTracked = false;
        const bool isClick = m_pressClock.isValid() && m_pressClock.elapsed() < 300
                             && (event->pos() - m_pressPos).manhattanLength() < 3;
        const int delta = isClick ? edgeTurnDelta(event->pos()) : 0;
        QGraphicsView::mouseReleaseEvent(event);
        if (delta != 0) {
            emit edgeTurnRequested(delta);
        }
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageCanvas::scrollContentsBy(int dx, int dy)
{
    QGraphicsView::scrollContentsBy(dx, dy);
    // 关键：QAbstractScrollArea 滚动是 viewport()->scroll() 实现的，会把视口的
    // **子控件整体平移**——箭头正是 viewport 子控件，翻页/缩放/平移一次就被
    // 拖偏一次（实测翻一页 +45px，点击落空）。滚动后立刻归位。
    if ((dx != 0 || dy != 0) && m_arrowPrev) {
        placeEdgeArrows();
    }
}

void ImageCanvas::leaveEvent(QEvent *event)
{
    // 指针离开画布：光标还原、箭头淡出（否则停在带内会一直挂着）
    if (m_arrowPrev) {
        m_arrowPrev->setShown(false);
        m_arrowNext->setShown(false);
        viewport()->unsetCursor();
    }
    QGraphicsView::leaveEvent(event);
}

void ImageCanvas::enterEvent(QEnterEvent *event)
{
    QGraphicsView::enterEvent(event);
    refreshEdgeHover();   // 进画布时若光标已停在箭头上，立即显箭头
}

void ImageCanvas::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    refreshEdgeHover();
}

void ImageCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    // 拖窗口边界：进入 burst 时主动清掉上一次的采样目标尺寸（防止拖回去后
    // 拿拖动前的旧尺寸当命中），几何照常每帧算，重采样交给收尾定时器补一次。
    if (!m_resizeBurst) {
        m_resizeBurst = true;
        m_samplingSize = QSize();
    }
    m_sampleFlush.start();
    if (m_fitToWindow) {
        fitToWindowGeom();    // 拖动期只做几何，不触发 refreshSampling
    } else if (m_fitWidth) {
        fitToWidthGeom();
    }
    if (m_logo) {
        m_logo->move((viewport()->width() - m_logo->width()) / 2,
                     (viewport()->height() - m_logo->height()) / 2);
    }
    placeEdgeArrows();
    refreshEdgeHover();   // 窗口/视口变化后，箭头矩形随动，按真实光标重算
}
