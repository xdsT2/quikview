#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QVariantAnimation;
class QFocusEvent;
class QKeyEvent;
class QWheelEvent;

// ---------------------------------------------------------------------------
// QuikView Windows 11 铬件。
// 全部为自绘控件：标题栏 / 命令栏 / 浮动控制胶囊 / 状态浮层 / 空状态 / 消息条。
// 视觉参数来自 Temp/quikview-win11-prototype.html。
// ---------------------------------------------------------------------------
namespace Chrome {

// resources/skin 目录解析（构建树与部署树都覆盖）
QString skinDir();
QPixmap skinPixmap(const QString &name);
QPixmap skinFrame(const QString &name, int idx);

// ===========================================================================
// 按钮状态（标题栏 / 命令栏 / 底部胶囊共用的唯一一份状态描述）
// 说明：状态与插值量只参与**绘制**，绝不改动按钮 geometry、行高或胶囊尺寸。
// ===========================================================================
struct BtnState {
    bool hover = false;      // 指针悬停
    bool pressed = false;    // 按下（鼠标按下 / 键盘 Space、Enter 按住）
    bool checked = false;    // 锁定 / 选中（淡蓝底 + 蓝描边 + 蓝图标）
    bool focus = false;      // 键盘焦点（2px 淡蓝焦点环）
    bool disabled = false;   // 降低不透明度，不响应 Hover / Pressed 动画
    bool danger = false;     // 关闭按钮：红色 Hover，不套蓝色选中态
};

// 由 ButtonFeedback 推进的绘制插值量（0..1）。
struct BtnVisual {
    qreal hover = 0.0;   // 80ms 淡入
    qreal press = 0.0;   // 65ms 按下 / 100ms 松开；绘制时压暗 + 图标缩至 98%
    qreal checked = 0.0; // 120ms 选中态切换（锁定图标交叉淡化）
};

// 系统「减少动画」：QUIKVIEW_REDUCE_MOTION 环境变量优先，其次 Windows 客户端动画设置。
// 开启时关掉缩放与淡入动画，但保留背景色反馈。
bool reducedMotion();

// 一个铬件内所有按钮共用的状态动画器。
// 单一 16ms 定时器推进所有按钮的插值，中断/连点/拖出都能收尾干净。
class ButtonFeedback : public QObject
{
    Q_OBJECT
public:
    explicit ButtonFeedback(QObject *parent = nullptr);

    void setHover(int index);      // -1 = 无悬停（会淡出）
    void setPressed(int index);    // -1 = 松开（100ms 恢复，不弹跳）
    void setChecked(int index, bool on); // 选中态目标（锁定按钮交叉淡化）
    void reset();                  // 立即收回全部状态（禁用 / 失去焦点）
    BtnVisual visual(int index) const;
    int pressedIndex() const { return m_pressed; }

signals:
    void changed();                // 插值变化，宿主调用 update()

private:
    void tick();
    void start();
    void snap();

    QHash<int, BtnVisual> m_vals;
    QSet<int> m_checked;
    int m_hover = -1;
    int m_pressed = -1;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
};

// ===========================================================================
// 统一 Tooltip 提示器（标题栏 / 命令栏 / 底部胶囊共用）
// 悬停 400ms 出现；离开、按下、滚动由宿主调用 hideNow() 立即消失。
// 显示位置按文本宽度估算后夹在宿主窗口 frameGeometry 内，不越出窗口。
// ===========================================================================
class TooltipHint : public QObject
{
    Q_OBJECT
public:
    explicit TooltipHint(QWidget *host);

    // text 为空 = 取消；anchorGlobal 为指针的全局坐标。文本变化才重新计时。
    void hover(const QString &text, const QPoint &anchorGlobal);
    void hideNow();
    bool isShown() const { return m_shown; }

private:
    void reveal();

    QWidget *m_host = nullptr;
    QTimer *m_timer = nullptr;
    QString m_text;
    QPoint m_anchor;
    bool m_shown = false;
};

// ==================== 顶部单行工具栏（38px，标题与工具同一行） ====================
class TopBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopBar(QWidget *parent = nullptr);

    void setTitle(const QString &title);
    QString title() const { return m_title; }

    // 三个独立状态中的两个：顶部工具栏锁定 / 图片窗口置顶。
    // 底栏锁定由 ControlBar 自己持有，三者互不代管。
    void setTopLocked(bool locked);
    bool isTopLocked() const { return m_topLocked; }
    void setPinned(bool pinned);
    bool isPinned() const { return m_pinned; }
    void setLayerState(bool available, bool open);
    bool layersAvailable() const { return m_layersAvailable; }

    // 窄窗收起探针：0打开 1浏览 2幻灯片 3书签 4编辑 5复制到
    // 6图层 7顶栏锁 8置顶 9三横菜单
    QVector<int> hiddenActionIds() const;
    QPoint menuAnchorGlobal() const;
    QPoint closeAnchorGlobal() const;
    QPoint actionAnchorGlobal(int id) const;

    // 冒烟取证：按 id 合成 hover / pressed，截图不必依赖真实光标。
    void probeHover(int id);
    void probePress(int id);

signals:
    void openClicked();
    void browseMenuRequested(const QPoint &globalPos);
    void slideshowMenuRequested(const QPoint &globalPos);
    void bookmarkMenuRequested(const QPoint &globalPos);
    void editMenuRequested(const QPoint &globalPos);
    void copytoMenuRequested(const QPoint &globalPos);
    void layersClicked();
    void topLockToggled();
    void pinToggled();
    void menuRequested(const QPoint &globalPos);
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();
    void systemMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    // 键盘焦点在顶栏时，方向键/空格必须用于按钮导航，不能被「上一张/下一张」
    // 等窗口级快捷键抢走；ShortcutOverride 是 Qt 里唯一能在快捷键分发前拦下的钩子。
    bool event(QEvent *event) override;

private:
    enum class Action {
        None, Open, Browse, Slideshow, Bookmark, Edit, CopyTo,
        Layers, TopLock, Pin, Menu, Minimize, Maximize, Close
    };
    struct Btn {
        Action action = Action::None;
        QRect rect;
        QString label;
        bool iconOnly = true;
        bool caret = false;
        bool disabled = false;
    };
    QVector<Btn> buttons() const;
    int hitTest(const QPoint &pos) const;
    void activate(Action action, const QPoint &globalPos);
    void moveFocus(int step);
    QString tipText(Action action) const;
    static int idForAction(Action action);
    static Action actionForId(int id);

    QString m_title;
    bool m_topLocked = false;
    bool m_pinned = false;
    bool m_layersAvailable = false;
    bool m_layersOpen = false;
    int m_hover = -1;
    int m_press = -1;
    int m_focus = -1;
    bool m_keyboardFocus = false;
    ButtonFeedback *m_feedback = nullptr;
    TooltipHint *m_tip = nullptr;
    // buttons() 每次重算的布局结果（const 方法内写探针/文件名矩形）
    mutable QVector<int> m_hiddenIds;
    mutable QRect m_filenameRect;
};

// ==================== 底部控制岛（54px 胶囊，浮于画布下缘 17px） ====================
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);

    void setPage(int current, int total);
    int page() const { return m_page; }
    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }
    // 缩放胶囊独立显示（与目录进度无关）。kind: 0=自定义比例 1=原始 100%
    // 2=适合窗口；决定悬停提示（下一步操作）与点击切换语义，文案由 MainWindow 同步。
    void setZoomState(const QString &text, int kind);
    QString zoomText() const { return m_zoomText; }
    int zoomKind() const { return m_zoomKind; }
    // 画布拖动时弱化（不隐藏）。
    void setWeakened(bool weakened);
    bool isWeakened() const { return m_weakened; }
    // 栏内交互 / 进度拖动 / 键盘焦点期间必须保持可见。
    bool keepVisible() const;
    // 窗口可用宽度 -> 档位与自身宽度（常规 188 / 窄 119 轨道）。
    void setAvailableWidth(int width);
    int preferredWidth() const { return m_preferredWidth; }
    int trackWidth() const { return m_trackWidth; }
    QRect sliderRect() const;

    // 冒烟取证：合成 hover / pressed（不依赖真实光标）。
    void probeHover(int index);
    void probePress(int index);

signals:
    void openClicked();
    void listClicked();
    void zoomInClicked();
    void zoomOutClicked();
    void zoomToggleClicked();   // 缩放胶囊：100% ↔ 适合窗口 循环切换
    void rotateLeftClicked();
    void rotateRightClicked();
    void fullscreenClicked();
    void lockToggled();
    void prevClicked();
    void nextClicked();
    void prev10Clicked();
    void next10Clicked();
    void prevFolderClicked();
    void nextFolderClicked();
    void pageRequested(int index);
    void pagePreviewRequested(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    // 同 TopBar：底岛有键盘焦点时方向键用于按钮导航，不触发翻页快捷键。
    bool event(QEvent *event) override;

private:
    enum class Action {
        None, Open, List, ZoomOut, ZoomValue, ZoomIn,
        Rotate, Fullscreen, Prev, PageSlider, PageValue, Next, Lock
    };
    struct Item {
        Action action = Action::None;
        QRect rect;
    };
    enum class Tier { Full, Compact, Narrow, Minimal, Tiny };

    QVector<Item> items() const;
    QRect progressRect() const;
    Tier tier() const;
    bool showGroup1() const;
    bool showGroup3() const;
    bool showZoomButtons() const;
    void recomputeWidth();
    int hitIndex(const QPoint &pos) const;
    void activate(Action action, Qt::MouseButton button);
    void moveFocus(int step);
    void applyPageFromX(int x);
    int sliderPageFromX(int x) const;
    QString tipText(Action action) const;
    bool sliderEnabled() const { return m_pageCount >= 2; }
    int clampedPage(int page) const;

    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;
    QElapsedTimer m_previewClock;
    bool m_locked = false;
    bool m_weakened = false;
    int m_page = 1;              // 1-based 当前页
    int m_pageCount = 0;         // 0 = 未打开；1 = 单张（滑杆禁用）
    int m_pressPage = 1;
    QString m_zoomText = QStringLiteral("100%");
    int m_zoomKind = 1;          // 0=自定义比例 1=原始 100% 2=适合窗口
    // 档位文字交叉淡化：胶囊宽度固定，只做内容淡化，不做伸缩。
    QString m_prevZoomText;
    QVariantAnimation *m_zoomFade = nullptr;
    int m_availableWidth = 2000;
    int m_preferredWidth = 0;
    int m_trackWidth = 188;
    ButtonFeedback *m_feedback = nullptr;
    TooltipHint *m_tip = nullptr;
    int m_focus = -1;
    bool m_keyboardFocus = false;
};

// ============================ 状态浮层 ============================
// 左下图片信息、右下图片序号；始终显示，不拦截鼠标。
class StatusOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit StatusOverlay(QWidget *parent = nullptr);
    void setImageInfo(const QString &text);
    void setCounter(const QString &text);
    void clear();
    // 「用小字体显示 EXIF / 文件信息」：底部信息行 11px ↔ 10px。
    void setSmallFont(bool small);
    bool smallFont() const { return m_smallFont; }
    // 底部控制岛显示时把信息行抬到岛上方，避免被岛压住半行字。
    void setBottomInset(int inset);
    int bottomInset() const { return m_bottomInset; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_info;
    QString m_counter;
    bool m_smallFont = false;
    int m_bottomInset = 0;
};

// ============================ 空状态 ============================
class EmptyState : public QWidget
{
    Q_OBJECT
public:
    explicit EmptyState(QWidget *parent = nullptr);

signals:
    void openClicked();
    void folderClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect primaryRect() const;
    QRect secondaryRect() const;
    int m_hover = -1;
    int m_press = -1;
};

// ============================ 消息提示 ============================
class Toast : public QWidget
{
    Q_OBJECT
public:
    explicit Toast(QWidget *parent = nullptr);
    void showMessage(const QString &text);
    // 通知位置（键 config/notify_corner，默认 true）：true=右下角，false=顶部居中。
    void setCorner(bool corner);
    // 父窗口尺寸变化时重算几何（applyChromeLayout 调用）。
    void reposition();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    QTimer *m_timer = nullptr;
    bool m_corner = true;
};

// ==================== 左下角缩放浮标 ====================
class ZoomBadge : public QWidget
{
    Q_OBJECT
public:
    explicit ZoomBadge(QWidget *parent = nullptr);
    void showText(const QString &text);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    QTimer *m_timer = nullptr;
    QGraphicsOpacityEffect *m_opacity = nullptr;
    QPropertyAnimation *m_fade = nullptr;
};

} // namespace Chrome
