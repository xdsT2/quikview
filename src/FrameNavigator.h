#pragma once

#include <QImage>
#include <QString>

class QImageReader;

// 多帧图片导航：动画 GIF / MNG、多页 TIFF 的逐帧读取。
// 独立于主窗口，MainWindow 只负责把它返回的帧显示出来。
//
// 两种工作模式：
//  1) 随机访问（GIF / TIFF）：imageCount() 有效，frameAt() 任意定位；
//  2) 顺序访问（MNG）：libmng 插件不实现 imageCount / jumpToImage，
//     只能 read() 一路往下读，到末尾重建 reader 回绕。
class FrameNavigator
{
public:
    ~FrameNavigator();

    // 打开文件，若为多帧（GIF/MNG/TIFF）则持有 reader，否则清空。
    void open(const QString &path);
    void clear();

    bool hasFrames() const { return m_sequential || m_count > 1; }
    int frameIndex() const { return m_index; }
    // 总帧数；0 表示未知（MNG 顺序模式不预扫描，避免打开即全解一遍）
    int frameCount() const { return m_sequential ? 0 : m_count; }

    // 最近一次成功取到的帧的文件自带延迟（毫秒）。
    // P0 动图自动播放按它推进，禁止用固定值（需求书硬规则 1）。
    int lastDelay() const { return m_lastDelay; }
    // 是否属于“应自动播放”的格式：GIF（随机访问）与 MNG（顺序）。
    // 多页 TIFF 保持手动逐帧（扫描件自动翻页是干扰），APNG/动 WebP 原生读不出多帧。
    bool animatable() const { return m_animatable; }

    // 读取指定帧；越界/失败返回空 QImage。
    QImage frameAt(int index);
    QImage nextFrame();  // 回绕
    QImage prevFrame();  // 回绕

private:
    void restartSequential();   // 顺序模式：重建 reader 回到第 0 帧

    QImageReader *m_reader = nullptr;
    int m_index = 0;
    int m_count = 0;
    int m_lastDelay = 0;
    int m_seqPos = 0;           // 顺序模式：当前已读到第几帧（0 基）
    bool m_animatable = false;
    bool m_sequential = false;
};
