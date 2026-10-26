#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColor>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDesktopServices>
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
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QTextStream>
#include <QTimer>
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

    // 画布缩放联动：左下角浮标 + 浏览方式图标降级（胶囊滑杆已是翻页进度，不再跟随缩放）
    connect(m_canvas, &ImageCanvas::zoomChanged, this, [this](qreal z) {
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
        // 这里绝不能把「非 fit」推断成 m_viewMode=0：滚轮手动放大同样走
        // zoomChanged，误改后翻页 setViewMode(0) 就变成 100% 显示。
        // 浏览方式只在显式 setViewMode()（菜单/快捷键/冒烟命令）里变，滚轮只是
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
        // 高度随卡片数量，最多占到窗口底部（面板不再强制满高，也不再为胶囊让位）
        const int avail = h - 76 - kBrowserGap;
        m_browser->setGeometry(x + kBrowserGap, y + 76, kBrowserWidth,
                               qMin(m_browser->preferredHeight(), avail));
        m_browser->raise();
    }
    // 图片信息：左上覆盖层（原型 .info-overlay）。缩略图面板开合跟着横移，
    // 不遮挡面板（面板右缘 12+160=172，浮层 196 起）；全屏时贴顶 10px。
    // 宽度是浮层自己算好的自然宽（钳在 280..440），高度钳到可用高度，
    // 超出部分由浮层内部滚动。覆盖层不参与 updateCanvasInsets，不挤压画布。
    if (m_metadata && m_metadata->isVisible()) {
        const bool browserOpen = m_browser && m_browser->isVisible();
        const int left = browserOpen ? 196 : 14;
        const int top = full ? 10 : 70;
        const int availH = h - top - 24;
        const QSize hint = m_metadata->sizeHint();
        // 临时验收探针：GUI Tab 流程下面板几何实参（定位高度被裁）
        {
            QFile f(QStringLiteral("Temp/meta-dump.txt"));
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QTextStream ts(&f);
                ts << "GEOM win=" << width() << 'x' << height() << " h=" << h
                   << " availH=" << availH << " hint=" << hint.height()
                   << " final=" << qMin(hint.height(), qMax(120, availH)) << "\n";
            }
        }
        m_metadata->setGeometry(x + left, y + top,
                                hint.width(), qMin(hint.height(), qMax(120, availH)));
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
    auto mk = [this](const QString &text, const QKeySequence &seq,
                     void (MainWindow::*slot)()) {
        auto *a = new QAction(text, this);
        if (!seq.isEmpty()) {
            a->setShortcut(seq);
        }
        connect(a, &QAction::triggered, this, slot);
        // QAction 的快捷键只有在「挂到窗口」后才参与 QShortcutMap 分发。
        // 此前所有动作都只 new 没 addAction，Ctrl+O 之类一直靠菜单/冒烟直调才生效。
        addAction(a);
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
    actOpen = mk(tr("打开(&O)..."), QKeySequence::Open, &MainWindow::onOpen);
    actOpen->setShortcuts({ QKeySequence::Open, QKeySequence(QStringLiteral("F2")) });
    actOpenFolder = mk(tr("打开文件夹..."), QKeySequence(QStringLiteral("F")), &MainWindow::onOpenFolder);
    actSaveAs = mk(tr("另存为…(&S)"), QKeySequence::SaveAs, &MainWindow::onSaveAs);
    actPrint = mk(tr("打印"), QKeySequence::Print, &MainWindow::onPrint);          // Ctrl+P
    actBatchConvert = mk(tr("批量转换(&B)"), QKeySequence(QStringLiteral("Ctrl+T")), &MainWindow::onBatchConvert);
    // Honeyview：F5 = 设置。F9 保留为副键（旧标签与文档一直写 F9）
    actSettings = mk(tr("设置(&C)"), QKeySequence(QStringLiteral("F5")), &MainWindow::onSettings);
    actSettings->setShortcuts({ QKeySequence(QStringLiteral("F5")), QKeySequence(QStringLiteral("F9")) });
    // 书签菜单：添加书签 B（原型），勾选/取消勾选由 onToggleFavorite 承担
    actFavorite = mk(tr("添加书签"), QKeySequence(QStringLiteral("B")), &MainWindow::onToggleFavorite);
    actDelete = mk(tr("删除"), QKeySequence::Delete, &MainWindow::onDeleteFile);   // Del
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

    // 视图（浏览方式单选：原始 0 / 适合窗口 9 / 适合宽度 8，对照原版快捷键）
    auto *viewModeGroup = new QActionGroup(this);
    viewModeGroup->setExclusive(true);
    actActualSize = mk(tr("原始大小 (100%)"), QKeySequence(QStringLiteral("0")), &MainWindow::onActualSize);
    actFit = mk(tr("适合窗口"), QKeySequence(QStringLiteral("9")), &MainWindow::onFit);
    actFitWidth = mk(tr("适合宽度"), QKeySequence(QStringLiteral("8")), &MainWindow::onFitWidth);
    actTwoL2R = new QAction(tr("智能双页阅览 (左→右)"), this);
    actTwoL2R->setShortcut(QKeySequence(QStringLiteral("7")));
    connect(actTwoL2R, &QAction::triggered, this, [this]() { setViewMode(3); });
    actTwoR2L = new QAction(tr("智能双页阅览 (左←右)"), this);
    actTwoR2L->setShortcut(QKeySequence(QStringLiteral("6")));
    connect(actTwoR2L, &QAction::triggered, this, [this]() { setViewMode(4); });
    for (auto *a : { actActualSize, actFit, actFitWidth, actTwoL2R, actTwoR2L }) {
        a->setCheckable(true);
        viewModeGroup->addAction(a);
        addAction(a);   // 不挂窗口快捷键不参与分发（此前 7/6 是死的）
    }
    actFit->setChecked(true);
    actZoomIn = mk(tr("放大(&+)"), QKeySequence::ZoomIn, &MainWindow::onZoomIn);
    actZoomOut = mk(tr("缩小(&-)"), QKeySequence::ZoomOut, &MainWindow::onZoomOut);
    // 无修饰的 + / - 作为第二快捷键（对照原版 MENUSTR_ZOOM_IN/OUT），菜单显示仍取第一个
    actZoomIn->setShortcuts({ QKeySequence::ZoomIn, QKeySequence(QStringLiteral("+")) });
    actZoomOut->setShortcuts({ QKeySequence::ZoomOut, QKeySequence(QStringLiteral("-")) });
    // ←/→ 由 QAction 承担：主菜单右侧会自动显示箭头，避免和 keyPressEvent 两处处理。
    // PageUp/PageDown 作为副快捷键保留（原先就是翻页），显示仍取第一个 ←/→。
    // Backspace = 上一张（原版空格/退格翻页语义，空格在 keyPressEvent 已有）。
    actPrev = mk(tr("上一张图片"), QKeySequence(Qt::Key_Left), &MainWindow::onPrev);
    actNext = mk(tr("下一张图片"), QKeySequence(Qt::Key_Right), &MainWindow::onNext);
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
    actPrevFolder = mk(tr("上一个 文件夹/压缩包"), QKeySequence(QStringLiteral("[")), &MainWindow::onPrevFolder);
    actNextFolder = mk(tr("下一个 文件夹/压缩包"), QKeySequence(QStringLiteral("]")), &MainWindow::onNextFolder);
    actNextFrame = mk(tr("下一帧"), QKeySequence(QStringLiteral("Ctrl+Right")), &MainWindow::onNextFrame);
    actPrevFrame = mk(tr("上一帧"), QKeySequence(QStringLiteral("Ctrl+Left")), &MainWindow::onPrevFrame);

    actSlideshow = new QAction(tr("幻灯片放映(&S)"), this);
    actSlideshow->setCheckable(true);
    // 放映本体无直接键（原版同）：Ctrl+0 停止 / Ctrl+1..9 以 N 秒开始放映，
    // 见下面的 slideshow interval 动作组。
    addAction(actSlideshow);
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
        if (on) {
            m_metadata->dumpHeights(QStringLiteral("Temp/meta-dump.txt"), QStringLiteral("TABPRE"));
        }
        m_metadata->setVisible(on);
        if (on) {
            applyChromeLayout();
            m_metadata->dumpHeights(QStringLiteral("Temp/meta-dump.txt"), QStringLiteral("TABPOST"));
        }
    });
    // 面板自带 × 关掉时，把菜单勾选态同步回未勾选，避免「关了还打勾」
    connect(m_metadata, &MetadataPanel::closed, this, [this]() {
        actToggleMeta->setChecked(false);
    });
    connect(m_metadata, &MetadataPanel::copied, this, [this]() {
        m_toast->showMessage(tr("已复制图片信息参数"));
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
    // 不旋转 Alt+Up / 旋转 180° Alt+Down（MENUSTR_ROTATION_NONE/180）
    mk(tr("不旋转"), QKeySequence(QStringLiteral("Alt+Up")), &MainWindow::resetGeometry);
    mkFn(tr("旋转 180°"), QKeySequence(QStringLiteral("Alt+Down")), [this]() { rotateImage(180); });
    // 翻转：Ctrl+M 水平 / Ctrl+F 垂直（MENUSTR_IMAGE_PRICESSING_MIRROR/FLIP）
    actFlipH = mk(tr("水平翻转"), QKeySequence(QStringLiteral("Ctrl+M")), &MainWindow::onFlipH);
    actFlipV = mk(tr("垂直翻转"), QKeySequence(QStringLiteral("Ctrl+F")), &MainWindow::onFlipV);
    // 原版 X = 退出程序（已绑 actQuit）；裁剪走菜单/转换对话框，不占单键
    actCrop = mk(tr("裁剪(&C)"), {}, &MainWindow::onCrop);
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
    addMenuItem(nav, tr("下一个 文件夹/压缩包"), QStringLiteral("]"), [this]() { onNextFolder(); });
    addMenuItem(nav, tr("上一个 文件夹/压缩包"), QStringLiteral("["), [this]() { onPrevFolder(); });
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

    // —— 浏览方式（原型取 showmode 前 5 项）——
    QMenu *show = menu->addMenu(tr("浏览方式"));
    actActualSize->setChecked(m_viewMode == 0);
    actFit->setChecked(m_viewMode == 1);
    actFitWidth->setChecked(m_viewMode == 2);
    show->addAction(actActualSize);
    show->addAction(actFit);
    show->addAction(actFitWidth);
    actTwoL2R->setChecked(m_viewMode == 3);
    actTwoR2L->setChecked(m_viewMode == 4);
    show->addAction(actTwoL2R);
    show->addAction(actTwoR2L);
    UiStyle::polishMenu(show);

    // —— 缩放工具（6 项）——
    QMenu *zoom = menu->addMenu(tr("缩放工具"));
    addMenuItem(zoom, tr("放大"), QStringLiteral("+"), [this]() { onZoomIn(); });
    addMenuItem(zoom, tr("缩小"), QStringLiteral("−"), [this]() { onZoomOut(); });
    actZoomLock->setChecked(m_zoomLocked);
    zoom->addAction(actZoomLock);       // Ctrl+K
    addMenuItem(zoom, tr("100%"), QString(), [this]() { setViewMode(0); });
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

    // 画布外观（背景色 / 视口边框色 / 侧边箭头）：设置页存的是 #RRGGBB 颜色名，
    // 读不到或解析失败时退回默认值——否则 QColor 无效会让画布画成黑块或干脆不描边。
    QSettings &viewQs = Config::instance().settings();
    const QColor bkgnd(viewQs.value(QStringLiteral("config/view/bkgnd_color"),
                                    QStringLiteral("#202020")).toString());
    const QColor border(viewQs.value(QStringLiteral("config/view/border_color"),
                                     QStringLiteral("#000000")).toString());
    m_canvas->setBackgroundColor(bkgnd.isValid() ? bkgnd : QColor(QStringLiteral("#202020")));
    m_canvas->setBorderColor(border.isValid() ? border : QColor(QStringLiteral("#000000")));
    // 设置页的「不显示侧边箭头」是反选（inverted），存的就是 show_side_arrow 本身：
    // true=显示（默认）。关掉只影响视觉，两侧点击仍翻页。
    m_canvas->setSideArrowsEnabled(viewQs.value(QStringLiteral("config/view/show_side_arrow"),
                                                true).toBool());

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
    jumpTo(m_currentPath);
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

void MainWindow::onLoadFailed(const QString &path, const QString &error)
{
    if (path != m_currentPath) {
        return;
    }
    QString rawErr;
    QImage img = loadRawImage(path, &rawErr, m_hdrTonemap);
    if (!img.isNull()) {
        showImage(img, path);
        return;
    }
    QString heifErr;
    img = loadHeifImage(path, &heifErr, m_hdrTonemap);
    if (!img.isNull()) {
        showImage(img, path);
        return;
    }
    QString lastErr = rawErr.isEmpty() ? heifErr : rawErr;
    m_toast->showMessage(tr("无法打开：%1").arg(lastErr.isEmpty() ? error : lastErr));
}

void MainWindow::showImage(const QImage &image, const QString &path, bool preview)
{
    // 连按中途的快帧：只换画布与标题顶住体感。源图/效果/元数据/动图等簿记
    // 等全图到了走完整流程，避免快帧（缩放解码的小图）污染旋转/Gamma 基准。
    if (preview) {
        m_canvas->setPixmap(QPixmap::fromImage(image));
        setWindowTitle(QFileInfo(path).fileName() + QStringLiteral(" - QuikView"));
        m_caption->setTitle(QFileInfo(path).fileName());
        return;
    }
    // HDR 色调映射通用口：loader 内部的高位深在解码层就映射完（到这里已是
    // 8bit，toneMapToSdr 原样放行）；QImageReader 产出的 RGBA64（16bit PNG/
    // TIFF 等）在这里映射。8bit 图硬规则：一个像素都不动。
    const QImage sdr = m_hdrTonemap ? ToneMap::toneMapToSdr(image) : image;
    // 双页模式先把相邻两页拼成一张大图，后面所有几何/颜色变换都作用在整跨页上，
    // 视图层面永远只有「一张图」，不需要两套坐标。
    QImage base = sdr;
    if (isSpreadMode()) {
        const QImage second = readImageSync(spreadSecondPath());
        base = (m_viewMode == 3) ? composeSpread(sdr, second) : composeSpread(second, sdr);
    }
    // m_sourceImage 保存「文件里的原始像素」，旋转/翻转/反相/Gamma 全都从它派生。
    // 这样「不旋转」才能真回到原样，而不是靠撤销栈一层层退。
    m_sourceImage = base;
    m_netRotation = 0;
    m_flipH = false;
    m_flipV = false;
    m_negative = false;
    m_gamma = 1.0;
    m_currentImage = base;
    m_originalImage = base;
    m_effect = 0;
    if (actEffectNone) {
        actEffectNone->setChecked(true);
    }
    const qreal prevZoom = m_canvas->zoom();
    // fitAfter=false：这里只装图，适配交给下面的 setViewMode / fitToWindow
    // 做一次——否则 setPixmap 内部先 fit 一次、setViewMode 又 fit 一次，
    // Lanczos 模式下等于每次翻页多做一次全图重采样。
    m_canvas->setPixmap(QPixmap::fromImage(base), /*fitAfter=*/false);
    // 首图按设置默认（fit/actual）；之后翻页保持用户当前浏览方式。
    // 锁定缩放时两者都不走：直接沿用上一张的比例，换图不跳比例。
    if (m_zoomLocked && !m_firstImage && prevZoom > 0.0) {
        m_canvas->setPixmapPreservingView(QPixmap::fromImage(base), QPointF(0.5, 0.5),
                                          prevZoom, false, false);
        // 这里不能写 m_viewMode：锁定只是「沿用比例」，不是把浏览方式改成原始尺寸。
        // 写了会把用户锁定前的模式永久污染成 0，解锁后翻页全部变 100%。
    } else if (m_firstImage && Config::instance().defaultZoom() == QLatin1String("actual")) {
        setViewMode(0);
    } else if (isSpreadMode()) {
        // 双页：每次换页都按新的跨页重新适配
        m_canvas->fitToWindow();
    } else {
        setViewMode(m_viewMode);
    }
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
    if (m_frames && m_frames->hasFrames()) {
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
    case 3:
    case 4:
        // 智能双页阅览：重新拼跨页 + 适配（勾选态与 toast 都在 setSpreadMode 里）
        setSpreadMode(mode);
        return;
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
    else if (c == QLatin1String("spread:l2r")) { setViewMode(3); }
    else if (c == QLatin1String("spread:r2l")) { setViewMode(4); }
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
               << " page=" << (m_control ? m_control->page() : 0)
               << " rot=" << m_netRotation
               << " vm=" << m_viewMode
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
    const qreal z = m_canvas->zoom();
    const QRectF sr = m_canvas->sceneRect();
    const QPointF center = m_canvas->mapToScene(m_canvas->viewport()->rect().center());
    QPointF anchor(0.5, 0.5);
    if (sr.width() > 1.0 && sr.height() > 1.0) {
        anchor = QPointF(center.x() / sr.width(), center.y() / sr.height());
    }
    m_currentImage = img;
    m_canvas->setPixmapPreservingView(QPixmap::fromImage(img), anchor, z, false, false);
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
        // 开锁瞬间记住当前浏览方式，锁定期间不再被改写。
        if (m_viewModeBeforeLock < 0) {
            m_viewModeBeforeLock = m_viewMode;
        }
    } else {
        // 解锁还原：没有记录（例如启动时配置就是锁定态）则回到「适合窗口」。
        m_viewMode = (m_viewModeBeforeLock >= 0) ? m_viewModeBeforeLock : 1;
        m_viewModeBeforeLock = -1;
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
    hint->setStyleSheet(QStringLiteral("color:#9aa0a6;font-size:12px;"));
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
    return m_viewMode == 3 || m_viewMode == 4;
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
    m_sourceImage = (m_viewMode == 3) ? composeSpread(first, second)
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
    m_canvas->fitToWindow();
    updateInfoOverlay();
}

void MainWindow::setSpreadMode(int mode)
{
    m_viewMode = mode;
    refreshSpread();
    if (actTwoL2R) {
        actTwoL2R->setChecked(mode == 3);
    }
    if (actTwoR2L) {
        actTwoR2L->setChecked(mode == 4);
    }
    if (actActualSize) {
        actActualSize->setChecked(false);
    }
    if (actFit) {
        actFit->setChecked(false);
    }
    if (actFitWidth) {
        actFitWidth->setChecked(false);
    }
    if (m_toast) {
        m_toast->showMessage(mode == 3 ? tr("智能双页阅览（左→右）")
                                       : tr("智能双页阅览（左←右）"));
    }
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
        openFile(path);
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
                          "<p>版本 %1</p>"
                          "<p>MIT 许可证开源，零遥测、零推广。</p>")
                           .arg(QStringLiteral(QUIKVIEW_VERSION)));
}

// ---------------------------------------------------------------------------
// 键盘 / 拖放
// ---------------------------------------------------------------------------

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
