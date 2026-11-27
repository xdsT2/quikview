// COM 模块入口：DllMain / DllGetClassObject / DllCanUnloadNow /
// DllRegisterServer / DllUnregisterServer（regsvr32 自注册走进程位数控视图）。
// 本 DLL 装两个 COM 类：缩略图提供程序（阶段 2）+ 上下文菜单（阶段 1）。
// 交办单硬约束：所有 COM 入口 try/catch(...) 包裹，异常一律 E_FAIL，不许穿出 DLL。
#include <initializer_list>
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
// 只注册缩略图的扩展名（有渲染能力、但主程序打不开的格式）。
// SVG 走 nanosvg 软渲染（src/svg_raster.cpp）。主程序解码白名单
// （src/DirectoryLoader.cpp 的 kImageGroups）里没有 svg，发布包也不带 Qt 的
// qsvg 图像插件，右键「用 SoleView 浏览」点开会失败 —— 所以 SVG 不进上面的
// 上下文菜单清单，只进缩略图注册清单。脚本与安装器的 ShellEx 段同样只多这一项。
const wchar_t* const kThumbOnlyExts[] = { L".svg" };
const wchar_t kClsidString[] = L"{D8A1C4E2-7B35-4F69-92A4-5C6D7E8F1A2B}";
const wchar_t kFriendlyName[] = L"SoleView Thumbnail Provider";
const wchar_t kThumbCategory[] =
    L"{e357fccd-a995-4576-b01f-234630154e96}";   // 缩略图 ShellEx 类别
const wchar_t kCtxClsidString[] = L"{C9FFAFAD-7C1C-4CCC-A37C-A075ED1F27AF}";
const wchar_t kCtxFriendlyName[] = L"SoleView Context Menu Handler";
// ContextMenuHandlers 下的键名（regsvr32 与脚本一致）；过渡期同时清理指向同一 CLSID 的旧 QuikView 键
const wchar_t kCtxHandlerName[] = L"SoleView";
const wchar_t kLegacyCtxHandlerName[] = L"QuikView";

// 支持扩展名（quikthumb.h 声明；thumbnail 注册与上下文菜单过滤共用一份）
}   // namespace

size_t QuikSupportedExtCount() {
    return sizeof(kSupportedExts) / sizeof(kSupportedExts[0]);
}

const wchar_t* QuikSupportedExt(size_t i) {
    return (i < QuikSupportedExtCount()) ? kSupportedExts[i] : L"";
}

namespace {

// 缩略图注册清单（= 上下文菜单清单 + 只做缩略图的扩展名），
// 只在 ShellEx 注册/反注册里用；顺序沿用两份数组的原顺序。
size_t ThumbExtCount() {
    return sizeof(kSupportedExts) / sizeof(kSupportedExts[0]) +
           sizeof(kThumbOnlyExts) / sizeof(kThumbOnlyExts[0]);
}

const wchar_t* ThumbExt(size_t i) {
    const size_t n = sizeof(kSupportedExts) / sizeof(kSupportedExts[0]);
    if (i < n) return kSupportedExts[i];
    i -= n;
    return (i < sizeof(kThumbOnlyExts) / sizeof(kThumbOnlyExts[0]))
               ? kThumbOnlyExts[i]
               : L"";
}

}   // namespace

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
    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddLock();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        DllReleaseLock();
        return 1;
    }
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
    STDMETHODIMP_(ULONG) AddRef() override {
        DllAddLock();
        return 2;
    }
    STDMETHODIMP_(ULONG) Release() override {
        DllReleaseLock();
        return 1;
    }
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

// 上下文菜单 Handler：只挂在真正支持的图片扩展名上，逐扩展一份
// （Software\Classes\.png\shellex\ContextMenuHandlers\QuikView）。
// 0.5.3 及以前挂在 `*`（全部文件类型）上：等于任意文件右键都要把本 DLL 注入
// 资源管理器进程，而本 DLL 弹菜单时要做一次 WIC 解码 —— 不值得把 Shell 的
// 稳定性押在一次解码上。`*` 键在注册时顺手清掉（只删仍指向本产品的）。
LONG WriteHandlerClsid(const wchar_t* key) {
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

// 只删仍指向本产品 CLSID 的键；别家同名项、已被别家接管的位置一律不动
void DeleteHandlerKeyIfOurs(const wchar_t* key) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE, &hk) !=
        ERROR_SUCCESS) {
        return;
    }
    wchar_t val[64] = {};
    DWORD type = 0;
    DWORD cb = sizeof(val) - sizeof(wchar_t);
    const LONG rc = RegQueryValueExW(hk, nullptr, nullptr, &type, (BYTE*)val,
                                     &cb);
    RegCloseKey(hk);
    val[_countof(val) - 1] = L'\0';
    if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)
        && lstrcmpiW(val, kCtxClsidString) == 0) {
        RegDeleteKeyW(HKEY_LOCAL_MACHINE, key);
    }
}

// 0.5.3 及以前的 `*` 宽注册以及旧名 QuikView 键：升级/反注册都清掉（只删仍指向本产品 CLSID 的项）
void DeleteLegacyHandlers() {
    wchar_t key[288];
    for (const wchar_t* name : { kCtxHandlerName, kLegacyCtxHandlerName }) {
        wsprintfW(key,
                  L"Software\\Classes\\*\\shellex\\ContextMenuHandlers\\%s",
                  name);
        DeleteHandlerKeyIfOurs(key);
        wsprintfW(key,
                  L"Software\\Classes\\WOW6432Node\\*\\shellex\\ContextMenuHandlers\\%s",
                  name);
        DeleteHandlerKeyIfOurs(key);
    }
    for (const wchar_t* ext : kSupportedExts) {
        wsprintfW(key,
                  L"Software\\Classes\\%s\\shellex\\ContextMenuHandlers\\%s",
                  ext, kLegacyCtxHandlerName);
        DeleteHandlerKeyIfOurs(key);
    }
}

LONG RegisterContextMenuHandler() {
    LONG firstErr = ERROR_SUCCESS;
    for (const wchar_t* ext : kSupportedExts) {
        wchar_t key[288];
        wsprintfW(key,
                  L"Software\\Classes\\%s\\shellex\\ContextMenuHandlers\\%s",
                  ext, kCtxHandlerName);
        const LONG rc = WriteHandlerClsid(key);
        if (rc != ERROR_SUCCESS && firstErr == ERROR_SUCCESS) firstErr = rc;
    }
    DeleteLegacyHandlers();
    return firstErr;
}

LONG UnregisterContextMenuHandler() {
    for (const wchar_t* ext : kSupportedExts) {
        wchar_t key[288];
        wsprintfW(key,
                  L"Software\\Classes\\%s\\shellex\\ContextMenuHandlers\\%s",
                  ext, kCtxHandlerName);
        DeleteHandlerKeyIfOurs(key);
    }
    DeleteLegacyHandlers();
    return ERROR_SUCCESS;
}

// 用户级（HKCU）是否已被别家缩略图处理器占住。Explorer 取缩略图按用户注册
// 优先，所以「装到 HKLM 但 HKCU 写着别人」= 装了也不生效。产品语义是
// 「检测到被别人占用就提示、不接管」：这里只检测，命中就跳过写 HKLM，
// 由设置页如实显示「已被其他程序占用（不接管）」。
bool PerUserHandlerIsForeign(const wchar_t* ext) {
    wchar_t key[256];
    wsprintfW(key, L"Software\\Classes\\%s\\ShellEx\\%s", ext, kThumbCategory);
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &hk) !=
        ERROR_SUCCESS) {
        return false;
    }
    wchar_t val[128] = {};
    DWORD type = 0;
    DWORD cb = sizeof(val) - sizeof(wchar_t);
    const LONG rc =
        RegQueryValueExW(hk, nullptr, nullptr, &type, (BYTE*)val, &cb);
    RegCloseKey(hk);
    val[_countof(val) - 1] = L'\0';
    if (rc == ERROR_MORE_DATA) return true;
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || val[0] == L'\0') return false;
    return lstrcmpiW(val, kClsidString) != 0;
}

// .ext\ShellEx 键为 32/64 位共享视图，两个位数的 regsvr32 写的是同一份，幂等
LONG RegisterShellExKeys() {
    for (size_t i = 0; i < ThumbExtCount(); ++i) {
        const wchar_t* ext = ThumbExt(i);
        if (PerUserHandlerIsForeign(ext)) continue;   // 被占用：不接管
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
        for (size_t i = 0; i < ThumbExtCount(); ++i) {
            const wchar_t* ext = ThumbExt(i);
            wchar_t key[256];
            wsprintfW(key, fmt, ext, kThumbCategory);
            HKEY hk = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, key, 0, KEY_QUERY_VALUE,
                              &hk) != ERROR_SUCCESS) {
                continue;
            }
            wchar_t val[64] = {};
            DWORD type = 0;
            DWORD cb = sizeof(val) - sizeof(wchar_t);
            const LONG rc = RegQueryValueExW(hk, nullptr, nullptr, &type,
                                             (BYTE*)val, &cb);
            RegCloseKey(hk);
            val[_countof(val) - 1] = L'\0';
            if (rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)
                && lstrcmpiW(val, kClsidString) == 0) {
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
    return InterlockedCompareExchange(&g_cLock, 0, 0) == 0 ? S_OK : S_FALSE;
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
