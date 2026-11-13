#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QPair>
#include <QString>
#include <QWidget>

#include "UiStyle.h"   // hud:: 材质令牌用于成员默认值

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;
class QFrame;

class HistogramView;
class HudBackdrop;
class HudEffect;
class WrappingLabel;

// 图片信息 HUD（左上覆盖层，Tab-HUD 设计定稿 2026-11-13）。
// 结构：30px 标题栏（「图片信息」+ 右侧 28×28 复制/关闭）→ 键值 grid
//       （键列 76、列间 8、行框 18、行间 2；组间 4+1px 细分隔+4；有值才显示，
//       长值 WrapAnywhere 换行读全，Tooltip/复制保留全文）→ GPS 行下「在地图中
//       查看」中性文字链接 → 底部可折叠直方图（默认展开，折叠态跨切图保持）。
// 材质：固定深色（亮暗主题同套）。磨砂材质开（config/ui/blur_material）时背板
//       实时采样画布做 ~20px 模糊 + #212121@91% 染色；关/不可用时回退 #242424
//       实底。外圆角 12、1px 白 16% 边界，阴影经 HudEffect（下 5 / 模糊 18 / 黑 33%）。
// 动效：开 110ms（透明度 0→1、缩放 0.99→1，原点左上）；关 85ms 仅淡出；
//       系统减少动效时直接切换。HudEffect 常驻（静息时只画阴影）。
// 交互：非模态浮层，画布焦点保留；Esc 优先关闭由 MainWindow::keyPressEvent 分支；
//       应用失活由 MainWindow::onActivationChanged 调 hideNow()。
// 布局：宽度自适应 [314, 340]，确需扩宽 ≤440 且受 MainWindow 下发的可用宽约束；
//       位置（含全屏贴顶）由 MainWindow::applyChromeLayout 决定，不参与
//       updateCanvasInsets，不挤压画布。
class MetadataPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void showImage(const QString &path, const QImage &image);
    void clear();

    // 「用小字体显示 EXIF / 文件信息」：键值行 12px ↔ 10px。切换后按最近一次
    // showImage 的入参重建 grid，宽度与折行随之更新。
    void setSmallFont(bool small);
    // 当前字号档位（SET-02 即时预览：设置页开窗时快照运行态用）
    bool smallFont() const { return m_smallFont; }

    // 背板采样源（画布视口）：磨砂开时对它做区域渲染 + 模糊
    void setBackdropSource(QWidget *canvasViewport);

    // 可用宽度约束（MainWindow 按「窗口宽 - 左边距 - 右侧余量」下发）
    void setAvailableWidth(int w);

    // 显隐语义（Tab/关闭按钮/Esc/失焦统一走这三条路径）：
    //   showHud()  显示并播 110ms 开启动效（减少动效时直接显示）
    //   dismiss()  动画关闭（85ms 淡出）+ 发 closed 同步菜单勾选
    //   hideNow()  失活等场景直接隐藏（无动画）+ 发 closed
    void showHud();
    void dismiss();
    void hideNow();
    // 关闭动画进行中（report 的 hud= 字段用它判定，85ms 窗口内不算可见）
    bool isClosing() const { return m_closing; }
    bool histFolded() const;

    // 主题切换后重装面板局部 QSS（磨砂设置也可能变了，背板一并重采样）
    void refreshThemeStyle();

    // 验收探针：把面板各层 sizeHint/实际高度写到文件（smoke 命令 metadump）
    void dumpHeights(const QString &path, const QString &tag = QString()) const;

    // 验收探针：直方图折叠开关（smoke 命令 histtoggle 触发 click）
    QPushButton *m_histToggle = nullptr;
    // 验收探针：复制参数按钮（smoke 命令 hudcopy 触发 click，验证真实剪贴板动作）
    QPushButton *m_copyBtn = nullptr;

signals:
    void closed();
    void copied();   // 复制参数成功（MainWindow 负责 toast 反馈）
    void heightChanged();   // 直方图折叠/展开改了内容高度（MainWindow 重排浮层几何）

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void rebuildGrid();
    struct Row {
        QString key;
        QString value;
        bool strong = false;   // 文件名 600 字重
    };
    void addGroup(const QList<Row> &rows);
    void addGroupGap();
    void finishLayout();   // 组装完 grid 后算宽度/行高
    void updateHeights();   // 确定性总高：键值 grid + 标题栏 + 直方图逐项求和
    void renderHistogram(const QImage &image);
    void startOpenAnimation();
    static bool reduceMotion();
    void updateFocusProperty(QObject *watched, QEvent *event);

    QStringList m_copyLines;     // 与界面行序一致的「键: 值」文本（组间为空行）

    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_gridHolder = nullptr;
    QGridLayout *m_grid = nullptr;
    QPushButton *m_mapBtn = nullptr;
    QString m_mapUrl;

    QWidget *m_histBody = nullptr;
    HistogramView *m_histogram = nullptr;
    QFrame *m_histSep = nullptr;

    HudBackdrop *m_backdrop = nullptr;
    HudEffect *m_effect = nullptr;
    bool m_closing = false;

    QList<QPair<WrappingLabel *, int>> m_valueRows;   // 值标签 → grid 行（行高按折行数求出）
    QHash<int, int> m_fixedRows;   // 固定高行（组分隔 9 / 地图链接 20）→ 行高
    int m_gridRow = 0;             // 自管行号：Qt6 空 QGridLayout::rowCount() 返回 1，不可信

    int m_contentWidth = UiStyle::hud::WidthBase;   // 两列 grid 的自然宽度（钳到 [314, 440]）
    int m_availW = 0;            // MainWindow 下发的窗口可用宽约束（0 = 不约束）
    int m_maxValW = 0;           // 自然宽度的值列度量（单行宽，钳 330）
    bool m_smallFont = false;    // 键值行字号档位（false=12px，true=10px）
    QString m_lastPath;          // 最近一次 showImage 的入参（字号切换时重建用）
    QImage m_lastImage;
};
