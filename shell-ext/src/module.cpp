// COM 模块入口：DllMain / DllGetClassObject / DllCanUnloadNow /
// DllRegisterServer / DllUnregisterServer（regsvr32 自注册走进程位数控视图）。
// 本 DLL 装两个 COM 类：缩略图提供程序（阶段 2）+ 上下文菜单（阶段 1）。
// 交办单硬约束：所有 COM 入口 try/catch(...) 包裹，异常一律 E_FAIL，不许穿出 DLL。
#include <new>

#include "contextmenu.h"
#include "quikthumb.h"

namespace {

HMODULE g_hInst = nullptr;
volatile LONG g_cLock = 0;   // 模块锁：存活对象数 + LockServer

// 注册脚本与 regsvr32 都写这些常量，改动必须同步 shell-ext/scripts/*.ps1
const wchar_t* const kSupportedExts[] = {
    L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".webp", L".tif", L".tiff",
    L".psd", L".psb",
};
const wchar_t kClsidString[] = L"{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}";
const wchar_t kFriendlyName[] = L"QuikView Thumbnail Provider";
const wchar_t kThumbCategory[] =
    L"{e357fccd-a995-4576-b01f-234630154e96}";   // 缩略图 ShellEx 类别
const wchar_t kCtxClsidString[] = L"{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}";
const wchar_t kCtxFriendlyName[] = L"QuikView Context Menu Handler";
// ContextMenuHandlers 下的键名（regsvr32 与脚本一致）
const wchar_t kCtxHandlerName[] = L"QuikView";

// 支持扩展名（quikthumb.h 声明；thumbnail 注册与上下文菜单过滤共用一份）
}   // namespace

size_t QuikSupportedExtCount() {
    return sizeof(kSupportedExts) / sizeof(kSupportedExts[0]);
}

const wchar_t* QuikSupportedExt(size_t i) {
    return (i < QuikSupportedExtCount()) ? kSupportedExts[i] : L"";
}

namespace {

// 进程内静态单例工厂：引用计数恒为 2（永不销毁），卸载由锁计数决定
class QuikThumbClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 2; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
                                void** ppv) override {
        try {
            if (!ppv) return E_POINTER;
            *ppv = nullptr;
            if (outer) return CLASS_E_NOAGGREGATION;
            QuikThumbProvider* obj = new (std::nothrow) QuikThumbProvider();
            if (!obj) return E_OUTOFMEMORY;
            const HRESULT hr = obj->QueryInterface(riid, ppv);
            obj->Release();
            return hr;
        } catch (...) {
            return E_FAIL;
        }
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            DllAddLock();
        } else {
            DllReleaseLock();
        }
        return S_OK;
    }
};

// 上下文菜单类工厂：与缩略图工厂同构（静态单例、引用计数恒为 2）
class QuikCtxClassFactory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IClassFactory)) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 2; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid,
                                void** ppv) override {
        try {
            if (!ppv) return E_POINTER;
            *ppv = nullptr;
            if (outer) return CLASS_E_NOAGGREGATION;
            QuikContextMenu* obj = new (std::nothrow) QuikContextMenu();
            if (!obj) return E_OUTOFMEMORY;
            const HRESULT hr = obj->QueryInterface(riid, ppv);
            obj->Release();
            return hr;
        } catch (...) {
            return E_FAIL;
        }
    }
    STDMETHODIMP LockServer(BOOL fLock) override {
        if (fLock) {
            DllAddLock();
        } else {
            DllReleaseLock();
        }
        return S_OK;
    }
};

// CLSID 注册：写进程默认视图（64 位 regsvr32 → 64 位视图；
// 32 位 regsvr32 → 重定向到 WOW6432Node）
struct ClsidEntry {
    const wchar_t* clsid;
    const wchar_t* name;
};

const ClsidEntry kClsidEntries[] = {
    { kClsidString, kFriendlyName },
    { kCtxClsidString, kCtxFriendlyName },
};

LONG RegisterClsids() {
    wchar_t dllPath[MAX_PATH] = {};
    if (GetModuleFileNameW(g_hInst, dllPath, MAX_PATH) == 0) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    for (const ClsidEntry& entry : kClsidEntries) {
        wchar_t clsidKey[160];
        wsprintfW(clsidKey, L"Software\\Classes\\CLSID\\%s", entry.clsid);

        HKEY hk = nullptr;
        LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, clsidKey, 0, nullptr,
                                  REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                  nullptr, &hk, nullptr);
        if (rc != ERROR_SUCCESS) return rc;
        RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)entry.name,
                       (DWORD)(lstrlenW(entry.name) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);

        wchar_t ips[192];
        wsprintfW(ips, L"%s\\InprocServer32", clsidKey);
        rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, ips, 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr,
                             &hk, nullptr);
        if (rc != ERROR_SUCCESS) return rc;
        RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)dllPath,
                       (DWORD)(lstrlenW(dllPath) + 1) * sizeof(wchar_t));
        const wchar_t threading[] = L"Apartment";
        RegSetValueExW(hk, L"ThreadingModel", 0, REG_SZ, (const BYTE*)threading,
                       (DWORD)(lstrlenW(threading) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);
    }
    return ERROR_SUCCESS;
}

LONG UnregisterClsids() {
    for (const ClsidEntry& entry : kClsidEntries) {
        wchar_t clsidKey[160];
        wsprintfW(clsidKey, L"Software\\Classes\\CLSID\\%s", entry.clsid);
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, clsidKey);
    }
    return ERROR_SUCCESS;
}

// 上下文菜单 Handler：`*`（全部文件类型）\shellex\ContextMenuHandlers\QuikView
// 指向上下文菜单 CLSID。注册脚本会显式写 64/32 双视图；regsvr32 只写本位数
// 默认视图（32 位进程对 Software\Classes 的写落在 WOW6432Node，正合适）。
LONG RegisterContextMenuHandler() {
    wchar_t key[256];
    wsprintfW(key,
              L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s",
              kCtxHandlerName);
    HKEY hk = nullptr;
    const LONG rc = RegCreateKeyExW(HKEY_LOCAL_MACHINE, key, 0, nullptr,
                                    REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                                    nullptr, &hk, nullptr);
    if (rc != ERROR_SUCCESS) return rc;
    RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)kCtxClsidString,
                   (DWORD)(lstrlenW(kCtxClsidString) + 1) * sizeof(wchar_t));
    RegCloseKey(hk);
    return ERROR_SUCCESS;
}

// 反注册只删仍指向本产品 CLSID 的 Handler 键，别家的 ContextMenuHandlers 不动；
// WOW6432Node 侧兜底对称清理（键不存在属正常）
LONG UnregisterContextMenuHandler() {
    const wchar_t* const kFmtBases[] = {
        L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s",
        L"Software\\Classes\\WOW6432Node\\*\\shellex\\ContextMenuHandlers\\%s",
    };
    for (const wchar_t* fmt : kFmtBases) {
        wchar_t key[288];
        wsprintfW(key, fmt, kCtxHandlerName);
        HKEY hk = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &hk) !=
            ERROR_SUCCESS) {
            continue;
        }
        wchar_t val[64] = {};
        DWORD cb = sizeof(val);
        const LONG rc = RegQueryValueExW(hk, nullptr, nullptr, nullptr,
                                         (BYTE*)val, &cb);
        RegCloseKey(hk);
        if (rc == ERROR_SUCCESS && lstrcmpiW(val, kCtxClsidString) == 0) {
            RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
        }
    }
    return ERROR_SUCCESS;
}

// .ext\ShellEx 键为 32/64 位共享视图，两个位数的 regsvr32 写的是同一份，幂等
LONG RegisterShellExKeys() {
    for (const wchar_t* ext : kSupportedExts) {
        wchar_t key[256];
        wsprintfW(key, L"Software\\Classes\\%s\\ShellEx\\%s", ext,
                  kThumbCategory);
        HKEY hk = nullptr;
        const LONG rc = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, key, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE, nullptr, &hk, nullptr);
        if (rc != ERROR_SUCCESS) return rc;
        RegSetValueExW(hk, nullptr, 0, REG_SZ, (const BYTE*)kClsidString,
                       (DWORD)(lstrlenW(kClsidString) + 1) * sizeof(wchar_t));
        RegCloseKey(hk);
    }
    return ERROR_SUCCESS;
}

// 反注册只删仍指向本产品 CLSID 的 ShellEx 值，别家的 handler 不动；
// 顺带清理 WOW6432Node 侧扩展键（对称反注册，键不存在属正常）
LONG UnregisterShellExKeys() {
    const wchar_t* const bases[] = {
        L"Software\\Classes\\%s\\ShellEx\\%s",
        L"Software\\Classes\\WOW6432Node\\%s\\ShellEx\\%s",
    };
    for (const wchar_t* fmt : bases) {
        for (const wchar_t* ext : kSupportedExts) {
            wchar_t key[256];
            wsprintfW(key, fmt, ext, kThumbCategory);
            HKEY hk = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE,
                              &hk) != ERROR_SUCCESS) {
                continue;
            }
            wchar_t val[64] = {};
            DWORD cb = sizeof(val);
            const LONG rc = RegQueryValueExW(hk, nullptr, nullptr, nullptr,
                                             (BYTE*)val, &cb);
            RegCloseKey(hk);
            if (rc == ERROR_SUCCESS && lstrcmpiW(val, kClsidString) == 0) {
                RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
            }
        }
    }
    return ERROR_SUCCESS;
}

}   // namespace

void DllAddLock() { InterlockedIncrement(&g_cLock); }
void DllReleaseLock() { InterlockedDecrement(&g_cLock); }
HMODULE DllGetModule() { return g_hInst; }

extern "C" BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

// 四个标准 COM 入口的导出：
// 这四个函数系统头（objbase.h/combaseapi.h）已有声明。MSVC 下定义必须与
// 声明完全一致（否则 C2375），导出交给 CMake 的 /EXPORT 链接选项（x86 需
// 去修饰名）；MinGW 无此冲突，定义处直接 dllexport（导出无修饰名）。
#ifdef _MSC_VER
#define QV_EXPORT extern "C" HRESULT STDAPICALLTYPE
#else
#define QV_EXPORT \
    extern "C" __declspec(dllexport) HRESULT STDAPICALLTYPE
#endif

QV_EXPORT DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualCLSID(rclsid, CLSID_QuikThumbProvider)) {
            static QuikThumbClassFactory factory;
            return factory.QueryInterface(riid, ppv);
        }
        if (IsEqualCLSID(rclsid, CLSID_QuikContextMenu)) {
            static QuikCtxClassFactory factory;
            return factory.QueryInterface(riid, ppv);
        }
        return CLASS_E_CLASSNOTAVAILABLE;
    } catch (...) {
        return E_FAIL;
    }
}

QV_EXPORT DllCanUnloadNow(void) {
    return g_cLock == 0 ? S_OK : S_FALSE;
}

// 自注册只覆盖本位数控视图；脚本（scripts/*.ps1）负责一次注册 64+32 双视图，
// 两者注册模型一致。安装器集成时以 ctxmenu/use_shell_menu 总开关决定是否调用。
QV_EXPORT DllRegisterServer(void) {
    try {
        LONG rc = RegisterClsids();
        if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
        rc = RegisterShellExKeys();
        if (rc != ERROR_SUCCESS) return HRESULT_FROM_WIN32(rc);
        rc = RegisterContextMenuHandler();
        return rc == ERROR_SUCCESS ? S_OK : HRESULT_FROM_WIN32(rc);
    } catch (...) {
        return E_FAIL;
    }
}

QV_EXPORT DllUnregisterServer(void) {
    try {
        UnregisterClsids();
        UnregisterShellExKeys();
        UnregisterContextMenuHandler();
        return S_OK;   // 尽力清理，反注册失败不阻塞卸载
    } catch (...) {
        return E_FAIL;
    }
}
