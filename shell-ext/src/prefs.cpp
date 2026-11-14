// QuikLoadMenuPrefs：定位 QuikView 的 config.ini 并读出菜单偏好。
// 定位顺序与 src/Config.cpp::defaultConfigPath 一致：
//   1) 环境变量 QUIKVIEW_CONFIG_PATH（测试钩子，资源管理器进程通常没有）
//   2) <DLL 目录>\config.ini（便携模式：壳扩展 DLL 与 quikview.exe 同目录）
//   3) %LOCALAPPDATA%\QuikView\QuikView\config.ini（安装版默认）
// 三处都不存在 → 返回全默认（总开关与各细项全开）。
#include "prefs.h"

#include <shlobj.h>
#include <stdio.h>

#include "quikthumb.h"   // DllGetModule（便携模式按 DLL 目录定位 config.ini）

namespace {

bool FileExistsW(const wchar_t* path) {
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool FindConfigIni(wchar_t* out, DWORD cch) {
    // 1) 测试钩子
    wchar_t env[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"QUIKVIEW_CONFIG_PATH", env, MAX_PATH) > 0 &&
        FileExistsW(env)) {
        lstrcpynW(out, env, (int)cch);
        return true;
    }
    // 2) 便携模式（DLL 旁边的 config.ini）
    wchar_t buf[MAX_PATH] = {};
    if (DllGetModule() &&
        GetModuleFileNameW(DllGetModule(), buf, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) {
            lstrcpynW(slash + 1, L"config.ini",
                      (int)(MAX_PATH - (slash + 1 - buf)));
            if (FileExistsW(buf)) {
                lstrcpynW(out, buf, (int)cch);
                return true;
            }
        }
    }
    // 3) 安装版默认配置目录
    wchar_t appdata[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
                                   SHGFP_TYPE_CURRENT, appdata)) &&
        appdata[0]) {
        wchar_t cand[MAX_PATH + 64] = {};
        wsprintfW(cand, L"%s\\QuikView\\QuikView\\config.ini", appdata);
        if (FileExistsW(cand)) {
            lstrcpynW(out, cand, (int)cch);
            return true;
        }
    }
    return false;
}

// 单键读取：QSettings IniFormat 的转义写法（'\'）优先，'/' 原样兜底；
// 缺键返回 true（默认开）。值解析接受 true/false/1/0/on/off，其余按开。
bool ReadBoolKey(const wchar_t* ini, const wchar_t* name) {
    wchar_t key[96] = {};
    wchar_t val[16] = {};
    wsprintfW(key, L"ctxmenu\\%s", name);
    GetPrivateProfileStringW(L"config", key, L"", val,
                             (DWORD)(_countof(val) - 1), ini);
    if (!val[0]) {
        wsprintfW(key, L"ctxmenu/%s", name);
        GetPrivateProfileStringW(L"config", key, L"", val,
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
    p.master  = ReadBoolKey(ini, L"use_shell_menu");
    p.browse  = ReadBoolKey(ini, L"show_browse");
    p.convert = ReadBoolKey(ini, L"show_convert");
    p.preview = ReadBoolKey(ini, L"show_preview");
    p.info    = ReadBoolKey(ini, L"show_info");
    p.icon    = ReadBoolKey(ini, L"show_icon");
    return p;
}
