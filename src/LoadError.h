#pragma once

#include <QMetaType>

// 解码失败类别（派单《格式兼容诊断提示》2026-11-18）。
//
// 判定一律落在 loader 层（PsdLoader 的错误分支、ImageCache 的魔数白名单），
// UI 层只按类别取文案——禁止在 UI 上对错误字符串做 contains/startsWith 匹配。
enum class LoadErrorCategory {
    None,             // 不是失败（正常）
    Corrupt,          // 头部合法但图像数据读取失败：截断、坏块、I/O 失败
    Unsupported,      // 魔数不匹配，或不在支持白名单里
    NoCompatPreview,  // PSD/PSB 专属：结构可解析，但内嵌合成预览缺失
};

Q_DECLARE_METATYPE(LoadErrorCategory)
