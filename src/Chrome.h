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

private:
    QRect buttonRect(int index) const;   // 0=min 1=max 2=close
    int hitTest(const QPoint &pos) const;

    QString m_title;
    int m_hover = -1;
    int m_press = -1;
    ButtonFeedback *m_feedback = nullptr;
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

signals:
    void openClicked();
    void browseMenuRequested(const QPoint &globalPos);
    void slideshowMenuRequested(const QPoint &globalPos);
    void bookmarkMenuRequested(const QPoint &globalPos);
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

    int m_hover = -1;
    int m_press = -1;
    int m_showModeIcon = 0;
    bool m_locked = false;
    ButtonFeedback *m_feedback = nullptr;
    int m_focus = -1;            // 键盘焦点所在按钮
    bool m_keyboardFocus = false; // 焦点是否由键盘进入（决定是否显示焦点环、是否吃空格）
};

// ==================== 底部胶囊控制栏（408x72，浮于画布下缘） ====================
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    void setZoomPercent(int percent);   // 画布缩放百分比（100 = 实际大小）
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
    void zoomRequested(int percent);

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
        Prev, Next, Lock, ZoomOut, ZoomIn, ZoomValue, Slider
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
    void applySliderValueFromX(int x);
    int currentSliderPercent() const;

    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;
    bool m_locked = false;
    int m_percent = 68;
    ButtonFeedback *m_feedback = nullptr;
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
