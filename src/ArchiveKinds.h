#pragma once

#include <QString>
#include <QStringList>
#include <QStringView>

// 「可当图片容器打开的压缩格式」唯一判定表。
// 为什么单独抽出来：ArchiveStore 的热路径判据、文件对话框过滤器、单元测试
// 必须共用同一份表，否则加一个后缀要改三处、漏一处就出现「能拖进来但对话框
// 看不见」这类不一致。
namespace ArchiveKinds {

// 返回全部可打开容器后缀（小写、不含点），顺序固定，便于过滤器拼接。
// 只收「本质是 zip、且内嵌图片有意义」的格式；doc/pdf 这类非 zip 容器不在内。
inline QStringList openableSuffixes()
{
    // 函数内 static：第一次调用时构造一次，之后按隐式共享复制。isArchiveFile
    // 在目录扫描里是逐文件调用的，不能让每次判定都重新拼十几个 QString。
    static const QStringList kSuffixes = {
        // 漫画 / 通用包
        QStringLiteral("zip"),  QStringLiteral("cbz"),
        // Office OOXML：docm/dotx、pptm/potx、xlsm/xltx 同样只是换了扩展名的 zip
        QStringLiteral("docx"), QStringLiteral("docm"), QStringLiteral("dotx"),
        QStringLiteral("pptx"), QStringLiteral("pptm"), QStringLiteral("potx"),
        QStringLiteral("xlsx"), QStringLiteral("xlsm"), QStringLiteral("xltx"),
        // Office ODF
        QStringLiteral("odt"),  QStringLiteral("ods"),  QStringLiteral("odp"),
        // 电子书
        QStringLiteral("epub"),
        // 安装包 / 打包产物（apk、jar 本身就是 zip，内含 drawable 图片）
        QStringLiteral("apk"),  QStringLiteral("jar"),
    };
    return kSuffixes;
}

// 内部用：把 "*.docx" / " .Docx" 这类写法统一成 "docx"。
// 调用方来自拖放、命令行、文件对话框过滤器三处，写法各异，容错必须集中在一处。
inline QString normalizeSuffix(const QString &suffix)
{
    QString norm = suffix.trimmed();
    while (norm.startsWith(QLatin1Char('*'))) {
        norm.remove(0, 1);
    }
    while (norm.startsWith(QLatin1Char('.'))) {
        norm.remove(0, 1);
    }
    return norm.toLower();
}

// 是否可当容器打开。suffix 允许带点或带 '*' 前缀，大小写不敏感。
inline bool isOpenableSuffix(const QString &suffix)
{
    const QString norm = normalizeSuffix(suffix);
    if (norm.isEmpty()) {
        return false;
    }
    return openableSuffixes().contains(norm);
}

// 热路径版本：调用方已按分隔符切出裸后缀，这里不做 normalize、不构造 QString。
// ArchiveStore::looksVirtual 每张图要调若干次，拼 ".<后缀>/" 再 contains 会产生
// 三十多个临时字符串，所以单独留一条零分配的比较通道。
inline bool isOpenableSuffixView(QStringView suffix)
{
    if (suffix.isEmpty()) {
        return false;
    }
    const QStringList table = openableSuffixes();
    for (const QString &known : table) {
        if (suffix.compare(QStringView(known), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

// 是否是「文档/安装包」类容器（非漫画包）：仅用于将来做提示区分，本任务不消费。
// 未知后缀返回 false——这里只回答「是哪种容器」，不回答「是不是文件」。
inline bool isDocumentContainer(const QString &suffix)
{
    const QString norm = normalizeSuffix(suffix);
    if (!isOpenableSuffix(norm)) {
        return false;
    }
    return norm != QLatin1String("zip") && norm != QLatin1String("cbz");
}

} // namespace ArchiveKinds
