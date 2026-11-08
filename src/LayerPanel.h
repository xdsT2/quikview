#pragma once

#include <QHash>
#include <QVector>
#include <QWidget>

#include "PsdLoader.h"

class QLabel;
class QScrollArea;
class QVBoxLayout;

// 图层面板：主窗口右侧浮层。整图 + 文档图层顺序 + 可折叠分组 + 独立眼睛按钮。
// 只发信号，不直接改画布；可见性状态由 MainWindow 持有（会话内临时状态）。
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

signals:
    void eyeToggled(int index);
    void layerSelected(int index);
    void compositeSelected();
    void closeRequested();
    void contentChanged();

private:
    void rebuild();

    PsdDocument m_doc;
    QVector<bool> m_visible;
    QHash<int, bool> m_collapsed;   // 组头层索引 -> 是否折叠
    int m_selected = -1;            // -1 = 整图
    bool m_simplified = false;
    QVBoxLayout *m_rows = nullptr;
    QScrollArea *m_scroll = nullptr;
    QLabel *m_hint = nullptr;
};
