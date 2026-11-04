// QuikContextMenu 实现（P-19 / 批次 B）。
// 菜单项带图标（壳扩展内嵌资源，不加载 exe）、单选图右侧显示「宽×高 · 大小」、
// 顶部自绘 ≤256px 缩略图。解码超时预算 50ms，超时不出缩略图项，绝不阻塞 Explorer。
#include "contextmenu.h"

#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
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

IWICImagingFactory* WicFactory() {
    static IWICImagingFactory* s_factory = nullptr;
    if (!s_factory) {
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                        IID_IWICImagingFactory, (void**)&s_factory);
    }
    return s_factory;
}

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

void FormatSize(ULONGLONG bytes, wchar_t* out, int cch) {
    if (bytes < 1024ULL) {
        wsprintfW(out, L"%llu B", bytes);
    } else if (bytes < 1024ULL * 1024) {
        wsprintfW(out, L"%.1f KB", (double)bytes / 1024.0);
    } else {
        wsprintfW(out, L"%.1f MB", (double)bytes / (1024.0 * 1024.0));
    }
    (void)cch;
}

}   // namespace

QuikContextMenu::QuikContextMenu()
    : m_ref(1), m_hasFile(false),
      m_iconBmp(nullptr), m_thumbBmp(nullptr), m_thumbW(0), m_thumbH(0) {
    m_file[0] = L'\0';
    m_infoText[0] = L'\0';
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

STDMETHODIMP_(ULONG) QuikContextMenu::AddRef() { return ++m_ref; }

STDMETHODIMP_(ULONG) QuikContextMenu::Release() {
    const ULONG ref = --m_ref;
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
        if (HGLOBAL hg = stm.hGlobal) {
            if (const DROPFILES* df = (const DROPFILES*)GlobalLock(hg)) {
                const char* base = (const char*)df + df->pFiles;
                if (df->fWide) {
                    const wchar_t* files = (const wchar_t*)base;
                    if (files[0]) { lstrcpynW(m_file, files, MAX_PATH); ok = true; }
                } else {
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

// IContextMenu
STDMETHODIMP QuikContextMenu::QueryContextMenu(HMENU hmenu, UINT indexMenu,
                                               UINT idCmdFirst, UINT idCmdLast,
                                               UINT uFlags) {
    try {
        if (!hmenu) return E_INVALIDARG;
        if ((uFlags & CMF_DEFAULTONLY) || !m_hasFile ||
            !HasSupportedExt(m_file)) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }
        if (idCmdFirst + CMD_COUNT > idCmdLast) {
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }

        const ULONGLONG t0 = GetTickCount64();

        // —— B1：菜单项小图标（资源内嵌，不加载 exe）——
        HICON hIcon = LoadIconW(DllGetModule(), MAKEINTRESOURCEW(101));
        m_iconBmp = IconToBitmap(hIcon, 32, 32);

        // —— B2：单选图「宽×高 · 文件大小」——
        if (IWICImagingFactory* fac = WicFactory()) {
            ComPtr<IWICBitmapDecoder> decoder;
            if (SUCCEEDED(fac->CreateDecoderFromFilename(
                        m_file, nullptr, 0,
                        WICDecodeMetadataCacheOnDemand, &decoder))) {
                ComPtr<IWICBitmapFrameDecode> frame;
                if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                    UINT w = 0, h = 0;
                    if (SUCCEEDED(frame->GetSize(&w, &h)) && w && h) {
                        LARGE_INTEGER sz = {};
                        HANDLE hf = CreateFileW(m_file, GENERIC_READ, FILE_SHARE_READ,
                                                nullptr, OPEN_EXISTING, 0, nullptr);
                        if (hf != INVALID_HANDLE_VALUE) {
                            GetFileSizeEx(hf, &sz);
                            CloseHandle(hf);
                        }
                        wchar_t sizeStr[48] = {};
                        FormatSize(sz.QuadPart, sizeStr, _countof(sizeStr));
                        wsprintfW(m_infoText, L"%u×%u · %s", w, h, sizeStr);
                    }
                }
            }
        }

        // —— B3：自绘缩略图（≤256px，超 50ms 预算就放弃）——
        if (GetTickCount64() - t0 <= kDecodeBudgetMs) {
            if (IWICImagingFactory* fac = WicFactory()) {
                ComPtr<IWICBitmapDecoder> decoder;
                if (SUCCEEDED(fac->CreateDecoderFromFilename(
                            m_file, nullptr, 0,
                            WICDecodeMetadataCacheOnDemand, &decoder))) {
                    ComPtr<IWICBitmapFrameDecode> frame;
                    if (SUCCEEDED(decoder->GetFrame(0, &frame))) {
                        UINT sw = 0, sh = 0;
                        frame->GetSize(&sw, &sh);
                        if (sw && sh) {
                            const UINT big = sw > sh ? sw : sh;
                            double scale = (double)kThumbMaxDim / (double)big;
                            if (scale > 1.0) scale = 1.0;
                            m_thumbW = (int)(sw * scale);
                            m_thumbH = (int)(sh * scale);
                            ComPtr<IWICBitmapScaler> scaler;
                            if (SUCCEEDED(fac->CreateBitmapScaler(&scaler)) &&
                                SUCCEEDED(scaler->Initialize(frame.Get(), m_thumbW,
                                                             m_thumbH,
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
                                    if (hb) {
                                        const UINT stride = m_thumbW * 4;
                                        conv->CopyPixels(nullptr, stride,
                                                         stride * m_thumbH,
                                                         (BYTE*)bits);
                                        if (GetTickCount64() - t0 <= kDecodeBudgetMs) {
                                            m_thumbBmp = hb;
                                        } else {
                                            DeleteObject(hb);
                                            m_thumbW = m_thumbH = 0;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        if (!m_thumbBmp) { m_thumbW = m_thumbH = 0; }

        MENUITEMINFOW mi = {};
        mi.cbSize = sizeof(mi);
        mi.fMask = MIIM_FTYPE;
        mi.fType = MFT_SEPARATOR;
        InsertMenuItemW(hmenu, indexMenu, TRUE, &mi);
        int pos = indexMenu + 1;

        // 自绘缩略图项（成功才加）
        if (m_thumbBmp) {
            mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_TYPE | MIIM_STATE | MIIM_DATA;
            mi.fType = MFT_OWNERDRAW;
            mi.fState = MFS_ENABLED;
            mi.wID = idCmdFirst + CMD_THUMB;
            mi.dwTypeData = const_cast<LPWSTR>(kMenuThumb);
            InsertMenuItemW(hmenu, pos++, TRUE, &mi);
        }

        // 浏览项：右侧 \t 显示尺寸/大小
        wchar_t browseText[192] = {};
        if (m_infoText[0]) {
            wsprintfW(browseText, L"%s\t%s", kMenuBrowse, m_infoText);
        } else {
            lstrcpynW(browseText, kMenuBrowse, _countof(browseText));
        }

        const wchar_t* items[2] = { browseText, kMenuConvert };
        for (int i = 0; i < 2; ++i) {
            mi = {};
            mi.cbSize = sizeof(mi);
            mi.fMask = MIIM_ID | MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_BITMAP;
            mi.fType = MFT_STRING;
            mi.fState = MFS_ENABLED;
            mi.wID = idCmdFirst + (i == 0 ? CMD_BROWSE : CMD_CONVERT);
            mi.hbmpItem = m_iconBmp;
            mi.dwTypeData = const_cast<LPWSTR>(items[i]);
            mi.cch = (UINT)lstrlenW(items[i]);
            InsertMenuItemW(hmenu, pos++, TRUE, &mi);
        }

        const UINT count = (m_thumbBmp ? 3u : 2u);
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, count);
    } catch (...) {
        return E_FAIL;
    }
}

STDMETHODIMP QuikContextMenu::HandleMenuMsg(UINT uMsg, WPARAM, LPARAM lParam) {
    try {
        if (uMsg == WM_MEASUREITEM && lParam) {
            MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
            if (mis->CtlType == ODT_MENU && m_thumbBmp) {
                mis->itemWidth = m_thumbW + 24;
                mis->itemHeight = m_thumbH + 16;
            }
            return TRUE;
        }
        if (uMsg == WM_DRAWITEM && lParam) {
            DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
            if (dis->CtlType == ODT_MENU && m_thumbBmp) {
                int xoff = ((dis->rcItem.right - dis->rcItem.left) - m_thumbW) / 2;
                if (xoff < 0) xoff = 0;
                int yoff = ((dis->rcItem.bottom - dis->rcItem.top) - m_thumbH) / 2;
                if (yoff < 0) yoff = 0;
                const int x = dis->rcItem.left + xoff;
                const int y = dis->rcItem.top + yoff;
                HDC hdc = CreateCompatibleDC(dis->hDC);
                HGDIOBJ old = SelectObject(hdc, m_thumbBmp);
                BitBlt(dis->hDC, x, y, m_thumbW, m_thumbH, hdc, 0, 0, SRCCOPY);
                SelectObject(hdc, old);
                DeleteDC(hdc);
            }
            return TRUE;
        }
        return E_NOTIMPL;
    } catch (...) {
        return E_FAIL;
    }
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
            cmd = CMD_BROWSE;
        } else if (IS_INTRESOURCE(pici->lpVerb)) {
            cmd = LOWORD((UINT_PTR)pici->lpVerb);
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
        if (idCmd >= CMD_COUNT) return E_INVALIDARG;
        if (uType == GCS_VALIDATEA || uType == GCS_VALIDATEW) return S_OK;
        if (idCmd == CMD_THUMB) return E_NOTIMPL;   // 自绘项无动词
        if (uType == GCS_VERBW) {
            const wchar_t* verb =
                (idCmd == CMD_BROWSE) ? kVerbBrowseW : kVerbConvertW;
            if (!pszName || cchMax < (UINT)lstrlenW(verb) + 1) return E_FAIL;
            lstrcpynW((LPWSTR)pszName, verb, (int)cchMax);
            return S_OK;
        }
        if (uType == GCS_VERBA) {
            const char* verb = (idCmd == CMD_BROWSE) ? kVerbBrowseA : kVerbConvertA;
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
        wsprintfW(buf, L"%s\\quikview.exe", dir);
        if (FileExists(buf)) { lstrcpynW(exePath, buf, (int)cch); return true; }
    }
    return false;
}

HRESULT QuikContextMenu::Launch(bool batch) const {
    wchar_t exe[MAX_PATH] = {};
    if (!FindQuikViewExe(exe, MAX_PATH)) return E_FAIL;
    wchar_t cmdLine[MAX_PATH * 2 + 32] = {};
    wsprintfW(cmdLine, L"\"%s\" %s\"%s\"", exe, batch ? L"--batch " : L"", m_file);
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
