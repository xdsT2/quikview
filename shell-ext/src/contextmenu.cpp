// QuikContextMenu 实现：IShellExtInit + IContextMenu（交办单 D2 阶段 1）。
// 注册于 HKLM\Software\Classes\*\shellex\ContextMenuHandlers\QuikView（见脚本）。
// 菜单两项：「用 QuikView 浏览」= quikview.exe <path>；
//          「用 QuikView 转换」= quikview.exe --batch <path>。
// 多选策略：只处理第一个选中文件（交办单允许二选一，README 有说明）；
// 第一个文件扩展名不在支持列表内时不注入菜单项。
// 启动走 CreateProcessW（kernel32），失败一律返回失败 HRESULT 静默失败——
// 绝不让异常穿出 DLL（交办单硬约束 1）。
#include "contextmenu.h"

#include <ole2.h>      // ReleaseStgMedium（WIN32_LEAN_AND_MEAN 不自动引入）
#include <shellapi.h>  // CF_HDROP
#include <shlobj.h>    // DROPFILES（MinGW 在此，MSVC 在 shellapi.h）

#include "quikthumb.h"

namespace {

// 菜单命令 ID 偏移与动词名（InvokeCommand / GetCommandString 共用）
enum { CMD_BROWSE = 0, CMD_CONVERT = 1, CMD_COUNT = 2 };
const wchar_t kVerbBrowseW[] = L"QuikView.Browse";
const wchar_t kVerbConvertW[] = L"QuikView.Convert";
const char kVerbBrowseA[] = "QuikView.Browse";
const char kVerbConvertA[] = "QuikView.Convert";

const wchar_t kMenuBrowse[] = L"用 QuikView 浏览";
const wchar_t kMenuConvert[] = L"用 QuikView 转换";

// 注册表：安装器（packaging/quikview.nsi）写 HKLM\Software\QuikView\InstallDir；
// 交办单口径的 InstallPath 作为预留别名。读取固定走 64 位视图，安装器只写一处。
const wchar_t kAppRegKey[] = L"Software\\QuikView";

bool FileExists(const wchar_t* path) {
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 读安装目录（InstallDir 优先，InstallPath 兜底），root 传 HKCU 或 HKLM
bool GetInstallDirFromRegistry(HKEY root, wchar_t* dir, DWORD cch) {
    HKEY hk = nullptr;
    // KEY_WOW64_64KEY：32 位进程也读 64 位视图（安装器只写一处）
    if (RegOpenKeyExW(root, kAppRegKey, 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS) {
        return false;
    }
    static const wchar_t* const kValueNames[] = { L"InstallDir",
                                                  L"InstallPath" };
    bool ok = false;
    for (const wchar_t* name : kValueNames) {
        wchar_t buf[MAX_PATH] = {};
        DWORD type = 0;
        DWORD cb = sizeof(buf);
        if (RegQueryValueExW(hk, name, nullptr, &type, (BYTE*)buf, &cb) ==
                ERROR_SUCCESS &&
            type == REG_SZ && buf[0]) {
            lstrcpynW(dir, buf, (int)cch);
            ok = true;
            break;
        }
    }
    RegCloseKey(hk);
    return ok;
}

// 取路径扩展名（含点）；点属于目录名（如 "D:\v1.2\"）时不算
const wchar_t* FindExt(const wchar_t* path) {
    const wchar_t* dot = nullptr;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'.') {
            dot = p;
        } else if (*p == L'\\' || *p == L'/') {
            dot = nullptr;
        }
    }
    return dot;
}

bool HasSupportedExt(const wchar_t* path) {
    const wchar_t* ext = FindExt(path);
    if (!ext) return false;
    for (size_t i = 0; i < QuikSupportedExtCount(); ++i) {
        if (lstrcmpiW(ext, QuikSupportedExt(i)) == 0) return true;
    }
    return false;
}

}   // namespace

QuikContextMenu::QuikContextMenu() : m_ref(1), m_hasFile(false) {
    m_file[0] = L'\0';
    DllAddLock();
}

QuikContextMenu::~QuikContextMenu() {
    DllReleaseLock();
}

// IUnknown
STDMETHODIMP QuikContextMenu::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown)) {
            // 双基类下直接转 IUnknown 有歧义；经首基类即得 IUnknown 子对象
            *ppv = static_cast<IShellExtInit*>(this);
        } else if (IsEqualIID(riid, IID_IShellExtInit)) {
            *ppv = static_cast<IShellExtInit*>(this);
        } else if (IsEqualIID(riid, IID_IContextMenu)) {
            *ppv = static_cast<IContextMenu*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

STDMETHODIMP_(ULONG) QuikContextMenu::AddRef() { return ++m_ref; }

STDMETHODIMP_(ULONG) QuikContextMenu::Release() {
    const ULONG ref = --m_ref;
    if (ref == 0) delete this;
    return ref;
}

// IShellExtInit：从数据对象取 CF_HDROP，只保留第一个文件（多选策略见 README）
STDMETHODIMP QuikContextMenu::Initialize(LPCITEMIDLIST, IDataObject* pdtobj,
                                         HKEY) {
    try {
        if (!pdtobj) return E_INVALIDARG;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stm = {};
        if (FAILED(pdtobj->GetData(&fe, &stm))) return E_FAIL;

        bool ok = false;
        if (HGLOBAL hg = stm.hGlobal) {
            if (const DROPFILES* df = (const DROPFILES*)GlobalLock(hg)) {
                const char* base = (const char*)df + df->pFiles;
                if (df->fWide) {
                    const wchar_t* files = (const wchar_t*)base;
                    if (files[0]) {
                        lstrcpynW(m_file, files, MAX_PATH);
                        ok = true;
                    }
                } else {
                    // ANSI 列表兜底（资源管理器实际总是 fWide=1）
                    const int n = MultiByteToWideChar(CP_ACP, 0, base, -1,
                                                      m_file, MAX_PATH);
                    ok = n > 0 && m_file[0];
                }
                GlobalUnlock(hg);
            }
        }
        ReleaseStgMedium(&stm);
        m_hasFile = ok;
        return ok ? S_OK : E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

// IContextMenu：注入分隔线 + 两个菜单项；返回值 = 消耗的命令 ID 数
STDMETHODIMP QuikContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                               UINT idCmdFirst, UINT idCmdLast,
                                               UINT uFlags) {
    try {
        if (!hmenu) return E_INVALIDARG;
        // 默认动词模式（双击等）不注入；没有文件或第一个文件不受支持也不注入
        if ((uFlags & CMF_DEFAULTONLY) || !m_hasFile ||
            !HasSupportedExt(m_file)) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }
        if (idCmdFirst + CMD_COUNT > idCmdLast) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);   // ID 空间不足：让位
        }

        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_FTYPE;
        mi.fType = MFT_SEPARATOR;
        if (!InsertMenuItemW(hmenu, indexMenu, TRUE, &mi)) return E_FAIL;

        const wchar_t* const kItems[CMD_COUNT] = { kMenuBrowse, kMenuConvert };
        for (UINT i = 0; i < CMD_COUNT; ++i) {
            mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING;
            mi.fType = MFT_STRING;
            mi.fState = MFS_ENABLED;
            mi.wID = idCmdFirst + i;
            mi.dwTypeData = const_cast<LPWSTR>(kItems[i]);
            mi.cch = (UINT)lstrlenW(kItems[i]);
            if (!InsertMenuItemW(hmenu, indexMenu + 1 + i, TRUE, &mi)) {
                return E_FAIL;
            }
        }
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, CMD_COUNT);
    } catch (...) {
        return E_FAIL;
    }
}

// IContextMenu：按序号（MAKEINTRESOURCE）或动词字符串执行命令。
// 注意 MAKEINTRESOURCEA(0) == nullptr：Explorer 调第一个命令传来的 lpVerb
// 就是空指针，不能当"没给动词"处理。
STDMETHODIMP QuikContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici) {
    try {
        if (!pici) return E_INVALIDARG;
        int cmd = -1;
        // 1) Unicode 字符串动词优先（CMINVOKECOMMANDINFOEX + CMIC_MASK_UNICODE）
        const wchar_t* verbW = nullptr;
        if (pici->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) &&
            (pici->fMask & CMIC_MASK_UNICODE)) {
            verbW = ((LPCMINVOKECOMMANDINFOEX)pici)->lpVerbW;
        }
        if (verbW && !IS_INTRESOURCE(verbW)) {
            if (lstrcmpiW(verbW, kVerbBrowseW) == 0) cmd = CMD_BROWSE;
            else if (lstrcmpiW(verbW, kVerbConvertW) == 0) cmd = CMD_CONVERT;
        } else if (pici->lpVerb == nullptr) {
            // 2) MAKEINTRESOURCEA(0)：即序号 0（浏览）
            cmd = CMD_BROWSE;
        } else if (IS_INTRESOURCE(pici->lpVerb)) {
            // 3) 序号动词
            cmd = LOWORD((UINT_PTR)pici->lpVerb);
        } else {
            // 4) ANSI 字符串动词
            if (lstrcmpiA(pici->lpVerb, kVerbBrowseA) == 0) cmd = CMD_BROWSE;
            else if (lstrcmpiA(pici->lpVerb, kVerbConvertA) == 0) {
                cmd = CMD_CONVERT;
            }
        }
        if (cmd < 0 || cmd >= CMD_COUNT || !m_hasFile) return E_INVALIDARG;
        return Launch(cmd == CMD_CONVERT);
    } catch (...) {
        return E_FAIL;
    }
}

STDMETHODIMP QuikContextMenu::GetCommandString(UINT_PTR idCmd, UINT uType,
                                               UINT*, CHAR* pszName,
                                               UINT cchMax) {
    try {
        if (idCmd >= CMD_COUNT) return E_INVALIDARG;
        // GCS_VALIDATE* 是"验证命令是否合法"，返回 S_OK 即合法，不写缓冲
        if (uType == GCS_VALIDATEA || uType == GCS_VALIDATEW) return S_OK;
        if (uType == GCS_VERBW) {
            const wchar_t* verb =
                (idCmd == CMD_BROWSE) ? kVerbBrowseW : kVerbConvertW;
            if (!pszName || cchMax < (UINT)lstrlenW(verb) + 1) return E_FAIL;
            lstrcpynW((LPWSTR)pszName, verb, (int)cchMax);
            return S_OK;
        }
        if (uType == GCS_VERBA) {
            const char* verb = (idCmd == CMD_BROWSE) ? kVerbBrowseA
                                                     : kVerbConvertA;
            if (!pszName || cchMax < (UINT)lstrlenA(verb) + 1) return E_FAIL;
            lstrcpynA(pszName, verb, (int)cchMax);
            return S_OK;
        }
        return E_NOTIMPL;   // GCS_HELPTEXT* 不提供（静态菜单项无提示需求）
    } catch (...) {
        return E_FAIL;
    }
}

// 解析 quikview.exe 全路径：DLL 同目录 → HKCU → HKLM 的 InstallDir/InstallPath
bool QuikContextMenu::FindQuikViewExe(wchar_t* exePath, DWORD cch) const {
    // 1) DLL 同目录（绿色安装与自测首选：和 DLL 一起部署即可用）
    wchar_t buf[MAX_PATH] = {};
    if (DllGetModule() &&
        GetModuleFileNameW(DllGetModule(), buf, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) {
            lstrcpynW(slash + 1, L"quikview.exe",
                      (int)(MAX_PATH - (slash + 1 - buf)));
            if (FileExists(buf)) {
                lstrcpynW(exePath, buf, (int)cch);
                return true;
            }
        }
    }
    // 2) 注册表 InstallDir（HKCU 预留给按用户安装，HKLM 是安装器现行）
    const HKEY kRoots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (const HKEY root : kRoots) {
        wchar_t dir[MAX_PATH] = {};
        if (!GetInstallDirFromRegistry(root, dir, MAX_PATH)) continue;
        const size_t len = lstrlenW(dir);
        if (len && dir[len - 1] == L'\\') dir[len - 1] = L'\0';
        wsprintfW(buf, L"%s\\quikview.exe", dir);
        if (FileExists(buf)) {
            lstrcpynW(exePath, buf, (int)cch);
            return true;
        }
    }
    return false;
}

// CreateProcessW 启动：browse = `exe "file"`；convert = `exe --batch "file"`
HRESULT QuikContextMenu::Launch(bool batch) const {
    wchar_t exe[MAX_PATH] = {};
    if (!FindQuikViewExe(exe, MAX_PATH)) {
        return E_FAIL;   // 找不到主程序：静默失败，不影响 Explorer
    }
    wchar_t cmdLine[MAX_PATH * 2 + 32] = {};
    wsprintfW(cmdLine, L"\"%s\" %s\"%s\"", exe, batch ? L"--batch " : L"",
              m_file);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe, cmdLine, nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi)) {
        return E_FAIL;   // 启动失败：返回失败码，Explorer 静默忽略
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}
