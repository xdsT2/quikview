#pragma once

#include <QHash>
#include <QImage>
#include <QKeySequence>
#include <QList>
#include <QMainWindow>
#include <QMargins>
#include <QElapsedTimer>
#include <QPair>
#include <QPointer>
#include <QVariant>

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
    // 打开文件并按图片尺寸调窗（仅「启动带文件 / 文件→打开」两条路径用；
    // 见 fitWindowToImage()，受设置「根据图片大小调节窗口大小」控制）
    void openFileFitting(const QString &path);
    void openSettings() { onSettings(); }   // 调试/自动化：直接打开设置对话框
    void setZoomMode(int mode);             // 调试/自动化 + 菜单共用：0原始/1适合窗口/2适合宽度
    void runSmokeCommand(const QString &cmd); // 调试/自动化：--smoke 单命令驱动（P0 冒烟）

    // —— SET-02/03 设置页即时预览 ——
    // 设置页在会话中直接下发运行态（不等「保存设置」）。已接：
    // view/exif_small_font（SET-02）；always_on_top / view/show_side_arrow /
    // view/show_zoom_control（SET-03 小批 A）；view/show_anim_control（SET-04
    // 浏览页批次）；theme（外观页跟进单）。
    // 应用失败返回 false 并写入 error。
    bool applyLiveSetting(const QString &key, const QVariant &value, QString *error);
    // 主题切换后刷新「局部 QSS / 位置相关」的表面：画布底色 + 信息浮层 QSS。
    // appQss 由 Theme::applySetting 覆盖全局；这两处各自 setStyleSheet 过，
    // 不刷新会停在旧主题（SET-02 交接里 3.1.1 的「重复切换残留」即指此）。
    void refreshThemeSurfaces();
    // 打开设置时的运行态快照来源（与配置值独立，供取消回滚）。
    QVariant liveSettingValue(const QString &key) const;

    // 键盘页冲突检测数据源：14 个可配置动作之外、持有键位的动作清单
    // （每个快捷键一条，含副键；显示名已剥 & 助记符与 '\t' 后缀）。
    QList<QPair<QString, QKeySequence>> fixedShortcutActions() const;
    // 14 个可配置动作的出厂键位全表（id → 键位列表，含副键）。设置页冲突检测用：
    // 自定义键位不得撞上「仍处于出厂态」的其他动作的任何键。
    QHash<QString, QList<QKeySequence>> factoryShortcutLists() const;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
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
    // 「深色模式」勾选态跟随实际生效的主题（跟随系统时显示系统解析出的实际模式）。
    void syncThemeActions();
    void onToggleFavorite();
    // 鼠标按键映射（C9~C12）：把配置里的动作名派发到已有槽
    void onMouseAction(const QString &action);
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
    void onJumpRandom(bool forward);   // 随机跳转（Ctrl+Alt+PageUp/Down，原版语义）
    void onSlideshowStop();            // Ctrl+0：停止放映
    void onSlideshowInterval(int sec); // Ctrl+1..9 = 以 N 秒间隔开始放映
    void onToggleStretchSmall();       // 拉伸小图（Z）开关
    void onActivateImage(int index);
    // 帧
    void onNextFrame();
    void onPrevFrame();
    // 动图自动播放（P0：GIF）
    void startAnimation();
    void stopAnimation();
    void animationTick();
    void showAnimFrame(const QImage &img);
    // GIF 透明区域底色：0 不铺底 / 1 白 / 2 黑 / 3 固定自定义色（#808080）
    QImage applyGifBackdrop(const QImage &img) const;
    // 32 位图（带 alpha，非 GIF）底色：0 网格 / 1 白 / 2 黑 / 3 深色网格
    QImage applyImage32Backdrop(const QImage &img) const;
    // 异步加载
    void onFullReady(const QString &path, const QImage &image, bool preview);
    // 拖动滑杆跟手预览（只换画布与标题，索引/元数据等松手后由 jumpTo 走全图）
    void onDragPreviewReady(const QString &path, const QImage &image);
    void onLoadFailed(const QString &path, const QString &error);
    // 幻灯片
    void onSlideshowTick();

private:
    // 动作系统：所有 QAction 只创建一次，右键菜单 / 工具栏下拉菜单共用
    void applyNativeFrameStyles();   // Win11：补回 DWM 帧（描边/阴影/圆角）
    void toggleFullscreen();         // 全屏/还原唯一入口：标题栏 · F11 · 中键 · smoke
    void createActions();
    QMenu *buildContextMenu();
    QMenu *buildBrowseMenu();
    QMenu *buildSlideshowMenu();
    QMenu *buildBookmarkMenu();
    QMenu *buildEditMenu();
    QMenu *buildCopytoMenu();
    QMenu *buildSystemMenu();

    void applyRuntimeSettings();
    // 键盘快捷键自定义（批次 D1）：按 config/keyboard/* 给 14 个可配置动作
    // 重装键位。启动（createActions 末尾）与设置页保存返回后各走一次。
    void applyShortcutConfig();
    // 冒烟取证：14 个可配置动作的当前键位（PortableText，无键记 '-'）。
    QString shortcutsReport() const;
    void loadAsync(const QString &path);
    // 空闲预读：翻页停手 300ms 且解码队列空了，向当前页前后各预读 1 张
    void preloadNeighbors();
    // 显式跳转：先清空解码队列再加载（区别于顺序翻页的逐张播放）
    void jumpTo(const QString &path);
    void showImage(const QImage &image, const QString &path, bool preview = false);
    // 「顶端菜单隐藏时显示文件名 (Shift+TAB)」：画布顶部居中浮层，3s 后淡出。
    void showNameOverlay();
    void placeNameOverlay();
    // 按当前图片像素尺寸 + 铬件高度 resize 窗口，clamp 到可用屏幕 80%。
    // 只对 m_fitWindowPending 标记的「启动打开 / 文件→打开」生效一次，
    // 翻页/拖放/换目录都不触发（避免每翻一页窗口跟着跳）。
    void fitWindowToImage();
    void setSlideshow(bool on);
    void applyCrop();
    void refreshLibraryMenus();
    void applyEffect();
    void resetEffect();
    void updateStatus(const QString &path);
    void toggleAlwaysOnTop();
    void refreshZoomBadge(qreal z);           // 缩放浮标文案：当前缩放方式/百分比
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
    bool isSpreadMode() const;               // 智能双页阅览（m_spreadMode 3/4 且非 0）
    void setSpreadMode(int mode);            // 0 = 关，3 = 左→右，4 = 左←右
    void applyCurrentZoom();                 // 只用 m_zoomMode 适配画布（单图/拼图共用）
    void syncViewModeActions();              // 勾选态与内部状态对齐（硬规则：三者同步）
    void updateShowModeIcon();               // 工具栏图标：双页开取 spread，关取 zoom
    QString spreadSecondPath() const;        // 跨页的右/左手那张
    QImage readImageSync(const QString &path) const;   // 同步解码（双页拼图用）
    void refreshSpread();                    // 按当前索引重新拼接跨页
    void updateCanvasLogo();
    void updateInfoOverlay();
    void setTopChromeVisible(bool visible, bool animate = true);
    void setDockVisible(bool visible, bool animate = true);
    void toggleTopLock();
    void toggleDockLock();
    void updateChromeProximity();
    void updateTopProximity();
    void updateDockProximity();
    QPoint probeLocalPoint() const;
    QStringList listSiblingFolders();   // 同级含图文件夹（名称序），文件夹跳转用
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
    QAction *actNegative = nullptr;    // 反色（Ctrl+I）
    QAction *actGamma = nullptr;       // Gamma 补偿（Ctrl+G）
    QAction *actZoomLock = nullptr;    // 缩放锁定（Ctrl+K）
    QAction *actCrop = nullptr;
    QAction *actResize = nullptr;
    QAction *actNextFrame = nullptr;
    QAction *actPrevFrame = nullptr;
    QAction *actDark = nullptr;
    QAction *actToggleMeta = nullptr;
    QAction *actEffectNone = nullptr;
    QAction *actEffectSoften = nullptr;
    QAction *actEffectSharpen = nullptr;
    // 键盘页可配置动作注册表（动作 id → QAction）。14 个 id 见
    // applyShortcutConfig 的槽位表；键位下发与冲突检测都按 id 走这里。
    QHash<QString, QAction *> m_actions;
    QAction *actInterpFast = nullptr;
    QAction *actInterpSmooth = nullptr;
    QAction *actInterpLanczos = nullptr;
    QAction *actSortName = nullptr;
    QAction *actSortTime = nullptr;
    QAction *actSortSize = nullptr;
    QAction *actSortType = nullptr;
    QAction *actSortDesc = nullptr;
    QAction *actStretchSmall = nullptr;   // 拉伸小图（Z，原版 MENUSTR_FILTER_STRETCHWHENSMALL）

    // —— 引擎成员 ——
    ImageCanvas *m_canvas = nullptr;
    ImageCache *m_cache = nullptr;
    DirectoryLoader *m_dir = nullptr;
    MetadataPanel *m_metadata = nullptr;
    ThumbnailBrowser *m_browser = nullptr;
    FrameNavigator *m_frames = nullptr;
    QTimer *m_slideshowTimer = nullptr;
    // 翻页停手 300ms 后触发空闲预读（前后各一张，只进缓存不上屏）
    QTimer *m_preloadTimer = nullptr;
    // —— 动图自动播放（P0：GIF）——
    // 绝对时间点校正：nextDeadline 是“下一帧应出现的绝对时刻”（m_animClock 起点），
    // 每帧 += 当前帧延迟；解码慢导致的落后不再累加，防止长动图越播越慢。
    QTimer *m_animTimer = nullptr;
    QElapsedTimer m_animClock;
    qint64 m_animNextDeadline = 0;
    bool m_animPlaying = false;
    QMenu *m_favMenu = nullptr;
    QMenu *m_recentMenu = nullptr;

    // —— 铬件（Honeyview 贴图复刻）——
    // 三个常驻栏用 QPointer 持有（2026-10-25 悬空指针防御审计）：
    // 若未来任何路径提前销毁 widget，指针自动置空而不是留悬空值，
    // 现有的 `if (m_caption)` 判空检查直接生效。
    QPointer<Chrome::CaptionBar> m_caption;
    QPointer<Chrome::ToolBar> m_toolbar;
    QPointer<Chrome::ControlBar> m_control;
    Chrome::Toast *m_toast = nullptr;
    // Shift+TAB 文件名浮层（惰性创建，挂在画布 viewport 顶部居中）
    QLabel *m_nameOverlay = nullptr;
    QGraphicsOpacityEffect *m_nameOverlayFx = nullptr;
    QPropertyAnimation *m_nameOverlayFade = nullptr;
    QTimer *m_nameOverlayTimer = nullptr;
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
    // 浏览方式 = 两个正交维度：「缩放」与「双页」。别再加第三个复合枚举。
    int m_zoomMode = 1;      // 0=原始 1=适合窗口 2=适合宽度
    int m_spreadMode = 0;    // 0=关 3=双页左→右 4=双页左←右
    bool m_firstImage = true;

    QString m_currentPath;
    QString m_lastShownPath;   // 上一次完整上屏的路径，用于判断是否触发翻页过渡
    QString m_dragPreviewPath; // 最近一次滑块跟手预览的路径（它的全图落地不算翻页）
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
    // SET-03 即时预览：「不显示缩放信息控制」的运行态覆盖值。设置页会话内
    // 不写配置，zoomChanged 先看这里；关闭设置页后清空标志，回到配置值。
    bool m_liveZoomControlOverride = false;
    bool m_liveZoomControlOverrideSet = false;
    // SET-04 浏览页批次：「不显示动画信息控制」的运行态覆盖值。设置页会话内
    // 不写配置，updateInfoOverlay 先看这里；关闭设置页后清空标志，回到配置值。
    bool m_liveAnimControlOverride = false;
    bool m_liveAnimControlOverrideSet = false;
    // 装图/适配期间屏蔽「保存缩放状态」写盘（见 zoomChanged 连接处）
    bool m_zoomSaveGuard = false;
    // 开锁前的缩放方式（0=原始 1=适合窗口 2=适合宽度），-1 表示未记录。
    // 锁定只影响缩放这一维，双页模式不参与记忆。
    int m_zoomModeBeforeLock = -1;
    bool m_hdrTonemap = false; // HDR 色调映射（命令栏 HDR 钮 / config/hdr_tonemap，默认 false）
    int m_interpMode = 1;      // 0 bilinear / 1 bicubic / 2 lanczos
    bool m_slideshow = false;
    bool m_slideshowRandom = false;
    bool m_cropMode = false;
    // 下一次全图就绪后做一次「按图调窗」（openFileFitting / 文件→打开 置位，
    // 见 fitWindowToImage()；翻页等其它打开路径不动它）
    bool m_fitWindowPending = false;
};
