#pragma once

#include <windows.h>
#include <objidl.h>
#include <thumbcache.h>
#include <propsys.h>
#include <wincodec.h>

// 资源管理器缩略图提供者（IThumbnailProvider + IInitializeWithStream）。
// 用 WIC 解码图片并缩放到目标尺寸，输出带 alpha 的 HBITMAP。
class ThumbnailProvider
    : public IThumbnailProvider
    , public IInitializeWithStream
{
public:
    ThumbnailProvider();
    virtual ~ThumbnailProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream *pstream, DWORD grfMode) override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha) override;

private:
    LONG m_ref = 1;
    IStream *m_stream = nullptr;
};
