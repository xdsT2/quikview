#pragma once

#include <QGraphicsEffect>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QWidget>

#include <functional>

// 画布浮层共用外壳（派单 2026-11-14「Tab 信息与 PSD 图层浮层标准统一」）。
// 以信息浮层（MetadataPanel）2026-11-13 定稿为唯一基准，三件套供两块浮层共用：
//   HudBackdrop    底板：12px 圆角 + 1px 边界自绘。磨砂材质开（UiStyle::blurMaterial，
//                  设置页「磨砂材质（右键菜单与信息浮层）」）时对画布视口做区域渲染
//                  → 盒式模糊 ≈20px → 叠 #212121@91% 染色；关/采样不可用回退实底。
//                  采样在事件循环里异步做（源每次 Paint 只标记脏 + 120ms 节流），
//                  paintEvent 里绝不递归 render。
//   HudEffect      常驻特效：静息只画缓存投影（向下 5 / 模糊 18 / 强度随主题）；
//                  progress 0→1 开启（可带左上原点 0.99→1 缩放），关闭只动 progress。
//   HudToolButton  标题栏 28×28 热区按钮（复制 / 关闭）：自绘悬停底、按下加深与
//                  键盘焦点环；焦点环只在键盘导航（Tab/Backtab/Shortcut 原因）时
//                  出现，鼠标点击不取焦点（TabFocus 策略），画布焦点得以保留。
// 开合动效（110ms 开 / 85ms 关 / 系统减少动效直切）由 hudAnimateShow /
// hudAnimateDismiss 提供，两块浮层不得各写一套。

class HudBackdrop : public QWidget
{
    Q_OBJECT
public:
    explicit HudBackdrop(QWidget *parent = nullptr);

    void setSource(QWidget *src);
    void invalidate();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void resample();

    QTimer m_resampleTimer;
    QPointer<QWidget> m_source;
    QImage m_blurred;
    QPoint m_sampleOffset;
    bool m_dirty = false;
};

class HudEffect : public QGraphicsEffect
{
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ progress WRITE setProgress)
public:
    explicit HudEffect(QObject *parent = nullptr, qreal radius = -1.0);
    void setShadowOpacity(qreal opacity) { m_shadowOpacity = opacity; update(); }

    void setProgress(qreal v);
    qreal progress() const { return m_progress; }
    void setScaleOn(bool on) { m_scaleOn = on; }
    void invalidateShadow() { m_shadow = QImage(); update(); }

protected:
    QRectF boundingRectFor(const QRectF &r) const override;
    void draw(QPainter *painter) override;

private:
    void rebuildShadow(const QRectF &src, QPainter *painter);

    QImage m_shadow;
    qreal m_keyW = -1;
    qreal m_keyH = -1;
    qreal m_progress = 1.0;
    bool m_scaleOn = false;
    qreal m_pad = 0;
    qreal m_radius;
    qreal m_shadowOpacity = 1.0;
    int m_shadowAlpha = 0;
};

class HudToolButton : public QPushButton
{
    Q_OBJECT
public:
    enum Glyph { Close, Copy };

    HudToolButton(Glyph glyph, const QString &tip, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Glyph m_glyph;
};

// 系统「减少动效」检测（SPI_GETCLIENTAREAANIMATION）：开启时开合直切。
bool hudReduceMotion();

// 取消 panel 上还在跑的浮层动效（开/关共用一条通道，防交叉残留）。
void hudStopAnimations(QWidget *panel);

// 开启动效：110ms 透明度 0→1 + 左上原点 0.99→1 缩放；减少动效/无特效直切。
void hudAnimateShow(QWidget *panel, HudEffect *effect);

// 关闭动效：85ms 仅淡出无位移，结束后隐藏面板并把 progress 复位到 1；
// onDone 在每条完成路径（含直切）上回调，供浮层复位自身 closing 旗。
void hudAnimateDismiss(QWidget *panel, HudEffect *effect,
                       const std::function<void()> &onDone);
