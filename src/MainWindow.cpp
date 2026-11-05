#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QColorSpace>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEasingCurve>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QScreen>
#include <QStyleHints>
#include <cmath>
#include <QResizeEvent>
#include <QTextStream>
#include <QTimer>
#include <QToolTip>
#include <QTransform>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "BatchConvertDialog.h"
#include "Chrome.h"

#include <functional>
#include "Config.h"
#include "DirectoryLoader.h"
#include "FrameNavigator.h"
#include "GammaDialog.h"
#include "HeifLoader.h"
#include "ImageCache.h"
#include "ImageCanvas.h"
#include "ImageEffects.h"
#include "MetadataPanel.h"
#include "ToneMap.h"
#include "RawImageLoader.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "ThumbnailBrowser.h"
#include "Wallpaper.h"

// 版本号由 CMakeLists 的 project(VERSION ...) 注入（见 QUIKVIEW_VERSION），
// 这里只做兜底，避免脱离 CMake 直接编译时报未定义。
#ifndef QUIKVIEW_VERSION
#define QUIKVIEW_VERSION "0.0.0"
#endif

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
namespace {
inline short xFromLparam(long v) { return static_cast<short>(LOWORD(v)); }
inline short yFromLparam(long v) { return static_cast<short>(HIWORD(v)); }
}
#endif

namespace {
constexpr int kBrowserWidth = 160;   // 缩略图面板宽（20:53：小而随内容）
constexpr int kBrowserGap = 12;      // 面板与窗口左缘/画布之间的留白
constexpr int kTopChromeH = 76;      // 标题栏 28 + 命令栏 34 + 视觉留白（仅作面板/浮层定位）

// 双页拼图：两页等高并排，高度取较高的一页，另一页按比例放大，
// 避免跨页出现深色竖缝。左←右（漫画式）由调用方调换左右顺序。
QImage composeSpread(const QImage &left, const QImage &right)
{
    if (left.isNull()) {
        return right;
    }
    if (right.isNull()) {
        return left;
    }
    const int h = qMax(left.height(), right.height());
    const QImage l = left.scaledToHeight(h, Qt::SmoothTransformation);
    const QImage r = right.scaledToHeight(h, Qt::SmoothTransformation);
    QImage out(l.width() + r.width(), h, QImage::Format_ARGB32);
    out.fill(Qt::black);
    QPainter p(&out);
    p.drawImage(0, 0, l);
    p.drawImage(l.width(), 0, r);
    return out;
}

// 屏幕保护禁用（设置页「运行 QuikView 时禁用屏幕保护程序」）：开 = 持续申请
// ES_DISPLAY_REQUIRED（显示器常亮），关/程序退出 = 还原 ES_CONTINUOUS。
// ES_* 请求绑定在调用线程（GUI 线程），进程退出时系统自动清除，这里仍显式还原。
void applyScreensaverPrevention(bool on)
{
#ifdef Q_OS_WIN
    SetThreadExecutionState(on ? (ES_CONTINUOUS | ES_DISPLAY_REQUIRED) : ES_CONTINUOUS);
#else
    Q_UNUSED(on);
#endif
}

// 翻页音效（设置「翻页时播放音效」，键 "config/etc/pageturn_wav"）：
// 首次使用时在内存里合成一段 40ms 衰减正弦 WAV（8kHz / 16bit / 单声道，
// 含 RIFF 头），之后每次经 winmm PlaySoundW 以 SND_MEMORY 异步播放。
// 不落仓库、不产生任何音频素材文件；非 Windows 平台为空操作。
void playPageturnTone()
{
    if (!Config::instance().pageturnWav()) {
        return;
    }
#ifdef Q_OS_WIN
    // 静态持有字节流：SND_MEMORY 异步播放期间内存必须保持有效
    static const QByteArray kPageturnWav = []() {
        constexpr int kSampleRate = 8000;
        const int n = kSampleRate * 40 / 1000;
        QByteArray wav;
        auto put16 = [&wav](quint16 v) {
            wav.append(char(v & 0xFF));
            wav.append(char((v >> 8) & 0xFF));
        };
        auto put32 = [&wav, &put16](quint32 v) {
            put16(quint16(v & 0xFFFF));
            put16(quint16((v >> 16) & 0xFFFF));
        };
        const quint32 dataBytes = quint32(n) * 2;
        wav.append("RIFF", 4);
        put32(36 + dataBytes);
        wav.append("WAVE", 4);
        wav.append("fmt ", 4);
        put32(16);
        put16(1);                              // PCM
        put16(1);                              // 单声道
        put32(quint32(kSampleRate));
        put32(quint32(kSampleRate) * 2);       // 字节率 = 采样率 × 块对齐
        put16(2);                              // 块对齐
        put16(16);                             // 位深
        wav.append("data", 4);
        put32(dataBytes);
        for (int i = 0; i < n; ++i) {
            const double t = double(i) / kSampleRate;
            const double env = std::exp(-34.0 * t);
            const qint16 s = qint16(0.55 * env
                                    * std::sin(2.0 * 3.14159265358979323846 * 2200.0 * t)
                                    * 32767.0);
            wav.append(char(s & 0xFF));
            wav.append(char((s >> 8) & 0xFF));
        }
        return wav;
    }();
    // SND_MEMORY：首参是 WAV 字节流内存指针；SND_NODEFAULT：失败不回落系统提示音
    PlaySoundW(reinterpret_cast<LPCWSTR>(kPageturnWav.constData()),
               nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
#else
    // 非 Windows 无 winmm，空操作
#endif
}

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
    qApp->installEventFilter(this);
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

    // 动图自动播放（P0：GIF）。singleShot 单发推进，每次 tick 按
    // 「下一帧绝对时间点」重设间隔，实现累计误差校正。
    m_animTimer = new QTimer(this);
    m_animTimer->setSingleShot(true);
    connect(m_animTimer, &QTimer::timeout, this, &MainWindow::animationTick);

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
    connect(m_caption, &Chrome::CaptionBar::fullscreenClicked, this, &MainWindow::toggleFullscreen);
    connect(m_caption, &Chrome::CaptionBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_caption, &Chrome::CaptionBar::maximizeClicked, this, [this]() {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_caption, &Chrome::CaptionBar::closeClicked, this, &QWidget::close);
    connect(m_caption, &Chrome::CaptionBar::systemMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSystemMenu();
        menu->exec(g);
        menu->deleteLater();
    });

    m_toolbar = new Chrome::ToolBar(this);
    connect(m_toolbar, &Chrome::ToolBar::openClicked, this, &MainWindow::onOpen);
    connect(m_toolbar, &Chrome::ToolBar::browseMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildBrowseMenu();
        menu->exec(g);
        menu->deleteLater();
    });
    connect(m_toolbar, &Chrome::ToolBar::slideshowMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSlideshowMenu();
        menu->exec(g);
        menu->deleteLater();
    });
    connect(m_toolbar, &Chrome::ToolBar::bookmarkMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildBookmarkMenu();
        menu->exec(g);
        menu->deleteLater();
    });
    connect(m_toolbar, &Chrome::ToolBar::editMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildEditMenu();
        menu->exec(g);
        menu->deleteLater();
    });
    connect(m_toolbar, &Chrome::ToolBar::copytoMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildCopytoMenu();
        menu->exec(g);
        menu->deleteLater();
    });
    connect(m_toolbar, &Chrome::ToolBar::lockToggled, this, &MainWindow::toggleTopLock);
    // HDR 色调映射开关（activate 已翻转 isHdr，这里只持久化 + 让当前图走一遍新管线）
    connect(m_toolbar, &Chrome::ToolBar::hdrToggled, this, [this]() {
        m_hdrTonemap = m_toolbar->isHdr();
        Config::instance().settings().setValue(QStringLiteral("config/hdr_tonemap"),
                                               m_hdrTonemap);
        m_cache->clearFull();   // 解码输出随开关变化，原图缓存全部失效
        if (!m_currentPath.isEmpty()) {
            loadAsync(m_currentPath);
        }
        m_toast->showMessage(m_hdrTonemap ? tr("HDR 已开启") : tr("HDR 已关闭"));
    });
    m_toolbar->setHdr(Config::instance().hdrTonemap());
    m_hdrTonemap = Config::instance().hdrTonemap();
    connect(m_toolbar, &Chrome::ToolBar::moreMenuRequested, this, [this](const QPoint &g) {
        QMenu *menu = buildSystemMenu();
        menu->exec(g);
        menu->deleteLater();
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
    // 翻页滑杆：拖动中不发信号，松手才跳一次图（1-based）
    connect(m_control, &Chrome::ControlBar::pageRequested, this, [this](int index) {
        if (!m_dir || !m_dir->isValid() || index < 1 || index > m_dir->count()) {
            return;
        }
        if (index - 1 == m_dir->index()) {
            return;
        }
        m_dir->setIndex(index - 1);
        jumpTo(m_dir->current());
    });
    // 拖动中跟手预览：经过的每一张都出一帧（Chrome 侧已按 ≥60ms 节流）。
    // 不碰 m_dir 索引、不写元数据，松手仍由上面的 pageRequested 走一次全图跳转。
    connect(m_control, &Chrome::ControlBar::pagePreviewRequested, this, [this](int index) {
        if (!m_dir || !m_dir->isValid() || index < 1 || index > m_dir->count()) {
            return;
        }
        if (m_preloadTimer) {
            m_preloadTimer->stop();   // 拖动中不预读，让出解码给跟手预览
        }
        m_cache->requestDragPreview(m_dir->files().at(index - 1),
                                    m_canvas ? m_canvas->viewport()->size() : QSize());
    });
    connect(m_cache, &ImageCache::dragPreviewReady, this, &MainWindow::onDragPreviewReady);

    // 画布两侧箭头带点击翻页（对齐原版 40px 侧边带）：首尾行为由
    // onPrev/onNext 复用 config/edge_action，不另写一套
    connect(m_canvas, &ImageCanvas::edgeTurnRequested, this, [this](int delta) {
        if (delta < 0) {
            onPrev();
        } else {
            onNext();
        }
    });

    // 中键单击（位移 <3px 且 <300ms）= 全屏/还原；按住拖动仍由画布平移处理
    connect(m_canvas, &ImageCanvas::middleClickRequested,
            this, &MainWindow::toggleFullscreen);

    // 左键双击（设置「双击最大化 / 还原」开启时）= 窗口最大化/还原。
    // 画布已处理与边缘单击翻页的冲突：220ms 内收到双击会先取消翻页。
    connect(m_canvas, &ImageCanvas::doubleClickRequested, this, [this]() {
        if (isMaximized()) {
            showNormal();
        } else {
            showMaximized();
        }
    });

    // 鼠标按键映射（C9~C12）：画布只负责判定，动作统一在这里派发。
    connect(m_canvas, &ImageCanvas::mouseActionRequested,
            this, &MainWindow::onMouseAction);

    // 画布缩放联动：左下角浮标 + 浏览方式图标降级（胶囊滑杆已是翻页进度，不再跟随缩放）
    connect(m_canvas, &ImageCanvas::zoomChanged, this, [this](qreal z) {
        // SET-03 即时预览：设置页会话内以运行态覆盖值为准（不写配置）。
        const bool showZoom = m_liveZoomControlOverrideSet
                                  ? m_liveZoomControlOverride
                                  : Config::instance().showZoomControl();
        if (m_zoomBadge && !m_currentImage.isNull() && showZoom) {
            refreshZoomBadge(z);
        }
        // 保存缩放状态（键 "config/view/save_zoom_state"）：把当前缩放按路径记下来，
        // 下次打开同一张图时恢复。装图/适配/拖动预览期间的 zoomChanged 是「程序自己
        // 算出来的」，必须屏蔽，否则一打开就把用户上次的 300% 覆盖成 fit。
        if (!m_zoomSaveGuard && Config::instance().saveZoomState() && !m_currentPath.isEmpty()) {
            const QString st = m_canvas->isFitToWindow()  ? QStringLiteral("fit")
                             : m_canvas->isFitToWidth()   ? QStringLiteral("width")
                                                         : QString::number(z, 'f', 4);
            Config::instance().setSavedViewState(m_currentPath, st);
        }
        // 这里绝不能把「非 fit」推断成 m_zoomMode=0：滚轮手动放大同样走
        // zoomChanged，误改后翻页 setZoomMode(0) 就变成 100% 显示。
        // 缩放方式只在显式 setZoomMode()（菜单/快捷键/冒烟命令）里变，滚轮只是
        // 临时缩放——翻页后恢复当前浏览方式（Honeyview 基准：放大看细节，
        // 翻下一张回到适应窗口）。
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

    // 淡出结束才算 Hidden：动画途中不销毁、不重建、不切控件，避免出现闪烁。
    connect(m_dockFade, &QPropertyAnimation::finished, this, [this]() {
        if (m_dockOpacity && qFuzzyCompare(m_dockOpacity->opacity() + 1.0, 0.0 + 1.0)) {
            m_control->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_control->hide();
            m_dockPhase = ChromePhase::Hidden;
        } else {
            m_dockPhase = ChromePhase::Visible;
        }
    });
    connect(m_captionFade, &QPropertyAnimation::finished, this, [this]() {
        const bool hidden = m_captionOpacity
                            && qFuzzyCompare(m_captionOpacity->opacity() + 1.0, 0.0 + 1.0);
        if (hidden) {
            if (m_caption) {
                m_caption->hide();
            }
            if (m_toolbar) {
                m_toolbar->hide();
            }
            m_topPhase = ChromePhase::Hidden;
        } else {
            m_topPhase = ChromePhase::Visible;
        }
    });

    // 离开热区后延迟淡出：进出边缘时不会因为一两帧抖动就闪一下。
    m_topHideTimer = new QTimer(this);
    m_topHideTimer->setSingleShot(true);
    m_topHideTimer->setInterval(150);
    connect(m_topHideTimer, &QTimer::timeout, this, [this]() {
        if (m_topPhase == ChromePhase::Visible || m_topPhase == ChromePhase::Showing) {
            setTopChromeVisible(false);
            m_topPhase = ChromePhase::Hiding;
        }
    });

    m_dockHideTimer = new QTimer(this);
    m_dockHideTimer->setSingleShot(true);
    m_dockHideTimer->setInterval(150);
    connect(m_dockHideTimer, &QTimer::timeout, this, [this]() {
        if (m_dockPhase == ChromePhase::Visible || m_dockPhase == ChromePhase::Showing) {
            setDockVisible(false);
            m_dockPhase = ChromePhase::Hiding;
        }
    });

    m_chromeTimer = new QTimer(this);
    m_chromeTimer->setInterval(60);
    connect(m_chromeTimer, &QTimer::timeout, this, &MainWindow::updateChromeProximity);

    // 空闲预读：单次 300ms，每次翻页（loadAsync）重启；到点时队列还没清空就再等一轮
    m_preloadTimer = new QTimer(this);
    m_preloadTimer->setSingleShot(true);
    m_preloadTimer->setInterval(300);
    connect(m_preloadTimer, &QTimer::timeout, this, &MainWindow::preloadNeighbors);

    createActions();
    applyRuntimeSettings();
    applyChromeLayout();
    updateCanvasLogo();
    m_chromeTimer->start();
}

MainWindow::~MainWindow()
{
    // 硬规则：关窗必须停定时器 + 释放 reader（FrameNavigator 析构内 clear），
    // 否则退出后解码循环还在跑，CPU 不归零。
    stopAnimation();
    delete m_frames;
    // 若曾禁用屏幕保护，退出时还原（进程退出本会自动清除，这里显式做一遍）
    applyScreensaverPrevention(false);
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
        // 通知位置由 config/notify_corner 决定：开=右下角（抬高避开胶囊），关=顶部居中
        m_toast->setCorner(Config::instance().notifyCorner());
        m_toast->reposition();
        m_toast->raise();
    }
    if (m_zoomBadge) {
        m_zoomBadge->move(x + 12, y + h - m_zoomBadge->height() - 34);
        m_zoomBadge->raise();
    }
    if (m_browser && m_browser->isVisible()) {
        // 高度随卡片数量，最多占到窗口底部（面板不再强制满高，也不再为胶囊让位）
        const int avail = h - 76 - kBrowserGap;
        m_browser->setGeometry(x + kBrowserGap, y + 76, kBrowserWidth,
                               qMin(m_browser->preferredHeight(), avail));
        m_browser->raise();
    }
    // 图片信息：左上覆盖层（原型 .info-overlay）。缩略图面板开合跟着横移，
    // 不遮挡面板（面板右缘 12+160=172，浮层 196 起）；全屏时贴顶 10px。
    // 宽度是浮层自己算好的自然宽（钳在 280..440）。2026-11-04 定稿：高度就是
    // 内容自然高，不钳可用高度、不内部滚动——内容比画面高就让画面截断底边
    // （用户口头修订覆盖工单 1.3 的滚动条款）。
    if (m_metadata && m_metadata->isVisible()) {
        const bool browserOpen = m_browser && m_browser->isVisible();
        const int left = browserOpen ? 196 : 14;
        const int top = full ? 10 : 70;
        const QSize hint = m_metadata->sizeHint();
        m_metadata->setGeometry(x + left, y + top, hint.width(), hint.height());
        m_metadata->raise();
    }

    m_caption->raise();
    m_toolbar->raise();
}

// 画布可视区：21:15 交办——图片客户区上下恒为 0，画布直接通顶通底，
// 顶部栏 / 胶囊 / 左下信息 / 右下序号全部作为浮层覆盖，不占布局。
// 只有缩略图面板展开时让出左侧（160 + 两侧 12 = 184）。
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
    const QMargins margins(left, 0, 0, 0);
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
        const int avail = height() - 2 * fw - 76 - kBrowserGap;
        m_browser->setGeometry(fw + kBrowserGap, fw + 76, kBrowserWidth,
                               qMin(m_browser->preferredHeight(), avail));
        m_browser->raise();
    }
    updateCanvasInsets();
    applyChromeLayout();            // 面板开合改变浮层避让位置（14/196），浮层开着时必须重排
    m_canvas->refreshEdgeHover();   // 视口被面板挤压/释放，按真实光标重算箭头
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
        // 21:15：鼠标命中只在这一帧「要显示」时才打开；隐藏要等淡出动画结束
        // （finished 回调里关命中并 hide），动画中途仍可点，避免边淡边吃事件。
        if (visible) {
            widget->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        }
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
            if (!visible) {
                widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
                widget->hide();
            }
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
    if (visible) {
        m_control->setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
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
        // 不做动画时不会有 finished 回调，必须在这里把隐藏彻底收尾，
        // 否则透明但仍在的胶囊会继续吃鼠标。
        if (!visible) {
            m_control->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            m_control->hide();
        }
    }
}

void MainWindow::toggleTopLock()
{
    m_topLocked = !m_topLocked;
    if (m_toolbar) {
        m_toolbar->setLocked(m_topLocked);
    }
    setTopChromeVisible(true);
    m_topPhase = ChromePhase::Visible;
    m_topHideTimer->stop();
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
    m_dockPhase = ChromePhase::Visible;
    m_dockHideTimer->stop();
    if (m_toast) {
        m_toast->showMessage(m_dockLocked ? tr("底部控制胶囊已锁定")
                                          : tr("底部控制胶囊已解锁"));
    }
}

// 热区判定统一从这里取「鼠标在窗口内的位置」。默认就是真实光标；
// --smoke hover:x:y 会灌入一个合成坐标，让滞回/防闪烁能被复现地验证
// （外部挪鼠标的时序跟窗口位置都不稳，测出来不可信）。
QPoint MainWindow::probeLocalPoint() const
{
    return m_probeCursorActive ? m_probeCursor : mapFromGlobal(QCursor::pos());
}

void MainWindow::updateChromeProximity()
{
    // 21:15：只要窗口可见就响应热区，不再要求 isActiveWindow()——非前台可见窗口
    // 同样要能靠悬停唤出铬件，而且这里从不 activateWindow()，不会抢焦点。
    if (!isVisible() || isMinimized()) {
        return;
    }
    updateTopProximity();
    updateDockProximity();
}

// 顶部两条：进入窗口上方 76px 显示，退到 96px 以下才隐藏（20px 滞回）。
void MainWindow::updateTopProximity()
{
    if (!m_caption) {
        return;
    }
    const QPoint local = probeLocalPoint();
    const bool inside = rect().contains(local);

    if (m_topLocked) {
        m_topHideTimer->stop();
        if (m_topPhase != ChromePhase::Visible && m_topPhase != ChromePhase::Showing) {
            setTopChromeVisible(true, false);
            m_topPhase = ChromePhase::Visible;
        }
        return;
    }

    const bool shown = (m_topPhase == ChromePhase::Visible || m_topPhase == ChromePhase::Showing);
    const bool want = inside && local.y() <= (shown ? 96 : 76);

    if (want) {
        m_topHideTimer->stop();
        if (m_topPhase == ChromePhase::Hidden || m_topPhase == ChromePhase::Hiding) {
            setTopChromeVisible(true);
            m_topPhase = ChromePhase::Showing;
        }
    } else if (shown && !m_topHideTimer->isActive()) {
        m_topHideTimer->start();
    }
}

// 底部胶囊：进入窗口下方 124px 显示，退到 140px 以上才隐藏（16px 滞回）；
// 胶囊自身矩形外扩 12px 恒为热区，鼠标一旦进来就立刻取消隐藏计时。
void MainWindow::updateDockProximity()
{
    if (!m_control) {
        return;
    }
    if (m_emptyState && m_emptyState->isVisible()) {
        m_dockHideTimer->stop();
        if (m_dockPhase != ChromePhase::Hidden) {
            setDockVisible(false, false);
            m_dockPhase = ChromePhase::Hidden;
        }
        return;
    }
    if (m_dockLocked) {
        m_dockHideTimer->stop();
        if (m_dockPhase != ChromePhase::Visible && m_dockPhase != ChromePhase::Showing) {
            setDockVisible(true, false);
            m_dockPhase = ChromePhase::Visible;
        }
        return;
    }

    const QPoint local = probeLocalPoint();
    const bool inside = rect().contains(local);
    const bool overDock = m_control->geometry().adjusted(-12, -12, 12, 12).contains(local);
    const bool shown = (m_dockPhase == ChromePhase::Visible || m_dockPhase == ChromePhase::Showing);

    bool want;
    if (overDock) {
        want = true;
    } else if (!inside) {
        want = false;
    } else {
        want = shown ? (local.y() >= height() - 140) : (local.y() >= height() - 124);
    }

    if (want) {
        m_dockHideTimer->stop();
        if (m_dockPhase == ChromePhase::Hidden || m_dockPhase == ChromePhase::Hiding) {
            setDockVisible(true);
            m_dockPhase = ChromePhase::Showing;
        }
    } else if (shown && !m_dockHideTimer->isActive()) {
        m_dockHideTimer->start();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    applyChromeLayout();
    placeNameOverlay();   // 浮层按视口顶部居中，窗口尺寸一变就重新摆
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), UiStyle::chromeBg());
    if (!isFullScreen()) {
        p.setPen(QPen(UiStyle::color(UiStyle::ChromeBorder), 1));
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

// 全屏/还原的唯一入口：标题栏按钮、F11、中键单击、--smoke fullscreen 共用。
// 不要在画布里直接调 showFullScreen：Qt 会按窗口 flags 重写原生样式，
// 退出时要靠 changeEvent 再把 DWM 帧补回来（见 applyNativeFrameStyles）。
void MainWindow::toggleFullscreen()
{
    if (isFullScreen()) {
        showNormal();
    } else {
        showFullScreen();
    }
}

// Qt 的 showFullScreen/showNormal 会整体重写原生样式（FramelessWindowHint
// 语义 → 纯 WS_POPUP），DWM 描边/投影/圆角随之丢失。窗口状态变化且不是
// 全屏时，把帧样式补回来。
void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        // 全屏进出：视口几何即将变化，按真实光标重算箭头显隐
        if (m_canvas) {
            m_canvas->refreshEdgeHover();
        }
#ifdef Q_OS_WIN
        if (!isFullScreen())
            applyNativeFrameStyles();
#endif
    }
}
#endif

// ---------------------------------------------------------------------------
// 动作系统
// ---------------------------------------------------------------------------

void MainWindow::createActions()
{
    // id 非空时登记进 m_actions：键盘自定义（config/keyboard/*）按 id 下发键位
    auto mk = [this](const QString &text, const QKeySequence &seq,
                     void (MainWindow::*slot)(), const QString &id = QString()) {
        auto *a = new QAction(text, this);
        if (!seq.isEmpty()) {
            a->setShortcut(seq);
        }
        connect(a, &QAction::triggered, this, slot);
        // QAction 的快捷键只有在「挂到窗口」后才参与 QShortcutMap 分发。
        // 此前所有动作都只 new 没 addAction，Ctrl+O 之类一直靠菜单/冒烟直调才生效。
        addAction(a);
        if (!id.isEmpty()) {
            m_actions.insert(id, a);
        }
        return a;
    };
    // 同 mk，但槽是 lambda（按键对齐 Honeyview 时大量一次性动作没有独立槽）
    auto mkFn = [this](const QString &text, const QKeySequence &seq,
                       const std::function<void()> &fn) {
        auto *a = new QAction(text, this);
        if (!seq.isEmpty()) {
            a->setShortcut(seq);
        }
        connect(a, &QAction::triggered, this, [fn]() { fn(); });
        addAction(a);
        return a;
    };

    // 文件（快捷键对照 Honeyview 语言表 MENUSTR_*，下同）
    // —— 传 id 的 14 个动作 = 键盘页可配置动作（批次 D1），键位由 config/keyboard/* 下发
    actOpen = mk(tr("打开(&O)..."), QKeySequence::Open, &MainWindow::onOpen, QStringLiteral("open"));
    actOpen->setShortcuts({ QKeySequence::Open, QKeySequence(QStringLiteral("F2")) });
    actOpenFolder = mk(tr("打开文件夹..."), QKeySequence(QStringLiteral("F")), &MainWindow::onOpenFolder, QStringLiteral("openfolder"));
    actSaveAs = mk(tr("另存为…(&S)"), QKeySequence::SaveAs, &MainWindow::onSaveAs);
    actPrint = mk(tr("打印"), QKeySequence::Print, &MainWindow::onPrint);          // Ctrl+P
    actBatchConvert = mk(tr("批量转换(&B)"), QKeySequence(QStringLiteral("Ctrl+T")), &MainWindow::onBatchConvert);
    // Honeyview：F5 = 设置。F9 保留为副键（旧标签与文档一直写 F9）
    actSettings = mk(tr("设置(&C)"), QKeySequence(QStringLiteral("F5")), &MainWindow::onSettings);
    actSettings->setShortcuts({ QKeySequence(QStringLiteral("F5")), QKeySequence(QStringLiteral("F9")) });
    // 书签菜单：添加书签 B（原型），勾选/取消勾选由 onToggleFavorite 承担
    actFavorite = mk(tr("添加书签"), QKeySequence(QStringLiteral("B")), &MainWindow::onToggleFavorite, QStringLiteral("favorite"));
    actDelete = mk(tr("删除"), QKeySequence::Delete, &MainWindow::onDeleteFile, QStringLiteral("del"));   // Del
    actCopyToFolder = mk(tr("复制到 图片文件夹"), QKeySequence(QStringLiteral("Ins")), &MainWindow::onCopyToFolder);
    actOpenWithEditor = mk(tr("用图像编辑器打开"), QKeySequence(QStringLiteral("Ctrl+E")), &MainWindow::onOpenWithEditor);
    actCopyToClipboard = mk(tr("复制到剪贴板"), QKeySequence::Copy, &MainWindow::onCopyToClipboard);
    actWallpaper = mk(tr("设为桌面壁纸…"), {}, &MainWindow::onSetWallpaper);
    actAbout = mk(tr("关于 QuikView"), QKeySequence(QStringLiteral("F1")), &MainWindow::onAbout);
    // Honeyview：X = 退出。Ctrl+Q 保留为副键
    actQuit = new QAction(tr("退出(&Q)"), this);
    actQuit->setShortcuts({ QKeySequence(QStringLiteral("X")), QKeySequence::Quit });
    connect(actQuit, &QAction::triggered, qApp, &QApplication::quit);
    addAction(actQuit);   // 不挂到窗口快捷键不参与分发（此前 Ctrl+Q 是死的）
    // Honeyview：F4 = 关闭
    mkFn(tr("关闭"), QKeySequence(QStringLiteral("F4")), [this]() { close(); });

    // 视图：缩放三兄弟一个互斥组；双页两项是**另一个**独立勾选组（可勾选、可同时勾选）。
    // 两维正交：缩放决定「怎么适配」，双页决定「显示几页」，菜单/画面/内部状态三者同步。
    auto *zoomModeGroup = new QActionGroup(this);
    zoomModeGroup->setExclusive(true);
    actActualSize = mk(tr("原始大小 (100%)"), QKeySequence(QStringLiteral("0")), &MainWindow::onActualSize, QStringLiteral("actual_size"));
    actFit = mk(tr("适合窗口"), QKeySequence(QStringLiteral("9")), &MainWindow::onFit, QStringLiteral("fit"));
    actFitWidth = mk(tr("适合宽度"), QKeySequence(QStringLiteral("8")), &MainWindow::onFitWidth);
    for (auto *a : { actActualSize, actFit, actFitWidth }) {
        a->setCheckable(true);
        zoomModeGroup->addAction(a);
        addAction(a);   // 不挂窗口快捷键不参与分发
    }
    actFit->setChecked(true);
    // 双页：不进 zoomModeGroup。点未勾项=换方向，点已勾项=取消双页（缩放不变）；
    // 快捷键 7/6 走同一条 QAction::triggered，同语义。
    actTwoL2R = new QAction(tr("智能双页阅览 (左→右)"), this);
    actTwoL2R->setShortcut(QKeySequence(QStringLiteral("7")));
    connect(actTwoL2R, &QAction::triggered, this, [this]() {
        setSpreadMode(m_spreadMode == 3 ? 0 : 3);
    });
    actTwoR2L = new QAction(tr("智能双页阅览 (左←右)"), this);
    actTwoR2L->setShortcut(QKeySequence(QStringLiteral("6")));
    connect(actTwoR2L, &QAction::triggered, this, [this]() {
        setSpreadMode(m_spreadMode == 4 ? 0 : 4);
    });
    for (auto *a : { actTwoL2R, actTwoR2L }) {
        a->setCheckable(true);
        addAction(a);   // 不挂窗口快捷键不参与分发（此前 7/6 是死的）
    }
    actZoomIn = mk(tr("放大(&+)"), QKeySequence::ZoomIn, &MainWindow::onZoomIn, QStringLiteral("zoom_in"));
    actZoomOut = mk(tr("缩小(&-)"), QKeySequence::ZoomOut, &MainWindow::onZoomOut, QStringLiteral("zoom_out"));
    // 无修饰的 + / - 作为第二快捷键（对照原版 MENUSTR_ZOOM_IN/OUT），菜单显示仍取第一个
    actZoomIn->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(QStringLiteral("+")) });
    actZoomOut->setShortcuts({ QKeySequence::ZoomOut, QKeySequence(QStringLiteral("-")) });
    // ←/→ 由 QAction 承担：主菜单右侧会自动显示箭头，避免和 keyPressEvent 两处处理。
    // PageUp/PageDown 作为副快捷键保留（原先就是翻页），显示仍取第一个 ←/→。
    // Backspace = 上一张（原版空格/退格翻页语义，空格在 keyPressEvent 已有）。
    actPrev = mk(tr("上一张图片"), QKeySequence(Qt::Key_Left), &MainWindow::onPrev, QStringLiteral("prev"));
    actNext = mk(tr("下一张图片"), QKeySequence(Qt::Key_Right), &MainWindow::onNext, QStringLiteral("next"));
    actPrev->setShortcuts({ QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp),
                            QKeySequence(Qt::Key_Backspace) });
    actNext->setShortcuts({ QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown) });
    // 菜单右侧要显示箭头而不是 Qt 的英文键名（Right/Left）。QMenu 把 '\t' 之后当快捷键列，
    // 这里显式写箭头：快捷键仍由 QAction 承担，显示文案单独指定。
    actPrev->setText(tr("上一张图片") + QLatin1Char('\t') + QStringLiteral("←"));
    actNext->setText(tr("下一张图片") + QLatin1Char('\t') + QStringLiteral("→"));
    // 浏览导航（对照原版 MENUSTR_PAGEMOVE_*）：10 张跳转/首尾，边界 toast
    actPrev10 = mk(tr("向上10张图片"), QKeySequence(QStringLiteral("Ctrl+PageUp")), &MainWindow::onPrev10);
    actNext10 = mk(tr("向下10张图片"), QKeySequence(QStringLiteral("Ctrl+PageDown")), &MainWindow::onNext10);
    actFirst = mk(tr("首张图片"), QKeySequence(QStringLiteral("Home")), &MainWindow::onFirst);
    actLast = mk(tr("末尾图片"), QKeySequence(QStringLiteral("End")), &MainWindow::onLast);
    // 同级文件夹跳转（对照原版 MENUSTR_PAGEMOVE_NEXT_PATH/PREV_PATH：] [）
    actPrevFolder = mk(tr("上一个文件夹"), QKeySequence(QStringLiteral("[")), &MainWindow::onPrevFolder);
    actNextFolder = mk(tr("下一个文件夹"), QKeySequence(QStringLiteral("]")), &MainWindow::onNextFolder);
    actNextFrame = mk(tr("下一帧"), QKeySequence(QStringLiteral("Ctrl+Right")), &MainWindow::onNextFrame, QStringLiteral("next_frame"));
    actPrevFrame = mk(tr("上一帧"), QKeySequence(QStringLiteral("Ctrl+Left")), &MainWindow::onPrevFrame, QStringLiteral("prev_frame"));

    actSlideshow = new QAction(tr("幻灯片放映(&S)"), this);
    actSlideshow->setCheckable(true);
    // 放映本体无直接键（原版同）：Ctrl+0 停止 / Ctrl+1..9 以 N 秒开始放映，
    // 见下面的 slideshow interval 动作组。
    addAction(actSlideshow);
    m_actions.insert(QStringLiteral("slideshow"), actSlideshow);   // 键盘页可配置（出厂无键）
    connect(actSlideshow, &QAction::toggled, this, &MainWindow::setSlideshow);
    // Ctrl+0 = 停止放映；Ctrl+1..9 = 以 N 秒间隔开始（对照原版 MENUSTR_SLIDESHOW_*）
    mk(tr("停止放映"), QKeySequence(QStringLiteral("Ctrl+0")), &MainWindow::onSlideshowStop);
    for (int s = 1; s <= 9; ++s) {
        mkFn(tr("%1 秒放映").arg(s), QKeySequence(QStringLiteral("Ctrl+%1").arg(s)),
           [this, s]() { onSlideshowInterval(s); });
    }

    actToggleMeta = new QAction(tr("显示/隐藏信息面板(&I)"), this);
    actToggleMeta->setCheckable(true);
    // Tab = 信息面板开关（对照原版）。Chrome 工具栏的 Tab 焦点导航分支已删，
    // 避免工具栏吃掉 Tab 导致快捷键失效。
    actToggleMeta->setShortcut(QKeySequence(QStringLiteral("Tab")));
    addAction(actToggleMeta);
    // 浮层几何依赖可见性（隐藏时 applyChromeLayout 跳过它），显示后要立刻排一次
    connect(actToggleMeta, &QAction::toggled, this, [this](bool on) {
        m_metadata->setVisible(on);
        if (on) {
            applyChromeLayout();
        }
    });
    // 面板自带 × 关掉时，把菜单勾选态同步回未勾选，避免「关了还打勾」
    connect(m_metadata, &MetadataPanel::closed, this, [this]() {
        actToggleMeta->setChecked(false);
    });
    connect(m_metadata, &MetadataPanel::copied, this, [this]() {
        m_toast->showMessage(tr("已复制图片信息参数"));
    });
    // 直方图折叠/展开改了内容高度：浮层高度=内容自然高，须重排几何（2026-11-04）。
    // 布局激活是异步的（T0 时 hint 仍旧值 525，T100 才降到 427），延迟一拍以上
    // 再排：60ms 对齐浮层其它重排节奏，用户肉眼无感。
    connect(m_metadata, &MetadataPanel::heightChanged, this, [this]() {
        QTimer::singleShot(60, this, [this]() { applyChromeLayout(); });
    });

    actDark = new QAction(tr("深色模式(&D)"), this);
    actDark->setCheckable(true);
    connect(actDark, &QAction::toggled, this, [](bool on) {
        // 菜单点击=显式选择深/浅，退出「跟随系统」。
        Config::instance().settings().setValue(QStringLiteral("config/theme"),
                                                on ? QStringLiteral("dark") : QStringLiteral("light"));
        Theme::applySetting(*qApp, on ? QStringLiteral("dark") : QStringLiteral("light"));
    });
    syncThemeActions();
    // 跟随系统时，Windows 深浅色切换也要把菜单勾选态带到实际模式上。
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, &MainWindow::syncThemeActions);

    // 编辑（旋转快捷键对照原版 MENUSTR_ROTATION_*：Alt+Left/Right）
    actRotateLeft = mk(tr("逆时针旋转 90°"), QKeySequence(QStringLiteral("Alt+Left")), &MainWindow::onRotateLeft);
    actRotateRight = mk(tr("顺时针旋转 90°"), QKeySequence(QStringLiteral("Alt+Right")), &MainWindow::onRotateRight);
    // 不旋转 Alt+Up / 旋转 180° Alt+Down（MENUSTR_ROTATION_NONE/180）
    mk(tr("不旋转"), QKeySequence(QStringLiteral("Alt+Up")), &MainWindow::resetGeometry);
    mkFn(tr("旋转 180°"), QKeySequence(QStringLiteral("Alt+Down")), [this]() { rotateImage(180); });
    // 翻转：Ctrl+M 水平 / Ctrl+F 垂直（MENUSTR_IMAGE_PRICESSING_MIRROR/FLIP）
    actFlipH = mk(tr("水平翻转"), QKeySequence(QStringLiteral("Ctrl+M")), &MainWindow::onFlipH);
    actFlipV = mk(tr("垂直翻转"), QKeySequence(QStringLiteral("Ctrl+F")), &MainWindow::onFlipV);
    // 原版 X = 退出程序（已绑 actQuit）；裁剪走菜单/转换对话框，不占单键
    actCrop = mk(tr("裁剪(&C)"), {}, &MainWindow::onCrop, QStringLiteral("crop"));   // 键盘页可配置（出厂无键）
    actResize = mk(tr("调整大小(&R)"), {}, &MainWindow::onResize);
    // 反色 Ctrl+I / Gamma 补偿 Ctrl+G（MENUSTR_IMAGE_PROCESSING_NEGATIVE/GAMMA）
    actNegative = mk(tr("反色"), QKeySequence(QStringLiteral("Ctrl+I")), &MainWindow::onNegative);
    actNegative->setCheckable(true);
    actGamma = mk(tr("Gamma 补偿…"), QKeySequence(QStringLiteral("Ctrl+G")), &MainWindow::onGamma);
    // 拉伸小图 Z（MENUSTR_FILTER_STRETCHWHENSMALL）：菜单勾选态用这一份
    actStretchSmall = mk(tr("拉伸小图"), QKeySequence(QStringLiteral("Z")), &MainWindow::onToggleStretchSmall);
    actStretchSmall->setCheckable(true);
    actStretchSmall->setChecked(Config::instance().stretchSmall());
    // 缩放锁定 Ctrl+K（MENUSTR_ZOOM_FIX）
    actZoomLock = mk(tr("锁定缩放"), QKeySequence(QStringLiteral("Ctrl+K")), &MainWindow::toggleZoomLock);
    actZoomLock->setCheckable(true);
    // 100% 也响应 *（MENUSTR_ZOOM_100）；0 仍是主键
    actActualSize->setShortcuts({ QKeySequence(QStringLiteral("0")), QKeySequence(QStringLiteral("*")) });
    // 1% 步进（MENUSTR_ZOOM_IN_1/OUT_1）
    mkFn(tr("放大 (1%)"), QKeySequence(QStringLiteral("Ctrl++")), [this]() {
        if (!m_currentImage.isNull()) {
            m_canvas->setZoom(m_canvas->zoom() + 0.01);
        }
    });
    mkFn(tr("缩小 (1%)"), QKeySequence(QStringLiteral("Ctrl+-")), [this]() {
        if (!m_currentImage.isNull()) {
            m_canvas->setZoom(m_canvas->zoom() - 0.01);
        }
    });
    // 选择图片 Enter（MENUSTR_PAGESELECT）：裁剪模式下 Enter 先落地选区
    mkFn(tr("选择图片"), QKeySequence(Qt::Key_Return), [this]() {
        if (m_cropMode) {
            applyCrop();
            return;
        }
        onPageSelect();
    });
    mkFn(tr("选择图片 (小键盘)"), QKeySequence(Qt::Key_Enter), [this]() {
        if (m_cropMode) {
            applyCrop();
            return;
        }
        onPageSelect();
    });
    // 定位文件 Ctrl+Enter（MENUSTR_OPEN_FILEPOSITION）
    mkFn(tr("定位文件"), QKeySequence(QStringLiteral("Ctrl+Return")), [this]() {
        if (m_currentPath.isEmpty()) {
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_currentPath).absolutePath()));
    });
    // 随机跳转 Ctrl+Alt+PageDown/Up（MENUSTR_PAGEMOVE_RAND_*）
    mkFn(tr("向后随机转到某张图片"), QKeySequence(QStringLiteral("Ctrl+Alt+PageDown")),
       [this]() { onJumpRandom(true); });
    mkFn(tr("向前随机转到某张图片"), QKeySequence(QStringLiteral("Ctrl+Alt+PageUp")),
       [this]() { onJumpRandom(false); });
    // 编辑书签 F3（MENUSTR_BOOKMARK_EDIT）
    mk(tr("编辑书签"), QKeySequence(QStringLiteral("F3")), &MainWindow::onEditBookmarks);

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
        addAction(a);   // 不挂窗口快捷键不参与分发（此前 U/I/S 是死的）
    }
    actEffectNone->setChecked(true);
    connect(actEffectNone, &QAction::triggered, this, [this]() { onEffect(0); });
    connect(actEffectSoften, &QAction::triggered, this, [this]() { onEffect(1); });
    connect(actEffectSharpen, &QAction::triggered, this, [this]() { onEffect(2); });

    // 插值算法（三选一，对照原版 MENUSTR_INTERPOLATEMODE_*）
    auto *interpGroup = new QActionGroup(this);
    interpGroup->setExclusive(true);
    actInterpFast = new QAction(tr("Bilinear 插值 (快)"), this);
    actInterpSmooth = new QAction(tr("Bicubic 插值 (慢，高品质)"), this);
    actInterpLanczos = new QAction(tr("Lanczos 插值 (慢，高品质)"), this);
    for (auto *a : { actInterpFast, actInterpSmooth, actInterpLanczos }) {
        a->setCheckable(true);
        interpGroup->addAction(a);
    }
    connect(actInterpFast, &QAction::triggered, this, [this]() { setInterpolationMode(QStringLiteral("bilinear")); });
    connect(actInterpSmooth, &QAction::triggered, this, [this]() { setInterpolationMode(QStringLiteral("bicubic")); });
    connect(actInterpLanczos, &QAction::triggered, this, [this]() { setInterpolationMode(QStringLiteral("lanczos")); });

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

    // 键盘快捷键自定义：启动即按 config/keyboard/* 下发（无配置 = 出厂键位，行为不变）
    applyShortcutConfig();
}

// ---------------------------------------------------------------------------
// 键盘快捷键自定义（批次 D1）：14 个可配置动作的键位下发。
// 槽位语义（与设置页键盘页一一对应）：每一行是一个「键位槽」，
// config/keyboard/<槽位>_enabled / _key / _action，槽位默认名 = 动作 id。
//   槽位禁用 → 该槽位不贡献键位；
//   未自定义（无 _key 或配回出厂主键，且绑定自身）→ 贡献出厂完整键位表
//   （PageUp/Backspace/* 等副键保留，与改动前行为逐键一致）；
//   自定义 → 向目标动作贡献该键位（顶掉目标动作的原键位，含副键）。
// 多个槽位可以指向同一目标动作，键位按目标动作聚合后一次安装（第 15 期修复：
// 逐槽 setShortcuts 会被后遍历的槽位覆盖，导致改绑后两个键都失效）。
// 调用点：createActions 末尾（启动）、onSettings 返回（保存/取消都重装，
// 取消时配置已被快照回滚，重装回旧值）。冲突在设置页保存时拦截。

namespace {
struct SlotDef {
    const char *slot;
    QList<QKeySequence> defaults;   // 出厂键位（主键在前，含副键）
};

// 出厂槽位表。改动任何一个动作的出厂键位须同步设置页键盘页的 defs 表。
// 注意：变量名不能用 slots——它是 Qt 的空宏。
const QList<SlotDef> &shortcutSlotDefs()
{
    static const QList<SlotDef> defs = {
        { "open",        { QKeySequence::Open, QKeySequence(QStringLiteral("F2")) } },
        { "openfolder",  { QKeySequence(QStringLiteral("F")) } },
        { "next",        { QKeySequence(Qt::Key_Right), QKeySequence(Qt::Key_PageDown) } },
        { "prev",        { QKeySequence(Qt::Key_Left), QKeySequence(Qt::Key_PageUp),
                           QKeySequence(Qt::Key_Backspace) } },
        { "next_frame",  { QKeySequence(QStringLiteral("Ctrl+Right")) } },
        { "prev_frame",  { QKeySequence(QStringLiteral("Ctrl+Left")) } },
        { "zoom_in",     { QKeySequence::ZoomIn, QKeySequence(QStringLiteral("+")) } },
        { "zoom_out",    { QKeySequence::ZoomOut, QKeySequence(QStringLiteral("-")) } },
        { "actual_size", { QKeySequence(QStringLiteral("0")), QKeySequence(QStringLiteral("*")) } },
        { "fit",         { QKeySequence(QStringLiteral("9")) } },
        { "slideshow",   {} },
        { "crop",        {} },
        { "favorite",    { QKeySequence(QStringLiteral("B")) } },
        { "del",         { QKeySequence::Delete } },
    };
    return defs;
}
} // namespace

void MainWindow::applyShortcutConfig()
{
    const Config &cfg = Config::instance();

    // 先全部清空再统一安装：槽位被挪用/禁用时，原动作不留旧键
    for (const SlotDef &d : shortcutSlotDefs()) {
        if (QAction *a = m_actions.value(QString::fromLatin1(d.slot))) {
            a->setShortcuts({});
        }
    }

    // 按「目标动作」聚合，而不是逐槽位 setShortcuts：多个槽位可以指向同一个
    // 动作（如「打开」改绑到「打开文件夹」），而 setShortcuts 是替换语义，
    // 逐槽安装会让后遍历的槽位把先前写入的键覆盖掉（第 15 期 P0 bug）。
    // 同一动作收到重复键只装一次，避免 Qt 的 ambiguous shortcut 警告。
    QHash<QString, QList<QKeySequence>> perAction;
    for (const SlotDef &d : shortcutSlotDefs()) {
        const QString slot = QString::fromLatin1(d.slot);
        if (!cfg.actionEnabled(slot, true)) {
            continue;   // 槽位禁用：动作保持无键
        }
        const QString target = cfg.actionBinding(slot, slot);
        if (!m_actions.contains(target)) {
            continue;
        }
        const QString stored = cfg.actionKey(slot);
        const QKeySequence seq = QKeySequence::fromString(stored, QKeySequence::PortableText);
        QList<QKeySequence> &keys = perAction[target];
        if (target == slot && (stored.isEmpty() || seq == d.defaults.value(0))) {
            // 未自定义 / 配回出厂主键：恢复完整出厂表（副键保留，行为与旧版一致）
            for (const QKeySequence &k : d.defaults) {
                if (!keys.contains(k))
                    keys.append(k);
            }
            continue;
        }
        if (!seq.isEmpty() && !keys.contains(seq)) {
            keys.append(seq);
        }
    }
    for (auto it = perAction.constBegin(); it != perAction.constEnd(); ++it) {
        if (QAction *dst = m_actions.value(it.key()))
            dst->setShortcuts(it.value());
    }
}

// 14 个可配置动作的出厂键位全表（设置页冲突检测用）。
QHash<QString, QList<QKeySequence>> MainWindow::factoryShortcutLists() const
{
    QHash<QString, QList<QKeySequence>> out;
    for (const SlotDef &d : shortcutSlotDefs())
        out.insert(QString::fromLatin1(d.slot), d.defaults);
    return out;
}

// 键盘页冲突检测数据源：除 14 个可配置动作外，其余动作（打印 Ctrl+P、设置 F5、
// 退出 X…）的键位是固定的，新键位撞上即冲突。每个快捷键一条（副键也算）。
QList<QPair<QString, QKeySequence>> MainWindow::fixedShortcutActions() const
{
    const QSet<QAction *> configurable = {
        m_actions.value(QStringLiteral("open")),
        m_actions.value(QStringLiteral("openfolder")),
        m_actions.value(QStringLiteral("next")),
        m_actions.value(QStringLiteral("prev")),
        m_actions.value(QStringLiteral("next_frame")),
        m_actions.value(QStringLiteral("prev_frame")),
        m_actions.value(QStringLiteral("zoom_in")),
        m_actions.value(QStringLiteral("zoom_out")),
        m_actions.value(QStringLiteral("actual_size")),
        m_actions.value(QStringLiteral("fit")),
        m_actions.value(QStringLiteral("slideshow")),
        m_actions.value(QStringLiteral("crop")),
        m_actions.value(QStringLiteral("favorite")),
        m_actions.value(QStringLiteral("del")),
    };
    QList<QPair<QString, QKeySequence>> out;
    const auto all = findChildren<QAction *>();
    for (QAction *a : all) {
        if (configurable.contains(a)) {
            continue;
        }
        QString label = a->text();
        const int tab = label.indexOf(QLatin1Char('\t'));
        if (tab >= 0) {
            label.truncate(tab);
        }
        label.remove(QLatin1Char('&'));
        const auto seqs = a->shortcuts();
        for (const QKeySequence &s : seqs) {
            out.append({ label, s });
        }
    }
    return out;
}

// 冒烟取证（--smoke report 的 keys= 字段）：14 个可配置动作的当前键位。
QString MainWindow::shortcutsReport() const
{
    static const char *const ids[] = {
        "open", "openfolder", "next", "prev", "next_frame", "prev_frame", "zoom_in",
        "zoom_out", "actual_size", "fit", "slideshow", "crop", "favorite", "del",
    };
    QStringList parts;
    for (const char *id : ids) {
        QStringList seqs;
        if (QAction *a = m_actions.value(QString::fromLatin1(id))) {
            const auto list = a->shortcuts();
            for (const QKeySequence &s : list) {
                seqs << s.toString(QKeySequence::PortableText);
            }
        }
        parts << QStringLiteral("%1=%2").arg(QString::fromLatin1(id),
                                             seqs.isEmpty() ? QStringLiteral("-")
                                                            : seqs.join(QLatin1Char('|')));
    }
    return parts.join(QLatin1Char(';'));
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
    menu->addAction(actTwoL2R);
    menu->addAction(actTwoR2L);
    menu->addSeparator();
    actStretchSmall->setChecked(Config::instance().stretchSmall());
    menu->addAction(actStretchSmall);   // Z
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
        // 与 Ctrl+1..9 共用同一份实现：未放映时选秒数 = 立即开始放映；
        // 放映中选秒数 = 只换节奏，不打断。（以前这里只写配置，
        // 未放映时点菜单什么都不发生，工具栏就成了唯一入口且永远开不了。）
        QAction *a = addCheckableItem(menu, tr("%1 秒").arg(s), shortcut, currentSec == s,
                                      [this, s]() { onSlideshowInterval(s); });
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
    addMenuItem(menu, tr("编辑书签"), QStringLiteral("F3"), [this]() { onEditBookmarks(); });
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
    actNegative->setChecked(m_negative);
    menu->addAction(actNegative);      // Ctrl+I
    menu->addAction(actGamma);         // Ctrl+G
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
    transferTo(tr("复制到 图片文件夹 2"), QString(),
               QStringLiteral("config/etc/photo_folder_2"), false);
    transferTo(tr("移动到 图片文件夹 1"), QString(),
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

    // —— 文件（快捷键对照 Honeyview，且都已真实绑定）——
    addMenuItem(menu, tr("打开图片"), QStringLiteral("F2"), [this]() { onOpen(); });
    addMenuItem(menu, tr("打开文件夹"), QStringLiteral("F"), [this]() { onOpenFolder(); });
    addMenuItem(menu, tr("关闭"), QStringLiteral("F4"), [this]() { close(); });
    addMenuItem(menu, tr("选择图片"), QStringLiteral("Enter"), [this]() { onPageSelect(); });
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
    // ←/→ 不再手写死字符串：挂真实 QAction，菜单右侧自动显示 QAction 的快捷键（←/→），
    // 快捷键只有一个事实来源，不会再出现「菜单写 →、实际按了没反应」。
    nav->addAction(actNext);
    nav->addAction(actPrev);
    addMenuItem(nav, tr("首张图片"), QStringLiteral("Home"), [this]() { onFirst(); });
    addMenuItem(nav, tr("末尾图片"), QStringLiteral("End"), [this]() { onLast(); });
    addMenuItem(nav, tr("向下10张图片"), QStringLiteral("Ctrl+PageDown"), [this]() { onNext10(); });
    addMenuItem(nav, tr("向上10张图片"), QStringLiteral("Ctrl+PageUp"), [this]() { onPrev10(); });
    // 随机跳转（实现在 onJumpRandom，Ctrl+Alt+PageUp/Down 已真实绑定）
    addMenuItem(nav, tr("向后随机转到某张图片"), QStringLiteral("Ctrl+Alt+PageDown"),
                [this]() { onJumpRandom(true); });
    addMenuItem(nav, tr("向前随机转到某张图片"), QStringLiteral("Ctrl+Alt+PageUp"),
                [this]() { onJumpRandom(false); });
    addMenuItem(nav, tr("下一个文件夹"), QStringLiteral("]"), [this]() { onNextFolder(); });
    addMenuItem(nav, tr("上一个文件夹"), QStringLiteral("["), [this]() { onPrevFolder(); });
    UiStyle::polishMenu(nav);

    // —— 插值算法（3 项，勾选态跟随真实设置）——
    QMenu *interp = menu->addMenu(tr("插值算法"));
    actInterpFast->setChecked(m_interpMode == 0);
    actInterpSmooth->setChecked(m_interpMode == 1);
    actInterpLanczos->setChecked(m_interpMode == 2);
    interp->addAction(actInterpFast);
    interp->addAction(actInterpSmooth);
    addCheckableItem(interp, tr("Lanczos 插值 (慢，高品质)"), QString(), m_interpMode == 2,
                     [this]() { setInterpolationMode(QStringLiteral("lanczos")); });
    UiStyle::polishMenu(interp);

    // —— 图片效果（4 项）——
    QMenu *fx = menu->addMenu(tr("图片效果"));
    actNegative->setChecked(m_negative);
    fx->addAction(actNegative);        // Ctrl+I
    fx->addAction(actGamma);           // Ctrl+G
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

    // —— 浏览方式（原型取 showmode 前 5 项）：缩放三选 + 双页两勾，勾选态各自独立 ——
    QMenu *show = menu->addMenu(tr("浏览方式"));
    syncViewModeActions();
    show->addAction(actActualSize);
    show->addAction(actFit);
    show->addAction(actFitWidth);
    show->addAction(actTwoL2R);
    show->addAction(actTwoR2L);
    UiStyle::polishMenu(show);

    // —— 缩放工具（6 项）——
    QMenu *zoom = menu->addMenu(tr("缩放工具"));
    addMenuItem(zoom, tr("放大"), QStringLiteral("+"), [this]() { onZoomIn(); });
    addMenuItem(zoom, tr("缩小"), QStringLiteral("−"), [this]() { onZoomOut(); });
    actZoomLock->setChecked(m_zoomLocked);
    zoom->addAction(actZoomLock);       // Ctrl+K
    addMenuItem(zoom, tr("100%"), QString(), [this]() { setZoomMode(0); });
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
    addCheckableItem(rot, tr("不旋转"), QStringLiteral("Alt+Up"),
                     m_netRotation == 0 && !m_flipH && !m_flipV, [this]() { resetGeometry(); });
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
    // 右键已被映射成别的动作时，画布按下已经派发过，这里不再弹菜单；
    // 映射成 context_menu 或 none 时照旧弹菜单。
    const QString rightAct = Config::instance().mouseRightAction();
    if (rightAct != QLatin1String("none") && rightAct != QLatin1String("context_menu")) {
        return;
    }
    QMenu *menu = buildContextMenu();
    menu->exec(event->globalPos());
    menu->deleteLater();
}

// ---------------------------------------------------------------------------
// 运行设置
// ---------------------------------------------------------------------------

void MainWindow::applyRuntimeSettings()
{
    const Config &cfg = Config::instance();

    // 置顶标志只在**值真的变化**时才下发：setWindowFlag() 会重建原生窗口，
    // 保存设置时值没变却无条件调用，窗口会消失再出现（用户以为程序重启）。
    // 值变化时属于必要重建，顺手记下几何/可见/最大化状态，重建后还原。
    const bool wantTop = cfg.alwaysOnTop();
    const bool isTop = windowFlags().testFlag(Qt::WindowStaysOnTopHint);
    if (wantTop != isTop) {
        const QRect geo = geometry();
        const bool wasVisible = isVisible();
        const bool wasMaximized = isMaximized();
        setWindowFlag(Qt::WindowStaysOnTopHint, wantTop);
        if (wasMaximized) {
            showMaximized();
        } else if (wasVisible) {
            show();
            setGeometry(geo);
        }
    }

    setInterpolationMode(cfg.interpolation());
    m_canvas->setStretchSmall(cfg.stretchSmall());
    m_zoomLocked = Config::instance().settings()
                       .value(QStringLiteral("config/zoom_fix"), false).toBool();
    m_canvas->setWheelZoomEnabled(cfg.wheelZoom());
    m_canvas->setMiddlePanEnabled(cfg.middlePan());
    // 图片大于窗口时显示图片顶部（键 "config/imageproc/align_top_when_large"）
    m_canvas->setAlignTopWhenLarge(cfg.alignTopWhenLarge());
    // 缩放百分比浮标（键 "config/view/show_zoom_control"）：关掉时把已经在淡出
    // 队列里的浮标立即收掉，之后 zoomChanged 不再触发显示。
    if (m_zoomBadge && !cfg.showZoomControl()) {
        m_zoomBadge->hide();
    }
    // EXIF / 文件信息小字体（键 "config/view/exif_small_font"）：信息面板键值行
    // 12px ↔ 10px，底部状态行 11px ↔ 10px；切换后立即按最近一张图重建。
    if (m_metadata) {
        m_metadata->setSmallFont(cfg.exifSmallFont());
    }
    if (m_statusOverlay) {
        m_statusOverlay->setSmallFont(cfg.exifSmallFont());
    }
    // 双击最大化/还原开关（键 "config/mouse/dblclick_maximize"）：开启时画布
    // 会把边缘单击翻页延迟 220ms 等第二击，避免「双击」先翻页又最大化。
    m_canvas->setDoubleClickMaximizeEnabled(cfg.dblclickMaximize());
    // 原图缓存上限随设置即时生效：保存设置不用重启（键 "config/imageproc/cache_size_mb"）
    m_cache->setFullCacheLimit(cfg.cacheSizeMb());
    // 运行期间禁用屏幕保护（键 "config/etc/disable_screensaver"）：开关保存后立即生效，
    // 程序退出时在析构里还原。
    applyScreensaverPrevention(cfg.disableScreensaver());

    // 画布外观按用户 2026-11-03 决定固定：纯色 CanvasBg、无边框、无背景图。
    // 设置入口已在第 18 期移除；ImageCanvas 的 setter 保留，恢复时只需接回 UI。
    m_canvas->setBackgroundColor(UiStyle::canvasBg());
    m_canvas->setBorderColor(QColor(Qt::transparent));
    m_canvas->setBackgroundImage(QImage());
    // 设置页的「不显示侧边箭头」是反选（inverted），存的就是 show_side_arrow 本身：
    // true=显示（默认）。关掉只影响视觉，两侧点击仍翻页。
    m_canvas->setSideArrowsEnabled(Config::instance().settings()
                                       .value(QStringLiteral("config/view/show_side_arrow"), true)
                                       .toBool());

    // 缩略图预览尺寸按 20:53 交办固定 128×128（面板 160 宽），
    // 设置里的「缩略图大小」项暂时不再影响面板，等确认后再决定是否从设置页移除。

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
    applyChromeLayout();   // 通知位置等铬件几何按当前配置立即重排（改设置不用重启）

#ifdef Q_OS_WIN
    // setWindowFlag() 会让 Qt 按 flags 重建原生样式，把 WS_THICKFRAME|WS_CAPTION
    // 一起抹掉。没有 WS_THICKFRAME，Windows 对 WM_NCLBUTTONDOWN 的命中结果直接
    // 忽略——点边框既不进入 SC_SIZE 循环也不改尺寸（表现为窗口"拖不动边"）。
    // 这里统一补回，幂等。
    applyNativeFrameStyles();
#endif
}

void MainWindow::refreshZoomBadge(qreal z)
{
    if (!m_zoomBadge || m_currentImage.isNull())
        return;
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
    // 自动保存当前图片路径为书签（键 "config/bookmark/auto_bookmark"）：
    // 只在「打开」路径触发（翻页走 loadAsync，不经过这里），已收藏则不重复写。
    const bool autoFav = Config::instance().autoBookmark()
                         && !m_currentPath.isEmpty()
                         && !Config::instance().favorites().contains(m_currentPath);
    if (autoFav) {
        Config::instance().toggleFavorite(m_currentPath);
    }
    if (Config::instance().rememberBrowsedFolders()) {
        Config::instance().addRecentFile(m_currentPath);
        refreshLibraryMenus();
    } else if (autoFav) {
        refreshLibraryMenus();   // 未记忆文件夹时书签菜单也要刷新
    }
    jumpTo(m_currentPath);
}

// 打开文件并按图调窗：仅「启动带文件 / 文件→打开」两条路径使用。
// 内部复用 openFile（目录/记忆/跳转逻辑不重写），只额外置位调窗标记。
void MainWindow::openFileFitting(const QString &path)
{
    m_fitWindowPending = true;
    openFile(path);
}

// 显式跳转（打开/换目录/滑块/缩略图/书签/首尾/随机/排序）：清空播放队列，
// 避免跳转后先把旧队列播完才理新位置。顺序翻页（onNext/onPrev/±10/幻灯片）不走这里。
void MainWindow::jumpTo(const QString &path)
{
    m_cache->cancelPending();
    loadAsync(path);
}

void MainWindow::loadAsync(const QString &path)
{
    m_currentPath = path;
    m_frames->clear();
    // 快帧目标 = 视口像素：连按时先上缩放解码的快帧顶住体感
    m_cache->requestFull(path, m_canvas ? m_canvas->viewport()->size() : QSize());
    if (m_preloadTimer) {
        m_preloadTimer->start();   // 又有输入：空闲预读重新计时
    }
}

// 空闲预读：向当前页前后各预读 1 张（双页阅览按跨页 = 2 张），只填缓存不上屏。
// 队列没消化完就再等 300ms —— 预读永远给用户请求让路。
void MainWindow::preloadNeighbors()
{
    // 空闲预读受「缓存下一张图片」开关控制（键 "config/imageproc/cache_image"）：
    // 关闭时整段预读直接短路，翻页只解码当前图。
    if (!Config::instance().cacheNextImage()) {
        return;
    }
    if (!m_dir->isValid() || !m_cache->isIdle()) {
        if (m_dir->isValid() && m_preloadTimer) {
            m_preloadTimer->start();
        }
        return;
    }
    const int step = isSpreadMode() ? 2 : 1;
    const QStringList files = m_dir->files();
    const int i = m_dir->index();
    if (i - step >= 0) {
        m_cache->preload(files.at(i - step));
    }
    if (i + step < files.size()) {
        m_cache->preload(files.at(i + step));
    }
}

void MainWindow::onFullReady(const QString &path, const QImage &image, bool preview)
{
    // FIFO 单飞保证 emit 顺序 == 请求顺序，这里不再做过期丢弃：
    // 连按/长按时每张都按序上屏（快帧先到先显示，全图到了替换）。
    showImage(image, path, preview);
}

// 拖动滑杆的跟手预览：只换画布、标题、缩放基准，让「经过的每一张」都有一帧；
// 索引 / 元数据 / 帧序列 / 页面滑块状态都不动 —— 松手后 pageRequested 会走
// 一次正规 jumpTo + 全尺寸解码，落点最终一定是清晰的。
void MainWindow::onDragPreviewReady(const QString &path, const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    if (m_frames->animatable()) {
        stopAnimation();   // 旧 GIF 的 tick 会把画布刷回上一帧，拖动期间先停
    }
    m_dragPreviewPath = path;   // 滑块跟手预览：不让它的全图落地再触发翻页过渡
    showImage(image, path, true);
}

void MainWindow::onLoadFailed(const QString &path, const QString &error)
{
    if (path != m_currentPath) {
        return;
    }
    QString rawErr;
    // 「不打开 RAW 图片文件」（键 "config/etc/no_raw_load"）：跳过 libraw 兜底，
    // 明确告知是设置拦下的，而不是伪装成「解码失败」。
    // 其余 RAW 的全尺寸解码已由 ImageCache 的后台线程做过（B12），这里不再重复解。
    const bool rawFile = isRawFilePath(path);
    if (rawFile) {
        rawErr = Config::instance().noRawLoad() ? tr("已在设置中禁用 RAW 解码")
                                                : tr("RAW 解码失败");
    } else {
        QImage rawImg = loadRawImage(path, &rawErr, m_hdrTonemap);
        if (!rawImg.isNull()) {
            showImage(rawImg, path);
            return;
        }
    }
    QString heifErr;
    QImage img = loadHeifImage(path, &heifErr, m_hdrTonemap);
    if (!img.isNull()) {
        showImage(img, path);
        return;
    }
    QString lastErr = rawErr.isEmpty() ? heifErr : rawErr;
    m_toast->showMessage(tr("无法打开：%1").arg(lastErr.isEmpty() ? error : lastErr));
}

void MainWindow::showImage(const QImage &image, const QString &path, bool preview)
{
    // 装图期间所有 zoomChanged 都是程序算的（fit / 恢复缩放 / 预览换图），
    // 统一屏蔽「保存缩放状态」写盘，避免把用户上次的缩放覆盖掉。
    m_zoomSaveGuard = true;
    // 连按中途的快帧：只换画布与标题顶住体感。源图/效果/元数据/动图等簿记
    // 等全图到了走完整流程，避免快帧（缩放解码的小图）污染旋转/Gamma 基准。
    if (preview) {
        m_canvas->setPixmap(QPixmap::fromImage(image));
        setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" - QuikView"));
        m_caption->setTitle(QFileInfo(path).fileName());
        m_zoomSaveGuard = false;
        return;
    }
    // HDR 色调映射通用口：loader 内部的高位深在解码层就映射完（到这里已是
    // 8bit，toneMapToSdr 原样放行）；QImageReader 产出的 RGBA64（16bit PNG/
    // TIFF 等）在这里映射。8bit 图硬规则：一个像素都不动。
    // 「使用图片文件的 ICC 设置」（键 "config/imageproc/image_icc"）：带内嵌
    // 非 sRGB ICC 的图（Display P3 / AdobeRGB）先转到 sRGB；无 ICC 或已是 sRGB
    // 的图原样直通，普通截图的像素不受影响。
    const bool icc = Config::instance().imageIcc();
    auto toSrgb = [icc](QImage img) {
        if (!icc) {
            return img;
        }
        const QColorSpace cs = img.colorSpace();
        if (cs.isValid() && cs != QColorSpace::SRgb) {
            img.convertToColorSpace(QColorSpace::SRgb);
        }
        return img;
    };
    const QImage sdr = toSrgb(m_hdrTonemap ? ToneMap::toneMapToSdr(image) : image);
    // 双页模式先把相邻两页拼成一张大图，后面所有几何/颜色变换都作用在整跨页上，
    // 视图层面永远只有「一张图」，不需要两套坐标。
    QImage base = sdr;
    if (isSpreadMode()) {
        const QImage second = toSrgb(readImageSync(spreadSecondPath()));
        base = (m_spreadMode == 3) ? composeSpread(sdr, second) : composeSpread(second, sdr);
    }
    // m_sourceImage 保存「文件里的原始像素」，旋转/翻转/反相/Gamma 全都从它派生。
    // 这样「不旋转」才能真回到原样，而不是靠撤销栈一层层退。
    m_sourceImage = base;
    m_netRotation = 0;
    m_flipH = false;
    m_flipV = false;
    m_negative = false;
    m_gamma = 1.0;
    // GIF 透明底色（键 "config/imageproc/gif_transparent"）只影响上屏的那一份，
    // m_sourceImage 保留带 alpha 的原始像素，旋转/翻转仍从原始派生。
    const bool gifPath = QFileInfo(path).suffix().compare(QLatin1String("gif"),
                                                         Qt::CaseInsensitive) == 0;
    QImage display = base;
    if (gifPath) {
        display = applyGifBackdrop(base);
    } else if (base.hasAlphaChannel()) {
        // 32 位 PNG/WebP/TIFF 等：按 img32_transparent 铺底（默认网格）。
        display = applyImage32Backdrop(base);
    }
    m_currentImage = display;
    m_originalImage = display;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
    const qreal prevZoom = m_canvas->zoom();
    // fitAfter=false：这里只装图，适配交给下面的 applyCurrentZoom
    // 做一次——否则 setPixmap 内部先 fit 一次、再适配一次，
    // Lanczos 模式下等于每次翻页多做一次全图重采样。
    // 翻页效果（键 "config/imageproc/pageturn_fx"）：只在上屏一张「不同路径」的
    // 整图时触发；首图、原地重绘（旋转/效果）、滑块跟手预览都不算换页。
    const int pageFx = Config::instance().pageTurnFx();
    // 滑块跟手预览（onDragPreviewReady）不推动画；它落地成全图时也不再算一次翻页，
    // 否则拖动结束时还会补一次过渡。其余整图换页（含快帧预览后的全图）照常触发。
    const bool fromDragPreview = (path == m_dragPreviewPath);
    m_dragPreviewPath.clear();
    const bool pageTurn = !fromDragPreview && !m_lastShownPath.isEmpty()
                          && m_lastShownPath != path;
    m_lastShownPath = path;
    if (pageTurn && pageFx > 0 && !m_zoomLocked) {
        m_canvas->setPixmapAnimated(QPixmap::fromImage(display), pageFx, /*fitAfter=*/false);
    } else {
        m_canvas->setPixmap(QPixmap::fromImage(display), /*fitAfter=*/false);
    }
    // 保存缩放状态（键 "config/view/save_zoom_state"）：开关开着且该路径有记录时，
    // 按记录恢复（fit / width / 具体比例）；没有记录才回落到 default_zoom。
    // 锁定缩放优先级更高——它的语义就是「翻页一律沿用上一张比例」。
    QString savedState;
    if (Config::instance().saveZoomState() && !m_zoomLocked) {
        savedState = Config::instance().savedViewState(path);
    }
    if (m_zoomLocked && !m_firstImage && prevZoom > 0.0) {
        m_canvas->setPixmapPreservingView(QPixmap::fromImage(display), QPointF(0.5, 0.5),
                                          prevZoom, false, false);
        // 这里不能写 m_zoomMode：锁定只是「沿用比例」，不是把浏览方式改成原始尺寸。
        // 写了会把用户锁定前的模式永久污染成 0，解锁后翻页全部变 100%。
    } else if (savedState == QLatin1String("width")) {
        m_canvas->fitToWidth();
    } else if (savedState == QLatin1String("fit")) {
        m_canvas->fitToWindow();
    } else if (!savedState.isEmpty() && savedState.toDouble() > 0.0) {
        // 恢复用户上次的具体比例（如 300%）；走 setZoom 而不是 setZoomMode，
        // 不污染 m_zoomMode——「缩放状态」只记这一张图，浏览方式仍是全局的。
        m_canvas->setZoom(savedState.toDouble());
    } else if (m_firstImage && Config::instance().defaultZoom() == QLatin1String("actual")) {
        setZoomMode(0);
    } else {
        // 单图与双页拼图走同一条适配：只看 m_zoomMode（0 原始 / 1 窗口 / 2 宽度）
        applyCurrentZoom();
    }
    // 每次装图后把勾选态/图标对齐（首图、默认 actual、双页拼图都覆盖到）
    syncViewModeActions();
    updateShowModeIcon();
    m_firstImage = false;
    updateCanvasLogo();
    // 胶囊第一行 = 翻页进度（对齐原型 #pageSlider）；单张目录时控件自动禁用
    m_control->setPage(m_dir->index() + 1, m_dir->count());
    m_metadata->showImage(path, image);
    // 信息浮层宽度随元数据自适应，换图后重排一次几何
    applyChromeLayout();
    m_browser->setCurrent(m_dir->index());
    setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" - QuikView"));
    m_caption->setTitle(QFileInfo(path).fileName());
    m_frames->open(path);
    // 动图打开即自动播放（P0：仅 GIF）；换到静态图 / 多页 TIFF / 双页模式则停。
    // 这里不弹 toast（自动行为不打扰），空格切换时才提示。
    if (m_frames->hasFrames() && m_frames->animatable() && !isSpreadMode()) {
        startAnimation();
    } else {
        stopAnimation();
    }
    // 按图调窗：仅「启动带文件 / 文件→打开」路径触发一次（fitWindowToImage 内部
    // 自清理 m_fitWindowPending 并受设置「根据图片大小调节窗口大小」控制）
    fitWindowToImage();
    // 装图全链路（含按图调窗引发的 resize→fit）结束，恢复写盘
    m_zoomSaveGuard = false;
}

// 按当前图片像素尺寸调窗（键 "config/etc/fit_window_to_image"）：
// 目标尺寸 = 图片 + 顶部铬件/底部胶囊等固定开销，clamp 到可用屏幕 80% 后居中。
// 只在 m_fitWindowPending 置位（openFileFitting / 文件→打开）时执行一次；
// 翻页/拖放/换目录等其它打开路径不置位，窗口不跟着每张图跳。
void MainWindow::fitWindowToImage()
{
    if (!m_fitWindowPending || m_currentImage.isNull()) {
        return;
    }
    m_fitWindowPending = false;
    if (!Config::instance().fitWindowToImage()) {
        return;
    }
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }
    const QRect avail = screen->availableGeometry();
    const QSize img = m_currentImage.size();
    // 固定开销估算：顶部铬件（kTopChromeH）+ 底部胶囊 72 + 视口留白 16；
    // 窗口自带最小尺寸兜底，不会缩到不可用。
    const int kOverheadH = kTopChromeH + 72 + 16;
    const int maxW = qRound(avail.width() * 0.8);
    const int maxH = qRound(avail.height() * 0.8);
    const int w = qBound(200, img.width(), maxW);
    const int h = qBound(160, img.height() + kOverheadH, maxH);
    resize(w, h);
    move(avail.x() + (avail.width() - w) / 2, avail.y() + (avail.height() - h) / 2);
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
        m_dockHideTimer->stop();
        setDockVisible(false, false);
        m_dockPhase = ChromePhase::Hidden;
    } else if (m_dockLocked) {
        setDockVisible(true, false);
        m_dockPhase = ChromePhase::Visible;
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
    // 多帧文件：信息行追加当前帧 / 总帧。图片序号（1/6）在 setCounter，
    // 帧号（12/48）只进这里，两个计数不能混。
    // 「不显示动画信息控制」只作用于自动播放格式（GIF/MNG）：多页 TIFF 是
    // 逐帧翻阅，帧号不属于动画控制，开关关闭时仍然保留。
    if (m_frames && m_frames->hasFrames()
        && (!m_frames->animatable() || Config::instance().showAnimControl())) {
        // MNG 顺序模式不预扫描总帧数，frameCount()=0 表示未知，只显示当前帧号
        const int total = m_frames->frameCount();
        parts << (total > 1
                      ? tr("第 %1/%2 帧").arg(m_frames->frameIndex() + 1).arg(total)
                      : tr("第 %1 帧").arg(m_frames->frameIndex() + 1));
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
        jumpTo(p);
    }
}

// ---------------------------------------------------------------------------
// 浏览 / 视图
// ---------------------------------------------------------------------------

void MainWindow::onFit()        { setZoomMode(1); }
void MainWindow::onFitWidth()  { setZoomMode(2); }
void MainWindow::onActualSize() { setZoomMode(0); }
void MainWindow::onZoomIn()    { m_canvas->zoomIn(); }
void MainWindow::onZoomOut()   { m_canvas->zoomOut(); }

// 缩放维度统一入口：0=原始 1=适合窗口 2=适合宽度。只改缩放，不动双页。
void MainWindow::setZoomMode(int mode)
{
    m_zoomMode = mode;
    applyCurrentZoom();
    syncViewModeActions();
    updateShowModeIcon();
}

// 只用 m_zoomMode 适配画布。单图与双页拼图共用这一条 ——
// 以前双页分支硬编码 fitToWindow()，就是「双页 + 原始大小」被吃掉的根因。
void MainWindow::applyCurrentZoom()
{
    if (!m_canvas) {
        return;
    }
    switch (m_zoomMode) {
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
}

// 硬规则：菜单勾选、画面、内部状态三者同步。任何改状态的地方都调它。
void MainWindow::syncViewModeActions()
{
    if (actActualSize) {
        actActualSize->setChecked(m_zoomMode == 0);
    }
    if (actFit) {
        actFit->setChecked(m_zoomMode == 1);
    }
    if (actFitWidth) {
        actFitWidth->setChecked(m_zoomMode == 2);
    }
    if (actTwoL2R) {
        actTwoL2R->setChecked(m_spreadMode == 3);
    }
    if (actTwoR2L) {
        actTwoR2L->setChecked(m_spreadMode == 4);
    }
}

void MainWindow::updateShowModeIcon()
{
    if (m_toolbar) {
        m_toolbar->setShowModeIcon(m_spreadMode != 0 ? m_spreadMode : m_zoomMode);
    }
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
    else if (c == QLatin1String("fit"))        setZoomMode(1);
    else if (c == QLatin1String("fitwidth"))   setZoomMode(2);
    else if (c == QLatin1String("actual"))     setZoomMode(0);
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
    // 图像效果 / 几何（第一批功能验收用）
    else if (c == QLatin1String("negative")) { onNegative(); }
    else if (c.startsWith(QLatin1String("gamma:"))) {
        m_gamma = qBound(0.10, c.mid(6).toDouble(), 3.00);
        applyAdjustments(m_canvas ? m_canvas->viewCenterNormalized() : QPointF(0.5, 0.5));
    }
    else if (c == QLatin1String("rotatenone")) { resetGeometry(); }
    else if (c == QLatin1String("stretchon") || c == QLatin1String("stretchoff")) {
        onStretchSmall(c == QLatin1String("stretchon"));
    }
    else if (c.startsWith(QLatin1String("interp:"))) { setInterpolationMode(c.mid(7)); }
    // 双页维度：l2r/r2l = 直接设定方向（不 toggle，冒烟要确定性）；
    // off = 关双页（缩放保留）；toggle / toggle:r2l = 走菜单勾选项同一条 QAction::triggered，
    // 等价于按 7 / 6（验证「点已勾项=取消」的语义）。
    else if (c == QLatin1String("spread:l2r")) { setSpreadMode(3); }
    else if (c == QLatin1String("spread:r2l")) { setSpreadMode(4); }
    else if (c == QLatin1String("spread:off")) { setSpreadMode(0); }
    else if (c == QLatin1String("spread:toggle")) { actTwoL2R->trigger(); }
    else if (c == QLatin1String("spread:toggle:r2l")) { actTwoR2L->trigger(); }
    else if (c == QLatin1String("zoomlock")) { toggleZoomLock(); }
    else if (c.startsWith(QLatin1String("page:"))) {
        const int n = c.mid(5).toInt();
        if (m_dir->isValid() && n >= 1 && n <= m_dir->count()) {
            m_dir->setIndex(n - 1);
            const QString p = m_dir->current();
            if (!p.isEmpty()) loadAsync(p);
        }
    }
    // 循环开关（用于验收「首尾边界」两种模式）。applyRuntimeSettings() 会从配置回读，
    // 所以这里必须写配置，否则下一次加载就被覆盖。
    else if (c == QLatin1String("loopon") || c == QLatin1String("loopoff")) {
        const QString edge = (c == QLatin1String("loopon")) ? QStringLiteral("first")
                                                            : QStringLiteral("stop");
        Config::instance().settings().setValue(QStringLiteral("config/edge_action"), edge);
        m_dir->setEdgeAction(edge);
    }
    else if (c == QLatin1String("fullscreen")) toggleFullscreen();
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
    // hover:x:y —— 灌入合成光标（窗口内坐标）驱动热区；hover:off 交回真实鼠标。
    // 外部真挪鼠标的时序 + 窗口定位都会漂，测滞回不可信，这里直接喂点。
    else if (c.startsWith(QLatin1String("hover:"))) {
        const QString v = c.mid(6);
        if (v == QLatin1String("off")) {
            m_probeCursorActive = false;
        } else {
            const QStringList a = v.split(QLatin1Char(':'));
            if (a.size() == 2) {
                m_probeCursor = QPoint(a.at(0).toInt(), a.at(1).toInt());
                m_probeCursorActive = true;
            }
        }
        updateChromeProximity();
    }
    // —— 二级界面（截图冒烟用，非模态打开以便截到窗口本身）——
    else if (c == QLatin1String("settings")) {
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
    // 视觉回归探针：打开设置并切到指定页（settingspage:<序号>，0=一般）。
    // 不新增 SettingsDialog 接口——导航项就是带 objectName 的 QPushButton，点它等价于用户点击。
    // 「关于」按钮也挂 navItem，按文案排除后再取第 N 个，顺序与页面注册顺序一致。
    else if (c.startsWith(QLatin1String("settingspage:"))) {
        const QString key = c.mid(13).trimmed();
        // 允许 ASCII 别名（appearance = 外观），避免命令行中文编码在不同 shell 下失真
        const QString want = (key == QLatin1String("appearance"))
                                 ? QString::fromUtf8("\xE5\xA4\x96\xE8\xA7\x82")
                                 : key;
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        QTimer::singleShot(400, dlg, [dlg, want]() {
            QList<QPushButton *> navs;
            const auto btns = dlg->findChildren<QPushButton *>(QStringLiteral("navItem"));
            for (QPushButton *b : btns) {
                if (!b->text().contains(QStringLiteral("关于"))) {
                    navs.append(b);
                }
            }
            for (QPushButton *b : navs) {
                if (!want.isEmpty() && b->text().contains(want)) {
                    b->click();
                    return;
                }
            }
            const int index = want.toInt();
            if (index >= 0 && index < navs.size()) {
                navs.at(index)->click();
            }
        });
    }
    // —— 临时几何探针（M-05 返工取证）：打开设置→键盘页，dump 关键部件几何 ——
    // 取证用，M-05 交付前删除。输出追加到 Temp/keyboard-layout-20261105/geom.txt，
    // 每行带 dpr 与窗口尺寸，便于 100%/125% 两档对比。
    else if (c == QLatin1String("keygeom")) {
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        QTimer::singleShot(1500, dlg, [dlg]() {
            const auto btns = dlg->findChildren<QPushButton *>(QStringLiteral("navItem"));
            for (QPushButton *b : btns) {
                if (b->text().contains(QString::fromUtf8("\xE9\x94\xAE\xE7\x9B\x98"))) {
                    b->click();   // 「键盘」
                    break;
                }
            }
            QTimer::singleShot(900, dlg, [dlg]() {
                QFile f(QStringLiteral("Temp/keyboard-layout-20261105/geom.txt"));
                if (!f.open(QIODevice::Append | QIODevice::Text)) {
                    return;
                }
                QTextStream ts(&f);
                auto fmt = [](QWidget *w) {
                    if (!w) return QStringLiteral("null");
                    const QRect r = w->geometry();
                    return QStringLiteral("x=%1 y=%2 %3x%4 (bottom=%5)")
                        .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height())
                        .arg(r.y() + r.height());
                };
                auto *rowsArea = dlg->findChild<QScrollArea *>(QStringLiteral("keyRowsArea"));
                auto *rowsBox = dlg->findChild<QWidget *>(QStringLiteral("keyRowsBox"));
                auto *table = dlg->findChild<QWidget *>(QStringLiteral("keyTable"));
                auto *head = dlg->findChild<QWidget *>(QStringLiteral("keyHead"));
                auto *hint = dlg->findChild<QLabel *>(QStringLiteral("pageHint"));
                ts << "=== dpr=" << dlg->devicePixelRatioF()
                   << " dlgClient=" << dlg->width() << 'x' << dlg->height()
                   << " dlgFrame=" << fmt(dlg) << "\n";
                // 外层滚动区（内容区共用）：从 rowsArea 往上找最近的 QScrollArea
                QWidget *w = rowsArea ? rowsArea->parentWidget() : nullptr;
                QScrollArea *outer = nullptr;
                while (w) {
                    if (auto *sa = qobject_cast<QScrollArea *>(w)) {
                        if (sa != rowsArea) { outer = sa; break; }
                    }
                    w = w->parentWidget();
                }
                ts << "outerViewport=" << (outer ? fmt(outer->viewport()) : QStringLiteral("null"))
                   << "\n";
                ts << "hint=" << fmt(hint) << " head=" << fmt(head)
                   << " table=" << fmt(table) << "\n";
                ts << "rowsArea=" << fmt(rowsArea)
                   << " maxH=" << (rowsArea ? rowsArea->maximumHeight() : -1)
                   << " sizeHintH=" << (rowsArea ? rowsArea->sizeHint().height() : -1)
                   << " minH=" << (rowsArea ? rowsArea->minimumHeight() : -1)
                   << "\n";
                if (rowsArea) {
                    ts << "rowsAreaViewport=" << fmt(rowsArea->viewport())
                       << " vbarVisible=" << (rowsArea->verticalScrollBar()->isVisible() ? 1 : 0)
                       << " vbarValue=" << rowsArea->verticalScrollBar()->value()
                       << " vbarMax=" << rowsArea->verticalScrollBar()->maximum() << "\n";
                }
                ts << "rowsBox=" << fmt(rowsBox)
                   << " sizeHint=" << (rowsBox ? rowsBox->sizeHint().height() : -1) << "\n";
                if (rowsBox && rowsBox->layout()) {
                    if (auto *boxLay = qobject_cast<QVBoxLayout *>(rowsBox->layout())) {
                        ts << "rowsBoxLay: count=" << boxLay->count()
                           << " margins=" << boxLay->contentsMargins().left() << ','
                           << boxLay->contentsMargins().top() << ','
                           << boxLay->contentsMargins().right() << ','
                           << boxLay->contentsMargins().bottom()
                           << " spacing=" << boxLay->spacing() << "\n";
                    }
                    // 首行与末行几何：判断行组是否被推离顶部
                    for (int i : {0, 1}) {
                        if (auto *iw = rowsBox->layout()->itemAt(i)
                                           ? rowsBox->layout()->itemAt(i)->widget()
                                           : nullptr) {
                            ts << "  first[" << i << "] " << fmt(iw) << "\n";
                        }
                    }
                    // 最后两行（末行 + 末条分隔线）的几何：留白 = rowsArea 可视底 − 末行底
                    const int n = rowsBox->layout()->count();
                    for (int i = qMax(0, n - 2); i < n; ++i) {
                        if (auto *iw = rowsBox->layout()->itemAt(i)->widget()) {
                            ts << "  item[" << i << "] " << fmt(iw) << "\n";
                        }
                    }
                    const int lastRowBottom = [&]() {
                        for (int i = n - 1; i >= 0; --i) {
                            if (auto *iw = rowsBox->layout()->itemAt(i)->widget()) {
                                if (iw->height() > 4) {   // 分隔线高 1，行高 32
                                    return iw->geometry().y() + iw->geometry().height();
                                }
                            }
                        }
                        return -1;
                    }();
                    const int visBottom = rowsArea ? rowsArea->viewport()->height() : -1;
                    ts << "lastRowBottomInBox=" << lastRowBottom
                       << " viewportBottom=" << visBottom
                       << " gap=" << (visBottom - lastRowBottom) << "\n";
                }
                ts << "\n";
                f.close();

                // 第二次 dump：缩小窗口到 600 高，测整行吸附 + 滚到底留白
                dlg->resize(dlg->width(), 600);
                QTimer::singleShot(800, dlg, [dlg]() {
                    auto *ra = dlg->findChild<QScrollArea *>(QStringLiteral("keyRowsArea"));
                    auto *rb = dlg->findChild<QWidget *>(QStringLiteral("keyRowsBox"));
                    // 先滚到底
                    if (ra) ra->verticalScrollBar()->setValue(ra->verticalScrollBar()->maximum());
                    QTimer::singleShot(300, dlg, [dlg, ra, rb]() {
                        QFile f2(QStringLiteral("Temp/keyboard-layout-20261105/geom.txt"));
                        if (!f2.open(QIODevice::Append | QIODevice::Text)) return;
                        QTextStream ts2(&f2);
                        ts2 << "=== after resize to 600h (scrolled to bottom) ===\n";
                        ts2 << "dlgClient=" << dlg->width() << 'x' << dlg->height() << "\n";
                        ts2 << "rowsAreaH=" << (ra ? ra->height() : -1)
                            << " maxH=" << (ra ? ra->maximumHeight() : -1)
                            << " sizeHintH=" << (ra ? ra->sizeHint().height() : -1) << "\n";
                        if (ra) {
                            ts2 << "viewportH=" << ra->viewport()->height()
                                << " vbarVisible=" << (ra->verticalScrollBar()->isVisible() ? 1 : 0)
                                << " vbarMax=" << ra->verticalScrollBar()->maximum() << "\n";
                        }
                        if (rb && rb->layout() && ra) {
                            const int n = rb->layout()->count();
                            int lastRowBottom = -1;
                            for (int i = n - 1; i >= 0; --i) {
                                if (auto *iw = rb->layout()->itemAt(i)->widget()) {
                                    if (iw->height() > 4) { lastRowBottom = iw->geometry().y() + iw->height(); break; }
                                }
                            }
                            // 滚到底后，可视底 = viewportH + vbarValue，但末行在 box 坐标系
                            // 实际留白 = viewportH - (lastRowBottom - vbarValue)
                            const int vbarVal = ra->verticalScrollBar()->value();
                            const int gap = ra->viewport()->height() - (lastRowBottom - vbarVal);
                            ts2 << "lastRowBottomInBox=" << lastRowBottom
                                << " vbarVal=" << vbarVal
                                << " gapAtBottom=" << gap << "\n";
                        }
                        ts2 << "\n";
                    });
                });
            });
        });
    }
    // —— 键盘自定义验收探针（批次 D1，截图 + config 取证用）——
    else if (c == QLatin1String("keyconflict")) {
        // 把「删除」行热键改成 Ctrl+P（撞固定动作「打印」）走真实保存链路：
        // 应弹占用提示、冲突行高亮、config 不落盘。证据截图落 Temp/keys-conflict-*.png。
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        QTimer::singleShot(400, dlg, [dlg]() { dlg->probeKeyConflict(); });
    }
    else if (c == QLatin1String("keyreset")) {
        // footer「恢复默认」全链路（确认框自动答「是」）→ 14 项回出厂键位；
        // 之后 report 的 keys= 字段应回到出厂值。
        auto *dlg = new SettingsDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
        connect(dlg, &QObject::destroyed, this, [this]() { applyShortcutConfig(); });
        QTimer::singleShot(400, dlg, [dlg]() { dlg->probeResetDefaults(); });
    }
    else if (c == QLatin1String("convert")) {
        auto *dlg = new BatchConvertDialog(m_dir->files(), this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    }
    // 验收探针：模拟 Tab 开关信息面板（走 actToggleMeta toggled 同一链路），
    // 用于对比「smoke info 直开」与「Tab 切换」两条路径下面板几何差异
    else if (c == QLatin1String("tabtoggle")) {
        if (actToggleMeta) {
            actToggleMeta->setChecked(!actToggleMeta->isChecked());
        }
    }
    else if (c == QLatin1String("tabname")) {
        // 冒烟：合成 Shift+TAB（与真实按键同一条 keyPressEvent 链路）
        QKeyEvent ev(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(this, &ev);
    }
    else if (c.startsWith(QLatin1String("clickb:"))) {
        // 冒烟：向画布投递真实鼠标按下/松开（走 ImageCanvas 的映射判定），
        // 用法 clickb:left|right|x|y:<x>:<y>（画布视口坐标）
        const QStringList parts = c.split(QLatin1Char(':'));
        const QString which = parts.value(1);
        const Qt::MouseButton btn = (which == QLatin1String("left")) ? Qt::LeftButton
                                   : (which == QLatin1String("right")) ? Qt::RightButton
                                   : (which == QLatin1String("x")) ? Qt::XButton1
                                                                   : Qt::XButton2;
        const QPoint pos(parts.value(2).toInt(), parts.value(3).toInt());
        if (m_canvas) {
            QMouseEvent pr(QEvent::MouseButtonPress, pos,
                           m_canvas->viewport()->mapToGlobal(pos), btn, btn, Qt::NoModifier);
            QMouseEvent rl(QEvent::MouseButtonRelease, pos,
                           m_canvas->viewport()->mapToGlobal(pos), btn, Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(m_canvas->viewport(), &pr);
            QApplication::sendEvent(m_canvas->viewport(), &rl);
        }
    }
    else if (c == QLatin1String("info")) {
        if (m_metadata) {
            m_metadata->show();
            m_metadata->raise();
            applyChromeLayout();
        }
    }
    // 验收探针：切换信息浮层可见性（等价 Tab），验证折叠态跨翻图/重开保持
    else if (c == QLatin1String("infotoggle")) {
        if (m_metadata) {
            m_metadata->setVisible(!m_metadata->isVisible());
            applyChromeLayout();
        }
    }
    // 验收探针：折叠/展开直方图（等价点「▾ 直方图」标题），配合 next 验证
    // 折叠态不被翻图重置、卡片高度跟着收紧
    else if (c == QLatin1String("histtoggle")) {
        if (m_metadata && m_metadata->m_histToggle) {
            m_metadata->m_histToggle->click();
        }
    }
    // 验收探针：收藏/取消收藏当前图（等价按 B），配合 next 连打可验收
    // 「最大书签数」上限淘汰（B4）。
    else if (c == QLatin1String("fav")) {
        onToggleFavorite();
    }
    else if (c == QLatin1String("ctxmenu")) {
        QMenu *menu = buildContextMenu();
        menu->popup(mapToGlobal(QPoint(width() / 2 - 120, height() / 3)));
    }
    // 验收探针：直接弹出「浏览方式」子菜单 —— 勾选态（缩放✔ + 双页✔ 同亮）只能靠
    // --screenshot-ui 截子菜单本身来判定。按 actTwoL2R 定位，不依赖菜单文案/语言。
    else if (c == QLatin1String("viewmenu")) {
        QMenu *menu = buildContextMenu();
        for (QAction *a : menu->actions()) {
            if (a->menu() && a->menu()->actions().contains(actTwoL2R)) {
                a->menu()->popup(mapToGlobal(QPoint(width() / 2 - 120, height() / 3)));
                break;
            }
        }
    }
    // 验收探针：把右键菜单的「浏览导航」子菜单**自身**渲染成 png（第 12 期文案验收用）。
    // 用法 navmenu:<out.png>；按 ] 快捷键定位该子菜单，不依赖菜单文案/语言。
    // 弹窗在后台窗口里会被系统收回，--screenshot-ui 抓不到，所以直接 grab 控件。
    else if (c.startsWith(QLatin1String("navmenu"))) {
        QString out = QStringLiteral("Temp/navmenu.png");
        const int sep = c.indexOf(QLatin1Char(':'));
        if (sep > 0) {
            out = c.mid(sep + 1);
        }
        QMenu *menu = buildContextMenu();
        for (QAction *a : menu->actions()) {
            if (!a->menu()) {
                continue;
            }
            for (QAction *sub : a->menu()->actions()) {
                // 这两项是 addMenuItem 生成的纯展示项（快捷键只是文本），只能按文案定位
                if (sub->text().contains(QStringLiteral("文件夹"))) {
                    QMenu *nav = a->menu();
                    nav->popup(mapToGlobal(QPoint(width() / 2 - 120, height() / 3)));
                    QTimer::singleShot(400, nav, [nav, out]() {
                        nav->grab().save(out);
                    });
                    return;
                }
            }
        }
    }
    // 调试/验收：把缩放与边距的**真实数值**追加到日志（截图只能看个大概）
    // 验收探针：信息面板各层高度 dump（配合有/无 EXIF 图对比定位高度 bug）
    else if (c == QLatin1String("metadump")) {
        if (m_metadata) {
            m_metadata->dumpHeights(QStringLiteral("Temp/meta-dump.txt"));
        }
    }
    else if (c == QLatin1String("report")) {
        QFile f(QStringLiteral("Temp/zoom-report.txt"));
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            const QPointF ctr = m_canvas->viewCenterNormalized();
            ts << "mode=" << m_zoomMode
               << " zoom=" << QString::number(m_canvas->zoom(), 'f', 4)
               << " center=" << QString::number(ctr.x(), 'f', 4) << '/' << QString::number(ctr.y(), 'f', 4)
               << " fitWin=" << (m_canvas->isFitToWindow() ? 1 : 0)
               << " fitW=" << (m_canvas->isFitToWidth() ? 1 : 0)
               << " canvas=" << m_canvas->width() << 'x' << m_canvas->height()
               << " viewport=" << m_canvas->viewport()->width() << 'x' << m_canvas->viewport()->height()
               // 可拖行程实测：拖动验收/自由拖动返工用（range=单侧×2）
               << " scrH=" << m_canvas->horizontalScrollBar()->value() << '/'
               << m_canvas->horizontalScrollBar()->maximum()
               << " scrV=" << m_canvas->verticalScrollBar()->value() << '/'
               << m_canvas->verticalScrollBar()->maximum()
               << " margins=" << m_canvasInsets.left() << ',' << m_canvasInsets.top() << ','
               << m_canvasInsets.right() << ',' << m_canvasInsets.bottom()
               << " win=" << width() << 'x' << height()
               << " idx=" << m_dir->index() << '/' << m_dir->count()
               << " cap=" << (m_control ? m_control->size().width() : 0) << 'x'
               << (m_control ? m_control->size().height() : 0)
               << " page=" << (m_control ? m_control->page() : 0)
               << " rot=" << m_netRotation
               << " vm=" << (isSpreadMode() ? m_spreadMode : m_zoomMode)
               << " spread=" << m_spreadMode
               << " icon=" << (m_toolbar ? m_toolbar->showModeIcon() : -1)
               << " zlock=" << (m_zoomLocked ? 1 : 0)
               << " neg=" << (m_negative ? 1 : 0)
               << " gam=" << QString::number(m_gamma, 'f', 2)
               << " stretch=" << (m_canvas && m_canvas->stretchSmall() ? 1 : 0)
               << " interp=" << m_interpMode
               // 显隐状态机相位（0 Hidden / 1 Showing / 2 Visible / 3 Hiding），
               // 滞回与防闪烁只能靠它验证，截图看不出中间态
               << " dock=" << static_cast<int>(m_dockPhase)
               << " dockvis=" << (m_control && m_control->isVisible() ? 1 : 0)
               << " dockop=" << (m_dockOpacity ? QString::number(m_dockOpacity->opacity(), 'f', 2) : QStringLiteral("na"))
               << " top=" << static_cast<int>(m_topPhase)
               << " dpr=" << devicePixelRatioF()
               << " keys=" << shortcutsReport()
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
    // 双页阅览一次翻一跨页（2 张），普通浏览是 1 张
    const int step = isSpreadMode() ? 2 : 1;
    const int before = m_dir->index();
    const QString p = m_dir->advance(-step);
    if (p.isEmpty()) {
        if (m_toast) m_toast->showMessage(tr("已经是第一张图片"));
        return;
    }
    loadAsync(p);
    playPageturnTone();
    if (before - step < 0 && m_dir->count() > 1 && m_toast) {
        m_toast->showMessage(tr("已回到最后一张图片"));
    }
}

void MainWindow::onNext()
{
    if (!m_dir->isValid()) {
        return;
    }
    const int step = isSpreadMode() ? 2 : 1;
    const int before = m_dir->index();
    const QString p = m_dir->advance(step);
    if (p.isEmpty()) {
        if (m_toast) m_toast->showMessage(tr("已经是最后一张图片"));
        return;
    }
    loadAsync(p);
    playPageturnTone();
    if (before + step >= m_dir->count() && m_dir->count() > 1 && m_toast) {
        m_toast->showMessage(tr("已回到第一张图片"));
    }
}

// 10 张跳转：边界停住并提示（TEXT_MSG_FIRSTPAGE/LASTPAGE）
void MainWindow::onPrev10()
{
    if (qEnvironmentVariableIsSet("QUIKVIEW_PERF")) { qWarning("PERF onPrev10 fired"); }
    const QString p = m_dir->prev10();
    if (!p.isEmpty()) {
        loadAsync(p);
    } else if (m_dir->isValid()) {
        m_toast->showMessage(tr("已经是第一张图片"));
    }
}

void MainWindow::onNext10()
{
    if (qEnvironmentVariableIsSet("QUIKVIEW_PERF")) { qWarning("PERF onNext10 fired"); }
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
        jumpTo(m_dir->current());
    }
}

void MainWindow::onLast()
{
    if (m_dir->count() > 0) {
        m_dir->setIndex(m_dir->count() - 1);
        jumpTo(m_dir->current());
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
        jumpTo(m_currentPath);
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
        jumpTo(m_currentPath);
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

// Ctrl+0：停止放映（勾选态经由 actSlideshow toggled 回到 setSlideshow(false)）
void MainWindow::onSlideshowStop()
{
    if (m_slideshow) {
        actSlideshow->setChecked(false);
    }
}

// Ctrl+1..9：记住间隔并开始放映（已在放映则只换节奏）
void MainWindow::onSlideshowInterval(int sec)
{
    Config::instance().settings().setValue(QStringLiteral("config/slideshow_interval_ms"),
                                           sec * 1000);
    m_slideshowTimer->setInterval(sec * 1000);
    if (m_slideshow) {
        m_slideshowTimer->start();
    } else {
        actSlideshow->setChecked(true);
    }
}

// Ctrl+Alt+PageUp/Down：随机跳转。向后 = 只在当前位置之后取（越界回绕到前段），
// 向前对称；与原版 MENUSTR_PAGEMOVE_RAND_* 语义一致。
void MainWindow::onJumpRandom(bool forward)
{
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
    jumpTo(m_dir->current());
}

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
    stopAnimation();    // 手动翻帧后暂停自动播放，不自动恢复（需求硬规则）
    const QImage img = m_frames->nextFrame();
    if (img.isNull()) {
        return;
    }
    showAnimFrame(img);
    updateInfoOverlay();
}

void MainWindow::onPrevFrame()
{
    stopAnimation();
    const QImage img = m_frames->prevFrame();
    if (img.isNull()) {
        return;
    }
    showAnimFrame(img);
    updateInfoOverlay();
}

// ---------------------------------------------------------------------------
// 动图自动播放（P0：GIF）
// ---------------------------------------------------------------------------

// 动画帧上屏。不能走 ImageCanvas::setPixmap——那会 fitToWindow() 重置缩放，
// 用户播放中一缩放就被打回适合窗口。改用 PreservingView + 视口中心锚点：
// GIF 各帧同尺寸，缩放比例、平移位置、浏览方式在帧切换后原样保留。
void MainWindow::showAnimFrame(const QImage &img)
{
    // GIF 透明底色（键 "config/imageproc/gif_transparent"）：帧与首帧走同一铺底，
    // 否则底部黑区会随播放忽隐忽现。
    const bool gifPath = !m_currentPath.isEmpty()
                         && QFileInfo(m_currentPath).suffix().compare(QLatin1String("gif"),
                                                                     Qt::CaseInsensitive) == 0;
    const QImage display = gifPath ? applyGifBackdrop(img) : img;
    const qreal z = m_canvas->zoom();
    const QRectF sr = m_canvas->sceneRect();
    const QPointF center = m_canvas->mapToScene(m_canvas->viewport()->rect().center());
    QPointF anchor(0.5, 0.5);
    if (sr.width() > 1.0 && sr.height() > 1.0) {
        anchor = QPointF(center.x() / sr.width(), center.y() / sr.height());
    }
    m_currentImage = display;
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(display), anchor, z, false, false);
}

// "0" 默认调色板 = 不铺底，透明区透出画布底色；"3" 的自定义色暂无取色器，
// 先固定中灰 #808080（与 1/2 一样可观测），后续接取色器时只需改这里。
QImage MainWindow::applyGifBackdrop(const QImage &img) const
{
    const int mode = Config::instance().gifTransparent();
    if (mode <= 0 || !img.hasAlphaChannel()) {
        return img;
    }
    const QColor bg = (mode == 1) ? QColor(Qt::white)
                     : (mode == 2) ? QColor(Qt::black)
                                   : QColor(128, 128, 128);
    QImage out(img.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(bg);
    QPainter p(&out);
    p.drawImage(0, 0, img);
    p.end();
    return out;
}

QImage MainWindow::applyImage32Backdrop(const QImage &img) const
{
    if (!img.hasAlphaChannel()) {
        return img;
    }
    const int mode = Config::instance().img32Transparent();
    QImage out(img.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    if (mode == 1 || mode == 2) {
        out.fill(mode == 1 ? QColor(Qt::white) : QColor(Qt::black));
    } else {
        // 0 浅色网格 / 3 深色网格：16px 平铺块，图像坐标系固定 8px 方格。
        // 贴图在图像空间铺，缩放时随图整体放大缩小，不会每帧重算导致抖动。
        const QColor a = (mode == 0) ? QColor(0xF0, 0xF0, 0xF0) : QColor(0x30, 0x30, 0x30);
        const QColor b = (mode == 0) ? QColor(0xC8, 0xC8, 0xC8) : QColor(0x24, 0x24, 0x24);
        QImage tile(16, 16, QImage::Format_RGB32);
        tile.fill(a);
        {
            QPainter tp(&tile);
            tp.fillRect(QRect(0, 0, 8, 8), b);
            tp.fillRect(QRect(8, 8, 8, 8), b);
        }
        p.fillRect(out.rect(), QBrush(tile));
    }
    p.drawImage(0, 0, img);
    p.end();
    return out;
}

void MainWindow::startAnimation()
{
    if (m_animPlaying || !m_frames || !m_frames->hasFrames() || !m_frames->animatable()) {
        return;
    }
    m_animPlaying = true;
    m_animClock.start();
    m_animNextDeadline = 0;
    m_animTimer->start(1);
}

void MainWindow::stopAnimation()
{
    m_animPlaying = false;
    if (m_animTimer) {
        m_animTimer->stop();
    }
}

// 单发定时器 tick：推一帧、按文件自带延迟重设下一次间隔。
// 硬规则：帧延迟读 QImageReader::nextImageDelay()（FrameNavigator 已在
// read() 前记录）；累计误差用绝对时间点校正，绝不允许每帧 sleep 固定值。
void MainWindow::animationTick()
{
    if (!m_animPlaying || !m_frames || !m_frames->hasFrames()) {
        return;
    }
    const QImage img = m_frames->nextFrame();
    if (img.isNull()) {
        // 解码失败就停，不许空转烧 CPU
        stopAnimation();
        return;
    }
    showAnimFrame(img);
    updateInfoOverlay();
    int delay = m_frames->lastDelay();
    if (delay <= 10) {
        delay = 100;    // 0/超短延迟按 GIF 惯例当 100ms，防 busy-loop
    }
    m_animNextDeadline += delay;
    const qint64 now = m_animClock.elapsed();
    if (m_animNextDeadline < now) {
        // 落后已超过一个周期（解码/绘制太慢）：放弃追帧重新对齐，
        // 避免「连发补偿」变成全速空转。
        m_animNextDeadline = now + delay;
    }
    m_animTimer->start(int(qMax<qint64>(1, m_animNextDeadline - now)));
}

// ---------------------------------------------------------------------------
// 编辑
// ---------------------------------------------------------------------------

// 原地旋转：保留当前缩放比例与「视口中心看到的那一点」，转完后这一点仍在
// 视口中心，所以图像不会跳位、也不会被悄悄退回适配窗口。
void MainWindow::rotateImage(int degrees)
{
    if (m_sourceImage.isNull() || !m_canvas) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();

    // 净角度累加后取模：反复转 90° 四次回到 0，且每次都从**源图**重算，
    // 不会在已经转过的图上再转（那样重采样损失会一遍遍叠上去）。
    m_netRotation = ((m_netRotation + degrees) % 360 + 360) % 360;

    // 归一化锚点跟着画布一起转：顺 90°(u,v)→(1-v,u)，逆 90°(u,v)→(v,1-u)
    QPointF anchor = n;
    if (degrees == 90) {
        anchor = QPointF(1.0 - n.y(), n.x());
    } else if (degrees == -90) {
        anchor = QPointF(n.y(), 1.0 - n.x());
    } else if (degrees == 180 || degrees == -180) {
        anchor = QPointF(1.0 - n.x(), 1.0 - n.y());
    }
    rebuildFromSource(anchor);
}

// 从源图重新应用「净旋转 + 翻转」，再走一遍反相 / Gamma / 图片效果，最后保视图刷新。
// 所有几何类操作（旋转、翻转、不旋转）都收敛到这一个入口，规则只有一份。
void MainWindow::rebuildFromSource(const QPointF &anchor)
{
    if (m_sourceImage.isNull() || !m_canvas) {
        return;
    }
    QTransform t;
    t.rotate(m_netRotation);
    if (m_flipH) {
        t.scale(-1, 1);   // 屏幕方向左右镜像
    }
    if (m_flipV) {
        t.scale(1, -1);
    }
    m_originalImage = m_sourceImage.transformed(t);
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
    applyAdjustments(anchor);
    updateInfoOverlay();
}

// 不旋转：清空所有几何变换，回到文件原始像素
void MainWindow::resetGeometry()
{
    if (m_sourceImage.isNull() || !m_canvas) {
        return;
    }
    if (m_netRotation == 0 && !m_flipH && !m_flipV) {
        if (m_toast) {
            m_toast->showMessage(tr("图像未旋转"));
        }
        return;
    }
    m_netRotation = 0;
    m_flipH = false;
    m_flipV = false;
    rebuildFromSource(QPointF(0.5, 0.5));
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
    if (m_sourceImage.isNull()) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();
    m_flipH = !m_flipH;
    // 屏幕左右镜像：锚点的 u 跟着翻，v 不动
    rebuildFromSource(QPointF(1.0 - n.x(), n.y()));
}

void MainWindow::onFlipV()
{
    if (m_sourceImage.isNull()) {
        return;
    }
    const QPointF n = m_canvas->viewCenterNormalized();
    m_flipV = !m_flipV;
    rebuildFromSource(QPointF(n.x(), 1.0 - n.y()));
}

void MainWindow::onEffect(int mode)
{
    m_effect = mode;
    applyEffect();
}

void MainWindow::applyEffect()
{
    applyAdjustments(m_canvas ? m_canvas->viewCenterNormalized() : QPointF(0.5, 0.5));
}

// 统一渲染管线：m_originalImage（几何已就位）→ 反相 → Gamma → 减淡/锐化 → 画布。
// 顺序固定，每一步都读上一步的副本、不写回 m_originalImage，
// 所以反复切换（反相开关几十次、Gamma 拖着滑）不会累积误差或越调越糊。
void MainWindow::applyAdjustments(const QPointF &anchor)
{
    if (m_originalImage.isNull()) {
        return;
    }
    QImage img = m_originalImage;
    if (m_negative) {
        img = applyNegative(img);
    }
    if (!qFuzzyCompare(m_gamma, 1.0)) {
        img = applyGamma(img, m_gamma);
    }
    switch (m_effect) {
    case 1:
        img = applySoften(img);
        break;
    case 2:
        img = applySharpen(applySoften(img));
        break;
    default:
        break;
    }
    m_currentImage = img;
    if (!m_canvas) {
        return;
    }
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(m_currentImage), anchor,
                                      m_canvas->zoom(), m_canvas->isFitToWindow(),
                                      m_canvas->isFitToWidth());
}

void MainWindow::onNegative()
{
    if (m_originalImage.isNull() || !m_canvas) {
        return;
    }
    m_negative = !m_negative;
    applyAdjustments(m_canvas->viewCenterNormalized());
    if (m_toast) {
        m_toast->showMessage(m_negative ? tr("反色：开") : tr("反色：关"));
    }
}

void MainWindow::onGamma()
{
    if (m_originalImage.isNull() || !m_canvas) {
        return;
    }
    GammaDialog dlg(m_gamma, this);
    // 拖动滑块实时预览：直接改 m_gamma 再重算，取消时回滚原值。
    const qreal before = m_gamma;
    connect(&dlg, &GammaDialog::valueChanged, this, [this](qreal g) {
        m_gamma = g;
        applyAdjustments(m_canvas->viewCenterNormalized());
    });
    if (dlg.exec() != QDialog::Accepted) {
        m_gamma = before;
        applyAdjustments(m_canvas->viewCenterNormalized());
        return;
    }
    m_gamma = dlg.value();
    applyAdjustments(m_canvas->viewCenterNormalized());
    if (m_toast) {
        m_toast->showMessage(qFuzzyCompare(m_gamma, 1.0)
                                 ? tr("Gamma 补偿：已还原")
                                 : tr("Gamma 补偿：%1").arg(m_gamma, 0, 'f', 2));
    }
}

// 拉伸小图像（原版 Z）：只影响「适合窗口」——开启后小于窗口的图会等比放大填满。
void MainWindow::onStretchSmall(bool on)
{
    Config::instance().settings().setValue(QStringLiteral("config/stretch_small"), on);
    if (m_canvas) {
        m_canvas->setStretchSmall(on);
    }
    if (m_toast) {
        m_toast->showMessage(on ? tr("拉伸小图像：开") : tr("拉伸小图像：关"));
    }
}

// Z：拉伸小图开关（同步勾选态；具体行为在 onStretchSmall）
void MainWindow::onToggleStretchSmall()
{
    onStretchSmall(!Config::instance().stretchSmall());
    actStretchSmall->setChecked(Config::instance().stretchSmall());
}

void MainWindow::setInterpolationMode(const QString &mode)
{
    int m = 1;
    if (mode == QLatin1String("bilinear")) {
        m = 0;
    } else if (mode == QLatin1String("lanczos")) {
        m = 2;
    }
    m_interpMode = m;
    if (m_canvas) {
        m_canvas->setInterpolationMode(m);
    }
    Config::instance().settings().setValue(QStringLiteral("config/interpolation"), mode);
    // 双向桥：设置页「插值算法」下拉绑的是 config/imageproc/interpolate（"0"/"1"/"2"），
    // 菜单切插值后必须同步写回，否则设置页回显的还是旧档位（bicubic→1、lanczos→2）。
    Config::instance().settings().setValue(QStringLiteral("config/imageproc/interpolate"),
                                           QString::number(m));
    if (actInterpFast) {
        actInterpFast->setChecked(m == 0);
    }
    if (actInterpSmooth) {
        actInterpSmooth->setChecked(m == 1);
    }
    if (actInterpLanczos) {
        actInterpLanczos->setChecked(m == 2);
    }
}

void MainWindow::resetEffect()
{
    m_originalImage = m_currentImage;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
}

// 锁定缩放（原版 TEXT_MSG_ZOOMFIX_ON/OFF）：开启后翻页不做适配，沿用同一比例。
// 和「适合窗口」是互斥的两条路：锁定时改窗口尺寸也不会重算，符合「锁定」的直觉。
void MainWindow::toggleZoomLock()
{
    m_zoomLocked = !m_zoomLocked;
    if (m_zoomLocked) {
        // 开锁瞬间记住当前缩放方式，锁定期间不再被改写（双页维度不参与）。
        if (m_zoomModeBeforeLock < 0) {
            m_zoomModeBeforeLock = m_zoomMode;
        }
    } else {
        // 解锁还原：没有记录（例如启动时配置就是锁定态）则回到「适合窗口」。
        m_zoomMode = (m_zoomModeBeforeLock >= 0) ? m_zoomModeBeforeLock : 1;
        m_zoomModeBeforeLock = -1;
        syncViewModeActions();
        updateShowModeIcon();
    }
    Config::instance().settings().setValue(QStringLiteral("config/zoom_fix"), m_zoomLocked);
    if (m_toast) {
        m_toast->showMessage(m_zoomLocked ? tr("锁定缩放：开") : tr("锁定缩放：关"));
    }
}

// 选择图片（原版 MENUSTR_PAGESELECT / Enter）：按序号跳到序列里的某一张。
void MainWindow::onPageSelect()
{
    if (!m_dir || !m_dir->isValid() || m_dir->count() <= 1) {
        if (m_toast) {
            m_toast->showMessage(tr("当前目录没有可跳转的图片序列"));
        }
        return;
    }
    bool ok = false;
    const int cur = m_dir->index() + 1;
    const int total = m_dir->count();
    const int n = QInputDialog::getInt(this, tr("选择图片"),
                                       tr("跳到第几张（共 %1 张）：").arg(total),
                                       cur, 1, total, 1, &ok);
    if (!ok || n == cur) {
        return;
    }
    m_dir->setIndex(n - 1);
    const QString p = m_dir->current();
    if (!p.isEmpty()) {
        jumpTo(p);
    }
}

// 编辑书签（F3）：列出已存书签，支持打开 / 删除选中 / 全部删除。
void MainWindow::onEditBookmarks()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("编辑书签"));
    dlg.setModal(true);
    dlg.setStyleSheet(UiStyle::dialogQss());
    dlg.resize(620, 420);

    auto *root = new QVBoxLayout(&dlg);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(10);

    auto *hint = new QLabel(tr("双击一行可直接打开；书签就是收藏过的图片路径。"), &dlg);
    hint->setStyleSheet(QStringLiteral("color:%1;font-size:12px;")
                            .arg(QLatin1String(UiStyle::HintText)));
    root->addWidget(hint);

    auto *list = new QListWidget(&dlg);
    list->setAlternatingRowColors(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    root->addWidget(list, 1);

    const QStringList favs = Config::instance().favorites();
    for (const QString &f : favs) {
        auto *item = new QListWidgetItem(QFileInfo(f).fileName() + QStringLiteral("    ") + f, list);
        item->setData(Qt::UserRole, f);
        item->setToolTip(f);
    }
    if (favs.isEmpty()) {
        auto *empty = new QListWidgetItem(tr("（还没有书签，按 B 可把当前图片加为书签）"), list);
        empty->setFlags(Qt::NoItemFlags);
    }

    auto *buttons = new QDialogButtonBox(&dlg);
    auto *openBtn = buttons->addButton(tr("打开"), QDialogButtonBox::ActionRole);
    auto *delBtn = buttons->addButton(tr("删除选中"), QDialogButtonBox::DestructiveRole);
    auto *clearBtn = buttons->addButton(tr("全部删除"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(QDialogButtonBox::Close);
    for (auto *b : buttons->buttons()) {
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(88);
    }
    root->addWidget(buttons);

    // 书签改动立即落盘：对话框关掉以后菜单里的「最近/书签」列表要跟着变。
    auto persist = [this, list]() {
        QStringList out;
        for (int i = 0; i < list->count(); ++i) {
            const QString p = list->item(i)->data(Qt::UserRole).toString();
            if (!p.isEmpty()) {
                out << p;
            }
        }
        Config::instance().settings().setValue(QStringLiteral("favorites"), out);
        refreshLibraryMenus();
        return out.size();
    };

    connect(openBtn, &QPushButton::clicked, &dlg, [this, list]() {
        auto *cur = list->currentItem();
        if (!cur || cur->data(Qt::UserRole).toString().isEmpty()) {
            return;
        }
        const QString p = cur->data(Qt::UserRole).toString();
        if (!QFileInfo::exists(p)) {
            m_toast->showMessage(tr("文件已不存在"));
            return;
        }
        jumpTo(p);
    });
    connect(list, &QListWidget::itemDoubleClicked, &dlg, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const QString p = item->data(Qt::UserRole).toString();
        if (p.isEmpty()) {
            return;
        }
        if (!QFileInfo::exists(p)) {
            m_toast->showMessage(tr("文件已不存在"));
            return;
        }
        jumpTo(p);
    });
    connect(delBtn, &QPushButton::clicked, &dlg, [list, persist]() {
        const auto sel = list->selectedItems();
        for (auto *it : sel) {
            delete it;
        }
        persist();
    });
    connect(clearBtn, &QPushButton::clicked, &dlg, [list, persist]() {
        list->clear();
        persist();
    });
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.exec();
}

// ---------------------------------------------------------------------------
// 智能双页阅览（原版 MENUSTR_SHOWMODE_TWO_L2R / R2L，快捷键 7 / 6）
// 实现方式：把相邻两页横向拼成一张大图，当作单图交给画布。这样 fit / 缩放 /
// 拖动 / 旋转 / 反相 / Gamma 全部自动沿用单图那套逻辑，不需要在画布层开双槽，
// 也不会出现「左页缩了右页没缩」这类两套坐标打架的问题。
// ---------------------------------------------------------------------------

bool MainWindow::isSpreadMode() const
{
    return m_spreadMode == 3 || m_spreadMode == 4;
}

// 同步解码：双页拼图需要立刻拿到另一页，异步缓存那条链路来不及。
QImage MainWindow::readImageSync(const QString &path) const
{
    if (path.isEmpty()) {
        return QImage();
    }
    QString err;
    QImage img = loadRawImage(path, &err, m_hdrTonemap);
    if (!img.isNull()) {
        return img;
    }
    img = loadHeifImage(path, &err, m_hdrTonemap);
    if (!img.isNull()) {
        return img;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage viaReader = reader.read();
    return m_hdrTonemap ? ToneMap::toneMapToSdr(viaReader) : viaReader;
}

QString MainWindow::spreadSecondPath() const
{
    if (!m_dir || !m_dir->isValid()) {
        return QString();
    }
    const int n = m_dir->index() + 1;
    return n < m_dir->count() ? m_dir->files().at(n) : QString();
}

namespace {

} // namespace

void MainWindow::refreshSpread()
{
    if (!m_canvas || m_currentPath.isEmpty() || !isSpreadMode()) {
        return;
    }
    const QImage first = readImageSync(m_currentPath);
    if (first.isNull()) {
        return;
    }
    const QImage second = readImageSync(spreadSecondPath());
    // 左←右（漫画式）：第一页在右边、第二页在左边
    m_sourceImage = (m_spreadMode == 3) ? composeSpread(first, second)
                                        : composeSpread(second, first);
    m_netRotation = 0;
    m_flipH = false;
    m_flipV = false;
    m_originalImage = m_sourceImage;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
    applyAdjustments(QPointF(0.5, 0.5));
    applyCurrentZoom();   // 拼图完成后按 m_zoomMode 适配（0/1/2 都由这一条决定）
    updateInfoOverlay();
}

// 双页维度统一入口：mode 0=关 3=左→右 4=左←右。缩放维度原样保留。
void MainWindow::setSpreadMode(int mode)
{
    if (mode != 0 && mode != 3 && mode != 4) {
        mode = 0;
    }
    if (mode == m_spreadMode) {
        syncViewModeActions();
        updateShowModeIcon();
        return;
    }
    const int old = m_spreadMode;
    m_spreadMode = mode;
    if (mode != 0) {
        // 开双页：重新拼跨页，然后按当前缩放方式适配（不再硬编码适合窗口）
        if (!m_currentPath.isEmpty()) {
            refreshSpread();
        }
        if (m_toast) {
            m_toast->showMessage(mode == 3 ? tr("智能双页阅览（左→右）")
                                           : tr("智能双页阅览（左←右）"));
        }
    } else if (old != 0 && !m_currentPath.isEmpty()) {
        // 关双页：画面必须还原成单图（以前只改状态，画面留着拼图 → 状态/画面互相矛盾）。
        // 走 showImage 走一次完整流程：重置旋转/效果、按 m_zoomMode 适配、元数据与标题一致。
        const QImage single = readImageSync(m_currentPath);
        if (!single.isNull()) {
            showImage(single, m_currentPath);
        } else {
            applyCurrentZoom();
        }
    } else {
        applyCurrentZoom();
    }
    syncViewModeActions();
    updateShowModeIcon();
}

void MainWindow::onCrop()
{
    m_cropMode = !m_cropMode;
    m_canvas->setCropMode(m_cropMode);
    m_canvas->refreshEdgeHover();   // 裁剪进出：箭头收起/恢复按真实光标重算
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
                                            m_interpMode == 0 ? Qt::FastTransformation
                                                              : Qt::SmoothTransformation);
    showImage(m_currentImage, m_currentPath);
}

// ---------------------------------------------------------------------------
// 文件操作
// ---------------------------------------------------------------------------

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("打开图片"), QString(), DirectoryLoader::imageFilter());
    if (!path.isEmpty()) {
        // 文件→打开：按图片尺寸调窗（受设置「根据图片大小调节窗口大小」控制）
        openFileFitting(path);
    }
}

QStringList MainWindow::listImagesInDir(const QString &dirPath)
{
    const QDir dir(dirPath);
    const QStringList entries = dir.entryList(DirectoryLoader::imageExtensions(),
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
    const int before = Config::instance().favorites().size();
    const bool wasFav = Config::instance().favorites().contains(m_currentPath);
    Config::instance().toggleFavorite(m_currentPath);
    refreshLibraryMenus();
    if (wasFav) {
        m_toast->showMessage(tr("已取消收藏"));
        return;
    }
    // 上限淘汰提示（键 config/bookmark/max_bookmarks，Config::maxBookmarks）：
    // 加入前已到上限 → 这一次必然挤掉最早的那条，如实告知而不是只说「已收藏」。
    const int cap = Config::instance().maxBookmarks();
    if (before >= cap) {
        m_toast->showMessage(tr("已收藏（书签已满 %1 个，最早的书签已移除）").arg(cap));
    } else {
        m_toast->showMessage(tr("已收藏"));
    }
}

// 鼠标按键映射（C9~C12）：动作名 → 已有槽。none/pan 不在这里出现（画布已过滤）。
void MainWindow::onMouseAction(const QString &action)
{
    if (action == QLatin1String("next")) {
        onNext();
    } else if (action == QLatin1String("prev")) {
        onPrev();
    } else if (action == QLatin1String("next_frame")) {
        onNextFrame();
    } else if (action == QLatin1String("prev_frame")) {
        onPrevFrame();
    } else if (action == QLatin1String("zoom_in")) {
        onZoomIn();
    } else if (action == QLatin1String("zoom_out")) {
        onZoomOut();
    } else if (action == QLatin1String("actual_size")) {
        setZoomMode(0);
    } else if (action == QLatin1String("fit")) {
        setZoomMode(1);
    } else if (action == QLatin1String("slideshow")) {
        if (actSlideshow) actSlideshow->trigger();
    } else if (action == QLatin1String("fullscreen")) {
        toggleFullscreen();
    } else if (action == QLatin1String("close")) {
        close();
    } else if (action == QLatin1String("context_menu")) {
        QMenu *menu = buildContextMenu();
        menu->popup(QCursor::pos());
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
    } else if (action == QLatin1String("open")) {
        onOpen();
    } else if (action == QLatin1String("openfolder")) {
        onOpenFolder();
    } else if (action == QLatin1String("crop")) {
        onCrop();
    } else if (action == QLatin1String("favorite")) {
        onToggleFavorite();
    } else if (action == QLatin1String("del")) {
        onDeleteFile();
    }
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

void MainWindow::syncThemeActions()
{
    // 勾选态=实际生效的模式：跟随系统时随系统深浅色走，菜单里显示的是真相。
    // 程序化 setChecked 必须挡信号：否则启动/系统切换时 toggled 会把本次勾选
    // 当成用户点击，反向写库并把「跟随系统」覆盖成显式 dark/light（实测翻车）。
    if (!actDark)
        return;
    const bool dark = Theme::resolveMode(Config::instance().theme()) == Theme::Mode::Dark;
    if (actDark->isChecked() == dark)
        return;
    const QSignalBlocker b(actDark);
    actDark->setChecked(dark);
}

// ---------------------------------------------------------------------------
// SET-02 即时预览：设置页会话中的运行态下发 / 快照
// ---------------------------------------------------------------------------
bool MainWindow::applyLiveSetting(const QString &key, const QVariant &value, QString *error)
{
    // 验收注入：QUIKVIEW_TEST_LIVE_FAIL=1 时第一次即时应用强制失败，用来验证
    // 「应用失败 → 控件与运行态回退、不写盘、错误可见」。只影响进程内首次调用。
    static bool failOnce = qEnvironmentVariableIsSet("QUIKVIEW_TEST_LIVE_FAIL");
    if (failOnce) {
        failOnce = false;
        if (error)
            *error = tr("模拟失败：信息面板拒绝本次字号切换。");
        return false;
    }

    if (key == QLatin1String("view/exif_small_font")) {
        const bool small = value.toBool();
        if (m_metadata)
            m_metadata->setSmallFont(small);
        if (m_statusOverlay)
            m_statusOverlay->setSmallFont(small);
        // 字号变化会改信息面板高度：预览时必须立刻按新尺寸排浮层，否则要等到
        // 关闭设置页才「跳」一下。重建布局是延迟激活的，紧接着再排一次校正。
        applyChromeLayout();
        QTimer::singleShot(0, this, [this]() { applyChromeLayout(); });
        return true;
    }

    if (key == QLatin1String("view/show_side_arrow")) {
        if (m_canvas)
            m_canvas->setSideArrowsEnabled(value.toBool());
        return true;
    }

    if (key == QLatin1String("view/show_zoom_control")) {
        // 运行态覆盖：会话内不写配置；关掉立即收浮标，打开立即按当前缩放补一次。
        m_liveZoomControlOverride = value.toBool();
        m_liveZoomControlOverrideSet = true;
        if (!m_liveZoomControlOverride) {
            if (m_zoomBadge)
                m_zoomBadge->hide();
        } else if (m_zoomBadge && m_canvas && !m_currentImage.isNull()) {
            refreshZoomBadge(m_canvas->zoom());
        }
        return true;
    }

    if (error)
        *error = tr("未知的即时设置项：%1").arg(key);
    return false;
}

QVariant MainWindow::liveSettingValue(const QString &key) const
{
    if (key == QLatin1String("view/exif_small_font")) {
        if (m_metadata)
            return m_metadata->smallFont();
        if (m_statusOverlay)
            return m_statusOverlay->smallFont();
        return Config::instance().exifSmallFont();
    }
    if (key == QLatin1String("view/show_side_arrow"))
        return m_canvas ? m_canvas->sideArrowsEnabled() : true;
    if (key == QLatin1String("view/show_zoom_control")) {
        return m_liveZoomControlOverrideSet ? m_liveZoomControlOverride
                                            : Config::instance().showZoomControl();
    }
    return QVariant();
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    dlg.exec();
    // 预览会话结束：清掉运行态覆盖，之后一律以配置为准（保存=新值，取消=旧值）。
    m_liveZoomControlOverrideSet = false;
    // 设置页可能改了主题（三档）；按当前取值重新解析应用（system 时随系统）。
    // 磨砂开关要先落进 UiStyle，否则 appQss() 里菜单底还是旧的不透明度。
    UiStyle::setBlurMaterial(Config::instance().blurMaterial());
    Theme::applySetting(*qApp, Config::instance().theme());
    syncThemeActions();   // 菜单勾选态跟着新主题走
    m_slideshowTimer->setInterval(Config::instance().slideshowIntervalMs());
    applyRuntimeSettings();
    applyShortcutConfig();   // 键盘自定义：保存立即生效；取消时配置已回滚，重装回旧值
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("关于 QuikView"),
                       tr("<h3>QuikView</h3>"
                          "<p>版本 %1</p>"
                          "<p>MIT 许可证开源，零遥测、零推广。</p>")
                           .arg(QStringLiteral(QUIKVIEW_VERSION)));
}

// ---------------------------------------------------------------------------
// 键盘 / 拖放
// ---------------------------------------------------------------------------

// 「顶端菜单隐藏时显示文件名 (Shift+TAB)」：胶囊（控制栏）隐藏时才响应，
// 画布顶部居中显示「文件名 + 序号」，3s 后淡出（键 view/overlay_filename）。
void MainWindow::showNameOverlay()
{
    if (!m_canvas || !Config::instance().overlayFilename()) {
        return;
    }
    if (!m_nameOverlay) {
        m_nameOverlay = new QLabel(m_canvas->viewport());
        m_nameOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_nameOverlay->setAlignment(Qt::AlignCenter);
        m_nameOverlay->setStyleSheet(QStringLiteral(
            "background: %1; color: %2; border-radius: 6px;"
            "padding: 6px 14px; font-size: 13px;")
            .arg(QLatin1String(UiStyle::OverlayBg), QLatin1String(UiStyle::OverlayText)));
        m_nameOverlayFx = new QGraphicsOpacityEffect(m_nameOverlay);
        m_nameOverlayFx->setOpacity(0.0);
        m_nameOverlay->setGraphicsEffect(m_nameOverlayFx);
        m_nameOverlayFade = new QPropertyAnimation(m_nameOverlayFx, "opacity", this);
        m_nameOverlayTimer = new QTimer(this);
        m_nameOverlayTimer->setSingleShot(true);
        m_nameOverlayTimer->setInterval(3000);   // 交办单验收：3s 消失
        connect(m_nameOverlayTimer, &QTimer::timeout, this, [this]() {
            m_nameOverlayFade->stop();
            m_nameOverlayFade->setDuration(280);
            m_nameOverlayFade->setStartValue(m_nameOverlayFx->opacity());
            m_nameOverlayFade->setEndValue(0.0);
            m_nameOverlayFade->start();
        });
    }
    const QString name = m_currentPath.isEmpty()
                             ? tr("(无图片)") : QFileInfo(m_currentPath).fileName();
    QString text = name;
    if (m_dir && m_dir->isValid() && m_dir->count() > 0) {
        text += QStringLiteral("   %1 / %2").arg(m_dir->index() + 1).arg(m_dir->count());
    }
    m_nameOverlay->setText(text);
    placeNameOverlay();
    m_nameOverlay->show();
    m_nameOverlay->raise();
    m_nameOverlayFade->stop();
    m_nameOverlayFx->setOpacity(1.0);
    m_nameOverlayTimer->start();
}

void MainWindow::placeNameOverlay()
{
    if (!m_nameOverlay || !m_canvas) {
        return;
    }
    const QRect vp = m_canvas->viewport()->rect();
    m_nameOverlay->adjustSize();
    const int w = qMin(m_nameOverlay->width(), vp.width() - 32);
    const int h = m_nameOverlay->height();
    m_nameOverlay->setGeometry(vp.x() + (vp.width() - w) / 2, vp.y() + 16, w, h);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
    case Qt::Key_Down: {
        // ↑/↓ = 上移/下移（原版语义，不是翻页）；Shift 加大到半屏。
        // ←/→/PageUp/PageDown 已交给 QAction 快捷键，这里不再重复处理。
        const qreal frac = (event->modifiers() & Qt::ShiftModifier) ? 0.5 : 0.1;
        m_canvas->scrollVerticallyBy(event->key() == Qt::Key_Down ? frac : -frac);
        event->accept();
        return;
    }
    case Qt::Key_Backtab:
        // Shift+TAB（Qt 归一成 Backtab）：胶囊隐藏时显示文件名浮层；
        // 胶囊可见时按无效，直接吞掉，避免落到焦点导航。
        if (!m_dockVisible) {
            showNameOverlay();
        }
        event->accept();
        return;
    case Qt::Key_Space:
        // 空格：多帧动图 = 播放/暂停；静态图保持原行为（下一张）
        if (m_frames && m_frames->hasFrames() && m_frames->animatable()) {
            if (m_animPlaying) {
                stopAnimation();
                m_toast->showMessage(tr("已暂停 · 空格继续"));
            } else {
                startAnimation();
                m_toast->showMessage(tr("GIF 播放中 · 空格暂停"));
            }
            return;
        }
        onNext();
        return;
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
        toggleFullscreen();
        break;
    default:
        break;
    }
    QMainWindow::keyPressEvent(event);
}

// ---------------------------------------------------------------------------
// 外部拖放（Explorer / 微信 / QQ / 截图工具）
//
// 两条硬约束：
// 1) enter / move 阶段只枚举 formats，绝不读数据内容。微信、QQ 这类源在松手前
//    不给数据（延迟渲染），此时读内容要么落空、要么把 UI 卡在对方的 GetData 上。
// 2) 口径是「宁多收勿错杀」：enter 多收的代价只是光标变一下，误杀的代价是
//    用户拖不进来而且没有任何提示。真正的筛选放在 drop。
// ---------------------------------------------------------------------------
namespace {

bool dropMimeLooksUsable(const QMimeData *mime)
{
    if (!mime) {
        return false;
    }
    if (mime->hasUrls() || mime->hasImage()) {
        return true;
    }
    const QStringList formats = mime->formats();
    for (const QString &format : formats) {
        if (format == QLatin1String("text/uri-list")
            || format == QLatin1String("text/plain")) {
            return true;
        }
    }
    // 其余格式也收下：Qt 对 CF_HDROP / 私有剪贴板格式的包装名各不相同，
    // 逐个点名会漏；这里只要枚举出格式就当作潜在可拖放对象。
    return !formats.isEmpty();
}

void acceptDropAsCopy(QDropEvent *event)
{
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

// 多选拖入时 urls() 里可能先出现 http 链接 / 已删除的路径，
// first() 直接用会静默失败，这里取第一个真实存在的本地文件。
QString firstExistingLocalFile(const QMimeData *mime)
{
    if (!mime) {
        return {};
    }
    const QList<QUrl> urls = mime->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            return path;
        }
    }
    return {};
}

// 纯位图拖放没有文件名，落盘到 %TEMP%\QuikView\drop-<时间戳>.png 再走 openFile，
// 这样标题栏、目录索引、后续翻页都按普通文件处理，不额外开一条显示通道。
QString saveDroppedImage(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }
    const QString dir = QDir::tempPath() + QStringLiteral("/QuikView");
    if (!QDir().mkpath(dir)) {
        return {};
    }
    const QString name = QStringLiteral("drop-")
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"))
        + QStringLiteral(".png");
    const QString path = dir + QLatin1Char('/') + name;
    return image.save(path, "PNG") ? path : QString();
}

// 兜底：某些源只给纯文本，但内容是 file:// 的 uri-list。
QString firstLocalFileInText(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
            const QString path = QUrl(line).toLocalFile();
            if (!path.isEmpty() && QFileInfo::exists(path)) {
                return path;
            }
        }
    }
    return {};
}

} // namespace

// 画布是 QGraphicsView，落在光标下的是它的 viewport（一个普通 QWidget，
// 且 QGraphicsView 给 viewport 开了 acceptDrops）——Qt 把拖放事件交给
// viewport 后就不再往 MainWindow 传，主窗口的 dragEnterEvent / dropEvent
// 根本收不到。这就是「有时拿不到」的另一半原因：落点在画布上必丢。
// 这里把发给子控件的拖放事件统一转给主窗口处理，保证整窗都能落图。
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    // 「不显示鼠标悬浮工具提示」：原生 QToolTip（setToolTip 的控件）由这里统一拦。
    // 铬件自绘的 400ms 悬停提示在 TooltipHint::hover 里读同一个键，两处一个口径。
    if (type == QEvent::ToolTip && !Config::instance().showTooltip()) {
        QToolTip::hideText();
        return true;
    }
    if (type == QEvent::DragEnter || type == QEvent::DragMove || type == QEvent::Drop) {
        QWidget *child = qobject_cast<QWidget *>(watched);
        if (child && child != this && isAncestorOf(child)) {
            switch (type) {
            case QEvent::DragEnter:
                dragEnterEvent(static_cast<QDragEnterEvent *>(event));
                break;
            case QEvent::DragMove:
                dragMoveEvent(static_cast<QDragMoveEvent *>(event));
                break;
            default:
                dropEvent(static_cast<QDropEvent *>(event));
                break;
            }
            // 已经按主窗口口径处理过，别再让 viewport 自己吞一遍。
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (dropMimeLooksUsable(event->mimeData())) {
        acceptDropAsCopy(event);
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    // 鼠标在窗口内移动时持续确认可放置，否则部分源会在中途把光标改成禁止。
    if (dropMimeLooksUsable(event->mimeData())) {
        acceptDropAsCopy(event);
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mime = event->mimeData();
    if (!mime) {
        return;
    }

    // 1) 真实文件（含多选）：取第一个存在的本地文件
    QString path = firstExistingLocalFile(mime);
    if (path.isEmpty() && mime->hasImage()) {
        // 2) 纯位图（截图工具 / 网页 / 部分聊天软件）：没有 URL，只有 CF_DIB
        const QVariant data = mime->imageData();
        QImage image = data.value<QImage>();
        if (image.isNull()) {
            image = data.value<QPixmap>().toImage();
        }
        path = saveDroppedImage(image);
    }
    if (path.isEmpty()) {
        // 3) 兜底：纯文本里带 file:// 路径
        path = firstLocalFileInText(mime->text());
    }
    if (path.isEmpty()) {
        // 4) 认不出来就静默忽略：不弹错、不崩溃，也别把无效内容顶上去
        return;
    }

    acceptDropAsCopy(event);
    openFile(path);
}
