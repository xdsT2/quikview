#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// 统一「打开」选择器（2026-11-25 填充态跟随生命周期单 / 原生对话框替换单）。
// 纯函数部分（buildFilters / resolveFolderTarget）不依赖窗口与 COM，可在无环境下单测；
// Win 原生 IFileOpenDialog 实现在 Q_OS_WIN 分支内。
namespace OpenPicker {

struct FilterSpec {
    QString label;  // 下拉展示名
    QString spec;   // 通配串，形如 *.jpg;*.png
};

// 从通配表构造原生对话框首项筛选（图片 + 压缩包）。去重、保序；patterns 形如 "*.jpg"。
QVector<FilterSpec> buildFilters(const QStringList &patterns,
                                 const QStringList &archivePatterns = QStringList());

// 文件夹目标解析：选中项是目录 → 选中项绝对路径；选中项是文件 / 为空 → 回退当前目录。
QString resolveFolderTarget(const QString &selectedItem, const QString &currentFolder);

enum class Outcome { Cancelled, File, Folder };

struct Result {
    Outcome outcome = Outcome::Cancelled;
    QString file;    // 确认图片文件（File 态）
    QString folder;  // 确认打开的目录（Folder 态）
};

#ifdef Q_OS_WIN
// 阻塞弹出 Windows 原生 IFileOpenDialog（IFileDialogCustomize 加「打开文件夹」钮）。
// ownerHwnd 可空；startDir 为空则不预设目录。主线程 COM 已由 Qt 初始化，这里不再 CoInitialize。
// 取消码 HRESULT_FROM_WIN32(ERROR_CANCELLED) 按正常取消处理，不弹错。
Result showNativeDialog(void *ownerHwnd, const QString &startDir,
                        const QStringList &patterns, const QStringList &archivePatterns,
                        const QString &folderButtonTitle, const QString &windowTitle);
#endif

}  // namespace OpenPicker
