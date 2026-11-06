#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class HistogramView;

// 图片信息浮层：对齐原型 .info-overlay（左上覆盖层，宽度自适应内容、上限 440）。
// 2026-11-03 深夜定稿：深色半透明底板（rgba(28,29,31,0.88) + 1px 细边 + 8px 圆角）、
// 有值才显示（空串/'-'/未知整行不生成，无 EXIF 时不显示占位行）、
// 值列不省略——长文件名/长 GPS 允许 WrapAnywhere 换行读全，Tooltip/复制保留全文。
// 结构：顶部 22px 圆钮行（关闭 / 复制参数，无标题文字）→ 两列键值 grid
//       （键左对齐灰、值左对齐，组间 7px 空隙）→ 底部可折叠直方图
//       （默认展开，点标题折叠；翻图/clear 不重置用户选择）。
// 相比旧版去掉：预览图、文件路径、三页签。
// 浮层是覆盖层：不参与 updateCanvasInsets，不挤压画布；位置（含缩略图面板
// 开合时的横移、全屏贴顶）由 MainWindow::applyChromeLayout 决定。
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

    // 主题切换后重装面板局部 QSS（颜色令牌变了，不重装会停在旧主题）
    void refreshThemeStyle();

    // 验收探针：把面板各层 sizeHint/实际高度写到文件（smoke 命令 metadump）
    void dumpHeights(const QString &path, const QString &tag = QString()) const;

    // 验收探针：直方图折叠开关（smoke 命令 histtoggle 触发 click）
    QPushButton *m_histToggle = nullptr;

signals:
    void closed();
    void copied();   // 复制参数成功（MainWindow 负责 toast 反馈）
    void heightChanged();   // 直方图折叠/展开改了内容高度（MainWindow 重排浮层几何）

private:
    void buildUi();
    void rebuildGrid();
    struct Row {
        QString key;
        QString value;
    };
    void addGroup(const QList<Row> &rows);
    void addGroupGap();
    void finishLayout();   // 组装完 grid 后算宽度
    void renderHistogram(const QImage &image);

    QStringList m_copyLines;     // 与界面行序一致的「键: 值」文本（组间为空行）

    QWidget *m_scroll = nullptr;   // 内容区容器（2026-11-04 起不再滚动）
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_gridHolder = nullptr;
    QGridLayout *m_grid = nullptr;
    QPushButton *m_mapBtn = nullptr;
    QString m_mapUrl;

    QWidget *m_histBody = nullptr;
    HistogramView *m_histogram = nullptr;

    int m_contentWidth = 320;    // 两列 grid 的自然宽度（钳到 [280, 440]）
    int m_maxKeyW = 0;           // 自然宽度的两列度量（各 label 自己的字体）
    int m_maxValW = 0;
    bool m_smallFont = false;    // 键值行字号档位（false=12px，true=10px）
    QString m_lastPath;          // 最近一次 showImage 的入参（字号切换时重建用）
    QImage m_lastImage;
};
