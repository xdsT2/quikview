#pragma once

#include <QDateTime>
#include <QImage>
#include <QRect>
#include <QString>

// 显示就绪帧（Display Frame）生成模块（P0 显示管线提速）。
//
// 背景：基线版本把「内嵌 ICC→sRGB」与「HDR tone map」留在 GUI 主线程，
// 随每次 setPixmap 串行做整图转换，是翻页顿挫的来源之一。本模块把这两道
// 后处理抽成纯函数，交给后台解码链路随解码一起产出「显示就绪帧」；GUI 侧
// 只剩 QPixmap::fromImage + setPixmap。
//
// 操作顺序与基线 MainWindow::showImage 旧路径逐行对齐（先 toneMap，再
// ICC→sRGB），8bit/SDR 直通的图原样返回（QImage 隐式共享，不复制像素）。
namespace DisplayFrame {

// 帧准备选项快照。后台 worker 线程只读，不在线程里读 QSettings（跨线程
// 读配置不安全）；由 GUI 线程在提交任务时取一次快照带进来。
struct PrepareOptions {
    bool imageIcc = true;      // 使用图片内嵌 ICC：非 sRGB 图转 sRGB
    bool hdrTonemap = false;   // 高位深图映射到 SDR
};

// 旧路径复刻（基线 MainWindow::showImage 的 toSrgb/toneMap 语义）。
// 仅用于等价校验对照与离线基准，生产链路走 prepare()。两者当前是同一实现，
// 顺序/直通规则严格对齐基线，逐字节等价由单元测试锁定。
QImage legacyPrepare(const QImage &raw, const PrepareOptions &opt);

// 新路径：解码后处理，产出显示就绪帧。后台解码链路调用。
QImage prepare(const QImage &raw, const PrepareOptions &opt);

// 就绪帧缓存键：图源路径 + 文件 mtime + imageIcc + hdrTonemap + 显示器
// profile 标识。切 ICC/HDR 开关、换屏、profile 更新、文件改动都不会取到错帧。
QString cacheKey(const QString &path, qint64 mtimeSec,
                 const PrepareOptions &opt, const QString &monitorTag);

// 显示器 profile 标识：屏幕名 + 屏幕几何 + profile 路径 + profile 修改时间。
// 作为 monitorTag 拼进缓存键；ACM 开启 / 无有效 profile 时由调用方给空串。
QString monitorTag(const QString &screenName, const QRect &geometry,
                   const QString &profilePath, const QDateTime &profileMtime);

} // namespace DisplayFrame
