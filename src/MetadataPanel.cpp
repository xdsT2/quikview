#include "MetadataPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QColorSpace>
#include <QDesktopServices>
#include <QEvent>
#include <QGraphicsEffect>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOption>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QtMath>

#include "ExifParser.h"
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
// HUD 背板：面板最底层的子部件，画圆角底板 + 边界。
// 磨砂材质开（UiStyle::blurMaterial，即设置页「磨砂材质（右键菜单与信息浮层）」）
// 时对画布视口做区域渲染 → 盒式模糊 ≈20px → 再叠 #212121@91% 染色，亮暗画布
// 上都有设计稿的材质感；关/采样不可用时回退 #242424 实底（不退回蓝灰蓝紫）。
// 采样在事件循环里异步做（画布每次 Paint 只标记脏 + 120ms 节流），paintEvent
// 里绝不递归 render；GIF 连续帧重绘自动合并成 ~8 次/秒的重采样。
// ---------------------------------------------------------------------------
class HudBackdrop : public QWidget
{
public:
    explicit HudBackdrop(QWidget *parent) : QWidget(parent)
    {
        m_resampleTimer.setSingleShot(true);
        m_resampleTimer.setInterval(120);
        connect(&m_resampleTimer, &QTimer::timeout, this, &HudBackdrop::resample);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

    void setSource(QWidget *src)
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

    void invalidate()
    {
        m_dirty = true;
        if (!m_resampleTimer.isActive()) {
            m_resampleTimer.start();
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_source && event->type() == QEvent::Paint) {
            invalidate();   // 画布换图/缩放/平移/动画帧都经 Paint 事件到这里
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
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

private:
    void resample()
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

    QTimer m_resampleTimer;
    QPointer<QWidget> m_source;
    QImage m_blurred;
    QPoint m_sampleOffset;
    bool m_dirty = false;
};

// ---------------------------------------------------------------------------
// HUD 常驻特效：静息只画缓存投影（向下 5 / 模糊 18 / 黑 33%）；开启动效期间
// progress 0→1 同步淡入，并以左上为原点从 0.99 缩放回 1。关闭淡出只动 progress。
// 投影由圆角矩形轮廓模糊而来（面板形状固定，尺寸变了才重建一次）。
// ---------------------------------------------------------------------------
class HudEffect : public QGraphicsEffect
{
public:
    explicit HudEffect(QObject *parent = nullptr) : QGraphicsEffect(parent) {}

    void setProgress(qreal v)
    {
        m_progress = qBound(0.0, v, 1.0);
        update();
    }
    qreal progress() const { return m_progress; }
    void setScaleOn(bool on) { m_scaleOn = on; }
    void invalidateShadow() { m_shadow = QImage(); update(); }

protected:
    QRectF boundingRectFor(const QRectF &r) const override
    {
        const qreal pad = hud::ShadowBlur + 2;
        return r.adjusted(-pad, -pad, pad, pad + hud::ShadowDy);
    }

    void draw(QPainter *painter) override
    {
        const QRectF src = sourceBoundingRect();
        if (src.isEmpty()) {
            return;
        }
        rebuildShadow(src, painter);
        painter->save();
        painter->setOpacity(m_progress);
        if (!m_shadow.isNull()) {
            // 投影轮廓比面板大一圈 pad，落位时对齐面板顶点再向下偏移
            painter->drawImage(QPointF(src.left() - m_pad, src.top() - m_pad + hud::ShadowDy),
                               m_shadow);
        }
        if (m_scaleOn && m_progress < 1.0) {
            const qreal s = 0.99 + 0.01 * m_progress;
            painter->translate(src.topLeft());
            painter->scale(s, s);
            painter->translate(-src.topLeft());
        }
        drawSource(painter);
        painter->restore();
    }

private:
    void rebuildShadow(const QRectF &src, QPainter *painter)
    {
        const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
        const qreal keyW = src.width() * 1000.0 + dpr;
        const qreal keyH = src.height() * 1000.0 + dpr;
        if (!m_shadow.isNull() && keyW == m_keyW && keyH == m_keyH) {
            return;
        }
        m_keyW = keyW;
        m_keyH = keyH;
        m_pad = int(qCeil(hud::ShadowBlur * dpr)) + 2;
        QImage silhouette(int(src.width() * dpr) + m_pad * 2, int(src.height() * dpr) + m_pad * 2,
                          QImage::Format_ARGB32_Premultiplied);
        silhouette.setDevicePixelRatio(dpr);
        silhouette.fill(Qt::transparent);
        {
            QPainter sp(&silhouette);
            sp.setRenderHint(QPainter::Antialiasing, true);
            const QRectF panelRect(m_pad, m_pad, src.width() * dpr, src.height() * dpr);
            QPainterPath path;
            path.addRoundedRect(panelRect, hud::Radius * dpr, hud::Radius * dpr);
            sp.fillPath(path, QColor(0, 0, 0));
        }
        // 盒式模糊作用在 ARGB32_Premultiplied 的 alpha 上；三遍近高斯
        m_shadow = blurImage(silhouette, qMax(2, qRound(hud::ShadowBlur * dpr / 2.0)), 3);
        // 黑色 33%：轮廓 alpha 压到峰值 33%，RGB 归零（premultiplied 黑）。
        uchar *bits = m_shadow.bits();
        for (int i = 0; i < m_shadow.sizeInBytes(); i += 4) {
            bits[i + 0] = 0;
            bits[i + 1] = 0;
            bits[i + 2] = 0;
            bits[i + 3] = uchar(bits[i + 3] * 0.33);
        }
    }

    QImage m_shadow;
    qreal m_keyW = -1;
    qreal m_keyH = -1;
    qreal m_progress = 1.0;
    bool m_scaleOn = false;
    int m_pad = 0;
};

// ---------------------------------------------------------------------------
// 标题栏 28×28 热区按钮（复制 / 关闭）：自绘悬停底、按下加深与键盘焦点环。
// 焦点环只在键盘导航（Tab/Backtab/Shortcut 焦点原因）时出现，见
// MetadataPanel::updateFocusProperty；鼠标点击不取焦点（TabFocus 策略），
// 画布焦点得以保留。
// ---------------------------------------------------------------------------
class HudToolButton : public QPushButton
{
public:
    enum Glyph { Close, Copy };

    HudToolButton(Glyph glyph, const QString &tip, QWidget *parent = nullptr)
        : QPushButton(parent), m_glyph(glyph)
    {
        setObjectName(QStringLiteral("hudToolBtn"));
        setFixedSize(hud::ToolBtn, hud::ToolBtn);
        setToolTip(tip);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
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

private:
    Glyph m_glyph;
};

// ---------------------------------------------------------------------------
// 可换行值标签：值列不省略，长文件名/长 GPS 允许 WrapAnywhere 折成多行读全；
// Tooltip 保留全文。宽度由网格按浮层宽分配，不产生横向滚动。
// ---------------------------------------------------------------------------
class WrappingLabel : public QLabel
{
public:
    explicit WrappingLabel(QWidget *parent = nullptr) : QLabel(parent)
    {
        setWordWrap(true);
        // 水平 Ignored：列宽交给 QGridLayout 按浮层宽分配；高度按折行后的
        // heightForWidth 走，避免「占两行的值被压成一行」。
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }

    void setFullText(const QString &text)
    {
        // QLabel 的 wordWrap 只在词边界断行，连续长词（超长文件名 aaaa…、无空格
        // URL）会一行到底溢出被裁。在每个字符间插零宽空格（宽度为 0，不改变
        // 显示与度量），折行即等价 WrapAnywhere：过长就回车切断，下一行继续。
        QString display;
        display.reserve(text.size() * 2);
        for (int i = 0; i < text.size(); ++i) {
            if (i > 0) {
                display += QChar(0x200B);
            }
            display += text[i];
        }
        QLabel::setText(display);
        setToolTip(text);
    }

    // 高度收紧到设计行框 18：单行 12px 雅黑 fm.height≈16，+2 刚好 18（旧版 +4
    // 是给下划线留的，下划线已随地图链接移出值标签）。未布局时 width()==0：
    // heightForWidth(0) 会把每个字符都当成一行（实测 23 行键值撑出 3060px），
    // 所以 0 宽按浮层上限宽估，真实换行交给布局的 hfw。
    QSize sizeHint() const override
    {
        QSize hint = QLabel::sizeHint();
        const int w = width() > 0 ? width() : 300;
        hint.setHeight(qMax(fontMetrics().height() + 2, heightForWidth(w) + 2));
        return hint;
    }

    QSize minimumSizeHint() const override { return QSize(0, sizeHint().height()); }

    int naturalHeight(int w) const { return heightForWidth(qMax(1, w)) + 2; }

private:
    QString m_full;
};

// ---------------------------------------------------------------------------
// 直方图：深色井底 + 中性灰柱 + 0–255 刻度（设计定稿：不改灰度直方图语义，
// 只换中性配色；10px 刻度 #BDBDBD，图表内圆角 6）。
// ---------------------------------------------------------------------------
class HistogramView : public QWidget
{
public:
    explicit HistogramView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(90);
    }

    void setBins(const int *bins)
    {
        for (int i = 0; i < 256; ++i) {
            m_bins[i] = bins[i];
        }
        update();
    }

    void clearBins()
    {
        for (int i = 0; i < 256; ++i) {
            m_bins[i] = 0;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath plate;
        plate.addRoundedRect(box, 6, 6);
        painter.fillPath(plate, hud::chartPlate());
        painter.setPen(QPen(hud::chartLine(), 1));
        painter.drawPath(plate);

        const int padX = 9;
        const int padTop = 9;
        const int labelH = 13;
        QRectF bars(box.left() + padX, box.top() + padTop,
                    box.width() - padX * 2, box.height() - padTop - labelH - 5);

        QFont small = font();
        small.setPixelSize(10);
        painter.setFont(small);
        painter.setPen(hud::tickText());
        const QRectF labelRow(box.left() + padX, bars.bottom() + 2,
                              box.width() - padX * 2, labelH);
        painter.drawText(labelRow, Qt::AlignLeft | Qt::AlignTop, QStringLiteral("0"));
        painter.drawText(labelRow, Qt::AlignRight | Qt::AlignTop, QStringLiteral("255"));

        if (bars.height() <= 0) {
            return;
        }
        // 256 个 bin 在这个宽度下会糊成一片，合并成 64 根（一根根分开的柱体）
        constexpr int kGroups = 64;
        int sums[kGroups] = {};
        int peak = 1;
        for (int g = 0; g < kGroups; ++g) {
            int sum = 0;
            for (int i = 0; i < 4; ++i) {
                sum += m_bins[g * 4 + i];
            }
            sums[g] = sum;
            peak = qMax(peak, sum);
        }

        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(Qt::NoPen);
        painter.setBrush(hud::chartBar());
        const qreal slot = bars.width() / kGroups;
        for (int g = 0; g < kGroups; ++g) {
            if (sums[g] <= 0) {
                continue;
            }
            const qreal h = bars.height() * sums[g] / peak;
            if (h < 1.0) {
                continue;  // 不足 1px 的低柱画出来只会变成一串虚线点
            }
            painter.drawRect(QRectF(bars.left() + g * slot, bars.bottom() - h,
                                    qMax<qreal>(1.0, slot - 1.0), h));
        }
    }

private:
    int m_bins[256] = {};
};

namespace {

// GPS 十进制度 → 「N 30°36'19.03"」样式（GPS 信息行的度分秒格式）
QString formatDms(double deg, QChar positive, QChar negative)
{
    const QChar hem = deg >= 0 ? positive : negative;
    qreal a = qAbs(deg);
    const int d = int(a);
    a = (a - d) * 60.0;
    const int m = int(a);
    const qreal s = (a - m) * 60.0;
    return QStringLiteral("%1 %2°%3'%4\"")
        .arg(hem).arg(d).arg(m).arg(s, 0, 'f', 2);
}

} // namespace

// ---------------------------------------------------------------------------

MetadataPanel::MetadataPanel(QWidget *parent) : QWidget(parent)
{
    buildUi();
    clear();
}

void MetadataPanel::buildUi()
{
    setObjectName(QStringLiteral("infoPanel"));
    // 背板（材质/圆角/边界）由 HudBackdrop 自绘，本体透明，QSS 只管文字与按钮。
    setStyleSheet(UiStyle::hudQss());

    m_backdrop = new HudBackdrop(this);
    m_backdrop->setGeometry(rect());
    m_backdrop->lower();

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    m_body = new QWidget(this);
    m_bodyLayout = new QVBoxLayout(m_body);
    m_bodyLayout->setContentsMargins(hud::PadH, hud::PadTop, hud::PadH, hud::PadBottom);
    m_bodyLayout->setSpacing(0);
    root->addWidget(m_body);

    // —— 标题栏（30px）：「图片信息」左，复制/关闭 28×28 右 ——
    auto *titleRow = new QWidget(m_body);
    titleRow->setFixedHeight(hud::TitleH);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(2);
    auto *title = new QLabel(tr("图片信息"), titleRow);
    title->setObjectName(QStringLiteral("infoTitle"));
    QFont titleFont = UiStyle::font(14);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    titleLayout->addWidget(title);
    titleLayout->addStretch(1);

    m_copyBtn = new HudToolButton(HudToolButton::Copy, tr("复制参数"), titleRow);
    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_copyLines.join(QLatin1Char('\n')));
        emit copied();
    });
    titleLayout->addWidget(m_copyBtn);
    auto *closeBtn = new HudToolButton(HudToolButton::Close, tr("关闭 (Tab)"), titleRow);
    connect(closeBtn, &QPushButton::clicked, this, [this]() { dismiss(); });
    titleLayout->addWidget(closeBtn);
    m_bodyLayout->addWidget(titleRow);

    rebuildGrid();

    // GPS 入口：中性文字链接（无强调蓝），放进 grid 的 GPS 组内（见 showImage）
    m_mapBtn = new QPushButton(tr("在地图中查看"), m_body);
    m_mapBtn->setObjectName(QStringLiteral("mapLink"));
    m_mapBtn->setCursor(Qt::PointingHandCursor);
    m_mapBtn->setFlat(true);
    m_mapBtn->setFocusPolicy(Qt::NoFocus);
    QFont mapFont = UiStyle::font(12);
    mapFont.setUnderline(true);
    m_mapBtn->setFont(mapFont);
    m_mapBtn->setFixedHeight(hud::RowH + 2);   // 行框 20：按钮 min hint 不超过所在行
    connect(m_mapBtn, &QPushButton::clicked, this, [this]() {
        if (!m_mapUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(m_mapUrl));
        }
    });
    m_mapBtn->hide();

    // —— 直方图分隔（上下各 4px 留白 + 1px 细分隔）——
    auto *sepHolder = new QWidget(m_body);
    auto *sepLayout = new QVBoxLayout(sepHolder);
    sepLayout->setContentsMargins(0, 4, 0, 4);
    sepLayout->setSpacing(0);
    m_histSep = new QFrame(sepHolder);
    m_histSep->setObjectName(QStringLiteral("hudSep"));
    m_histSep->setFrameShape(QFrame::NoFrame);
    m_histSep->setFixedHeight(1);
    sepLayout->addWidget(m_histSep);
    m_bodyLayout->addWidget(sepHolder);

    // —— 底部可折叠直方图（默认展开，折叠态跨切图保持）——
    m_histToggle = new QPushButton(m_body);
    m_histToggle->setObjectName(QStringLiteral("histToggle"));
    m_histToggle->setCursor(Qt::PointingHandCursor);
    m_histToggle->setFlat(true);
    m_histToggle->setFocusPolicy(Qt::NoFocus);
    QFont histFont = UiStyle::font(12);
    histFont.setWeight(QFont::DemiBold);
    m_histToggle->setFont(histFont);
    m_histToggle->setText(tr("▾ 直方图"));
    connect(m_histToggle, &QPushButton::clicked, this, [this]() {
        const bool show = m_histBody->isHidden();
        m_histBody->setVisible(show);
        m_histToggle->setText(show ? tr("▾ 直方图") : tr("▸ 直方图"));
        // 折叠/展开改了内容高度：确定性重算总高（面板固定高随之变化），
        // MainWindow 收到 heightChanged 后重排浮层几何。
        updateHeights();
        emit heightChanged();
    });
    m_bodyLayout->addWidget(m_histToggle, 0, Qt::AlignLeft);

    m_histBody = new QWidget(m_body);
    auto *histLayout = new QVBoxLayout(m_histBody);
    histLayout->setContentsMargins(0, 4, 0, 0);
    m_histogram = new HistogramView(m_histBody);
    histLayout->addWidget(m_histogram);
    m_histBody->show();
    m_bodyLayout->addWidget(m_histBody);

    // 不加 addStretch：面板高度=内容自然高，多余的 stretch 只会在「面板被外部
    // 几何拉高」时把内容推开，形成无意义的空白。

    // 常驻特效：静息只画缓存投影，开/关动效共用 progress（构造即装，阴影常在）
    m_effect = new HudEffect(this);
    setGraphicsEffect(m_effect);

    // 键盘焦点环属性跟随（鼠标点击不产生 Tab 原因焦点，不会残留蓝框）
    m_copyBtn->installEventFilter(this);
    closeBtn->installEventFilter(this);
    m_mapBtn->installEventFilter(this);
    m_histToggle->installEventFilter(this);
}

// 键值 grid 每次换图整个重建：QGridLayout 移除 item 后行数/行高不会缩回去，
// 复用会踩到上一张图留下的空行与旧行高。
void MetadataPanel::rebuildGrid()
{
    if (m_gridHolder) {
        // 地图链接此前可能被 addWidget 复 parent 进旧 holder——不摘出来就会
        // 跟着旧 holder 一起被销毁，成员指针悬空。
        if (m_mapBtn && m_mapBtn->parentWidget() == m_gridHolder) {
            m_mapBtn->setParent(m_body);
        }
        // 先从布局摘除再延迟销毁：只 deleteLater 的话，旧 holder 要到下一轮
        // 事件循环才析构，期间仍留在 bodyLayout 里参与 sizeHint。
        m_bodyLayout->removeWidget(m_gridHolder);
        m_gridHolder->deleteLater();
        m_gridHolder = nullptr;
        m_grid = nullptr;
    }
    m_valueRows.clear();
    m_fixedRows.clear();
    // 自管行号：Qt6 空 QGridLayout::rowCount() 返回 1（实测），不能拿它当行游标，
    // 否则整个 grid 从第 1 行起排，留出一行幽灵空行。
    m_gridRow = 0;
    m_gridHolder = new QWidget(m_body);
    m_grid = new QGridLayout(m_gridHolder);
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setHorizontalSpacing(hud::KeyValGap);   // 列间 8
    m_grid->setVerticalSpacing(hud::RowGap);        // 行间 2
    m_grid->setColumnMinimumWidth(0, hud::KeyCol);  // 键列 76
    m_grid->setColumnStretch(0, 0);
    m_grid->setColumnStretch(1, 1);
    m_bodyLayout->insertWidget(1, m_gridHolder);    // 标题栏之后
}

void MetadataPanel::clear()
{
    m_copyLines.clear();
    m_maxValW = 0;
    m_mapUrl.clear();
    if (m_mapBtn) {
        m_mapBtn->hide();
    }
    if (m_histogram) {
        m_histogram->clearBins();
    }
    rebuildGrid();
}

void MetadataPanel::addGroup(const QList<Row> &rows)
{
    // 没有的数据不占位——空串 / 仅 '-' / 「未知」整行不生成；
    // 组内过滤后没有任何行就整组跳过（连组间距一起省掉）。
    QList<Row> live;
    for (const Row &r : rows) {
        const QString v = r.value.trimmed();
        if (v.isEmpty() || v == QLatin1String("-") || v == tr("未知")
            || v.compare(QLatin1String("unknown"), Qt::CaseInsensitive) == 0) {
            continue;
        }
        live.append(r);
    }
    if (live.isEmpty()) {
        return;
    }

    int row = m_gridRow;
    const int rowPx = m_smallFont ? 10 : 12;   // 「用小字体显示 EXIF / 文件信息」
    for (const Row &r : live) {
        auto *key = new QLabel(r.key + QStringLiteral(":"), m_gridHolder);
        key->setObjectName(QStringLiteral("infoKey"));
        key->setProperty("smallFont", m_smallFont);   // QSS 10px 档位（见 UiStyle.h）
        key->setFont(UiStyle::font(rowPx));
        key->setFixedHeight(hud::RowH);               // 行框 18

        // 值列不省略：超长值换行读全，Tooltip 保留全文，复制走 m_copyLines 全文
        auto *value = new WrappingLabel(m_gridHolder);
        value->setObjectName(QStringLiteral("infoVal"));
        value->setProperty("smallFont", m_smallFont);
        value->setFont(UiStyle::font(rowPx));
        if (r.strong) {
            QFont f = value->font();
            f.setWeight(QFont::DemiBold);             // 文件名 600
            value->setFont(f);
            value->setProperty("strong", true);
        }
        value->setFullText(r.value);

        // 自然宽度按值列最长单行计（超出上限的值在浮层内换行，不把卡片顶到 440；
        // 330 = 440 - 水平内边距 - 键列 - 列间）
        const int singleLine = value->fontMetrics().horizontalAdvance(r.value);
        m_maxValW = qMax(m_maxValW, qMin(singleLine,
                        hud::WidthMax - hud::PadH * 2 - hud::KeyCol - hud::KeyValGap));

        m_grid->addWidget(key, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
        m_grid->addWidget(value, row, 1);
        m_valueRows.append({ value, row });
        m_copyLines.append(r.key + QStringLiteral(": ") + r.value);
        ++row;
    }
    m_gridRow = row;
}

void MetadataPanel::addGroupGap()
{
    // 组间：上下 4px 留白 + 1px 细分隔（行框压 9px，分隔线垂直居中）
    const int row = m_gridRow++;
    auto *sep = new QFrame(m_gridHolder);
    sep->setObjectName(QStringLiteral("hudSep"));
    sep->setFrameShape(QFrame::NoFrame);
    sep->setFixedHeight(1);
    m_grid->addWidget(sep, row, 0, 1, 2, Qt::AlignVCenter);
    m_grid->setRowMinimumHeight(row, hud::SepH);
    m_fixedRows.insert(row, hud::SepH);
    if (!m_copyLines.isEmpty() && !m_copyLines.last().isEmpty()) {
        m_copyLines.append(QString());
    }
}

void MetadataPanel::showImage(const QString &path, const QImage &image)
{
    m_lastPath = path;
    m_lastImage = image;
    clear();

    const QFileInfo info(path);

    // —— 组 1：文件（文件名 / 图片大小 / 修改日期 / 图片信息 / 位深）——
    QList<MetadataPanel::Row> fileRows;
    fileRows.append({ tr("文件名"), info.fileName(), true });   // 文件名 600 字重
    const qint64 bytes = info.size();
    if (bytes >= 1024 * 1024) {
        fileRows.append({ tr("图片大小"), tr("%1 MB").arg(QString::number(bytes / 1048576.0, 'f', 1)) });
    } else if (bytes >= 1024) {
        fileRows.append({ tr("图片大小"), tr("%1 KB").arg(QString::number(bytes / 1024.0, 'f', 0)) });
    } else {
        fileRows.append({ tr("图片大小"), tr("%1 B").arg(bytes) });
    }
    fileRows.append({ tr("修改日期"),
                      info.lastModified().toString(QStringLiteral("yyyy/MM/dd HH:mm:ss")) });

    const QColorSpace space = image.colorSpace();
    QString colorSpaceText;
    if (space.isValid()) {
        colorSpaceText = space.description();
        // QColorSpace 的全名带规范号（"sRGB IEC61966-2.1"），只留色域短名
        if (int cut = colorSpaceText.indexOf(QLatin1Char(' ')); cut > 0) {
            colorSpaceText.truncate(cut);
        }
        if (colorSpaceText.isEmpty()) {
            colorSpaceText = tr("未知");
        }
    } else {
        colorSpaceText = QStringLiteral("sRGB");  // 无 ICC 时按 sRGB 解释
    }
    QString imageInfo = image.isNull()
        ? tr("未知")
        : tr("%1 × %2 (%3, %4)").arg(image.width()).arg(image.height())
              .arg(info.suffix().toUpper(), colorSpaceText);
    fileRows.append({ tr("图片信息"), imageInfo });

    // RGB32 只是内部对齐，真实色深仍是 24 位（多出来的字节是填充）
    int depth = image.depth();
    if (depth == 32 && !image.hasAlphaChannel()) {
        depth = 24;
    }
    fileRows.append({ tr("位深"), tr("%1 bit").arg(depth) });
    addGroup(fileRows);

    // —— 组 2 起：EXIF（按相机/拍摄参数归类，组间细分隔）——
    addGroupGap();
    const ExifParser::Result exif = ExifParser::parse(path);
    if (exif.hasGps) {
        // 对照 Honeyview：Google Maps 模板（真实地图入口不变）
        m_mapUrl = QStringLiteral("https://maps.google.com/maps?q=%1,%2&z=15")
                       .arg(QString::number(exif.lat, 'f', 6), QString::number(exif.lng, 'f', 6));
    }

    const QList<QPair<QString, QStringList>> groups = {
        {tr("相机"),
         {QStringLiteral("相机制造商"), QStringLiteral("相机型号"), QStringLiteral("镜头"),
          QStringLiteral("软件"), QStringLiteral("作者"), QStringLiteral("版权")}},
        {tr("拍摄参数"),
         {QStringLiteral("拍摄时间"), QStringLiteral("ISO"), QStringLiteral("曝光时间"),
          QStringLiteral("光圈"), QStringLiteral("焦距"), QStringLiteral("曝光补偿")}},
    };

    QMap<QString, QString> rest = exif.tags;
    bool exifRows = false;
    for (const auto &group : groups) {
        QList<MetadataPanel::Row> rows;
        for (const QString &key : group.second) {
            if (rest.contains(key)) {
                rows.append({ key, rest.take(key) });
            }
        }
        if (!rows.isEmpty()) {
            if (exifRows) {
                addGroupGap();
            }
            addGroup(rows);
            exifRows = true;
        }
    }

    // 位置：GPS 折成一行「GPS 信息」（度分秒），地图链接跟在该组下方。
    // ExifParser 会同时往 tags 里塞「GPS 纬度/经度」十进制行，与 DMS 行重复，去掉。
    if (exif.hasGps) {
        if (exifRows) {
            addGroupGap();
        }
        QList<MetadataPanel::Row> gps;
        gps.append({ tr("GPS 信息"),
                     formatDms(exif.lat, 'N', 'S') + QStringLiteral(", ")
                         + formatDms(exif.lng, 'E', 'W') });
        addGroup(gps);
        exifRows = true;
        rest.remove(QStringLiteral("GPS 纬度"));
        rest.remove(QStringLiteral("GPS 经度"));

        // 「在地图中查看」对齐值列，紧贴 GPS 行（对照设计稿 04）
        const int mapRow = m_gridRow++;
        m_grid->addWidget(m_mapBtn, mapRow, 1, Qt::AlignLeft | Qt::AlignVCenter);
        m_grid->setRowMinimumHeight(mapRow, hud::RowH + 2);
        m_fixedRows.insert(mapRow, hud::RowH + 2);
        m_mapBtn->show();
    }

    if (!rest.isEmpty()) {
        if (exifRows) {
            addGroupGap();
        }
        QList<MetadataPanel::Row> other;
        for (auto it = rest.constBegin(); it != rest.constEnd(); ++it) {
            other.append({ it.key(), it.value() });
        }
        addGroup(other);
        exifRows = true;
    }

    // 没有 EXIF 就什么都不显示——不生成占位行，面板只剩文件基础信息 + 直方图。

    // —— 直方图（同一份数据，折叠块；折叠态不在此处改动）——
    renderHistogram(image);

    finishLayout();
}

void MetadataPanel::setSmallFont(bool small)
{
    if (m_smallFont == small) {
        return;
    }
    m_smallFont = small;
    // 已有内容按最近一次的入参重建：字号变了，值列的自然宽度也要重算。
    if (!m_lastPath.isEmpty()) {
        showImage(m_lastPath, m_lastImage);
    }
}

void MetadataPanel::refreshThemeStyle()
{
    // HUD 材质不随主题翻面，但局部 QSS 表要按当前令牌重装；磨砂设置可能改了，
    // 背板采样一并重算（关磨砂会回 #242424 实底）。
    setStyleSheet(UiStyle::hudQss());
    if (m_backdrop) {
        m_backdrop->invalidate();
    }
    update();
}

void MetadataPanel::setBackdropSource(QWidget *canvasViewport)
{
    if (m_backdrop) {
        m_backdrop->setSource(canvasViewport);
    }
}

void MetadataPanel::setAvailableWidth(int w)
{
    if (m_availW == w) {
        return;
    }
    m_availW = w;
    finishLayout();   // 宽度变了折行/行高随之重排，sizeHint 才是新鲜的
}

void MetadataPanel::finishLayout()
{
    // 宽度自适应：基准 314；常规内容 ≤340；确需扩宽 ≤440；再钳到窗口可用宽。
    const int natural = hud::PadH * 2 + hud::KeyCol + hud::KeyValGap + m_maxValW;
    int w = qBound(hud::WidthBase, natural, hud::WidthNormal);
    if (natural > hud::WidthNormal) {
        w = qMin(natural, hud::WidthMax);
    }
    if (m_availW > 0) {
        // 极窄窗口不把 HUD 画出可用区域；下限保住标题 + 两个 28×28 热区
        w = qMin(w, qMax(hud::ToolBtn * 2 + 120, m_availW));
    }
    m_contentWidth = w;
    setFixedWidth(w);
    updateHeights();
}

// 确定性总高：标题栏 30 + 键值 grid（自管行号逐行求和）+ 分隔 9 + 折叠标题 24
// + 直方图 94 + 上下内边距。不依赖 Qt 布局缓存的 sizeHint 链——换图/折叠后
// applyChromeLayout 读到的 hint 恒等于设计行框求和（旧链路实测会残留 617/687）。
void MetadataPanel::updateHeights()
{
    if (!m_gridHolder) {
        return;
    }
    const int gridW = width() - hud::PadH * 2;
    const int valColW = qMax(40, gridW - hud::KeyCol - hud::KeyValGap);
    QHash<int, int> rowH;
    for (const auto &vr : m_valueRows) {
        // 行框 18/行：按折行数抬到 18 的倍数（≥ 标签自然高，不裁字）
        const QFontMetrics fm(vr.first->fontMetrics());
        const int naturalH = vr.first->naturalHeight(valColW);
        const int ls = qMax(1, fm.lineSpacing());
        const int lines = qMax(1, (naturalH + ls / 2) / ls);
        const int h = qMax(hud::RowH, lines * hud::RowH + (lines - 1) * hud::RowGap);
        rowH.insert(vr.second, h);
        m_grid->setRowMinimumHeight(vr.second, h);
    }
    for (auto it = m_fixedRows.constBegin(); it != m_fixedRows.constEnd(); ++it) {
        rowH.insert(it.key(), it.value());
    }
    int gridH = 0;
    for (int r = 0; r < m_gridRow; ++r) {
        gridH += rowH.value(r, hud::RowH);
        if (r > 0) {
            gridH += hud::RowGap;   // 行间 2
        }
    }
    m_gridHolder->setFixedSize(gridW, gridH);
    m_histToggle->setFixedHeight(24);
    const int histH = (m_histBody && m_histBody->isVisibleTo(this)) ? 94 : 0;
    const int total = hud::PadTop + hud::TitleH + gridH + hud::SepH + 24 + histH + hud::PadBottom;
    setFixedHeight(total);
    if (layout()) {
        layout()->invalidate();
    }
    updateGeometry();
}

void MetadataPanel::dumpHeights(const QString &path, const QString &tag) const
{
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream ts(&f);
    ts << (tag.isEmpty() ? QStringLiteral("DUMP") : tag) << " hint=" << sizeHint().height() << " actual=" << height()
       << " width=" << width()
       << " bodyHint=" << (m_body ? m_body->sizeHint().height() : -1)
       << " bodyActual=" << (m_body ? m_body->height() : -1)
       << " gridHolderHint=" << (m_gridHolder ? m_gridHolder->sizeHint().height() : -1)
       << " gridHolderActual=" << (m_gridHolder ? m_gridHolder->height() : -1)
       << " zombie=" << (m_body ? m_body->findChildren<QWidget *>().count() : -1)
       << " rowMins=";
    if (m_grid) {
        for (int r = 0; r < m_grid->rowCount(); ++r) {
            ts << m_grid->rowMinimumHeight(r) << ',';
        }
    } else {
        ts << '-';
    }
    ts << " labels=";
    for (const auto &vr : m_valueRows) {
        ts << vr.first->sizeHint().height() << '@' << vr.first->width() << ',';
    }
    ts
       << " rows=" << (m_grid ? m_grid->rowCount() : -1)
       << " rootHint=" << (layout() ? layout()->sizeHint().height() : -1)
       << " histVis=" << (m_histBody ? m_histBody->isVisibleTo(this) : -1)
       << " histH=" << (m_histBody ? m_histBody->sizeHint().height() : -1)
       << " mapBtn=" << (m_mapBtn && m_mapBtn->isVisibleTo(this) ? 1 : 0)
       << " mapUrl=" << (m_mapUrl.isEmpty() ? QStringLiteral("-") : m_mapUrl)
       << " copyLines=" << m_copyLines.size()
       << " closing=" << (m_closing ? 1 : 0)
       << "\n";
}

void MetadataPanel::renderHistogram(const QImage &image)
{
    if (image.isNull() || image.width() * image.height() > 24 * 1000 * 1000) {
        m_histogram->clearBins();
        return;
    }

    const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
    int bins[256] = {};
    const int step = qMax(1, gray.width() * gray.height() / 400000);  // 抽样，控制耗时
    for (int y = 0; y < gray.height(); y += step) {
        const uchar *line = gray.constScanLine(y);
        for (int x = 0; x < gray.width(); x += step) {
            ++bins[line[x]];
        }
    }
    m_histogram->setBins(bins);
}

// ---------------------------------------------------------------------------
// 显隐语义与动效
// ---------------------------------------------------------------------------

bool MetadataPanel::reduceMotion()
{
#ifdef Q_OS_WIN
    BOOL enabled = TRUE;
    if (SystemParametersInfo(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) && !enabled) {
        return true;   // 系统「减少动效」开启：直接切换，不播缩放/淡入
    }
#endif
    return false;
}

void MetadataPanel::startOpenAnimation()
{
    if (!m_effect || reduceMotion()) {
        return;
    }
    // 开 110ms：透明度 0→1 + 缩放 0.99→1（原点左上），无平移/上抬/弹跳
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(110);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        if (m_effect) {
            m_effect->setScaleOn(true);
            m_effect->setProgress(v.toDouble());
        }
    });
    connect(anim, &QVariantAnimation::finished, this, [this]() {
        if (m_effect) {
            m_effect->setScaleOn(false);
            m_effect->setProgress(1.0);
        }
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MetadataPanel::showHud()
{
    setVisible(true);
    raise();
}

void MetadataPanel::dismiss()
{
    if (m_closing || !isVisible()) {
        return;
    }
    m_closing = true;   // 先立旗：closed() 连锁的 setChecked(false) 再进 dismiss 会早退
    emit closed();
    if (reduceMotion() || !m_effect) {
        m_closing = false;
        setVisible(false);
        return;
    }
    // 关 85ms：仅淡出，无位移
    auto *anim = new QVariantAnimation(this);
    anim->setDuration(85);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::Linear);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        if (m_effect) {
            m_effect->setScaleOn(false);
            m_effect->setProgress(v.toDouble());
        }
    });
    connect(anim, &QVariantAnimation::finished, this, [this]() {
        m_closing = false;
        setVisible(false);
        if (m_effect) {
            m_effect->setProgress(1.0);   // 复位，下次 showEvent 重新播开启动效
        }
    });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MetadataPanel::hideNow()
{
    const bool wasVisible = isVisible();
    m_closing = false;
    for (auto *anim : findChildren<QVariantAnimation *>()) {
        anim->stop();
        anim->deleteLater();
    }
    if (m_effect) {
        m_effect->setScaleOn(false);
        m_effect->setProgress(1.0);
    }
    setVisible(false);
    if (wasVisible) {
        emit closed();   // 失焦隐藏也同步菜单勾选态（「清理可见状态」）
    }
}

bool MetadataPanel::histFolded() const
{
    return m_histBody && !m_histBody->isVisibleTo(this);
}

void MetadataPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_backdrop) {
        m_backdrop->invalidate();   // 面板位置/画布内容都可能变了
    }
    startOpenAnimation();
}

void MetadataPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_backdrop) {
        m_backdrop->setGeometry(rect());
        m_backdrop->invalidate();
    }
}

bool MetadataPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        updateFocusProperty(watched, event);
    }
    return QWidget::eventFilter(watched, event);
}

void MetadataPanel::updateFocusProperty(QObject *watched, QEvent *event)
{
    auto *w = qobject_cast<QWidget *>(watched);
    if (!w) {
        return;
    }
    bool kbd = false;
    if (event->type() == QEvent::FocusIn) {
        const Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
        kbd = reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason
              || reason == Qt::ShortcutFocusReason;
    }
    if (w->property("kbdFocus").toBool() != kbd) {
        w->setProperty("kbdFocus", kbd);
        w->style()->unpolish(w);
        w->style()->polish(w);
        w->update();
    }
}
