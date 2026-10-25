#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
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
    qreal hover = 0.0;   // 120ms 淡入
    qreal press = 0.0;   // 90ms，绘制时整体缩放至 94%
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
    void setPressed(int index);    // -1 = 松开（会 90ms 恢复，不弹跳）
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

// ============================ 标题栏（28px） ============================
class CaptionBar : public QWidget
{
    Q_OBJECT
public:
    explicit CaptionBar(QWidget *parent = nullptr);
    void setTitle(const QString &title);

signals:
    void fullscreenClicked();
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
    void wheelEvent(QWheelEvent *event) override;

private:
    QRect buttonRect(int index) const;   // 0=min 1=max 2=close
    int hitTest(const QPoint &pos) const;
    QString tipText(int index) const;

    QString m_title;
    int m_hover = -1;
    int m_press = -1;
    ButtonFeedback *m_feedback = nullptr;
    TooltipHint *m_tip = nullptr;
};

// ============================ 命令栏（34px） ============================
class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);
    void setShowModeIcon(int index);   // 兼容旧调用，视觉暂不展示模式小图
    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }
    // HDR 色调映射开关（原型 .command.hdr-toggle：44px 文字钮，开时 accent 选中态）
    void setHdr(bool on);
    bool isHdr() const { return m_hdr; }

signals:
    void openClicked();
    void browseMenuRequested(const QPoint &globalPos);
    void slideshowMenuRequested(const QPoint &globalPos);
    void bookmarkMenuRequested(const QPoint &globalPos);
    void hdrToggled();
    void editMenuRequested(const QPoint &globalPos);
    void copytoMenuRequested(const QPoint &globalPos);
    void lockToggled();
    void moreMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    struct Btn {
        int id = -1;
        QString text;
        QRect rect;
        bool primary = false;
        bool caret = false;
    };
    QVector<Btn> buttons() const;
    int hitTest(const QPoint &pos) const;
    void activate(int index, const QPoint &globalPos);
    void moveFocus(int step);
    QString tipText(int index) const;

    int m_hover = -1;
    int m_press = -1;
    int m_showModeIcon = 0;
    bool m_locked = false;
    bool m_hdr = false;
    ButtonFeedback *m_feedback = nullptr;
    TooltipHint *m_tip = nullptr;
    int m_focus = -1;            // 键盘焦点所在按钮
    bool m_keyboardFocus = false; // 焦点是否由键盘进入（决定是否显示焦点环、是否吃空格）
};

// ==================== 底部胶囊控制栏（440x72，浮于画布下缘） ====================
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    // 第一行 = 翻页进度（对齐原型 #pageSlider）：current 1-based；total<=0 整行
    // 不绘制，total<2 滑杆禁用（提示「只有一张图片」）。缩放百分比只由左下
    // ZoomBadge 呈现，胶囊里不再出现百分比。
    void setPage(int current, int total);
    int page() const { return m_page; }   // 冒烟报告核对滑杆与当前图是否同步
    void setLocked(bool locked);
    bool isLocked() const { return m_locked; }

signals:
    void openClicked();
    void listClicked();
    void fitClicked();
    void zoomInClicked();
    void zoomOutClicked();
    void rotateLeftClicked();
    void rotateRightClicked();
    void lockToggled();
    void prevClicked();
    void nextClicked();
    void prev10Clicked();       // 右键点 ‹
    void next10Clicked();       // 右键点 ›
    void prevFolderClicked();   // 中键点 ‹
    void nextFolderClicked();   // 中键点 ›
    void pageRequested(int index);   // 1-based；只在松开滑杆 / 键盘步进时发

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

private:
    enum class Action {
        None, Open, List, Fit, RotateLeft, RotateRight,
        Prev, Next, Lock, ZoomOut, ZoomIn, PageValue, PageSlider
    };
    struct Item {
        Action action = Action::None;
        QRect rect;
    };
    QVector<Item> items() const;
    QVector<Item> layoutItems(QVector<int> *separators) const;  // 唯一布局来源
    int hitIndex(const QPoint &pos) const;
    void activate(Action action);
    void moveFocus(int step);
    void applyPageFromX(int x);        // 拖动中只更新视觉，不发 pageRequested
    int sliderPageFromX(int x) const;
    QString tipText(Action action) const;
    bool sliderEnabled() const { return m_pageCount >= 2; }
    int clampedPage(int page) const;

    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;
    bool m_locked = false;
    int m_page = 1;              // 1-based 当前页
    int m_pageCount = 0;         // 0 = 未打开（整行不画）；1 = 单张（滑杆禁用）
    int m_pressPage = 1;         // 按下滑杆时的页码，松开时比较决定是否发信号
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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_info;
    QString m_counter;
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

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_text;
    QTimer *m_timer = nullptr;
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
