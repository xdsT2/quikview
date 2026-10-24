#pragma once

#include <QImage>
#include <QMainWindow>
#include <QMargins>

class ImageCanvas;
class ImageCache;
class DirectoryLoader;
class MetadataPanel;
class ThumbnailBrowser;
class FrameNavigator;
class QAction;
class QLabel;
class QTimer;
class QMenu;
class QMouseEvent;
class QResizeEvent;
class QKeyEvent;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

namespace Chrome {
class CaptionBar;
class ToolBar;
class ControlBar;
class Toast;
class ZoomBadge;
class StatusOverlay;
class EmptyState;
}

// 主窗口：对照 Honeyview 5.53 的可观察布局（尺寸/间距参数）。
// 无边框窗口：6px 外框 + 25px 标题栏 + 31px 工具栏 + 画布 + 底部胶囊控制栏。
// 功能动作（createActions）为唯一真源，右键菜单 / 工具栏下拉菜单共用。
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void openFile(const QString &path);
    void openSettings() { onSettings(); }   // 调试/自动化：直接打开设置对话框
    void setViewMode(int mode);             // 调试/自动化 + 菜单共用：0原始/1适合窗口/2适合宽度
    void runSmokeCommand(const QString &cmd); // 调试/自动化：--smoke 单命令驱动（P0 冒烟）

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message,
                     qint64 *result) override;
    void changeEvent(QEvent *event) override;

private slots:
    // 文件
    void onOpen();
    void onOpenFolder();
    void onSaveAs();
    void onPrint();
    void onBatchConvert();
    void onSettings();
    void onToggleFavorite();
    void onDeleteFile();
    void onCopyToFolder();
    void onOpenWithEditor();
    void onCopyToClipboard();
    void onSetWallpaper();
    void onAbout();
    // 视图
    void onFit();
    void onFitWidth();
    void onZoomIn();
    void onZoomOut();
    void onActualSize();
    void onSlideshow();
    void onPrev();
    void onNext();
    void onPrev10();
    void onNext10();
    void onFirst();
    void onLast();
    void onPrevFolder();
    void onNextFolder();
    void onSort(int mode);
    void onSortDescending(bool on);
    // 编辑
    void onRotateLeft();
    void onRotateRight();
    void onFlipH();
    void onFlipV();
    void onCrop();
    void onResize();
    void onEffect(int mode);
    void onActivateImage(int index);
    // 帧
    void onNextFrame();
    void onPrevFrame();
    // 异步加载
    void onFullReady(const QString &path, const QImage &image);
    void onLoadFailed(const QString &path, const QString &error);
    // 幻灯片
    void onSlideshowTick();

private:
    // 动作系统：所有 QAction 只创建一次，右键菜单 / 工具栏下拉菜单共用
    void applyNativeFrameStyles();   // Win11：补回 DWM 帧（描边/阴影/圆角）
    void createActions();
    QMenu *buildContextMenu();
    QMenu *buildBrowseMenu();
    QMenu *buildSlideshowMenu();
    QMenu *buildBookmarkMenu();
    QMenu *buildEditMenu();
    QMenu *buildCopytoMenu();
    QMenu *buildSystemMenu();

    void applyRuntimeSettings();
    void loadAsync(const QString &path);
    void showImage(const QImage &image, const QString &path);
    void setSlideshow(bool on);
    void applyCrop();
    void refreshLibraryMenus();
    void applyEffect();
    void resetEffect();
    void updateStatus(const QString &path);
    void toggleAlwaysOnTop();
    void applyChromeLayout();
    void updateCanvasInsets();               // 缩略图栏展开时让出左侧可视区
    void toggleBrowserPanel();
    void rotateImage(int degrees);           // 原地旋转（保持缩放与视图中心）
    void resetGeometry();                    // 不旋转：清空旋转/翻转，回到源图
    void rebuildFromSource(const QPointF &anchor);   // 从源图重算净几何变换
    void applyAdjustments(const QPointF &anchor);    // 反相 + Gamma + 效果，保视图刷新
    void onNegative();
    void onGamma();
    void onStretchSmall(bool on);
    void setInterpolationMode(const QString &mode);
    void toggleZoomLock();                   // 锁定缩放（Ctrl+K）：翻页沿用同一比例
    void onPageSelect();                     // 选择图片（Enter）：按序号跳转
    void onEditBookmarks();                  // 编辑书签（F3）：书签管理对话框
    bool isSpreadMode() const;               // 智能双页阅览（viewMode 3/4）
    void setSpreadMode(int mode);            // 3 = 左→右，4 = 左←右
    QString spreadSecondPath() const;        // 跨页的右/左手那张
    QImage readImageSync(const QString &path) const;   // 同步解码（双页拼图用）
    void refreshSpread();                    // 按当前索引重新拼接跨页
    void updateCanvasLogo();
    void updateInfoOverlay();
    void syncZoom();
    void setTopChromeVisible(bool visible, bool animate = true);
    void setDockVisible(bool visible, bool animate = true);
    void toggleTopLock();
    void toggleDockLock();
    void updateChromeProximity();
    void updateTopProximity();
    void updateDockProximity();
    QPoint probeLocalPoint() const;
    QStringList listSiblingFolders();   // 同级含图文件夹（名称序），文件夹跳转用
    static QStringList imageExtensions();
    static QStringList listImagesInDir(const QString &dirPath);

    // —— 共享动作（唯一真源）——
    QAction *actOpen = nullptr;
    QAction *actOpenFolder = nullptr;
    QAction *actSaveAs = nullptr;
    QAction *actPrint = nullptr;
    QAction *actBatchConvert = nullptr;
    QAction *actSettings = nullptr;
    QAction *actFavorite = nullptr;
    QAction *actDelete = nullptr;
    QAction *actCopyToFolder = nullptr;
    QAction *actOpenWithEditor = nullptr;
    QAction *actCopyToClipboard = nullptr;
    QAction *actWallpaper = nullptr;
    QAction *actAbout = nullptr;
    QAction *actQuit = nullptr;
    QAction *actFit = nullptr;
    QAction *actFitWidth = nullptr;
    QAction *actTwoL2R = nullptr;    // 智能双页阅览（左→右）7
    QAction *actTwoR2L = nullptr;    // 智能双页阅览（左←右）6
    QAction *actZoomIn = nullptr;
    QAction *actZoomOut = nullptr;
    QAction *actActualSize = nullptr;
    QAction *actSlideshow = nullptr;
    QAction *actPrev = nullptr;
    QAction *actNext = nullptr;
    QAction *actPrev10 = nullptr;
    QAction *actNext10 = nullptr;
    QAction *actFirst = nullptr;
    QAction *actLast = nullptr;
    QAction *actPrevFolder = nullptr;
    QAction *actNextFolder = nullptr;
    QAction *actRotateLeft = nullptr;
    QAction *actRotateRight = nullptr;
    QAction *actFlipH = nullptr;
    QAction *actFlipV = nullptr;
    QAction *actCrop = nullptr;
    QAction *actResize = nullptr;
    QAction *actNextFrame = nullptr;
    QAction *actPrevFrame = nullptr;
    QAction *actDark = nullptr;
    QAction *actToggleMeta = nullptr;
    QAction *actEffectNone = nullptr;
    QAction *actEffectSoften = nullptr;
    QAction *actEffectSharpen = nullptr;
    QAction *actInterpFast = nullptr;
    QAction *actInterpSmooth = nullptr;
    QAction *actInterpLanczos = nullptr;
    QAction *actSortName = nullptr;
    QAction *actSortTime = nullptr;
    QAction *actSortSize = nullptr;
    QAction *actSortType = nullptr;
    QAction *actSortDesc = nullptr;

    // —— 引擎成员 ——
    ImageCanvas *m_canvas = nullptr;
    ImageCache *m_cache = nullptr;
    DirectoryLoader *m_dir = nullptr;
    MetadataPanel *m_metadata = nullptr;
    ThumbnailBrowser *m_browser = nullptr;
    FrameNavigator *m_frames = nullptr;
    QTimer *m_slideshowTimer = nullptr;
    QMenu *m_favMenu = nullptr;
    QMenu *m_recentMenu = nullptr;

    // —— 铬件（Honeyview 贴图复刻）——
    Chrome::CaptionBar *m_caption = nullptr;
    Chrome::ToolBar *m_toolbar = nullptr;
    Chrome::ControlBar *m_control = nullptr;
    Chrome::Toast *m_toast = nullptr;
    Chrome::ZoomBadge *m_zoomBadge = nullptr;
    Chrome::StatusOverlay *m_statusOverlay = nullptr;
    Chrome::EmptyState *m_emptyState = nullptr;
    QWidget *m_sidePrev = nullptr;
    QWidget *m_sideNext = nullptr;

    // 悬浮铬件显隐：上下两套状态完全独立，各自可锁定常显。
    // 21:15：显隐是独立视觉状态机（Hidden → Showing → Visible → Hiding 单一状态源），
    // 只改 opacity / 鼠标命中，绝不动画布几何；热区带滞回，避免边缘来回抖动导致闪烁。
    enum class ChromePhase { Hidden, Showing, Visible, Hiding };
    QTimer *m_chromeTimer = nullptr;
    QTimer *m_topHideTimer = nullptr;
    QTimer *m_dockHideTimer = nullptr;
    ChromePhase m_topPhase = ChromePhase::Visible;
    bool m_probeCursorActive = false;   // --smoke hover:x:y 时用合成坐标代替真实光标
    QPoint m_probeCursor;
    ChromePhase m_dockPhase = ChromePhase::Visible;
    QGraphicsOpacityEffect *m_captionOpacity = nullptr;
    QGraphicsOpacityEffect *m_toolbarOpacity = nullptr;
    QGraphicsOpacityEffect *m_dockOpacity = nullptr;
    QPropertyAnimation *m_captionFade = nullptr;
    QPropertyAnimation *m_toolbarFade = nullptr;
    QPropertyAnimation *m_dockFade = nullptr;
    bool m_topLocked = false;
    bool m_dockLocked = false;
    bool m_topVisible = true;
    bool m_dockVisible = true;
    QMargins m_canvasInsets{-1, -1, -1, -1};   // 当前画布可视区边距（-1 = 未应用）

    bool m_fullscreen = false;
    int m_viewMode = 1;      // 0=原始 1=适合窗口 2=适合宽度
    bool m_firstImage = true;

    QString m_currentPath;
    QImage m_sourceImage;      // 文件原始像素，不做任何旋转/翻转/效果（「不旋转」的基准）
    QImage m_currentImage;
    QImage m_originalImage;
    int m_effect = 0;
    int m_netRotation = 0;     // 净旋转角：0 / 90 / 180 / 270
    bool m_flipH = false;
    bool m_flipV = false;
    bool m_negative = false;   // 反相
    qreal m_gamma = 1.0;       // Gamma 补偿（1.0 = 不变）
    bool m_zoomLocked = false; // 锁定缩放：翻页后不做适配，沿用同一比例
    int m_interpMode = 1;      // 0 bilinear / 1 bicubic / 2 lanczos
    bool m_slideshow = false;
    bool m_slideshowRandom = false;
    bool m_cropMode = false;
};
