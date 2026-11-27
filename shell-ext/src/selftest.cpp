// thumb_selftest：无注册表加载自测。
// LoadLibrary(DLL) → DllGetClassObject 直接拿类厂 → CreateInstance →
//   缩略图：SHCreateStreamOnFileW 喂图片流 → GetThumbnail → 校验 HBITMAP 尺寸；
//   上下文菜单：自建 CF_HDROP 数据对象 → Initialize → QueryContextMenu 枚举
//   菜单项 → GetCommandString 动词 → InvokeCommand（soleview.exe 由
//   ctx_fake_soleview.exe 顶替，写标记文件供断言，不会拉起真实应用）。
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
#include <vector>

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
// 失败」用例，避免把真实安装的 soleview.exe / quikview.exe 拉起来。
bool InstallDirConfigured() {
    static const wchar_t* const kKeys[] = { L"Software\\SoleView", L"Software\\QuikView" };
    static const wchar_t* const kNames[] = { L"InstallDir", L"InstallPath" };
    const HKEY kRoots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (const wchar_t* regKey : kKeys) {
        for (const HKEY root : kRoots) {
            HKEY hk = nullptr;
            if (RegOpenKeyExW(root, regKey, 0,
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

// 偏好用例助手：写临时 config.ini 的 [config] ctxmenu\<name>=<value>。
// 与 Qt QSettings IniFormat 的落盘形态一致（键内 '/' 转义成 '\'）。
bool WriteCtxCfg(const wchar_t* ini, const wchar_t* name, const wchar_t* value) {
    const std::wstring key = std::wstring(L"ctxmenu\\") + name;
    return WritePrivateProfileStringW(L"config", key.c_str(), value, ini) != 0;
}

// ===== SVG 用例助手（B3 遗留：内容嗅探 + nanosvg 软渲染）=====

// 单个文件 → GetThumbnail 全链路（建实例 → 开流 → Load → QI → GetThumbnail）
bool ThumbOfFile(IClassFactory* cf, const wchar_t* path, UINT cx,
                 HBITMAP* phbm, WTS_ALPHATYPE* pat) {
    *phbm = nullptr;
    *pat = WTSAT_UNKNOWN;
    QvInitializeWithStream* init = nullptr;
    HRESULT hr = cf->CreateInstance(nullptr, IID_IInitializeWithStream,
                                    (void**)&init);
    if (FAILED(hr) || !init) return false;
    IStream* st = nullptr;
    bool ok = false;
    hr = SHCreateStreamOnFileW(path, STGM_READ | STGM_SHARE_DENY_WRITE, &st);
    if (SUCCEEDED(hr) && st && SUCCEEDED(init->Load(st, STGM_READ))) {
        IThumbnailProvider* tp = nullptr;
        if (SUCCEEDED(init->QueryInterface(IID_IThumbnailProvider,
                                          (void**)&tp)) &&
            tp) {
            ok = SUCCEEDED(tp->GetThumbnail(cx, phbm, pat)) && *phbm != nullptr;
            tp->Release();
        }
    }
    if (st) st->Release();
    init->Release();
    return ok;
}

// 取整帧像素（自顶向下 32bpp BGRA），采样断言用
bool ReadPixels(HBITMAP hbm, std::vector<BYTE>* out) {
    BITMAP bm = {};
    if (!hbm || !GetObjectW(hbm, sizeof(bm), &bm)) return false;
    if (bm.bmWidth <= 0 || bm.bmHeight <= 0 || bm.bmBitsPixel != 32) return false;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = bm.bmWidth;
    bi.bmiHeader.biHeight = -bm.bmHeight;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out->assign((size_t)bm.bmWidth * bm.bmHeight * 4, 0);
    HDC dc = GetDC(nullptr);
    const int lines =
        GetDIBits(dc, hbm, 0, (UINT)bm.bmHeight, out->data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    return lines == bm.bmHeight;
}

struct PxB {
    int b, g, r, a;   // 预乘 BGRA（不透明处 alpha=255，分量即原色）
};

PxB PixelAt(const std::vector<BYTE>& px, int w, int x, int y) {
    const BYTE* p = &px[((size_t)y * w + x) * 4];
    return PxB{p[0], p[1], p[2], p[3]};
}

bool Near(int v, int want, int tol) {
    return (v > want ? v - want : want - v) <= tol;
}

// 「通道差」类断言用：判断某像素偏红/偏蓝（渐变两端）
bool Dominant(const PxB& c, char ch, int margin) {
    if (ch == 'r') return c.r > c.b + margin;
    if (ch == 'b') return c.b > c.r + margin;
    return false;
}

// SVG 夹具逐条断言。dir = testdata 目录（--svg-dir=），tmpDir = DLL 所在目录
// （临时文件只写这里，跑完即删，不往仓库里落东西）。
void RunSvgCases(IClassFactory* cf, const char* dir, const char* tmpDir) {
    printf("--- SVG ---\n");
    if (!dir || !*dir) {
        printf("SKIP: svg-1..8（未传 --svg-dir=<shell-ext/testdata>）\n");
        return;
    }
    const std::string base(dir);
    auto pathOf = [&](const char* name) {
        return ToWide((base + "\\" + name).c_str());
    };

    HBITMAP hbm = nullptr;
    WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
    std::vector<BYTE> px;
    BITMAP bm = {};

    // svg-1：基本夹具 256px —— 尺寸/位深/alpha 类型 + 两处颜色采样
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-basic.svg").c_str(),
                                    256, &hbm, &alpha);
        bool pass = false;
        if (ok && GetObjectW(hbm, sizeof(bm), &bm) && bm.bmWidth == 256 &&
            bm.bmHeight == 256 && bm.bmBitsPixel == 32 &&
            alpha == WTSAT_ARGB && ReadPixels(hbm, &px)) {
            const PxB corner = PixelAt(px, 256, 2, 2);       // 白底
            const PxB center = PixelAt(px, 256, 128, 128);   // 蓝圆
            pass = Near(corner.r, 255, 8) && Near(corner.g, 255, 8) &&
                   Near(corner.b, 255, 8) && corner.a == 255 &&
                   Near(center.r, 0x1f, 10) && Near(center.g, 0x6f, 10) &&
                   Near(center.b, 0xeb, 10) && center.a == 255;
            if (!pass) {
                printf("      diag svg-1 corner=%d,%d,%d,%d center=%d,%d,%d,%d\n",
                       corner.b, corner.g, corner.r, corner.a, center.b,
                       center.g, center.r, center.a);
            }
        } else {
            printf("      diag svg-1 ok=%d w=%ld h=%ld bpp=%d alpha=%d\n",
                   (int)ok, bm.bmWidth, bm.bmHeight, bm.bmBitsPixel, (int)alpha);
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-1 基本夹具 256px：白底+蓝圆颜色/尺寸/ARGB");
    }

    // svg-2：同一夹具 64px —— Explorer 小图标的请求尺度
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-basic.svg").c_str(), 64,
                                    &hbm, &alpha);
        bm = BITMAP();
        const bool pass = ok && GetObjectW(hbm, sizeof(bm), &bm) &&
                          bm.bmWidth == 64 && bm.bmHeight == 64 &&
                          bm.bmBitsPixel == 32 && alpha == WTSAT_ARGB;
        if (!pass) {
            printf("      diag svg-2 ok=%d w=%ld h=%ld bpp=%d alpha=%d\n",
                   (int)ok, bm.bmWidth, bm.bmHeight, bm.bmBitsPixel, (int)alpha);
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-2 基本夹具 64px 出图");
    }

    // svg-3：透明样张 —— 四角全透明、圆心不透明（预乘 alpha 正确）
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-alpha.svg").c_str(),
                                    256, &hbm, &alpha);
        bool pass = false;
        if (ok && ReadPixels(hbm, &px)) {
            const PxB tl = PixelAt(px, 256, 1, 1);
            const PxB br = PixelAt(px, 256, 254, 254);
            const PxB c = PixelAt(px, 256, 128, 128);
            pass = tl.a == 0 && br.a == 0 && c.a == 255 &&
                   Near(c.r, 0xe0, 12) && Near(c.b, 0x52, 12);
            // 预乘校验：不透明处分量就该等于原色（上面已查），半透明边缘
            // 分量不得超过 alpha（预乘定义）
            if (pass) {
                bool premulOk = true;
                int checked = 0;
                for (int y = 0; y < 256; y += 4) {
                    for (int x = 0; x < 256; x += 4) {
                        const PxB p = PixelAt(px, 256, x, y);
                        if (p.a > 0 && p.a < 255) {
                            ++checked;
                            if (p.r > p.a || p.g > p.a || p.b > p.a)
                                premulOk = false;
                        }
                    }
                }
                printf("      svg-3 半透明采样点=%d premul=%d\n", checked,
                       (int)premulOk);
                pass = premulOk && checked > 0;
            }
            if (!pass) {
                printf("      diag svg-3 corner=(a=%d,%d) center=(%d,%d,%d,a=%d)\n",
                       tl.a, br.a, c.b, c.g, c.r, c.a);
            }
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-3 透明样张：角落 alpha=0、中心不透明、边缘已预乘");
    }

    // svg-4：渐变样张 —— 线性渐变两端颜色必须不同且方向正确
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-gradient.svg").c_str(),
                                    256, &hbm, &alpha);
        bool pass = false;
        if (ok && ReadPixels(hbm, &px)) {
            const PxB left = PixelAt(px, 256, 4, 128);
            const PxB right = PixelAt(px, 256, 251, 128);
            pass = left.a == 255 && right.a == 255 && Dominant(left, 'r', 40) &&
                   Dominant(right, 'b', 40);
            if (!pass) {
                printf("      diag svg-4 left=%d,%d,%d,%d right=%d,%d,%d,%d\n",
                       left.b, left.g, left.r, left.a, right.b, right.g,
                       right.r, right.a);
            }
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-4 渐变样张：线性渐变红→蓝两端生效");
    }

    // svg-5：文字样张 —— 形状色带渲染、<text> 不渲染（nanosvg 无字体栈）
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-text.svg").c_str(),
                                    256, &hbm, &alpha);
        bool pass = false;
        if (ok && ReadPixels(hbm, &px)) {
            const PxB band = PixelAt(px, 256, 128, 40);   // svg y≈80
            const PxB text1 = PixelAt(px, 256, 40, 140);  // svg y≈280
            const PxB text2 = PixelAt(px, 256, 200, 140);
            pass = band.a == 255 && Near(band.r, 0x33, 8) &&
                   Near(band.g, 0x33, 8) && Near(band.b, 0x33, 8) &&
                   text1.a == 0 && text2.a == 0;
            if (!pass) {
                printf("      diag svg-5 band=(%d,%d,%d,a=%d) text=(a=%d,%d)\n",
                       band.b, band.g, band.r, band.a, text1.a, text2.a);
            }
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-5 文字样张：色带渲染、<text> 不渲染（局限记录）");
    }

    // svg-6：viewBox 50000px —— 不按原始尺寸分配位图，正常出 256
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-huge.svg").c_str(), 256,
                                    &hbm, &alpha);
        bool pass = false;
        if (ok && GetObjectW(hbm, sizeof(bm), &bm) && ReadPixels(hbm, &px)) {
            const PxB c = PixelAt(px, 256, 128, 128);
            pass = bm.bmWidth == 256 && bm.bmHeight == 256 && c.a == 255 &&
                   Near(c.r, 0x11, 10) && Near(c.b, 0x33, 10);
            if (!pass) {
                printf("      diag svg-6 w=%ld h=%ld center=(%d,%d,%d,%d)\n",
                       bm.bmWidth, bm.bmHeight, c.b, c.g, c.r, c.a);
            }
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-6 viewBox 50000px：按目标尺度出图，不炸尺寸");
    }

    // svg-7：非 SVG 文本不被误判（嗅探为假 → WIC 也解不了 → 失败）
    {
        char negPath[MAX_PATH] = {};
        snprintf(negPath, sizeof(negPath), "%s\\svg_sniff_neg.txt", tmpDir);
        FILE* f = fopen(negPath, "wb");
        bool wrote = false;
        if (f) {
            wrote = fputs("plain text, no markup at all <svx>\n", f) >= 0;
            fclose(f);
        }
        hbm = nullptr;
        alpha = WTSAT_UNKNOWN;
        HBITMAP hbm2 = nullptr;
        WTS_ALPHATYPE alpha2 = WTSAT_UNKNOWN;
        const bool ok = wrote && !ThumbOfFile(cf, ToWide(negPath).c_str(), 256,
                                             &hbm2, &alpha2);
        const char* why = !wrote ? "临时文件写失败" : (ok ? "" : "竟然出图");
        if (hbm2) DeleteObject(hbm2);
        DeleteFileA(negPath);
        CTX(ok, "svg-7 非 SVG 文本不误判（嗅探为假）");
        if (!ok) printf("      diag svg-7 %s\n", why);
    }

    // svg-8：超大 cx 被夹到单边上限 2048（防内存炸）
    {
        const bool ok = ThumbOfFile(cf, pathOf("test-svg-basic.svg").c_str(),
                                    4096, &hbm, &alpha);
        bm = BITMAP();
        const bool pass = ok && GetObjectW(hbm, sizeof(bm), &bm) &&
                          bm.bmWidth == 2048 && bm.bmHeight == 2048;
        if (!pass) {
            printf("      diag svg-8 ok=%d w=%ld h=%ld（期望 2048）\n", (int)ok,
                   bm.bmWidth, bm.bmHeight);
        }
        if (hbm) DeleteObject(hbm);
        hbm = nullptr;
        CTX(pass, "svg-8 cx=4096 被夹到 2048 单边上限");
    }
}

}   // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃也能看到已打印的部分
    if (argc < 3) {
        printf("usage: thumb_selftest.exe <solethumb.dll> <image> [image...]\n");
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
        // --svg-dir=<dir> 是 SVG 用例的夹具目录，不是图片
        if (strncmp(argv[i], "--svg-dir=", 10) == 0) continue;
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

    // ===== SVG（B3 遗留：内容嗅探 + nanosvg 软渲染）=====
    {
        const char* svgDir = nullptr;
        for (int i = 1; i < argc; ++i) {
            if (strncmp(argv[i], "--svg-dir=", 10) == 0) svgDir = argv[i] + 10;
        }
        char svgTmpDir[MAX_PATH] = {};
        GetFullPathNameA(argv[1], MAX_PATH, svgTmpDir, nullptr);
        DirOfA(svgTmpDir);
        RunSvgCases(cf, svgDir, svgTmpDir);
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

        // ctx-4：QueryContextMenu → 分隔线 + 「浏览」+「转换」。
        // B3 缩略图项只在 WIC 解码成功（且 50ms 预算内）时插入，位置在分隔线
        // 之后、浏览项之前——用宽容断言：分隔线 + 两项文字存在，位置不限。
        if (ctx) {
            HMENU menu = CreatePopupMenu();
            hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
            bool ok = (hr & 0xFFFF) >= 2;   // SEVERITY_SUCCESS + 注入项数 ≥2
            bool sep = false, browse = false, convert = false;
            wchar_t txt[128] = {};
            const int cnt = GetMenuItemCount(menu);
            for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                MENUITEMINFOW mii = {};
                mii.cbSize = sizeof(mii);
                mii.fMask = MIIM_FTYPE | MIIM_STRING;
                mii.dwTypeData = txt;
                mii.cch = 127;
                if (!GetMenuItemInfoW(menu, pos, TRUE, &mii)) continue;
                if (mii.fType == MFT_SEPARATOR) sep = true;
                if (wcsstr(txt, L"浏览")) browse = true;
                if (wcsstr(txt, L"转换")) convert = true;
            }
            ok = ok && sep && browse && convert;
            if (!ok) {
                printf("      diag: hr=0x%08lX count=%d sep=%d browse=%d "
                       "convert=%d\n",
                       (unsigned long)hr, cnt, (int)sep, (int)browse,
                       (int)convert);
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
            bool ok = SUCCEEDED(hr) && wcscmp(wverb, L"SoleView.Browse") == 0;
            hr = ctx->GetCommandString(1, GCS_VERBW, nullptr, (CHAR*)wverb, 64);
            ok = ok && SUCCEEDED(hr) &&
                 wcscmp(wverb, L"SoleView.Convert") == 0;
            hr = ctx->GetCommandString(0, GCS_VERBA, nullptr, averb, 64);
            ok = ok && SUCCEEDED(hr) && strcmp(averb, "SoleView.Browse") == 0;
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
        snprintf(fakeSrc, sizeof(fakeSrc), "%s\\ctx_fake_soleview.exe", selfDir);
        snprintf(fakeDst, sizeof(fakeDst), "%s\\soleview.exe", dllDir);
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

        // ctx-7：找不到 soleview.exe 时 InvokeCommand 静默失败（不弹窗不崩）
        if (InstallDirConfigured()) {
            printf("SKIP: ctx-7（本机已配置 InstallDir/InstallPath，"
                   "跳过「找不到 exe 必失败」用例）\n");
        } else {
            CMINVOKECOMMANDINFO ci = {};
            ci.cbSize = sizeof(ci);
            ci.lpVerb = MAKEINTRESOURCEA(0);
            hr = ctx ? ctx->InvokeCommand(&ci) : S_OK;
            CTX(ctx && FAILED(hr), "ctx-7 无 soleview.exe 时静默失败");
        }

        // fake 替身就位后：InvokeCommand 真启动进程并核对命令行
        const bool haveFake = FileExistsA(fakeSrc) &&
                              CopyFileA(fakeSrc, fakeDst, FALSE);
        if (!haveFake) {
            printf("SKIP: ctx-8..12（缺 ctx_fake_soleview.exe，"
                   "先构建 --target ctx_fake_soleview）\n");
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
            SetEnvironmentVariableW(L"SOLEVIEW_FAKE_MARK", wMark.c_str());

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
            ciex.lpVerbW = L"SoleView.Convert";
            hr = ctx->InvokeCommand((LPCMINVOKECOMMANDINFO)&ciex);
            const std::wstring mark3 = ReadMarkerWait(wMark.c_str(), 10000);
            CTX(hr == S_OK && mark3.find(L"--batch") != std::wstring::npos,
                "ctx-10 字符串动词 SoleView.Convert");

            // ctx-11：未知动词拒绝
            ci.lpVerb = MAKEINTRESOURCEA(7);
            const HRESULT hrBad = ctx->InvokeCommand(&ci);
            CMINVOKECOMMANDINFOEX ciex2 = {};
            ciex2.cbSize = sizeof(ciex2);
            ciex2.fMask = CMIC_MASK_UNICODE;
            ciex2.lpVerbW = L"SoleView.Bogus";
            const HRESULT hrBad2 = ctx->InvokeCommand(
                (LPCMINVOKECOMMANDINFO)&ciex2);
            CTX(hrBad == E_INVALIDARG && hrBad2 == E_INVALIDARG,
                "ctx-11 未知动词一律 E_INVALIDARG");

            SetEnvironmentVariableW(L"SOLEVIEW_FAKE_MARK", nullptr);
        }

        // ctx-12：多选只取第一张
        hr = E_FAIL;
        if (NewCtxHandler(ccf, kMulti, 3, &ctxMulti)) {
            if (haveFake) {
                SetEnvironmentVariableW(L"SOLEVIEW_FAKE_MARK", wMark.c_str());
                DeleteFileW(wMark.c_str());
                CMINVOKECOMMANDINFO ci = {};
                ci.cbSize = sizeof(ci);
                ci.lpVerb = MAKEINTRESOURCEA(0);
                hr = ctxMulti->InvokeCommand(&ci);
                const std::wstring mark4 =
                    ReadMarkerWait(wMark.c_str(), 10000);
                SetEnvironmentVariableW(L"SOLEVIEW_FAKE_MARK", nullptr);
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
            // 缩略图项（ownerdraw）可能占位置 1，按文字找浏览项，不假设位置
            wchar_t txt[192] = {};
            const int items16 = GetMenuItemCount(menu);
            for (UINT pos = 0; pos < (UINT)items16; ++pos) {
                wchar_t t[192] = {};
                GetMenuStringW(menu, pos, t, 192, MF_BYPOSITION);
                if (wcsstr(t, L"浏览")) {
                    lstrcpynW(txt, t, 192);
                    break;
                }
            }
            DestroyMenu(menu);
            const bool hasText = wcsstr(txt, L"浏览") != nullptr;
            const bool hasDims = wcsstr(txt, L"×") != nullptr &&
                                 wcschr(txt, L'\t') != nullptr;
            printf("      ctx-16 browse text len=%u tab=%d dims=%d\n",
                   (unsigned)wcslen(txt), (int)(wcschr(txt, L'\t') != nullptr),
                   (int)(wcsstr(txt, L"×") != nullptr));
            CTX(hasText && hasDims,
                "ctx-16 B2 浏览项文案（有图时右侧附 宽×高 · 大小）");
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

        // ===== 菜单偏好（设置页 ctxmenu/*，Qt 写 config.ini、DLL 每次现读）=====
        // 每例写一份临时 ini + 指定 SOLEVIEW_CONFIG_PATH，再走 QueryContextMenu。
        // 预览项是否出现取决于解码预算，用例对它做宽容断言；其余项为确定性断言。
        {
            // ini 放 dllDir（借标记文件路径取目录），用完即删
            wchar_t iniPath[MAX_PATH] = {};
            {
                std::wstring dir = wMark;
                const size_t bs = dir.find_last_of(L'\\');
                if (bs != std::wstring::npos) dir.resize(bs);
                wsprintfW(iniPath, L"%s\\ctx_prefs.ini", dir.c_str());
            }
            DeleteFileW(iniPath);

            // ctx-18：总开关关 → 右键完全不注入（无分隔线、无菜单项）
            WriteCtxCfg(iniPath, L"use_shell_menu", L"0");
            SetEnvironmentVariableW(L"SOLEVIEW_CONFIG_PATH", iniPath);
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                const bool ok = HRESULT_CODE(hr) == 0 &&
                                GetMenuItemCount(menu) == 0;
                DestroyMenu(menu);
                CTX(ok, "ctx-18 总开关关：完全不注入（无分隔线）");
            } else {
                CtxFail("ctx-18 总开关关（建实例失败）");
            }

            // ctx-19：缺键 = 默认（空 ini，0.5.0 体验原样）
            DeleteFileW(iniPath);   // 存在但无 ctxmenu 键
            SetEnvironmentVariableW(L"SOLEVIEW_CONFIG_PATH", iniPath);
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                bool ok = (hr & 0xFFFF) >= 2, sep = false, browse = false,
                     convert = false;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    wchar_t t[192] = {};
                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_FTYPE | MIIM_STRING;
                    mii.dwTypeData = t;
                    mii.cch = 191;
                    if (!GetMenuItemInfoW(menu, pos, TRUE, &mii)) continue;
                    if (mii.fType == MFT_SEPARATOR) sep = true;
                    if (wcsstr(t, L"浏览")) browse = true;
                    if (wcsstr(t, L"转换")) convert = true;
                }
                ok = ok && sep && browse && convert;
                DestroyMenu(menu);
                CTX(ok, "ctx-19 缺键沿用默认：分隔线+浏览+转换");
            } else {
                CtxFail("ctx-19 缺键默认（建实例失败）");
            }

            // ctx-20：转换关 → 只剩浏览（±预览），无「转换」
            WriteCtxCfg(iniPath, L"show_convert", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                bool browse = false, convert = false;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    wchar_t t[192] = {};
                    GetMenuStringW(menu, pos, t, 192, MF_BYPOSITION);
                    if (wcsstr(t, L"浏览")) browse = true;
                    if (wcsstr(t, L"转换")) convert = true;
                }
                DestroyMenu(menu);
                CTX(browse && !convert, "ctx-20 转换关：无「转换」项");
            } else {
                CtxFail("ctx-20 转换关（建实例失败）");
            }

            // ctx-21：浏览关 → 只剩转换（±预览），无「浏览」
            WriteCtxCfg(iniPath, L"show_convert", L"1");
            WriteCtxCfg(iniPath, L"show_browse", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                bool browse = false, convert = false;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    wchar_t t[192] = {};
                    GetMenuStringW(menu, pos, t, 192, MF_BYPOSITION);
                    if (wcsstr(t, L"浏览")) browse = true;
                    if (wcsstr(t, L"转换")) convert = true;
                }
                DestroyMenu(menu);
                CTX(!browse && convert, "ctx-21 浏览关：无「浏览」项");
            } else {
                CtxFail("ctx-21 浏览关（建实例失败）");
            }

            // ctx-22：浏览关时的 ID 重映射——偏移 0 应是转换动词（不留空洞）
            if (ctx) {
                wchar_t wverb[64] = {};
                hr = ctx->GetCommandString(0, GCS_VERBW, nullptr, (CHAR*)wverb, 64);
                CTX(SUCCEEDED(hr) && wcscmp(wverb, L"SoleView.Convert") == 0,
                    "ctx-22 浏览关时偏移 0 重映射为转换");
            } else {
                CtxFail("ctx-22 ID 重映射（建实例失败）");
            }

            // ctx-23：预览关 → 无自绘缩略图项（确定性：项数=3 分隔+浏览+转换）
            WriteCtxCfg(iniPath, L"show_browse", L"1");
            WriteCtxCfg(iniPath, L"show_preview", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                int ownerdraw = 0;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_FTYPE;
                    if (GetMenuItemInfoW(menu, pos, TRUE, &mii) &&
                        (mii.fType & MFT_OWNERDRAW) != 0) {
                        ++ownerdraw;
                    }
                }
                DestroyMenu(menu);
                // 命令数=2（浏览+转换），菜单项=3（含分隔线）
                CTX(HRESULT_CODE(hr) == 2 && cnt == 3 && ownerdraw == 0,
                    "ctx-23 预览关：无自绘项（分隔线+浏览+转换）");
            } else {
                CtxFail("ctx-23 预览关（建实例失败）");
            }

            // ctx-24：信息关 → 浏览项文案无「宽×高 · 大小」（无 \t）
            WriteCtxCfg(iniPath, L"show_info", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                bool browse = false, hasTab = false;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    wchar_t t[192] = {};
                    GetMenuStringW(menu, pos, t, 192, MF_BYPOSITION);
                    if (wcsstr(t, L"浏览")) {
                        browse = true;
                        hasTab = wcschr(t, L'\t') != nullptr;
                    }
                }
                DestroyMenu(menu);
                CTX(browse && !hasTab, "ctx-24 信息关：浏览项不带尺寸/大小");
            } else {
                CtxFail("ctx-24 信息关（建实例失败）");
            }

            // ctx-25：图标关 → 文本项均无 MIIM_BITMAP
            WriteCtxCfg(iniPath, L"show_info", L"1");
            WriteCtxCfg(iniPath, L"show_icon", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                int withBitmap = 0;
                const int cnt = GetMenuItemCount(menu);
                for (UINT pos = 0; pos < (UINT)cnt; ++pos) {
                    MENUITEMINFOW mii = {};
                    mii.cbSize = sizeof(mii);
                    mii.fMask = MIIM_FTYPE | MIIM_BITMAP;
                    if (GetMenuItemInfoW(menu, pos, TRUE, &mii) &&
                        mii.hbmpItem && (mii.fType & MFT_SEPARATOR) == 0) {
                        ++withBitmap;
                    }
                }
                DestroyMenu(menu);
                CTX(withBitmap == 0, "ctx-25 图标关：菜单项无位图");
            } else {
                CtxFail("ctx-25 图标关（建实例失败）");
            }

            // ctx-26：浏览/转换/预览全关 → 不插空分隔线，一个项都不占
            WriteCtxCfg(iniPath, L"show_icon", L"1");
            WriteCtxCfg(iniPath, L"show_browse", L"0");
            WriteCtxCfg(iniPath, L"show_convert", L"0");
            WriteCtxCfg(iniPath, L"show_preview", L"0");
            if (ctx) {
                HMENU menu = CreatePopupMenu();
                hr = ctx->QueryContextMenu(menu, 0, 1, 0x7FFF, CMF_NORMAL);
                const bool ok = HRESULT_CODE(hr) == 0 &&
                                GetMenuItemCount(menu) == 0;
                DestroyMenu(menu);
                CTX(ok, "ctx-26 三入口全关：不插空分隔线");
            } else {
                CtxFail("ctx-26 全关（建实例失败）");
            }

            // 恢复缺省（清配置指针），默认体验用例已覆盖
            DeleteFileW(iniPath);
            SetEnvironmentVariableW(L"SOLEVIEW_CONFIG_PATH", nullptr);
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
