// QuikThumbProvider 实现：IInitializeWithStream + IThumbnailProvider。
// 解码链（纯 WIC，零第三方依赖）：
//   CreateDecoderFromStream → （内嵌缩略图足够大则用它，否则整帧）
//   IWICBitmapScaler 等比缩到 cx 盒内 → IWICFormatConverter 转 32bppPBGRA（预乘）
//   → CopyPixels 直接写进 CreateDIBSection 的缓冲 → 返回 HBITMAP + WTSAT_ARGB。
// 批次 C：PSD/PSB 不做完整像素解码，只解析文件头 → image resources 找
//   ID 0x040C 内嵌 JPEG 缩略图，交现有 WIC 链；找不到回退默认图标。
#include <wincodec.h>
#include <shlobj.h>
#include <vector>

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

inline ULONG ReadU32BE(const BYTE* p) {
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
           ((ULONG)p[2] << 8) | (ULONG)p[3];
}
inline USHORT ReadU16BE(const BYTE* p) {
    return (USHORT)(((USHORT)p[0] << 8) | (USHORT)p[1]);
}

// 从 IStream 顺序读 N 字节；不足返回 false。
bool ReadBytes(IStream* stm, void* buf, ULONG n) {
    ULONG done = 0;
    while (done < n) {
        ULONG got = 0;
        if (FAILED(stm->Read((BYTE*)buf + done, n - done, &got)) || got == 0)
            return false;
        done += got;
    }
    return true;
}
bool SeekBytes(IStream* stm, LARGE_INTEGER off) {
    return SUCCEEDED(stm->Seek(off, STREAM_SEEK_SET, nullptr));
}

// 解析 PSD/PSB，抽 image resources 中 ID 0x040C 的内嵌 JPEG 缩略图。
// 只读文件头到 image resources 块（通常 < 几 MB），失败立即返回。
HRESULT ExtractPsdThumbJpeg(IStream* stm, std::vector<BYTE>* jpeg) {
    BYTE hdr[26];
    if (!SeekBytes(stm, {}) || !ReadBytes(stm, hdr, sizeof(hdr))) return E_FAIL;
    // 签名 "8BPS"；版本 1=PSD, 2=PSB
    if (memcmp(hdr, "8BPS", 4) != 0) return E_FAIL;

    BYTE buf[256];
    // color mode data: 4B 长度 + 数据
    if (!ReadBytes(stm, buf, 4)) return E_FAIL;
    ULONG cmLen = ReadU32BE(buf);
    // 跳过（一次最多 16MB，超过说明不是 PSD）
    if (cmLen > 16u * 1024 * 1024) return E_FAIL;
    ULONG skipped = 0;
    while (skipped < cmLen) {
        ULONG chunk = cmLen - skipped;
        if (chunk > sizeof(buf)) chunk = sizeof(buf);
        if (!ReadBytes(stm, buf, chunk)) return E_FAIL;
        skipped += chunk;
    }

    // image resources: 4B 长度 + 块
    if (!ReadBytes(stm, buf, 4)) return E_FAIL;
    ULONG irLen = ReadU32BE(buf);
    if (irLen == 0 || irLen > 64u * 1024 * 1024) return E_FAIL;

    // 顺序扫描 image resources 条目
    ULONG consumed = 0;
    while (consumed + 8 <= irLen) {
        if (!ReadBytes(stm, buf, 8)) return E_FAIL;   // "8BIM" + ID(2) + namelen(1)+pad(1)
        consumed += 8;
        if (memcmp(buf, "8BIM", 4) != 0) {
            // 不是合法资源头，放弃
            return E_FAIL;
        }
        const USHORT id = ReadU16BE(buf + 4);
        // Pascal 名：1B 长度 + 字符，整体补偶
        if (!ReadBytes(stm, buf, 1)) return E_FAIL;
        consumed += 1;
        const ULONG nameLen = buf[0];
        ULONG namePad = (nameLen + 1 + 1) & ~1u;   // 长度字节 + 数据，补偶
        if (namePad > 260) return E_FAIL;
        if (namePad > 1) {
            if (!ReadBytes(stm, buf, namePad - 1)) return E_FAIL;
            consumed += namePad - 1;
        }
        // data length (4B)
        if (!ReadBytes(stm, buf, 4)) return E_FAIL;
        consumed += 4;
        const ULONG dataLen = ReadU32BE(buf);
        if (dataLen > 32u * 1024 * 1024) return E_FAIL;

        if (id == 0x040C) {
            // 缩略图资源：前 28 字节是预览头（width/height/...），之后是 JPEG
            const ULONG kPreviewHdr = 28;
            if (dataLen <= kPreviewHdr) return E_FAIL;
            ULONG jpegLen = dataLen - kPreviewHdr;
            jpeg->resize(jpegLen);
            // 跳过预览头
            if (!ReadBytes(stm, buf, kPreviewHdr > sizeof(buf) ? sizeof(buf) : kPreviewHdr))
                return E_FAIL;
            if (kPreviewHdr > sizeof(buf)) {
                ULONG rest = kPreviewHdr - sizeof(buf);
                while (rest) {
                    ULONG c = rest > sizeof(buf) ? sizeof(buf) : rest;
                    if (!ReadBytes(stm, buf, c)) return E_FAIL;
                    rest -= c;
                }
            }
            if (!ReadBytes(stm, jpeg->data(), jpegLen)) return E_FAIL;
            return S_OK;
        }

        // 跳过本条 data（补偶）
        ULONG skip = (dataLen + 1) & ~1u;
        while (skip) {
            ULONG c = skip > sizeof(buf) ? sizeof(buf) : skip;
            if (!ReadBytes(stm, buf, c)) return E_FAIL;
            consumed += c;
            skip -= c;
        }
    }
    return E_FAIL;   // 没找到 0x040C
}

// 共享尾部：给定一帧，缩放到 cx 盒并产出 DIB
HRESULT FinishThumb(QvPtr<IWICImagingFactory>& factory,
                   IWICBitmapFrameDecode* frame, UINT cx,
                   HBITMAP* phbm, WTS_ALPHATYPE* pat) {
    UINT srcW = 0, srcH = 0;
    frame->GetSize(&srcW, &srcH);
    if (srcW == 0 || srcH == 0) return E_FAIL;

    UINT dstW = 0, dstH = 0;
    FitSize(srcW, srcH, cx, &dstW, &dstH);

    QvPtr<IWICBitmapSource> embedded;
    IWICBitmapSource* source = frame;
    if (SUCCEEDED(frame->GetThumbnail(&embedded)) && embedded.Get()) {
        UINT embW = 0, embH = 0;
        if (SUCCEEDED(embedded->GetSize(&embW, &embH)) &&
            embW >= dstW && embH >= dstH) {
            source = embedded.Get();
        }
    }

    QvPtr<IWICBitmapScaler> scaler;
    HRESULT hr = factory->CreateBitmapScaler(&scaler);
    if (FAILED(hr)) return hr;
    hr = scaler->Initialize(source, dstW, dstH, WICBitmapInterpolationModeFant);
    if (FAILED(hr)) return hr;

    QvPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) return hr;
    hr = converter->Initialize(scaler.Get(), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return hr;

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
    hr = converter->CopyPixels(nullptr, stride, stride * dstH, (BYTE*)pvBits);
    if (FAILED(hr)) {
        DeleteObject(hbm);
        return hr;
    }
    *phbm = hbm;
    *pat = WTSAT_ARGB;
    return S_OK;
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

        // —— 批次 C：先试 PSD/PSB 内嵌 JPEG 旁路 ——
        std::vector<BYTE> jpeg;
        hr = ExtractPsdThumbJpeg(m_stream, &jpeg);
        if (SUCCEEDED(hr) && !jpeg.empty()) {
            HGLOBAL hg = GlobalAlloc(GPTR, jpeg.size());
            if (hg) {
                void* p = GlobalLock(hg);
                if (p) {
                    memcpy(p, jpeg.data(), jpeg.size());
                    GlobalUnlock(hg);
                }
                QvPtr<IStream> mem;
                if (SUCCEEDED(CreateStreamOnHGlobal(hg, TRUE, &mem))) {
                    QvPtr<IWICBitmapDecoder> jdec;
                    hr = factory->CreateDecoderFromStream(
                        mem.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &jdec);
                    if (SUCCEEDED(hr)) {
                        QvPtr<IWICBitmapFrameDecode> frame;
                        hr = jdec->GetFrame(0, &frame);
                        if (SUCCEEDED(hr)) {
                            return FinishThumb(factory, frame.Get(), cx, phbm, pat);
                        }
                    }
                }
            }
            // 旁路失败：继续走下面的常规路径（Explorer 不认识 PSD 时会回退默认）
        }

        // —— 常规 WIC 路径 ——
        // PSD 探测函数已把流读过一段，回绕到 0 再解码
        {
            LARGE_INTEGER zero = {};
            m_stream->Seek(zero, STREAM_SEEK_SET, nullptr);
        }
        QvPtr<IWICBitmapDecoder> decoder;
        hr = factory->CreateDecoderFromStream(m_stream, nullptr,
                                              WICDecodeMetadataCacheOnDemand,
                                              &decoder);
        if (FAILED(hr)) return hr;

        QvPtr<IWICBitmapFrameDecode> frame;
        hr = decoder->GetFrame(0, &frame);
        if (FAILED(hr)) return hr;

        return FinishThumb(factory, frame.Get(), cx, phbm, pat);
    } catch (...) {
        return E_FAIL;   // 异常绝不穿出，防 Explorer 连带崩溃
    }
}
