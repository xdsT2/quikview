#pragma once

#include <QCheckBox>

class QVariantAnimation;

// 拨动开关（设置弹窗共用）：复用 QCheckBox 的 checked / toggled / 键盘空格与
// 可访问性语义，只把指示器换成「轨道 + 圆形滑块」自绘；不带文字（旁边由 QLabel
// 排版）。
//
// 视觉母版与绘制结构参考 Qlementine 的 Switch（MIT）：
//   https://github.com/oclero/qlementine/blob/dev/lib/src/widgets/Switch.cpp
//   初次抓取 2026-11-22；不可变锚点（2026-11-24 核对）：release tag v1.5.2，
//   commit 7825161fce66ac5a49bc0ffe39ab13df5ff8ff4a；快照 SHA256 见
//   src/PillToggle.cpp 文件头与 NOTICE 第七节。
//   上游为 QAbstractButton + QStyle 取色，此处仅参考绘制结构与状态划分，
//   几何与颜色均为本项目自定，颜色接 SoleView 现有色板（见 NOTICE 第七节）。
//
// 统一规格（所有设置页同一套，不再分 mini / 大纲放大档）：
//   轨道 40×22、圆角 11、滑块 16×16、滑块内边距 3、轨道描边 1；
//   控件 / 命中区 48×30（轨道 + 焦点环外扩 2 + 外距 2），焦点环 2px。
//   开 / 关 / 悬停 / 按下 / 禁用 / 键盘焦点共用同一套几何，状态由滑块位置、
//   填色对比与焦点环区分。
class PillToggle : public QCheckBox
{
    // Q_OBJECT：SettingsDialog 用 findChildren<PillToggle*> 统一遍历开关，
    // QMetaObject 查询要求该宏（2026-11-14 补）。
    Q_OBJECT
public:
    explicit PillToggle(QWidget *parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // 兼容保留（2026-11-22 起不再改变外观尺寸）：所有页面统一 40×22 轨道。
    // 签名与含义沿用旧调用点，避免逐处改调用。
    void setFluent(bool on);
    bool fluent() const { return m_fluent; }

    // 兼容保留：原「查看器右键菜单」页大纲放大档，现与其它页面同规格。
    void setLargeTrack(bool on);
    bool largeTrack() const { return m_large; }

    // 轨道与滑块尺寸（逻辑像素），供探针/取证读取，避免各处硬编码。
    static int trackWidth() { return 40; }
    static int trackHeight() { return 22; }
    static int handleDiameter() { return 16; }

    // 滑块从当前位置滑到新位置的时长（毫秒）。0 表示不做过渡。
    static int motionDurationMs() { return 120; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    // 自绘后没有原生指示器；整个 48×30 控件都是命中区（轨道 + 环位）。
    bool hitButton(const QPoint &pos) const override;
    // 用户操作（点击 / 空格）拨动时走短过渡；程序改值（同步主题状态）直接落位。
    void nextCheckState() override;
    void checkStateSet() override;

private:
    void startMotion(bool on);

    bool m_hover = false;
    bool m_pressed = false;
    bool m_userToggle = false;
    bool m_fluent = false;   // 仅记录调用方意图，不参与尺寸
    bool m_large = false;    // 同上
    qreal m_pos = 0.0;       // 0 = 关，1 = 开；过渡中为当前插值位置
    QVariantAnimation *m_motion = nullptr;
};
