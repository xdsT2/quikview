#ifndef QUIK_CONFIG_KEYS_H
#define QUIK_CONFIG_KEYS_H
// QuikView 配置读取单一来源（主程序与壳扩展共用）。
//
// 约束：
//   * 纯 C++17 + Win32 字面量，**不依赖 Qt**（壳扩展不能拉 Qt，见 shell-ext/README.md）；
//   * 只放「文件名 / 目录候选 / 键名」这类常量与拼装函数，不放读取逻辑，也不改默认值；
//   * 主程序侧用 QString::fromWCharArray 转换，壳扩展侧直接用 std::wstring / const wchar_t*。
//
// 改动这里等于同时改主程序（QSettings）与壳扩展（GetPrivateProfileStringW）的行为，
// 改键名前先确认旧 config.ini 的兼容性。

#include <string>

namespace quikcfg {

// —— 配置文件与定位 ——

// 配置文件名（便携模式与安装模式同名）。
inline constexpr const wchar_t* kConfigFileName = L"config.ini";

// 测试/冒烟隔离钩子：指向一份临时 config.ini，优先级最高。
// 宽/窄两份字面量：壳扩展用 GetEnvironmentVariableW，主程序 qgetenv 要窄字符串。
inline constexpr const wchar_t* kConfigPathEnvVar = L"QUIKVIEW_CONFIG_PATH";
inline constexpr const char* kConfigPathEnvVarA = "QUIKVIEW_CONFIG_PATH";

// 安装模式目录：%LOCALAPPDATA%\QuikView\QuikView（相对 LOCALAPPDATA 的两级，
// 分别对应 Qt 的 organizationName / applicationName：QuikView/QuikView）。
inline constexpr const wchar_t* kInstallConfigOrgDir = L"QuikView";
inline constexpr const wchar_t* kInstallConfigAppDir = L"QuikView";

// 便携模式：<模块所在目录>\config.ini。modulePath 传 exe 或 DLL 的完整路径
// （主程序传 applicationDirPath + 文件名，壳扩展传 GetModuleFileNameW 结果）。
inline std::wstring PortableConfigPath(const std::wstring& modulePath) {
    const size_t slash = modulePath.find_last_of(L"\\/");
    std::wstring dir = (slash == std::wstring::npos) ? std::wstring()
                                                     : modulePath.substr(0, slash);
    if (dir.empty()) return std::wstring(kConfigFileName);
    return dir + L"\\" + kConfigFileName;
}

// 安装模式：<localAppData>\QuikView\QuikView\config.ini。
inline std::wstring InstallConfigPath(const std::wstring& localAppData) {
    std::wstring p = localAppData;
    if (p.empty()) return std::wstring(kConfigFileName);
    if (p.back() == L'\\' || p.back() == L'/') p.pop_back();
    return p + L"\\" + kInstallConfigOrgDir + L"\\" + kInstallConfigAppDir + L"\\" +
           kConfigFileName;
}

// —— 资源管理器右键菜单开关 ——
// 文件布局：[config] 段下的 ctxmenu\<leaf>（QSettings 视图里就是
// config/ctxmenu/<leaf>）。六个 leaf 的历史顺序不要动：设置页顺序与
// 迁移逻辑都按它。

inline constexpr const wchar_t* kCtxIniSection = L"config";
inline constexpr const wchar_t* kCtxGroup = L"ctxmenu";

enum CtxLeaf {
    CtxUseShellMenu = 0,   // 总开关：右键菜单里是否出现 QuikView
    CtxShowBrowse,         // 「用 QuikView 浏览」
    CtxShowConvert,        // 「用 QuikView 转换」
    CtxShowPreview,        // 菜单内自绘缩略图
    CtxShowInfo,           // 浏览项右侧「宽×高 · 大小」
    CtxShowIcon,           // 菜单项软件图标
    CtxLeafCount
};

inline const wchar_t* CtxLeafName(CtxLeaf leaf) {
    static const wchar_t* const kNames[CtxLeafCount] = {
        L"use_shell_menu", L"show_browse", L"show_convert",
        L"show_preview",   L"show_info",   L"show_icon",
    };
    const int i = static_cast<int>(leaf);
    return (i >= 0 && i < CtxLeafCount) ? kNames[i] : L"";
}

// 壳扩展读 ini 用：ctxmenu\use_shell_menu（QSettings 写盘的原样转义形式）。
inline std::wstring CtxIniKeyBackslash(CtxLeaf leaf) {
    return std::wstring(kCtxGroup) + L"\\" + CtxLeafName(leaf);
}

// 兼容手写 ini 的 ctxmenu/use_shell_menu 形式（壳扩展回退查找用）。
inline std::wstring CtxIniKeySlash(CtxLeaf leaf) {
    return std::wstring(kCtxGroup) + L"/" + CtxLeafName(leaf);
}

// QSettings 相对键（设置页 writeValues 自己拼 "config/" 前缀时用）：
// ctxmenu/use_shell_menu。
inline std::wstring CtxSettingsSubKey(CtxLeaf leaf) {
    return std::wstring(kCtxGroup) + L"/" + CtxLeafName(leaf);
}

// QSettings 完整键：config/ctxmenu/use_shell_menu。
inline std::wstring CtxSettingsKey(CtxLeaf leaf) {
    return std::wstring(kCtxIniSection) + L"/" + kCtxGroup + L"/" + CtxLeafName(leaf);
}

}   // namespace quikcfg

#endif   // QUIK_CONFIG_KEYS_H
