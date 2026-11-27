// QuikLoadMenuPrefs：定位 SoleView 的 config.ini 并读出菜单偏好。
// 定位顺序与 src/Config.cpp::defaultConfigPath 一致（文件名、候选目录与键名
// 全部来自 shared/sole_config_keys.h，壳扩展与主程序共用同一份字面量）：
//   1) 环境变量 SOLEVIEW_CONFIG_PATH（回退 QUIKVIEW_CONFIG_PATH，测试钩子）
//   2) <DLL 目录>\config.ini（便携模式：壳扩展 DLL 与 soleview.exe 同目录）
//   3) %LOCALAPPDATA%\SoleView\SoleView\config.ini（安装版默认；只读回退旧 QuikView 目录）
// 三处都不存在 → 返回全默认（总开关与各细项全开）。
#include "prefs.h"

#include <shlobj.h>
#include <stdio.h>

#include "sole_config_keys.h"
#include "quikthumb.h"   // DllGetModule（便携模式按 DLL 目录定位 config.ini）

namespace {

bool FileExistsW(const wchar_t* path) {
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool FindConfigIni(wchar_t* out, DWORD cch) {
    // 1) 测试钩子（优先 SOLEVIEW_CONFIG_PATH，兼容 QUIKVIEW_CONFIG_PATH）
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(solecfg::kConfigPathEnvVar, env, MAX_PATH) > 0 &&
        FileExistsW(env)) {
        lstrcpynW(out, env, (int)cch);
        return true;
    }
    if (GetEnvironmentVariableW(solecfg::kLegacyConfigPathEnvVar, env, MAX_PATH) > 0 &&
        FileExistsW(env)) {
        lstrcpynW(out, env, (int)cch);
        return true;
    }
    // 2) 便携模式（DLL 旁边的 config.ini）
    wchar_t module[MAX_PATH] = {};
    if (DllGetModule() &&
        GetModuleFileNameW(DllGetModule(), module, MAX_PATH) > 0) {
        const std::wstring portable = solecfg::PortableConfigPath(module);
        if (FileExistsW(portable.c_str())) {
            lstrcpynW(out, portable.c_str(), (int)cch);
            return true;
        }
    }
    // 3) 安装版默认配置目录（SoleView 优先，只读回退旧 QuikView 目录）
    wchar_t appdata[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, appdata)) &&
        appdata[0]) {
        const std::wstring installed = solecfg::InstallConfigPath(appdata);
        if (FileExistsW(installed.c_str())) {
            lstrcpynW(out, installed.c_str(), (int)cch);
            return true;
        }
        const std::wstring legacy = solecfg::LegacyInstallConfigPath(appdata);
        if (FileExistsW(legacy.c_str())) {
            lstrcpynW(out, legacy.c_str(), (int)cch);
            return true;
        }
    }
    return false;
}

// 单键读取：QSettings IniFormat 的转义写法（'\'）优先，'/' 原样兜底；
// 缺键返回 true（默认开）。值解析接受 true/false/1/0/on/off，其余按开。
bool ReadBoolKey(const wchar_t* ini, quikcfg::CtxLeaf leaf) {
    wchar_t key[96] = {};
    wchar_t val[16] = {};
    const std::wstring backslash = quikcfg::CtxIniKeyBackslash(leaf);
    lstrcpynW(key, backslash.c_str(), _countof(key));
    GetPrivateProfileStringW(quikcfg::kCtxIniSection, key, L"", val,
                             (DWORD)(_countof(val) - 1), ini);
    if (!val[0]) {
        const std::wstring slash = quikcfg::CtxIniKeySlash(leaf);
        lstrcpynW(key, slash.c_str(), _countof(key));
        GetPrivateProfileStringW(quikcfg::kCtxIniSection, key, L"", val,
                                 (DWORD)(_countof(val) - 1), ini);
    }
    if (!val[0]) return true;
    return lstrcmpiW(val, L"false") != 0 && lstrcmpiW(val, L"0") != 0 &&
           lstrcmpiW(val, L"off") != 0;
}

}   // namespace

QuikMenuPrefs QuikLoadMenuPrefs() {
    QuikMenuPrefs p = { true, true, true, true, true, true };
    wchar_t ini[MAX_PATH] = {};
    if (!FindConfigIni(ini, MAX_PATH)) return p;
    p.master  = ReadBoolKey(ini, quikcfg::CtxUseShellMenu);
    p.browse  = ReadBoolKey(ini, quikcfg::CtxShowBrowse);
    p.convert = ReadBoolKey(ini, quikcfg::CtxShowConvert);
    p.preview = ReadBoolKey(ini, quikcfg::CtxShowPreview);
    p.info    = ReadBoolKey(ini, quikcfg::CtxShowInfo);
    p.icon    = ReadBoolKey(ini, quikcfg::CtxShowIcon);
    return p;
}
