#pragma once

#include <QDockWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QImage;
class QLabel;

// 元数据面板：基本信息 + EXIF（libexiv2）+ GPS（带地图查看按钮）。
class MetadataPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit MetadataPanel(QWidget *parent = nullptr);

    void showImage(const QString &path, const QImage &image);
    void clear();

private:
    void loadExif(const QString &path);
    void addSection(const QString &title);
    void addItem(const QString &key, const QString &value);
    void renderHistogram(const QImage &image);

    QTreeWidget *m_tree = nullptr;
    QPushButton *m_mapBtn = nullptr;
    QLabel *m_histogram = nullptr;
    QString m_mapUrl;
};
