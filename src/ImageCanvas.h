#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QMargins>
#include <QTimer>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QLabel;
class QVariantAnimation;
class QWheelEvent;
class QMouseEvent;
class QKeyEvent;

// 图片画布：缩放 + 平移 + 适配窗口 + 裁剪选区。M0 用 QGraphicsView 实现；
// 后续 M1 接自绘高性能渲染（自定义 QWidget + 分块纹理）再替换。
class ImageCanvas : public QGraphicsView
{
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // fitAfter=false 时不做适配，只装图（调用方随后自行 fit/setZoomMode），
    // 用于消除「setPixmap 内部 fit 一次 + setZoomMode 又 fit 一次」的双重计算。
    void setPixmap(const QPixmap &pixmap, bool fitAfter = true);
    // 「翻页效果」：从当前 pixmap 过渡到新 pixmap。mode 0 无 / 1 淡入淡出 /
    // 2 Alpha 滑动 / 3 滑动；时长固定 140ms。只在翻页路径调用，滑块跟手预览
    // 与平滑缩放不经过这里。
    void setPixmapAnimated(const QPixmap &pixmap, int mode, bool fitAfter = false);
    // 旋转/翻转等原地替换：保持当前缩放与视口中心（anchorNorm 为新图中要占据
    // 视口中心的归一化坐标），避免转完后图像跳位或被重新适配。
    void setPixmapPreservingView(const QPixmap &pixmap, const QPointF &anchorNorm,
                                 qreal zoom, bool fitWindow, bool fitWidth);
    // 视口中心落在当前图片内的归一化坐标（0..1），无图时返回 (0.5, 0.5)
    QPointF viewCenterNormalized() const;
    void clear();
    void setSmooth(bool on);
    // 插值算法："bilinear"(0) / "bicubic"(1) / "lanczos"(2)。bicubic 走 GPU 平滑，
    // lanczos 在缩小显示时按目标尺寸做一次高质量重采样（原版标注「慢，高品质」）。
    enum InterpMode { InterpBilinear = 0, InterpBicubic = 1, InterpLanczos = 2 };
    void setInterpolationMode(int mode);
    int interpolationMode() const { return m_interp; }
    // 原版「拉伸小图像」：关（默认）时小于窗口的图保持 100% 不放大
    void setStretchSmall(bool on);
    bool stretchSmall() const { return m_stretchSmall; }
    void setWheelZoomEnabled(bool on) { m_wheelZoom = on; }
    // 「图片大于窗口时显示图片顶部」（键 imageproc/align_top_when_large，默认开）：
    // 适合宽度 / 原始大小两种模式下图片高度超出视口时，开=滚到顶，关=垂直居中。
    // 适合窗口模式图片恒不超出视口，该开关在那条路径上无可观测差异。
    void setAlignTopWhenLarge(bool on) { m_alignTopWhenLarge = on; }
    void setMiddlePanEnabled(bool on) { m_middlePan = on; }
    // 双击最大化/还原开关（设置页「双击最大化 / 还原」）：开启时边缘单击翻页
    // 延迟 220ms 等第二击，收到双击就取消翻页只做最大化；关闭时边缘单击零延迟。
    void setDoubleClickMaximizeEnabled(bool on) { m_dblClickMaximize = on; }
    void setContentMargins(const QMargins &margins);

    // 画布外观（设置页「背景颜色 / 边框颜色 / 不显示侧边箭头」）。
    // setBackgroundColor 必须同时改 backgroundBrush 与 stylesheet：QGraphicsView
    // 的 stylesheet 优先级高于 brush，只改一处画布底色不会变。
    void setBackgroundColor(const QColor &color);
    // 「更改默认背景图片」：空图恢复纯色。图片按可见场景区拉伸铺满，画在
    // 图像下方、纯色之上；不进入 QGraphicsScene，不参与缩放/旋转。
    void setBackgroundImage(const QImage &image);
    // 视口 1px 内沿描边；color 无效（默认）时不画，保持原来的无边框观感。
    void setBorderColor(const QColor &color);
    // false：两支 SideArrow 一律不显示，但 edgeTurnDelta 的命中矩形保留，
    // 悬停两侧仍出鼠标手型、点击两侧仍翻页——只是没有箭头这个视觉提示。
    void setSideArrowsEnabled(bool on);
    bool sideArrowsEnabled() const { return m_sideArrowsEnabled; }

    void fitToWindow();
    void fitToWidth();                    // 浏览模式 2：按图宽缩放，垂直可滚
    void zoomIn();
    void zoomOut();
    void zoomActual();
    qreal zoom() const { return m_zoom; }
    void setZoom(qreal zoom);
    // 以 anchorViewportPt（视口坐标）为锚点应用缩放，进入手动模式且不触发 Fit。
    // 滚轮用鼠标位置，按钮/键盘/滑杆用视口中心。
    void applyZoomAt(qreal zoom, const QPoint &anchorViewportPt);
    bool isFitToWindow() const { return m_fitToWindow; }
    bool isFitToWidth() const { return m_fitWidth; }

    // ↑/↓（原版「上移/下移」）：按视口高度比例滚动，边界由滚动条自己钳制。
    // 画面小于视口（适应窗口）时没有可滚动范围，等于不动。
    void scrollVerticallyBy(qreal fraction);

    // 裁剪模式：进入后左键拖拽画选区，selectionRect() 返回图像像素坐标
    void setCropMode(bool on);

    // 按**当前真实光标位置**重算侧边箭头显隐/光标形状。换图、Resize、缩略图
    // 开合、全屏进出、裁剪切换、show/enter 时鼠标不动也要刷新，统一走这里。
    void refreshEdgeHover();
    bool cropMode() const { return m_cropMode; }
    QRect selectionRect() const;
    void clearSelection();

    // 空态风车 logo（原版画布中央灰色 4 菱形）
    void setLogoVisible(bool on);

signals:
    // 任何缩放来源（滚轮/按钮/适配模式）变更后发出，供控制栏滑条与浮标联动
    void zoomChanged(qreal zoom);
    // 画布两侧 40px 箭头带内「点击」（位移 <3px、按压 <300ms）翻页：
    // -1 = 上一张 / +1 = 下一张。首尾行为由 MainWindow 走 onPrev/onNext
    // 复用 config/edge_action，不在这里另写一套。
    void edgeTurnRequested(int delta);
    // 中键「点击」（位移 <3px、按压 <300ms，与左侧箭头带同一套阈值）：
    // 请求切换全屏/还原。按住拖动仍是平移，不走这个信号。
    void middleClickRequested();
    // 左键双击（开启双击最大化时）：请求窗口最大化/还原。边缘双击会先取消
    // 220ms 内挂起的单击翻页（见 mouseReleaseEvent / mouseDoubleClickEvent）。
    void doubleClickRequested();

protected:
    // 视口 1px 内沿边框：基类画完图像后直接描一圈，与 scene 缩放/平移无关
    void paintEvent(QPaintEvent *event) override;
    // 背景图（设置页「更改默认背景图片」）画在纯色之上、图像之下
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    // 翻页过渡画在场景最上层而不是 paintEvent 里另起 QPainter：后者与
    // QWidget::grab 的渲染画笔画到同一 viewport 会冲突（截图抓不到过渡）。
    void drawForeground(QPainter *painter, const QRectF &rect) override;
    void wheelEvent(QWheelEvent *event) override;
    // 画布不消费导航键：QGraphicsView/QAbstractScrollArea 默认会把方向键吃掉做滚动，
    // 事件就传不到 MainWindow::keyPressEvent。这里统一 ignore() 冒泡。
    // 注意不能用 setFocusPolicy(NoFocus) 一刀切——裁剪模式还要靠画布收 Esc。
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    // 把场景点重新对回指定的视口点（用于边距/缩放变化后还原缩放锚点）
    void keepScenePointAt(const QPointF &scenePt, const QPoint &viewportPt);
    // Lanczos：缩小显示时按目标尺寸重采样一次，用 devicePixelRatio 保持逻辑尺寸
    // 不变（scene 几何、fit/center/归一化锚点全都不受影响），只提高采样质量。
    // refreshSampling() 带交互态闸门：拖窗口/连续缩放时只记账不计算，
    // 停止 150ms 后由 m_sampleFlush 调 doRefreshSampling() 补一次。
    void refreshSampling();
    void doRefreshSampling();
    // fit/fitWidth 的纯几何部分（resetTransform+scale+flags+centerOn+emit），
    // 与 fitToWindow()/fitToWidth() 共用；resizeEvent 拖动期只调这里不算采样。
    void fitToWindowGeom();
    void fitToWidthGeom();
    // 「图片大于窗口时显示图片顶部」：高于视口时滚到顶（fit / 100% 适配路径共用）
    void applyAlignTopIfLarge();
    // 侧边翻页箭头带：命中判定 + 悬停光标/箭头显隐。pos 为视口坐标，
    // 与图片缩放无关；左带被缩略图面板压缩时随视口边距自动右移。
    int edgeTurnDelta(const QPoint &pos) const;
    void updateEdgeHover(const QPoint &pos);
    void placeEdgeArrows();

    QGraphicsPixmapItem *m_item = nullptr;
    QImage m_backgroundImage;   // 空 = 只用纯色背景
    // 翻页过渡：m_transPixmap 是上一张的屏幕位图，进度 0→1 画在视图最上层
    QPixmap m_transPixmap;
    QRectF m_transSceneRect;
    qreal m_transProgress = 1.0;
    int m_transMode = 0;
    QVariantAnimation *m_transAnim = nullptr;
    QGraphicsRectItem *m_rubber = nullptr;
    QLabel *m_logo = nullptr;
    QPoint m_lastPan;
    QPointF m_origin;
    bool m_panning = false;
    bool m_selecting = false;
    bool m_fitToWindow = true;
    bool m_fitWidth = false;
    bool m_cropMode = false;
    bool m_smooth = true;
    bool m_wheelZoom = true;
    bool m_middlePan = true;
    bool m_stretchSmall = false;
    bool m_alignTopWhenLarge = true;   // 图片大于窗口时显示图片顶部（默认开）
    bool m_resizeBurst = false;  // 窗口边界拖动进行中（resizeEvent 置位，收尾定时器复位）
    bool m_sideArrowsEnabled = true;   // 侧边箭头开关（关=不显示但仍可点）
    QColor m_backgroundColor;    // 画布底色（brush 与 stylesheet 同步维护）
    QColor m_borderColor;        // 视口内沿描边色，无效值=不画
    int m_interp = InterpBicubic;
    QPixmap m_sourcePixmap;      // 未经 Lanczos 预处理的原图，缩放变化时按它重算
    QSize m_samplingSize;        // 上次 Lanczos 重采样的目标像素尺寸（相同就跳过重算）
    qreal m_zoom = 1.0;
    // 交互收尾定时器：拖动/连续缩放停止 150ms 后补做一次重采样（单发）
    QTimer m_sampleFlush;
    QElapsedTimer m_lastSampleReq;   // 上一次 refreshSampling 请求时刻（判连续交互）
    // 左键按下记账：松手时「位移 <3px 且 <300ms 且在箭头带内」才算点击翻页，
    // 其余仍是 ScrollHandDrag 拖拽平移
    QElapsedTimer m_pressClock;
    QPoint m_pressPos;
    bool m_pressTracked = false;
    // 中键按下记账：与左键那套分开，互不干扰。松手时「位移 <3px 且 <300ms」
    // 判定为单击（切全屏），否则算拖动平移。m_item 为空 / 裁剪模式不记。
    QElapsedTimer m_midPressClock;
    QPoint m_midPressPos;
    bool m_midPressTracked = false;
    bool m_dblClickMaximize = false;    // 双击最大化开关（applyRuntimeSettings 下发）
    // 双击最大化开启时，边缘单击先挂起的翻页方向；收到双击/超时执行后清零。
    // 非 0 表示有一笔「等第二击」的翻页在途（220ms 后兑现或取消）。
    int m_pendingEdgeTurn = 0;
    class SideArrow;
    SideArrow *m_arrowPrev = nullptr;   // 悬停带内 160ms 淡入，纯视觉不接鼠标
    SideArrow *m_arrowNext = nullptr;
};
