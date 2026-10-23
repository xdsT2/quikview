#include "PreviewHandler.h"

#include <shlwapi.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

PreviewHandler::PreviewHandler() = default;

PreviewHandler::~PreviewHandler()
{
    if (m_stream) {
        m_stream->Release();
    }
}

IFACEMETHODIMP PreviewHandler::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] = {
        QITABENT(PreviewHandler, IPreviewHandler),
        QITABENT(PreviewHandler, IInitializeWithStream),
        { nullptr, 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) PreviewHandler::AddRef()
{
    return InterlockedIncrement(&m_ref);
}

IFACEMETHODIMP_(ULONG) PreviewHandler::Release()
{
    const LONG c = InterlockedDecrement(&m_ref);
    if (c == 0) {
        delete this;
    }
    return c;
}

IFACEMETHODIMP PreviewHandler::Initialize(IStream *pstream, DWORD)
{
    if (m_stream) {
        m_stream->Release();
        m_stream = nullptr;
    }
    m_stream = pstream;
    if (m_stream) {
        m_stream->AddRef();
    }
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetWindow(HWND hwnd, const RECT *prc)
{
    m_hwnd = hwnd;
    if (prc) {
        m_rect = *prc;
        m_hasRect = true;
    } else {
        m_rect = {};
        m_hasRect = false;
    }
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetRect(const RECT *prc)
{
    if (prc) {
        m_rect = *prc;
        m_hasRect = true;
    }
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::DoPreview()
{
    if (!m_hwnd || !m_stream) {
        return E_UNEXPECTED;
    }

    RECT rc = m_rect;
    if (!m_hasRect || (rc.right - rc.left) <= 0 || (rc.bottom - rc.top) <= 0) {
        GetClientRect(m_hwnd, &rc);
    }
    return Render(m_hwnd, rc);
}

IFACEMETHODIMP PreviewHandler::Unload()
{
    if (m_stream) {
        m_stream->Release();
        m_stream = nullptr;
    }
    m_hwnd = nullptr;
    m_rect = {};
    m_hasRect = false;
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::SetFocus()
{
    if (m_hwnd) {
        ::SetFocus(m_hwnd);
    }
    return S_OK;
}

IFACEMETHODIMP PreviewHandler::QueryFocus(HWND *phwnd)
{
    if (!phwnd) {
        return E_POINTER;
    }
    *phwnd = m_hwnd;
    return m_hwnd ? S_OK : S_FALSE;
}

IFACEMETHODIMP PreviewHandler::TranslateAccelerator(MSG *pmsg)
{
    (void)pmsg;
    return S_FALSE; // 不处理任何快捷键，交由预览宿主处理
}

HRESULT PreviewHandler::Render(HWND hwnd, const RECT &rc)
{
    const LONG rcW = rc.right - rc.left;
    const LONG rcH = rc.bottom - rc.top;
    if (rcW <= 0 || rcH <= 0) {
        return E_INVALIDARG;
    }

    HDC dc = GetDC(hwnd);
    if (!dc) {
        return E_FAIL;
    }

    // 深色背景铺满整个预览区域
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    IWICImagingFactory *factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        IWICBitmapDecoder *decoder = nullptr;
        hr = factory->CreateDecoderFromStream(m_stream, nullptr,
                                              WICDecodeMetadataCacheOnDemand, &decoder);
        if (SUCCEEDED(hr)) {
            IWICBitmapFrameDecode *frame = nullptr;
            hr = decoder->GetFrame(0, &frame);
            if (SUCCEEDED(hr)) {
                UINT srcW = 0, srcH = 0;
                frame->GetSize(&srcW, &srcH);

                // 转 32bppBGR（BI_RGB，忽略 alpha，透明区域呈黑色，契合深色背景）
                IWICFormatConverter *conv = nullptr;
                hr = factory->CreateFormatConverter(&conv);
                if (SUCCEEDED(hr)) {
                    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGR,
                                          WICBitmapDitherTypeNone, nullptr, 0.0,
                                          WICBitmapPaletteTypeCustom);
                }
                if (SUCCEEDED(hr) && srcW > 0 && srcH > 0) {
                    UINT cw = 0, ch = 0;
                    conv->GetSize(&cw, &ch);
                    const UINT stride = cw * 4;
                    BYTE *pixels = new BYTE[static_cast<size_t>(stride) * ch];
                    hr = conv->CopyPixels(nullptr, stride,
                                          static_cast<UINT>(stride) * ch, pixels);
                    if (SUCCEEDED(hr)) {
                        // 等比缩放，居中绘制
                        const double scale =
                            (static_cast<double>(rcW) / cw < static_cast<double>(rcH) / ch)
                                ? static_cast<double>(rcW) / cw
                                : static_cast<double>(rcH) / ch;
                        LONG tw = static_cast<LONG>(cw * scale + 0.5);
                        LONG th = static_cast<LONG>(ch * scale + 0.5);
                        if (tw < 1) tw = 1;
                        if (th < 1) th = 1;
                        const LONG tx = rc.left + (rcW - tw) / 2;
                        const LONG ty = rc.top + (rcH - th) / 2;

                        BITMAPINFO bmi = {};
                        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                        bmi.bmiHeader.biWidth = static_cast<LONG>(cw);
                        bmi.bmiHeader.biHeight = -static_cast<LONG>(ch); // top-down
                        bmi.bmiHeader.biPlanes = 1;
                        bmi.bmiHeader.biBitCount = 32;
                        bmi.bmiHeader.biCompression = BI_RGB;

                        SetStretchBltMode(dc, HALFTONE);
                        StretchDIBits(dc, tx, ty, tw, th,
                                      0, 0, static_cast<int>(cw), static_cast<int>(ch),
                                      pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
                    }
                    delete[] pixels;
                }
                if (conv) conv->Release();
                frame->Release();
            }
            decoder->Release();
        }
        factory->Release();
    }

    ReleaseDC(hwnd, dc);
    return SUCCEEDED(hr) ? S_OK : hr;
}
