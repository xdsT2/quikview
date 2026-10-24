#pragma once

#include <QColor>
#include <QPixmap>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>
#include <QWidget>

class QTimer;
class QGraphicsOpacityEffect;
class QPropertyAnimation;

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

    int m_hover = -1;
    int m_press = -1;
    int m_showModeIcon = 0;
    bool m_locked = false;
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
    Action hitTest(const QPoint &pos) const;
    void applySliderValueFromX(int x);
    int currentSliderPercent() const;

    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;
    bool m_locked = false;
    int m_percent = 68;
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
