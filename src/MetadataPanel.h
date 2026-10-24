#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class QPushButton;
class QScrollArea;
class QTabBar;
class QVBoxLayout;

class ElidedLabel;
class HistogramView;
class PreviewBox;

// 图片信息浮层：对齐原型 .info-panel（344 宽深色圆角卡片）。
// 结构：44px 头部（标题 + 关闭）→ 滚动区（预览 / 文件名 / 路径 / 三页签 / 元数据）。
// 三个页签共用同一个滚动区，靠显隐切换，避免嵌套滚动。
class MetadataPanel : public QWidget
{
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void showImage(const QString &path, const QImage &image);
    void clear();

signals:
    void closed();

private:
    void buildUi();
    void selectTab(int index);
    void fillBasicPage(const QString &path, const QImage &image);
    void loadExif(const QString &path);
    void renderHistogram(const QImage &image);

    QScrollArea *m_scroll = nullptr;
    PreviewBox *m_preview = nullptr;
    ElidedLabel *m_fileName = nullptr;
    ElidedLabel *m_filePath = nullptr;
    QTabBar *m_tabs = nullptr;

    QWidget *m_basicPage = nullptr;
    QWidget *m_exifPage = nullptr;
    QWidget *m_histPage = nullptr;
    QVBoxLayout *m_basicLayout = nullptr;
    QVBoxLayout *m_exifLayout = nullptr;
    QVBoxLayout *m_histLayout = nullptr;

    QPushButton *m_mapBtn = nullptr;
    HistogramView *m_histogram = nullptr;       // 直方图页签
    HistogramView *m_histogramBasic = nullptr;  // 基本信息页底部（同一份数据）
    QString m_mapUrl;
};
