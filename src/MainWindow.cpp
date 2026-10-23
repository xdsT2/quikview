#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTimer>
#include <QTransform>
#include <QUrl>

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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // Honeyview 无边框铬件窗口
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("QuikView"));
    resize(970, 806);
    setMinimumSize(500, 400);
    setAcceptDrops(true);

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
    m_caption->setTitle(QStringLiteral("QuikView"));
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
    connect(m_toolbar, &Chrome::ToolBar::lockToggled, this, &MainWindow::toggleAlwaysOnTop);

    m_control = new Chrome::ControlBar(this);
    connect(m_control, &Chrome::ControlBar::openClicked, this, &MainWindow::onOpen);
    connect(m_control, &Chrome::ControlBar::listClicked, this, [this]() {
        m_browser->setVisible(!m_browser->isVisible());
        if (m_browser->isVisible()) {
            // show 后顶层 layout 会拉回 sizeHint，须重设面板几何
            m_browser->setGeometry(6 + 8, 62 + 8, 260, height() - 62 - 6 - 16);
            m_browser->raise();
        }
    });
    connect(m_control, &Chrome::ControlBar::zoomInClicked, this, &MainWindow::onZoomIn);
    connect(m_control, &Chrome::ControlBar::zoomOutClicked, this, &MainWindow::onZoomOut);
    connect(m_control, &Chrome::ControlBar::rotateLeftClicked, this, &MainWindow::onRotateLeft);
    connect(m_control, &Chrome::ControlBar::rotateRightClicked, this, &MainWindow::onRotateRight);
    connect(m_control, &Chrome::ControlBar::lockToggled, this, &MainWindow::toggleAlwaysOnTop);
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
    });

    m_toast = new Chrome::Toast(this);
    m_zoomBadge = new Chrome::ZoomBadge(this);

    createActions();
    applyRuntimeSettings();
    applyChromeLayout();
    updateCanvasLogo();
}

MainWindow::~MainWindow()
{
    delete m_frames;
}

// ---------------------------------------------------------------------------
// 布局：外框 6px / 标题栏 25 / 工具栏 31 / 画布 client；
// 胶囊栏 460x80 浮于画布下缘中央（距底 26px）——尺寸沿用 5.53 的可观察行为
// ---------------------------------------------------------------------------

void MainWindow::applyChromeLayout()
{
    if (!m_canvas || !m_caption) {
        return;
    }
    const int fw = 6;                       // 外框厚
    const int capH = 25, tbH = 31;
    const bool full = isFullScreen();
    const int top = full ? 0 : fw;

    m_caption->setVisible(!full);
    m_toolbar->setVisible(!full);

    const int x = full ? 0 : fw;
    const int w = width() - (full ? 0 : 2 * fw);
    int canvasTop = top;
    if (!full) {
        m_caption->setGeometry(x, fw, w, capH);
        m_toolbar->setGeometry(x, fw + capH, w, tbH);
        canvasTop = fw + capH + tbH;
    }
    const int canvasH = height() - canvasTop - (full ? 0 : fw);
    m_canvas->setGeometry(x, canvasTop, w, canvasH);

    if (m_control) {
        // 500 容器窗口居中 + 左 padding 30 → 胶囊中心右偏 ~15 逻辑 px（布局参数）
        m_control->move(x + (w - m_control->width()) / 2 + 18,
                        canvasTop + canvasH - m_control->height() - 26);
        m_control->raise();
    }
    if (m_toast) {
        m_toast->setGeometry(x, canvasTop, w, 56);
        m_toast->raise();
    }
    if (m_zoomBadge) {
        m_zoomBadge->move(x + 12, canvasTop + canvasH - m_zoomBadge->height() - 12);
        m_zoomBadge->raise();
    }
    if (m_browser && m_browser->isVisible()) {
        m_browser->setGeometry(x + 8, canvasTop + 8, 260, canvasH - 16);
        m_browser->raise();
    }
    if (m_metadata && m_metadata->isVisible() && m_metadata->isFloating()) {
        m_metadata->resize(320, height() / 2);
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
    // 6px 外框（border_*.png 拉伸；均值 #ababab）
    QPainter p(this);
    const int fw = 6;
    const QPixmap left = Chrome::skinPixmap(QStringLiteral("border_left.png"));
    const QPixmap right = Chrome::skinPixmap(QStringLiteral("border_right.png"));
    const QPixmap bottom = Chrome::skinPixmap(QStringLiteral("border_bottom.png"));
    const QPixmap top = Chrome::skinPixmap(QStringLiteral("border_top.png"));
    const int h = height();
    const int w = width();
    if (!left.isNull()) {
        p.drawPixmap(QRect(0, 0, fw, h), left, QRect(0, 0, left.width(), left.height()));
    } else {
        p.fillRect(0, 0, fw, h, QColor("#ababab"));
    }
    if (!right.isNull()) {
        p.drawPixmap(QRect(w - fw, 0, fw, h), right, QRect(0, 0, right.width(), right.height()));
    } else {
        p.fillRect(w - fw, 0, fw, h, QColor("#ababab"));
    }
    if (!bottom.isNull()) {
        p.drawPixmap(QRect(0, h - fw, w, fw), bottom, QRect(0, 0, bottom.width(), bottom.height()));
    } else {
        p.fillRect(0, h - fw, w, fw, QColor("#ababab"));
    }
    if (!top.isNull()) {
        p.drawPixmap(QRect(0, 0, w, fw), top, QRect(0, 0, top.width(), top.height()));
    } else {
        p.fillRect(0, 0, w, fw, QColor("#ababab"));
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
    actPrint = mk(tr("打印(&P)..."), QKeySequence::Print, &MainWindow::onPrint);
    actBatchConvert = mk(tr("批量转换(&B)"), {}, &MainWindow::onBatchConvert);
    actSettings = mk(tr("设置(&C)"), QKeySequence(QStringLiteral("F9")), &MainWindow::onSettings);
    actFavorite = mk(tr("收藏/取消收藏(&F)"), QKeySequence(QStringLiteral("F2")), &MainWindow::onToggleFavorite);
    actDelete = mk(tr("删除(&D)"), QKeySequence::Delete, &MainWindow::onDeleteFile);
    actCopyToFolder = mk(tr("复制到 图片文件夹..."), QKeySequence(QStringLiteral("Ins")), &MainWindow::onCopyToFolder);
    actOpenWithEditor = mk(tr("用图像编辑器打开"), QKeySequence(QStringLiteral("Ctrl+E")), &MainWindow::onOpenWithEditor);
    actCopyToClipboard = mk(tr("复制到剪贴板"), QKeySequence::Copy, &MainWindow::onCopyToClipboard);
    actWallpaper = mk(tr("设为壁纸(&W)"), {}, &MainWindow::onSetWallpaper);
    actAbout = mk(tr("关于 QuikView"), {}, &MainWindow::onAbout);
    actQuit = new QAction(tr("退出(&Q)"), this);
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);

    // 视图（浏览方式单选：原始 0 / 适合窗口 9 / 适合宽度 8，对照原版快捷键）
    auto *viewModeGroup = new QActionGroup(this);
    viewModeGroup->setExclusive(true);
    actActualSize = mk(tr("原始大小 (100%)\t0"), QKeySequence(QStringLiteral("0")), &MainWindow::onActualSize);
    actFit = mk(tr("适合窗口\t9"), QKeySequence(QStringLiteral("9")), &MainWindow::onFit);
    actFitWidth = mk(tr("适合宽度\t8"), QKeySequence(QStringLiteral("8")), &MainWindow::onFitWidth);
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

    // 图像效果（三选一）
    auto *effectGroup = new QActionGroup(this);
    effectGroup->setExclusive(true);
    actEffectNone = new QAction(tr("无"), this);
    actEffectSoften = new QAction(tr("减淡"), this);
    actEffectSharpen = new QAction(tr("减淡+锐化"), this);
    for (auto *a : { actEffectNone, actEffectSoften, actEffectSharpen }) {
        a->setCheckable(true);
        effectGroup->addAction(a);
    }
    actEffectNone->setChecked(true);
    connect(actEffectNone, &QAction::triggered, this, [this]() { onEffect(0); });
    connect(actEffectSoften, &QAction::triggered, this, [this]() { onEffect(1); });
    connect(actEffectSharpen, &QAction::triggered, this, [this]() { onEffect(2); });

    // 插值算法（二选一）
    auto *interpGroup = new QActionGroup(this);
    interpGroup->setExclusive(true);
    actInterpFast = new QAction(tr("快速"), this);
    actInterpSmooth = new QAction(tr("平滑"), this);
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
// 工具栏下拉菜单（对照原版：浏览/幻灯片放映/书签/编辑/复制到/锁定）
// ---------------------------------------------------------------------------

QMenu *MainWindow::buildBrowseMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actActualSize);
    menu->addAction(actFit);
    menu->addAction(actFitWidth);
    menu->addSeparator();
    menu->addAction(actSlideshow);
    menu->addAction(actNextFrame);
    menu->addAction(actPrevFrame);
    menu->addSeparator();
    auto *sort = menu->addMenu(tr("排序方式"));
    sort->addAction(actSortName);
    sort->addAction(actSortTime);
    sort->addAction(actSortSize);
    sort->addAction(actSortType);
    sort->addSeparator();
    sort->addAction(actSortDesc);
    auto *interp = menu->addMenu(tr("插值算法"));
    interp->addAction(actInterpFast);
    interp->addAction(actInterpSmooth);
    auto *fx = menu->addMenu(tr("图像效果"));
    fx->addAction(actEffectNone);
    fx->addAction(actEffectSoften);
    fx->addAction(actEffectSharpen);
    menu->addSeparator();
    menu->addAction(actDark);
    return menu;
}

QMenu *MainWindow::buildSlideshowMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actSlideshow);
    menu->addSeparator();
    // 随机播放（对照原版 MENUSTR_SLIDESHOW_RANDOM）
    auto *randAction = menu->addAction(tr("随机"));
    randAction->setCheckable(true);
    randAction->setChecked(m_slideshowRandom);
    connect(randAction, &QAction::toggled, this, [this](bool on) {
        m_slideshowRandom = on;
        Config::instance().settings().setValue(QStringLiteral("config/slideshow_random"), on);
    });
    // 间隔秒数
    auto *intervalMenu = menu->addMenu(tr("放映间隔"));
    const QList<int> secs = { 1, 2, 3, 5, 10, 15, 30, 60, 90 };
    auto *group = new QActionGroup(menu);
    group->setExclusive(true);
    const int cur = Config::instance().slideshowIntervalMs() / 1000;
    for (int s : secs) {
        auto *a = intervalMenu->addAction(s == 1 ? tr("1 秒") : tr("%1 秒").arg(s));
        a->setCheckable(true);
        a->setChecked(cur == s);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, s]() {
            Config::instance().settings().setValue(QStringLiteral("config/slideshow_interval_ms"), s * 1000);
            m_slideshowTimer->setInterval(s * 1000);
            if (m_slideshow) {
                m_slideshowTimer->start();
            }
        });
    }
    // 到达首尾行为
    auto *edge = menu->addMenu(tr("到达首尾图片时"));
    const QString curEdge = Config::instance().edgeAction();
    const QList<QPair<QString, QString>> edges = {
        { "first", tr("转到首个文件") }, { "stop", tr("停止") }, { "ask", tr("询问") },
    };
    for (const auto &e : edges) {
        auto *a = edge->addAction(e.second);
        a->setCheckable(true);
        a->setChecked(curEdge == e.first);
        connect(a, &QAction::triggered, this, [this, e]() {
            Config::instance().settings().setValue(QStringLiteral("config/edge_action"), e.first);
            m_dir->setEdgeAction(e.first);
        });
    }
    return menu;
}

QMenu *MainWindow::buildBookmarkMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actFavorite);
    if (!m_favMenu) {
        m_favMenu = new QMenu(tr("收藏夹"), this);
    }
    refreshLibraryMenus();
    menu->addMenu(m_favMenu);
    menu->addSeparator();
    menu->addAction(tr("打开设置（书签页）"), this, &MainWindow::onSettings);
    return menu;
}

QMenu *MainWindow::buildEditMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actRotateLeft);
    menu->addAction(actRotateRight);
    menu->addAction(actFlipV);
    menu->addAction(actFlipH);
    menu->addSeparator();
    menu->addAction(actCrop);
    menu->addAction(actResize);
    menu->addSeparator();
    menu->addAction(actBatchConvert);
    menu->addAction(actWallpaper);
    menu->addAction(actPrint);
    menu->addAction(actOpenWithEditor);
    return menu;
}

QMenu *MainWindow::buildCopytoMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actCopyToFolder);
    menu->addAction(actDelete);
    menu->addSeparator();
    menu->addAction(actCopyToClipboard);
    menu->addAction(actSaveAs);
    menu->addSeparator();
    menu->addAction(tr("打开图片文件夹"), this, [this]() {
        const QString d = Config::instance().settings()
                              .value(QStringLiteral("config/etc/photo_folder_1")).toString();
        if (!d.isEmpty()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(d));
        } else {
            m_toast->showMessage(tr("请先在设置 → 杂项 中配置图片文件夹"));
        }
    });
    menu->addAction(actSettings);
    return menu;
}

QMenu *MainWindow::buildSystemMenu()
{
    auto *menu = new QMenu(this);
    menu->addAction(actOpen);
    menu->addAction(actOpenFolder);
    menu->addSeparator();
    menu->addAction(actSettings);
    menu->addAction(actAbout);
    menu->addSeparator();
    menu->addAction(actQuit);
    return menu;
}

QMenu *MainWindow::buildContextMenu()
{
    auto *menu = new QMenu(this);

    menu->addAction(actOpen);
    menu->addAction(actOpenFolder);
    menu->addSeparator();
    menu->addAction(actToggleMeta);
    menu->addAction(actWallpaper);
    menu->addSeparator();
    menu->addAction(actDelete);
    menu->addAction(actCopyToFolder);
    menu->addAction(actOpenWithEditor);
    menu->addAction(actPrint);
    if (Config::instance().contextCopy()) {
        menu->addAction(actCopyToClipboard);
    }
    menu->addSeparator();

    QMenu *rot = menu->addMenu(tr("旋转"));
    rot->addAction(actRotateLeft);
    rot->addAction(actRotateRight);
    rot->addAction(actFlipH);
    rot->addAction(actFlipV);

    QMenu *effect = menu->addMenu(tr("图像效果"));
    effect->addAction(actEffectNone);
    effect->addAction(actEffectSoften);
    effect->addAction(actEffectSharpen);

    menu->addAction(actCrop);
    menu->addAction(actResize);
    menu->addSeparator();
    // 浏览导航子菜单（原版 MENUSTR_PAGEMOVE）
    QMenu *nav = menu->addMenu(tr("浏览导航"));
    nav->addAction(actPrev);
    nav->addAction(actNext);
    nav->addAction(actFirst);
    nav->addAction(actLast);
    nav->addSeparator();
    nav->addAction(actPrev10);
    nav->addAction(actNext10);
    nav->addSeparator();
    nav->addAction(actPrevFolder);
    nav->addAction(actNextFolder);
    menu->addAction(actSlideshow);
    menu->addSeparator();
    menu->addAction(actSettings);
    menu->addAction(actAbout);
    menu->addAction(actQuit);

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
    m_canvas->setLogoVisible(m_currentImage.isNull());
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
    else if (c == QLatin1String("slideshow"))  actSlideshow->trigger();
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
        m_browser->setVisible(!m_browser->isVisible());
        if (m_browser->isVisible()) {
            // QWidget 顶层 layout 会在 show 时把尺寸拉回 sizeHint，须在
            // 显示后重设面板几何（与 applyChromeLayout 同公式）。
            m_browser->setGeometry(6 + 8, 62 + 8, 260, height() - 62 - 6 - 16);
            m_browser->raise();
        }
    }
    else if (c == QLatin1String("fullscreen")) isFullScreen() ? showNormal() : showFullScreen();
}

void MainWindow::onPrev()
{
    const QString p = m_dir->prev();
    if (!p.isEmpty()) loadAsync(p);
}

void MainWindow::onNext()
{
    const QString p = m_dir->next();
    if (!p.isEmpty()) loadAsync(p);
}

// 10 张跳转：边界停住并提示（TEXT_MSG_FIRSTPAGE/LASTPAGE）
void MainWindow::onPrev10()
{
    const QString p = m_dir->prev10();
    if (!p.isEmpty()) {
        loadAsync(p);
    } else if (m_dir->isValid()) {
        m_toast->showMessage(tr("第一张图片"));
    }
}

void MainWindow::onNext10()
{
    const QString p = m_dir->next10();
    if (!p.isEmpty()) {
        loadAsync(p);
    } else if (m_dir->isValid()) {
        m_toast->showMessage(tr("最后一张图片"));
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

void MainWindow::onRotateLeft()
{
    QTransform t; t.rotate(-90);
    m_currentImage = m_currentImage.transformed(t);
    resetEffect();
    m_canvas->setPixmap(QPixmap::fromImage(m_currentImage));
}

void MainWindow::onRotateRight()
{
    QTransform t; t.rotate(90);
    m_currentImage = m_currentImage.transformed(t);
    resetEffect();
    m_canvas->setPixmap(QPixmap::fromImage(m_currentImage));
}

void MainWindow::onFlipH()
{
    m_currentImage = m_currentImage.flipped(Qt::Horizontal);
    resetEffect();
    m_canvas->setPixmap(QPixmap::fromImage(m_currentImage));
}

void MainWindow::onFlipV()
{
    m_currentImage = m_currentImage.flipped(Qt::Vertical);
    resetEffect();
    m_canvas->setPixmap(QPixmap::fromImage(m_currentImage));
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
        m_caption->setTitle(QStringLiteral("QuikView"));
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
