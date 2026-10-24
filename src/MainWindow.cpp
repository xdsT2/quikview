#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPropertyAnimation>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTextStream>
#include <QTimer>
#include <QTransform>
#include <QUrl>
#include <QWheelEvent>

#include "BatchConvertDialog.h"
#include "Chrome.h"
#include "Config.h"
#include "DirectoryLoader.h"
#include "FrameNavigator.h"
#include "HeifLoader.h"
#include "ImageCache.h"
#include "ImageCanvas.h"
#include "ImageEffects.h"
#include "MetadataPanel.h"
#include "RawImageLoader.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "ThumbnailBrowser.h"
#include "Wallpaper.h"

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
namespace {
inline short xFromLparam(long v) { return static_cast<short>(LOWORD(v)); }
inline short yFromLparam(long v) { return static_cast<short>(HIWORD(v)); }
}
#endif

namespace {
constexpr int kBrowserWidth = 260;   // 缩略图栏宽
constexpr int kBrowserGap = 8;       // 与窗口左缘/画布之间的留白
constexpr int kTopChromeH = 76;      // 标题栏 28 + 命令栏 34 + 视觉留白
constexpr int kDockInset = 84;       // 底部胶囊占位
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Honeyview 无边框铬件窗口
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("QuikView"));
    resize(1080, 815);
    setMinimumSize(720, 500);
    setAcceptDrops(true);
    setMouseTracking(true);

#ifdef Q_OS_WIN
    // Windows 11 原生窗口圆角（DWMWA_WINDOW_CORNER_PREFERENCE=33, ROUND=2）。
    // Win10 上该属性调用失败静默忽略，窗口保持直角（原版行为）。
    {
        HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
        if (dwm) {
            using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
            auto fn = reinterpret_cast<DwmSetWindowAttributeFn>(
                GetProcAddress(dwm, "DwmSetWindowAttribute"));
            if (fn) {
                const int pref = 2;   // DWMWCP_ROUND
                fn(reinterpret_cast<HWND>(winId()), 33, &pref, sizeof(pref));
            }
        }
    }
    // 光有 DWMWCP_ROUND 只是把轮廓削圆——纯 WS_POPUP 无边框窗口没有 DWM 的
    // 1px 系统描边和投影，摆到原生窗口旁边就是"光秃秃切了一刀"。把
    // WS_THICKFRAME|WS_CAPTION 补回原生样式（Qt 层仍保持 FramelessWindowHint，
    // 两边各按各的理解互不打架），配合 WM_NCCALCSIZE 返回 0 把非客户区清零，
    // DWM 就会照常画描边/投影/圆角——观感与原生窗口完全一致。
    applyNativeFrameStyles();
#endif

    // —— 引擎 ——
    m_canvas = new ImageCanvas(this);
    m_cache = new ImageCache(this);
    m_dir = new DirectoryLoader;
    // 启动就套用「循环」设置：此前只在菜单里切换时才生效，保存过的值重启后等于没读
    m_dir->setEdgeAction(Config::instance().edgeAction());
    m_frames = new FrameNavigator;

    m_metadata = new MetadataPanel(this);
    m_metadata->hide();                     // 浮动面板，菜单开关

    m_browser = new ThumbnailBrowser(m_cache, this);
    m_browser->hide();
    connect(m_browser, &ThumbnailBrowser::activateImage, this, &MainWindow::onActivateImage);

    m_slideshowTimer = new QTimer(this);
    m_slideshowTimer->setInterval(Config::instance().slideshowIntervalMs());
    connect(m_slideshowTimer, &QTimer::timeout, this, &MainWindow::onSlideshowTick);
    m_slideshowRandom = Config::instance().slideshowRandom();

    connect(m_cache, &ImageCache::fullReady, this, &MainWindow::onFullReady);
    connect(m_cache, &ImageCache::loadFailed, this, &MainWindow::onLoadFailed);

    // —— 铬件 ——
    m_caption = new Chrome::CaptionBar(this);
    m_caption->setTitle(tr("未打开图片"));
    connect(m_caption, &Chrome::CaptionBar::fullscreenClicked, this, [this]() {
        isFullScreen() ? showNormal() : showFullScreen();
    });
    connect(m_caption, &Chrome::CaptionBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_caption, &Chrome::CaptionBar::maximizeClicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_caption, &Chrome::CaptionBar::closeClicked, this, &QWidget::close);
    connect(m_caption, &Chrome::CaptionBar::systemMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSystemMenu();
        menu->exec(g);
        delete menu;
    });

    m_toolbar = new Chrome::ToolBar(this);
    connect(m_toolbar, &Chrome::ToolBar::openClicked, this, &MainWindow::onOpen);
    connect(m_toolbar, &Chrome::ToolBar::browseMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildBrowseMenu();
        menu->exec(g);
        delete menu;
    });
    connect(m_toolbar, &Chrome::ToolBar::slideshowMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSlideshowMenu();
        menu->exec(g);
        delete menu;
    });
    connect(m_toolbar, &Chrome::ToolBar::bookmarkMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildBookmarkMenu();
        menu->exec(g);
        delete menu;
    });
    connect(m_toolbar, &Chrome::ToolBar::editMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildEditMenu();
        menu->exec(g);
        delete menu;
    });
    connect(m_toolbar, &Chrome::ToolBar::copytoMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildCopytoMenu();
        menu->exec(g);
        delete menu;
    });
    connect(m_toolbar, &Chrome::ToolBar::lockToggled, this, &MainWindow::toggleTopLock);
    connect(m_toolbar, &Chrome::ToolBar::moreMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSystemMenu();
        menu->exec(g);
        delete menu;
    });

    m_control = new Chrome::ControlBar(this);
    connect(m_control, &Chrome::ControlBar::openClicked, this, &MainWindow::onOpen);
    connect(m_control, &Chrome::ControlBar::listClicked, this, &MainWindow::toggleBrowserPanel);
    connect(m_control, &Chrome::ControlBar::zoomInClicked, this, &MainWindow::onZoomIn);
    connect(m_control, &Chrome::ControlBar::zoomOutClicked, this, &MainWindow::onZoomOut);
    connect(m_control, &Chrome::ControlBar::fitClicked, this, &MainWindow::onFit);
    connect(m_control, &Chrome::ControlBar::rotateLeftClicked, this, &MainWindow::onRotateLeft);
    connect(m_control, &Chrome::ControlBar::rotateRightClicked, this, &MainWindow::onRotateRight);
    connect(m_control, &Chrome::ControlBar::lockToggled, this, &MainWindow::toggleDockLock);
    connect(m_control, &Chrome::ControlBar::prevClicked, this, &MainWindow::onPrev);
    connect(m_control, &Chrome::ControlBar::nextClicked, this, &MainWindow::onNext);
    connect(m_control, &Chrome::ControlBar::prev10Clicked, this, &MainWindow::onPrev10);
    connect(m_control, &Chrome::ControlBar::next10Clicked, this, &MainWindow::onNext10);
    connect(m_control, &Chrome::ControlBar::prevFolderClicked, this, &MainWindow::onPrevFolder);
    connect(m_control, &Chrome::ControlBar::nextFolderClicked, this, &MainWindow::onNextFolder);
    connect(m_control, &Chrome::ControlBar::zoomRequested, this, [this](int pct) {
        m_canvas->setZoom(pct / 100.0);
    });

    // 画布缩放（滚轮/按钮/滑条/适配模式）统一联动：控制栏滑条 + 左下角浮标 + 浏览方式图标降级
    connect(m_canvas, &ImageCanvas::zoomChanged, this, [this](qreal z) {
        m_control->setZoomPercent(qRound(z * 100));
        if (m_zoomBadge && !m_currentImage.isNull()) {
            QString txt;
            if (m_canvas->isFitToWindow()) {
                txt = tr("适合窗口");
            } else if (m_canvas->isFitToWidth()) {
                txt = tr("适合宽度");
            } else {
                txt = QStringLiteral("%1%").arg(qRound(z * 100));
            }
            m_zoomBadge->showText(txt);
        }
        if (!m_canvas->isFitToWindow() && !m_canvas->isFitToWidth() && m_viewMode != 0) {
            m_viewMode = 0;
            if (actActualSize) {
                actActualSize->setChecked(true);
            }
            if (m_toolbar) {
                m_toolbar->setShowModeIcon(0);
            }
        }
        // 缩放模式变了就重算内容边距：进入手动缩放时上下安全区归零，
        // 回到「适合窗口」时再让出来。
        updateCanvasInsets();
    });

    m_toast = new Chrome::Toast(this);
    m_zoomBadge = new Chrome::ZoomBadge(this);
    m_statusOverlay = new Chrome::StatusOverlay(this);
    m_emptyState = new Chrome::EmptyState(this);
    connect(m_emptyState, &Chrome::EmptyState::openClicked, this, &MainWindow::onOpen);
    connect(m_emptyState, &Chrome::EmptyState::folderClicked, this, &MainWindow::onOpenFolder);

    // 悬浮铬件透明度：顶部两条同步淡入淡出，底部胶囊独立。
    auto setupFade = [this](QWidget *widget, QGraphicsOpacityEffect *&effect,
                            QPropertyAnimation *&animation) {
        effect = new QGraphicsOpacityEffect(widget);
        effect->setOpacity(1.0);
        widget->setGraphicsEffect(effect);
        animation = new QPropertyAnimation(effect, "opacity", this);
        animation->setDuration(180);
        animation->setEasingCurve(QEasingCurve::OutCubic);
    };
    setupFade(m_caption, m_captionOpacity, m_captionFade);
    setupFade(m_toolbar, m_toolbarOpacity, m_toolbarFade);
    setupFade(m_control, m_dockOpacity, m_dockFade);

    m_chromeTimer = new QTimer(this);
    m_chromeTimer->setInterval(60);
    connect(m_chromeTimer, &QTimer::timeout, this, &MainWindow::updateChromeProximity);

    createActions();
    applyRuntimeSettings();
    applyChromeLayout();
    updateCanvasLogo();
    m_chromeTimer->start();
}

MainWindow::~MainWindow()
{
    delete m_frames;
}

// ---------------------------------------------------------------------------
// 布局：1px 外框；顶部铬件（28+34）悬浮覆盖画布；胶囊 440x72 浮于下缘 19px。
// 画布始终占满客户区，只通过 viewport margin 给图片留出视觉呼吸区，避免
// 顶部栏显隐时图片被重新排版（原型中的“卡一下”）。
// ---------------------------------------------------------------------------

void MainWindow::applyChromeLayout()
{
    if (!m_canvas || !m_caption) {
        return;
    }
    const bool full = isFullScreen();
    const int fw = full ? 0 : 1;
    const int x = fw;
    const int y = fw;
    const int w = width() - 2 * fw;
    const int h = height() - 2 * fw;

    m_caption->setGeometry(x, y, w, 28);
    m_toolbar->setGeometry(x, y + 28, w, 34);
    m_canvas->setGeometry(x, y, w, h);
    updateCanvasInsets();

    if (m_emptyState) {
        m_emptyState->setGeometry(x, y, w, h);
        if (m_emptyState->isVisible()) {
            m_emptyState->raise();
        }
    }
    if (m_statusOverlay) {
        m_statusOverlay->setGeometry(x, y, w, h);
        m_statusOverlay->raise();
    }
    if (m_control) {
        m_control->move(x + (w - m_control->width()) / 2,
                        y + h - m_control->height() - 19);
        m_control->raise();
    }
    if (m_toast) {
        m_toast->setGeometry(x, y + 76, w, 56);
        m_toast->raise();
    }
    if (m_zoomBadge) {
        m_zoomBadge->move(x + 12, y + h - m_zoomBadge->height() - 34);
        m_zoomBadge->raise();
    }
    if (m_browser && m_browser->isVisible()) {
        m_browser->setGeometry(x + kBrowserGap, y + 76, kBrowserWidth,
                               h - 76 - kDockInset);
        m_browser->raise();
    }
    // 图片信息：右侧内缩浮层（原型 344 宽、上下左右各留 14）
    if (m_metadata && m_metadata->isVisible()) {
        m_metadata->setGeometry(x + w - UiStyle::InfoPanelW - 14, y + 14,
                                UiStyle::InfoPanelW, h - 28);
        m_metadata->raise();
    }

    m_caption->raise();
    m_toolbar->raise();
}

// 画布可视区：顶部悬浮铬件、底部胶囊各留一段呼吸区；缩略图栏展开时再把左侧
// 让出来——图片在「真正看得见的那块区域」里居中，既不会被面板压住一半，拖动
// 和缩放也围绕这块区域进行。
// 画布可视区：缩略图栏展开时让出左侧。
// 上下安全区**只服务于「适合窗口」**——手动缩放 / 原始大小 / 适合宽度一律贴满整窗，
// 悬浮铬件改用覆盖式（允许压在图片上）。否则手动放大后上下会留两条 76/84px 的
// 死黑边，图片永远用不到完整窗口高度。
// 注意：这里刻意不看铬件显隐状态，显隐只影响透明度/位移，绝不能反过来改画布几何。
void MainWindow::updateCanvasInsets()
{
    if (!m_canvas) {
        return;
    }
    const bool full = isFullScreen();
    int left = 0;
    if (!full && m_browser && m_browser->isVisible()) {
        left = kBrowserWidth + 2 * kBrowserGap;
    }
    const bool safeArea = !full && m_canvas->isFitToWindow();
    const QMargins margins(left,
                           safeArea ? kTopChromeH : 0,
                           0,
                           safeArea ? kDockInset : 0);
    if (margins == m_canvasInsets) {
        return;
    }
    m_canvasInsets = margins;
    m_canvas->setContentMargins(margins);
}

void MainWindow::toggleBrowserPanel()
{
    if (!m_browser) {
        return;
    }
    m_browser->setVisible(!m_browser->isVisible());
    if (m_browser->isVisible()) {
        // show 后顶层 layout 会把尺寸拉回 sizeHint，须在显示后重设几何
        const int fw = isFullScreen() ? 0 : 1;
        m_browser->setGeometry(fw + kBrowserGap, fw + 76, kBrowserWidth,
                               height() - 2 * fw - 76 - kDockInset);
        m_browser->raise();
    }
    updateCanvasInsets();
}

void MainWindow::setTopChromeVisible(bool visible, bool animate)
{
    if (!m_caption || !m_toolbar) {
        return;
    }
    m_topVisible = visible;
    auto apply = [&](QWidget *widget, QGraphicsOpacityEffect *effect,
                     QPropertyAnimation *animation) {
        if (!widget || !effect || !animation) {
            return;
        }
        widget->setAttribute(Qt::WA_TransparentForMouseEvents, !visible);
        // 状态（或正在进行的同一目标动画）已经到位就不要再 show/raise/重启动画：
        // 60ms 一次的热区轮询会把它变成持续重绘，点按钮时表现为整条栏闪一下。
        const bool fading = animation->state() == QAbstractAnimation::Running
                            && qFuzzyCompare(animation->endValue().toDouble() + 1.0,
                                             (visible ? 1.0 : 0.0) + 1.0);
        if (widget->isVisible() == visible
            && (fading || qFuzzyCompare(effect->opacity() + 1.0, (visible ? 1.0 : 0.0) + 1.0))) {
            return;
        }
        if (visible) {
            widget->show();
            widget->raise();
        }
        animation->stop();
        animation->setStartValue(effect->opacity());
        animation->setEndValue(visible ? 1.0 : 0.0);
        if (animate && !qFuzzyCompare(effect->opacity(), visible ? 1.0 : 0.0)) {
            animation->start();
        } else {
            effect->setOpacity(visible ? 1.0 : 0.0);
        }
    };
    apply(m_caption, m_captionOpacity, m_captionFade);
    apply(m_toolbar, m_toolbarOpacity, m_toolbarFade);
}

void MainWindow::setDockVisible(bool visible, bool animate)
{
    if (!m_control) {
        return;
    }
    if (m_emptyState && m_emptyState->isVisible()) {
        visible = false;
    }
    m_dockVisible = visible;
    m_control->setAttribute(Qt::WA_TransparentForMouseEvents, !visible);
    if (!m_dockOpacity || !m_dockFade) {
        return;
    }
    const bool fading = m_dockFade->state() == QAbstractAnimation::Running
                        && qFuzzyCompare(m_dockFade->endValue().toDouble() + 1.0,
                                         (visible ? 1.0 : 0.0) + 1.0);
    if (m_control->isVisible() == visible
        && (fading || qFuzzyCompare(m_dockOpacity->opacity() + 1.0, (visible ? 1.0 : 0.0) + 1.0))) {
        return; // 同上：目标状态已达成，不做无谓重绘
    }
    if (visible) {
        m_control->show();
        m_control->raise();
    }
    m_dockFade->stop();
    m_dockFade->setStartValue(m_dockOpacity->opacity());
    m_dockFade->setEndValue(visible ? 1.0 : 0.0);
    if (animate && !qFuzzyCompare(m_dockOpacity->opacity(), visible ? 1.0 : 0.0)) {
        m_dockFade->start();
    } else {
        m_dockOpacity->setOpacity(visible ? 1.0 : 0.0);
    }
}

void MainWindow::toggleTopLock()
{
    m_topLocked = !m_topLocked;
    if (m_toolbar) {
        m_toolbar->setLocked(m_topLocked);
    }
    setTopChromeVisible(true);
    if (m_toast) {
        m_toast->showMessage(m_topLocked ? tr("顶部栏已锁定") : tr("顶部栏已解锁"));
    }
}

void MainWindow::toggleDockLock()
{
    m_dockLocked = !m_dockLocked;
    if (m_control) {
        m_control->setLocked(m_dockLocked);
    }
    setDockVisible(true);
    if (m_toast) {
        m_toast->showMessage(m_dockLocked ? tr("底部控制胶囊已锁定")
                                          : tr("底部控制胶囊已解锁"));
    }
}

void MainWindow::updateChromeProximity()
{
    if (!isVisible() || isMinimized()) {
        return;
    }
    const QPoint local = mapFromGlobal(QCursor::pos());
    const bool inside = rect().contains(local);
    if (!m_topLocked) {
        setTopChromeVisible(isActiveWindow() && inside && local.y() <= 76);
    }
    if (!m_dockLocked) {
        setDockVisible(isActiveWindow() && inside && local.y() >= height() - 124);
    } else if (m_emptyState && m_emptyState->isVisible()) {
        setDockVisible(false);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyChromeLayout();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(QStringLiteral("#161719")));
    if (!isFullScreen()) {
        p.setPen(QPen(QColor(QStringLiteral("#3a3c40")), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

#ifdef Q_OS_WIN
// 无边框窗口边缘拖拽改尺寸（6px 逻辑命中带）
// 注意：msg->lParam 的坐标是物理像素，width()/height() 是 Qt 逻辑像素。
// 高 DPI（125%）下必须先按 devicePixelRatio 换算，否则窗口底部
// （物理 y > 逻辑 h-6 的整片区域，含胶囊栏）会被误判成缩放边框，
// Qt 收不到任何鼠标事件——胶囊按钮全部失灵。
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qint64 *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam) {
        // 客户区 = 整个窗口（原生标题栏/边框区域清零），DWM 的描边、投影、
        // 圆角不经过这里、照常生效。最大化时窗口矩形比屏幕大一圈 frame，
        // 要把客户区缩回来，否则四边内容被裁掉一条。
        if (IsZoomed(msg->hwnd)) {
            const UINT dpi = GetDpiForWindow(msg->hwnd);
            const int fx = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                         + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            const int fy = GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi)
                         + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam);
            params->rgrc[0].left   += fx;
            params->rgrc[0].top    += fy;
            params->rgrc[0].right  -= fx;
            params->rgrc[0].bottom -= fy;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST && !isMaximized() && !isFullScreen()) {
        POINT pt{ xFromLparam(msg->lParam), yFromLparam(msg->lParam) };
        ScreenToClient(msg->hwnd, &pt);
        const qreal dpr = devicePixelRatioF();
        const QPointF logical(pt.x / dpr, pt.y / dpr);
        const int b = 6;
        const double w = width(), h = height();
        const bool l = logical.x() < b, r = logical.x() > w - b;
        const bool t = logical.y() < b, bm = logical.y() > h - b;
        if (l && t)      { *result = HTTOPLEFT;     return true; }
        if (r && t)      { *result = HTTOPRIGHT;    return true; }
        if (l && bm)     { *result = HTBOTTOMLEFT;  return true; }
        if (r && bm)     { *result = HTBOTTOMRIGHT; return true; }
        if (l)           { *result = HTLEFT;        return true; }
        if (r)           { *result = HTRIGHT;       return true; }
        if (t)           { *result = HTTOP;         return true; }
        if (bm)          { *result = HTBOTTOM;      return true; }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

// 给无边框窗口补回 DWM 帧样式。幂等：样式位都在就不动（避免 FRAMECHANGED
// 引起闪烁）。Qt 全屏（showFullScreen）会按窗口 flags 重写样式、把这里的
// 补丁剥掉，退出全屏时由 changeEvent 再补回来。
void MainWindow::applyNativeFrameStyles()
{
    if (isFullScreen())
        return;
    HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR want = WS_THICKFRAME | WS_CAPTION | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if ((style & want) == want)
        return;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style | want);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                 SWP_FRAMECHANGED);
}

// Qt 的 showFullScreen/showNormal 会整体重写原生样式（FramelessWindowHint
// 语义 → 纯 WS_POPUP），DWM 描边/投影/圆角随之丢失。窗口状态变化且不是
// 全屏时，把帧样式补回来。
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
#ifdef Q_OS_WIN
    if (event->type() == QEvent::WindowStateChange && !isFullScreen())
        applyNativeFrameStyles();
#endif
}
#endif

// ---------------------------------------------------------------------------
// 动作系统
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    auto mk = [this](const QString &text, const QKeySequence &seq,
                     void (MainWindow::*slot)()) {
        auto *a = new QAction(text, this);
        if (!seq.isEmpty()) {
            a->setShortcut(seq);
        }
        connect(a, &QAction::triggered, this, slot);
        return a;
    };

    // 文件
    actOpen = mk(tr("打开(&O)..."), QKeySequence::Open, &MainWindow::onOpen);
    actOpenFolder = mk(tr("打开文件夹..."), {}, &MainWindow::onOpenFolder);
    actSaveAs = mk(tr("另存为…(&S)"), QKeySequence::SaveAs, &MainWindow::onSaveAs);
    actPrint = mk(tr("打印"), QKeySequence::Print, &MainWindow::onPrint);
    actBatchConvert = mk(tr("批量转换(&B)"), {}, &MainWindow::onBatchConvert);
    actSettings = mk(tr("设置(&C)"), QKeySequence(QStringLiteral("F9")), &MainWindow::onSettings);
    // 书签菜单：添加书签 B（原型），勾选/取消勾选由 onToggleFavorite 承担
    actFavorite = mk(tr("添加书签"), QKeySequence(QStringLiteral("B")), &MainWindow::onToggleFavorite);
    actDelete = mk(tr("删除"), QKeySequence::Delete, &MainWindow::onDeleteFile);
    actCopyToFolder = mk(tr("复制到 图片文件夹"), QKeySequence(QStringLiteral("Ins")), &MainWindow::onCopyToFolder);
    actOpenWithEditor = mk(tr("用图像编辑器打开"), QKeySequence(QStringLiteral("Ctrl+E")), &MainWindow::onOpenWithEditor);
    actCopyToClipboard = mk(tr("复制到剪贴板"), QKeySequence::Copy, &MainWindow::onCopyToClipboard);
    actWallpaper = mk(tr("设为桌面壁纸…"), {}, &MainWindow::onSetWallpaper);
    actAbout = mk(tr("关于 QuikView"), {}, &MainWindow::onAbout);
    actQuit = new QAction(tr("退出(&Q)"), this);
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);

    // 视图（浏览方式单选：原始 0 / 适合窗口 9 / 适合宽度 8，对照原版快捷键）
    auto *viewModeGroup = new QActionGroup(this);
    viewModeGroup->setExclusive(true);
    actActualSize = mk(tr("原始大小 (100%)"), QKeySequence(QStringLiteral("0")), &MainWindow::onActualSize);
    actFit = mk(tr("适合窗口"), QKeySequence(QStringLiteral("9")), &MainWindow::onFit);
    actFitWidth = mk(tr("适合宽度"), QKeySequence(QStringLiteral("8")), &MainWindow::onFitWidth);
    for (auto *a : { actActualSize, actFit, actFitWidth }) {
        a->setCheckable(true);
        viewModeGroup->addAction(a);
    }
    actFit->setChecked(true);
    actZoomIn = mk(tr("放大(&+)"), QKeySequence::ZoomIn, &MainWindow::onZoomIn);
    actZoomOut = mk(tr("缩小(&-)"), QKeySequence::ZoomOut, &MainWindow::onZoomOut);
    actPrev = mk(tr("上一张图片"), {}, &MainWindow::onPrev);
    actNext = mk(tr("下一张图片"), {}, &MainWindow::onNext);
    // 浏览导航（对照原版 MENUSTR_PAGEMOVE_*）：10 张跳转/首尾，边界 toast
    actPrev10 = mk(tr("向上10张图片"), QKeySequence(QStringLiteral("Ctrl+PageUp")), &MainWindow::onPrev10);
    actNext10 = mk(tr("向下10张图片"), QKeySequence(QStringLiteral("Ctrl+PageDown")), &MainWindow::onNext10);
    actFirst = mk(tr("首张图片"), QKeySequence(QStringLiteral("Home")), &MainWindow::onFirst);
    actLast = mk(tr("末尾图片"), QKeySequence(QStringLiteral("End")), &MainWindow::onLast);
    // 同级文件夹跳转（对照原版 MENUSTR_PAGEMOVE_NEXT_PATH/PREV_PATH：] [）
    actPrevFolder = mk(tr("上一个 文件夹/压缩包"), QKeySequence(QStringLiteral("[")), &MainWindow::onPrevFolder);
    actNextFolder = mk(tr("下一个 文件夹/压缩包"), QKeySequence(QStringLiteral("]")), &MainWindow::onNextFolder);
    actNextFrame = mk(tr("下一帧"), QKeySequence(QStringLiteral("Ctrl+Right")), &MainWindow::onNextFrame);
    actPrevFrame = mk(tr("上一帧"), QKeySequence(QStringLiteral("Ctrl+Left")), &MainWindow::onPrevFrame);

    actSlideshow = new QAction(tr("幻灯片放映(&S)"), this);
    actSlideshow->setCheckable(true);
    connect(actSlideshow, &QAction::toggled, this, &MainWindow::setSlideshow);

    actToggleMeta = new QAction(tr("显示/隐藏信息面板(&I)"), this);
    actToggleMeta->setCheckable(true);
    connect(actToggleMeta, &QAction::toggled, m_metadata, &QWidget::setVisible);
    // 面板自带 × 关掉时，把菜单勾选态同步回未勾选，避免「关了还打勾」
    connect(m_metadata, &MetadataPanel::closed, this, [this]() {
        actToggleMeta->setChecked(false);
    });

    actDark = new QAction(tr("深色模式(&D)"), this);
    actDark->setCheckable(true);
    actDark->setChecked(Config::instance().theme() != QLatin1String("light"));
    connect(actDark, &QAction::toggled, this, [](bool on) {
        Config::instance().settings().setValue(QStringLiteral("config/theme"),
                                                on ? QStringLiteral("dark") : QStringLiteral("light"));
        Theme::apply(*qApp, on ? Theme::Mode::Dark : Theme::Mode::Light);
    });

    // 编辑（旋转快捷键对照原版 MENUSTR_ROTATION_*：Alt+Left/Right）
    actRotateLeft = mk(tr("逆时针旋转 90°"), QKeySequence(QStringLiteral("Alt+Left")), &MainWindow::onRotateLeft);
    actRotateRight = mk(tr("顺时针旋转 90°"), QKeySequence(QStringLiteral("Alt+Right")), &MainWindow::onRotateRight);
    actFlipH = mk(tr("水平翻转"), {}, &MainWindow::onFlipH);
    actFlipV = mk(tr("垂直翻转"), {}, &MainWindow::onFlipV);
    actCrop = mk(tr("裁剪(&C)"), QKeySequence(QStringLiteral("X")), &MainWindow::onCrop);
    actResize = mk(tr("调整大小(&R)"), {}, &MainWindow::onResize);

    // 图像效果（三选一，对照原型 contextItems：无 U / 减淡 I / 减淡 + 锐化 S）
    auto *effectGroup = new QActionGroup(this);
    effectGroup->setExclusive(true);
    actEffectNone = new QAction(tr("无"), this);
    actEffectSoften = new QAction(tr("减淡"), this);
    actEffectSharpen = new QAction(tr("减淡 + 锐化"), this);
    actEffectNone->setShortcut(QKeySequence(QStringLiteral("U")));
    actEffectSoften->setShortcut(QKeySequence(QStringLiteral("I")));
    actEffectSharpen->setShortcut(QKeySequence(QStringLiteral("S")));
    for (auto *a : { actEffectNone, actEffectSoften, actEffectSharpen }) {
        a->setCheckable(true);
        effectGroup->addAction(a);
    }
    actEffectNone->setChecked(true);
    connect(actEffectNone, &QAction::triggered, this, [this]() { onEffect(0); });
    connect(actEffectSoften, &QAction::triggered, this, [this]() { onEffect(1); });
    connect(actEffectSharpen, &QAction::triggered, this, [this]() { onEffect(2); });

    // 插值算法（二选一，文案对照原型"插值算法"子菜单；Lanczos 暂无实现）
    auto *interpGroup = new QActionGroup(this);
    interpGroup->setExclusive(true);
    actInterpFast = new QAction(tr("Bilinear 插值 (快)"), this);
    actInterpSmooth = new QAction(tr("Bicubic 插值 (慢，高品质)"), this);
    actInterpFast->setCheckable(true);
    actInterpSmooth->setCheckable(true);
    interpGroup->addAction(actInterpFast);
    interpGroup->addAction(actInterpSmooth);
    actInterpSmooth->setChecked(m_smoothInterpolation);
    actInterpFast->setChecked(!m_smoothInterpolation);
    connect(actInterpFast, &QAction::triggered, this, [this]() {
        m_smoothInterpolation = false;
        m_canvas->setSmooth(false);
        Config::instance().settings().setValue(QStringLiteral("config/interpolation"), QStringLiteral("fast"));
    });
    connect(actInterpSmooth, &QAction::triggered, this, [this]() {
        m_smoothInterpolation = true;
        m_canvas->setSmooth(true);
        Config::instance().settings().setValue(QStringLiteral("config/interpolation"), QStringLiteral("smooth"));
    });

    // 排序方式（四选一 + 降序开关，对照原版 MENUSTR_SORT_*）
    auto *sortGroup = new QActionGroup(this);
    sortGroup->setExclusive(true);
    actSortName = new QAction(tr("文件名"), this);
    actSortTime = new QAction(tr("修改日期"), this);
    actSortSize = new QAction(tr("大小"), this);
    actSortType = new QAction(tr("类型"), this);
    for (auto *a : { actSortName, actSortTime, actSortSize, actSortType }) {
        a->setCheckable(true);
        sortGroup->addAction(a);
    }
    actSortName->setChecked(true);
    connect(actSortName, &QAction::triggered, this, [this]() { onSort(0); });
    connect(actSortTime, &QAction::triggered, this, [this]() { onSort(1); });
    connect(actSortSize, &QAction::triggered, this, [this]() { onSort(2); });
    connect(actSortType, &QAction::triggered, this, [this]() { onSort(3); });
    actSortDesc = new QAction(tr("降序"), this);
    actSortDesc->setCheckable(true);
    connect(actSortDesc, &QAction::toggled, this, &MainWindow::onSortDescending);
}

// ---------------------------------------------------------------------------
// 工具栏下拉菜单 / 右键菜单
// 唯一真源：Temp/quikview-secondary-prototype.html 的 toolbarMenus / contextSubmenus /
// contextItems。菜单每次弹出重建，条目/顺序/快捷键/勾选态直接照抄原型；
// 快捷键写在 '\t' 之后只作右对齐展示，真实按键绑定仍由已有 QAction::setShortcut 承担。
// ---------------------------------------------------------------------------

namespace {

// 菜单项：label + 右对齐快捷键（仅展示）。fn 为动作实现。
template <typename Fn>
QAction *addMenuItem(QMenu *menu, const QString &label, const QString &shortcut, Fn fn)
{
    QString text = label;
    if (!shortcut.isEmpty()) {
        text += QLatin1Char('\t') + shortcut;
    }
    QAction *action = menu->addAction(text);
    QObject::connect(action, &QAction::triggered, action, fn);
    return action;
}

// 带勾选态的菜单项：checked 由调用方按真实运行状态给出。
template <typename Fn>
QAction *addCheckableItem(QMenu *menu, const QString &label, const QString &shortcut,
                          bool checked, Fn fn)
{
    QAction *action = addMenuItem(menu, label, shortcut, fn);
    action->setCheckable(true);
    action->setChecked(checked);
    return action;
}

} // namespace

// 浏览：原始大小 / 适合窗口 / 适合宽度 / 智能双页×2 / 拉伸小图（7 项）
QMenu *MainWindow::buildBrowseMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actActualSize);   // 0
    menu->addAction(actFit);          // 9
    menu->addAction(actFitWidth);     // 8
    addMenuItem(menu, tr("智能双页阅览 (左→右)"), QStringLiteral("7"), [this]() {
        m_toast->showMessage(tr("「智能双页阅览」暂未实现"));
    });
    addMenuItem(menu, tr("智能双页阅览 (左←右)"), QStringLiteral("6"), [this]() {
        m_toast->showMessage(tr("「智能双页阅览」暂未实现"));
    });
    menu->addSeparator();
    addMenuItem(menu, tr("拉伸小图"), QStringLiteral("Z"), [this]() {
        m_toast->showMessage(tr("「拉伸小图」暂未实现"));
    });
    UiStyle::polishMenu(menu);
    return menu;
}

// 幻灯片放映：停止放映 + 1~9 秒（Ctrl+1~9）+ 10/15/20/25/30/60/90 秒 + 循环 + 随机（19 项）
QMenu *MainWindow::buildSlideshowMenu()
{
    auto *menu = new QMenu(this);
    addCheckableItem(menu, tr("停止放映"), QStringLiteral("Ctrl+0"), !m_slideshow, [this]() {
        if (m_slideshow) {
            actSlideshow->setChecked(false);
        }
    });

    // 间隔秒数：同一时间只有一个勾选（真实值来自 config/slideshow_interval_ms）
    auto *intervalGroup = new QActionGroup(menu);
    intervalGroup->setExclusive(true);
    const int currentSec = qMax(1, Config::instance().slideshowIntervalMs() / 1000);
    const QList<int> seconds = { 1, 2, 3, 4, 5, 6, 7, 8, 9,
                                 10, 15, 20, 25, 30, 60, 90 };
    for (int s : seconds) {
        const QString shortcut = s <= 9 ? QStringLiteral("Ctrl+%1").arg(s) : QString();
        QAction *a = addCheckableItem(menu, tr("%1 秒").arg(s), shortcut, currentSec == s,
                                      [this, s]() {
            Config::instance().settings().setValue(QStringLiteral("config/slideshow_interval_ms"),
                                                   s * 1000);
            m_slideshowTimer->setInterval(s * 1000);
            if (m_slideshow) {
                m_slideshowTimer->start();
            }
        });
        intervalGroup->addAction(a);
    }

    menu->addSeparator();
    // 循环：对应"到达首尾时转到首个文件"（config/edge_action = first）
    addCheckableItem(menu, tr("循环"), QString(),
                     Config::instance().edgeAction() == QLatin1String("first"), [this]() {
        const QString next = Config::instance().edgeAction() == QLatin1String("first")
                                 ? QStringLiteral("stop") : QStringLiteral("first");
        Config::instance().settings().setValue(QStringLiteral("config/edge_action"), next);
        m_dir->setEdgeAction(next);
    });
    addCheckableItem(menu, tr("随机"), QString(), m_slideshowRandom, [this]() {
        m_slideshowRandom = !m_slideshowRandom;
        Config::instance().settings().setValue(QStringLiteral("config/slideshow_random"),
                                               m_slideshowRandom);
    });
    UiStyle::polishMenu(menu);
    return menu;
}

// 书签：添加书签 / 编辑书签 / 删除所有书签 / 书签设置（6 项含 2 条分隔）
QMenu *MainWindow::buildBookmarkMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actFavorite);   // 添加书签 B
    addMenuItem(menu, tr("编辑书签"), QStringLiteral("F3"), [this]() {
        m_toast->showMessage(tr("「编辑书签」暂未实现"));
    });
    menu->addSeparator();
    addMenuItem(menu, tr("删除所有书签"), QString(), [this]() {
        Config::instance().settings().setValue(QStringLiteral("favorites"), QStringList());
        refreshLibraryMenus();
        m_toast->showMessage(tr("已删除所有书签"));
    });
    menu->addSeparator();
    addMenuItem(menu, tr("书签设置"), QString(), [this]() { onSettings(); });
    UiStyle::polishMenu(menu);
    return menu;
}

// 编辑：反色 / Gamma / 垂直翻转 / 水平翻转 / 转换图片 / 壁纸 / 打印 / 编辑器（8 项）
QMenu *MainWindow::buildEditMenu()
{
    auto *menu = new QMenu(this);
    addMenuItem(menu, tr("反色"), QString(), [this]() {
        m_toast->showMessage(tr("「反色」暂未实现"));
    });
    addMenuItem(menu, tr("Gamma 补偿…"), QString(), [this]() {
        m_toast->showMessage(tr("「Gamma 补偿」暂未实现"));
    });
    menu->addAction(actFlipV);
    menu->addAction(actFlipH);
    menu->addSeparator();
    // 原版"转换图片"即图片转换对话框；本项目对应 BatchConvertDialog（标题「图片转换」）
    addMenuItem(menu, tr("转换图片 (旋转/裁剪)"), QStringLiteral("Ctrl+T"), [this]() {
        onBatchConvert();
    });
    menu->addAction(actWallpaper);
    menu->addAction(actPrint);
    menu->addAction(actOpenWithEditor);
    UiStyle::polishMenu(menu);
    return menu;
}

// 复制到：图片文件夹 1 / 2、移动到 1、路径定义、打开文件夹 1（5 项）
QMenu *MainWindow::buildCopytoMenu()
{
    auto *menu = new QMenu(this);
    auto transferTo = [this, menu](const QString &label, const QString &shortcut,
                                   const QString &key, bool moveFile) {
        addMenuItem(menu, label, shortcut, [this, key, moveFile]() {
            const QString dir = Config::instance().settings().value(key).toString();
            if (dir.isEmpty()) {
                m_toast->showMessage(tr("请先在 设置 → 杂项 中配置图片文件夹"));
                return;
            }
            if (m_currentPath.isEmpty()) {
                return;
            }
            const QString dest = QDir(dir).filePath(QFileInfo(m_currentPath).fileName());
            if (QFileInfo::exists(dest)) {
                m_toast->showMessage(tr("目标已存在：%1").arg(dest));
                return;
            }
            const bool ok = moveFile ? QFile::rename(m_currentPath, dest)
                                     : QFile::copy(m_currentPath, dest);
            if (!ok) {
                m_toast->showMessage(tr("操作失败：%1").arg(dest));
            } else if (moveFile) {
                m_toast->showMessage(tr("已移动到：%1").arg(dest));
            } else {
                m_toast->showMessage(tr("已复制到：%1").arg(dest));
            }
        });
    };
    transferTo(tr("复制到 图片文件夹 1"), QStringLiteral("Insert"),
               QStringLiteral("config/etc/photo_folder_1"), false);
    transferTo(tr("复制到 图片文件夹 2"), QStringLiteral("Shift+Insert"),
               QStringLiteral("config/etc/photo_folder_2"), false);
    transferTo(tr("移动到 图片文件夹 1"), QStringLiteral("Ctrl+Insert"),
               QStringLiteral("config/etc/photo_folder_1"), true);
    menu->addSeparator();
    addMenuItem(menu, tr("图片文件夹 路径定义"), QString(), [this]() { onSettings(); });
    addMenuItem(menu, tr("打开 图片文件夹 1"), QString(), [this]() {
        const QString dir = Config::instance().settings()
                                .value(QStringLiteral("config/etc/photo_folder_1")).toString();
        if (dir.isEmpty() || !QFileInfo::exists(dir)) {
            m_toast->showMessage(tr("请先在 设置 → 杂项 中配置图片文件夹"));
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    UiStyle::polishMenu(menu);
    return menu;
}

// 系统菜单（原型未定义，保留"更多"菜单：收纳 5 个下拉不再出现的既有功能，
// 避免已实现的动作丢失入口）
QMenu *MainWindow::buildSystemMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actOpen);
    menu->addAction(actOpenFolder);
    menu->addAction(actSaveAs);
    menu->addSeparator();
    menu->addAction(actSlideshow);
    menu->addAction(actPrevFrame);
    menu->addAction(actNextFrame);
    menu->addAction(actCrop);
    menu->addAction(actResize);
    menu->addSeparator();
    menu->addAction(actDark);
    menu->addAction(actSettings);
    menu->addAction(actAbout);
    menu->addSeparator();
    menu->addAction(actQuit);
    UiStyle::polishMenu(menu);
    return menu;
}

// 右键菜单：25 个根项 + 7 组级联（浏览导航/插值算法/图片效果/排序方式/
// 浏览方式/缩放工具/图像旋转），条目顺序与分隔线位置照抄 contextItems。
QMenu *MainWindow::buildContextMenu()
{
    auto *menu = new QMenu(this);
    // 暂无实现的条目：点击后明确提示，不做静默无反应
    auto pending = [this](QMenu *target, const QString &label, const QString &shortcut) {
        addMenuItem(target, label, shortcut, [this, label]() {
            m_toast->showMessage(tr("「%1」暂未实现").arg(label));
        });
    };

    // —— 文件 ——
    addMenuItem(menu, tr("打开图片"), QStringLiteral("F2"), [this]() { onOpen(); });
    addMenuItem(menu, tr("打开文件夹"), QStringLiteral("F"), [this]() { onOpenFolder(); });
    addMenuItem(menu, tr("关闭"), QStringLiteral("F4"), [this]() { close(); });
    pending(menu, tr("选择图片"), QStringLiteral("Enter"));
    addMenuItem(menu, tr("定位文件"), QStringLiteral("Ctrl+Enter"), [this]() {
        if (m_currentPath.isEmpty()) {
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentPath).absolutePath()));
    });
    menu->addSeparator();

    // —— 显示效果（真实勾选态由 actEffect* 单选组承载）——
    menu->addAction(actEffectNone);
    menu->addAction(actEffectSoften);
    menu->addAction(actEffectSharpen);
    menu->addSeparator();

    // —— 浏览导航（10 项）——
    QMenu *nav = menu->addMenu(tr("浏览导航"));
    addMenuItem(nav, tr("下一张图片"), QStringLiteral("→"), [this]() { onNext(); });
    addMenuItem(nav, tr("上一张图片"), QStringLiteral("←"), [this]() { onPrev(); });
    addMenuItem(nav, tr("首张图片"), QStringLiteral("Home"), [this]() { onFirst(); });
    addMenuItem(nav, tr("末尾图片"), QStringLiteral("End"), [this]() { onLast(); });
    addMenuItem(nav, tr("向下10张图片"), QStringLiteral("Ctrl+PageDown"), [this]() { onNext10(); });
    addMenuItem(nav, tr("向上10张图片"), QStringLiteral("Ctrl+PageUp"), [this]() { onPrev10(); });
    // 随机跳转：向后 = 只在当前位置之后取，越界回绕
    auto jumpRandom = [this](bool forward) {
        const int n = m_dir->count();
        if (n < 2) {
            return;
        }
        const int cur = m_dir->index();
        int lo = 0;
        int hi = n - 1;
        if (forward) {
            lo = cur + 1;
            if (lo > hi) {
                lo = 0;
                hi = cur - 1;
            }
        } else {
            hi = cur - 1;
            if (hi < lo) {
                lo = cur + 1;
                hi = n - 1;
            }
        }
        if (hi < lo) {
            return;
        }
        const int target = lo + QRandomGenerator::global()->bounded(hi - lo + 1);
        m_dir->setIndex(target);
        loadAsync(m_dir->current());
    };
    addMenuItem(nav, tr("向后随机转到某张图片"), QStringLiteral("Ctrl+Alt+PageDown"),
                [jumpRandom]() { jumpRandom(true); });
    addMenuItem(nav, tr("向前随机转到某张图片"), QStringLiteral("Ctrl+Alt+PageUp"),
                [jumpRandom]() { jumpRandom(false); });
    addMenuItem(nav, tr("下一个 文件夹/压缩包"), QStringLiteral("]"), [this]() { onNextFolder(); });
    addMenuItem(nav, tr("上一个 文件夹/压缩包"), QStringLiteral("["), [this]() { onPrevFolder(); });
    UiStyle::polishMenu(nav);

    // —— 插值算法（3 项，勾选态跟随真实设置）——
    QMenu *interp = menu->addMenu(tr("插值算法"));
    actInterpFast->setChecked(!m_smoothInterpolation);
    actInterpSmooth->setChecked(m_smoothInterpolation);
    interp->addAction(actInterpFast);
    interp->addAction(actInterpSmooth);
    pending(interp, tr("Lanczos 插值 (慢，高品质)"), QString());
    UiStyle::polishMenu(interp);

    // —— 图片效果（4 项）——
    QMenu *fx = menu->addMenu(tr("图片效果"));
    pending(fx, tr("反色"), QString());
    pending(fx, tr("Gamma 补偿…"), QString());
    fx->addAction(actFlipV);
    fx->addAction(actFlipH);
    UiStyle::polishMenu(fx);

    // —— 排序方式（5 项 + 1 条分隔，勾选态来自真实排序设置）——
    QMenu *sort = menu->addMenu(tr("排序方式"));
    const int sortMode = Config::instance().sortMode();
    actSortName->setChecked(sortMode == 0);
    actSortTime->setChecked(sortMode == 1);
    actSortSize->setChecked(sortMode == 2);
    actSortType->setChecked(sortMode == 3);
    actSortDesc->setChecked(Config::instance().sortDescending());
    sort->addAction(actSortName);
    sort->addAction(actSortTime);
    sort->addAction(actSortSize);
    sort->addAction(actSortType);
    sort->addSeparator();
    sort->addAction(actSortDesc);
    UiStyle::polishMenu(sort);
    menu->addSeparator();

    // —— 浏览方式（原型取 showmode 前 5 项）——
    QMenu *show = menu->addMenu(tr("浏览方式"));
    actActualSize->setChecked(m_viewMode == 0);
    actFit->setChecked(m_viewMode == 1);
    actFitWidth->setChecked(m_viewMode == 2);
    show->addAction(actActualSize);
    show->addAction(actFit);
    show->addAction(actFitWidth);
    pending(show, tr("智能双页阅览 (左→右)"), QStringLiteral("7"));
    pending(show, tr("智能双页阅览 (左←右)"), QStringLiteral("6"));
    UiStyle::polishMenu(show);

    // —— 缩放工具（6 项）——
    QMenu *zoom = menu->addMenu(tr("缩放工具"));
    addMenuItem(zoom, tr("放大"), QStringLiteral("+"), [this]() { onZoomIn(); });
    addMenuItem(zoom, tr("缩小"), QStringLiteral("−"), [this]() { onZoomOut(); });
    pending(zoom, tr("锁定"), QStringLiteral("Ctrl+K"));
    addMenuItem(zoom, tr("100%"), QStringLiteral("*"), [this]() { setViewMode(0); });
    addMenuItem(zoom, tr("放大 (1%)"), QStringLiteral("Ctrl++"), [this]() {
        if (!m_currentImage.isNull()) {
            m_canvas->setZoom(m_canvas->zoom() + 0.01);
        }
    });
    addMenuItem(zoom, tr("缩小 (1%)"), QStringLiteral("Ctrl+−"), [this]() {
        if (!m_currentImage.isNull()) {
            m_canvas->setZoom(m_canvas->zoom() - 0.01);
        }
    });
    UiStyle::polishMenu(zoom);

    // —— 图像旋转（4 项）——
    QMenu *rot = menu->addMenu(tr("图像旋转"));
    addCheckableItem(rot, tr("不旋转"), QStringLiteral("Alt+Up"), false, [this]() {
        m_toast->showMessage(tr("「不旋转」暂未实现"));
    });
    rot->addAction(actRotateLeft);    // 逆时针 90° Alt+Left
    rot->addAction(actRotateRight);   // 顺时针 90° Alt+Right
    addMenuItem(rot, tr("旋转 180°"), QStringLiteral("Alt+Down"), [this]() { rotateImage(180); });
    UiStyle::polishMenu(rot);
    menu->addSeparator();

    // —— 信息 / 文件操作 ——
    addCheckableItem(menu, tr("EXIF 信息"), QStringLiteral("TAB"), m_metadata->isVisible(),
                     [this]() { actToggleMeta->setChecked(!m_metadata->isVisible()); });
    menu->addAction(actWallpaper);
    menu->addAction(actDelete);
    menu->addAction(actCopyToFolder);
    menu->addAction(actOpenWithEditor);
    menu->addAction(actPrint);
    if (Config::instance().contextCopy()) {
        menu->addAction(actCopyToClipboard);
    }
    menu->addSeparator();

    // —— 应用 ——
    addMenuItem(menu, tr("设置"), QStringLiteral("F5"), [this]() { onSettings(); });
    addMenuItem(menu, tr("关于"), QStringLiteral("F1"), [this]() { onAbout(); });
    addMenuItem(menu, tr("退出"), QStringLiteral("X"), [this]() { close(); });

    UiStyle::polishMenu(menu);
    return menu;
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = buildContextMenu();
    menu->exec(event->globalPos());
    delete menu;
}

// ---------------------------------------------------------------------------
// 运行设置
// ---------------------------------------------------------------------------

void MainWindow::applyRuntimeSettings()
{
    const Config &cfg = Config::instance();

    setWindowFlag(Qt::WindowStaysOnTopHint, cfg.alwaysOnTop());

    m_smoothInterpolation = (cfg.interpolation() != QLatin1String("fast"));
    m_canvas->setSmooth(m_smoothInterpolation);
    m_canvas->setWheelZoomEnabled(cfg.wheelZoom());
    m_canvas->setMiddlePanEnabled(cfg.middlePan());

    const QString ts = cfg.thumbnailSize();
    m_browser->setThumbSize(ts == QLatin1String("small") ? 96
                            : ts == QLatin1String("large") ? 160 : 128);

    m_dir->setEdgeAction(cfg.edgeAction());

    // 排序设置恢复（键 + 方向），菜单勾选态同步
    m_dir->setSort(cfg.sortMode());
    m_dir->setSortDescending(cfg.sortDescending());
    if (actSortName) {
        actSortName->setChecked(cfg.sortMode() == 0);
    }
    if (actSortTime) {
        actSortTime->setChecked(cfg.sortMode() == 1);
    }
    if (actSortSize) {
        actSortSize->setChecked(cfg.sortMode() == 2);
    }
    if (actSortType) {
        actSortType->setChecked(cfg.sortMode() == 3);
    }
    if (actSortDesc) {
        actSortDesc->setChecked(cfg.sortDescending());
    }

#ifdef Q_OS_WIN
    // setWindowFlag() 会让 Qt 按 flags 重建原生样式，把 WS_THICKFRAME|WS_CAPTION
    // 一起抹掉。没有 WS_THICKFRAME，Windows 对 WM_NCLBUTTONDOWN 的命中结果直接
    // 忽略——点边框既不进入 SC_SIZE 循环也不改尺寸（表现为窗口"拖不动边"）。
    // 这里统一补回，幂等。
    applyNativeFrameStyles();
#endif
}

void MainWindow::toggleAlwaysOnTop()
{
    const bool on = !Config::instance().alwaysOnTop();
    Config::instance().settings().setValue(QStringLiteral("config/always_on_top"), on);
    applyRuntimeSettings();
    m_toast->showMessage(on ? tr("总在最前端：开") : tr("总在最前端：关"));
}

// ---------------------------------------------------------------------------
// 文件打开与加载
// ---------------------------------------------------------------------------

void MainWindow::openFile(const QString &path)
{
    if (!m_dir->open(path)) {
        m_toast->showMessage(tr("无法读取文件：") + path);
        return;
    }
    m_browser->setFiles(m_dir->files(), m_dir->index());
    m_currentPath = m_dir->current();
    if (Config::instance().rememberBrowsedFolders()) {
        Config::instance().addRecentFile(m_currentPath);
        refreshLibraryMenus();
    }
    loadAsync(m_currentPath);
}

void MainWindow::loadAsync(const QString &path)
{
    m_currentPath = path;
    m_frames->clear();
    m_cache->requestFull(path);
}

void MainWindow::onFullReady(const QString &path, const QImage &image)
{
    if (path != m_currentPath) {
        return; // 过期结果（用户已切到别的图）
    }
    showImage(image, path);
}

void MainWindow::onLoadFailed(const QString &path, const QString &error)
{
    if (path != m_currentPath) {
        return;
    }
    QString rawErr;
    QImage img = loadRawImage(path, &rawErr);
    if (!img.isNull()) {
        showImage(img, path);
        return;
    }
    QString heifErr;
    img = loadHeifImage(path, &heifErr);
    if (!img.isNull()) {
        showImage(img, path);
        return;
    }
    QString lastErr = rawErr.isEmpty() ? heifErr : rawErr;
    m_toast->showMessage(tr("无法打开：%1").arg(lastErr.isEmpty() ? error : lastErr));
}

void MainWindow::showImage(const QImage &image, const QString &path)
{
    m_currentImage = image;
    m_originalImage = image;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
    m_canvas->setPixmap(QPixmap::fromImage(image));
    // 首图按设置默认（fit/actual）；之后翻页保持用户当前浏览方式
    if (m_firstImage && Config::instance().defaultZoom() == QLatin1String("actual")) {
        setViewMode(0);
    } else {
        setViewMode(m_viewMode);
    }
    m_firstImage = false;
    updateCanvasLogo();
    m_metadata->showImage(path, image);
    m_browser->setCurrent(m_dir->index());
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" - QuikView"));
    m_caption->setTitle(QFileInfo(path).fileName());
    m_frames->open(path);
}

void MainWindow::updateCanvasLogo()
{
    const bool empty = m_currentImage.isNull();
    m_canvas->setLogoVisible(false);
    if (m_emptyState) {
        m_emptyState->setVisible(empty);
        if (empty) {
            m_emptyState->raise();
        }
    }
    if (m_statusOverlay) {
        m_statusOverlay->setVisible(!empty);
    }
    if (empty) {
        if (m_statusOverlay) {
            m_statusOverlay->clear();
        }
        setDockVisible(false, false);
    } else if (m_dockLocked) {
        setDockVisible(true, false);
    } else {
        // 翻页会经过这里。之前无条件隐藏胶囊、再被热区轮询唤出，点一下就是
        // 一次「闪」。改为按鼠标实际位置重新判定，鼠标还在胶囊上就保持原样。
        updateChromeProximity();
    }
    updateInfoOverlay();
}

void MainWindow::updateInfoOverlay()
{
    if (!m_statusOverlay) {
        return;
    }
    if (m_currentImage.isNull() || m_currentPath.isEmpty()) {
        m_statusOverlay->clear();
        return;
    }
    const QFileInfo fi(m_currentPath);
    QStringList parts;
    parts << QStringLiteral("%1 × %2").arg(m_currentImage.width()).arg(m_currentImage.height());
    if (fi.exists()) {
        const qint64 bytes = fi.size();
        if (bytes >= 1024 * 1024) {
            parts << QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
        } else {
            parts << QStringLiteral("%1 KB").arg(qMax<qint64>(1, bytes / 1024));
        }
    }
    QString suffix = fi.suffix().toUpper();
    if (suffix == QLatin1String("JPG")) {
        suffix = QStringLiteral("JPEG");
    } else if (suffix == QLatin1String("TIF")) {
        suffix = QStringLiteral("TIFF");
    }
    if (!suffix.isEmpty()) {
        parts << suffix;
    }
    m_statusOverlay->setImageInfo(parts.join(QStringLiteral("  •  ")));

    if (m_dir) {
        const QStringList files = m_dir->files();
        const int index = m_dir->index();
        if (!files.isEmpty() && index >= 0) {
            m_statusOverlay->setCounter(
                QStringLiteral("%1 / %2").arg(index + 1).arg(files.size()));
        } else {
            m_statusOverlay->setCounter(QString());
        }
    }
}

void MainWindow::updateStatus(const QString &path)
{
    // 状态并入标题栏（原版无状态栏）
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" - QuikView"));
    m_caption->setTitle(QFileInfo(path).fileName());
}

void MainWindow::onActivateImage(int index)
{
    m_dir->setIndex(index);
    const QString p = m_dir->current();
    if (!p.isEmpty()) {
        loadAsync(p);
    }
}

// ---------------------------------------------------------------------------
// 浏览 / 视图
// ---------------------------------------------------------------------------

void MainWindow::onFit()        { setViewMode(1); }
void MainWindow::onFitWidth()  { setViewMode(2); }
void MainWindow::onActualSize() { setViewMode(0); }
void MainWindow::onZoomIn()    { m_canvas->zoomIn(); }
void MainWindow::onZoomOut()   { m_canvas->zoomOut(); }

// 浏览方式统一入口：0=原始 1=适合窗口 2=适合宽度。
// 同步画布几何、动作勾选态与工具栏模式图标（static_showmode_0..2）
void MainWindow::setViewMode(int mode)
{
    m_viewMode = mode;
    switch (mode) {
    case 0:
        m_canvas->zoomActual();
        break;
    case 2:
        m_canvas->fitToWidth();
        break;
    case 1:
    default:
        m_canvas->fitToWindow();
        break;
    }
    if (actActualSize) {
        actActualSize->setChecked(mode == 0);
    }
    if (actFit) {
        actFit->setChecked(mode == 1);
    }
    if (actFitWidth) {
        actFitWidth->setChecked(mode == 2);
    }
    if (m_toolbar) {
        m_toolbar->setShowModeIcon(mode);
    }
}

void MainWindow::syncZoom()
{
    m_control->setZoomPercent(qRound(m_canvas->zoom() * 100));
}

// 冒烟自动化：单命令驱动（--smoke next10 等）。命令在首图就绪后由 main() 调度。
// 命令集随 P0 任务扩展：翻页/浏览方式/排序/文件夹跳转/幻灯片。
void MainWindow::runSmokeCommand(const QString &cmd)
{
    const QString c = cmd.trimmed();
    if (c == QLatin1String("next"))            onNext();
    else if (c == QLatin1String("prev"))       onPrev();
    else if (c == QLatin1String("next10"))     onNext10();
    else if (c == QLatin1String("prev10"))     onPrev10();
    else if (c == QLatin1String("first"))      onFirst();
    else if (c == QLatin1String("last"))       onLast();
    else if (c == QLatin1String("nextfolder")) onNextFolder();
    else if (c == QLatin1String("prevfolder")) onPrevFolder();
    else if (c == QLatin1String("fit"))        setViewMode(1);
    else if (c == QLatin1String("fitwidth"))   setViewMode(2);
    else if (c == QLatin1String("actual"))     setViewMode(0);
    else if (c == QLatin1String("zoomin"))     onZoomIn();
    else if (c == QLatin1String("zoomout"))    onZoomOut();
    else if (c == QLatin1String("rotateleft")) onRotateLeft();
    else if (c == QLatin1String("rotateright")) onRotateRight();
    else if (c == QLatin1String("slideshow"))  actSlideshow->trigger();
    // resize:WxH —— 模拟用户拖窗口改变尺寸（验收「调窗口不打回适合窗口」）
    else if (c.startsWith(QLatin1String("resize:"))) {
        const QStringList wh = c.mid(7).split(QLatin1Char('x'));
        if (wh.size() == 2) {
            const int rw = wh[0].toInt();
            const int rh = wh[1].toInt();
            if (rw > 200 && rh > 200) {
                resize(rw, rh);
            }
        }
    }
    else if (c.startsWith(QLatin1String("sort:"))) {
        bool ok = false;
        const int mode = c.mid(5).toInt(&ok);
        if (ok) {
            switch (mode) {
            case 0: actSortName->trigger(); break;
            case 1: actSortTime->trigger(); break;
            case 2: actSortSize->trigger(); break;
            case 3: actSortType->trigger(); break;
            default: break;
            }
        }
    }
    else if (c == QLatin1String("sortdesc"))   actSortDesc->toggle();
    else if (c == QLatin1String("browser")) {
        toggleBrowserPanel();
    }
    else if (c == QLatin1String("locktop")) { if (!m_topLocked) toggleTopLock(); }
    else if (c == QLatin1String("lockdock")) { if (!m_dockLocked) toggleDockLock(); }
    // 循环开关（用于验收「首尾边界」两种模式）。applyRuntimeSettings() 会从配置回读，
    // 所以这里必须写配置，否则下一次加载就被覆盖。
    else if (c == QLatin1String("loopon") || c == QLatin1String("loopoff")) {
        const QString edge = (c == QLatin1String("loopon")) ? QStringLiteral("first")
                                                            : QStringLiteral("stop");
        Config::instance().settings().setValue(QStringLiteral("config/edge_action"), edge);
        m_dir->setEdgeAction(edge);
    }
    else if (c == QLatin1String("fullscreen")) isFullScreen() ? showNormal() : showFullScreen();
    // wheel:slider|bar:up|down[:ctrl] —— 合成一次滚轮事件，验证「滑杆上滚翻页 / 其余位置缩放 / Ctrl 保留缩放」。
    // 外部注入滚轮要过 DPI 与消息循环，结果不可复现；这里直接把事件送到胶囊，判定只依赖胶囊自身逻辑。
    else if (c.startsWith(QLatin1String("wheel:"))) {
        const QStringList a = c.mid(6).split(QLatin1Char(':'));
        const bool onSlider = (a.value(0) == QLatin1String("slider"));
        const int dy = (a.value(1) == QLatin1String("up")) ? 120 : -120;
        const bool ctrl = (a.value(2) == QLatin1String("ctrl"));
        // 胶囊几何固定 440x72：第一排滑杆在该点，第二排取一个非滑杆位置
        const QPointF local(onSlider ? 193.0 : 150.0, onSlider ? 19.0 : 50.0);
        QWheelEvent ev(local, m_control->mapToGlobal(local.toPoint()), QPoint(0, 0),
                       QPoint(0, dy), Qt::NoButton,
                       ctrl ? Qt::ControlModifier : Qt::NoModifier,
                       Qt::NoScrollPhase, false);
        QApplication::sendEvent(m_control, &ev);
    }
    // —— 二级界面（截图冒烟用，非模态打开以便截到窗口本身）——
    else if (c == QLatin1String("settings")) {
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
    else if (c == QLatin1String("convert")) {
        auto *dlg = new BatchConvertDialog(m_dir->files(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
    else if (c == QLatin1String("info")) {
        if (m_metadata) {
            m_metadata->show();
            m_metadata->raise();
            applyChromeLayout();
        }
    }
    else if (c == QLatin1String("ctxmenu")) {
        QMenu *menu = buildContextMenu();
        menu->popup(mapToGlobal(QPoint(width() / 2 - 120, height() / 3)));
    }
    // 调试/验收：把缩放与边距的**真实数值**追加到日志（截图只能看个大概）
    else if (c == QLatin1String("report")) {
        QFile f(QStringLiteral("Temp/zoom-report.txt"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            const QPointF ctr = m_canvas->viewCenterNormalized();
            ts << "mode=" << m_viewMode
               << " zoom=" << QString::number(m_canvas->zoom(), 'f', 4)
               << " center=" << QString::number(ctr.x(), 'f', 4) << '/' << QString::number(ctr.y(), 'f', 4)
               << " fitWin=" << (m_canvas->isFitToWindow() ? 1 : 0)
               << " fitW=" << (m_canvas->isFitToWidth() ? 1 : 0)
               << " canvas=" << m_canvas->width() << 'x' << m_canvas->height()
               << " viewport=" << m_canvas->viewport()->width() << 'x' << m_canvas->viewport()->height()
               << " margins=" << m_canvasInsets.left() << ',' << m_canvasInsets.top() << ','
               << m_canvasInsets.right() << ',' << m_canvasInsets.bottom()
               << " win=" << width() << 'x' << height()
               << " idx=" << m_dir->index() << '/' << m_dir->count()
               << " cap=" << (m_control ? m_control->size().width() : 0) << 'x'
               << (m_control ? m_control->size().height() : 0)
               << " pct=" << m_control->zoomPercent()
               << " dpr=" << devicePixelRatioF()
               << "\n";
        }
    }
}

// 首尾边界：所有翻页入口（胶囊、菜单、快捷键、滚轮）都走这里，规则只有一份。
//   非循环（edge_action=stop）：停在边界并提示；
//   循环（edge_action=first）：回绕，但明确告知回到了另一端。
void MainWindow::onPrev()
{
    if (!m_dir->isValid()) {
        return;
    }
    const int before = m_dir->index();
    const QString p = m_dir->prev();
    if (p.isEmpty()) {
        if (m_toast) m_toast->showMessage(tr("已经是第一张图片"));
        return;
    }
    loadAsync(p);
    if (before == 0 && m_dir->count() > 1 && m_toast) {
        m_toast->showMessage(tr("已回到最后一张图片"));
    }
}

void MainWindow::onNext()
{
    if (!m_dir->isValid()) {
        return;
    }
    const int before = m_dir->index();
    const QString p = m_dir->next();
    if (p.isEmpty()) {
        if (m_toast) m_toast->showMessage(tr("已经是最后一张图片"));
        return;
    }
    loadAsync(p);
    if (before == m_dir->count() - 1 && m_dir->count() > 1 && m_toast) {
        m_toast->showMessage(tr("已回到第一张图片"));
    }
}

// 10 张跳转：边界停住并提示（TEXT_MSG_FIRSTPAGE/LASTPAGE）
void MainWindow::onPrev10()
{
    const QString p = m_dir->prev10();
    if (!p.isEmpty()) {
        loadAsync(p);
    } else if (m_dir->isValid()) {
        m_toast->showMessage(tr("已经是第一张图片"));
    }
}

void MainWindow::onNext10()
{
    const QString p = m_dir->next10();
    if (!p.isEmpty()) {
        loadAsync(p);
    } else if (m_dir->isValid()) {
        m_toast->showMessage(tr("已经是最后一张图片"));
    }
}

void MainWindow::onFirst()
{
    if (m_dir->count() > 0) {
        m_dir->setIndex(0);
        loadAsync(m_dir->current());
    }
}

void MainWindow::onLast()
{
    if (m_dir->count() > 0) {
        m_dir->setIndex(m_dir->count() - 1);
        loadAsync(m_dir->current());
    }
}

// 同级含图文件夹列表（名称序）。边界 = 列表首/尾。
QStringList MainWindow::listSiblingFolders()
{
    if (m_currentPath.isEmpty()) {
        return {};
    }
    // 当前图所在目录的父目录，其下各子目录即「同级文件夹」
    QDir parent = QFileInfo(m_currentPath).dir();
    if (!parent.cdUp()) {
        return {};
    }
    const QStringList dirs = parent.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                              QDir::Name | QDir::LocaleAware);
    QStringList out;
    out.reserve(dirs.size());
    for (const QString &d : dirs) {
        const QString abs = parent.absoluteFilePath(d);
        if (!listImagesInDir(abs).isEmpty()) {
            out << abs;
        }
    }
    return out;
}

void MainWindow::onNextFolder()
{
    const QStringList folders = listSiblingFolders();
    if (folders.isEmpty()) {
        return;
    }
    const int i = folders.indexOf(QFileInfo(m_currentPath).absolutePath());
    if (i < 0 || i >= folders.size() - 1) {
        m_toast->showMessage(tr("已是最后一个文件夹"));
        return;
    }
    const QString target = folders.at(i + 1);
    const QStringList files = listImagesInDir(target);
    if (files.isEmpty()) {
        return;
    }
    m_toast->showMessage(tr("下一个文件夹：") + QFileInfo(target).fileName());
    openFile(files.first());
}

void MainWindow::onPrevFolder()
{
    const QStringList folders = listSiblingFolders();
    if (folders.isEmpty()) {
        return;
    }
    const int i = folders.indexOf(QFileInfo(m_currentPath).absolutePath());
    if (i <= 0) {
        m_toast->showMessage(tr("已是第一个文件夹"));
        return;
    }
    const QString target = folders.at(i - 1);
    const QStringList files = listImagesInDir(target);
    if (files.isEmpty()) {
        return;
    }
    m_toast->showMessage(tr("上一个文件夹：") + QFileInfo(target).fileName());
    openFile(files.first());
}

void MainWindow::onSort(int mode)
{
    m_dir->setSort(mode);
    Config::instance().settings().setValue(QStringLiteral("config/sort_mode"), mode);
    if (m_currentPath.isEmpty() || !m_dir->isValid()) {
        return;
    }
    if (m_dir->open(m_currentPath)) {
        m_browser->setFiles(m_dir->files(), m_dir->index());
        loadAsync(m_currentPath);
    }
}

void MainWindow::onSortDescending(bool on)
{
    m_dir->setSortDescending(on);
    Config::instance().settings().setValue(QStringLiteral("config/sort_desc"), on);
    if (m_currentPath.isEmpty() || !m_dir->isValid()) {
        return;
    }
    if (m_dir->open(m_currentPath)) {
        m_browser->setFiles(m_dir->files(), m_dir->index());
        loadAsync(m_currentPath);
    }
}

void MainWindow::setSlideshow(bool on)
{
    m_slideshow = on;
    if (on) {
        m_slideshowTimer->start();
        m_toast->showMessage(tr("幻灯片放映：开"));
    } else {
        m_slideshowTimer->stop();
        m_toast->showMessage(tr("幻灯片放映：停"));
    }
}

void MainWindow::onSlideshow() { setSlideshow(!m_slideshow); }

// 幻灯片翻页 tick：随机模式跳到不同于当前的随机张（count>1），否则顺序下一张
void MainWindow::onSlideshowTick()
{
    if (m_slideshowRandom && m_dir->count() > 1) {
        int i = m_dir->index();
        while (i == m_dir->index()) {
            i = QRandomGenerator::global()->bounded(m_dir->count());
        }
        m_dir->setIndex(i);
        loadAsync(m_dir->current());
    } else {
        onNext();
    }
}

// ---------------------------------------------------------------------------
// 帧导航
// ---------------------------------------------------------------------------

void MainWindow::onNextFrame()
{
    const QImage img = m_frames->nextFrame();
    if (img.isNull()) {
        return;
    }
    m_currentImage = img;
    m_canvas->setPixmap(QPixmap::fromImage(img));
    updateStatus(m_currentPath);
}

void MainWindow::onPrevFrame()
{
    const QImage img = m_frames->prevFrame();
    if (img.isNull()) {
        return;
    }
    m_currentImage = img;
    m_canvas->setPixmap(QPixmap::fromImage(img));
    updateStatus(m_currentPath);
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------

// 原地旋转：保留当前缩放比例与「视口中心看到的那一点」，转完后这一点仍在
// 视口中心，所以图像不会跳位、也不会被悄悄退回适配窗口。
void MainWindow::rotateImage(int degrees)
{
    if (m_currentImage.isNull() || !m_canvas) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();
    const qreal z = m_canvas->zoom();
    const bool fitWindow = m_canvas->isFitToWindow();
    const bool fitWidth = m_canvas->isFitToWidth();

    QTransform t;
    t.rotate(degrees);
    m_currentImage = m_currentImage.transformed(t);

    // 归一化锚点跟着画布一起转：顺 90°(u,v)→(1-v,u)，逆 90°(u,v)→(v,1-u)
    QPointF anchor = n;
    if (degrees == 90) {
        anchor = QPointF(1.0 - n.y(), n.x());
    } else if (degrees == -90) {
        anchor = QPointF(n.y(), 1.0 - n.x());
    }

    resetEffect();
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(m_currentImage), anchor,
                                      z, fitWindow, fitWidth);
    updateInfoOverlay();
}

void MainWindow::onRotateLeft()
{
    rotateImage(-90);
}

void MainWindow::onRotateRight()
{
    rotateImage(90);
}

void MainWindow::onFlipH()
{
    if (m_currentImage.isNull()) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();
    const qreal z = m_canvas->zoom();
    const bool fitWindow = m_canvas->isFitToWindow();
    const bool fitWidth = m_canvas->isFitToWidth();
    m_currentImage = m_currentImage.flipped(Qt::Horizontal);
    resetEffect();
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(m_currentImage),
                                      QPointF(1.0 - n.x(), n.y()), z, fitWindow, fitWidth);
}

void MainWindow::onFlipV()
{
    if (m_currentImage.isNull()) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();
    const qreal z = m_canvas->zoom();
    const bool fitWindow = m_canvas->isFitToWindow();
    const bool fitWidth = m_canvas->isFitToWidth();
    m_currentImage = m_currentImage.flipped(Qt::Vertical);
    resetEffect();
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(m_currentImage),
                                      QPointF(n.x(), 1.0 - n.y()), z, fitWindow, fitWidth);
}

void MainWindow::onEffect(int mode)
{
    m_effect = mode;
    applyEffect();
}

void MainWindow::applyEffect()
{
    if (m_originalImage.isNull()) {
        return;
    }
    switch (m_effect) {
    case 1:
        m_currentImage = applySoften(m_originalImage);
        break;
    case 2:
        m_currentImage = applySharpen(applySoften(m_originalImage));
        break;
    default:
        m_currentImage = m_originalImage;
        break;
    }
    m_canvas->setPixmap(QPixmap::fromImage(m_currentImage));
}

void MainWindow::resetEffect()
{
    m_originalImage = m_currentImage;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
}

void MainWindow::onCrop()
{
    m_cropMode = !m_cropMode;
    m_canvas->setCropMode(m_cropMode);
    if (m_cropMode) {
        m_toast->showMessage(tr("拖拽框选裁剪区域，回车确认，Esc 取消"));
    } else {
        m_canvas->clearSelection();
    }
}

void MainWindow::applyCrop()
{
    const QRect r = m_canvas->selectionRect();
    if (r.isEmpty() || r.width() < 2 || r.height() < 2) {
        m_toast->showMessage(tr("请先框选裁剪区域"));
        return;
    }
    m_currentImage = m_currentImage.copy(r);
    showImage(m_currentImage, m_currentPath);
    onCrop(); // 退出裁剪模式
}

void MainWindow::onResize()
{
    if (m_currentImage.isNull()) {
        return;
    }
    bool ok = false;
    const int pct = QInputDialog::getInt(this, tr("调整大小"),
                                         tr("缩放比例（%）："), 100, 1, 400, 1, &ok);
    if (!ok || pct == 100) {
        return;
    }
    const QSize ns = m_currentImage.size() * pct / 100;
    m_currentImage = m_currentImage.scaled(ns, Qt::KeepAspectRatio,
                                            m_smoothInterpolation ? Qt::SmoothTransformation
                                                                  : Qt::FastTransformation);
    showImage(m_currentImage, m_currentPath);
}

// ---------------------------------------------------------------------------
// 文件操作
// ---------------------------------------------------------------------------

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff *.avif *.heic *.dng *.nef *.cr2 *.arw *.rw2 *.orf *.raf *.pef *.raw);;所有文件 (*)"));
    if (!path.isEmpty()) {
        openFile(path);
    }
}

QStringList MainWindow::imageExtensions()
{
    return {
        QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"), QStringLiteral("*.gif"), QStringLiteral("*.webp"),
        QStringLiteral("*.heic"), QStringLiteral("*.avif"),
        QStringLiteral("*.tif"), QStringLiteral("*.tiff"), QStringLiteral("*.ico"),
        QStringLiteral("*.ppm"), QStringLiteral("*.pgm"), QStringLiteral("*.pbm"),
        QStringLiteral("*.xbm"), QStringLiteral("*.xpm"),
        QStringLiteral("*.dng"), QStringLiteral("*.nef"), QStringLiteral("*.cr2"),
        QStringLiteral("*.arw"), QStringLiteral("*.rw2"), QStringLiteral("*.orf"),
        QStringLiteral("*.raf"), QStringLiteral("*.pef"), QStringLiteral("*.raw"),
    };
}

QStringList MainWindow::listImagesInDir(const QString &dirPath)
{
    const QDir dir(dirPath);
    const QStringList entries = dir.entryList(imageExtensions(),
                                              QDir::Files | QDir::Readable,
                                              QDir::Name | QDir::LocaleAware);
    QStringList out;
    out.reserve(entries.size());
    for (const QString &name : entries) {
        out << dir.absoluteFilePath(name);
    }
    return out;
}

void MainWindow::onOpenFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("打开文件夹"));
    if (dir.isEmpty()) {
        return;
    }
    const QStringList files = listImagesInDir(dir);
    if (files.isEmpty()) {
        m_toast->showMessage(tr("该文件夹没有可识别的图片"));
        return;
    }
    openFile(files.first());
}

void MainWindow::onDeleteFile()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    const QString name = QFileInfo(m_currentPath).fileName();
    if (Config::instance().deleteConfirm()) {
        const auto ret = QMessageBox::question(
            this, tr("删除文件"), tr("删除文件 %1？此操作进入回收站").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    if (!QFile::moveToTrash(m_currentPath)) {
        QMessageBox::warning(this, tr("删除失败"), tr("无法删除文件：%1").arg(m_currentPath));
        return;
    }
    if (m_dir->count() > 1) {
        onNext();
    } else {
        m_currentPath.clear();
        m_currentImage = QImage();
        m_canvas->clear();
        updateCanvasLogo();
        m_browser->setFiles({}, -1);
        setWindowTitle(QStringLiteral("QuikView"));
        m_caption->setTitle(tr("未打开图片"));
        m_toast->showMessage(tr("已删除"));
    }
}

void MainWindow::onCopyToFolder()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(this, tr("复制到图片文件夹"));
    if (dir.isEmpty()) {
        return;
    }
    const QString dest = QDir(dir).filePath(QFileInfo(m_currentPath).fileName());
    if (QFileInfo::exists(dest)) {
        QMessageBox::information(this, tr("复制失败"),
                                 tr("目标文件已存在，不覆盖：%1").arg(dest));
        return;
    }
    if (QFile::copy(m_currentPath, dest)) {
        m_toast->showMessage(tr("已复制到：%1").arg(dest));
    } else {
        QMessageBox::warning(this, tr("复制失败"), tr("无法复制到：%1").arg(dest));
    }
}

void MainWindow::onOpenWithEditor()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    const QString editor = Config::instance().settings()
                               .value(QStringLiteral("config/etc/image_editor")).toString();
    if (!editor.isEmpty() && QFile::exists(editor)) {
        QProcess::startDetached(editor, { m_currentPath });
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentPath));
    }
}

void MainWindow::onCopyToClipboard()
{
    if (m_currentImage.isNull()) {
        return;
    }
    QGuiApplication::clipboard()->setImage(m_currentImage);
    m_toast->showMessage(tr("已复制到剪贴板"));
}

void MainWindow::onSetWallpaper()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    QString err;
    if (setDesktopWallpaper(m_currentPath, &err)) {
        m_toast->showMessage(tr("已设为壁纸"));
    } else {
        m_toast->showMessage(tr("设壁纸失败：") + err);
    }
}

void MainWindow::onSaveAs()
{
    if (m_currentImage.isNull()) {
        return;
    }
    const QString out = QFileDialog::getSaveFileName(
        this, tr("另存为"), m_currentPath,
        tr("PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;WebP (*.webp);;TIFF (*.tif *.tiff)"));
    if (out.isEmpty()) {
        return;
    }
    if (m_currentImage.save(out)) {
        m_toast->showMessage(tr("已保存：%1").arg(out));
    } else {
        m_toast->showMessage(tr("保存失败"));
    }
}

void MainWindow::onBatchConvert()
{
    if (m_dir->files().isEmpty()) {
        m_toast->showMessage(tr("当前目录没有可转换的图片"));
        return;
    }
    BatchConvertDialog dlg(m_dir->files(), this);
    dlg.exec();
}

void MainWindow::onPrint()
{
    if (m_currentImage.isNull()) {
        return;
    }
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QPainter painter(&printer);
    const QRect page = painter.viewport();
    const QSize size = m_currentImage.size();
    const QSize fit = size.scaled(page.size(), Qt::KeepAspectRatio);
    const int x = page.x() + (page.width() - fit.width()) / 2;
    const int y = page.y() + (page.height() - fit.height()) / 2;
    painter.setViewport(x, y, fit.width(), fit.height());
    painter.setWindow(m_currentImage.rect());
    painter.drawImage(0, 0, m_currentImage);
    painter.end();
}

void MainWindow::onToggleFavorite()
{
    if (m_currentPath.isEmpty()) {
        return;
    }
    const QStringList favs = Config::instance().favorites();
    const bool wasFav = favs.contains(m_currentPath);
    Config::instance().toggleFavorite(m_currentPath);
    refreshLibraryMenus();
    m_toast->showMessage(wasFav ? tr("已取消收藏") : tr("已收藏"));
}

void MainWindow::refreshLibraryMenus()
{
    auto fill = [this](QMenu *menu, const QStringList &list) {
        if (!menu) return;
        menu->clear();
        if (list.isEmpty()) {
            menu->addAction(tr("（空）"))->setEnabled(false);
            return;
        }
        for (const QString &p : list) {
            menu->addAction(QFileInfo(p).fileName(), this, [this, p]() {
                openFile(p);
            });
        }
    };
    fill(m_favMenu, Config::instance().favorites());
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
    const bool dark = (Config::instance().theme() != QLatin1String("light"));
    Theme::apply(*qApp, dark ? Theme::Mode::Dark : Theme::Mode::Light);
    m_slideshowTimer->setInterval(Config::instance().slideshowIntervalMs());
    applyRuntimeSettings();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于 QuikView"),
                       tr("<h3>QuikView</h3>"
                          "<p>版本 0.3.0</p>"
                          "<p>MIT 许可证开源，零遥测、零推广。</p>"));
}

// ---------------------------------------------------------------------------
// 键盘 / 拖放
// ---------------------------------------------------------------------------

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_Right:
            onNextFrame();
            return;
        case Qt::Key_Left:
            onPrevFrame();
            return;
        default:
            break;
        }
    }
    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_PageUp:
        onPrev();
        return;
    case Qt::Key_Right:
    case Qt::Key_PageDown:
    case Qt::Key_Space:
        onNext();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (m_cropMode) {
            applyCrop();
            return;
        }
        break;
    case Qt::Key_Escape:
        if (m_cropMode) {
            onCrop(); // 取消裁剪
            return;
        }
        if (isFullScreen()) {
            showNormal();
            return;
        }
        if (Config::instance().escExit()) {
            close();
        }
        break;
    case Qt::Key_F11:
        isFullScreen() ? showNormal() : showFullScreen();
        break;
    default:
        break;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        openFile(urls.first().toLocalFile());
    }
}
