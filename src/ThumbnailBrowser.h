#pragma once

#include <QWidget>

class ImageCache;
class QListWidget;

// 缩略图浏览器：左侧停靠面板，网格展示当前目录所有图片，点击切换。
class ThumbnailBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ThumbnailBrowser(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    void setThumbSize(int px);

signals:
    void activateImage(int index);

private:
    void onThumbReady(const QString &path, const QImage &image);

    QListWidget *m_list = nullptr;
    ImageCache *m_cache = nullptr;
    QStringList m_files;
    int m_thumbSize = 96;
};
