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
// Honeyview 5.53 主窗口铬件（对照 resources/skin/skin_utf8.xml 布局参数 +
// 真机截图）。四个自绘部件：标题栏 / 工具栏 / 底部胶囊控制栏 / 消息提示。
// 皮肤贴图 4 帧横排：帧0 常态 / 帧1 悬停 / 帧2 按下 / 帧3 禁用。
// ---------------------------------------------------------------------------
namespace Chrome {

// resources/skin 目录解析（构建树与部署树都覆盖）
QString skinDir();

// 取整张贴图
QPixmap skinPixmap(const QString &name);
// 取 4 帧贴图的第 idx 帧
QPixmap skinFrame(const QString &name, int idx);

// ============================ 标题栏（25px） ============================
// 布局：左 logo(btn_logo) + 标题文字；右 全屏/最小/最大(btn_* 24x21) + 关闭(红 50x21)
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
    void leaveEvent(QEvent *event) override;

private:
    QVector<QRect> buttonRects() const;   // 0=full 1=min 2=max 3=close
    int hitTest(const QPoint &pos) const;
    void startDrag();

    QString m_title;
    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;
    QPoint m_dragOffset;
};

// ============================ 工具栏（31px） ============================
// 右对齐按钮组（自右向左）：锁定 / 复制到 / 编辑 / 书签 / 幻灯片放映 / 浏览
// 每钮 = 文字 + btn_expand 下拉箭头（锁定为文字 + btn_small_check 圆勾）
// 浏览钮最左带 static_showmode_0..4 模式图标。悬停浅蓝 tint。
class ToolBar : public QWidget
{
    Q_OBJECT
public:
    explicit ToolBar(QWidget *parent = nullptr);
    void setShowModeIcon(int index);   // 0..4 -> static_showmode_0..4

signals:
    void browseMenuRequested(const QPoint &globalPos);
    void slideshowMenuRequested(const QPoint &globalPos);
    void bookmarkMenuRequested(const QPoint &globalPos);
    void editMenuRequested(const QPoint &globalPos);
    void copytoMenuRequested(const QPoint &globalPos);
    void lockToggled();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Btn {
        QString text;
        bool isLock;
        bool isShowmode;
        QRect rect;
    };
    QVector<Btn> buttons() const;
    int hitTest(const QPoint &pos) const;

    int m_hover = -1;
    int m_press = -1;
    int m_showModeIcon = 0;
};

// ==================== 底部胶囊控制栏（460x80，浮于画布下缘） ====================
// static_controlpanel.png 整体贴图；左 ▲打开 ☰列表，中部滑轨 + − ↺ ↻ 锁定，
// 右 ‹ ›。大圆钮 46x46（btn_open/btn_list/btn_prev/btn_next 含圆底），
// 小钮 28x28（btn_small_zoomin/zoomout、btn_rotate_left/right）。
class ControlBar : public QWidget
{
    Q_OBJECT
public:
    explicit ControlBar(QWidget *parent = nullptr);
    void setZoomPercent(int percent);   // 画布缩放百分比（100 = 实际大小）

signals:
    void openClicked();
    void listClicked();
    void zoomInClicked();
    void zoomOutClicked();
    void rotateLeftClicked();
    void rotateRightClicked();
    void lockToggled();
    void prevClicked();
    void nextClicked();
    void prev10Clicked();   // 右键点 ‹（skin: CommandR=CMD_PREV10）
    void next10Clicked();   // 右键点 ›
    void prevFolderClicked();  // 中键点 ‹（skin: CommandM=CMD_PREVPATH）
    void nextFolderClicked();  // 中键点 ›
    void zoomRequested(int percent);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *) override;

private:
    struct Item {
        const char *name;
        QRect rect;
    };
    QVector<Item> items() const;
    int hitTest(const QPoint &pos) const;   // -1 无
    void applySliderValueFromX(int x);      // 轨道 x -> 滑轨值 -> zoomRequested
    QRect sliderTrackRect() const;          // 轨道槽（贴图拉伸区）

    int m_hover = -1;
    int m_press = -1;
    bool m_dragging = false;                // 拖动滑轨中
    int m_value = 0;                        // 滑轨值 -100..100（0 = 100%）
};

// ============================ 消息提示 ============================
// 画布顶部居中的深色圆角条，2 秒后消失
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

// ==================== 左下角缩放浮标（SPEC Viewer 合同） ====================
// 缩放/浏览方式变化时短暂显示（"适合窗口"/"适合宽度"/"214%"），1.5s 后淡出，非常驻
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
