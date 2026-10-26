#include "FrameNavigator.h"

#include <QFileInfo>
#include <QImageIOHandler>
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
        && suffix != QLatin1String("mng")
        && suffix != QLatin1String("tif")
        && suffix != QLatin1String("tiff")) {
        return;
    }

    auto *reader = new QImageReader(path);
    const int count = reader->imageCount();
    if (count > 1) {
        // 按 EXIF Orientation 自动旋转是**恒开**功能：没有开关，也不读配置。
        // 设置页「根据 EXIF 信息自动旋转图片」勾选框因此已移除——勾上关不了、
        // 取消照样转，是假开关。要恢复可配，得让本文件、ImageCache、MainWindow
        // 三处解码点统一读同一个 key，光加回勾选框没有意义。
        reader->setAutoTransform(true);
        m_reader = reader;
        m_count = count;
        m_index = 0;
        // P0 自动播放：GIF、MNG。多页 TIFF 保持手动逐帧（扫描件自动翻页是干扰）。
        m_animatable = (suffix == QLatin1String("gif")
                        || suffix == QLatin1String("mng"));
        return;
    }

    // MNG 走顺序模式：实测 Qt 6.11 的 qmng（libmng）imageCount() 恒为 0、
    // jumpToImage() 无效，但 supportsOption(Animation) 为真，read() 能一帧帧读，
    // nextImageDelay() 给的是文件自带延迟。因此不预扫描总帧数（大动图会打开即全解）。
    if (suffix == QLatin1String("mng")
        && reader->supportsOption(QImageIOHandler::Animation)) {
        reader->setAutoTransform(true);
        m_reader = reader;
        m_sequential = true;
        m_count = 0;        // 0 = 总数未知
        m_seqPos = 0;
        m_lastDelay = qMax(0, reader->nextImageDelay());
        if (reader->read().isNull()) {   // 首帧已在 showImage 里显示，这里只用于定位
            clear();
            return;
        }
        m_animatable = true;
        return;
    }

    delete reader;
}

void FrameNavigator::clear()
{
    delete m_reader;
    m_reader = nullptr;
    m_index = 0;
    m_count = 0;
    m_lastDelay = 0;
    m_seqPos = 0;
    m_animatable = false;
    m_sequential = false;
}

QImage FrameNavigator::frameAt(int index)
{
    if (!m_reader || index < 0) {
        return QImage();
    }
    if (m_sequential) {
        // 顺序模式没有随机访问，只能重建 reader 从头跳。
        restartSequential();
        QImage img;
        for (int i = 0; i <= index; ++i) {
            m_lastDelay = qMax(0, m_reader->nextImageDelay());
            img = m_reader->read();
            if (img.isNull()) {
                return QImage();
            }
            m_seqPos = i;
        }
        m_index = index;
        return img;
    }
    if (index >= m_count) {
        return QImage();
    }
    // 先尝试在现有 reader 上跳转：TIFF 支持随机访问，GIF 支持向前跳。
    // 注意 GIF 的 jumpToImage() 返回 false 但向前跳仍生效，故不检查返回值。
    m_reader->jumpToImage(index);
    // 帧延迟必须在 read() 之前取：nextImageDelay() 返回的正是即将读出的这一帧
    // 的显示时长（毫秒）。动图推进按它来，不写死。
    m_lastDelay = qMax(0, m_reader->nextImageDelay());
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
    m_lastDelay = qMax(0, m_reader->nextImageDelay());
    img = m_reader->read();
    if (!img.isNull()) {
        m_index = index;
    }
    return img;
}

void FrameNavigator::restartSequential()
{
    if (!m_reader) {
        return;
    }
    const QString path = m_reader->fileName();
    delete m_reader;
    m_reader = new QImageReader(path);
    m_reader->setAutoTransform(true);
    m_seqPos = -1;      // 还没读任何帧，下一次 read() 才是第 0 帧
    m_index = 0;
    m_lastDelay = qMax(0, m_reader->nextImageDelay());
}

QImage FrameNavigator::nextFrame()
{
    if (!hasFrames()) {
        return QImage();
    }
    if (m_sequential) {
        m_lastDelay = qMax(0, m_reader->nextImageDelay());
        QImage img = m_reader->read();
        if (img.isNull()) {
            // 到末尾：回绕重放；重建后仍读不出才是真的解码失败
            restartSequential();
            m_lastDelay = qMax(0, m_reader->nextImageDelay());
            img = m_reader->read();
            if (img.isNull()) {
                return QImage();
            }
            m_seqPos = 0;
            m_index = 0;
            return img;
        }
        ++m_seqPos;
        m_index = m_seqPos;
        return img;
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
    if (m_sequential) {
        // 顺序模式只能重放：重建 reader 后前进到目标帧（最多 O(n)，MNG 少见）
        return frameAt(qMax(0, m_seqPos - 1));
    }
    int idx = m_index - 1;
    if (idx < 0) {
        idx = m_count - 1;
    }
    return frameAt(idx);
}
