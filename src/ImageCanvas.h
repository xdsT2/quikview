#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QImage>
#include <QMargins>
#include <QString>
#include <QThreadPool>
#include <QTimer>

#include "ZoomMotion.h"

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
    // 原地换图：animated=true 且帧尺寸不变时走快路径——只更新源与 item 位图，
    // 不重建 scene、不动滚动条/光标/fit 标志、不发 zoomChanged、不投异步重采样，
    // 裁剪选区与叠加层状态原样保留；尺寸不一致或 animated=false 时回退保视图换图
    // （锚点=视口中心，fit 标志与当前缩放保持现状）。
    void replacePixmapInPlace(const QPixmap &pixmap, bool animated = false);
    QPointF viewCenterNormalized() const;
    void clear();
    void setSmooth(bool on);
    // 插值算法："bilinear"(0) / "bicubic"(1) / "lanczos"(2)。bicubic 走 GPU 平滑，
    // lanczos 在缩小显示时按目标尺寸做一次高质量重采样（原版标注「慢，高品质」）。
    enum InterpMode { InterpBilinear = 0, InterpBicubic = 1, InterpLanczos = 2 };
    void setInterpolationMode(int mode);
    int interpolationMode() const { return m_interp; }
    // 「使用强锐化滤镜」（键 imageproc/strong_sharpen，默认关）：Lanczos 且缩小到
    // 100% 以下时走锐化核。切换点由 MainWindow::applyLiveSetting 即时下发：作废
    // 采样记账并立刻按新值重算一次，不再等下一次开图（批次04 #20）。
    void setStrongSharpen(bool on);
    bool strongSharpen() const { return m_strongSharpen; }
    // 原版「拉伸小图像」：关（默认）时小于窗口的图保持 100% 不放大
    void setStretchSmall(bool on);
    bool stretchSmall() const { return m_stretchSmall; }
    // 滚轮上下 / 中键单击与拖动已独立映射（翻页批次 2）：画布在事件里现读
    // Config::wheelUpAction / wheelDownAction / middleClickAction / middleDragAction，
    // 设置改动下次触发即生效，不再需要运行态 setter。

    // 显示器 ICC（SDR 验证版，翻页批次 2）：开关/跨屏后按当前屏幕 profile
    // 重转当前画面（源 pixmap 保持 sRGB，只换显示项）。
    void reapplyMonitorIcc();
    // 「图片大于窗口时显示图片顶部」（键 imageproc/align_top_when_large，默认开）：
    // 适合宽度 / 原始大小两种模式下图片高度超出视口时，开=滚到顶，关=垂直居中。
    // 适合窗口模式图片恒不超出视口，该开关在那条路径上无可观测差异。
    void setAlignTopWhenLarge(bool on) { m_alignTopWhenLarge = on; }
    bool alignTopWhenLarge() const { return m_alignTopWhenLarge; }
    // 设置页切换点（批次04 #23）：改值并当场重排当前画面——开＝大图滚到顶，
    // 关＝大图垂直居中；没有图或图不高于视口时只记住新值，下一次适配按它走。
    void applyAlignTopWhenLargeLive(bool on);
    // 双击最大化/还原开关（设置页「双击最大化 / 还原」）：开启时边缘单击翻页
    // 延迟 220ms 等第二击，收到双击就取消翻页只做最大化；关闭时边缘单击零延迟。
    void setDoubleClickMaximizeEnabled(bool on) { m_dblClickMaximize = on; }
    void setContentMargins(const QMargins &margins);

    // 画布外观（设置页「背景颜色 / 边框颜色 / 不显示侧边箭头」）。
    // setBackgroundColor 必须同时改 backgroundBrush 与 stylesheet：QGraphicsView
    // 的 stylesheet 优先级高于 brush，只改一处画布底色不会变。
    // —— 第 18 期起暂无 UI 入口（下线），能力保留 ——
    void setBackgroundColor(const QColor &color);
    // —— UI-001 §3.8 全画布背景（M-05）：棋盘/纯色二选一，颜色随主题 ——
    // kind 取 UiStyle::canvas::Backdrop；铺满整个视口（绘制层先画背景再画
    // 图片），棋盘 tile 固定在视口逻辑坐标，不随缩放/平移变化。同值重调
    // 也会按当前主题重取令牌色并重绘（主题切换后 MainWindow 调它刷新）。
    void setCanvasBackdrop(int kind);
    int canvasBackdrop() const { return m_backdrop; }
    // 透明图像阴影（UI-001 §3.8.3）：开关默认关，仅对带 Alpha 的 32 位透明
    // 图片生效（eligible 由 MainWindow 按当前图是否为 GIF 判定，GIF 恒不画）。
    // 阴影按图片最外层矩形（场景外接框）在图片下方绘制，主题令牌决定颜色
    // 与模糊半径；只属于屏幕预览层，导出链路不走画布绘制，天然隔离。
    void setImageShadowEnabled(bool on);
    void setImageShadowEligible(bool eligible);
    // 「显示透明区域」（UI-001 §3.8 返工三控件之二）：开启时纯色模式下在
    // 图片矩形内为透明像素铺指示棋盘；关闭时透明像素直接透出全画布背景。
    // 棋盘模式下全画布棋盘本身连续透出，该开关视觉上无差异（与果核一致）。
    void setShowTransparency(bool on);
    // 空图恢复纯色。图片按可见场景区拉伸铺满，画在图像下方、纯色之上；
    // 不进入 QGraphicsScene，不参与缩放/旋转。暂无 UI 入口（第 18 期下线），能力保留。
    void setBackgroundImage(const QImage &image);
    // 视口 1px 内沿描边；color 无效（默认）时不画，保持原来的无边框观感。
    // 暂无 UI 入口（第 18 期下线），能力保留。
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
    qreal targetZoom() const { return m_zoomMotion.active ? qreal(m_zoomMotion.target) : m_zoom; }
    bool isZoomAnimating() const { return m_zoomMotion.active; }
    void setZoom(qreal zoom);
    // 以 anchorViewportPt（视口坐标）为锚点应用缩放，进入手动模式且不触发 Fit。
    // 滚轮用鼠标位置，按钮/键盘/滑杆用视口中心。
    void applyZoomAt(qreal zoom, const QPoint &anchorViewportPt);
    bool isFitToWindow() const { return m_fitToWindow; }
    bool isFitToWidth() const { return m_fitWidth; }

    // ↑/↓（原版「上移/下移」）：按视口高度比例滚动，边界由滚动条自己钳制。
    // 画面小于视口（适应窗口）时没有可滚动范围，等于不动。
    void scrollVerticallyBy(qreal fraction);

    // ←/→ 同语义的水平版本（「仅方向键滚动」模式用）：按视口宽度比例滚动，
    // 边界同样交给滚动条钳制；无可滚动行程时不动。
    void scrollHorizontallyBy(qreal fraction);

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
    // 派发 config/mouse/btn_middle_click 映射的动作（默认 fullscreen）。
    // 按住拖动由 btn_middle_drag 独立控制（默认 pan），不走点击派发。
    // 左键双击（开启双击最大化时）：请求窗口最大化/还原。边缘双击会先取消
    // 220ms 内挂起的单击翻页（见 mouseReleaseEvent / mouseDoubleClickEvent）。
    void doubleClickRequested();
    // 鼠标按键映射（C9~C12）：右键 / XButton1 / XButton2 在按下时派发，
    // 左键在松手且判定为「点击」时派发（拖动平移不受影响）。
    // action 取 SettingsDialog 的动作表（next / prev / zoom_in / ...）。
    void mouseActionRequested(const QString &action);
    // 画布平移（左键拖动 / 中键拖动）起止：底部控制岛据此弱化，不隐藏。
    void panStateChanged(bool panning);

protected:
    // 视口 1px 内沿边框：基类画完图像后直接描一圈，与 scene 缩放/平移无关
    void paintEvent(QPaintEvent *event) override;
    // 背景图（设置页「更改默认背景图片」）画在纯色之上、图像之下
    void drawBackground(QPainter *painter, const QRectF &rect) override;
    // §3.8 棋盘 tile（按 DPR 预渲染）与透明图像阴影缓存（低分辨率预模糊）
    void rebuildCheckerTile(bool indicator = false);
    void rebuildShadowCache(const QRectF &projected, bool light);
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
    void applyZoomAtInternal(qreal zoom, const QPoint &anchorViewportPt, bool deferSampling);
    void animateZoomBy(qreal factor, const QPoint &anchorViewportPt);
    void stopZoomAnimation();
    void stepZoomAnimation();
    // Lanczos：缩小显示时按目标尺寸重采样一次，用 devicePixelRatio 保持逻辑尺寸
    // 不变（scene 几何、fit/center/归一化锚点全都不受影响），只提高采样质量。
    // 重采样本身跑在专用单线程池里（见 SamplingTask），GUI 线程只投任务和应用结果；
    // refreshSampling() 只对「拖窗口」保留 150ms 收尾闸门。
    void refreshSampling();
    void doRefreshSampling();
    // 投后台任务 / 校验并应用回来结果 / 让已上屏与在飞结果一起作废。
    void startPendingSample();
    void applySampleResult(quint64 generation, QImage result, const QSize &target,
                           qint64 sourceKey, int interp, bool sharp);
    void invalidateSampling();
    // 等后台结果期间顶画面的廉价帧（长边 1600 的快速缩小版，按源图缓存）。
    QPixmap samplingPreview();
    class SamplingTask;
    // fit/fitWidth 的纯几何部分（resetTransform+scale+flags+centerOn+emit），
    // 与 fitToWindow()/fitToWidth() 共用；resizeEvent 拖动期只调这里不算采样。
    void fitToWindowGeom();
    void fitToWidthGeom();
    // 场景矩形收口：图片矩形四周各加 2 倍视口 padding，fit 态也有滚动行程
    // → 打开即可拖、可拖出边界。m_item 始终保持 (0,0) 起，pad 只在 sceneRect。
    void updateSceneRect();
    // 「图片大于窗口时显示图片顶部」：高于视口时滚到顶（fit / 100% 适配路径共用）
    void applyAlignTopIfLarge();
    // 长图轴锁（D2）：当前图满足「高/宽 ≥ 2.5 且纵向溢出视口」时返回 true。
    // 尺寸取 pixmap 的 deviceIndependentSize，旋转已在 MainWindow 落到 pixmap 上，
    // 所以 90° 后高宽互换，这里判的永远是当前显示方向；全览（纵向不溢出）即失效。
    bool longStripLock() const;
    // 把横向滚动条钉在图片水平中心：padding 对称，滚动条中点即图像中心。
    void pinHorizontalCenter();
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
    bool m_stretchSmall = false;
    bool m_alignTopWhenLarge = true;   // 图片大于窗口时显示图片顶部（默认开）
    bool m_strongSharpen = false;      // 使用强锐化滤镜（Lanczos 缩小重采样时生效）
    bool m_resizeBurst = false;  // 窗口边界拖动进行中（resizeEvent 置位，收尾定时器复位）
    bool m_sideArrowsEnabled = true;   // 侧边箭头开关（关=不显示但仍可点）
    QColor m_backgroundColor;    // 画布底色（brush 与 stylesheet 同步维护）
    QColor m_borderColor;        // 视口内沿描边色，无效值=不画
    // UI-001 §3.8：全画布背景材质 + 透明图像阴影（颜色都从 UiStyle::canvas::
    // 按当前主题现取，不缓存 QColor，主题切换后重绘即生效）
    int m_backdrop = 0;                 // UiStyle::canvas::Backdrop（0=棋盘 1=纯色）
    bool m_showTransparency = true;     // 「显示透明区域」开关（默认开）
    bool m_shadowOn = false;            // 「图片阴影」开关（默认关）
    bool m_shadowEligible = false;      // 当前图非 GIF（MainWindow 按路径判定）
    QPixmap m_checkerTile;              // 棋盘 tile（按 DPR 预渲染，保证边缘清晰）
    qreal m_tileDpr = 0.0;              // tile 对应的 devicePixelRatio（变了就重铺）
    QImage m_shadowCache;               // 预模糊的阴影图（外扩 margin，含主题 alpha）
    QRectF m_shadowCacheRect;           // 缓存对应的场景外接框
    bool m_shadowCacheLight = false;    // 缓存生成时的主题（变了就重建）
    int m_interp = InterpBicubic;
    QPixmap m_sourcePixmap;      // 未经 Lanczos 预处理的原图，缩放变化时按它重算
    // 显示器 ICC SDR 验证版：sRGB 显示图 → 当前屏幕 profile（ACM 开启的
    // 屏幕跳过，避免与系统合成转换叠加）。非 Windows / 开关关 = 原样返回。
    QPixmap toMonitorSpace(const QPixmap &src) const;
    QImage m_sourceImage;        // 与 m_sourcePixmap 同源（隐式共享），后台线程只读重采样
    QPixmap m_previewPixmap;     // 廉价预览帧：等采样结果时顶画面，避免重绘几十 MP 原图
    qint64 m_previewSourceKey = 0;   // 预览帧对应的原图 cacheKey（换图就重建）
    QSize m_samplingSize;        // 已上屏结果的目标像素尺寸（相同就跳过重算）
    qint64 m_samplingSourceKey = 0;  // 已上屏结果对应的原图 cacheKey
    int m_samplingInterp = -1;       // 已上屏结果对应的插值档
    bool m_samplingSharp = false;    // 已上屏结果对应的强锐化态
    quint64 m_sampleGeneration = 0;  // 每次启动/失效自增，旧任务凭它作废
    bool m_sampleRunning = false;    // 线程池里是否有任务在跑（单飞）
    QThreadPool m_samplePool;        // 专用单线程池：重采样不占 GUI 线程，也不抢全局池
    qreal m_zoom = 1.0;
    // 平滑缩放动画（D1）：log-space 匀速叠加引擎 + 60Hz 定时器；拖动/切图/重置时即停。
    ZoomMotion::State m_zoomMotion;
    QTimer m_zoomTimer;
    QElapsedTimer m_zoomClock;
    QPointF m_zoomAnchorScene;
    QPoint m_zoomAnchorWindow;
    // 长图首次装图时的一次性意图：由 fitToWindowGeom 消费成一次 fitToWidth，
    // 让条漫从「适合宽度」起读。用户随后主动选「适合窗口」不会再被改写。
    bool m_pendingLongStripFitWidth = false;
    // 交互收尾定时器：拖动连续缩放停止 150ms 后补做一次重采样（单发）
    QTimer m_sampleFlush;
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
    bool m_panSignalActive = false;   // 已发 panStateChanged(true)，保证成对发出 false
    bool m_dblClickMaximize = false;    // 双击最大化开关（applyRuntimeSettings 下发）
    // 中央单击「待判」：当左键映射为翻页类动作且双击最大化开启时，中央单击不立即派发，
    // 暂存动作并等系统双击间隔；若间隔内收到中央双击则取消待判改走最大化，避免一次双击
    // 既翻一页又最大化。侧边热区与「双击最大化关闭」时仍即时派发，不延迟。
    QTimer m_pendingActionTimer;
    QString m_pendingAction;
    class SideArrow;
    SideArrow *m_arrowPrev = nullptr;   // 悬停带内 160ms 淡入，纯视觉不接鼠标
    SideArrow *m_arrowNext = nullptr;
};
