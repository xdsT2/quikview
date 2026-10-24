#include "DirectoryLoader.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>

#include <algorithm>

QStringList DirectoryLoader::imageExtensions()
{
    // Qt6 支持的常见格式（qt6-base + qt6-imageformats 插件）
    return {
        QStringLiteral("*.png"), QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
        QStringLiteral("*.bmp"), QStringLiteral("*.gif"), QStringLiteral("*.webp"),
        QStringLiteral("*.heic"), QStringLiteral("*.avif"),
        QStringLiteral("*.tif"), QStringLiteral("*.tiff"), QStringLiteral("*.ico"),
        QStringLiteral("*.ppm"), QStringLiteral("*.pgm"), QStringLiteral("*.pbm"),
        QStringLiteral("*.xbm"), QStringLiteral("*.xpm"),
        QStringLiteral("*.dng"), QStringLiteral("*.nef"), QStringLiteral("*.cr2"),
        QStringLiteral("*.arw"), QStringLiteral("*.rw2"), QStringLiteral("*.orf"),
        QStringLiteral("*.raf"), QStringLiteral("*.pef"), QStringLiteral("*.raw"),
    };
}

bool DirectoryLoader::open(const QString &filePath)
{
    const QFileInfo fi(filePath);
    if (!fi.exists()) {
        return false;
    }
    const QDir dir = fi.dir();
    QFileInfoList infos = dir.entryInfoList(imageExtensions(),
                                             QDir::Files | QDir::Readable);

    // 排序：键（名称/时间/大小/类型）+ 方向（升/降），对照原版排序方式 4+1 项。
    // 名称用 QCollator（LocaleAware）；类型 = 扩展名分组、组内按名称。
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    const bool asc = !m_sortDesc;
    std::sort(infos.begin(), infos.end(),
              [this, &collator, asc](const QFileInfo &a, const QFileInfo &b) {
        int r = 0;
        switch (m_sort) {
        case 1:   // 修改时间
            r = (a.lastModified() == b.lastModified()) ? 0
                : (a.lastModified() < b.lastModified() ? -1 : 1);
            break;
        case 2:   // 大小
            r = (a.size() == b.size()) ? 0 : (a.size() < b.size() ? -1 : 1);
            break;
        case 3: { // 类型：扩展名分组，组内名称
            r = a.suffix().compare(b.suffix(), Qt::CaseInsensitive);
            if (r == 0) {
                r = collator.compare(a.fileName(), b.fileName());
            }
            break;
        }
        default:  // 名称
            r = collator.compare(a.fileName(), b.fileName());
            break;
        }
        return asc ? (r < 0) : (r > 0);
    });

    m_files.clear();
    m_files.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        m_files << info.absoluteFilePath();
    }

    const QString canonical = QFileInfo(filePath).absoluteFilePath();
    m_index = m_files.indexOf(canonical);
    return m_index >= 0;
}

QString DirectoryLoader::current() const
{
    return isValid() ? m_files.at(m_index) : QString();
}

QString DirectoryLoader::next()
{
    if (m_files.isEmpty()) {
        return QString();
    }
    if (m_edgeAction == QLatin1String("stop") && m_index >= m_files.size() - 1) {
        return QString();
    }
    m_index = (m_index + 1) % m_files.size();
    return m_files.at(m_index);
}

// 一次走 step 张：双页阅览一跨页是 2 张，普通浏览是 1 张。
// 到边界按 edgeAction 处理——stop 原地不动并返回空串（调用方据此提示），
// first 回绕到另一端（保留原有循环语义）。
QString DirectoryLoader::advance(int step)
{
    if (m_files.isEmpty() || step == 0) {
        return QString();
    }
    const int target = m_index + step;
    if (target < 0 || target >= m_files.size()) {
        if (m_edgeAction != QLatin1String("first")) {
            return QString();
        }
        m_index = target < 0 ? m_files.size() - 1 : 0;
    } else {
        m_index = target;
    }
    return m_files.at(m_index);
}

QString DirectoryLoader::prev()
{
    if (m_files.isEmpty()) {        return QString();
    }
    if (m_edgeAction == QLatin1String("stop") && m_index <= 0) {
        return QString();
    }
    m_index = (m_index - 1 + m_files.size()) % m_files.size();
    return m_files.at(m_index);
}

// 10 张跳转：边界 clamp 停住（原版 CMD_PREV10/NEXT10 语义），不回绕
QString DirectoryLoader::next10()
{
    if (m_files.isEmpty() || m_index >= m_files.size() - 1) {
        return QString();
    }
    m_index = qMin(m_index + 10, m_files.size() - 1);
    return m_files.at(m_index);
}

QString DirectoryLoader::prev10()
{
    if (m_files.isEmpty() || m_index <= 0) {
        return QString();
    }
    m_index = qMax(m_index - 10, 0);
    return m_files.at(m_index);
}
