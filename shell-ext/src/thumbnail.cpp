// QuikThumbProvider 实现：IInitializeWithStream + IThumbnailProvider。
// 解码链（纯 WIC，零第三方依赖）：
//   CreateDecoderFromStream → （内嵌缩略图足够大则用它，否则整帧）
//   IWICBitmapScaler 等比缩到 cx 盒内 → IWICFormatConverter 转 32bppPBGRA（预乘）
//   → CopyPixels 直接写进 CreateDIBSection 的缓冲 → 返回 HBITMAP + WTSAT_ARGB。
// 任何一步失败都返回失败 HRESULT，Explorer 回退默认图标，不影响其它格式。
#include <wincodec.h>

#include "quikthumb.h"

namespace {

// 轻量 RAII：等价 ComPtr，不引 ATL/WRL
template <class T>
class QvPtr {
public:
    QvPtr() : m_p(nullptr) {}
    ~QvPtr() { Release(); }
    T** operator&() {
        Release();
        return &m_p;
    }
    T* operator->() const { return m_p; }
    T* Get() const { return m_p; }
    void Release() {
        if (m_p) {
            m_p->Release();
            m_p = nullptr;
        }
    }

private:
    QvPtr(const QvPtr&);
    QvPtr& operator=(const QvPtr&);
    T* m_p;
};

// 等比缩放到内切 cx 盒；小图不放大，最小 1px
void FitSize(UINT w, UINT h, UINT cx, UINT* tw, UINT* th) {
    if (w == 0 || h == 0) {
        *tw = 1;
        *th = 1;
        return;
    }
    if (w <= cx && h <= cx) {
        *tw = w;
        *th = h;
        return;
    }
    if (w >= h) {
        *tw = cx;
        *th = (UINT)((UINT64)h * cx / w);
        if (*th == 0) *th = 1;
    } else {
        *tw = (UINT)((UINT64)w * cx / h);
        if (*tw == 0) *tw = 1;
        *th = cx;
    }
}

}   // namespace

QuikThumbProvider::QuikThumbProvider() : m_ref(1), m_stream(nullptr) {
    DllAddLock();
}

QuikThumbProvider::~QuikThumbProvider() {
    if (m_stream) m_stream->Release();
    DllReleaseLock();
}

// IUnknown
STDMETHODIMP QuikThumbProvider::QueryInterface(REFIID riid, void** ppv) {
    try {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_IInitializeWithStream)) {
            *ppv = static_cast<QvInitializeWithStream*>(this);
        } else if (IsEqualIID(riid, IID_IThumbnailProvider)) {
            *ppv = static_cast<IThumbnailProvider*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

STDMETHODIMP_(ULONG) QuikThumbProvider::AddRef() {
    return InterlockedIncrement(&m_ref);
}

STDMETHODIMP_(ULONG) QuikThumbProvider::Release() {
    const ULONG ref = InterlockedDecrement(&m_ref);
    if (ref == 0) delete this;
    return ref;
}

// IInitializeWithStream：只留存流引用，解码推迟到 GetThumbnail
STDMETHODIMP QuikThumbProvider::Load(IStream* pStm, DWORD /*grfMode*/) {
    try {
        if (!pStm) return E_INVALIDARG;
        if (m_stream) m_stream->Release();
        m_stream = pStm;
        m_stream->AddRef();
        return S_OK;
    } catch (...) {
        return E_FAIL;
    }
}

// IThumbnailProvider
STDMETHODIMP QuikThumbProvider::GetThumbnail(UINT cx, HBITMAP* phbm,
                                             WTS_ALPHATYPE* pat) {
    try {
        if (!phbm || !pat || cx == 0) return E_INVALIDARG;
        *phbm = nullptr;
        *pat = WTSAT_UNKNOWN;
        if (!m_stream) return E_FAIL;

        QvPtr<IWICImagingFactory> factory;
        HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_IWICImagingFactory,
                                      (void**)&factory);
        if (FAILED(hr)) return hr;

        QvPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromStream(m_stream, nullptr,
                                              WICDecodeMetadataCacheOnDemand,
                                              &decoder);
        if (FAILED(hr)) return hr;   // 不认识的格式 → Explorer 回退默认图标

        QvPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return hr;

        UINT srcW = 0, srcH = 0;
        frame->GetSize(&srcW, &srcH);
        if (srcW == 0 || srcH == 0) return E_FAIL;

        UINT dstW = 0, dstH = 0;
        FitSize(srcW, srcH, cx, &dstW, &dstH);

        // 内嵌缩略图不小于目标尺寸才用（小缩略图放大反而模糊）；
        // GetThumbnail 只出源，尺寸另用 IWICBitmapSource::GetSize 取
        QvPtr<IWICBitmapSource> embedded;
        IWICBitmapSource* source = frame.Get();
        if (SUCCEEDED(frame->GetThumbnail(&embedded)) && embedded.Get()) {
            UINT embW = 0, embH = 0;
            if (SUCCEEDED(embedded->GetSize(&embW, &embH)) &&
                embW >= dstW && embH >= dstH) {
                source = embedded.Get();
            }
        }

        QvPtr<IWICBitmapScaler> scaler;
        hr = factory->CreateBitmapScaler(&scaler);
        if (FAILED(hr)) return hr;
        hr = scaler->Initialize(source, dstW, dstH,
                                WICBitmapInterpolationModeFant);
        if (FAILED(hr)) return hr;

        QvPtr<IWICFormatConverter> converter;
        hr = factory->CreateFormatConverter(&converter);
        if (FAILED(hr)) return hr;
        // PBGRA = 预乘 alpha 的 BGRA，与 WTSAT_ARGB 的约定一致
        hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
        if (FAILED(hr)) return hr;

        // 32bpp top-down DIB，CopyPixels 直写其缓冲，免二次拷贝
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = (LONG)dstW;
        bi.bmiHeader.biHeight = -(LONG)dstH;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* pvBits = nullptr;
        HBITMAP hbm = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &pvBits,
                                       nullptr, 0);
        if (!hbm || !pvBits) {
            if (hbm) DeleteObject(hbm);
            return E_OUTOFMEMORY;
        }
        const UINT stride = dstW * 4;
        hr = converter->CopyPixels(nullptr, stride, stride * dstH,
                                   (BYTE*)pvBits);
        if (FAILED(hr)) {
            DeleteObject(hbm);
            return hr;
        }

        *phbm = hbm;
        *pat = WTSAT_ARGB;
        return S_OK;
    } catch (...) {
        return E_FAIL;   // 异常绝不穿出，防 Explorer 连带崩溃
    }
}
