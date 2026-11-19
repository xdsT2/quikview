#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// RAW + JPG 分组显示（BandiView §2.7 / H1-5）：目录列表构建层的纯函数。
// ---------------------------------------------------------------------------
// 输入是已经按当前排序规则排好的同目录文件绝对路径；输出是「每条 = 一个
// 胶片栏/翻页条目」的组表。本模块只做字符串与路径判断，不碰 QFileInfo、
// 不碰配置、不碰 UI，因此可以直接在后台线程调用。
//
// 组规则（派单 派单-RAW与JPG分组显示-20261118 §二）：
//   - 同目录、同主名（completeBaseName，大小写不敏感）；
//   - 一侧是「原始组」：RAW 组（dng/nef/cr2/arw/rw2/orf/raf/pef/raw，即
//     DirectoryLoader 的 kCameraExts 去掉 heic/heif/hif/avif）外加 PSD
//     （BandiView 的 PSD+JPG 分组，CLIP 不做）；
//   - 另一侧是「JPG 组」：jpg/jpeg/jfif；
//   - 两侧都有才成组：只有 RAW、只有 JPG、只有 PSD 都按普通单条处理；
//   - 排序位次取组内 JPG 的排序键位置；没有 JPG 的组（不会发生，见上）退
//     回原始侧位置。
//
// 显示优先：默认展示 JPG（浏览快）；showRaw 打开时展示原始侧。
namespace RawJpgGroup {

struct Entry
{
    QString jpg;          // JPG 侧成员（jpg/jpeg/jfif 中排序最靠前的一张）
    QString raw;          // 原始侧成员（RAW/PSD 中排序最靠前的一张）
    QStringList members;  // 组内全部文件（保持输入顺序）
    bool grouped = false; // false = 普通单条（不参与显示切换）

    // 当前模式真正要打开的路径：成组时按 showRaw 选侧，单条时就是自己。
    QString displayPath(bool showRaw) const;
    // 该条目是否包含 path（Windows 路径大小写不敏感）
    bool contains(const QString &path) const;
};

// 扩展名（不带点、全小写）归属判定；供扫描层的过滤与设置页文案共用。
bool isRawSideExt(const QString &suffixLower);
bool isJpgSideExt(const QString &suffixLower);

// 把排好序的文件表折叠成条目表。输出顺序 = 输入顺序（按首成员的位次），
// 不会重排，因此「关闭分组」时的顺序与现状逐位一致。
QList<Entry> build(const QStringList &sortedFiles, bool showRaw);

// 当前模式下每个条目的打开路径（长度与 entries 相同）。
QStringList displayFiles(const QList<Entry> &entries, bool showRaw);

// path 落在第几个条目；找不到返回 -1。锚点定位（打开某张图 → 页位）用它。
int indexOfPath(const QList<Entry> &entries, const QString &path);

} // namespace RawJpgGroup
