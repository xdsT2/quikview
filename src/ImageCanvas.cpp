#include "ImageCanvas.h"
#include "PerfProbe.h"
#include "UiStyle.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGraphicsOpacityEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QCursor>
#include <QFile>
#include <QKeyEvent>
#include <QLabel>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QRunnable>
#include <QThreadPool>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QTextStream>
#include <QTimer>
#include <QVariantAnimation>
#include <QWheelEvent>

#include <cmath>
#include <vector>

#include "Config.h"

namespace {

// —— 长图轴锁（D2）——
// 高/宽 ≥ 2.5 视为条漫式长图；纵向溢出视口时横向拖动被钉回中心。
constexpr qreal kLongStripAspect = 2.5;

// —— UI-001 §3.8.3 透明图像阴影的软边工具 ——
// 对 ARGB32_Premultiplied 图做 3 遍盒式模糊（只动 alpha，RGB 恒黑）。阴影源
// 永远是「图片外接矩形」填充，不做逐像素 Alpha 描摹——复杂透明形状不会脏边。
void boxBlurAlpha(QImage &img, int radius)
{
    if (radius <= 0 || img.isNull()) {
        return;
    }
    const int w = img.width();
    const int h = img.height();
    std::vector<quint8> a(size_t(w) * h);
    std::vector<quint8> tmp(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar *sl = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            a[size_t(y) * w + x] = sl[size_t(x) * 4 + 3];
        }
    }
    const auto passH = [&](std::vector<quint8> &src, std::vector<quint8> &dst) {
        for (int y = 0; y < h; ++y) {
            const int base = y * w;
            quint32 sum = 0;
            for (int x = -radius; x <= radius; ++x) {
                sum += src[base + qBound(0, x, w - 1)];
            }
            const quint32 denom = quint32(radius * 2 + 1);
            for (int x = 0; x < w; ++x) {
                dst[base + x] = quint8(sum / denom);
                const int out = qBound(0, x - radius, w - 1);
                const int in = qBound(0, x + radius + 1, w - 1);
                sum += quint32(src[base + in]) - quint32(src[base + out]);
            }
        }
    };
    const auto passV = [&](std::vector<quint8> &src, std::vector<quint8> &dst) {
        for (int x = 0; x < w; ++x) {
            quint32 sum = 0;
            for (int y = -radius; y <= radius; ++y) {
                sum += src[size_t(qBound(0, y, h - 1)) * w + x];
            }
            const quint32 denom = quint32(radius * 2 + 1);
            for (int y = 0; y < h; ++y) {
                dst[size_t(y) * w + x] = quint8(sum / denom);
                const int out = qBound(0, y - radius, h - 1);
                const int in = qBound(0, y + radius + 1, h - 1);
                sum += quint32(src[size_t(in) * w + x]) - quint32(src[size_t(out) * w + x]);
            }
        }
    };
    passH(a, tmp);
    passV(tmp, a);
    passH(a, tmp);
    passV(tmp, a);
    passH(a, tmp);
    passV(tmp, a);
    for (int y = 0; y < h; ++y) {
        uchar *sl = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            sl[size_t(x) * 4 + 3] = a[size_t(y) * w + x];
        }
    }
}

} // namespace

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

// 强锐化滤镜（C13）：固定强度 3×3 unsharp mask，接在 lanczosResample 之后。
// 高斯核 [1 2 1;2 4 2;1 2 1]/16 做模糊底，out = src + (src - blur) * 4/5。
// 高斯底比盒形底振铃小（交办单要求「无白边振铃」）；边缘像素原样复制。
QImage applyStrongSharpen(const QImage &src)
{
    if (src.isNull() || src.width() < 3 || src.height() < 3) {
        return src;
    }
    const QImage in = src.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width();
    const int h = in.height();
    QImage out(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        const QRgb *up = reinterpret_cast<const QRgb *>(in.constScanLine(qMax(0, y - 1)));
        const QRgb *mid = reinterpret_cast<const QRgb *>(in.constScanLine(y));
        const QRgb *dn = reinterpret_cast<const QRgb *>(in.constScanLine(qMin(h - 1, y + 1)));
        QRgb *orow = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                orow[x] = mid[x];
                continue;
            }
            const int xl = x - 1;
            const int xr = x + 1;
            // 3×3 高斯模糊：角 1、边 2、中心 4，核和 16
            const int blurR = (qRed(up[xl]) + qRed(up[xr]) + qRed(dn[xl]) + qRed(dn[xr])
                               + 2 * (qRed(up[x]) + qRed(mid[xl]) + qRed(mid[xr]) + qRed(dn[x]))
                               + 4 * qRed(mid[x])) / 16;
            const int blurG = (qGreen(up[xl]) + qGreen(up[xr]) + qGreen(dn[xl]) + qGreen(dn[xr])
                               + 2 * (qGreen(up[x]) + qGreen(mid[xl]) + qGreen(mid[xr]) + qGreen(dn[x]))
                               + 4 * qGreen(mid[x])) / 16;
            const int blurB = (qBlue(up[xl]) + qBlue(up[xr]) + qBlue(dn[xl]) + qBlue(dn[xr])
                               + 2 * (qBlue(up[x]) + qBlue(mid[xl]) + qBlue(mid[xr]) + qBlue(dn[x]))
                               + 4 * qBlue(mid[x])) / 16;
            // 固定强度 0.8：out = src + (src - blur) * 4/5，逐通道截断
            const int sr = qBound(0, qRed(mid[x]) + (qRed(mid[x]) - blurR) * 4 / 5, 255);
            const int sg = qBound(0, qGreen(mid[x]) + (qGreen(mid[x]) - blurG) * 4 / 5, 255);
            const int sb = qBound(0, qBlue(mid[x]) + (qBlue(mid[x]) - blurB) * 4 / 5, 255);
            orow[x] = qRgba(sr, sg, sb, qAlpha(mid[x]));
        }
    }
    return out;
}

// 后台重采样任务：只碰按值带过来的 QImage（Qt 隐式共享、只读线程安全），
// 不碰 QPixmap/QWidget。结果用 queued invokeMethod 回 GUI 线程，由
// applySampleResult 做全键校验；owner 销毁时 Qt 自动丢弃排队的函子。
class ImageCanvas::SamplingTask : public QRunnable
{
public:
    SamplingTask(ImageCanvas *owner, QImage src, const QSize &target, bool sharp,
                 quint64 generation, qint64 sourceKey, int interp)
        : m_owner(owner), m_src(std::move(src)), m_target(target), m_sharp(sharp),
          m_generation(generation), m_sourceKey(sourceKey), m_interp(interp)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QImage out = lanczosResample(m_src, m_target);
        // 强锐化滤镜（C13）：仅 lanczos 缩小插值路径叠加，固定强度 unsharp mask
        if (m_sharp)
            out = applyStrongSharpen(out);
        QMetaObject::invokeMethod(m_owner,
            [owner = m_owner, gen = m_generation, result = std::move(out),
             target = m_target, sourceKey = m_sourceKey, interp = m_interp,
             sharp = m_sharp]() mutable {
                owner->applySampleResult(gen, std::move(result), target, sourceKey, interp,
                                         sharp);
            }, Qt::QueuedConnection);
    }

private:
    ImageCanvas *m_owner;
    QImage m_src;
    QSize m_target;
    bool m_sharp;
    quint64 m_generation;
    qint64 m_sourceKey;
    int m_interp;
};

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
    // 锚点必须 NoAnchor：本项目所有缩放锚点由 applyZoomAt/keepScenePointAt
    // 自实现（以鼠标/视口中心为锚推滚动条），不消费 Qt 的 transformation
    // anchor。而 AnchorUnderMouse 会在 ScrollHandDrag 滚动后按鼠标位置重映射
    // 视图——实测（分帧截图+400ms 滚动条探针）：滚动条 value 每步 +44 被
    // anchor 校正吃掉，图片显示小于视口（zoom<fit，双向行程充足）时内容
    // 纹丝不动；显示大于视口时表现为部分抵消/过冲（拖 -220 位移 -176/-261）。
    // NoAnchor 后四档缩放拖动全部 1:1 跟手（返工单判据 7）。
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setRenderHints(QPainter::SmoothPixmapTransform | QPainter::Antialiasing);
    // 原版无可见滚动条（平移走左键拖动/滚轮缩放）
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShape(QFrame::NoFrame);
    setBackgroundColor(UiStyle::chromeBg());

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

    // 交互收尾：拖窗口边界停止 150ms 后补做一次重采样（只补当前 m_zoom）
    m_sampleFlush.setSingleShot(true);
    m_sampleFlush.setInterval(150);
    // 重采样线程池：单线程即可——同一时刻只允许一个任务在飞，交互期的新请求
    // 不排队，任务收尾时按「当时的状态」重发一次，天然只算最新一档。
    m_samplePool.setMaxThreadCount(1);

   connect(&m_sampleFlush, &QTimer::timeout, this, [this]() {
        m_resizeBurst = false;
        doRefreshSampling();
    });

    // 中央单击待判：间隔到了还没等到双击，就把暂存的动作真正派发出去。
    m_pendingActionTimer.setSingleShot(true);
    connect(&m_pendingActionTimer, &QTimer::timeout, this, [this]() {
        const QString act = m_pendingAction;
        m_pendingAction.clear();
        if (!act.isEmpty()) {
            emit mouseActionRequested(act);
        }
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

// UI-001 §3.8：全画布棋盘/纯色。kind 同值重调也重取令牌色——主题切换后
// MainWindow 会重调它，颜色必须跟着 isLight() 走。底 stylesheet 同步成
// 纯色令牌：stylesheet 优先级高于 backgroundBrush，resize 露底的瞬间
// 也不能闪出旧的深灰。
void ImageCanvas::setCanvasBackdrop(int kind)
{
    m_backdrop = (kind == int(UiStyle::canvas::Backdrop::Solid)) ? 1 : 0;
    const QColor base = UiStyle::canvas::solid(UiStyle::isLight());
    m_backgroundColor = base;
    setBackgroundBrush(base);
    setStyleSheet(QStringLiteral("QGraphicsView{background:%1;border:0;}")
                      .arg(base.name()));
    m_checkerTile = QPixmap();   // 强制按当前主题/DPR 重铺 tile
    m_tileDpr = 0.0;
    viewport()->update();
}

void ImageCanvas::setImageShadowEnabled(bool on)
{
    if (m_shadowOn == on) {
        return;
    }
    m_shadowOn = on;
    viewport()->update();
}

void ImageCanvas::setImageShadowEligible(bool eligible)
{
    if (m_shadowEligible == eligible) {
        return;
    }
    m_shadowEligible = eligible;
    viewport()->update();
}

void ImageCanvas::setShowTransparency(bool on)
{
    if (m_showTransparency == on) {
        return;
    }
    m_showTransparency = on;
    viewport()->update();
}

// 棋盘 tile：2×2 格（TileEdge 逻辑像素）按当前 DPR 预渲染。格边落在整数
// 逻辑像素上，100%/125%/150% DPR 下都是整数物理像素——不会半像素发虚。
void ImageCanvas::rebuildCheckerTile(bool indicator)
{
    const qreal dpr = devicePixelRatioF();
    const bool light = UiStyle::isLight();
    const int edge = UiStyle::canvas::TileEdge;
    const int cell = UiStyle::canvas::GridCell;
    QPixmap tile(qRound(edge * dpr), qRound(edge * dpr));
    tile.setDevicePixelRatio(dpr);
    // 指示棋盘（纯色模式的透明区域提示）与全画布棋盘两阶不同：深色
    // #343434/#424424 vs #141414/#202020（果核参考实测）。
    tile.fill(indicator ? UiStyle::canvas::indicatorA(light)
                        : UiStyle::canvas::checkerA(light));
    QPainter p(&tile);
    p.fillRect(0, 0, cell, cell, indicator ? UiStyle::canvas::indicatorB(light)
                                           : UiStyle::canvas::checkerB(light));
    p.fillRect(cell, cell, cell, cell, indicator ? UiStyle::canvas::indicatorB(light)
                                                 : UiStyle::canvas::checkerB(light));
    p.end();
    m_checkerTile = tile;
    m_tileDpr = dpr;
}

// 阴影缓存：以「图片外接框投影到视口」的尺寸为键（16px 步进量化，缩放过程
// 不至于每帧重建），低分辨率渲染 + 3 遍盒式模糊，绘制时平滑放大到实际尺寸。
void ImageCanvas::rebuildShadowCache(const QRectF &projected, bool light)
{
    const int margin = UiStyle::canvas::shadowMargin(light);
    const int blur = UiStyle::canvas::shadowBlur(light);
    const QSize logical(qRound(projected.width()), qRound(projected.height()));
    const QSize total(logical.width() + margin * 2, logical.height() + margin * 2);
    const qreal s = qMin(1.0, 360.0 / qreal(qMax(total.width(), total.height())));
    QImage src(qRound(total.width() * s), qRound(total.height() * s),
               QImage::Format_ARGB32_Premultiplied);
    src.fill(0);
    QPainter p(&src);
    p.fillRect(QRectF(margin * s, margin * s,
                      logical.width() * s, logical.height() * s),
               UiStyle::canvas::shadowColor(light));
    p.end();
    boxBlurAlpha(src, qMax(1, qRound(blur * s)));
    m_shadowCache = src;
    m_shadowCacheRect = QRectF(QPointF(), QSizeF(logical));
    m_shadowCacheLight = light;
}

void ImageCanvas::setBackgroundImage(const QImage &image)
{
    // QImage 隐式共享，路径没变时赋值几乎零成本；这里不比较内容。
    if (m_backgroundImage.cacheKey() == image.cacheKey()) {
        return;
    }
    m_backgroundImage = image;
    viewport()->update();
}

void ImageCanvas::drawBackground(QPainter *painter, const QRectF &rect)
{
    const bool light = UiStyle::isLight();
    const bool checkerMode = m_backdrop != int(UiStyle::canvas::Backdrop::Solid);
    // 图片外接框（场景→视口）：指示棋盘与阴影环都以它为界。
    QRectF projected;
    const bool hasImage = m_item != nullptr;
    if (hasImage) {
        const QRectF sceneRect = m_item->mapRectToScene(m_item->boundingRect());
        projected = mapFromScene(sceneRect).boundingRect();
    }
    if (checkerMode && hasImage) {
        // 棋盘铺满整个视口：tile 固定在视口逻辑坐标——切到设备坐标平铺，
        // 相位锚定视口左上角，缩放/平移/旋转图片时格子不动（§3.8.2）。
        if (m_checkerTile.isNull() || m_tileDpr != devicePixelRatioF()) {
            rebuildCheckerTile();
        }
        painter->save();
        painter->resetTransform();
        painter->drawTiledPixmap(viewport()->rect(), m_checkerTile);
        painter->restore();
    } else {
        painter->fillRect(rect, UiStyle::canvas::solid(light));
    }
    if (!m_backgroundImage.isNull()) {
        painter->drawImage(rect, m_backgroundImage);
    }
    // 「显示透明区域」：纯色模式下在图片矩形内为透明像素铺指示棋盘；
    // 棋盘模式下全画布棋盘已经连续透出，无需叠加（与果核参考一致）。
    // 指示棋盘同样固定在视口逻辑坐标。
    if (!checkerMode && m_showTransparency && hasImage
        && m_item->pixmap().hasAlphaChannel() && !projected.isEmpty()) {
        if (m_checkerTile.isNull() || m_tileDpr != devicePixelRatioF()) {
            rebuildCheckerTile(true);   // 指示棋盘 tile（比画布棋盘亮一档）
        }
        painter->save();
        painter->resetTransform();
        painter->setClipRect(projected.toRect());
        painter->drawTiledPixmap(viewport()->rect(), m_checkerTile);
        painter->restore();
    }
    // 图片阴影（§3.8.3 返工）：沿图片最外层矩形一圈的克制投影——绘制时
    // 剪除矩形内部（只在边界外圈可见），严禁形成覆盖透明区域的大块底板。
    if (m_shadowOn && m_shadowEligible && hasImage
        && m_item->pixmap().hasAlphaChannel() && !projected.isEmpty()
        && viewport()->rect().intersects(projected.toRect())) {
        const int step = 16;
        const QSize key(qRound(projected.width() / step) * step,
                        qRound(projected.height() / step) * step);
        const bool lightNow = UiStyle::isLight();
        if (m_shadowCache.isNull() || m_shadowCacheLight != lightNow
            || QSize(key) != m_shadowCacheRect.size().toSize()) {
            rebuildShadowCache(QRectF(QPointF(), QSizeF(key)), lightNow);
        }
        const int margin = UiStyle::canvas::shadowMargin(lightNow);
        const int dy = UiStyle::canvas::shadowDy(lightNow);
        const QRectF dst(projected.left() - margin, projected.top() - margin + dy,
                         key.width() + margin * 2.0, key.height() + margin * 2.0);
        painter->save();
        painter->resetTransform();
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        // 剪除图片矩形内部：阴影只在边界外圈可见（果核「在图片边缘加一圈阴影」）
        QRegion outside(viewport()->rect());
        outside = outside.subtracted(QRegion(projected.toRect()));
        painter->setClipRegion(outside);
        painter->drawImage(dst, m_shadowCache);
        painter->restore();
    }
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
// placeEdgeArrows 摆在原位，所以两侧点击照旧翻页。显示箭头的唯一路径是 updateEdgeHover()，
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

void ImageCanvas::setPixmapAnimated(const QPixmap &pixmap, int mode, bool fitAfter)
{
    if (mode <= 0 || m_item == nullptr || m_item->pixmap().isNull()) {
        setPixmap(pixmap, fitAfter);
        return;
    }
    // 连发直切：上一次过渡没走完又来换页 = 用户在连按/快滚，取消淡化直接切，
    // 不排队、不重启，避免拖影与积压。
    if (m_transAnim && m_transAnim->state() == QAbstractAnimation::Running) {
        m_transAnim->stop();
        m_transProgress = 1.0;
        m_transPixmap = QPixmap();
        setPixmap(pixmap, fitAfter);
        return;
    }
    m_transPixmap = m_item->pixmap();
    m_transSceneRect = scene()->sceneRect();
    m_transMode = mode;
    setPixmap(pixmap, fitAfter);
    m_transProgress = 0.0;
    if (!m_transAnim) {
        m_transAnim = new QVariantAnimation(this);
        connect(m_transAnim, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &v) {
            m_transProgress = v.toReal();
            viewport()->update();
        });
        connect(m_transAnim, &QVariantAnimation::finished, this, [this]() {
            m_transProgress = 1.0;
            m_transPixmap = QPixmap();
            viewport()->update();
        });
    }
    m_transAnim->stop();
    m_transAnim->setStartValue(0.0);
    m_transAnim->setEndValue(1.0);
    m_transAnim->setDuration(100);   // 单次切换 90–110ms 交叉淡化；连发已被上面的直切拦截
    m_transAnim->start();
}

void ImageCanvas::paintEvent(QPaintEvent *event)
{
    PerfProbe::firstPaint();
    QGraphicsView::paintEvent(event);
    // 边框画在视口 1px 内沿：位置只和视口尺寸有关（与 scene 的缩放/平移无关），
    // 所以每次重绘现算，不缓存任何几何。m_borderColor 无效时等于「不画边框」。
    if (m_borderColor.isValid()) {
        QPainter p(viewport());
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(QPen(m_borderColor, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(viewport()->rect().adjusted(0, 0, -1, -1));
    }
    // 翻页过渡在 drawForeground 里画（见该函数），这里只管视口内沿边框。
}

void ImageCanvas::drawForeground(QPainter *painter, const QRectF &rect)
{
    QGraphicsView::drawForeground(painter, rect);
    if (m_transProgress >= 1.0 || m_transPixmap.isNull() || m_transSceneRect.isEmpty()) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (m_transMode == 1) {
        // 淡入淡出：旧图按进度变透明，露出下面的新图
        painter->setOpacity(1.0 - m_transProgress);
        painter->drawPixmap(m_transSceneRect, m_transPixmap, m_transPixmap.rect());
    } else if (m_transMode == 2) {
        // Alpha 滑动：旧图向左让出 12% 宽度并同步淡出
        painter->setOpacity(1.0 - m_transProgress);
        painter->drawPixmap(m_transSceneRect.translated(-m_transSceneRect.width() * 0.12
                                                       * m_transProgress, 0),
                            m_transPixmap, m_transPixmap.rect());
    } else {
        // 滑动：旧图整幅横移出画，新图从右侧露出
        painter->drawPixmap(m_transSceneRect.translated(-m_transSceneRect.width()
                                                        * m_transProgress, 0),
                            m_transPixmap, m_transPixmap.rect());
    }
    painter->restore();
}

void ImageCanvas::setPixmap(const QPixmap &pixmap, bool fitAfter)
{
    m_sourcePixmap = pixmap;
    m_sourceImage = pixmap.toImage();   // 隐式共享（raster 后端不复制像素），后台线程只读
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    invalidateSampling();   // 换图后旧结果必是假命中（源都换了），在飞任务一并作废
    // QGraphicsPixmapItem 默认 FastTransformation——painter 的
    // SmoothPixmapTransform 不覆盖 item 自身采样，缩放必须显式开平滑。
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    m_zoom = 1.0;
    resetTransform();
    // 长图开图一次「适合宽度」（条漫阅读）：这里只记意图，由随后的
    // fitToWindow 消费。直接改模式会和调用方的 applyCurrentZoom /
    // 缩放状态恢复打架，也会让「用户主动选适合窗口」无法回到全览。
    const QSizeF isz = pixmap.deviceIndependentSize();
    m_pendingLongStripFitWidth = isz.width() > 0.0
                                 && isz.height() / isz.width() >= kLongStripAspect;
    // QGraphicsScene 的 sceneRect 只涨不缩：换图后必须显式重置。
    // 加 2 倍视口 padding 后 fit 态也有滚动行程 → 打开即可拖、可拖出边界。
    updateSceneRect();
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
    m_sourceImage = pixmap.toImage();   // 隐式共享，同 setPixmap
    scene()->clear();
    m_item = scene()->addPixmap(pixmap);
    m_item->setTransformationMode(Qt::SmoothTransformation);
    m_rubber = nullptr;
    invalidateSampling();   // 换图后旧的目标尺寸命中必是假命中（源都换了）
    resetTransform();
    updateSceneRect();   // 同 setPixmap：防止沿用旧矩形（含可拖 padding）
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

void ImageCanvas::replacePixmapInPlace(const QPixmap &pixmap)
{
    if (!m_item || pixmap.deviceIndependentSize() != m_sourcePixmap.deviceIndependentSize()) {
        setPixmapPreservingView(pixmap, viewCenterNormalized(), zoom(), false, false);
        return;
    }
    m_sourcePixmap = pixmap;
    m_sourceImage = pixmap.toImage();   // 隐式共享，同 setPixmap
    invalidateSampling();
    doRefreshSampling();
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

// 画布自由拖动：sceneRect = 图片矩形四周加「2 倍视口（物理像素）」padding。
// 借鉴 core-ui「不约束平移」的规则（MIT，仅规则不抄码）：滚动行程恒 > 0，
// fit 态也能拖，且能拖出大半个屏幕的空白。m_item 固定 (0,0) 起，
// pad 只体现在 sceneRect 上——所有以图片矩形为基准的锚点/归一化逻辑不变。
// padding 必须与 zoom 无关：场景单位里除以当前 zoom，pad×zoom 恒等于
// 2 倍视口物理像素——否则 zoom 越小行程越缩（zoom×sceneRect−viewport），
// ~0.1× 时行程归零完全拖不动（返工单实测）。z 取 m11 并 clamp 防御。
void ImageCanvas::updateSceneRect()
{
    if (!m_item) {
        scene()->setSceneRect(QRectF());
        return;
    }
    const QRectF img = m_item->boundingRect();
    const qreal z = qBound(0.05, transform().m11(), 1.0);
    const qreal px = viewport()->width() * 2.0 / z;
    const qreal py = viewport()->height() * 2.0 / z;
    scene()->setSceneRect(img.adjusted(-px, -py, px, py));
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
    invalidateSampling();
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
// 线程模型（2026-11-16）：耗时部分（Lanczos + 强锐化）放专用单线程池，GUI 线程
// 只做「算目标尺寸 → 投任务」和「校验后 setPixmap」。同一时刻最多一个任务在飞；
// 交互期的新请求不排队，任务回投时按 图片/尺寸/倍率/插值/锐化 全键复核，对不上
// 就丢弃并按最新状态补算一次——连发滚轮因此只算首尾几档，中间档一律不落地。
// 交互闸门只保留拖窗口（m_resizeBurst）：拖动期几何每帧都在变，等收尾再算一次。
void ImageCanvas::refreshSampling()
{
    if (!m_item || m_sourcePixmap.isNull()) {
        return;
    }
    if (m_resizeBurst) {
        m_sampleFlush.start();   // 反复 restart：拖动停止后 150ms 才补算一次
        return;
    }
    doRefreshSampling();
}

// 让「已上屏结果」的记账作废，并让在飞任务的结果过期。换图/切插值/拖窗口
// 都会走这里；不清这个记账，旧尺寸会命中新状态。
void ImageCanvas::invalidateSampling()
{
    ++m_sampleGeneration;
    m_samplingSize = QSize();
    m_samplingSourceKey = 0;
    m_samplingInterp = -1;
    m_samplingSharp = false;
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
    const qint64 srcKey = m_sourcePixmap.cacheKey();
    const bool sharp = Config::instance().strongSharpen();
    // 放大 / 100% / 非 lanczos：原图直出；在飞结果已不该上屏，一并作废
    if (m_interp != InterpLanczos || m_zoom >= 0.999 || m_zoom <= 0.0) {
        ++m_sampleGeneration;
        if (m_item->pixmap().cacheKey() != srcKey) {
            m_item->setPixmap(m_sourcePixmap);
        }
        m_samplingSize = QSize();
        m_samplingSourceKey = srcKey;
        m_samplingInterp = m_interp;
        m_samplingSharp = sharp;
        return;
    }
    const int tw = qMax(1, qRound(isz.width() * m_zoom));
    const int th = qMax(1, qRound(isz.height() * m_zoom));
    if (m_samplingSize == QSize(tw, th) && m_samplingSourceKey == srcKey
        && m_samplingInterp == m_interp && m_samplingSharp == sharp) {
        return;   // 已上屏的就是这一档（尺寸/插值/锐化都对得上）
    }
    // 目标像素数过大时直接放弃：单次重采样的耗时与目标面积成正比，
    // 超过 12MP 已经不是「看大图」而是拖 UI 了，交给 GPU 平滑即可。
    if (static_cast<qint64>(tw) * th > 12000000) {
        ++m_sampleGeneration;
        if (m_item->pixmap().cacheKey() != srcKey) {
            m_item->setPixmap(m_sourcePixmap);
        }
        m_samplingSize = QSize(tw, th);
        m_samplingSourceKey = srcKey;
        m_samplingInterp = m_interp;
        m_samplingSharp = sharp;
        return;
    }
    startPendingSample();
}

// 等后台结果期间顶画面的廉价帧。几十 MP 的原图每重绘一次就是百毫秒级的主线程
// 停顿（实测 5760×4320 → 1078×719 平滑绘制 ≈106ms），而快速缩小到长边 1600
// 只要 ~15ms，之后按目标缩放再画就很便宜。按源图缓存，换图即失效。
QPixmap ImageCanvas::samplingPreview()
{
    if (m_sourcePixmap.isNull()) {
        return QPixmap();
    }
    if (m_previewSourceKey == m_sourcePixmap.cacheKey() && !m_previewPixmap.isNull()) {
        return m_previewPixmap;
    }
    const QSizeF isz = m_sourcePixmap.deviceIndependentSize();
    if (isz.width() <= 0.0 || isz.height() <= 0.0) {
        return QPixmap();
    }
    const qreal longest = qMax(isz.width(), isz.height());
    if (longest <= 2400.0) {
        // 小图本身就是廉价帧，没必要再缩一遍
        m_previewPixmap = m_sourcePixmap;
        m_previewSourceKey = m_sourcePixmap.cacheKey();
        return m_previewPixmap;
    }
    const qreal k = 1600.0 / longest;
    const int pw = qMax(1, qRound(isz.width() * k));
    const int ph = qMax(1, qRound(isz.height() * k));
    QImage img = m_sourceImage.scaled(pw, ph, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    QPixmap pm = QPixmap::fromImage(std::move(img));
    // DPR 把逻辑尺寸还原成原图大小：预览帧上屏不改变 scene 几何
    pm.setDevicePixelRatio(static_cast<qreal>(pm.width()) / isz.width());
    m_previewPixmap = pm;
    m_previewSourceKey = m_sourcePixmap.cacheKey();
    return m_previewPixmap;
}

// 投一个后台任务。已有任务在飞就直接返回：它回投时会按最新状态重发，
// 所以丢掉这次请求不会丢结果（单飞 + 收尾复核）。
void ImageCanvas::startPendingSample()
{
    if (m_sampleRunning || !m_item || m_sourcePixmap.isNull() || m_sourceImage.isNull()) {
        return;
    }
    const QSizeF isz = m_sourcePixmap.deviceIndependentSize();
    if (isz.width() <= 0.0 || isz.height() <= 0.0) {
        return;
    }
    if (m_interp != InterpLanczos || m_zoom >= 0.999 || m_zoom <= 0.0) {
        return;
    }
    const QSize target(qMax(1, qRound(isz.width() * m_zoom)),
                       qMax(1, qRound(isz.height() * m_zoom)));
    const qint64 srcKey = m_sourcePixmap.cacheKey();
    const int interp = m_interp;
    const bool sharp = Config::instance().strongSharpen();
    if (m_samplingSize == target && m_samplingSourceKey == srcKey
        && m_samplingInterp == interp && m_samplingSharp == sharp) {
        return;   // 已经是这一档
    }
    if (static_cast<qint64>(target.width()) * target.height() > 12000000) {
        return;
    }
    m_sampleRunning = true;
    // 结果回来之前，画面上若还是原图，这次重绘就会自己变成一次百毫秒级停顿；
    // 换成廉价预览帧先把画面顶住（只换 item，不动采样记账）。
    if (m_item->pixmap().cacheKey() == m_sourcePixmap.cacheKey()) {
        const QPixmap preview = samplingPreview();
        if (!preview.isNull()) {
            m_item->setPixmap(preview);
        }
    }
    const quint64 gen = ++m_sampleGeneration;
    m_samplePool.start(new SamplingTask(this, m_sourceImage, target, sharp, gen, srcKey, interp));
}

// 后台结果回投（GUI 线程）。只有「仍是当前这一档」才上屏，否则丢弃——丢弃之后
// 统一按当时的状态再评估一次，有更新的档就直接开算。
void ImageCanvas::applySampleResult(quint64 generation, QImage result, const QSize &target,
                                    qint64 sourceKey, int interp, bool sharp)
{
    m_sampleRunning = false;
    if (m_item && !m_sourcePixmap.isNull() && !result.isNull()) {
        const QSizeF isz = m_sourcePixmap.deviceIndependentSize();
        const bool shrinking = m_interp == InterpLanczos && m_zoom > 0.0 && m_zoom < 0.999;
        const bool sameSource = sourceKey == m_sourcePixmap.cacheKey();
        const bool sameMode = interp == m_interp && sharp == Config::instance().strongSharpen();
        const bool sameTarget = isz.width() > 0.0
            && QSize(qMax(1, qRound(isz.width() * m_zoom)),
                     qMax(1, qRound(isz.height() * m_zoom))) == target;
        if (generation == m_sampleGeneration && shrinking && sameSource && sameMode
            && sameTarget) {
            QPixmap pm = QPixmap::fromImage(std::move(result));
            pm.setDevicePixelRatio(static_cast<qreal>(target.width()) / isz.width());
            m_item->setPixmap(pm);
            m_item->setTransformationMode(Qt::SmoothTransformation);
            m_samplingSize = target;
            m_samplingSourceKey = sourceKey;
            m_samplingInterp = interp;
            m_samplingSharp = sharp;
        }
    }
    doRefreshSampling();
}

void ImageCanvas::setCropMode(bool on)
{
    m_cropMode = on;
    clearSelection();
    setDragMode(on ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    // 非裁剪态交给 Qt 管理光标：ScrollHandDrag 拖动中给闭合手，松手恢复；
    // 原来的 ArrowCursor 会把抓手光标盖掉（可拖时张开手、按住握拳）
    if (on) {
        setCursor(Qt::CrossCursor);
    } else {
        unsetCursor();
    }
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
    // 长图首次装图：把这次「适合窗口」换成一次「适合宽度」（条漫起读）。
    // 消费即清，之后用户再选适合窗口就是真正的全览（轴锁随之解除）。
    if (m_pendingLongStripFitWidth) {
        m_pendingLongStripFitWidth = false;
        fitToWidthGeom();
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
    updateSceneRect();   // fit 的 zoom 变了，padding 场景换算同步重算
    centerOn(m_item);
    // 「图片大于窗口时显示图片顶部」：适合窗口按定义图片不会超出视口，这里只是
    // 防御（窗口尺寸变化竞态），真正可观测的消费点在 fitToWidth / zoomActual。
    applyAlignTopIfLarge();
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
    m_pendingLongStripFitWidth = false;   // 已经按宽度起读，意图达成
    resetTransform();
    // 宽度贴合要预留可能出现的垂直滚动条宽度，避免出现横向滚动条
    const int sbw = verticalScrollBar()->isVisible() || ih > viewport()->height()
                        ? verticalScrollBar()->sizeHint().width() : 0;
    const qreal z = qMax(1.0, static_cast<qreal>(viewport()->width() - sbw)) / iw;
    scale(z, z);
    m_zoom = z;
    m_fitToWindow = false;
    m_fitWidth = true;
    updateSceneRect();   // fit-width 的 zoom 变了，padding 场景换算同步重算
    if (ih * z <= viewport()->height()) {
        centerOn(m_item);
    } else if (m_alignTopWhenLarge) {
        // 图片高于视口：显示图片顶部（原版默认行为）。
        // padding 后滚动条 0 不是图片顶，改为图片顶边对齐视口顶边
        // （横向宽度已贴合，中心对齐即左对齐，不引入横向行程差异）
        const QRectF img = m_item->sceneBoundingRect();
        centerOn(img.center().x(), img.top() + viewport()->height() / (2.0 * z));
    } else {
        // 设置「图片大于窗口时显示图片顶部」关闭 → 垂直居中（横向仍贴左，
        // 宽度已贴合视口，横向滚动条无行程）
        centerOn(m_item);
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
    // 100% 显示且图片高于视口：按设置显示顶部（关=保持居中，即 applyZoomAt 的锚点结果）
    applyAlignTopIfLarge();
}

// 「图片大于窗口时显示图片顶部」（键 imageproc/align_top_when_large，默认开）：
// 图片显示尺寸高于视口时滚到顶，否则什么都不做（低于视口 Qt 自己居中）。
// 只在适配/模式切换路径调用；滚轮缩放走锚点对齐，不受此开关影响。
void ImageCanvas::applyAlignTopIfLarge()
{
    if (!m_alignTopWhenLarge || !m_item) {
        return;
    }
    const QSizeF isz = m_item->pixmap().deviceIndependentSize();
    if (isz.height() * transform().m11() > viewport()->height()) {
        // padding 后滚动条 0 不是图片顶：把图片顶边对齐到视口顶边
        // （场景点 = 图片顶 + 半个视口的高度 / 缩放，centerOn 送它到视口中心）
        const QRectF img = m_item->sceneBoundingRect();
        centerOn(img.center().x(), img.top() + viewport()->height() / (2.0 * transform().m11()));
    }
}

void ImageCanvas::setZoom(qreal zoom)
{
    applyZoomAt(zoom, viewport()->rect().center());
}

// 缩放锚点还原：视图无滚动行程时 Qt 自己居中（此时不需要对齐）；
// 有行程（含自由拖动 padding 给小图带来的行程）就按「场景点当前落在哪」
// 与「它应该落在哪」的像素差推滚动条。
void ImageCanvas::keepScenePointAt(const QPointF &scenePt, const QPoint &viewportPt)
{
    if (!m_item) {
        return;
    }
    const QScrollBar *hb = horizontalScrollBar();
    const QScrollBar *vb = verticalScrollBar();
    if (hb->maximum() <= hb->minimum() && vb->maximum() <= vb->minimum()) {
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
    m_pendingLongStripFitWidth = false;   // 明确缩放了，不再改起读方式
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
    // zoom 变了，可拖 padding 的场景换算跟着变——必须在 keepScenePointAt
    // 之前重算 sceneRect，否则对齐/拖动用的还是旧 zoom 的滚动行程
    // （insets 收完 viewport 已定型，这里算出的 padding 是最终值）。
    updateSceneRect();
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

// 长图轴锁（D2）：自动判定、无开关。条件是「当前显示方向的高/宽 ≥ 2.5
// 且纵向溢出视口」——前者覆盖条漫，后者保证只有需要上下拖时才锁；纵向能
// 全览（适合窗口/缩小到全图）即自动解除。横向是否溢出不影响判定：比例
// ≥ 2.5 的图横向溢出时纵向必然溢出，锁照样生效。旋转 90° 后 MainWindow
// 会把旋转烘进 pixmap，高宽互换，下一次事件即按新方向重新判定。
bool ImageCanvas::longStripLock() const
{
    if (!m_item || m_cropMode) {
        return false;
    }
    const QSizeF isz = m_item->pixmap().deviceIndependentSize();
    if (isz.width() <= 0.0 || isz.height() <= 0.0) {
        return false;
    }
    if (isz.height() / isz.width() < kLongStripAspect) {
        return false;
    }
    return isz.height() * transform().m11() > viewport()->height() + 0.5;
}

// 轴锁生效时把横向钉回中心：sceneRect 是图片四周等量 padding，滚动条中点
// 恰好对应图像水平中心。只由拖动路径调用——缩放锚点（滚轮/按钮）不经过
// 这里，保证「不影响缩放与旋转」。
void ImageCanvas::pinHorizontalCenter()
{
    QScrollBar *hb = horizontalScrollBar();
    const int mid = hb->minimum() + (hb->maximum() - hb->minimum()) / 2;
    if (hb->value() != mid) {
        hb->setValue(mid);
    }
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

// 悬停反馈由侧边箭头提供，光标保持普通箭头。
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
        viewport()->setCursor(Qt::ArrowCursor);
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
        if (!m_panSignalActive) {
            m_panSignalActive = true;
            emit panStateChanged(true);
        }
        event->accept();
        return;
    }
    // 鼠标按键映射（C10~C12）：右键 / X / Y 按下即派发。none 保留内置行为
    // （右键仍弹上下文菜单，X/Y 无操作）；映射成动作时吞掉按下，避免拖动/菜单。
    if (event->button() == Qt::RightButton || event->button() == Qt::XButton1
        || event->button() == Qt::XButton2) {
        const QString act = (event->button() == Qt::RightButton)
                                ? Config::instance().mouseRightAction()
                                : (event->button() == Qt::XButton1)
                                      ? Config::instance().mouseXAction()
                                      : Config::instance().mouseYAction();
        if (act != QLatin1String("none") && !act.isEmpty()) {
            emit mouseActionRequested(act);
            event->accept();
            return;
        }
    }
    // 左键按下先记账：松手时才判断「点击」还是「拖拽平移」（见 mouseReleaseEvent）
    if (event->button() == Qt::LeftButton) {
        m_pressTracked = m_item != nullptr && !m_sourcePixmap.isNull() && !m_cropMode;
        m_pressPos = event->pos();
        m_pressClock.restart();
    }
    // 保留 Qt 的拖拽处理，但侧边单击在超过拖拽阈值前保持普通箭头。
    QGraphicsView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton && edgeTurnDelta(event->pos()) != 0) {
        viewport()->setCursor(Qt::ArrowCursor);
    }
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
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        // 长图轴锁（D2）：横向钉回中心，拖动只上下动。
        if (longStripLock()) {
            pinHorizontalCenter();
        } else {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        }
        event->accept();
        return;
    }
    // 悬停（无按键）：箭头光标 + 箭头显隐（viewport 开了 mouse tracking）。
    // 统一走真实光标口径，鼠标不动时的其它刷新也走同一条路。
    if (event->buttons() == Qt::NoButton) {
        refreshEdgeHover();
    } else if (m_pressTracked && (event->buttons() & Qt::LeftButton)
               && (event->pos() - m_pressPos).manhattanLength() >= 3) {
        // 真正开始平移时恢复拖拽光标，松手不再翻页。
        if (edgeTurnDelta(m_pressPos) != 0 && dragMode() == QGraphicsView::ScrollHandDrag) {
            viewport()->setCursor(Qt::ClosedHandCursor);
        }
        m_pressTracked = false;
        // 拖动取消中央单击待判动作，避免拖完平移又翻一页。
        m_pendingActionTimer.stop();
        m_pendingAction.clear();
        if (!m_panSignalActive) {
            m_panSignalActive = true;
            emit panStateChanged(true);
        }
    }
    QGraphicsView::mouseMoveEvent(event);
    // 长图轴锁（D2）：左键拖动由 ScrollHandDrag 在基类里滚滚动条，先让 Qt
    // 走完本次拖拽，再把横向钉回中心，只保留上下位移。
    if ((event->buttons() & Qt::LeftButton) && longStripLock()) {
        pinHorizontalCenter();
    }
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
        if (m_panSignalActive) {
            m_panSignalActive = false;
            emit panStateChanged(false);
        }
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
    if (event->button() == Qt::LeftButton && m_panSignalActive) {
        m_panSignalActive = false;
        emit panStateChanged(false);
    }
    if (event->button() == Qt::LeftButton && m_pressTracked) {
        m_pressTracked = false;
        const bool isClick = m_pressClock.isValid() && m_pressClock.elapsed() < 300
                             && (event->pos() - m_pressPos).manhattanLength() < 3;
        const int delta = isClick ? edgeTurnDelta(event->pos()) : 0;
        QGraphicsView::mouseReleaseEvent(event);
        refreshEdgeHover();
        // 左键映射（C9）：非 none/pan 的动作在「点击」时派发（拖动平移不受影响）；
        // 边缘带照旧翻页，保证「两侧单击翻页」回归不被映射破坏。
        const QString leftAct = Config::instance().mouseLeftAction();
        if (isClick && delta == 0 && leftAct != QLatin1String("none")
            && leftAct != QLatin1String("pan")) {
            // 双击最大化开启时，中央单击暂存动作等系统双击间隔：若紧接着是中央双击，
            // mouseDoubleClickEvent 会取消待判改走最大化，避免一次双击既翻一页又最大化。
            // 侧边热区（delta!=0 不进此分支）与「双击最大化关闭」时保持即时派发。
            if (m_dblClickMaximize) {
                m_pendingAction = leftAct;
                m_pendingActionTimer.start(QApplication::doubleClickInterval());
            } else {
                emit mouseActionRequested(leftAct);
            }
            return;
        }
        // 两侧单击翻页受设置控制（键 "config/mouse/click_side_pageturn"）：
        // 关闭时箭头照常显示，点击不再翻页。
        if (delta == 0 || !Config::instance().sideClickPageturn()) {
            return;
        }
        emit edgeTurnRequested(delta);
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && edgeTurnDelta(event->pos()) != 0) {
        refreshEdgeHover();
    }
}

void ImageCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && edgeTurnDelta(event->pos()) != 0) {
        // Qt 将连续点击的第二次按下报告为双击，侧边区域仍按普通点击处理。
        // 注意这里只判「在不在侧边热区」，不判翻页开关：开关关掉时侧边双击
        // 只是「不翻页」，仍然不是画布中央的双击，不能落到下面去最大化。
        // 开关本身由 mousePressEvent 的派发路径把关。
        mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && m_dblClickMaximize) {
        // 中央双击：取消第一次单击暂存的动作（否则第一次 release 已派发翻页），
        // 只走最大化。待判定时器若还在跑，timeout 时 m_pendingAction 已清空，不会再发。
        m_pendingActionTimer.stop();
        m_pendingAction.clear();
        emit doubleClickRequested();
        event->accept();
        return;
    }
    QGraphicsView::mouseDoubleClickEvent(event);
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
    // 拖窗口边界：进入 burst 时主动清掉上一次的采样记账（防止拖回去后
    // 拿拖动前的旧尺寸当命中），几何照常每帧算，重采样交给收尾定时器补一次。
    if (!m_resizeBurst) {
        m_resizeBurst = true;
        invalidateSampling();
    }
    m_sampleFlush.start();
    // 自由拖动 padding 以视口尺寸为基准，视口变了 padding 跟着重算
    // （在 fit 几何之前调用：fit 的 centerOn/顶部对齐都依赖新 sceneRect）
    updateSceneRect();
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
