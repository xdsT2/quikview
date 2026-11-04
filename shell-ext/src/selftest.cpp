// thumb_selftest：无注册表加载自测。
// LoadLibrary(DLL) → DllGetClassObject 直接拿类厂 → CreateInstance →
//   缩略图：SHCreateStreamOnFileW 喂图片流 → GetThumbnail → 校验 HBITMAP 尺寸；
//   上下文菜单：自建 CF_HDROP 数据对象 → Initialize → QueryContextMenu 枚举
//   菜单项 → GetCommandString 动词 → InvokeCommand（quikview.exe 由
//   ctx_fake_quikview.exe 顶替，写标记文件供断言，不会拉起真实应用）。
// 全程不写注册表、不经 COM 激活，验证「编译链接 + 加载 + 全链路」闭环。
// 真实右键弹出菜单属人工验收（见 shell-ext/README.md）。
#include "quikthumb.h"   // 先于 shobjidl.h：guard 住自带 IInitializeWithStream，
                         // 防 shobjidl→propsys.h 同名类重定义
#include <ole2.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <shlobj.h>      // DROPFILES（MinGW 在此，MSVC 在 shellapi.h）
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <string>

typedef HRESULT(STDAPICALLTYPE* PFN_DllGetClassObject)(REFCLSID, REFIID,
                                                       void**);
typedef HRESULT(STDAPICALLTYPE* PFN_DllCanUnloadNow)(void);

// ---- 上下文菜单自测的通过/失败计数与断言 ----
static int g_pass = 0, g_fail = 0;
static void CtxPass(const char* name) { printf("PASS: %s\n", name); g_pass++; }
static void CtxFail(const char* name) { printf("FAIL: %s\n", name); g_fail++; }
#define CTX(cond, name) do { if (cond) CtxPass(name); else CtxFail(name); } while (0)

namespace {

std::wstring ToWide(const char* s) {
    wchar_t buf[MAX_PATH * 2] = {};
    MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, (int)(sizeof(buf) / sizeof(buf[0])));
    return buf;
}

// 就地去掉路径的文件名部分（保留到最后一个分隔符之前）
void DirOfA(char* path) {
    char* a = strrchr(path, '\\');
    char* b = strrchr(path, '/');
    if (b > a) a = b;
    if (a) *a = '\0';
}

bool FileExistsA(const char* path) {
    const DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 本机是否配置了安装目录（InstallDir/InstallPath）。有则跳过「找不到 exe 必
// 失败」用例，避免把真实安装的 quikview.exe 拉起来。
bool InstallDirConfigured() {
    static const wchar_t* const kNames[] = { L"InstallDir", L"InstallPath" };
    const HKEY kRoots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (const HKEY root : kRoots) {
        HKEY hk = nullptr;
        if (RegOpenKeyExW(root, L"Software\\QuikView", 0,
                          KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                          &hk) != ERROR_SUCCESS) {
            continue;
        }
        for (const wchar_t* name : kNames) {
            DWORD type = 0;
            DWORD cb = 0;
            if (RegQueryValueExW(hk, name, nullptr, &type, nullptr, &cb) ==
                    ERROR_SUCCESS &&
                type == REG_SZ && cb > sizeof(wchar_t)) {
                RegCloseKey(hk);
                return true;
            }
        }
        RegCloseKey(hk);
    }
    return false;
}

// ---- 测试用 IDataObject：只支持 CF_HDROP（其余一律桩）----
class DropDataObject : public IDataObject {
public:
    explicit DropDataObject(HGLOBAL hg) : m_ref(1), m_hg(hg) {}
    virtual ~DropDataObject() {
        if (m_hg) GlobalFree(m_hg);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IDataObject)) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override {
        const ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    STDMETHODIMP GetData(FORMATETC* pfe, STGMEDIUM* pstm) override {
        if (!pfe || !pstm || pfe->cfFormat != CF_HDROP ||
            !(pfe->tymed & TYMED_HGLOBAL) || !m_hg) {
            return DV_E_FORMATETC;
        }
        pstm->tymed = TYMED_HGLOBAL;
        pstm->hGlobal = m_hg;
        // 全局归本对象所有；ReleaseStgMedium 只会 Release 我们，不 GlobalFree
        pstm->pUnkForRelease = this;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP QueryGetData(FORMATETC* pfe) override {
        return (pfe && pfe->cfFormat == CF_HDROP &&
                (pfe->tymed & TYMED_HGLOBAL))
                   ? S_OK
                   : DV_E_FORMATETC;
    }
    STDMETHODIMP GetCanonicalFormatEtc(FORMATETC* pin, FORMATETC* pout)
        override {
        if (pout) {
            if (pin) *pout = *pin;
            pout->ptd = nullptr;
        }
        return S_OK;
    }
    STDMETHODIMP SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP EnumFormatEtc(DWORD, IEnumFORMATETC**) override {
        return E_NOTIMPL;
    }
    STDMETHODIMP DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    STDMETHODIMP DUnadvise(DWORD) override { return OLE_E_NOCONNECTION; }
    STDMETHODIMP EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    ULONG m_ref;
    HGLOBAL m_hg;
};

// 构造 CF_HDROP 全局内存（宽字符文件列表，资源管理器同款格式）
HGLOBAL MakeHDrop(const wchar_t* const* files, UINT count) {
    SIZE_T bytes = sizeof(DROPFILES);
    for (UINT i = 0; i < count; ++i) {
        bytes += (lstrlenW(files[i]) + 1) * sizeof(wchar_t);
    }
    bytes += sizeof(wchar_t);   // 列表结尾额外一个 '\0'
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
    if (!hg) return nullptr;
    DROPFILES* df = (DROPFILES*)GlobalLock(hg);
    if (!df) {
        GlobalFree(hg);
        return nullptr;
    }
    df->pFiles = (DWORD)sizeof(DROPFILES);
    df->fWide = TRUE;
    wchar_t* w = (wchar_t*)((BYTE*)df + df->pFiles);
    for (UINT i = 0; i < count; ++i) {
        const int n = lstrlenW(files[i]) + 1;
        memcpy(w, files[i], (size_t)n * sizeof(wchar_t));
        w += n;
    }
    *w = L'\0';
    GlobalUnlock(hg);
    return hg;
}

// CF_HDROP 初始化 → QI IContextMenu，建一个就绪的上下文菜单处理器实例
bool NewCtxHandler(IClassFactory* ccf, const wchar_t* const* files, UINT count,
                   IContextMenu** outCtx) {
    *outCtx = nullptr;
    HGLOBAL hg = MakeHDrop(files, count);
    if (!hg) return false;
    IDataObject* pdo = new (std::nothrow) DropDataObject(hg);
    if (!pdo) {
        GlobalFree(hg);
        return false;
    }
    IShellExtInit* sei = nullptr;
    HRESULT hr = ccf->CreateInstance(nullptr, IID_IShellExtInit, (void**)&sei);
    if (FAILED(hr) || !sei) {
        pdo->Release();
        return false;
    }
    hr = sei->Initialize(nullptr, pdo, nullptr);
    pdo->Release();
    if (hr != S_OK) {
        sei->Release();
        return false;
    }
    hr = sei->QueryInterface(IID_IContextMenu, (void**)outCtx);
    sei->Release();
    return SUCCEEDED(hr) && *outCtx;
}

// 等待并读取标记文件（fake exe 写的 UTF-16LE 命令行），超时返回已读内容
std::wstring ReadMarkerWait(const wchar_t* path, DWORD timeoutMs) {
    std::wstring out;
    const DWORD deadline = GetTickCount() + timeoutMs;
    for (;;) {
        HANDLE h = CreateFileW(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[8192];
            DWORD got = 0;
            if (ReadFile(h, buf, (DWORD)sizeof(buf) - 2, &got, nullptr) &&
                got >= 4) {
                out.assign((const wchar_t*)buf, got / sizeof(wchar_t));
            }
            CloseHandle(h);
            if (!out.empty()) return out;
        }
        if ((int)(GetTickCount() - deadline) >= 0) return out;
        Sleep(100);
    }
}

// 标记内容摘要（C locale 的 printf 渲染不了中文路径，只报标志位）
void DumpMark(const char* tag, HRESULT hr, const std::wstring& m,
              const std::wstring& img) {
    printf("      diag %s: hr=0x%08lX len=%zu img=%d batch=%d second=%d\n",
           tag, (unsigned long)hr, m.size(),
           (int)(m.find(img) != std::wstring::npos),
           (int)(m.find(L"--batch") != std::wstring::npos),
           (int)(m.find(L"second.png") != std::wstring::npos));
}

}   // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃也能看到已打印的部分
    if (argc < 3) {
        printf("usage: thumb_selftest.exe <quikthumb.dll> <image> [image...]\n");
        return 2;
    }

    HMODULE hDll = LoadLibraryExA(argv[1], nullptr,
                                  LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!hDll) {
        printf("FAIL: LoadLibrary(%s) gle=%lu\n", argv[1], GetLastError());
        return 1;
    }
    printf("OK: LoadLibrary %s\n", argv[1]);

    const PFN_DllGetClassObject pGetClassObject =
        (PFN_DllGetClassObject)GetProcAddress(hDll, "DllGetClassObject");
    const PFN_DllCanUnloadNow pCanUnloadNow =
        (PFN_DllCanUnloadNow)GetProcAddress(hDll, "DllCanUnloadNow");
    if (!pGetClassObject || !pCanUnloadNow) {
        printf("FAIL: missing exports (DllGetClassObject=%p DllCanUnloadNow=%p)\n",
               (void*)pGetClassObject, (void*)pCanUnloadNow);
        return 1;
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        printf("FAIL: CoInitializeEx\n");
        return 1;
    }

    IClassFactory* cf = nullptr;
    HRESULT hr = pGetClassObject(CLSID_QuikThumbProvider, IID_IClassFactory,
                                 (void**)&cf);
    if (FAILED(hr) || !cf) {
        printf("FAIL: DllGetClassObject hr=0x%08lX\n", (unsigned long)hr);
        return 1;
    }
    printf("OK: DllGetClassObject -> IClassFactory\n");

    // 未知 CLSID 必须拒绝（借 IID_IUnknown 当不存在的 CLSID 用）
    IClassFactory* bogus = nullptr;
    hr = pGetClassObject(IID_IUnknown, IID_IClassFactory, (void**)&bogus);
    if (hr != CLASS_E_CLASSNOTAVAILABLE || bogus != nullptr) {
        printf("FAIL: unknown CLSID not rejected (hr=0x%08lX)\n",
               (unsigned long)hr);
        return 1;
    }
    printf("OK: unknown CLSID rejected\n");

    // ===== 缩略图（阶段 2）=====
    int thumbPass = 0, thumbFail = 0;
    for (int i = 2; i < argc; i++) {
        wchar_t wpath[MAX_PATH];
        MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, wpath, MAX_PATH);

        QvInitializeWithStream* init = nullptr;
        hr = cf->CreateInstance(nullptr, IID_IInitializeWithStream,
                                (void**)&init);
        if (FAILED(hr) || !init) {
            printf("FAIL: %s CreateInstance hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            thumbFail++;
            continue;
        }

        IStream* st = nullptr;
        hr = SHCreateStreamOnFileW(wpath, STGM_READ | STGM_SHARE_DENY_WRITE,
                                   &st);
        if (FAILED(hr) || !st) {
            printf("FAIL: %s open stream hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            init->Release();
            thumbFail++;
            continue;
        }
        hr = init->Load(st, STGM_READ);
        st->Release();   // handler 自持引用
        if (FAILED(hr)) {
            printf("FAIL: %s Load hr=0x%08lX\n", argv[i], (unsigned long)hr);
            init->Release();
            thumbFail++;
            continue;
        }

        IThumbnailProvider* tp = nullptr;
        hr = init->QueryInterface(IID_IThumbnailProvider, (void**)&tp);
        if (FAILED(hr) || !tp) {
            printf("FAIL: %s QI(IThumbnailProvider) hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            init->Release();
            thumbFail++;
            continue;
        }

        HBITMAP hbm = nullptr;
        WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
        hr = tp->GetThumbnail(256, &hbm, &alpha);
        if (SUCCEEDED(hr) && hbm) {
            BITMAP bm = {};
            if (GetObjectW(hbm, sizeof(bm), &bm) && bm.bmWidth > 0 &&
                bm.bmHeight > 0 && bm.bmBitsPixel == 32) {
                printf("PASS: %s -> %ldx%ld alpha=%d\n", argv[i], bm.bmWidth,
                       bm.bmHeight, (int)alpha);
                thumbPass++;
            } else {
                printf("FAIL: %s bad BITMAP (w=%ld h=%ld bpp=%d)\n", argv[i],
                       bm.bmWidth, bm.bmHeight, bm.bmBitsPixel);
                thumbFail++;
            }
            DeleteObject(hbm);
        } else {
            printf("FAIL: %s GetThumbnail hr=0x%08lX\n", argv[i],
                   (unsigned long)hr);
            thumbFail++;
        }
        tp->Release();
        init->Release();
    }

    // ===== 上下文菜单（阶段 1）=====
    printf("--- IContextMenu ---\n");
    const std::wstring wImg = ToWide(argv[2]);
    // 多选用例的第二、三张只是字符串（CF_HDROP 不要求文件真实存在，
    // 处理器按多选策略只认第一个）
    const wchar_t* const kMulti[3] = { wImg.c_str(),
                                       L"E:\\__qv_test__\\second.png",
                                       L"E:\\__qv_test__\\third.png" };
    const wchar_t* const kTxt[1] = { L"E:\\__qv_test__\\note.txt" };

    IClassFactory* ccf = nullptr;
    hr = pGetClassObject(CLSID_QuikContextMenu, IID_IClassFactory,
                         (void**)&ccf);
    CTX(SUCCEEDED(hr) && ccf, "ctx-1 DllGetClassObject(CLSID_QuikContextMenu)");

    IContextMenu* ctx = nullptr;      // 单图实例（用例 2~11）
    IContextMenu* ctxMulti = nullptr; // 多选实例（用例 12）
    IContextMenu* ctxTxt = nullptr;   // 不支持扩展名实例（用例 13）
    if (ccf) {
        // ctx-2：建实例 + 接口分离（不对外暴露缩略图接口）
        {
            IShellExtInit* sei = nullptr;
            hr = ccf->CreateInstance(nullptr, IID_IShellExtInit, (void**)&sei);
            bool ok = SUCCEEDED(hr) && sei;
            if (ok) {
                IThumbnailProvider* tp = nullptr;
                hr = sei->QueryInterface(IID_IThumbnailProvider, (void**)&tp);
                ok = (hr == E_NOINTERFACE && !tp);
                if (tp) tp->Release();
            }
            if (ok) {
                hr = sei->QueryInterface(IID_IContextMenu, (void**)&ctx);
                ok = SUCCEEDED(hr) && ctx;
            }
            if (sei) sei->Release();
            CTX(ok, "ctx-2 CreateInstance(IShellExtInit)+QI(IContextMenu)，"
                    "拒绝 IThumbnailProvider");
        }

        // ctx-3：CF_HDROP 初始化
        hr = E_FAIL;
        if (ctx) {
            HGLOBAL hg = MakeHDrop(kMulti, 1);
            IDataObject* pdo = hg ? new (std::nothrow) DropDataObject(hg)
                                  : nullptr;
            if (!pdo && hg) GlobalFree(hg);
            IShellExtInit* sei = nullptr;
            ctx->QueryInterface(IID_IShellExtInit, (void**)&sei);
            hr = sei ? sei->Initialize(nullptr, pdo, nullptr) : E_FAIL;
            if (pdo) pdo->Release();
            if (sei) sei->Release();
        }
        CTX(hr == S_OK, "ctx-3 Initialize(CF_HDROP) == S_OK");

        // ctx-4：QueryContextMenu → 分隔线 + 两项，ID 依次递增
        if (ctx) {
            HMENU menu = CreatePopupMenu();
            hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
            bool ok = hr == MAKE_HRESULT(SEVERITY_SUCCESS, 0, 2) &&
                      GetMenuItemCount(menu) == 3;
            // 分隔线按 fType 判（实测 GetMenuItemID 对 MFT_SEPARATOR 返回存储的
            // wID=0，并非文档写的 0xFFFFFFFF）
            if (ok) {
                MENUITEMINFOW mii = {};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE;
                ok = GetMenuItemInfoW(menu, 0, TRUE, &mii) &&
                     mii.fType == MFT_SEPARATOR;
            }
            wchar_t txt[128] = {};
            if (ok) {
                ok = GetMenuItemID(menu, 1) == 1 && GetMenuItemID(menu, 2) == 2;
            }
            if (ok) {
                GetMenuStringW(menu, 1, txt, 128, MF_BYPOSITION);
                ok = wcsstr(txt, L"浏览") != nullptr;
                GetMenuStringW(menu, 2, txt, 128, MF_BYPOSITION);
                ok = ok && wcsstr(txt, L"转换") != nullptr;
            }
            if (!ok) {
                for (UINT pos = 0; pos < (UINT)GetMenuItemCount(menu); ++pos) {
                    wchar_t t[128] = {};
                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STRING;
                    mii.dwTypeData = t;
                    mii.cch = 127;
                    const BOOL qr = GetMenuItemInfoW(menu, pos, TRUE, &mii);
                    // C locale 的 printf 渲染不了中文，只报长度与是否含关键词
                    printf("      diag[%u]: qr=%d fType=0x%X id=%lu len=%u "
                           "browse=%d convert=%d\n",
                           pos, (int)qr, mii.fType, (unsigned long)mii.wID,
                           (unsigned)wcslen(t),
                           (int)(wcsstr(t, L"浏览") != nullptr),
                           (int)(wcsstr(t, L"转换") != nullptr));
                }
                printf("      diag: hr=0x%08lX count=%d\n", (unsigned long)hr,
                       GetMenuItemCount(menu));
            }
            DestroyMenu(menu);
            CTX(ok, "ctx-4 QueryContextMenu 枚举：分隔线+「浏览」+「转换」");
        } else {
            CtxFail("ctx-4 QueryContextMenu 枚举（无实例）");
        }

        // ctx-5：CMF_DEFAULTONLY 不注入
        if (ctx) {
            HMENU menu = CreatePopupMenu();
            hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_DEFAULTONLY);
            const bool ok = HRESULT_CODE(hr) == 0 && GetMenuItemCount(menu) == 0;
            DestroyMenu(menu);
            CTX(ok, "ctx-5 CMF_DEFAULTONLY 不注入");
        } else {
            CtxFail("ctx-5 CMF_DEFAULTONLY 不注入（无实例）");
        }

        // ctx-6：GetCommandString 动词
        if (ctx) {
            wchar_t wverb[64] = {};
            char averb[64] = {};
            hr = ctx->GetCommandString(0, GCS_VERBW, nullptr, (CHAR*)wverb, 64);
            bool ok = SUCCEEDED(hr) && wcscmp(wverb, L"QuikView.Browse") == 0;
            hr = ctx->GetCommandString(1, GCS_VERBW, nullptr, (CHAR*)wverb, 64);
            ok = ok && SUCCEEDED(hr) &&
                 wcscmp(wverb, L"QuikView.Convert") == 0;
            hr = ctx->GetCommandString(0, GCS_VERBA, nullptr, averb, 64);
            ok = ok && SUCCEEDED(hr) && strcmp(averb, "QuikView.Browse") == 0;
            hr = ctx->GetCommandString(0, GCS_VALIDATEW, nullptr, nullptr, 0);
            ok = ok && hr == S_OK;
            hr = ctx->GetCommandString(7, GCS_VERBW, nullptr, (CHAR*)wverb, 64);
            ok = ok && FAILED(hr);
            CTX(ok, "ctx-6 GetCommandString 动词/校验/越界");
        } else {
            CtxFail("ctx-6 GetCommandString（无实例）");
        }

        // 准备 fake 替身与标记文件路径（用例 7~11）
        char selfDir[MAX_PATH] = {};
        char dllDir[MAX_PATH] = {};
        GetModuleFileNameA(nullptr, selfDir, MAX_PATH);
        DirOfA(selfDir);
        GetFullPathNameA(argv[1], MAX_PATH, dllDir, nullptr);
        DirOfA(dllDir);
        char fakeSrc[MAX_PATH], fakeDst[MAX_PATH], markPathA[MAX_PATH];
        snprintf(fakeSrc, sizeof(fakeSrc), "%s\\ctx_fake_quikview.exe", selfDir);
        snprintf(fakeDst, sizeof(fakeDst), "%s\\quikview.exe", dllDir);
        snprintf(markPathA, sizeof(markPathA), "%s\\ctx_mark.txt", dllDir);
        const std::wstring wMark = ToWide(markPathA);

        // 上一轮崩溃可能残留替身/标记，先清掉；清不掉多半有进程/杀软占用，
        // 后续用例可能读到旧标记，这里提示出来
        if (FileExistsA(fakeDst) || FileExistsA(markPathA)) {
            DeleteFileA(fakeDst);
            DeleteFileA(markPathA);
            if (FileExistsA(fakeDst) || FileExistsA(markPathA)) {
                printf("WARN: 残留替身/标记删除失败，后续用例可能受影响\n");
            }
        }

        // ctx-7：找不到 quikview.exe 时 InvokeCommand 静默失败（不弹窗不崩）
        if (InstallDirConfigured()) {
            printf("SKIP: ctx-7（本机已配置 InstallDir/InstallPath，"
                   "跳过「找不到 exe 必失败」用例）\n");
        } else {
            CMINVOKECOMMANDINFO ci = {};
            ci.cbSize = sizeof(ci);
            ci.lpVerb = MAKEINTRESOURCEA(0);
            hr = ctx ? ctx->InvokeCommand(&ci) : S_OK;
            CTX(ctx && FAILED(hr), "ctx-7 无 quikview.exe 时静默失败");
        }

        // fake 替身就位后：InvokeCommand 真启动进程并核对命令行
        const bool haveFake = FileExistsA(fakeSrc) &&
                              CopyFileA(fakeSrc, fakeDst, FALSE);
        if (!haveFake) {
            printf("SKIP: ctx-8..12（缺 ctx_fake_quikview.exe，"
                   "先构建 --target ctx_fake_quikview）\n");
        } else {
            // 预热：新拷贝的 exe 可能撞上杀软实时扫描，首次 CreateProcess 易失败。
            // 空跑一次（不给标记环境变量 → 替身立即退出），确认本体可执行。
            {
                wchar_t wFakeDst[MAX_PATH] = {};
                MultiByteToWideChar(CP_UTF8, 0, fakeDst, -1, wFakeDst, MAX_PATH);
                wchar_t cmdLine[MAX_PATH + 4] = {};
                wsprintfW(cmdLine, L"\"%s\"", wFakeDst);
                STARTUPINFOW si = {};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi = {};
                if (CreateProcessW(wFakeDst, cmdLine, nullptr, nullptr, FALSE,
                                   0, nullptr, nullptr, &si, &pi)) {
                    WaitForSingleObject(pi.hProcess, 10000);
                    CloseHandle(pi.hThread);
                    CloseHandle(pi.hProcess);
                } else {
                    Sleep(1000);   // 稍等扫描结束，用例里的真启动再试
                }
            }
            SetEnvironmentVariableW(L"QUIKVIEW_FAKE_MARK", wMark.c_str());

            // ctx-8：浏览（无 --batch）
            DeleteFileW(wMark.c_str());
            CMINVOKECOMMANDINFO ci = {};
            ci.cbSize = sizeof(ci);
            ci.lpVerb = MAKEINTRESOURCEA(0);
            hr = ctx->InvokeCommand(&ci);
            const std::wstring mark = ReadMarkerWait(wMark.c_str(), 10000);
            const bool ok8 = hr == S_OK &&
                             mark.find(wImg) != std::wstring::npos &&
                             mark.find(L"--batch") == std::wstring::npos;
            if (!ok8) DumpMark("ctx-8", hr, mark, wImg);
            CTX(ok8, "ctx-8 InvokeCommand(0) 启动并展开 `exe \"图片\"`");

            // ctx-9：转换（带 --batch）
            DeleteFileW(wMark.c_str());
            ci.lpVerb = MAKEINTRESOURCEA(1);
            hr = ctx->InvokeCommand(&ci);
            const std::wstring mark2 = ReadMarkerWait(wMark.c_str(), 10000);
            CTX(hr == S_OK && mark2.find(L"--batch") != std::wstring::npos &&
                    mark2.find(wImg) != std::wstring::npos,
                "ctx-9 InvokeCommand(1) 展开为 `exe --batch \"图片\"`");

            // ctx-10：字符串动词（Unicode）
            DeleteFileW(wMark.c_str());
            CMINVOKECOMMANDINFOEX ciex = {};
            ciex.cbSize = sizeof(ciex);
            ciex.fMask = CMIC_MASK_UNICODE;
            ciex.lpVerbW = L"QuikView.Convert";
            hr = ctx->InvokeCommand((LPCMINVOKECOMMANDINFO)&ciex);
            const std::wstring mark3 = ReadMarkerWait(wMark.c_str(), 10000);
            CTX(hr == S_OK && mark3.find(L"--batch") != std::wstring::npos,
                "ctx-10 字符串动词 QuikView.Convert");

            // ctx-11：未知动词拒绝
            ci.lpVerb = MAKEINTRESOURCEA(7);
            const HRESULT hrBad = ctx->InvokeCommand(&ci);
            CMINVOKECOMMANDINFOEX ciex2 = {};
            ciex2.cbSize = sizeof(ciex2);
            ciex2.fMask = CMIC_MASK_UNICODE;
            ciex2.lpVerbW = L"QuikView.Bogus";
            const HRESULT hrBad2 = ctx->InvokeCommand(
                (LPCMINVOKECOMMANDINFO)&ciex2);
            CTX(hrBad == E_INVALIDARG && hrBad2 == E_INVALIDARG,
                "ctx-11 未知动词一律 E_INVALIDARG");

            SetEnvironmentVariableW(L"QUIKVIEW_FAKE_MARK", nullptr);
        }

        // ctx-12：多选只取第一张
        hr = E_FAIL;
        if (NewCtxHandler(ccf, kMulti, 3, &ctxMulti)) {
            if (haveFake) {
                SetEnvironmentVariableW(L"QUIKVIEW_FAKE_MARK", wMark.c_str());
                DeleteFileW(wMark.c_str());
                CMINVOKECOMMANDINFO ci = {};
                ci.cbSize = sizeof(ci);
                ci.lpVerb = MAKEINTRESOURCEA(0);
                hr = ctxMulti->InvokeCommand(&ci);
                const std::wstring mark4 =
                    ReadMarkerWait(wMark.c_str(), 10000);
                SetEnvironmentVariableW(L"QUIKVIEW_FAKE_MARK", nullptr);
                const bool ok12 = hr == S_OK &&
                                  mark4.find(wImg) != std::wstring::npos &&
                                  mark4.find(L"second.png") ==
                                      std::wstring::npos;
                if (!ok12) DumpMark("ctx-12", hr, mark4, wImg);
                CTX(ok12, "ctx-12 多选只取第一张");
            } else {
                printf("SKIP: ctx-12（缺 fake 替身）\n");
            }
        } else {
            CtxFail("ctx-12 多选只取第一张（建实例失败）");
        }

        // ctx-13：扩展名过滤（.txt 不注入）
        hr = E_FAIL;
        if (NewCtxHandler(ccf, kTxt, 1, &ctxTxt)) {
            HMENU menu = CreatePopupMenu();
            hr = ctxTxt->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
            const bool ok = HRESULT_CODE(hr) == 0 && GetMenuItemCount(menu) == 0;
            DestroyMenu(menu);
            CTX(ok, "ctx-13 .txt 不注入菜单");
        } else {
            CtxFail("ctx-13 .txt 不注入菜单（建实例失败）");
        }

        // ctx-14：未 Initialize 的实例不注入
        {
            IShellExtInit* sei = nullptr;
            IContextMenu* bare = nullptr;
            hr = ccf->CreateInstance(nullptr, IID_IShellExtInit, (void**)&sei);
            if (SUCCEEDED(hr) && sei) {
                sei->QueryInterface(IID_IContextMenu, (void**)&bare);
            }
            bool ok = false;
            if (bare) {
                HMENU menu = CreatePopupMenu();
                hr = bare->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                ok = HRESULT_CODE(hr) == 0 && GetMenuItemCount(menu) == 0;
                DestroyMenu(menu);
                bare->Release();
            }
            if (sei) sei->Release();
            CTX(ok, "ctx-14 未 Initialize 不注入");
        }

        // ctx-15 (B1)：浏览/转换两项带菜单位图（MIIM_BITMAP）
        if (ctx) {
            HMENU menu = CreatePopupMenu();
            ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
            int withBitmap = 0;
            const int n = GetMenuItemCount(menu);
            for (int pos = 0; pos < n; ++pos) {
                MENUITEMINFOW mii = {};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE | MIIM_BITMAP;
                const BOOL gr = GetMenuItemInfoW(menu, pos, TRUE, &mii);
                // MFT_STRING 恒为 0，不能用 & MFT_STRING 判定；直接数带 hbmp 的非分隔项
                if (gr && mii.hbmpItem && (mii.fType & MFT_SEPARATOR) == 0) {
                    ++withBitmap;
                }
            }
            DestroyMenu(menu);
            CTX(withBitmap == 2, "ctx-15 B1 菜单项带内嵌图标位图");
        } else {
            CtxFail("ctx-15 B1 带图标（建实例失败）");
        }

        // ctx-16 (B2)：浏览项文案含「浏览」；若 WIC 可读则含尺寸/大小（\t 后）
        if (ctx) {
            HMENU menu = CreatePopupMenu();
            ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
            wchar_t txt[192] = {};
            GetMenuStringW(menu, 1, txt, 192, MF_BYPOSITION);
            DestroyMenu(menu);
            bool ok = wcsstr(txt, L"浏览") != nullptr;
            printf("      ctx-16 browse text len=%u tab=%d dims=%d\n",
                   (unsigned)wcslen(txt), (int)(wcschr(txt, L'\t') != nullptr),
                   (int)(wcschr(txt, L'×') != nullptr));
            CTX(ok, "ctx-16 B2 浏览项文案（有图时右侧附 宽×高·大小）");
        } else {
            CtxFail("ctx-16 B2 尺寸文案（建实例失败）");
        }

        // ctx-17 (B3)：暴露 IContextMenu2，自绘 measure/draw 回调不崩
        if (ctx) {
            IContextMenu2* cm2 = nullptr;
            hr = ctx->QueryInterface(IID_IContextMenu2, (void**)&cm2);
            bool ok = SUCCEEDED(hr) && cm2 != nullptr;
            if (cm2) {
                // 伪造一次 measure / draw，确认回调不崩（值随意）
                MEASUREITEMSTRUCT mis = {};
                mis.CtlType = ODT_MENU;
                cm2->HandleMenuMsg(WM_MEASUREITEM, 0, (LPARAM)&mis);
                DRAWITEMSTRUCT dis = {};
                dis.CtlType = ODT_MENU;
                dis.hDC = GetDC(nullptr);
                dis.rcItem.left = dis.rcItem.top = 0;
                dis.rcItem.right = 200; dis.rcItem.bottom = 200;
                cm2->HandleMenuMsg(WM_DRAWITEM, 0, (LPARAM)&dis);
                ReleaseDC(nullptr, dis.hDC);
                cm2->Release();
            }
            CTX(ok, "ctx-17 B3 自绘缩略图项接口 IContextMenu2 可用");
        } else {
            CtxFail("ctx-17 B3 自绘接口（建实例失败）");
        }

        if (ctxTxt) ctxTxt->Release();
        if (ctxMulti) ctxMulti->Release();
        if (ctx) ctx->Release();
        ccf->Release();

        // 替身与标记清理（杀软可能短暂占用，失败等 1s 重试一轮）
        if ((!DeleteFileA(fakeDst) || !DeleteFileA(markPathA))) {
            Sleep(1000);
            DeleteFileA(fakeDst);
            DeleteFileA(markPathA);
        }
        if (FileExistsA(fakeDst) || FileExistsA(markPathA)) {
            printf("WARN: 清理 fake 替身/标记失败（%s %s）\n", fakeDst,
                   markPathA);
        }
    } else {
        CtxFail("ctx-1 后续用例（无类厂）");
        if (ccf) ccf->Release();
    }

    cf->Release();
    CoUninitialize();

    if (pCanUnloadNow() != S_OK) {
        printf("FAIL: DllCanUnloadNow != S_OK (引用计数泄漏)\n");
        g_fail++;
    } else {
        printf("OK: DllCanUnloadNow == S_OK\n");
    }
    FreeLibrary(hDll);

    const int passes = thumbPass + g_pass;
    const int fails = thumbFail + g_fail;
    printf("RESULT: %s (pass=%d fail=%d)\n", fails ? "FAIL" : "PASS", passes,
           fails);
    return fails ? 1 : 0;
}
