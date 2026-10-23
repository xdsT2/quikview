#pragma once

#include <QImage>
#include <QString>

class QImageReader;

// 多帧图片导航：动画 GIF / 多页 TIFF 的逐帧读取。
// 独立于主窗口，MainWindow 只负责把它返回的帧显示出来。
class FrameNavigator
{
public:
    ~FrameNavigator();

    // 打开文件，若为多帧（GIF/TIFF 且 imageCount>1）则持有 reader，否则清空。
    void open(const QString &path);
    void clear();

    bool hasFrames() const { return m_count > 1; }
    int frameIndex() const { return m_index; }
    int frameCount() const { return m_count; }

    // 读取指定帧；越界/失败返回空 QImage。
    QImage frameAt(int index);
    QImage nextFrame();  // 回绕
    QImage prevFrame();  // 回绕

private:
    QImageReader *m_reader = nullptr;
    int m_index = 0;
    int m_count = 0;
};
