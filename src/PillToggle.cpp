// SPDX-FileCopyrightText: 2026 QuikView contributors
// SPDX-License-Identifier: MIT
//
// 绘制结构与状态划分参考 Qlementine 的 Switch 控件：
//   SPDX-FileCopyrightText: Olivier Cléro <oclero@hotmail.com>
//   SPDX-License-Identifier: MIT
//   来源：https://github.com/oclero/qlementine/blob/dev/lib/src/widgets/Switch.cpp
//   初次抓取日期：2026-11-22（当时按 dev 分支原文对齐，未留存 commit sha）
//   不可变锚点（2026-11-24 核对）：release tag v1.5.2，commit
//     7825161fce66ac5a49bc0ffe39ab13df5ff8ff4a
//     （2026-10-05T18:27:53+02:00，Merge pull request #192 from oclero/dev）
//   锚点快照校验哈希（SHA256）：
//     lib/src/widgets/Switch.cpp
//       C1BA13C8C5E9B8CC1933AF1CEC9593F6FF1CA6DE209374C1D4901119F28BAA3A
//     lib/include/oclero/qlementine/style/Theme.hpp（v1.5.2 中主题头文件实际路径）
//       AE41BFFCC7829679FB1CD0C3335A3D6A6315F1EFCE51E449377DE74C10C10D3A
// 上游用一个 QAbstractButton 子类自绘「圆角轨道 + 圆形滑块」，几何取
// theme.controlHeightLarge × controlHeightSmall，滑块直径 = 轨道高 - 内边距 2 倍，
// 滑块左界 = 轨道左 + 内边距 + 可滑动余量 * 位置比例；悬停 / 按下 / 禁用 / 焦点
// 只换色不换形，位置动画 OutCubic。此处仅参考该绘制结构与状态划分逻辑，
// 几何为本项目自定（轨道 40×22、滑块 16、内边距 3；上游默认 28×16、滑块 12、
// 内边距 2），颜色改接 QuikView 色板（UiStyle::fluent），不引入 Qlementine 的
// QStyle；未整文件拷贝上游源码，未引入上游库。
// 详见 NOTICE 第七节与 third_party/qlementine/。

#include "PillToggle.h"

#include "UiStyle.h"

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QVariantAnimation>

namespace {

// —— 统一规格（逻辑像素） ——
// 轨道 40×22；圆角取轨道高一半；滑块 16（内边距 3，与上游 borderWidth*2 在
// 22 高度上等比折算：22 - 3*2 = 16，滑块/轨道高 = 0.727，接近上游 12/16）；
// 焦点环 2px，外扩 2px，环外再留 2px 到控件边缘 → 控件 48×30。
constexpr int kTrackW = 40;
constexpr int kTrackH = 22;
constexpr qreal kTrackRadius = kTrackH / 2.0;
constexpr int kHandleInset = 3;
constexpr int kHandleD = kTrackH - kHandleInset * 2;   // 16
constexpr int kRingWidth = 2;
constexpr int kRingGap = 2;
constexpr int kEdgePad = 2;
constexpr int kWidgetW = kTrackW + 2 * (kRingGap + kEdgePad);   // 48
constexpr int kWidgetH = kTrackH + 2 * (kRingGap + kEdgePad);   // 30

// 主题切换 / 开关过渡时颜色与位置一起插值，避免颜色先跳、滑块后到。
QColor mixColor(const QColor &from, const QColor &to, qreal t)
{
    const qreal k = qBound(0.0, t, 1.0);
    return QColor(qRound(from.red() + (to.red() - from.red()) * k),
                  qRound(from.green() + (to.green() - from.green()) * k),
                  qRound(from.blue() + (to.blue() - from.blue()) * k),
                  qRound(from.alpha() + (to.alpha() - from.alpha()) * k));
}

// 轨道填色（对应上游 switchGrooveColor）：开=绿、关=中性灰，悬停/按下只做加减
// 明度，禁用向卡片底混色但保留色相与对比，保证「禁用 + 开」仍读得出是开。
QColor grooveColor(bool on, bool hover, bool pressed, bool disabled)
{
    const QColor base = on ? UiStyle::fluent::switchOnColor()
                           : UiStyle::fluent::switchOffColor();
    if (disabled)
        return mixColor(base, UiStyle::fluent::cardBgColor(), on ? 0.42 : 0.52);
    if (pressed)
        return mixColor(base, QColor(0, 0, 0), on ? 0.14 : 0.10);
    if (hover)
        return on ? mixColor(base, QColor(255, 255, 255), 0.12)
                  : mixColor(base, UiStyle::fluent::textColor(), 0.14);
    return base;
}

// 轨道描边（对应上游 switchGrooveBorderColor）。开启态在浅色主题沿用
// #218941 内描边，其余按轨道色压暗 1 档，深浅两套都保证轨道边界清晰。
QColor grooveBorderColor(bool on, bool disabled)
{
    if (disabled) {
        const QColor offBase = UiStyle::fluent::switchOffColor();
        const QColor onBase = UiStyle::fluent::switchOnColor();
        return on ? mixColor(onBase, UiStyle::fluent::cardBgColor(), 0.42)
                  : mixColor(offBase, UiStyle::fluent::cardBgColor(), 0.52);
    }
    if (on) {
        return UiStyle::isLight() ? UiStyle::fluent::switchEdgeColor()
                                  : mixColor(UiStyle::fluent::switchOnColor(),
                                             QColor(0, 0, 0), 0.18);
    }
    return mixColor(UiStyle::fluent::switchOffColor(), QColor(0, 0, 0),
                    UiStyle::isLight() ? 0.18 : 0.10);
}

// 滑块填色（对应上游 switchHandleColor）：恒为近白，悬停/按下轻微吸入轨道色，
// 禁用向卡片底混色但保持正圆与位置。
QColor handleColor(bool on, bool hover, bool pressed, bool disabled)
{
    const QColor knob = UiStyle::fluent::knobColor();
    if (disabled)
        return mixColor(knob, UiStyle::fluent::cardBgColor(), 0.35);
    const QColor groove = grooveColor(on, false, false, false);
    if (pressed)
        return mixColor(knob, groove, 0.12);
    if (hover)
        return mixColor(knob, groove, 0.06);
    return knob;
}

} // namespace

PillToggle::PillToggle(QWidget *parent)
    : QCheckBox(parent)
{
    setText(QString());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    // 固定为规格尺寸：大纲页折叠行在布局未激活时会被容器宽度拉伸
    // （实测 100×30），固定后所有页面（含隐藏行）都严格 48×30。
    setFixedSize(kWidgetW, kWidgetH);
    m_pos = isChecked() ? 1.0 : 0.0;

    m_motion = new QVariantAnimation(this);
    m_motion->setDuration(motionDurationMs());
    m_motion->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_motion, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pos = v.toReal();
        update();
    });
}

void PillToggle::nextCheckState()
{
    m_userToggle = true;
    QCheckBox::nextCheckState();
    m_userToggle = false;
    startMotion(isChecked());
}

void PillToggle::checkStateSet()
{
    QCheckBox::checkStateSet();
    // 程序改值（打开对话框时同步主题状态、跟随系统解析值变化）：直接落位。
    if (!m_userToggle) {
        m_motion->stop();
        m_pos = isChecked() ? 1.0 : 0.0;
        update();
    }
}

void PillToggle::startMotion(bool on)
{
    const qreal target = on ? 1.0 : 0.0;
    // 禁用态不会有用户拨动；不可见时没必要花一帧一帧的动画。
    if (motionDurationMs() <= 0 || !isEnabled() || !isVisible()) {
        m_motion->stop();
        m_pos = target;
        update();
        return;
    }
    if (qFuzzyCompare(m_pos + 1.0, target + 1.0))
        return;   // 已经在目标位置（连续点击被打断后回到原状态）
    // 从当前位置续走：快速反向切换时位置连续，不会跳回起点。
    m_motion->stop();
    m_motion->setStartValue(m_pos);
    m_motion->setEndValue(target);
    m_motion->start();
}

void PillToggle::setFluent(bool on)
{
    // 2026-11-22：外观统一后本开关不再有「样板 / 旧版」两套尺寸，只记录调用方
    // 意图，不改几何；保留签名避免调用点分叉。
    m_fluent = on;
}

void PillToggle::setLargeTrack(bool on)
{
    // 同上：原大纲放大档已并入统一规格。
    m_large = on;
}

QSize PillToggle::sizeHint() const
{
    return QSize(kWidgetW, kWidgetH);   // 轨道 40×22 + 焦点环 2 + 外距 2
}

QSize PillToggle::minimumSizeHint() const
{
    return sizeHint();
}

void PillToggle::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 轨道在控件内居中；四周各留 4px 给焦点环（2px 环 + 2px 外距）。
    const QRectF track((width() - kTrackW) / 2.0, (height() - kTrackH) / 2.0,
                       kTrackW, kTrackH);
    const qreal pos = qBound(0.0, m_pos, 1.0);
    const bool disabled = !isEnabled();
    const bool hover = m_hover && !m_pressed;
    const bool pressed = m_pressed;

    const QColor fill = mixColor(grooveColor(false, hover, pressed, disabled),
                                 grooveColor(true, hover, pressed, disabled), pos);
    const QColor border = mixColor(grooveBorderColor(false, disabled),
                                   grooveBorderColor(true, disabled), pos);
    const QColor knob = mixColor(handleColor(false, hover, pressed, disabled),
                                 handleColor(true, hover, pressed, disabled), pos);

    // 轨道：填色矩形按上游做法内缩半个描边，再单独描 1px 边框。
    const qreal halfBorder = 0.5;
    const QRectF fillRect = track.adjusted(halfBorder, halfBorder,
                                           -halfBorder, -halfBorder);
    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(fillRect, kTrackRadius, kTrackRadius);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(border, 1));
    p.drawRoundedRect(fillRect, kTrackRadius, kTrackRadius);

    // 滑块：直径 = 轨道高 - 内边距 2 倍；左界 = 轨道左 + 内边距 + 可滑动余量 * 位置。
    const qreal grooveSpan = kTrackW - kHandleInset * 2 - kHandleD;
    qreal handleX = track.left() + kHandleInset + grooveSpan * pos;
    // 按下时滑块朝轨道中心收 1px，给出「被压住」的手感（与上游一致的做法）。
    if (pressed && !disabled)
        handleX += (pos >= 0.5 ? -1.0 : 1.0);
    const QRectF handle(handleX, track.top() + kHandleInset, kHandleD, kHandleD);
    p.setPen(Qt::NoPen);
    p.setBrush(knob);
    p.drawEllipse(handle);

    // 焦点环：只在「键盘导航 + 当前持有焦点」时出现。鼠标点击后 SettingsDialog
    // 的输入来源追踪会把 kbFocus 置 false，环立即消失。
    if (hasFocus() && property("kbFocus").toBool()) {
        const QRectF ring = track.adjusted(-kRingGap, -kRingGap, kRingGap, kRingGap);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(UiStyle::color(UiStyle::fluent::focus()), kRingWidth));
        const qreal ringRadius = ring.height() / 2.0;
        p.drawRoundedRect(ring, ringRadius, ringRadius);
    }
}

void PillToggle::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QCheckBox::enterEvent(event);
}

void PillToggle::leaveEvent(QEvent *event)
{
    m_hover = false;
    m_pressed = false;
    update();
    QCheckBox::leaveEvent(event);
}

void PillToggle::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && isEnabled()) {
        m_pressed = true;
        update();
    }
    QCheckBox::mousePressEvent(event);
}

void PillToggle::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressed = false;
    update();
    QCheckBox::mouseReleaseEvent(event);
}

bool PillToggle::hitButton(const QPoint &pos) const
{
    return rect().contains(pos);
}
