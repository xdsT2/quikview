#pragma once

#include <QHash>
#include <QVector>
#include <QWidget>

#include "PsdLoader.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;
class HudBackdrop;
class HudEffect;

// 图层面板：主窗口右侧浮层。整图 + 文档图层顺序 + 可折叠分组 + 独立眼睛按钮。
// 只发信号，不直接改画布；可见性状态由 MainWindow 持有（会话内临时状态）。
// 外壳（磨砂底板/投影/12px 圆角/28×28 关闭热区/开合动效）与信息浮层共用
// HudShell 标准（派单 2026-11-14「浮层标准统一」）；内容行与交互保持本面板
// 特有语义（缩略图/滚动/显隐/预览不动）。
class LayerPanel : public QWidget
{
    Q_OBJECT
public:
    explicit LayerPanel(QWidget *parent = nullptr);

    void setDocument(const PsdDocument &doc, const QVector<bool> &visible,
                     int selectedIndex, bool simplified);

    QSize sizeHint() const override { return QSize(248, 520); }
    // 按实际行数算的自然高，避免 5 行内容挂 520px 空白面板
    int preferredHeight() const;
    void toggleGroup(int index);

    // 背板采样源（画布视口）：磨砂开时对它做区域渲染 + 模糊
    void setBackdropSource(QWidget *canvasViewport);

    // 显隐语义与信息浮层同款（显隐统一走这三条路径）：
    //   showHud()  显示并播 110ms 开启动效（减少动效时直接显示）
    //   dismiss()  动画关闭（85ms 淡出）
    //   hideNow()  直接隐藏（无动画，文档清除等场景）
    void showHud();
    void dismiss();
    void hideNow();
    // 关闭动画进行中（85ms 窗口内 toggle 不把「关到一半」误判成已关闭）
    bool isClosing() const { return m_closing; }

signals:
    void eyeToggled(int index);
    void layerSelected(int index);
    void compositeSelected();
    void closeRequested();
    void contentChanged();

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    // 当前折叠状态下应显示的层索引（含组头行，不含 groupEnd 边界标记）。
    // rebuild() 与 preferredHeight() 共用这一份判定，避免两处口径走偏。
    QVector<int> visibleLayerIndexes() const;

    PsdDocument m_doc;
    QVector<bool> m_visible;
    QHash<int, bool> m_collapsed;   // 组头层索引 -> 是否折叠
    int m_selected = -1;            // -1 = 整图
    bool m_simplified = false;
    QVBoxLayout *m_rows = nullptr;
    QScrollArea *m_scroll = nullptr;
    QLabel *m_hint = nullptr;
    HudBackdrop *m_backdrop = nullptr;
    HudEffect *m_effect = nullptr;
    bool m_closing = false;
};
