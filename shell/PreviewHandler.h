#pragma once

#include <windows.h>
#include <objidl.h>
#include <shobjidl.h>
#include <propsys.h>
#include <wincodec.h>

// 资源管理器预览窗格提供者（IPreviewHandler + IInitializeWithStream）。
// 用 WIC 解码图片并等比缩放后绘制到预览窗口，深色背景。
class PreviewHandler
    : public IPreviewHandler
    , public IInitializeWithStream
{
public:
    PreviewHandler();
    virtual ~PreviewHandler();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pstream, DWORD grfMode) override;

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND hwnd, const RECT *prc) override;
    IFACEMETHODIMP SetRect(const RECT *prc) override;
    IFACEMETHODIMP DoPreview() override;
    IFACEMETHODIMP Unload() override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP QueryFocus(HWND *phwnd) override;
    IFACEMETHODIMP TranslateAccelerator(MSG *pmsg) override;

private:
    HRESULT Render(HWND hwnd, const RECT &rc);

    LONG m_ref = 1;
    IStream *m_stream = nullptr;
    HWND m_hwnd = nullptr;
    RECT m_rect = {};
    bool m_hasRect = false;
};
