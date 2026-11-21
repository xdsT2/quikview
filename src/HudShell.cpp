#include "HudShell.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QtMath>

#include "AppIcon.h"
#include "UiStyle.h"

#ifdef Q_OS_WIN
#  include <windows.h>   // SPI_GETCLIENTAREAANIMATION（减少动效检测）
#endif

using namespace UiStyle;   // 令牌直取：UiStyle::hud::xxx 以 hud::xxx 书写

// ---------------------------------------------------------------------------
// 盒式模糊：分离横向/纵向滑窗求和。passes=3 时近似高斯，半径取设计值的 1/2。
// RGB32（背板采样，3 通道）与 ARGB32_Premultiplied（阴影轮廓，含 alpha 通道）
// 都支持；premultiplied 各通道独立模糊仍满足预乘约束。小尺寸图都够快。
// ---------------------------------------------------------------------------
namespace {

void boxBlurPass(const QImage &src, QImage &dst, int radius, bool horizontal)
{
    const int w = src.width();
    const int h = src.height();
    const int outer = horizontal ? h : w;
    const int inner = horizontal ? w : h;
    const int channels = src.format() == QImage::Format_ARGB32_Premultiplied ? 4 : 3;
    for (int o = 0; o < outer; ++o) {
        int sums[4] = {0, 0, 0, 0};
        for (int i = -radius; i <= radius; ++i) {
            const int x = qBound(0, i, inner - 1);
            const uchar *px = horizontal ? src.constScanLine(o) + x * 4
                                         : src.constScanLine(x) + o * 4;
            for (int c = 0; c < channels; ++c) {
                sums[c] += px[c];
            }
        }
        for (int i = 0; i < inner; ++i) {
            uchar *out = horizontal ? dst.scanLine(o) + i * 4
                                    : dst.scanLine(i) + o * 4;
            for (int c = 0; c < channels; ++c) {
                out[c] = uchar(sums[c] / (radius * 2 + 1));
            }
            if (channels == 3) {
                out[3] = 255;
            }
            const int addPos = qBound(0, i + radius + 1, inner - 1);
            const int subPos = qBound(0, i - radius, inner - 1);
            const uchar *add = horizontal ? src.constScanLine(o) + addPos * 4
                                         : src.constScanLine(addPos) + o * 4;
            const uchar *sub = horizontal ? src.constScanLine(o) + subPos * 4
                                         : src.constScanLine(subPos) + o * 4;
            for (int c = 0; c < channels; ++c) {
                sums[c] += add[c] - sub[c];
            }
        }
    }
}

QImage blurImage(const QImage &input, int radius, int passes)
{
    if (input.isNull() || radius <= 0) {
        return input;
    }
    const bool premultiplied = input.format() == QImage::Format_ARGB32_Premultiplied;
    QImage a = premultiplied ? input : input.convertToFormat(QImage::Format_RGB32);
    QImage b(a.size(), a.format());
    for (int p = 0; p < passes; ++p) {
        boxBlurPass(a, b, radius, true);
        boxBlurPass(b, a, radius, false);
    }
    return a;
}

} // namespace

// ---------------------------------------------------------------------------
// HudBackdrop：面板最底层的子部件，画圆角底板 + 边界。
// ---------------------------------------------------------------------------
HudBackdrop::HudBackdrop(QWidget *parent) : QWidget(parent)
{
    m_resampleTimer.setSingleShot(true);
    m_resampleTimer.setInterval(120);
    connect(&m_resampleTimer, &QTimer::timeout, this, &HudBackdrop::resample);
    setAttribute(Qt::WA_NoSystemBackground, true);
}

void HudBackdrop::setSource(QWidget *src)
{
    if (m_source) {
        m_source->removeEventFilter(this);
    }
    m_source = src;
    if (m_source) {
        m_source->installEventFilter(this);
    }
    invalidate();
}

void HudBackdrop::invalidate()
{
    m_dirty = true;
    if (!m_resampleTimer.isActive()) {
        m_resampleTimer.start();
    }
}

bool HudBackdrop::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_source && event->type() == QEvent::Paint) {
        invalidate();   // 画布换图/缩放/平移/动画帧都经 Paint 事件到这里
    }
    return QWidget::eventFilter(watched, event);
}

void HudBackdrop::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rc = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(rc, hud::Radius, hud::Radius);

    p.save();
    p.setClipPath(clip);
    p.fillRect(rect(), hud::plateSolid());
    if (!m_blurred.isNull()) {
        // 采样图带 DPR，drawImage 按逻辑尺寸落位；面板超出画布的部分保持实底
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QPointF(m_sampleOffset), m_blurred);
    }
    p.fillRect(rect(), hud::plateTint());
    p.restore();

    p.setPen(QPen(hud::border(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawPath(clip);
}

void HudBackdrop::resample()
{
    m_dirty = false;
    if (!UiStyle::blurMaterial() || !m_source || !isVisible() || !m_source->isVisible()) {
        return;
    }
    // 面板矩形（兄弟部件，经共同顶层窗口换算）→ 视口坐标，取交集防越界
    QWidget *win = m_source->window();
    const QPoint panelTl = mapTo(win, QPoint(0, 0));
    const QPoint srcTl = m_source->mapFrom(win, panelTl);
    QRect src(QRect(srcTl, size()).intersected(m_source->rect()));
    if (src.isEmpty()) {
        m_blurred = QImage();
        update();
        return;
    }
    const qreal dpr = devicePixelRatioF();
    QImage sample(int(src.width() * dpr) + 1, int(src.height() * dpr) + 1,
                  QImage::Format_RGB32);
    sample.setDevicePixelRatio(dpr);
    sample.fill(QColor(0x20, 0x20, 0x20));
    QPainter sp(&sample);
    m_source->render(&sp, QPoint(0, 0), QRegion(src));
    sp.end();

    // 模糊半径按 DPR 放大，视觉上保持 ~20 逻辑像素
    m_blurred = blurImage(sample, qMax(2, qRound(hud::BackdropBlur * dpr / 2.0)), 3);
    m_sampleOffset = src.topLeft() - srcTl;
    update();
}

// ---------------------------------------------------------------------------
// HudEffect：静息只画缓存投影，开启动效期间 progress 0→1 同步淡入，并以左上
// 为原点从 0.99 缩放回 1。关闭淡出只动 progress。投影由圆角矩形轮廓模糊而来
// （面板形状固定，尺寸变了才重建一次）。
// ---------------------------------------------------------------------------
HudEffect::HudEffect(QObject *parent, qreal radius)
    : QGraphicsEffect(parent), m_radius(radius < 0 ? hud::Radius : radius) {}

void HudEffect::setProgress(qreal v)
{
    m_progress = qBound(0.0, v, 1.0);
    update();
}

QRectF HudEffect::boundingRectFor(const QRectF &r) const
{
    const qreal pad = hud::ShadowBlur * 1.5 + 2;
    return r.adjusted(-pad, -pad, pad, pad + hud::ShadowDy);
}

void HudEffect::draw(QPainter *painter)
{
    const QRectF src = sourceBoundingRect();
    if (src.isEmpty()) {
        return;
    }
    rebuildShadow(src, painter);
    painter->save();
    const qreal opacity = painter->opacity() * m_progress;
    painter->setOpacity(opacity * m_shadowOpacity);
    if (!m_shadow.isNull()) {
        // 投影轮廓比面板大一圈 pad，落位时对齐面板顶点再向下偏移
        painter->drawImage(QPointF(src.left() - m_pad, src.top() - m_pad + hud::ShadowDy),
                           m_shadow);
    }
    painter->setOpacity(opacity);
    if (m_scaleOn && m_progress < 1.0) {
        const qreal s = 0.99 + 0.01 * m_progress;
        painter->translate(src.topLeft());
        painter->scale(s, s);
        painter->translate(-src.topLeft());
    }
    drawSource(painter);
    painter->restore();
}

void HudEffect::rebuildShadow(const QRectF &src, QPainter *painter)
{
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const qreal keyW = src.width() * 1000.0 + dpr;
    const qreal keyH = src.height() * 1000.0 + dpr;
    const int alpha = hud::shadow().alpha();
    if (!m_shadow.isNull() && keyW == m_keyW && keyH == m_keyH && alpha == m_shadowAlpha) {
        return;
    }
    m_keyW = keyW;
    m_keyH = keyH;
    m_shadowAlpha = alpha;
    const int radius = qMax(2, qRound(hud::ShadowBlur * dpr / 2.0));
    const int pad = radius * 3 + 1;
    m_pad = pad / dpr;
    QImage silhouette(qCeil(src.width() * dpr) + pad * 2, qCeil(src.height() * dpr) + pad * 2,
                      QImage::Format_ARGB32_Premultiplied);
    silhouette.setDevicePixelRatio(dpr);
    silhouette.fill(Qt::transparent);
    {
        QPainter sp(&silhouette);
        sp.setRenderHint(QPainter::Antialiasing, true);
        const QRectF panelRect(m_pad, m_pad, src.width(), src.height());
        QPainterPath path;
        path.addRoundedRect(panelRect, m_radius, m_radius);
        sp.fillPath(path, QColor(0, 0, 0));
    }
    // 盒式模糊作用在 ARGB32_Premultiplied 的 alpha 上；三遍近高斯
    m_shadow = blurImage(silhouette, radius, 3);
    // 使用当前主题的投影强度。
    uchar *bits = m_shadow.bits();
    for (int i = 0; i < m_shadow.sizeInBytes(); i += 4) {
        bits[i + 0] = 0;
        bits[i + 1] = 0;
        bits[i + 2] = 0;
        bits[i + 3] = uchar(bits[i + 3] * alpha / 255);
    }
}

// ---------------------------------------------------------------------------
// HudToolButton：28×28 热区，自绘悬停底、按下加深与键盘焦点环。
// ---------------------------------------------------------------------------
HudToolButton::HudToolButton(Glyph glyph, const QString &tip, QWidget *parent)
    : QPushButton(parent), m_glyph(glyph)
{
    setObjectName(QStringLiteral("hudToolBtn"));
    setFixedSize(hud::ToolBtn, hud::ToolBtn);
    setToolTip(tip);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
}

void HudToolButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRectF rc = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (isDown() || underMouse()) {
        QPainterPath bg;
        bg.addRoundedRect(rc, 6, 6);
        p.fillPath(bg, QColor(255, 255, 255, isDown() ? 34 : 18));
    }
    if (property("kbdFocus").toBool()) {
        QPainterPath ring;
        ring.addRoundedRect(rc, 6, 6);
        p.setPen(QPen(hud::focusRing(), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawPath(ring);
    }

    QColor ink = underMouse() || isDown() ? hud::valText() : QColor(0xc9, 0xc9, 0xc9);
    // 复制 / 关闭改用 Fluent 同义图标（2026-11-19 定稿）；资源缺失时保留原几何回退。
    const QRectF glyphRect(rect().center().x() - 7.0, rect().center().y() - 7.0, 14.0, 14.0);
    if (AppIcon::paint(p, m_glyph == Close ? QStringLiteral("x") : QStringLiteral("copy"),
                       glyphRect, ink)) {
        return;
    }
    QPen pen(ink, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const QPointF c = rect().center();
    if (m_glyph == Close) {
        p.drawLine(c + QPointF(-4.5, -4.5), c + QPointF(4.5, 4.5));
        p.drawLine(c + QPointF(4.5, -4.5), c + QPointF(-4.5, 4.5));
    } else {
        // 两张叠纸（沿用旧复制图标语义）：后纸在右上，前纸在左下
        p.drawRoundedRect(QRectF(c.x() - 5.5, c.y() - 2.0, 7.5, 8.0), 1.5, 1.5);
        p.drawRoundedRect(QRectF(c.x() - 2.0, c.y() - 6.0, 7.5, 8.0), 1.5, 1.5);
    }
}

// ---------------------------------------------------------------------------
// 开合动效：两块浮层共用同一套时长与语义。
// ---------------------------------------------------------------------------
bool hudReduceMotion()
{
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) {
        return true;   // 系统「减少动效」开启：直接切换，不播缩放/淡入
    }
#endif
    return false;
}

void hudStopAnimations(QWidget *panel)
{
    for (auto *anim : panel->findChildren<QVariantAnimation *>()) {
        anim->stop();
        anim->deleteLater();
    }
}

void hudAnimateShow(QWidget *panel, HudEffect *effect)
{
    hudStopAnimations(panel);
    if (effect) {
        effect->setScaleOn(false);
        effect->setProgress(1.0);
    }
    if (!effect || hudReduceMotion()) {
        return;
    }
    // 开 110ms：透明度 0→1 + 缩放 0.99→1（原点左上），无平移/上抬/弹跳
    auto *anim = new QVariantAnimation(panel);
    anim->setDuration(110);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    QObject::connect(anim, &QVariantAnimation::valueChanged, panel, [effect](const QVariant &v) {
        effect->setScaleOn(true);
        effect->setProgress(v.toDouble());
    });
    QObject::connect(anim, &QVariantAnimation::finished, panel, [effect]() {
        effect->setScaleOn(false);
        effect->setProgress(1.0);
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void hudAnimateDismiss(QWidget *panel, HudEffect *effect,
                       const std::function<void()> &onDone)
{
    hudStopAnimations(panel);
    if (!effect || hudReduceMotion()) {
        panel->setVisible(false);
        if (effect) {
            effect->setScaleOn(false);
            effect->setProgress(1.0);
        }
        if (onDone) {
            onDone();
        }
        return;
    }
    // 关 85ms：仅淡出，无位移
    auto *anim = new QVariantAnimation(panel);
    anim->setDuration(85);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::Linear);
    QObject::connect(anim, &QVariantAnimation::valueChanged, panel, [effect](const QVariant &v) {
        effect->setScaleOn(false);
        effect->setProgress(v.toDouble());
    });
    QObject::connect(anim, &QVariantAnimation::finished, panel, [panel, effect, onDone]() {
        panel->setVisible(false);
        effect->setProgress(1.0);   // 复位，下次 show 重新播开启动效
        if (onDone) {
            onDone();
        }
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
