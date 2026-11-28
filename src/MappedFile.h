#pragma once

// 需求池 D3-2「内存映射 + 自有内存池」——本轮只做取数层 MMF。
//
// 为什么需要它：重格式解码（PSD/dds/exr/jxl/qoi/hdr…）此前一律先 f.read()
// 把最大 256MB 的文件整体复制进 QByteArray 堆缓冲，然后再从这份副本解码。
// 大文件会因此多吃一整份内存（峰值 = 文件大小 × 2 左右）。
// 这里对小文件保持整读（避免映射的系统调用开销与页错误成本），
// 大文件走 Windows 文件映射，解码器直接拿只读视图，不再复制。
//
// 为什么不用 QFile::map：Qt 的映射虽然底层同样是文件映射，但视图生命周期
// 绑在 QFile 对象上，回退到整读、以及"先复位再映射"的控制点都比较绕；
// 直接用 Win32 三段式（CreateFile / CreateFileMapping / MapViewOfFile）
// 生命周期一目了然，析构顺序可控。
//
// 一致性说明：共享模式允许 FILE_SHARE_WRITE，解码期间如果别的进程截断文件，
// 访问已映射但已越界的页会触发访问违例。原整读实现同样存在"读到半新半旧"
// 的问题，这里不额外加锁，风险等级与改造前一致。

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <climits>

#include "LoadError.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

class MappedFile
{
public:
    // 低于该阈值直接整读：8MB 以下文件的映射收益抵不过建映射 + 页错误开销。
    static constexpr qint64 kMinMapBytes = 8LL * 1024 * 1024;

    MappedFile() = default;

    ~MappedFile()
    {
        reset();
    }

    MappedFile(const MappedFile &) = delete;
    MappedFile &operator=(const MappedFile &) = delete;

    // 打开并映射。成功返回 true；mapped() 表示是否真的走了映射。
    // 失败时的错误文案与类别与旧 readFileBytes 完全一致（调用方文案不变）。
    bool open(const QString &path, qint64 maxBytes, QString *errorOut,
              LoadErrorCategory *categoryOut)
    {
        reset();

        // 只有 Windows 且文件足够大才尝试映射；其余情况静默落到整读。
        if (maxBytes > 0) {
            if (tryMap(path, maxBytes)) {
                return true;
            }
        }
        return readWhole(path, maxBytes, errorOut, categoryOut);
    }

    bool isMapped() const { return m_mapped; }

    // 映射时是 fromRawData 视图（指向 MapViewOfFile 返回的地址），
    // 回退时是自有 QByteArray。两者都只读，调用方不得写入。
    const QByteArray &bytes() const { return m_view; }

    bool isEmpty() const { return m_view.isEmpty(); }

    qint64 size() const { return qint64(m_view.size()); }

private:
    // 释放顺序：先断掉引用映射内存的视图，再 UnmapViewOfFile，最后关句柄。
    // 反过来的话 m_view 会短暂指向未映射地址，任何残留引用都可能崩。
    void reset()
    {
        m_view.clear();
        m_owned.clear();
        m_mapped = false;
#ifdef Q_OS_WIN
        if (m_address) {
            UnmapViewOfFile(m_address);
            m_address = nullptr;
        }
        if (m_mapping) {
            CloseHandle(m_mapping);
            m_mapping = nullptr;
        }
        if (m_file) {
            CloseHandle(m_file);
            m_file = nullptr;
        }
#endif
    }

    // 整读回退：与旧 readFileBytes 逐行等价，错误文案/类别保持一致。
    bool readWhole(const QString &path, qint64 maxBytes, QString *errorOut,
                   LoadErrorCategory *categoryOut)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            setError(QStringLiteral("file open failed"), LoadErrorCategory::Corrupt, errorOut,
                     categoryOut);
            return false;
        }
        if (f.size() > maxBytes) {
            setError(QStringLiteral("file exceeds size limit"), LoadErrorCategory::Corrupt,
                     errorOut, categoryOut);
            return false;
        }
        m_owned = f.read(maxBytes);
        if (m_owned.isEmpty()) {
            setError(QStringLiteral("file is empty"), LoadErrorCategory::Corrupt, errorOut,
                     categoryOut);
            return false;
        }
        // 共享 QByteArray 数据区，拷贝很便宜；bytes() 只需返回一个成员。
        m_view = m_owned;
        return true;
    }

    // 尝试映射。返回 false 时不做任何错误上报（交给整读路径给出统一文案），
    // 并且保证已释放拿到一半的资源。
    bool tryMap(const QString &path, qint64 maxBytes)
    {
#ifdef Q_OS_WIN
        if (maxBytes < kMinMapBytes) {
            return false;
        }

        const HANDLE hFile = CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()),
                                        GENERIC_READ,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        LARGE_INTEGER li{};
        if (!GetFileSizeEx(hFile, &li) || li.QuadPart <= 0 || li.QuadPart > maxBytes
            || li.QuadPart < kMinMapBytes || li.QuadPart > qint64(INT_MAX)) {
            // 空文件 / 超限 / 太小 / 超出 QByteArray 的 int 容量上限：都交给整读，
            // 由整读路径产出 "file is empty" / "file exceeds size limit" 等原文案。
            CloseHandle(hFile);
            return false;
        }

        const HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap) {
            CloseHandle(hFile);
            return false;
        }

        void *addr = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
        if (!addr) {
            CloseHandle(hMap); // 映射失败：MapViewOfFile 没有产出视图，不需要 Unmap
            CloseHandle(hFile);
            return false;
        }

        // 句柄在视图存在期间必须保持打开，所以这里保存下来，析构时统一释放。
        m_file = hFile;
        m_mapping = hMap;
        m_address = addr;
        m_view = QByteArray::fromRawData(static_cast<const char *>(addr), int(li.QuadPart));
        m_mapped = true;
        return true;
#else
        Q_UNUSED(path);
        Q_UNUSED(maxBytes);
        return false;
#endif
    }

    void setError(const QString &msg, LoadErrorCategory cat, QString *errOut,
                  LoadErrorCategory *catOut)
    {
        if (errOut) {
            *errOut = msg;
        }
        if (catOut) {
            *catOut = cat;
        }
    }

    QByteArray m_owned;
    QByteArray m_view;
    bool m_mapped = false;
#ifdef Q_OS_WIN
    void *m_file = nullptr;    // HANDLE
    void *m_mapping = nullptr; // HANDLE
    void *m_address = nullptr; // 视图基址
#endif
};
