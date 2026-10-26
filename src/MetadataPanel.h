#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class HistogramView;

// 图片信息浮层：对齐原型 .info-overlay（左上覆盖层，宽度自适应内容、上限 440）。
// 结构：顶部 22px 圆钮行（关闭 / 复制参数，无标题文字）→ 两列键值 grid
//       （键左对齐灰、值右对齐，组间 7px 空隙）→ 底部可折叠直方图（默认收起）。
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

    // 验收探针：把面板各层 sizeHint/实际高度写到文件（smoke 命令 metadump）
    void dumpHeights(const QString &path, const QString &tag = QString()) const;

signals:
    void closed();
    void copied();   // 复制参数成功（MainWindow 负责 toast 反馈）

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

    QScrollArea *m_scroll = nullptr;
    QWidget *m_body = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
    QWidget *m_gridHolder = nullptr;
    QGridLayout *m_grid = nullptr;
    QLabel *m_noExifHint = nullptr;
    QPushButton *m_mapBtn = nullptr;
    QString m_mapUrl;

    QPushButton *m_histToggle = nullptr;
    QWidget *m_histBody = nullptr;
    HistogramView *m_histogram = nullptr;

    int m_contentWidth = 320;    // 两列 grid 的自然宽度（钳到 [280, 440]）
    int m_maxKeyW = 0;           // 自然宽度的两列度量（各 label 自己的字体）
    int m_maxValW = 0;
};
