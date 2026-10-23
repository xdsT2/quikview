#include "ThumbnailProvider.h"

#include <shlwapi.h>
#include <wincodec.h>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shlwapi.lib")

ThumbnailProvider::ThumbnailProvider() = default;

ThumbnailProvider::~ThumbnailProvider()
{
    if (m_stream) {
        m_stream->Release();
    }
}

IFACEMETHODIMP ThumbnailProvider::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] = {
        QITABENT(ThumbnailProvider, IThumbnailProvider),
        QITABENT(ThumbnailProvider, IInitializeWithStream),
        { nullptr, 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::AddRef()
{
    return InterlockedIncrement(&m_ref);
}

IFACEMETHODIMP_(ULONG) ThumbnailProvider::Release()
{
    const LONG c = InterlockedDecrement(&m_ref);
    if (c == 0) {
        delete this;
    }
    return c;
}

IFACEMETHODIMP ThumbnailProvider::Initialize(IStream *pstream, DWORD)
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

IFACEMETHODIMP ThumbnailProvider::GetThumbnail(UINT cx, HBITMAP *phbmp, WTS_ALPHATYPE *pdwAlpha)
{
    if (!m_stream) {
        return E_UNEXPECTED;
    }
    *phbmp = nullptr;
    *pdwAlpha = WTSAT_UNKNOWN;

    IWICImagingFactory *factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    IWICBitmapDecoder *decoder = nullptr;
    hr = factory->CreateDecoderFromStream(m_stream, nullptr,
                                          WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr)) { factory->Release(); return hr; }

    IWICBitmapFrameDecode *frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); factory->Release(); return hr; }

    // 缩放到 cx x cx，保持宽高比
    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);
    UINT tgtW = cx, tgtH = cx;
    if (width > 0 && height > 0) {
        const double scale = static_cast<double>(cx) / (width > height ? width : height);
        tgtW = static_cast<UINT>(width * scale + 0.5);
        tgtH = static_cast<UINT>(height * scale + 0.5);
        if (tgtW < 1) tgtW = 1;
        if (tgtH < 1) tgtH = 1;
    }

    IWICBitmapScaler *scaler = nullptr;
    hr = factory->CreateBitmapScaler(&scaler);
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(frame, tgtW, tgtH, WICBitmapInterpolationModeFant);
    }

    IWICBitmapSource *source = scaler ? static_cast<IWICBitmapSource *>(scaler) : static_cast<IWICBitmapSource *>(frame);

    // 转 32bppPBGRA
    IWICFormatConverter *conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr)) {
        hr = conv->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }

    if (SUCCEEDED(hr)) {
        UINT cw = 0, ch = 0;
        conv->GetSize(&cw, &ch);
        const UINT stride = cw * 4;
        BYTE *pixels = new BYTE[static_cast<size_t>(stride) * ch];
        hr = conv->CopyPixels(nullptr, stride, static_cast<UINT>(stride) * ch, pixels);
        if (SUCCEEDED(hr)) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = static_cast<LONG>(cw);
            bmi.bmiHeader.biHeight = -static_cast<LONG>(ch); // top-down
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void *bits = nullptr;
            *phbmp = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (*phbmp && bits) {
                memcpy(bits, pixels, static_cast<size_t>(stride) * ch);
                *pdwAlpha = WTSAT_ARGB;
            }
        }
        delete[] pixels;
    }

    if (conv) conv->Release();
    if (scaler) scaler->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return (*phbmp != nullptr) ? S_OK : hr;
}
