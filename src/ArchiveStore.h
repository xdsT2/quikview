#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QSharedPointer>
#include <QSize>
#include <QString>

#include "LoadError.h"
#include "ZipArchive.h"

// 压缩包「虚拟目录」的唯一入口（派单《ZIP 与 CBZ 压缩包直读》2026-11-18）。
//
// 一个包内条目的路径 = 包文件的绝对路径 + '/' + 包内条目名：
//     E:/comics/book.cbz/001.jpg
// 这样 QFileInfo(path).fileName() 就是条目名，胶片栏/标题/缩略图的既有代码
// 不需要为「虚拟路径」再写一套取名的逻辑；同时 lastIndexOf('/') 之前的部分
// 天然就是包路径，拆分无歧义（第一阶段只列顶层条目，条目名不含 '/'）。
//
// 职责：
//  - 已打开包的注册表（跨线程共享，读接口 const）；
//  - 虚拟路径的拼装/拆分/展示名（「包名 › 内部路径」）；
//  - 条目解码（QImageReader 从内存流读，不落盘）。
// 不做：包内子目录递归、跨包连续浏览（第二阶段）。
class ArchiveStore
{
public:
    // 后缀判断（.zip / .cbz），只看名字不碰文件，供文件对话框/拖放/命令行共用
    static bool isArchiveFile(const QString &path);

    // 虚拟路径拼装与拆分
    // 批量拼 5000 条时用前缀复用，避免每条都做一次 QFileInfo 归一化
    static QString entryPathPrefix(const QString &archivePath);   // 归一化包路径 + '/'
    static QString joinEntryPath(const QString &archivePath, const QString &entryName);
    static bool splitEntryPath(const QString &path, QString *archivePath = nullptr,
                               QString *entryName = nullptr);
    static bool isEntryPath(const QString &path);
    static QString archivePathOf(const QString &path);   // 非条目路径返回空
    static QString entryNameOf(const QString &path);
    // 展示名：目录浏览仍是文件名；包内条目是「包名 › 内部路径」（派单 §五.5）
    static QString displayName(const QString &path);

    // 打开/复用（同一路径只解析一次中央目录）。失败返回空指针，
    // error/status 给出可展示文案与分类。
    static QSharedPointer<ZipArchive> open(const QString &archivePath,
                                          QString *error = nullptr,
                                          ZipArchive::Status *status = nullptr);
    // 记为「当前正在浏览的包」：下一张图解码、换包时释放旧包都以它为准
    static void setActive(const QString &archivePath);
    static QSharedPointer<ZipArchive> active();

    // 条目解码：scaledTo 有效时按比例解码（快帧/缩略图共用），不落盘。
    static QImage decode(const QString &entryPath, const QSize &scaledTo = QSize(),
                         QString *error = nullptr,
                         LoadErrorCategory *category = nullptr);
    // 条目原始字节（冒烟探针取证用；正常浏览走 decode）
    static QByteArray readEntry(const QString &entryPath, QString *error = nullptr);
    static QByteArray readEntry(const QString &entryPath, QString *error,
                                ZipArchive::ReadStatus *status);
    // 条目未压缩大小（信息浮层显示真实字节数）；失败返回 -1
    static qint64 entryBytes(const QString &entryPath);
    // 条目的修改时间（zip 记的是 DOS 本地时间）；非条目 / 无记录返回无效值
    static QDateTime entryModified(const QString &entryPath);

    // 当前打开的包数量上限：超过就丢最久未用的（正在浏览的包永不丢）
    static constexpr int kMaxOpenArchives = 3;

private:
    static QString normalized(const QString &path);
    static QSharedPointer<ZipArchive> lookup(const QString &entryPath, QString *entryName);

    static QMutex s_mutex;
    static QHash<QString, QSharedPointer<ZipArchive>> s_archives;   // 归一化包路径 → 已解析的包
    static QString s_active;                                       // 当前浏览的包
};
