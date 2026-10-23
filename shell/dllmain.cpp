#include <windows.h>
#include <shlobj.h>
#include <string>

#include "ThumbnailProvider.h"
#include "PreviewHandler.h"

// {C5B3E2D1-4A6F-4B7E-9C2D-1E8A3F5B7D90}
static const CLSID CLSID_QuikViewThumb =
{ 0xC5B3E2D1, 0x4A6F, 0x4B7E, { 0x9C, 0x2D, 0x1E, 0x8A, 0x3F, 0x5B, 0x7D, 0x90 } };

// 资源管理器缩略图处理器槽位 {E357FCCD-A995-4576-B01F-234630154E96}
static const CLSID kThumbHandlerSlot =
{ 0xE357FCCD, 0xA995, 0x4576, { 0xB0, 0x1F, 0x23, 0x46, 0x30, 0x15, 0x49, 0x6E } };

// {D4E5F6A7-8B9C-4D0E-8F1A-2B3C4D5E6F70}
static const CLSID CLSID_QuikViewPreview =
{ 0xD4E5F6A7, 0x8B9C, 0x4D0E, { 0x8F, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F, 0x70 } };

// 资源管理器预览窗格处理器槽位 {8895B1C6-B41F-4C1C-A562-0D564250836F}
static const CLSID kPreviewHandlerSlot =
{ 0x8895B1C6, 0xB41F, 0x4C1C, { 0xA5, 0x62, 0x0D, 0x56, 0x42, 0x50, 0x83, 0x6F } };

HINSTANCE g_hInst = nullptr;

// 注册到这些图片扩展名的缩略图槽位
static const wchar_t *kExtensions[] = {
    L".png", L".jpg", L".jpeg", L".bmp", L".gif", L".webp",
    L".tif", L".tiff", L".ico", L".dng", L".nef", L".cr2", L".arw",
};

class ThumbClassFactory : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto *obj = new ThumbnailProvider();
        const HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) override { return S_OK; }

private:
    LONG m_ref = 1;
};

class PreviewClassFactory : public IClassFactory
{
public:
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override
    {
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    IFACEMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    IFACEMETHODIMP_(ULONG) Release() override
    {
        const LONG c = InterlockedDecrement(&m_ref);
        if (c == 0) delete this;
        return c;
    }
    IFACEMETHODIMP CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto *obj = new PreviewHandler();
        const HRESULT hr = obj->QueryInterface(riid, ppv);
        obj->Release();
        return hr;
    }
    IFACEMETHODIMP LockServer(BOOL) override { return S_OK; }

private:
    LONG m_ref = 1;
};

static LONG g_lockCount = 0;

static HRESULT setRegValue(HKEY root, const wchar_t *sub, const wchar_t *name, const wchar_t *value)
{
    HKEY key = nullptr;
    HRESULT hr = RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE, nullptr, &key, nullptr);
    if (FAILED(hr)) return hr;
    RegSetValueExW(key, name, 0, REG_SZ,
                   reinterpret_cast<const BYTE *>(value),
                   static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return S_OK;
}

static std::wstring modulePath()
{
    wchar_t buf[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, buf, MAX_PATH);
    return buf;
}

static HRESULT registerServer()
{
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_QuikViewThumb, clsidStr, 64);
    wchar_t thumbSlotStr[64];
    StringFromGUID2(kThumbHandlerSlot, thumbSlotStr, 64); // {E357FCCD-...}

    wchar_t previewClsidStr[64];
    StringFromGUID2(CLSID_QuikViewPreview, previewClsidStr, 64);
    wchar_t previewSlotStr[64];
    StringFromGUID2(kPreviewHandlerSlot, previewSlotStr, 64); // {8895B1C6-...}

    const std::wstring cls = std::wstring(L"CLSID\\") + clsidStr;
    const std::wstring previewCls = std::wstring(L"CLSID\\") + previewClsidStr;
    const std::wstring path = modulePath();

    setRegValue(HKEY_CLASSES_ROOT, cls.c_str(), nullptr, L"QuikView Thumbnail Provider");
    setRegValue(HKEY_CLASSES_ROOT, (cls + L"\\InprocServer32").c_str(), nullptr, path.c_str());
    setRegValue(HKEY_CLASSES_ROOT, (cls + L"\\InprocServer32").c_str(), L"ThreadingModel", L"Apartment");

    for (const wchar_t *ext : kExtensions) {
        const std::wstring slot = std::wstring(ext) + L"\\ShellEx\\" + thumbSlotStr;
        setRegValue(HKEY_CLASSES_ROOT, slot.c_str(), nullptr, clsidStr);
    }

    setRegValue(HKEY_CLASSES_ROOT, previewCls.c_str(), nullptr, L"QuikView Preview Handler");
    setRegValue(HKEY_CLASSES_ROOT, (previewCls + L"\\InprocServer32").c_str(), nullptr, path.c_str());
    setRegValue(HKEY_CLASSES_ROOT, (previewCls + L"\\InprocServer32").c_str(), L"ThreadingModel", L"Apartment");

    for (const wchar_t *ext : kExtensions) {
        const std::wstring slot =
            std::wstring(L"SystemFileAssociations\\") + ext + L"\\ShellEx\\" + previewSlotStr;
        setRegValue(HKEY_CLASSES_ROOT, slot.c_str(), nullptr, previewClsidStr);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

static HRESULT unregisterServer()
{
    wchar_t clsidStr[64];
    StringFromGUID2(CLSID_QuikViewThumb, clsidStr, 64);
    wchar_t thumbSlotStr[64];
    StringFromGUID2(kThumbHandlerSlot, thumbSlotStr, 64);

    wchar_t previewClsidStr[64];
    StringFromGUID2(CLSID_QuikViewPreview, previewClsidStr, 64);
    wchar_t previewSlotStr[64];
    StringFromGUID2(kPreviewHandlerSlot, previewSlotStr, 64);

    const std::wstring cls = std::wstring(L"CLSID\\") + clsidStr;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, cls.c_str());
    for (const wchar_t *ext : kExtensions) {
        const std::wstring slot = std::wstring(ext) + L"\\ShellEx\\" + thumbSlotStr;
        RegDeleteKeyValueW(HKEY_CLASSES_ROOT, slot.c_str(), nullptr);
    }

    const std::wstring previewCls = std::wstring(L"CLSID\\") + previewClsidStr;
    RegDeleteTreeW(HKEY_CLASSES_ROOT, previewCls.c_str());
    for (const wchar_t *ext : kExtensions) {
        const std::wstring slot =
            std::wstring(L"SystemFileAssociations\\") + ext + L"\\ShellEx\\" + previewSlotStr;
        RegDeleteKeyValueW(HKEY_CLASSES_ROOT, slot.c_str(), nullptr);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void **ppv)
{
    IClassFactory *factory = nullptr;
    if (rclsid == CLSID_QuikViewThumb) {
        factory = new ThumbClassFactory();
    } else if (rclsid == CLSID_QuikViewPreview) {
        factory = new PreviewClassFactory();
    } else {
        return CLASS_E_CLASSNOTAVAILABLE;
    }
    const HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

STDAPI DllCanUnloadNow()
{
    return g_lockCount == 0 ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()   { return registerServer(); }
STDAPI DllUnregisterServer() { return unregisterServer(); }

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, void *)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_hInst = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}
