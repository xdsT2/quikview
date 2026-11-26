// QuikContextMenu 实现（P-19 / 批次 B）。
// 菜单项带图标（壳扩展内嵌资源，不加载 exe）、单选图右侧显示「宽×高 · 大小」、
// 顶部自绘 ≤256px 缩略图。解码超时预算 50ms，超时不出缩略图项，绝不阻塞 Explorer。
#include "contextmenu.h"

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <strsafe.h>
#include <wincodec.h>

#include "quikthumb.h"

namespace {

// 菜单命令 ID 偏移与动词名
enum { CMD_BROWSE = 0, CMD_CONVERT = 1, CMD_THUMB = 2, CMD_COUNT = 3 };
const wchar_t kVerbBrowseW[] = L"QuikView.Browse";
const wchar_t kVerbConvertW[] = L"QuikView.Convert";
const char kVerbBrowseA[] = "QuikView.Browse";
const char kVerbConvertA[] = "QuikView.Convert";

const wchar_t kMenuBrowse[] = L"用 QuikView 浏览";
const wchar_t kMenuConvert[] = L"用 QuikView 转换";
const wchar_t kMenuThumb[] = L"__qv_thumb__";   // 自绘项标识（不可见）

const wchar_t kAppRegKey[] = L"Software\\QuikView";
const UINT kThumbMaxDim = 256;     // 缩略图边长上限
const ULONGLONG kDecodeBudgetMs = 50;

bool FileExists(const wchar_t* path) {
    const DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES &&
           (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool GetInstallDirFromRegistry(HKEY root, wchar_t* dir, DWORD cch) {
    HKEY hk = nullptr;
    if (RegOpenKeyExW(root, kAppRegKey, 0,
                      KEY_QUERY_VALUE | KEY_WOW64_64KEY, &hk) != ERROR_SUCCESS) {
        return false;
    }
    static const wchar_t* const kValueNames[] = { L"InstallDir", L"InstallPath" };
    bool ok = false;
    for (const wchar_t* name : kValueNames) {
        wchar_t buf[MAX_PATH] = {};
        DWORD type = 0;
        DWORD cb = sizeof(buf);
        if (RegQueryValueExW(hk, name, nullptr, &type, (BYTE*)buf, &cb) ==
                ERROR_SUCCESS && type == REG_SZ && buf[0]) {
            lstrcpynW(dir, buf, (int)cch);
            ok = true;
            break;
        }
    }
    RegCloseKey(hk);
    return ok;
}

const wchar_t* FindExt(const wchar_t* path) {
    const wchar_t* dot = nullptr;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'.') dot = p;
        else if (*p == L'\\' || *p == L'/') dot = nullptr;
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

// 轻量 RAII（与 thumbnail.cpp 同构，不引 ATL）
template <class T>
class ComPtr {
public:
    ComPtr() : m_p(nullptr) {}
    ~ComPtr() { Release(); }
    T** operator&() { Release(); return &m_p; }
    T* Get() const { return m_p; }
    T* operator->() const { return m_p; }
    void Release() { if (m_p) { m_p->Release(); m_p = nullptr; } }
private:
    ComPtr(const ComPtr&); ComPtr& operator=(const ComPtr&);
    T* m_p;
};

// WIC 工厂不缓存：IWICImagingFactory 是 apartment 绑定对象，缓存成静态指针
// 会被资源管理器的另一条线程（缩略图提取、另一个 STA 窗口）拿到，跨套间
// 调用 WIC 属于未定义行为 —— 一旦崩在 explorer.exe 里，整个桌面跟着没。
// 改为每次弹菜单现建、返回前释放；创建成本在百微秒量级，远低于一次菜单重绘。

// 把 HICON 画进 32×32 32bpp DIB（菜单 MIIM_BITMAP 用）
HBITMAP IconToBitmap(HICON hIcon, int w, int h) {
    if (!hIcon) return nullptr;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP hb = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!hb) return nullptr;
    HDC hdc = CreateCompatibleDC(nullptr);
    HGDIOBJ old = SelectObject(hdc, hb);
    RECT rc = { 0, 0, w, h };
    FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
    DrawIconEx(hdc, 0, 0, hIcon, w, h, 0, nullptr, DI_NORMAL);
    SelectObject(hdc, old);
    DeleteDC(hdc);
    return hb;
}

// 无符号整数 → 十进制文本。不用 printf 的 %llu / %I64u：wsprintfW 不支持前者，
// 后者依赖 CRT 修饰符；整数手工拼出来最稳，也不给格式化留截断风险。
void U64ToW(ULONGLONG v, wchar_t* out, int cch) {
    if (!out || cch <= 0) return;
    wchar_t tmp[24];
    int n = 0;
    do {
        tmp[n++] = (wchar_t)(L'0' + (v % 10));
        v /= 10;
    } while (v && n < (int)_countof(tmp));
    const int digits = n;
    const int room = cch - 1;
    const int copy = digits < room ? digits : room;
    for (int i = 0; i < copy; ++i) out[i] = tmp[digits - 1 - i];
    out[copy] = L'\0';
}

// 「128.0 KB」/「512 B」/「4.0 GB」：一律整数运算，保留一位小数（四舍五入到 0.1），
// 单位按 KB/MB/GB 分档。cch 真正用于截断保护（StringCchPrintfW 的超长路径）。
void FormatSize(ULONGLONG bytes, wchar_t* out, int cch) {
    if (!out || cch <= 0) return;
    out[0] = L'\0';
    if (bytes < 1024ULL) {
        wchar_t num[24] = {};
        U64ToW(bytes, num, _countof(num));
        StringCchPrintfW(out, (size_t)cch, L"%s B", num);
        return;
    }
    const wchar_t* unit = L"KB";
    ULONGLONG div = 1024ULL;
    if (bytes >= 1024ULL * 1024 * 1024) {
        unit = L"GB";
        div = 1024ULL * 1024 * 1024;
    } else if (bytes >= 1024ULL * 1024) {
        unit = L"MB";
        div = 1024ULL * 1024;
    }
    ULONGLONG whole = bytes / div;
    const ULONGLONG rem = bytes % div;
    // rem < div ≤ 1GiB，故 rem*10 不会溢出 64 位。
    ULONGLONG tenth = (rem * 10 + div / 2) / div;
    if (tenth >= 10) { ++whole; tenth -= 10; }
    wchar_t wholeStr[24] = {};
    U64ToW(whole, wholeStr, _countof(wholeStr));
    StringCchPrintfW(out, (size_t)cch, L"%s.%u %s", wholeStr, (unsigned)tenth, unit);
}

}   // namespace

QuikContextMenu::QuikContextMenu()
    : m_ref(1), m_hasFile(false),
      m_iconBmp(nullptr), m_thumbBmp(nullptr), m_thumbW(0), m_thumbH(0),
      m_visibleCount(0), m_thumbMenuId(0) {
    m_file[0] = L'\0';
    m_infoText[0] = L'\0';
    m_idMap[0] = m_idMap[1] = m_idMap[2] = -1;
    DllAddLock();
}

QuikContextMenu::~QuikContextMenu() {
    ReleaseBitmaps();
    DllReleaseLock();
}

void QuikContextMenu::ReleaseBitmaps() {
    if (m_iconBmp) { DeleteObject(m_iconBmp); m_iconBmp = nullptr; }
    if (m_thumbBmp) { DeleteObject(m_thumbBmp); m_thumbBmp = nullptr; }
}

// IUnknown
STDMETHODIMP QuikContextMenu::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown)) {
            *ppv = static_cast<IShellExtInit*>(this);
        } else if (IsEqualIID(riid, IID_IShellExtInit)) {
            *ppv = static_cast<IShellExtInit*>(this);
        } else if (IsEqualIID(riid, IID_IContextMenu) ||
                   IsEqualIID(riid, IID_IContextMenu2)) {
            *ppv = static_cast<IContextMenu2*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

// 引用计数用原子自增/自减：资源管理器有多条线程同时持有同一个壳扩展对象
// （右键菜单、后台缩略图、不同 STA 窗口），裸 ++/-- 可能读到撕裂值后提前 delete。
// 与 thumbnail.cpp 的 QuikThumbProvider 保持同一口径。
STDMETHODIMP_(ULONG) QuikContextMenu::AddRef() {
    return (ULONG)InterlockedIncrement((LONG*)&m_ref);
}

STDMETHODIMP_(ULONG) QuikContextMenu::Release() {
    const ULONG ref = (ULONG)InterlockedDecrement((LONG*)&m_ref);
    if (ref == 0) delete this;
    return ref;
}

STDMETHODIMP QuikContextMenu::Initialize(LPCITEMIDLIST, IDataObject* pdtobj,
                                         HKEY) {
    try {
        if (!pdtobj) return E_INVALIDARG;
        FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
        STGMEDIUM stm = {};
        if (FAILED(pdtobj->GetData(&fe, &stm))) return E_FAIL;
        bool ok = false;
        if (stm.tymed == TYMED_HGLOBAL && stm.hGlobal) {
            const UINT n = DragQueryFileW((HDROP)stm.hGlobal, 0, m_file, MAX_PATH);
            ok = (n > 0 && n < MAX_PATH && m_file[0] != L'\0');
        }
        ReleaseStgMedium(&stm);
        m_hasFile = ok;
        return ok ? S_OK : E_FAIL;
    } catch (...) {
        return E_FAIL;
    }
}

// IContextMenu
STDMETHODIMP QuikContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                               UINT idCmdFirst, UINT idCmdLast,
                                               UINT uFlags) {
    try {
        if (!hmenu) return E_INVALIDARG;
        // 同实例重复弹菜单：上一轮的位图/文案/ID 表必须清零，否则偏好切换后
        // 残留旧产物（实测 info 关了还带尺寸后缀、图标关了还挂位图）
        ReleaseBitmaps();
        m_iconBmp = nullptr;
        m_thumbBmp = nullptr;
        m_thumbW = m_thumbH = 0;
        m_infoText[0] = L'\0';
        m_idMap[0] = m_idMap[1] = m_idMap[2] = -1;
        m_visibleCount = 0;
        if ((uFlags & CMF_DEFAULTONLY) || !m_hasFile ||
            !HasSupportedExt(m_file)) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }

        // —— 菜单偏好：每次现读（设置页改动在下一次弹菜单生效，无需重装）——
        const QuikMenuPrefs prefs = QuikLoadMenuPrefs();
        if (!prefs.master) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }
        // 三条可用入口全关时：不插分隔线、不占命令 ID，直接返回
        if (!prefs.browse && !prefs.convert && !prefs.preview) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }
        if (idCmdFirst + CMD_COUNT > idCmdLast) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }

        const ULONGLONG t0 = GetTickCount64();

        // 本条线程自己的 WIC 工厂（见上文「不缓存」说明）
        ComPtr<IWICImagingFactory> wicFactory;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_IWICImagingFactory, (void**)&wicFactory);

        // —— B1：菜单项小图标（资源内嵌，不加载 exe；show_icon 关时省略）——
        if (prefs.icon) {
            HICON hIcon = LoadIconW(DllGetModule(), MAKEINTRESOURCEW(101));
            m_iconBmp = IconToBitmap(hIcon, 32, 32);
        }

        // —— B2 & B3：共用一次打开的 WIC 解码器，带文件体积/尺寸安全上限 ——
        if (prefs.info || prefs.preview) {
            LARGE_INTEGER sz = {};
            bool haveSize = false;
            HANDLE hf = CreateFileW(m_file, GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, 0, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                haveSize = GetFileSizeEx(hf, &sz) != FALSE;
                CloseHandle(hf);
            }
            if (IWICImagingFactory* fac = wicFactory.Get()) {
                ComPtr<IWICBitmapDecoder> decoder;
                ComPtr<IWICBitmapFrameDecode> frame;
                UINT sw = 0, sh = 0;
                if (SUCCEEDED(fac->CreateDecoderFromFilename(
                            m_file, nullptr, GENERIC_READ,
                            WICDecodeMetadataCacheOnDemand, &decoder)) &&
                    SUCCEEDED(decoder->GetFrame(0, &frame)) &&
                    SUCCEEDED(frame->GetSize(&sw, &sh)) && sw > 0 && sh > 0) {
                    if (prefs.info) {
                        wchar_t sizeStr[48] = {};
                        FormatSize((ULONGLONG)sz.QuadPart, sizeStr, _countof(sizeStr));
                        StringCchPrintfW(m_infoText, _countof(m_infoText),
                                        L"%u×%u · %s", sw, sh, sizeStr);
                    }
                    const bool withinLimits =
                        (!haveSize || sz.QuadPart <= 16LL * 1024LL * 1024LL) &&
                        sw <= 8192 && sh <= 8192 &&
                        (UINT64)sw * (UINT64)sh <= 25000000ULL;
                    if (prefs.preview && withinLimits &&
                        GetTickCount64() - t0 <= kDecodeBudgetMs) {
                        const UINT big = sw > sh ? sw : sh;
                        double scale = (double)kThumbMaxDim / (double)big;
                        if (scale > 1.0) scale = 1.0;
                        m_thumbW = (int)(sw * scale);
                        m_thumbH = (int)(sh * scale);
                        if (m_thumbW < 1) m_thumbW = 1;
                        if (m_thumbH < 1) m_thumbH = 1;
                        ComPtr<IWICBitmapScaler> scaler;
                        if (SUCCEEDED(fac->CreateBitmapScaler(&scaler)) &&
                            SUCCEEDED(scaler->Initialize(frame.Get(), (UINT)m_thumbW,
                                                         (UINT)m_thumbH,
                                                         WICBitmapInterpolationModeFant))) {
                            ComPtr<IWICFormatConverter> conv;
                            if (SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
                                SUCCEEDED(conv->Initialize(
                                    scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                    WICBitmapDitherTypeNone, nullptr, 0.0,
                                    WICBitmapPaletteTypeCustom))) {
                                BITMAPINFO bi = {};
                                bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                                bi.bmiHeader.biWidth = m_thumbW;
                                bi.bmiHeader.biHeight = -m_thumbH;
                                bi.bmiHeader.biPlanes = 1;
                                bi.bmiHeader.biBitCount = 32;
                                bi.bmiHeader.biCompression = BI_RGB;
                                void* bits = nullptr;
                                HBITMAP hb = CreateDIBSection(nullptr, &bi,
                                            DIB_RGB_COLORS, &bits, nullptr, 0);
                                if (hb && bits) {
                                    const UINT stride = (UINT)m_thumbW * 4;
                                    if (SUCCEEDED(conv->CopyPixels(nullptr, stride,
                                                                   stride * (UINT)m_thumbH,
                                                                   (BYTE*)bits)) &&
                                        GetTickCount64() - t0 <= kDecodeBudgetMs) {
                                        m_thumbBmp = hb;
                                    } else {
                                        DeleteObject(hb);
                                    }
                                } else if (hb) {
                                    DeleteObject(hb);
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!m_thumbBmp) { m_thumbW = m_thumbH = 0; }
        m_thumbMenuId = 0;

        // 实际插入集合：预览项还取决于解码是否成功（预算超时不出）
        const bool showThumb = prefs.preview && m_thumbBmp != nullptr;
        if (!prefs.browse && !prefs.convert && !showThumb) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }

        // 命令 ID 按优先序（浏览 → 转换 → 预览）连续分配：缺省全开时与
        // 0.5.0 的 ID 布局一致（浏览 0 / 转换 1 / 预览 2）；跳过某项时
        // 后续 ID 前移，不留空洞（空洞会与同菜单的其他壳扩展撞 ID）
        m_visibleCount = 0;
        for (int c = 0; c < CMD_COUNT; ++c) m_idMap[c] = -1;
        for (int c = 0; c < CMD_COUNT; ++c) {
            const bool on = (c == CMD_BROWSE) ? prefs.browse
                          : (c == CMD_CONVERT) ? prefs.convert
                                               : showThumb;
            if (on) m_idMap[c] = m_visibleCount++;
        }

        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_FTYPE;
        mi.fType = MFT_SEPARATOR;
        InsertMenuItemW(hmenu, indexMenu, TRUE, &mi);
        int pos = indexMenu + 1;

        // 自绘缩略图项（成功才加）
        if (showThumb) {
            m_thumbMenuId = idCmdFirst + (UINT)m_idMap[CMD_THUMB];
            mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
            mi.fType = MFT_OWNERDRAW;
            mi.fState = MFS_ENABLED;
            mi.wID = m_thumbMenuId;
            mi.dwTypeData = const_cast<LPWSTR>(kMenuThumb);
            InsertMenuItemW(hmenu, pos++, TRUE, &mi);
        }

        // 浏览项：右侧 \t 显示尺寸/大小
        wchar_t browseText[192] = {};
        if (m_infoText[0]) {
            StringCchPrintfW(browseText, _countof(browseText), L"%s\t%s",
                             kMenuBrowse, m_infoText);
        } else {
            lstrcpynW(browseText, kMenuBrowse, _countof(browseText));
        }

        if (prefs.browse) {
            mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_BITMAP;
            mi.fType = MFT_STRING;
            mi.fState = MFS_ENABLED;
            mi.wID = idCmdFirst + m_idMap[CMD_BROWSE];
            mi.hbmpItem = m_iconBmp;
            mi.dwTypeData = const_cast<LPWSTR>(browseText);
            mi.cch = (UINT)lstrlenW(browseText);
            InsertMenuItemW(hmenu, pos++, TRUE, &mi);
        }
        if (prefs.convert) {
            mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_BITMAP;
            mi.fType = MFT_STRING;
            mi.fState = MFS_ENABLED;
            mi.wID = idCmdFirst + m_idMap[CMD_CONVERT];
            mi.hbmpItem = m_iconBmp;
            mi.dwTypeData = const_cast<LPWSTR>(kMenuConvert);
            mi.cch = (UINT)lstrlenW(kMenuConvert);
            InsertMenuItemW(hmenu, pos++, TRUE, &mi);
        }

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, (UINT)m_visibleCount);
    } catch (...) {
        return E_FAIL;
    }
}

STDMETHODIMP QuikContextMenu::HandleMenuMsg(UINT uMsg, WPARAM, LPARAM lParam) {
    try {
        if (uMsg == WM_MEASUREITEM && lParam) {
            MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
            if (mis->CtlType == ODT_MENU && m_thumbMenuId != 0
                && mis->itemID == m_thumbMenuId && m_thumbBmp) {
                mis->itemWidth = m_thumbW + 24;
                mis->itemHeight = m_thumbH + 16;
                return S_OK;
            }
            return S_FALSE;
        }
        if (uMsg == WM_DRAWITEM && lParam) {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_MENU && m_thumbMenuId != 0
                && dis->itemID == m_thumbMenuId && m_thumbBmp) {
                int xoff = ((dis->rcItem.right - dis->rcItem.left) - m_thumbW) / 2;
                if (xoff < 0) xoff = 0;
                int yoff = ((dis->rcItem.bottom - dis->rcItem.top) - m_thumbH) / 2;
                if (yoff < 0) yoff = 0;
                const int x = dis->rcItem.left + xoff;
                const int y = dis->rcItem.top + yoff;
                HDC hdc = CreateCompatibleDC(dis->hDC);
                if (hdc) {
                    HGDIOBJ old = SelectObject(hdc, m_thumbBmp);
                    BitBlt(dis->hDC, x, y, m_thumbW, m_thumbH, hdc, 0, 0, SRCCOPY);
                    SelectObject(hdc, old);
                    DeleteDC(hdc);
                }
                return S_OK;
            }
            return S_FALSE;
        }
        return E_NOTIMPL;
    } catch (...) {
        return E_FAIL;
    }
}

// 可见项命令表：偏移 → CMD_*。偏移按 QueryContextMenu 里的优先序
// （浏览 → 转换 → 预览）连续分配；越界返回 -1。
int QuikContextMenu::CmdAtOffset(UINT offset) const {
    if (m_visibleCount <= 0) {
        // 尚未 QueryContextMenu（Explorer 契约外；selftest 直发 InvokeCommand）：
        // 按当前偏好推默认映射（预览项按偏好算，不验解码成败）
        const QuikMenuPrefs prefs = QuikLoadMenuPrefs();
        int used = 0;
        for (int c = 0; c < CMD_COUNT; ++c) {
            const bool on = (c == CMD_BROWSE) ? prefs.browse
                          : (c == CMD_CONVERT) ? prefs.convert
                                               : prefs.preview;
            if (on) {
                if (used == (int)offset) return c;
                ++used;
            }
        }
        return -1;
    }
    if (offset >= (UINT)m_visibleCount) return -1;
    for (int c = 0; c < CMD_COUNT; ++c) {
        if (m_idMap[c] == (int)offset) return c;
    }
    return -1;
}

STDMETHODIMP QuikContextMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici) {
    try {
        if (!pici) return E_INVALIDARG;
        int cmd = -1;
        const wchar_t* verbW = nullptr;
        if (pici->cbSize >= sizeof(CMINVOKECOMMANDINFOEX) &&
            (pici->fMask & CMIC_MASK_UNICODE)) {
            verbW = ((LPCMINVOKECOMMANDINFOEX)pici)->lpVerbW;
        }
        if (verbW && !IS_INTRESOURCE(verbW)) {
            if (lstrcmpiW(verbW, kVerbBrowseW) == 0) cmd = CMD_BROWSE;
            else if (lstrcmpiW(verbW, kVerbConvertW) == 0) cmd = CMD_CONVERT;
        } else if (pici->lpVerb == nullptr) {
            // 默认动词 = 第一个可见项（优先序最前）；仅预览可见时点按 = 浏览
            const int at = CmdAtOffset(0);
            if (at == CMD_BROWSE || at == CMD_CONVERT) cmd = at;
            else if (at == CMD_THUMB) cmd = CMD_BROWSE;
        } else if (IS_INTRESOURCE(pici->lpVerb)) {
            cmd = CmdAtOffset(LOWORD((UINT_PTR)pici->lpVerb));
        } else {
            if (lstrcmpiA(pici->lpVerb, kVerbBrowseA) == 0) cmd = CMD_BROWSE;
            else if (lstrcmpiA(pici->lpVerb, kVerbConvertA) == 0) cmd = CMD_CONVERT;
        }
        if (cmd == CMD_THUMB) cmd = CMD_BROWSE;   // 点缩略图 = 浏览
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
        const int cmd = CmdAtOffset((UINT)idCmd);
        if (uType == GCS_VALIDATEA || uType == GCS_VALIDATEW) {
            return cmd >= 0 ? S_OK : E_FAIL;
        }
        if (cmd < 0) return E_INVALIDARG;
        if (cmd == CMD_THUMB) return E_NOTIMPL;   // 自绘项无动词
        if (uType == GCS_VERBW) {
            const wchar_t* verb =
                (cmd == CMD_BROWSE) ? kVerbBrowseW : kVerbConvertW;
            if (!pszName || cchMax < (UINT)lstrlenW(verb) + 1) return E_FAIL;
            lstrcpynW((LPWSTR)pszName, verb, (int)cchMax);
            return S_OK;
        }
        if (uType == GCS_VERBA) {
            const char* verb = (cmd == CMD_BROWSE) ? kVerbBrowseA : kVerbConvertA;
            if (!pszName || cchMax < (UINT)lstrlenA(verb) + 1) return E_FAIL;
            lstrcpynA(pszName, verb, (int)cchMax);
            return S_OK;
        }
        return E_NOTIMPL;
    } catch (...) {
        return E_FAIL;
    }
}

bool QuikContextMenu::FindQuikViewExe(wchar_t* exePath, DWORD cch) const {
    wchar_t buf[MAX_PATH] = {};
    if (DllGetModule() &&
        GetModuleFileNameW(DllGetModule(), buf, MAX_PATH) > 0) {
        wchar_t* slash = wcsrchr(buf, L'\\');
        if (slash) {
            lstrcpynW(slash + 1, L"quikview.exe",
                      (int)(MAX_PATH - (slash + 1 - buf)));
            if (FileExists(buf)) { lstrcpynW(exePath, buf, (int)cch); return true; }
        }
    }
    const HKEY kRoots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (const HKEY root : kRoots) {
        wchar_t dir[MAX_PATH] = {};
        if (!GetInstallDirFromRegistry(root, dir, MAX_PATH)) continue;
        const size_t len = lstrlenW(dir);
        if (len && dir[len - 1] == L'\\') dir[len - 1] = L'\0';
        if (FAILED(StringCchPrintfW(buf, _countof(buf), L"%s\\quikview.exe", dir))) continue;
        if (FileExists(buf)) { lstrcpynW(exePath, buf, (int)cch); return true; }
    }
    return false;
}

HRESULT QuikContextMenu::Launch(bool batch) const {
    if (!m_hasFile || wcschr(m_file, L'"') != nullptr) return E_FAIL;
    wchar_t exe[MAX_PATH] = {};
    if (!FindQuikViewExe(exe, MAX_PATH)) return E_FAIL;
    wchar_t cmdLine[MAX_PATH * 2 + 32] = {};
    if (FAILED(StringCchPrintfW(cmdLine, _countof(cmdLine),
                                L"\"%s\" %s\"%s\"",
                                exe, batch ? L"--batch " : L"", m_file))) {
        return E_FAIL;
    }
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe, cmdLine, nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &si, &pi)) {
        return E_FAIL;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return S_OK;
}
