#include "FrameNavigator.h"

#include <QFileInfo>
#include <QImageReader>

FrameNavigator::~FrameNavigator()
{
    clear();
}

void FrameNavigator::open(const QString &path)
{
    clear();

    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("gif")
        && suffix != QLatin1String("tif")
        && suffix != QLatin1String("tiff")) {
        return;
    }

    auto *reader = new QImageReader(path);
    const int count = reader->imageCount();
    if (count > 1) {
        reader->setAutoTransform(true);
        m_reader = reader;
        m_count = count;
        m_index = 0;
    } else {
        delete reader;
    }
}

void FrameNavigator::clear()
{
    delete m_reader;
    m_reader = nullptr;
    m_index = 0;
    m_count = 0;
}

QImage FrameNavigator::frameAt(int index)
{
    if (!m_reader || index < 0 || index >= m_count) {
        return QImage();
    }
    // 先尝试在现有 reader 上跳转：TIFF 支持随机访问，GIF 支持向前跳。
    // 注意 GIF 的 jumpToImage() 返回 false 但向前跳仍生效，故不检查返回值。
    m_reader->jumpToImage(index);
    QImage img = m_reader->read();
    if (!img.isNull()) {
        m_index = index;
        return img;
    }
    // GIF 向后跳（回绕）会失败：重建 reader 从头定位目标帧。
    const QString path = m_reader->fileName();
    delete m_reader;
    m_reader = new QImageReader(path);
    m_reader->setAutoTransform(true);
    m_reader->jumpToImage(index);
    img = m_reader->read();
    if (!img.isNull()) {
        m_index = index;
    }
    return img;
}

QImage FrameNavigator::nextFrame()
{
    if (!hasFrames()) {
        return QImage();
    }
    int idx = m_index + 1;
    if (idx >= m_count) {
        idx = 0;
    }
    return frameAt(idx);
}

QImage FrameNavigator::prevFrame()
{
    if (!hasFrames()) {
        return QImage();
    }
    int idx = m_index - 1;
    if (idx < 0) {
        idx = m_count - 1;
    }
    return frameAt(idx);
}
