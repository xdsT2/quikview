#pragma once

#include <QWidget>

class ImageCache;
class QListWidget;

// 缩略图浏览器：左侧停靠面板（宽 160、高度随卡片数量），
// 每张图固定在 128×128 方形预览区内等比完整显示，文件名在下方。
class ThumbnailBrowser : public QWidget
{
    Q_OBJECT
public:
    explicit ThumbnailBrowser(ImageCache *cache, QWidget *parent = nullptr);

    void setFiles(const QStringList &files, int currentIndex);
    void setCurrent(int index);
    // 面板按卡片数量取高：3 张就只包到 3 张，超出可用高度再由调用方封顶并滚动。
    int preferredHeight() const;

signals:
    void activateImage(int index);

private:
    void onThumbReady(const QString &path, const QImage &image);

    QListWidget *m_list = nullptr;
    ImageCache *m_cache = nullptr;
    QStringList m_files;
};
