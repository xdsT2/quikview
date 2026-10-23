#pragma once

#include <QImage>
#include <QMainWindow>

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

namespace Chrome {
class CaptionBar;
class ToolBar;
class ControlBar;
class Toast;
class ZoomBadge;
}

// 主窗口：Honeyview 5.53 像素级复刻（skin_utf8.xml 布局参数）。
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
    void updateCanvasLogo();
    void syncZoom();
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
    QWidget *m_sidePrev = nullptr;
    QWidget *m_sideNext = nullptr;
    bool m_fullscreen = false;
    int m_viewMode = 1;      // 0=原始 1=适合窗口 2=适合宽度
    bool m_firstImage = true;

    QString m_currentPath;
    QImage m_currentImage;
    QImage m_originalImage;
    int m_effect = 0;
    bool m_smoothInterpolation = true;
    bool m_slideshow = false;
    bool m_slideshowRandom = false;
    bool m_cropMode = false;
};
