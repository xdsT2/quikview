#include "ExtraImageLoader.h"

#include "HeifLoader.h"
#include "PsdPch.h"
#include "Psdminiz.h"
#include "ZipArchive.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <vector>

#ifdef HAS_LIBWEBPDEMUX
#include <webp/demux.h>
#endif

#ifdef HAS_LIBOPENJP2
#include <openjpeg.h>
#endif

#ifdef HAS_LIBJXL
#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/thread_parallel_runner.h>
#include <jxl/thread_parallel_runner_cxx.h>
#endif

#ifdef HAS_OPENEXR
#include <ImfIO.h>
#include <ImfRgbaFile.h>
#include <ImathBox.h>
#include <ImfArray.h>
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <gdiplus.h>
#endif

namespace {

constexpr qint64 kMaxFileBytes = 256LL * 1024 * 1024;
constexpr qint64 kMaxVectorBytes = 8LL * 1024 * 1024;
constexpr int kMaxDim = 30000;
constexpr quint64 kMaxPixels = 100000000ULL;
constexpr int kMaxVectorEdge = 4096;
constexpr quint64 kMaxVectorPixels = 8000000ULL;
constexpr int kMaxAnimFrames = 512;
constexpr quint64 kMaxAnimTotalBytes = 256ULL * 1024 * 1024;

void setFail(const QString &msg, LoadErrorCategory cat, QString *errOut, LoadErrorCategory *catOut)
{
    if (errOut) {
        *errOut = msg;
    }
    if (catOut) {
        *catOut = cat;
    }
}

bool validRasterSize(int w, int h)
{
    if (w <= 0 || h <= 0 || w > kMaxDim || h > kMaxDim) {
        return false;
    }
    return quint64(w) * quint64(h) <= kMaxPixels;
}

QSize clampVectorSize(int natW, int natH, const QSize &box, bool allowUpscale)
{
    if (natW <= 0 || natH <= 0) {
        return QSize();
    }
    QSize out(natW, natH);
    if (box.isValid() && !box.isEmpty()) {
        const double s = qMin(double(box.width()) / natW, double(box.height()) / natH);
        if (s > 1.0 && allowUpscale) {
            out = QSize(qMax(1, int(std::ceil(natW * s))), qMax(1, int(std::ceil(natH * s))));
        } else if (s < 1.0) {
            out = QSize(qMax(1, int(natW * s)), qMax(1, int(natH * s)));
        }
    }
    if (out.width() > kMaxVectorEdge || out.height() > kMaxVectorEdge) {
        out.scale(QSize(kMaxVectorEdge, kMaxVectorEdge), Qt::KeepAspectRatio);
    }
    const quint64 px = quint64(out.width()) * quint64(out.height());
    if (px > kMaxVectorPixels) {
        const double k = std::sqrt(double(kMaxVectorPixels) / double(px));
        out = QSize(qMax(1, int(out.width() * k)), qMax(1, int(out.height() * k)));
    }
    return out;
}

bool readFileBytes(const QString &path, qint64 maxBytes, QByteArray &out,
                   QString *errOut, LoadErrorCategory *catOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setFail(QStringLiteral("file open failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    if (f.size() > maxBytes) {
        setFail(QStringLiteral("file exceeds size limit"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    out = f.read(maxBytes);
    if (out.isEmpty()) {
        setFail(QStringLiteral("file is empty"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    return true;
}

inline quint16 rd16le(const uchar *p) { return qFromLittleEndian<quint16>(p); }
inline quint32 rd32le(const uchar *p) { return qFromLittleEndian<quint32>(p); }
inline quint16 rd16be(const uchar *p) { return qFromBigEndian<quint16>(p); }
inline quint32 rd32be(const uchar *p) { return qFromBigEndian<quint32>(p); }

inline uchar linearToSrgbByte(float v)
{
    if (!std::isfinite(v) || v <= 0.0f) {
        return 0;
    }
    // ACES Filmic 曲线近似 + sRGB 2.2，防止 HDR 高光过曝
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    float mapped = (v * (a * v + b)) / (v * (c * v + d) + e);
    mapped = std::clamp(mapped, 0.0f, 1.0f);
    const float srgb = (mapped <= 0.0031308f)
                           ? (mapped * 12.92f)
                           : (1.055f * std::pow(mapped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uchar>(std::clamp(int(std::lround(srgb * 255.0f)), 0, 255));
}

inline uchar directSrgbByte(float v)
{
    if (!std::isfinite(v) || v <= 0.0f) {
        return 0;
    }
    return static_cast<uchar>(std::clamp(int(std::lround(v * 255.0f)), 0, 255));
}

inline float halfToFloat(quint16 h)
{
    const quint32 sign = quint32(h & 0x8000u) << 16;
    const quint32 exp = (h >> 10) & 0x1Fu;
    const quint32 mant = h & 0x03FFu;
    quint32 fbits = 0;
    if (exp == 0) {
        if (mant == 0) {
            fbits = sign;
        } else {
            int e = -14;
            quint32 m = mant;
            while ((m & 0x0400u) == 0) {
                m <<= 1;
                --e;
            }
            m &= 0x03FFu;
            fbits = sign | (quint32(e + 127) << 23) | (m << 13);
        }
    } else if (exp == 31) {
        fbits = sign | 0x7F800000u | (mant << 13);
    } else {
        fbits = sign | (quint32(exp - 15 + 127) << 23) | (mant << 13);
    }
    float out = 0.0f;
    std::memcpy(&out, &fbits, sizeof(out));
    return out;
}

QString extLower(const QString &path)
{
    return QFileInfo(path).suffix().toLower();
}

// ===========================================================================
// 1. QOI (.qoi)
// ===========================================================================
QImage decodeQoi(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 22 || std::memcmp(data.constData(), "qoif", 4) != 0) {
        setFail(QStringLiteral("qoi invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 w = rd32be(p + 4);
    const quint32 h = rd32be(p + 8);
    const int channels = p[12];
    if (!validRasterSize(int(w), int(h)) || (channels != 3 && channels != 4)) {
        setFail(QStringLiteral("qoi invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    QImage img(int(w), int(h), channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (img.isNull()) {
        setFail(QStringLiteral("qoi alloc failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    struct Px { uchar r, g, b, a; };
    Px index[64] = {};
    Px px = { 0, 0, 0, 255 };
    int run = 0;
    size_t pos = 14;
    const size_t end = size_t(data.size()) - 8;

    for (quint32 y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(int(y));
        for (quint32 x = 0; x < w; ++x) {
            if (run > 0) {
                --run;
            } else {
                if (pos >= end) {
                    setFail(QStringLiteral("qoi truncated pixel data"), LoadErrorCategory::Corrupt, errOut, catOut);
                    return QImage();
                }
                const uchar b1 = p[pos++];
                if (b1 == 0xFE) {
                    if (pos + 3 > end) {
                        setFail(QStringLiteral("qoi truncated rgb"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    px.r = p[pos++];
                    px.g = p[pos++];
                    px.b = p[pos++];
                } else if (b1 == 0xFF) {
                    if (pos + 4 > end) {
                        setFail(QStringLiteral("qoi truncated rgba"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    px.r = p[pos++];
                    px.g = p[pos++];
                    px.b = p[pos++];
                    px.a = p[pos++];
                } else if ((b1 & 0xC0) == 0x00) {
                    px = index[b1];
                } else if ((b1 & 0xC0) == 0x40) {
                    px.r = uchar(px.r + ((b1 >> 4) & 0x03) - 2);
                    px.g = uchar(px.g + ((b1 >> 2) & 0x03) - 2);
                    px.b = uchar(px.b + (b1 & 0x03) - 2);
                } else if ((b1 & 0xC0) == 0x80) {
                    if (pos >= end) {
                        setFail(QStringLiteral("qoi truncated luma"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    const uchar b2 = p[pos++];
                    const int vg = int(b1 & 0x3F) - 32;
                    px.r = uchar(px.r + vg - 8 + ((b2 >> 4) & 0x0F));
                    px.g = uchar(px.g + vg);
                    px.b = uchar(px.b + vg - 8 + (b2 & 0x0F));
                } else {
                    run = int(b1 & 0x3F);
                }
                const int hash = (px.r * 3 + px.g * 5 + px.b * 7 + px.a * 11) & 63;
                index[hash] = px;
            }
            dst[x * channels + 0] = px.r;
            dst[x * channels + 1] = px.g;
            dst[x * channels + 2] = px.b;
            if (channels == 4) {
                dst[x * 4 + 3] = px.a;
            }
        }
    }
    return img;
}

// ===========================================================================
// 2. Radiance HDR (.hdr, .pic)
// ===========================================================================
QImage decodeRadianceHdr(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 16 || (!data.startsWith("#?RADIANCE") && !data.startsWith("#?RGBE"))) {
        setFail(QStringLiteral("hdr invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    int pos = 0;
    const int n = data.size();
    auto readLine = [&data, &pos, n]() -> QByteArray {
        const int start = pos;
        while (pos < n && data.at(pos) != '\n') {
            ++pos;
        }
        QByteArray line = data.mid(start, pos - start);
        if (pos < n && data.at(pos) == '\n') {
            ++pos;
        }
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        return line;
    };

    float exposure = 1.0f;
    bool foundBlank = false;
    while (pos < n) {
        const QByteArray line = readLine();
        if (line.isEmpty()) {
            foundBlank = true;
            break;
        }
        if (line.startsWith("EXPOSURE=")) {
            bool ok = false;
            const float ex = line.mid(9).trimmed().toFloat(&ok);
            if (ok && ex > 0.0f && std::isfinite(ex)) {
                exposure *= ex;
            }
        }
    }
    if (!foundBlank || pos >= n) {
        setFail(QStringLiteral("hdr header truncated"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const QByteArray dimLine = readLine();
    const QList<QByteArray> parts = dimLine.simplified().split(' ');
    if (parts.size() != 4) {
        setFail(QStringLiteral("hdr invalid resolution line"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    bool okH = false, okW = false;
    const int h = parts[1].toInt(&okH);
    const int w = parts[3].toInt(&okW);
    if (!okH || !okW || !validRasterSize(w, h)
        || (parts[0] != "-Y" && parts[0] != "+Y")
        || (parts[2] != "+X" && parts[2] != "-X")) {
        setFail(QStringLiteral("hdr unsupported orientation or size"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const bool flipY = (parts[0] == "+Y");
    const bool flipX = (parts[2] == "-X");

    QImage img(w, h, QImage::Format_RGB888);
    if (img.isNull()) {
        setFail(QStringLiteral("hdr alloc failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    std::vector<uchar> scan(size_t(w) * 4);
    const float invExp = 1.0f / exposure;

    for (int y = 0; y < h; ++y) {
        if (pos + 4 > n) {
            setFail(QStringLiteral("hdr truncated scanline"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        const bool newRle = (w >= 8 && w <= 0x7FFF && p[pos] == 2 && p[pos + 1] == 2
                             && (p[pos + 2] & 0x80) == 0
                             && ((int(p[pos + 2]) << 8) | p[pos + 3]) == w);
        if (newRle) {
            pos += 4;
            for (int ch = 0; ch < 4; ++ch) {
                int x = 0;
                while (x < w) {
                    if (pos >= n) {
                        setFail(QStringLiteral("hdr rle eof"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    const uchar code = p[pos++];
                    if (code > 128) {
                        const int cnt = code & 127;
                        if (pos >= n || x + cnt > w) {
                            setFail(QStringLiteral("hdr rle overflow"), LoadErrorCategory::Corrupt, errOut, catOut);
                            return QImage();
                        }
                        const uchar val = p[pos++];
                        for (int k = 0; k < cnt; ++k) {
                            scan[size_t(ch) * w + x++] = val;
                        }
                    } else {
                        const int cnt = code;
                        if (cnt == 0 || pos + cnt > n || x + cnt > w) {
                            setFail(QStringLiteral("hdr rle raw overflow"), LoadErrorCategory::Corrupt, errOut, catOut);
                            return QImage();
                        }
                        for (int k = 0; k < cnt; ++k) {
                            scan[size_t(ch) * w + x++] = p[pos++];
                        }
                    }
                }
            }
        } else {
            // Flat / Old RLE
            int x = 0;
            int rshift = 0;
            while (x < w) {
                if (pos + 4 > n) {
                    setFail(QStringLiteral("hdr flat eof"), LoadErrorCategory::Corrupt, errOut, catOut);
                    return QImage();
                }
                const uchar r = p[pos++];
                const uchar g = p[pos++];
                const uchar b = p[pos++];
                const uchar e = p[pos++];
                if (r == 1 && g == 1 && b == 1) {
                    if (x == 0 || rshift >= 24) {
                        setFail(QStringLiteral("hdr old rle invalid"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    const int cnt = int(e) << rshift;
                    rshift += 8;
                    const uchar pr = scan[size_t(x - 1)];
                    const uchar pg = scan[size_t(w) + x - 1];
                    const uchar pb = scan[size_t(w) * 2 + x - 1];
                    const uchar pe = scan[size_t(w) * 3 + x - 1];
                    if (x + cnt > w) {
                        setFail(QStringLiteral("hdr old rle overflow"), LoadErrorCategory::Corrupt, errOut, catOut);
                        return QImage();
                    }
                    for (int k = 0; k < cnt; ++k) {
                        scan[size_t(x)] = pr;
                        scan[size_t(w) + x] = pg;
                        scan[size_t(w) * 2 + x] = pb;
                        scan[size_t(w) * 3 + x] = pe;
                        ++x;
                    }
                } else {
                    scan[size_t(x)] = r;
                    scan[size_t(w) + x] = g;
                    scan[size_t(w) * 2 + x] = b;
                    scan[size_t(w) * 3 + x] = e;
                    ++x;
                    rshift = 0;
                }
            }
        }

        const int dstY = flipY ? (h - 1 - y) : y;
        uchar *dst = img.scanLine(dstY);
        for (int x = 0; x < w; ++x) {
            const int sx = flipX ? (w - 1 - x) : x;
            const uchar e = scan[size_t(w) * 3 + sx];
            if (e == 0) {
                dst[x * 3 + 0] = 0;
                dst[x * 3 + 1] = 0;
                dst[x * 3 + 2] = 0;
            } else {
                const float f = std::ldexp(1.0f, int(e) - 128 - 8) * invExp;
                dst[x * 3 + 0] = linearToSrgbByte((scan[size_t(sx)] + 0.5f) * f);
                dst[x * 3 + 1] = linearToSrgbByte((scan[size_t(w) + sx] + 0.5f) * f);
                dst[x * 3 + 2] = linearToSrgbByte((scan[size_t(w) * 2 + sx] + 0.5f) * f);
            }
        }
    }
    return img;
}

// ===========================================================================
// 3. Windows WIC（JPEG XR .jxr/.wdp/.hdp + DDS BC6H/BC7 回退）
// ===========================================================================
#ifdef Q_OS_WIN
struct ComScope {
    bool initialized = false;
    ComScope()
    {
        const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        initialized = (hr == S_OK || hr == S_FALSE);
    }
    ~ComScope()
    {
        if (initialized) {
            CoUninitialize();
        }
    }
};

QImage loadViaWic(const QString &path, QString *errOut, LoadErrorCategory *catOut)
{
    ComScope com;
    IWICImagingFactory *factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        setFail(QStringLiteral("wic factory failed"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    const std::wstring wpath = path.toStdWString();
    IWICBitmapDecoder *decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr) || !decoder) {
        factory->Release();
        setFail(QStringLiteral("wic decoder failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    IWICBitmapFrameDecode *frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr) || !frame) {
        decoder->Release();
        factory->Release();
        setFail(QStringLiteral("wic frame failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!validRasterSize(int(w), int(h))) {
        frame->Release();
        decoder->Release();
        factory->Release();
        setFail(QStringLiteral("wic size out of bounds"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    IWICFormatConverter *conv = nullptr;
    hr = factory->CreateFormatConverter(&conv);
    if (SUCCEEDED(hr) && conv) {
        hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                              WICBitmapDitherTypeNone, nullptr, 0.0,
                              WICBitmapPaletteTypeCustom);
    }
    QImage out;
    if (SUCCEEDED(hr) && conv) {
        out = QImage(int(w), int(h), QImage::Format_ARGB32);
        if (!out.isNull()) {
            hr = conv->CopyPixels(nullptr, UINT(out.bytesPerLine()),
                                  UINT(out.bytesPerLine()) * h, out.bits());
            if (FAILED(hr)) {
                out = QImage();
            }
        }
    }

    if (conv) conv->Release();
    frame->Release();
    decoder->Release();
    factory->Release();

    if (out.isNull()) {
        setFail(QStringLiteral("wic convert/copy failed"), LoadErrorCategory::Corrupt, errOut, catOut);
    }
    return out;
}

// ===========================================================================
// 4. Windows GDI+（WMF / EMF）
// ===========================================================================
bool ensureGdiplus()
{
    static std::once_flag once;
    static bool ok = false;
    std::call_once(once, []() {
        ULONG_PTR token = 0;
        Gdiplus::GdiplusStartupInput input;
        ok = (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);
    });
    return ok;
}

QImage loadMetafileGdiplus(const QString &path, const QSize &box, bool allowUpscale,
                           QString *errOut, LoadErrorCategory *catOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setFail(QStringLiteral("metafile open failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    if (f.size() > kMaxVectorBytes) {
        setFail(QStringLiteral("metafile exceeds 8 MB"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    f.close();

    if (!ensureGdiplus()) {
        setFail(QStringLiteral("gdiplus init failed"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    const std::wstring wpath = path.toStdWString();
    Gdiplus::Metafile meta(wpath.c_str());
    if (meta.GetLastStatus() != Gdiplus::Ok) {
        setFail(QStringLiteral("metafile load failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const int natW = int(meta.GetWidth());
    const int natH = int(meta.GetHeight());
    const QSize target = clampVectorSize(natW, natH, box, allowUpscale);
    if (!target.isValid() || target.isEmpty()) {
        setFail(QStringLiteral("metafile invalid size"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    Gdiplus::Bitmap bmp(target.width(), target.height(), PixelFormat32bppPARGB);
    if (bmp.GetLastStatus() != Gdiplus::Ok) {
        setFail(QStringLiteral("metafile bitmap alloc failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    {
        Gdiplus::Graphics g(&bmp);
        g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        g.DrawImage(&meta, 0, 0, target.width(), target.height());
    }

    Gdiplus::Rect rc(0, 0, target.width(), target.height());
    Gdiplus::BitmapData bd{};
    if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Gdiplus::Ok) {
        setFail(QStringLiteral("metafile lockbits failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    QImage img(target, QImage::Format_ARGB32_Premultiplied);
    if (!img.isNull()) {
        const uchar *src = static_cast<const uchar *>(bd.Scan0);
        const int rowBytes = target.width() * 4;
        for (int y = 0; y < target.height(); ++y) {
            std::memcpy(img.scanLine(y), src + size_t(y) * bd.Stride, size_t(rowBytes));
        }
    }
    bmp.UnlockBits(&bd);
    return img.convertToFormat(QImage::Format_ARGB32);
}
#endif

// ===========================================================================
// 5. DDS (.dds) — Uncompressed + BC1/BC2/BC3/BC4/BC5 + Windows WIC BC6H/BC7
// ===========================================================================
inline void decodeRgb565(quint16 c, uchar &r, uchar &g, uchar &b)
{
    const int r5 = (c >> 11) & 0x1F;
    const int g6 = (c >> 5) & 0x3F;
    const int b5 = c & 0x1F;
    r = uchar((r5 << 3) | (r5 >> 2));
    g = uchar((g6 << 2) | (g6 >> 4));
    b = uchar((b5 << 3) | (b5 >> 2));
}

void decodeBc1Block(const uchar *src, uchar out[16][4], bool allowAlpha)
{
    const quint16 c0 = rd16le(src);
    const quint16 c1 = rd16le(src + 2);
    uchar pal[4][4];
    decodeRgb565(c0, pal[0][0], pal[0][1], pal[0][2]);
    pal[0][3] = 255;
    decodeRgb565(c1, pal[1][0], pal[1][1], pal[1][2]);
    pal[1][3] = 255;
    if (c0 > c1 || !allowAlpha) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = uchar((2 * pal[0][i] + pal[1][i] + 1) / 3);
            pal[3][i] = uchar((pal[0][i] + 2 * pal[1][i] + 1) / 3);
        }
        pal[2][3] = 255;
        pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = uchar((pal[0][i] + pal[1][i]) / 2);
            pal[3][i] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    const quint32 bits = rd32le(src + 4);
    for (int i = 0; i < 16; ++i) {
        const int idx = (bits >> (i * 2)) & 0x03;
        std::memcpy(out[i], pal[idx], 4);
    }
}

void decodeBc4Channel(const uchar *src, uchar out[16])
{
    const uchar a0 = src[0];
    const uchar a1 = src[1];
    uchar lut[8];
    lut[0] = a0;
    lut[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i <= 6; ++i) {
            lut[i + 1] = uchar(((7 - i) * a0 + i * a1 + 3) / 7);
        }
    } else {
        for (int i = 1; i <= 4; ++i) {
            lut[i + 1] = uchar(((5 - i) * a0 + i * a1 + 2) / 5);
        }
        lut[6] = 0;
        lut[7] = 255;
    }
    quint64 bits = 0;
    for (int i = 0; i < 6; ++i) {
        bits |= quint64(src[2 + i]) << (i * 8);
    }
    for (int i = 0; i < 16; ++i) {
        out[i] = lut[(bits >> (i * 3)) & 0x07];
    }
}

QImage decodeDds(const QString &path, const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 128 || !data.startsWith("DDS ")) {
        setFail(QStringLiteral("dds invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 dwSize = rd32le(p + 4);
    const quint32 h = rd32le(p + 12);
    const quint32 w = rd32le(p + 16);
    if (dwSize != 124 || !validRasterSize(int(w), int(h))) {
        setFail(QStringLiteral("dds invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const quint32 pfFlags = rd32le(p + 80);
    const quint32 fourCC = rd32le(p + 84);
    const quint32 rgbBits = rd32le(p + 88);
    const quint32 rMask = rd32le(p + 92);
    const quint32 gMask = rd32le(p + 96);
    const quint32 bMask = rd32le(p + 100);
    const quint32 aMask = rd32le(p + 104);

    size_t offset = 128;
    quint32 dxgiFmt = 0;
    if ((pfFlags & 0x4u) && fourCC == 0x30315844u) {   // "DX10"
        if (data.size() < 148) {
            setFail(QStringLiteral("dds dx10 truncated"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        dxgiFmt = rd32le(p + 128);
        offset = 148;
    }

    enum class Kind { None, BC1, BC2, BC3, BC4, BC5, RGBA8, BGRA8, BGRX8, BGR8, RGB8, R8, RGBA16F, RGBA32F, Wic };
    Kind kind = Kind::None;
    if (dxgiFmt != 0) {
        switch (dxgiFmt) {
        case 70: case 71: case 72: kind = Kind::BC1; break;
        case 73: case 74: case 75: kind = Kind::BC2; break;
        case 76: case 77: case 78: kind = Kind::BC3; break;
        case 79: case 80: case 81: kind = Kind::BC4; break;
        case 82: case 83: case 84: kind = Kind::BC5; break;
        case 28: case 29:          kind = Kind::RGBA8; break;
        case 87: case 91:          kind = Kind::BGRA8; break;
        case 88: case 93:          kind = Kind::BGRX8; break;
        case 61:                   kind = Kind::R8; break;
        case 10:                   kind = Kind::RGBA16F; break;
        case 2:                    kind = Kind::RGBA32F; break;
        case 95: case 96: case 97: case 98: case 99: kind = Kind::Wic; break;   // BC6H / BC7
        default: kind = Kind::Wic; break;
        }
    } else if (pfFlags & 0x4u) {   // DDPF_FOURCC
        if (fourCC == 0x31545844u) kind = Kind::BC1;        // DXT1
        else if (fourCC == 0x33545844u) kind = Kind::BC2;   // DXT3
        else if (fourCC == 0x35545844u) kind = Kind::BC3;   // DXT5
        else if (fourCC == 0x31495441u || fourCC == 0x55344342u) kind = Kind::BC4;   // ATI1 / BC4U
        else if (fourCC == 0x32495441u || fourCC == 0x55354342u) kind = Kind::BC5;   // ATI2 / BC5U
        else if (fourCC == 113) kind = Kind::RGBA16F;
        else if (fourCC == 116) kind = Kind::RGBA32F;
        else kind = Kind::Wic;
    } else if (pfFlags & 0x40u) {   // DDPF_RGB
        if (rgbBits == 32) {
            if (rMask == 0x00FF0000u && gMask == 0x0000FF00u && bMask == 0x000000FFu) {
                kind = (aMask == 0xFF000000u) ? Kind::BGRA8 : Kind::BGRX8;
            } else if (rMask == 0x000000FFu && gMask == 0x0000FF00u && bMask == 0x00FF0000u) {
                kind = Kind::RGBA8;
            }
        } else if (rgbBits == 24) {
            kind = (rMask == 0x00FF0000u) ? Kind::BGR8 : Kind::RGB8;
        }
    } else if (pfFlags & 0x20000u && rgbBits == 8) {   // DDPF_LUMINANCE
        kind = Kind::R8;
    }

    if (kind == Kind::Wic) {
#ifdef Q_OS_WIN
        return loadViaWic(path, errOut, catOut);
#else
        Q_UNUSED(path);
        setFail(QStringLiteral("dds format requires wic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
#endif
    }
    if (kind == Kind::None) {
        setFail(QStringLiteral("dds unsupported pixel format"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    const uchar *src = p + offset;
    const size_t avail = size_t(data.size()) - offset;

    if (kind == Kind::BC1 || kind == Kind::BC2 || kind == Kind::BC3 || kind == Kind::BC4 || kind == Kind::BC5) {
        const quint32 bx = (w + 3) / 4;
        const quint32 by = (h + 3) / 4;
        const size_t blockBytes = (kind == Kind::BC1 || kind == Kind::BC4) ? 8 : 16;
        if (avail < quint64(bx) * by * blockBytes) {
            setFail(QStringLiteral("dds truncated block data"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        QImage img(int(w), int(h), QImage::Format_RGBA8888);
        if (img.isNull()) return QImage();
        for (quint32 y = 0; y < by; ++y) {
            for (quint32 x = 0; x < bx; ++x) {
                const uchar *blk = src + (size_t(y) * bx + x) * blockBytes;
                uchar px[16][4];
                if (kind == Kind::BC1) {
                    decodeBc1Block(blk, px, true);
                } else if (kind == Kind::BC2) {
                    decodeBc1Block(blk + 8, px, false);
                    for (int i = 0; i < 16; ++i) {
                        const uchar nib = (blk[i / 2] >> ((i & 1) * 4)) & 0x0F;
                        px[i][3] = uchar(nib | (nib << 4));
                    }
                } else if (kind == Kind::BC3) {
                    uchar a[16];
                    decodeBc4Channel(blk, a);
                    decodeBc1Block(blk + 8, px, false);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[i];
                } else if (kind == Kind::BC4) {
                    uchar g[16];
                    decodeBc4Channel(blk, g);
                    for (int i = 0; i < 16; ++i) {
                        px[i][0] = px[i][1] = px[i][2] = g[i];
                        px[i][3] = 255;
                    }
                } else {   // BC5
                    uchar r[16], g[16];
                    decodeBc4Channel(blk, r);
                    decodeBc4Channel(blk + 8, g);
                    for (int i = 0; i < 16; ++i) {
                        const float nx = r[i] / 127.5f - 1.0f;
                        const float ny = g[i] / 127.5f - 1.0f;
                        const float nz = std::sqrt(qMax(0.0f, 1.0f - nx * nx - ny * ny));
                        px[i][0] = r[i];
                        px[i][1] = g[i];
                        px[i][2] = uchar(std::clamp(int(std::lround((nz * 0.5f + 0.5f) * 255.0f)), 0, 255));
                        px[i][3] = 255;
                    }
                }
                for (int py = 0; py < 4; ++py) {
                    const quint32 iy = y * 4 + py;
                    if (iy >= h) break;
                    uchar *dst = img.scanLine(int(iy));
                    for (int pxIdx = 0; pxIdx < 4; ++pxIdx) {
                        const quint32 ix = x * 4 + pxIdx;
                        if (ix >= w) break;
                        std::memcpy(dst + ix * 4, px[py * 4 + pxIdx], 4);
                    }
                }
            }
        }
        return img;
    }

    const size_t bpp = (kind == Kind::RGBA16F) ? 8
                     : (kind == Kind::RGBA32F) ? 16
                     : (kind == Kind::BGR8 || kind == Kind::RGB8) ? 3
                     : (kind == Kind::R8) ? 1 : 4;
    if (avail < quint64(w) * h * bpp) {
        setFail(QStringLiteral("dds truncated pixels"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    QImage img(int(w), int(h), QImage::Format_RGBA8888);
    if (img.isNull()) return QImage();
    for (quint32 y = 0; y < h; ++y) {
        const uchar *row = src + size_t(y) * w * bpp;
        uchar *dst = img.scanLine(int(y));
        for (quint32 x = 0; x < w; ++x) {
            const uchar *sp = row + x * bpp;
            uchar *dp = dst + x * 4;
            switch (kind) {
            case Kind::RGBA8: dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = sp[3]; break;
            case Kind::BGRA8: dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = sp[3]; break;
            case Kind::BGRX8: dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = 255; break;
            case Kind::RGB8:  dp[0] = sp[0]; dp[1] = sp[1]; dp[2] = sp[2]; dp[3] = 255; break;
            case Kind::BGR8:  dp[0] = sp[2]; dp[1] = sp[1]; dp[2] = sp[0]; dp[3] = 255; break;
            case Kind::R8:    dp[0] = dp[1] = dp[2] = sp[0]; dp[3] = 255; break;
            case Kind::RGBA16F: {
                dp[0] = linearToSrgbByte(halfToFloat(rd16le(sp + 0)));
                dp[1] = linearToSrgbByte(halfToFloat(rd16le(sp + 2)));
                dp[2] = linearToSrgbByte(halfToFloat(rd16le(sp + 4)));
                dp[3] = uchar(std::clamp(int(std::lround(halfToFloat(rd16le(sp + 6)) * 255.0f)), 0, 255));
                break;
            }
            case Kind::RGBA32F: {
                float rgba[4];
                std::memcpy(rgba, sp, 16);
                dp[0] = linearToSrgbByte(rgba[0]);
                dp[1] = linearToSrgbByte(rgba[1]);
                dp[2] = linearToSrgbByte(rgba[2]);
                dp[3] = uchar(std::clamp(int(std::lround(rgba[3] * 255.0f)), 0, 255));
                break;
            }
            default: break;
            }
        }
    }
    return img;
}

// ===========================================================================
// 6. ANI (.ani) — Windows Animated Cursor (RIFF / ACON)
// ===========================================================================
bool decodeAniFrames(const QByteArray &data, QVector<QImage> &frames, QVector<int> &delaysMs,
                     QString *errOut, LoadErrorCategory *catOut)
{
    frames.clear();
    delaysMs.clear();
    if (data.size() < 12 || std::memcmp(data.constData(), "RIFF", 4) != 0
        || std::memcmp(data.constData() + 8, "ACON", 4) != 0) {
        setFail(QStringLiteral("ani invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return false;
    }

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const size_t n = size_t(data.size());
    quint32 defaultJiffies = 6;   // 100ms 默认
    QVector<quint32> rates;
    QVector<quint32> seq;
    QVector<QImage> rawFrames;

    std::function<void(size_t, size_t)> walk = [&](size_t start, size_t end) {
        size_t pos = start;
        while (pos + 8 <= end) {
            const char *id = reinterpret_cast<const char *>(p + pos);
            const quint32 sz = rd32le(p + pos + 4);
            pos += 8;
            if (pos + sz > end) {
                break;
            }
            if (std::memcmp(id, "LIST", 4) == 0 && sz >= 4) {
                walk(pos + 4, pos + sz);
            } else if (std::memcmp(id, "anih", 4) == 0 && sz >= 36) {
                const quint32 dispRate = rd32le(p + pos + 28);
                if (dispRate > 0) defaultJiffies = dispRate;
            } else if (std::memcmp(id, "rate", 4) == 0) {
                for (size_t i = 0; i + 4 <= sz; i += 4) {
                    rates.append(rd32le(p + pos + i));
                }
            } else if (std::memcmp(id, "seq ", 4) == 0) {
                for (size_t i = 0; i + 4 <= sz; i += 4) {
                    seq.append(rd32le(p + pos + i));
                }
            } else if (std::memcmp(id, "icon", 4) == 0 && rawFrames.size() < kMaxAnimFrames) {
                const QByteArray chunk = QByteArray::fromRawData(reinterpret_cast<const char *>(p + pos), int(sz));
                QImage img = QImage::fromData(chunk, "CUR");
                if (img.isNull()) img = QImage::fromData(chunk, "ICO");
                if (!img.isNull()) rawFrames.append(img);
            }
            pos += sz + (sz & 1u);
        }
    };
    walk(12, n);

    if (rawFrames.isEmpty()) {
        setFail(QStringLiteral("ani contains no decodable frames"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }

    const int steps = seq.isEmpty() ? rawFrames.size() : qMin(seq.size(), kMaxAnimFrames);
    quint64 totalBytes = 0;
    for (int i = 0; i < steps; ++i) {
        const int idx = seq.isEmpty() ? i : int(seq.at(i));
        if (idx < 0 || idx >= rawFrames.size()) continue;
        const QImage &f = rawFrames.at(idx);
        totalBytes += quint64(f.sizeInBytes());
        if (totalBytes > kMaxAnimTotalBytes) break;
        frames.append(f);
        const quint32 jif = (i < rates.size() && rates.at(i) > 0) ? rates.at(i) : defaultJiffies;
        delaysMs.append(qMax(16, int(qRound(jif * 1000.0 / 60.0))));
    }
    return !frames.isEmpty();
}

// ===========================================================================
// 7. PCX (.pcx)
// ===========================================================================
QImage decodePcx(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 128 || static_cast<uchar>(data.at(0)) != 0x0A) {
        setFail(QStringLiteral("pcx invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const int encoding = p[2];
    const int bpp = p[3];
    const int xmin = rd16le(p + 4);
    const int ymin = rd16le(p + 6);
    const int xmax = rd16le(p + 8);
    const int ymax = rd16le(p + 10);
    const int w = xmax - xmin + 1;
    const int h = ymax - ymin + 1;
    const int nPlanes = p[65];
    const int bytesPerLine = rd16le(p + 66);
    if (!validRasterSize(w, h) || bytesPerLine <= 0 || (encoding != 0 && encoding != 1)) {
        setFail(QStringLiteral("pcx invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const int totalPlanes = nPlanes;
    if (!((bpp == 8 && (totalPlanes == 1 || totalPlanes == 3 || totalPlanes == 4))
          || ((bpp == 1 || bpp == 4) && totalPlanes == 1)
          || (bpp == 1 && totalPlanes == 4))) {
        setFail(QStringLiteral("pcx unsupported bpp/planes"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    const int scanBytes = bytesPerLine * totalPlanes;
    std::vector<uchar> scan(size_t(scanBytes), 0);
    size_t pos = 128;
    const size_t end = size_t(data.size());

    // 256 色调色板位于文件尾 769 字节（首字节 0x0C）
    bool hasVgaPal = false;
    const uchar *vgaPal = nullptr;
    if (bpp == 8 && totalPlanes == 1 && data.size() >= 128 + 769 && p[data.size() - 769] == 0x0C) {
        hasVgaPal = true;
        vgaPal = p + data.size() - 768;
    }

    const bool hasAlpha = (bpp == 8 && totalPlanes == 4);
    QImage img(w, h, hasAlpha ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (img.isNull()) return QImage();

    for (int y = 0; y < h; ++y) {
        int outPos = 0;
        while (outPos < scanBytes) {
            if (pos >= end) {
                setFail(QStringLiteral("pcx truncated scanline"), LoadErrorCategory::Corrupt, errOut, catOut);
                return QImage();
            }
            const uchar b = p[pos++];
            if (encoding == 1 && (b & 0xC0) == 0xC0) {
                const int cnt = b & 0x3F;
                if (pos >= end) {
                    setFail(QStringLiteral("pcx rle eof"), LoadErrorCategory::Corrupt, errOut, catOut);
                    return QImage();
                }
                const uchar val = p[pos++];
                for (int k = 0; k < cnt && outPos < scanBytes; ++k) {
                    scan[size_t(outPos++)] = val;
                }
            } else {
                scan[size_t(outPos++)] = b;
            }
        }

        uchar *dst = img.scanLine(y);
        if (bpp == 8 && totalPlanes == 3) {
            for (int x = 0; x < w; ++x) {
                dst[x * 3 + 0] = scan[size_t(x)];
                dst[x * 3 + 1] = scan[size_t(bytesPerLine) + x];
                dst[x * 3 + 2] = scan[size_t(bytesPerLine) * 2 + x];
            }
        } else if (bpp == 8 && totalPlanes == 4) {
            for (int x = 0; x < w; ++x) {
                dst[x * 4 + 0] = scan[size_t(x)];
                dst[x * 4 + 1] = scan[size_t(bytesPerLine) + x];
                dst[x * 4 + 2] = scan[size_t(bytesPerLine) * 2 + x];
                dst[x * 4 + 3] = scan[size_t(bytesPerLine) * 3 + x];
            }
        } else if (bpp == 8 && totalPlanes == 1) {
            for (int x = 0; x < w; ++x) {
                const uchar idx = scan[size_t(x)];
                if (hasVgaPal) {
                    dst[x * 3 + 0] = vgaPal[idx * 3 + 0];
                    dst[x * 3 + 1] = vgaPal[idx * 3 + 1];
                    dst[x * 3 + 2] = vgaPal[idx * 3 + 2];
                } else {
                    dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = idx;
                }
            }
        } else if (bpp == 4 && totalPlanes == 1) {
            for (int x = 0; x < w; ++x) {
                const uchar byte = scan[size_t(x >> 1)];
                const int idx = (x & 1) ? (byte & 0x0F) : (byte >> 4);
                dst[x * 3 + 0] = p[16 + idx * 3 + 0];
                dst[x * 3 + 1] = p[16 + idx * 3 + 1];
                dst[x * 3 + 2] = p[16 + idx * 3 + 2];
            }
        } else if (bpp == 1 && totalPlanes == 4) {
            for (int x = 0; x < w; ++x) {
                const int bit = 7 - (x & 7);
                const int bi = x >> 3;
                int idx = 0;
                for (int pl = 0; pl < 4; ++pl) {
                    idx |= ((scan[size_t(pl) * bytesPerLine + bi] >> bit) & 1) << pl;
                }
                dst[x * 3 + 0] = p[16 + idx * 3 + 0];
                dst[x * 3 + 1] = p[16 + idx * 3 + 1];
                dst[x * 3 + 2] = p[16 + idx * 3 + 2];
            }
        } else {   // bpp == 1 && totalPlanes == 1
            for (int x = 0; x < w; ++x) {
                const uchar v = ((scan[size_t(x >> 3)] >> (7 - (x & 7))) & 1) ? 255 : 0;
                dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = v;
            }
        }
    }
    return img;
}

// ===========================================================================
// 8. SGI (.sgi, .rgb, .rgba, .bw, .int, .inta)
// ===========================================================================
QImage decodeSgi(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 512) {
        setFail(QStringLiteral("sgi header truncated"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    if (rd16be(p) != 0x01DAu) {
        setFail(QStringLiteral("sgi invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const int storage = p[2];
    const int bpc = p[3];
    const int dim = rd16be(p + 4);
    const int w = rd16be(p + 6);
    const int h = (dim >= 2) ? rd16be(p + 8) : 1;
    const int z = (dim >= 3) ? rd16be(p + 10) : 1;
    if ((storage != 0 && storage != 1) || (bpc != 1 && bpc != 2)
        || !validRasterSize(w, h) || (z != 1 && z != 3 && z != 4)) {
        setFail(QStringLiteral("sgi unsupported parameters"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    std::vector<uchar> planes(size_t(w) * h * z, 0);
    if (storage == 0) {
        const size_t need = 512 + size_t(w) * h * z * bpc;
        if (size_t(data.size()) < need) {
            setFail(QStringLiteral("sgi truncated verbatim"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        const uchar *src = p + 512;
        if (bpc == 1) {
            std::memcpy(planes.data(), src, planes.size());
        } else {
            for (size_t i = 0; i < planes.size(); ++i) {
                planes[i] = src[i * 2];   // BE 16-bit 高字节
            }
        }
    } else {
        const size_t tabEntries = size_t(h) * z;
        if (size_t(data.size()) < 512 + tabEntries * 8) {
            setFail(QStringLiteral("sgi rle table truncated"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        const uchar *startTab = p + 512;
        for (int ch = 0; ch < z; ++ch) {
            for (int y = 0; y < h; ++y) {
                const size_t ti = size_t(ch) * h + y;
                const quint32 off = rd32be(startTab + ti * 4);
                if (off >= quint32(data.size())) {
                    setFail(QStringLiteral("sgi rle offset invalid"), LoadErrorCategory::Corrupt, errOut, catOut);
                    return QImage();
                }
                size_t sp = off;
                int x = 0;
                uchar *dstRow = planes.data() + (size_t(ch) * h + y) * w;
                while (x < w) {
                    if (sp + bpc > size_t(data.size())) break;
                    const int pix = (bpc == 1) ? p[sp++] : (sp += 2, rd16be(p + sp - 2));
                    const int count = pix & 0x7F;
                    if (count == 0) break;
                    if (pix & 0x80) {
                        for (int k = 0; k < count && x < w && sp + bpc <= size_t(data.size()); ++k) {
                            dstRow[x++] = (bpc == 1) ? p[sp++] : (sp += 2, p[sp - 2]);
                        }
                    } else {
                        if (sp + bpc > size_t(data.size())) break;
                        const uchar val = (bpc == 1) ? p[sp++] : (sp += 2, p[sp - 2]);
                        for (int k = 0; k < count && x < w; ++k) {
                            dstRow[x++] = val;
                        }
                    }
                }
            }
        }
    }

    // SGI 坐标原点在左下角：需垂直翻转
    const QImage::Format fmt = (z == 1) ? QImage::Format_Grayscale8
                             : (z == 3) ? QImage::Format_RGB888
                                        : QImage::Format_RGBA8888;
    QImage img(w, h, fmt);
    if (img.isNull()) return QImage();
    for (int y = 0; y < h; ++y) {
        const int sy = h - 1 - y;
        uchar *dst = img.scanLine(y);
        if (z == 1) {
            std::memcpy(dst, planes.data() + size_t(sy) * w, size_t(w));
        } else {
            for (int x = 0; x < w; ++x) {
                for (int ch = 0; ch < z; ++ch) {
                    dst[x * z + ch] = planes[(size_t(ch) * h + sy) * w + x];
                }
            }
        }
    }
    return img;
}

// ===========================================================================
// 9. PAM (P7) & PFM (PF / Pf)
// ===========================================================================
QImage decodePam(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (!data.startsWith("P7\n") && !data.startsWith("P7\r\n")) {
        setFail(QStringLiteral("pam invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    int pos = 0;
    const int n = data.size();
    int w = 0, h = 0, depth = 0, maxval = 0;
    bool endhdr = false;
    while (pos < n) {
        const int lineStart = pos;
        while (pos < n && data.at(pos) != '\n') ++pos;
        QByteArray line = data.mid(lineStart, pos - lineStart).trimmed();
        if (pos < n && data.at(pos) == '\n') ++pos;
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line == "ENDHDR") {
            endhdr = true;
            break;
        }
        const QList<QByteArray> tok = line.simplified().split(' ');
        if (tok.size() >= 2) {
            if (tok[0] == "WIDTH") w = tok[1].toInt();
            else if (tok[0] == "HEIGHT") h = tok[1].toInt();
            else if (tok[0] == "DEPTH") depth = tok[1].toInt();
            else if (tok[0] == "MAXVAL") maxval = tok[1].toInt();
        }
    }
    if (!endhdr || !validRasterSize(w, h) || (depth < 1 || depth > 4)
        || maxval <= 0 || maxval > 65535) {
        setFail(QStringLiteral("pam invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const int bpc = (maxval > 255) ? 2 : 1;
    if (size_t(n - pos) < size_t(w) * h * depth * bpc) {
        setFail(QStringLiteral("pam truncated data"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const uchar *src = reinterpret_cast<const uchar *>(data.constData() + pos);
    const QImage::Format fmt = (depth == 1) ? QImage::Format_Grayscale8
                             : (depth == 3) ? QImage::Format_RGB888
                                            : QImage::Format_RGBA8888;
    QImage img(w, h, fmt);
    if (img.isNull()) return QImage();

    auto sample = [src, bpc, maxval](size_t idx) -> uchar {
        const quint32 v = (bpc == 1) ? src[idx] : rd16be(src + idx * 2);
        return uchar((v * 255u + quint32(maxval / 2)) / quint32(maxval));
    };

    for (int y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t base = (size_t(y) * w + x) * depth;
            if (depth == 1) {
                dst[x] = sample(base);
            } else if (depth == 2) {
                const uchar g = sample(base);
                dst[x * 4 + 0] = g;
                dst[x * 4 + 1] = g;
                dst[x * 4 + 2] = g;
                dst[x * 4 + 3] = sample(base + 1);
            } else {
                for (int c = 0; c < depth; ++c) {
                    dst[x * depth + c] = sample(base + c);
                }
            }
        }
    }
    return img;
}

QImage decodePfm(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 8 || (!data.startsWith("PF") && !data.startsWith("Pf"))) {
        setFail(QStringLiteral("pfm invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const int channels = data.startsWith("PF") ? 3 : 1;
    int pos = 0;
    const int n = data.size();
    auto readLine = [&data, &pos, n]() -> QByteArray {
        const int s = pos;
        while (pos < n && data.at(pos) != '\n') ++pos;
        QByteArray l = data.mid(s, pos - s).trimmed();
        if (pos < n && data.at(pos) == '\n') ++pos;
        return l;
    };
    readLine();   // PF / Pf
    const QList<QByteArray> wh = readLine().simplified().split(' ');
    if (wh.size() != 2) {
        setFail(QStringLiteral("pfm invalid size line"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const int w = wh[0].toInt();
    const int h = wh[1].toInt();
    const float scale = readLine().toFloat();
    if (!validRasterSize(w, h) || scale == 0.0f || !std::isfinite(scale)) {
        setFail(QStringLiteral("pfm invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const bool le = (scale < 0.0f);
    const size_t pxBytes = size_t(w) * h * channels * 4;
    if (size_t(n - pos) < pxBytes) {
        setFail(QStringLiteral("pfm truncated pixels"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const uchar *src = reinterpret_cast<const uchar *>(data.constData() + pos);
    auto readFloat = [src, le](size_t idx) -> float {
        const quint32 u = le ? rd32le(src + idx * 4) : rd32be(src + idx * 4);
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    };

    // 探测是否超过 1.0（HDR 浮点图走 ACES，[0,1] 图直出以免偏暗）
    bool isHdr = false;
    const size_t totalSamples = size_t(w) * h * channels;
    const size_t step = qMax<size_t>(1, totalSamples / 10000);
    for (size_t i = 0; i < totalSamples; i += step) {
        const float v = readFloat(i);
        if (std::isfinite(v) && v > 1.01f) {
            isHdr = true;
            break;
        }
    }

    QImage img(w, h, channels == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    if (img.isNull()) return QImage();
    for (int y = 0; y < h; ++y) {
        const int sy = h - 1 - y;   // PFM 默认底行优先
        uchar *dst = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const size_t base = (size_t(sy) * w + x) * channels;
            for (int c = 0; c < channels; ++c) {
                const float v = readFloat(base + c);
                dst[x * channels + c] = isHdr ? linearToSrgbByte(v) : directSrgbByte(v);
            }
        }
    }
    return img;
}

// ===========================================================================
// 10. Sun Raster (.ras, .sun)
// ===========================================================================
QImage decodeSunRaster(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 32) {
        setFail(QStringLiteral("ras header truncated"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    if (rd32be(p) != 0x59A66A95u) {
        setFail(QStringLiteral("ras invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const quint32 w = rd32be(p + 4);
    const quint32 h = rd32be(p + 8);
    const quint32 depth = rd32be(p + 12);
    const quint32 type = rd32be(p + 20);
    const quint32 mapType = rd32be(p + 24);
    const quint32 mapLen = rd32be(p + 28);
    if (!validRasterSize(int(w), int(h)) || (depth != 1 && depth != 8 && depth != 24 && depth != 32)
        || mapLen > 65536 || 32 + mapLen > quint32(data.size())) {
        setFail(QStringLiteral("ras invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const uchar *pal = p + 32;
    const size_t palCount = (mapType == 1) ? (mapLen / 3) : 0;
    size_t pos = 32 + mapLen;
    const size_t end = size_t(data.size());

    const size_t rowBits = size_t(w) * depth;
    const size_t rowBytes = ((rowBits + 15) / 16) * 2;   // 16 位对齐
    std::vector<uchar> raw(rowBytes * h, 0);

    if (type == 2) {   // RT_BYTE_ENCODED
        size_t outPos = 0;
        while (outPos < raw.size() && pos < end) {
            const uchar b = p[pos++];
            if (b == 0x80) {
                if (pos >= end) break;
                const uchar cnt = p[pos++];
                if (cnt == 0) {
                    raw[outPos++] = 0x80;
                } else {
                    if (pos >= end) break;
                    const uchar val = p[pos++];
                    for (int k = 0; k <= int(cnt) && outPos < raw.size(); ++k) {
                        raw[outPos++] = val;
                    }
                }
            } else {
                raw[outPos++] = b;
            }
        }
    } else {
        const size_t avail = qMin(raw.size(), end - pos);
        if (avail < rowBytes * h) {
            setFail(QStringLiteral("ras truncated pixels"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        std::memcpy(raw.data(), p + pos, avail);
    }

    const bool rgbOrder = (type == 3);
    QImage img(int(w), int(h), depth == 32 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    if (img.isNull()) return QImage();
    for (quint32 y = 0; y < h; ++y) {
        const uchar *row = raw.data() + size_t(y) * rowBytes;
        uchar *dst = img.scanLine(int(y));
        if (depth == 1) {
            for (quint32 x = 0; x < w; ++x) {
                const uchar bit = (row[x >> 3] >> (7 - (x & 7))) & 1;
                const uchar v = bit ? 0 : 255;
                dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = v;
            }
        } else if (depth == 8) {
            for (quint32 x = 0; x < w; ++x) {
                const uchar idx = row[x];
                if (palCount >= 256) {
                    dst[x * 3 + 0] = pal[idx];
                    dst[x * 3 + 1] = pal[palCount + idx];
                    dst[x * 3 + 2] = pal[palCount * 2 + idx];
                } else {
                    dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = idx;
                }
            }
        } else if (depth == 24) {
            for (quint32 x = 0; x < w; ++x) {
                if (rgbOrder) {
                    dst[x * 3 + 0] = row[x * 3 + 0];
                    dst[x * 3 + 1] = row[x * 3 + 1];
                    dst[x * 3 + 2] = row[x * 3 + 2];
                } else {
                    dst[x * 3 + 0] = row[x * 3 + 2];
                    dst[x * 3 + 1] = row[x * 3 + 1];
                    dst[x * 3 + 2] = row[x * 3 + 0];
                }
            }
        } else {   // 32
            for (quint32 x = 0; x < w; ++x) {
                if (rgbOrder) {
                    dst[x * 4 + 0] = row[x * 4 + 1];
                    dst[x * 4 + 1] = row[x * 4 + 2];
                    dst[x * 4 + 2] = row[x * 4 + 3];
                } else {
                    dst[x * 4 + 0] = row[x * 4 + 3];
                    dst[x * 4 + 1] = row[x * 4 + 2];
                    dst[x * 4 + 2] = row[x * 4 + 1];
                }
                dst[x * 4 + 3] = 255;
            }
        }
    }
    return img;
}

// ===========================================================================
// 11. Farbfeld (.ff)
// ===========================================================================
QImage decodeFarbfeld(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 16 || std::memcmp(data.constData(), "farbfeld", 8) != 0) {
        setFail(QStringLiteral("farbfeld invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 w = rd32be(p + 8);
    const quint32 h = rd32be(p + 12);
    if (!validRasterSize(int(w), int(h))) {
        setFail(QStringLiteral("farbfeld invalid size"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    if (quint64(data.size()) < 16ULL + quint64(w) * h * 8ULL) {
        setFail(QStringLiteral("farbfeld truncated pixels"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    QImage img(int(w), int(h), QImage::Format_RGBA8888);
    if (img.isNull()) return QImage();
    const uchar *src = p + 16;
    for (quint32 y = 0; y < h; ++y) {
        uchar *dst = img.scanLine(int(y));
        const uchar *row = src + size_t(y) * w * 8;
        for (quint32 x = 0; x < w; ++x) {
            dst[x * 4 + 0] = row[x * 8 + 0];
            dst[x * 4 + 1] = row[x * 8 + 2];
            dst[x * 4 + 2] = row[x * 8 + 4];
            dst[x * 4 + 3] = row[x * 8 + 6];
        }
    }
    return img;
}

// ===========================================================================
// 12. XWD (.xwd) — X11 Window Dump v7
// ===========================================================================
inline int maskShift(quint32 mask)
{
    if (mask == 0) return 0;
    int s = 0;
    while (((mask >> s) & 1u) == 0u && s < 32) ++s;
    return s;
}

inline int maskBits(quint32 mask)
{
    int b = 0;
    while (mask) {
        b += int(mask & 1u);
        mask >>= 1;
    }
    return b;
}

inline uchar extractMasked(quint32 val, quint32 mask)
{
    if (mask == 0) return 0;
    const int s = maskShift(mask);
    const int bits = maskBits(mask);
    const quint32 v = (val & mask) >> s;
    if (bits >= 8) return uchar(v >> (bits - 8));
    return uchar((v * 255u) / ((1u << bits) - 1u));
}

QImage decodeXwd(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 100) {
        setFail(QStringLiteral("xwd header truncated"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 hdrSize = rd32be(p + 0);
    const quint32 version = rd32be(p + 4);
    const quint32 pixFmt = rd32be(p + 8);
    const quint32 w = rd32be(p + 16);
    const quint32 h = rd32be(p + 20);
    const quint32 byteOrder = rd32be(p + 28);
    const quint32 bpp = rd32be(p + 44);
    const quint32 bytesPerLine = rd32be(p + 48);
    const quint32 visualClass = rd32be(p + 52);
    const quint32 rMask = rd32be(p + 56);
    const quint32 gMask = rd32be(p + 60);
    const quint32 bMask = rd32be(p + 64);
    const quint32 ncolors = rd32be(p + 76);

    if (version != 7 || hdrSize < 100 || hdrSize > quint32(data.size())
        || !validRasterSize(int(w), int(h)) || pixFmt != 2
        || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        || ncolors > 65536) {
        setFail(QStringLiteral("xwd unsupported format"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const quint64 colorBytes = quint64(ncolors) * 12ULL;
    if (hdrSize + colorBytes > quint64(data.size())) {
        setFail(QStringLiteral("xwd truncated colormap"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    QVector<QRgb> palette(int(ncolors), qRgb(0, 0, 0));
    const uchar *cp = p + hdrSize;
    for (quint32 i = 0; i < ncolors; ++i) {
        const uchar *e = cp + i * 12;
        palette[int(i)] = qRgb(e[4], e[6], e[8]);
    }
    const size_t pixOffset = hdrSize + size_t(colorBytes);
    const size_t rowStride = qMax<size_t>(bytesPerLine, (size_t(w) * bpp + 7) / 8);
    if (pixOffset + rowStride * h > size_t(data.size())) {
        setFail(QStringLiteral("xwd truncated pixels"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const bool msb = (byteOrder == 1);
    QImage img(int(w), int(h), QImage::Format_RGB888);
    if (img.isNull()) return QImage();

    for (quint32 y = 0; y < h; ++y) {
        const uchar *row = p + pixOffset + size_t(y) * rowStride;
        uchar *dst = img.scanLine(int(y));
        for (quint32 x = 0; x < w; ++x) {
            if (bpp == 8) {
                const uchar idx = row[x];
                if (visualClass == 3 && idx < palette.size()) {
                    const QRgb c = palette[idx];
                    dst[x * 3 + 0] = uchar(qRed(c));
                    dst[x * 3 + 1] = uchar(qGreen(c));
                    dst[x * 3 + 2] = uchar(qBlue(c));
                } else {
                    dst[x * 3 + 0] = dst[x * 3 + 1] = dst[x * 3 + 2] = idx;
                }
            } else if (bpp == 24) {
                const quint32 v = msb
                    ? ((quint32(row[x * 3]) << 16) | (quint32(row[x * 3 + 1]) << 8) | row[x * 3 + 2])
                    : (quint32(row[x * 3]) | (quint32(row[x * 3 + 1]) << 8) | (quint32(row[x * 3 + 2]) << 16));
                dst[x * 3 + 0] = extractMasked(v, rMask ? rMask : 0xFF0000u);
                dst[x * 3 + 1] = extractMasked(v, gMask ? gMask : 0x00FF00u);
                dst[x * 3 + 2] = extractMasked(v, bMask ? bMask : 0x0000FFu);
            } else if (bpp == 32) {
                const quint32 v = msb ? rd32be(row + x * 4) : rd32le(row + x * 4);
                dst[x * 3 + 0] = extractMasked(v, rMask ? rMask : 0x00FF0000u);
                dst[x * 3 + 1] = extractMasked(v, gMask ? gMask : 0x0000FF00u);
                dst[x * 3 + 2] = extractMasked(v, bMask ? bMask : 0x000000FFu);
            } else {   // 16
                const quint16 v = msb ? rd16be(row + x * 2) : rd16le(row + x * 2);
                dst[x * 3 + 0] = extractMasked(v, rMask ? rMask : 0xF800u);
                dst[x * 3 + 1] = extractMasked(v, gMask ? gMask : 0x07E0u);
                dst[x * 3 + 2] = extractMasked(v, bMask ? bMask : 0x001Fu);
            }
        }
    }
    return img;
}

// ===========================================================================
// 13. DPX (.dpx) — SMPTE 268M
// ===========================================================================
QImage decodeDpx(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 2048) {
        setFail(QStringLiteral("dpx header truncated"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const bool be = (std::memcmp(p, "SDPX", 4) == 0);
    const bool le = (std::memcmp(p, "XPDS", 4) == 0);
    if (!be && !le) {
        setFail(QStringLiteral("dpx invalid magic"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }
    auto u16 = [p, be](int off) { return be ? rd16be(p + off) : rd16le(p + off); };
    auto u32 = [p, be](int off) { return be ? rd32be(p + off) : rd32le(p + off); };

    const quint32 w = u32(772);
    const quint32 h = u32(776);
    const int descriptor = p[800];
    const int bitSize = p[803];
    const quint16 packing = u16(804);
    const quint32 elemOff = u32(808);
    const quint32 dataOffset = (elemOff != 0xFFFFFFFFu && elemOff >= u32(4)) ? elemOff : u32(4);

    if (!validRasterSize(int(w), int(h)) || dataOffset >= quint32(data.size())) {
        setFail(QStringLiteral("dpx invalid header"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const int channels = (descriptor == 50) ? 3
                       : (descriptor == 51 || descriptor == 52) ? 4
                       : (descriptor == 6) ? 1 : 0;
    if (channels == 0 || (bitSize != 8 && bitSize != 10 && bitSize != 16)) {
        setFail(QStringLiteral("dpx unsupported descriptor/bits"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    const uchar *src = p + dataOffset;
    const size_t avail = size_t(data.size()) - dataOffset;
    const QImage::Format fmt = (channels == 1) ? QImage::Format_Grayscale8
                             : (channels == 3) ? QImage::Format_RGB888
                                               : QImage::Format_RGBA8888;
    QImage img(int(w), int(h), fmt);
    if (img.isNull()) return QImage();

    if (bitSize == 8) {
        if (avail < size_t(w) * h * channels) {
            setFail(QStringLiteral("dpx truncated 8bit"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        for (quint32 y = 0; y < h; ++y) {
            const uchar *row = src + size_t(y) * w * channels;
            uchar *dst = img.scanLine(int(y));
            if (descriptor == 52) {   // ABGR
                for (quint32 x = 0; x < w; ++x) {
                    dst[x * 4 + 0] = row[x * 4 + 3];
                    dst[x * 4 + 1] = row[x * 4 + 2];
                    dst[x * 4 + 2] = row[x * 4 + 1];
                    dst[x * 4 + 3] = row[x * 4 + 0];
                }
            } else {
                std::memcpy(dst, row, size_t(w) * channels);
            }
        }
        return img;
    }

    if (bitSize == 10 && channels == 3) {
        if (avail < size_t(w) * h * 4) {
            setFail(QStringLiteral("dpx truncated 10bit"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        for (quint32 y = 0; y < h; ++y) {
            const uchar *row = src + size_t(y) * w * 4;
            uchar *dst = img.scanLine(int(y));
            for (quint32 x = 0; x < w; ++x) {
                const quint32 word = be ? rd32be(row + x * 4) : rd32le(row + x * 4);
                quint32 r10 = 0, g10 = 0, b10 = 0;
                if (packing == 1) {   // Filled A (MSBs)
                    r10 = (word >> 22) & 0x3FFu;
                    g10 = (word >> 12) & 0x3FFu;
                    b10 = (word >> 2) & 0x3FFu;
                } else {              // Filled B / default
                    b10 = word & 0x3FFu;
                    g10 = (word >> 10) & 0x3FFu;
                    r10 = (word >> 20) & 0x3FFu;
                }
                dst[x * 3 + 0] = uchar(r10 >> 2);
                dst[x * 3 + 1] = uchar(g10 >> 2);
                dst[x * 3 + 2] = uchar(b10 >> 2);
            }
        }
        return img;
    }

    if (bitSize == 16) {
        if (avail < size_t(w) * h * channels * 2) {
            setFail(QStringLiteral("dpx truncated 16bit"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        for (quint32 y = 0; y < h; ++y) {
            const uchar *row = src + size_t(y) * w * channels * 2;
            uchar *dst = img.scanLine(int(y));
            for (quint32 x = 0; x < w; ++x) {
                for (int c = 0; c < channels; ++c) {
                    const quint16 v = be ? rd16be(row + (x * channels + c) * 2)
                                         : rd16le(row + (x * channels + c) * 2);
                    dst[x * channels + c] = uchar(v >> 8);
                }
            }
        }
        return img;
    }
    setFail(QStringLiteral("dpx unsupported packing"), LoadErrorCategory::Unsupported, errOut, catOut);
    return QImage();
}

// ===========================================================================
// 14. APNG (.apng 及动态 .png)
// ===========================================================================
bool hasApngAnimationFast(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(65536);
    if (head.size() < 24 || std::memcmp(head.constData(), "\x89PNG\r\n\x1a\n", 8) != 0) {
        return false;
    }
    const uchar *p = reinterpret_cast<const uchar *>(head.constData());
    int pos = 8;
    while (pos + 12 <= head.size()) {
        const quint32 len = rd32be(p + pos);
        const char *type = reinterpret_cast<const char *>(p + pos + 4);
        if (std::memcmp(type, "acTL", 4) == 0 && len >= 8 && pos + 16 <= head.size()) {
            const quint32 numFrames = rd32be(p + pos + 8);
            return numFrames > 1;
        }
        if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) {
            return false;
        }
        if (len > quint32(head.size())) break;
        pos += 12 + int(len);
    }
    return false;
}

void appendPngChunk(QByteArray &out, const char type[4], const char *payload, quint32 len)
{
    uchar hdr[8];
    qToBigEndian(len, hdr);
    std::memcpy(hdr + 4, type, 4);
    out.append(reinterpret_cast<const char *>(hdr), 8);
    if (len > 0 && payload) {
        out.append(payload, int(len));
    }
    mz_ulong crc = mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const uchar *>(type), 4);
    if (len > 0 && payload) {
        crc = mz_crc32(crc, reinterpret_cast<const uchar *>(payload), len);
    }
    uchar crcBytes[4];
    qToBigEndian(quint32(crc), crcBytes);
    out.append(reinterpret_cast<const char *>(crcBytes), 4);
}

bool decodeApngFrames(const QByteArray &data, QVector<QImage> &frames, QVector<int> &delaysMs,
                      QString *errOut, LoadErrorCategory *catOut)
{
    frames.clear();
    delaysMs.clear();
    if (data.size() < 33 || std::memcmp(data.constData(), "\x89PNG\r\n\x1a\n", 8) != 0) {
        setFail(QStringLiteral("apng invalid signature"), LoadErrorCategory::Unsupported, errOut, catOut);
        return false;
    }

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    const size_t n = size_t(data.size());
    QByteArray ihdr;
    QVector<QByteArray> sharedChunks;   // PLTE, tRNS, gAMA, cHRM, sRGB, iCCP

    struct FrameSpec {
        quint32 w = 0, h = 0, x = 0, y = 0;
        quint16 delayNum = 0, delayDen = 0;
        uchar disposeOp = 0, blendOp = 0;
        QByteArray idatPayload;
    };
    QVector<FrameSpec> specs;
    QByteArray defaultIdat;
    bool seenAcTl = false;
    bool seenIdat = false;
    bool firstFrameUsesIdat = false;

    size_t pos = 8;
    while (pos + 12 <= n) {
        const quint32 len = rd32be(p + pos);
        if (pos + 12 + len > n) break;
        const char *type = reinterpret_cast<const char *>(p + pos + 4);
        const char *payload = reinterpret_cast<const char *>(p + pos + 8);
        if (std::memcmp(type, "IHDR", 4) == 0 && len == 13) {
            ihdr = QByteArray(payload, 13);
        } else if (std::memcmp(type, "acTL", 4) == 0 && len >= 8) {
            seenAcTl = true;
        } else if (std::memcmp(type, "fcTL", 4) == 0 && len >= 26) {
            if (specs.size() >= kMaxAnimFrames) break;
            FrameSpec fs;
            const uchar *fp = reinterpret_cast<const uchar *>(payload);
            fs.w = rd32be(fp + 4);
            fs.h = rd32be(fp + 8);
            fs.x = rd32be(fp + 12);
            fs.y = rd32be(fp + 16);
            fs.delayNum = rd16be(fp + 20);
            fs.delayDen = rd16be(fp + 22);
            fs.disposeOp = fp[24];
            fs.blendOp = fp[25];
            specs.append(fs);
            if (specs.size() == 1 && !seenIdat) {
                firstFrameUsesIdat = true;
            }
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            seenIdat = true;
            if (firstFrameUsesIdat && specs.size() == 1) {
                specs[0].idatPayload.append(payload, int(len));
            } else {
                defaultIdat.append(payload, int(len));
            }
        } else if (std::memcmp(type, "fdAT", 4) == 0 && len > 4) {
            if (!specs.isEmpty()) {
                specs.last().idatPayload.append(payload + 4, int(len - 4));
            }
        } else if (std::memcmp(type, "PLTE", 4) == 0 || std::memcmp(type, "tRNS", 4) == 0
                   || std::memcmp(type, "gAMA", 4) == 0 || std::memcmp(type, "cHRM", 4) == 0
                   || std::memcmp(type, "sRGB", 4) == 0 || std::memcmp(type, "iCCP", 4) == 0) {
            sharedChunks.append(QByteArray(reinterpret_cast<const char *>(p + pos + 4), int(len + 4)));   // type(4) + payload
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        pos += 12 + len;
    }

    if (ihdr.size() != 13) {
        setFail(QStringLiteral("apng missing ihdr"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    const quint32 canvasW = rd32be(reinterpret_cast<const uchar *>(ihdr.constData()));
    const quint32 canvasH = rd32be(reinterpret_cast<const uchar *>(ihdr.constData() + 4));
    if (!validRasterSize(int(canvasW), int(canvasH))) {
        setFail(QStringLiteral("apng invalid dimensions"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }

    // 非动图静态 .apng 或只有 1 帧时交单帧 PNG
    if (!seenAcTl || specs.isEmpty()) {
        QImage single = QImage::fromData(data, "PNG");
        if (single.isNull()) {
            setFail(QStringLiteral("apng static decode failed"), LoadErrorCategory::Corrupt, errOut, catOut);
            return false;
        }
        frames.append(single);
        delaysMs.append(100);
        return true;
    }

    QImage canvas(int(canvasW), int(canvasH), QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QImage prevCanvas = canvas;
    quint64 totalBytes = 0;

    for (int i = 0; i < specs.size(); ++i) {
        const FrameSpec &fs = specs.at(i);
        if (fs.w == 0 || fs.h == 0 || fs.x + fs.w > canvasW || fs.y + fs.h > canvasH) {
            continue;
        }
        QByteArray framePng;
        framePng.append("\x89PNG\r\n\x1a\n", 8);
        QByteArray fIhdr = ihdr;
        qToBigEndian(fs.w, reinterpret_cast<uchar *>(fIhdr.data()));
        qToBigEndian(fs.h, reinterpret_cast<uchar *>(fIhdr.data() + 4));
        appendPngChunk(framePng, "IHDR", fIhdr.constData(), 13);
        for (const QByteArray &sc : sharedChunks) {
            appendPngChunk(framePng, sc.constData(), sc.constData() + 4, quint32(sc.size() - 4));
        }
        appendPngChunk(framePng, "IDAT", fs.idatPayload.constData(), quint32(fs.idatPayload.size()));
        appendPngChunk(framePng, "IEND", nullptr, 0);

        QImage patch = QImage::fromData(framePng, "PNG");
        if (patch.isNull()) {
            continue;
        }
        patch = patch.convertToFormat(QImage::Format_ARGB32);
        if (fs.disposeOp == 2) {
            prevCanvas = canvas.copy();
        }

        {
            QPainter painter(&canvas);
            if (fs.blendOp == 0) {
                painter.setCompositionMode(QPainter::CompositionMode_Source);
            } else {
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            }
            painter.drawImage(int(fs.x), int(fs.y), patch);
        }

        totalBytes += quint64(canvas.sizeInBytes());
        if (totalBytes > kMaxAnimTotalBytes) {
            break;
        }
        frames.append(canvas.copy());
        const int den = (fs.delayDen == 0) ? 100 : int(fs.delayDen);
        const int ms = int(qRound(double(fs.delayNum) * 1000.0 / double(den)));
        delaysMs.append(ms <= 0 ? 100 : ms);

        if (fs.disposeOp == 1) {
            QPainter clr(&canvas);
            clr.setCompositionMode(QPainter::CompositionMode_Source);
            clr.fillRect(int(fs.x), int(fs.y), int(fs.w), int(fs.h), Qt::transparent);
        } else if (fs.disposeOp == 2) {
            canvas = prevCanvas;
        }
    }

    if (frames.isEmpty()) {
        setFail(QStringLiteral("apng zero decoded frames"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    return true;
}

// ===========================================================================
// 15. Animated WebP (.webp)
// ===========================================================================
bool hasWebpAnimationFast(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(64);
    if (head.size() < 24 || !head.startsWith("RIFF") || head.mid(8, 4) != "WEBP") {
        return false;
    }
    if (head.mid(12, 4) == "VP8X" && head.size() >= 21) {
        return (static_cast<uchar>(head.at(20)) & 0x02) != 0;
    }
    return false;
}

bool decodeWebpAnimFrames(const QByteArray &data, QVector<QImage> &frames, QVector<int> &delaysMs,
                          QString *errOut, LoadErrorCategory *catOut)
{
    frames.clear();
    delaysMs.clear();
#ifdef HAS_LIBWEBPDEMUX
    WebPData webpData{ reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()) };
    WebPAnimDecoderOptions opts;
    if (!WebPAnimDecoderOptionsInit(&opts)) {
        setFail(QStringLiteral("webp anim options init failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    opts.color_mode = MODE_BGRA;
    opts.use_threads = 1;
    WebPAnimDecoder *dec = WebPAnimDecoderNew(&webpData, &opts);
    if (!dec) {
        setFail(QStringLiteral("webp anim decoder failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }

    WebPAnimInfo info{};
    if (!WebPAnimDecoderGetInfo(dec, &info)
        || !validRasterSize(int(info.canvas_width), int(info.canvas_height))) {
        WebPAnimDecoderDelete(dec);
        setFail(QStringLiteral("webp anim invalid info"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }

    int prevTs = 0;
    quint64 totalBytes = 0;
    while (WebPAnimDecoderHasMoreFrames(dec) && frames.size() < kMaxAnimFrames) {
        uint8_t *buf = nullptr;
        int ts = 0;
        if (!WebPAnimDecoderGetNext(dec, &buf, &ts) || !buf) {
            break;
        }
        QImage frame(buf, int(info.canvas_width), int(info.canvas_height),
                     int(info.canvas_width) * 4, QImage::Format_ARGB32);
        QImage copy = frame.copy();
        totalBytes += quint64(copy.sizeInBytes());
        if (totalBytes > kMaxAnimTotalBytes) {
            break;
        }
        frames.append(std::move(copy));
        const int dur = ts - prevTs;
        prevTs = ts;
        delaysMs.append(dur > 0 ? dur : 100);
    }
    WebPAnimDecoderDelete(dec);
    if (frames.isEmpty()) {
        setFail(QStringLiteral("webp anim no frames"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    return true;
#else
    Q_UNUSED(data);
    setFail(QStringLiteral("libwebpdemux not linked"), LoadErrorCategory::Unsupported, errOut, catOut);
    return false;
#endif
}

// ===========================================================================
// 16. T3 格式占位 / 实现（JP2, KRA/ORA, JXL, EXR）
// ===========================================================================
QImage decodeKritaOrOra(const QString &path, QString *errOut, LoadErrorCategory *catOut)
{
    ZipArchive zip;
    QString openErr;
    if (!zip.open(path, &openErr)) {
        setFail(openErr.isEmpty() ? QStringLiteral("container open failed") : openErr,
                LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const QVector<ZipArchive::Entry> &entries = zip.entries();
    const ZipArchive::Entry *picked = nullptr;
    const QStringList candidates = {
        QStringLiteral("mergedimage.png"),
        QStringLiteral("preview.png"),
        QStringLiteral("thumbnails/thumbnail.png")
    };
    for (const QString &cand : candidates) {
        for (const ZipArchive::Entry &e : entries) {
            if (e.name.compare(cand, Qt::CaseInsensitive) == 0) {
                picked = &e;
                break;
            }
        }
        if (picked) break;
    }
    if (!picked) {
        setFail(QStringLiteral("container missing mergedimage.png"), LoadErrorCategory::NoCompatPreview,
                errOut, catOut);
        return QImage();
    }
    QString readErr;
    const QByteArray pngBytes = zip.read(*picked, &readErr);
    if (pngBytes.isEmpty()) {
        setFail(readErr.isEmpty() ? QStringLiteral("container preview read failed") : readErr,
                LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    QImage img = QImage::fromData(pngBytes, "PNG");
    if (img.isNull()) {
        setFail(QStringLiteral("container preview decode failed"), LoadErrorCategory::Corrupt,
                errOut, catOut);
    }
    return img;
}

#ifdef HAS_LIBOPENJP2
struct OpjMemStream {
    const uchar *data = nullptr;
    size_t size = 0;
    size_t pos = 0;
};

OPJ_SIZE_T opjMemRead(void *pBuffer, OPJ_SIZE_T pNbBytes, void *pUserData)
{
    auto *s = static_cast<OpjMemStream *>(pUserData);
    if (!s || s->pos >= s->size) return OPJ_SIZE_T(-1);
    const size_t avail = s->size - s->pos;
    const size_t take = qMin(avail, size_t(pNbBytes));
    std::memcpy(pBuffer, s->data + s->pos, take);
    s->pos += take;
    return OPJ_SIZE_T(take);
}

OPJ_OFF_T opjMemSkip(OPJ_OFF_T pNbBytes, void *pUserData)
{
    auto *s = static_cast<OpjMemStream *>(pUserData);
    if (!s) return -1;
    if (pNbBytes < 0) {
        const size_t back = size_t(-pNbBytes);
        s->pos = (back > s->pos) ? 0 : (s->pos - back);
        return pNbBytes;
    }
    const size_t adv = qMin(s->size - s->pos, size_t(pNbBytes));
    s->pos += adv;
    return OPJ_OFF_T(adv);
}

OPJ_BOOL opjMemSeek(OPJ_OFF_T pNbBytes, void *pUserData)
{
    auto *s = static_cast<OpjMemStream *>(pUserData);
    if (!s || pNbBytes < 0 || size_t(pNbBytes) > s->size) return OPJ_FALSE;
    s->pos = size_t(pNbBytes);
    return OPJ_TRUE;
}

QImage decodeJp2(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    if (data.size() < 4) {
        setFail(QStringLiteral("jp2 file too small"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    OPJ_CODEC_FORMAT fmt = OPJ_CODEC_JP2;
    if (data.size() >= 12 && std::memcmp(p, "\x00\x00\x00\x0C\x6A\x50\x20\x20", 8) == 0) {
        fmt = OPJ_CODEC_JP2;
    } else if (p[0] == 0xFF && p[1] == 0x4F && p[2] == 0xFF && p[3] == 0x51) {
        fmt = OPJ_CODEC_J2K;
    } else {
        setFail(QStringLiteral("jp2 unrecognized signature"), LoadErrorCategory::Unsupported, errOut, catOut);
        return QImage();
    }

    OpjMemStream mem{ p, size_t(data.size()), 0 };
    opj_stream_t *stream = opj_stream_default_create(OPJ_TRUE);
    if (!stream) {
        setFail(QStringLiteral("opj stream alloc failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
    opj_stream_set_user_data(stream, &mem, nullptr);
    opj_stream_set_user_data_length(stream, OPJ_UINT64(data.size()));
    opj_stream_set_read_function(stream, opjMemRead);
    opj_stream_set_skip_function(stream, opjMemSkip);
    opj_stream_set_seek_function(stream, opjMemSeek);

    opj_codec_t *codec = opj_create_decompress(fmt);
    opj_dparameters_t params;
    opj_set_default_decoder_parameters(&params);
    if (!codec || !opj_setup_decoder(codec, &params)) {
        if (codec) opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        setFail(QStringLiteral("opj setup failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    opj_image_t *image = nullptr;
    if (!opj_read_header(stream, codec, &image) || !image) {
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        setFail(QStringLiteral("opj read header failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const int w = int(image->x1 - image->x0);
    const int h = int(image->y1 - image->y0);
    if (!validRasterSize(w, h) || image->numcomps < 1) {
        opj_image_destroy(image);
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        setFail(QStringLiteral("opj invalid dimensions"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    if (!opj_decode(codec, stream, image) || !opj_end_decompress(codec, stream)) {
        opj_image_destroy(image);
        opj_destroy_codec(codec);
        opj_stream_destroy(stream);
        setFail(QStringLiteral("opj decode failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }

    const int comps = int(image->numcomps);
    for (int c = 0; c < comps; ++c) {
        if (int(image->comps[c].w) != w || int(image->comps[c].h) != h || !image->comps[c].data) {
            opj_image_destroy(image);
            opj_destroy_codec(codec);
            opj_stream_destroy(stream);
            setFail(QStringLiteral("opj subsampled component unsupported"), LoadErrorCategory::Unsupported,
                    errOut, catOut);
            return QImage();
        }
    }

    auto normComp = [&](int c, size_t idx) -> uchar {
        const opj_image_comp_t &comp = image->comps[c];
        int v = comp.data[idx];
        if (comp.sgnd) {
            v += (1 << (comp.prec - 1));
        }
        if (comp.prec > 8) {
            v >>= (comp.prec - 8);
        } else if (comp.prec < 8) {
            v <<= (8 - comp.prec);
        }
        return uchar(std::clamp(v, 0, 255));
    };

    const bool sycc = (image->color_space == OPJ_CLRSPC_SYCC && comps >= 3);
    QImage out(w, h, comps == 1 ? QImage::Format_Grayscale8
                     : comps >= 4 ? QImage::Format_RGBA8888
                                  : QImage::Format_RGB888);
    if (!out.isNull()) {
        for (int y = 0; y < h; ++y) {
            uchar *dst = out.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const size_t idx = size_t(y) * w + x;
                if (comps == 1) {
                    dst[x] = normComp(0, idx);
                } else if (sycc) {
                    const float Y = normComp(0, idx);
                    const float Cb = normComp(1, idx) - 128.0f;
                    const float Cr = normComp(2, idx) - 128.0f;
                    const int r = int(std::lround(Y + 1.402f * Cr));
                    const int g = int(std::lround(Y - 0.344136f * Cb - 0.714136f * Cr));
                    const int b = int(std::lround(Y + 1.772f * Cb));
                    const int step = comps >= 4 ? 4 : 3;
                    dst[x * step + 0] = uchar(std::clamp(r, 0, 255));
                    dst[x * step + 1] = uchar(std::clamp(g, 0, 255));
                    dst[x * step + 2] = uchar(std::clamp(b, 0, 255));
                    if (comps >= 4) dst[x * 4 + 3] = normComp(3, idx);
                } else {
                    const int step = comps >= 4 ? 4 : 3;
                    dst[x * step + 0] = normComp(0, idx);
                    dst[x * step + 1] = normComp(1, idx);
                    dst[x * step + 2] = normComp(2, idx);
                    if (comps >= 4) dst[x * 4 + 3] = normComp(3, idx);
                }
            }
        }
    }

    opj_image_destroy(image);
    opj_destroy_codec(codec);
    opj_stream_destroy(stream);
    return out;
}
#endif

#ifdef HAS_LIBJXL
bool hasJxlAnimationFast(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray head = f.read(65536);
    if (head.isEmpty()) return false;
    JxlDecoderPtr dec = JxlDecoderMake(nullptr);
    if (!dec) return false;
    if (JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO) != JXL_DEC_SUCCESS) return false;
    JxlDecoderSetInput(dec.get(), reinterpret_cast<const uint8_t *>(head.constData()), size_t(head.size()));
    JxlDecoderCloseInput(dec.get());
    if (JxlDecoderProcessInput(dec.get()) == JXL_DEC_BASIC_INFO) {
        JxlBasicInfo info{};
        if (JxlDecoderGetBasicInfo(dec.get(), &info) == JXL_DEC_SUCCESS) {
            return info.have_animation != 0;
        }
    }
    return false;
}

bool decodeJxlFrames(const QByteArray &data, bool onlyFirst, QVector<QImage> &frames,
                     QVector<int> &delaysMs, QString *errOut, LoadErrorCategory *catOut)
{
    frames.clear();
    delaysMs.clear();
    JxlThreadParallelRunnerPtr runner = JxlThreadParallelRunnerMake(nullptr, 2);
    JxlDecoderPtr dec = JxlDecoderMake(nullptr);
    if (!runner || !dec) {
        setFail(QStringLiteral("jxl init failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    if (JxlDecoderSetParallelRunner(dec.get(), JxlThreadParallelRunner, runner.get()) != JXL_DEC_SUCCESS
        || JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE) != JXL_DEC_SUCCESS) {
        setFail(QStringLiteral("jxl subscribe failed"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }

    JxlDecoderSetInput(dec.get(), reinterpret_cast<const uint8_t *>(data.constData()), size_t(data.size()));
    JxlDecoderCloseInput(dec.get());

    JxlBasicInfo info{};
    JxlPixelFormat fmt{ 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    QByteArray buf;
    int curDelayMs = 100;
    quint64 totalBytes = 0;

    for (;;) {
        const JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());
        if (status == JXL_DEC_ERROR) {
            setFail(QStringLiteral("jxl decode error"), LoadErrorCategory::Corrupt, errOut, catOut);
            return false;
        }
        if (status == JXL_DEC_NEED_MORE_INPUT) {
            setFail(QStringLiteral("jxl truncated"), LoadErrorCategory::Corrupt, errOut, catOut);
            return false;
        }
        if (status == JXL_DEC_BASIC_INFO) {
            if (JxlDecoderGetBasicInfo(dec.get(), &info) != JXL_DEC_SUCCESS
                || !validRasterSize(int(info.xsize), int(info.ysize))) {
                setFail(QStringLiteral("jxl invalid basic info"), LoadErrorCategory::Corrupt, errOut, catOut);
                return false;
            }
        } else if (status == JXL_DEC_FRAME) {
            JxlFrameHeader fh{};
            if (info.have_animation && JxlDecoderGetFrameHeader(dec.get(), &fh) == JXL_DEC_SUCCESS
                && info.animation.tps_numerator > 0) {
                const double sec = double(fh.duration) * double(info.animation.tps_denominator)
                                   / double(info.animation.tps_numerator);
                curDelayMs = qMax(16, int(std::lround(sec * 1000.0)));
            } else {
                curDelayMs = 100;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            size_t need = 0;
            if (JxlDecoderImageOutBufferSize(dec.get(), &fmt, &need) != JXL_DEC_SUCCESS || need == 0) {
                setFail(QStringLiteral("jxl buffer size failed"), LoadErrorCategory::Corrupt, errOut, catOut);
                return false;
            }
            buf.resize(int(need));
            if (JxlDecoderSetImageOutBuffer(dec.get(), &fmt, buf.data(), need) != JXL_DEC_SUCCESS) {
                setFail(QStringLiteral("jxl set buffer failed"), LoadErrorCategory::Corrupt, errOut, catOut);
                return false;
            }
        } else if (status == JXL_DEC_FULL_IMAGE) {
            QImage frame(reinterpret_cast<const uchar *>(buf.constData()),
                         int(info.xsize), int(info.ysize),
                         int(info.xsize) * 4, QImage::Format_RGBA8888);
            QImage copy = frame.copy();
            totalBytes += quint64(copy.sizeInBytes());
            if (totalBytes > kMaxAnimTotalBytes) break;
            frames.append(std::move(copy));
            delaysMs.append(curDelayMs);
            if (onlyFirst || frames.size() >= kMaxAnimFrames) {
                break;
            }
        } else if (status == JXL_DEC_SUCCESS) {
            break;
        }
    }

    if (frames.isEmpty()) {
        setFail(QStringLiteral("jxl no image decoded"), LoadErrorCategory::Corrupt, errOut, catOut);
        return false;
    }
    return true;
}
#endif

#ifdef HAS_OPENEXR
class MemoryExrIStream : public Imf::IStream
{
public:
    explicit MemoryExrIStream(const QByteArray &buf)
        : Imf::IStream("memory.exr"), m_buf(buf) {}
    bool isMemoryMapped() const override { return true; }
    char *readMemoryMapped(int n) override
    {
        if (m_pos + quint64(n) > quint64(m_buf.size())) {
            throw IEX_NAMESPACE::InputExc("Unexpected EOF in EXR memory stream");
        }
        char *ptr = const_cast<char *>(m_buf.constData() + m_pos);
        m_pos += quint64(n);
        return ptr;
    }
    bool read(char c[], int n) override
    {
        if (m_pos + quint64(n) > quint64(m_buf.size())) {
            throw IEX_NAMESPACE::InputExc("Unexpected EOF in EXR memory stream");
        }
        std::memcpy(c, m_buf.constData() + m_pos, size_t(n));
        m_pos += quint64(n);
        return m_pos < quint64(m_buf.size());
    }
    uint64_t tellg() override { return m_pos; }
    void seekg(uint64_t pos) override
    {
        if (pos > quint64(m_buf.size())) {
            throw IEX_NAMESPACE::InputExc("Seek beyond EOF in EXR memory stream");
        }
        m_pos = pos;
    }

private:
    const QByteArray &m_buf;
    uint64_t m_pos = 0;
};

QImage decodeExr(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut)
{
    try {
        MemoryExrIStream stream(data);
        Imf::RgbaInputFile file(stream);
        if (!file.isComplete()) {
            setFail(QStringLiteral("exr incomplete file"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        const Imath::Box2i dw = file.dataWindow();
        const int w = dw.max.x - dw.min.x + 1;
        const int h = dw.max.y - dw.min.y + 1;
        if (!validRasterSize(w, h)) {
            setFail(QStringLiteral("exr invalid dataWindow"), LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        Imf::Array2D<Imf::Rgba> pixels(h, w);
        file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * ptrdiff_t(w), 1, w);
        file.readPixels(dw.min.y, dw.max.y);

        QImage img(w, h, QImage::Format_RGBA8888);
        if (img.isNull()) return QImage();
        for (int y = 0; y < h; ++y) {
            uchar *dst = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const Imf::Rgba &px = pixels[y][x];
                dst[x * 4 + 0] = linearToSrgbByte(float(px.r));
                dst[x * 4 + 1] = linearToSrgbByte(float(px.g));
                dst[x * 4 + 2] = linearToSrgbByte(float(px.b));
                const float a = float(px.a);
                dst[x * 4 + 3] = std::isfinite(a)
                    ? uchar(std::clamp(int(std::lround(a * 255.0f)), 0, 255))
                    : 255;
            }
        }
        return img;
    } catch (const std::exception &ex) {
        setFail(QString::fromUtf8(ex.what()), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    } catch (...) {
        setFail(QStringLiteral("exr unknown decode error"), LoadErrorCategory::Corrupt, errOut, catOut);
        return QImage();
    }
}
#endif

} // namespace

namespace ExtraImageLoader {

bool isExtraPath(const QString &path)
{
    static const QSet<QString> kExts = {
        QStringLiteral("qoi"), QStringLiteral("hdr"), QStringLiteral("pic"),
        QStringLiteral("dds"),
        QStringLiteral("jxr"), QStringLiteral("wdp"), QStringLiteral("hdp"),
        QStringLiteral("wmf"), QStringLiteral("emf"),
        QStringLiteral("ani"), QStringLiteral("pcx"),
        QStringLiteral("sgi"), QStringLiteral("rgb"), QStringLiteral("rgba"),
        QStringLiteral("bw"),  QStringLiteral("int"), QStringLiteral("inta"),
        QStringLiteral("pam"), QStringLiteral("pfm"),
        QStringLiteral("ras"), QStringLiteral("sun"),
        QStringLiteral("ff"),  QStringLiteral("xwd"), QStringLiteral("dpx"),
        QStringLiteral("apng"),
        QStringLiteral("jp2"), QStringLiteral("j2k"), QStringLiteral("j2c"),
        QStringLiteral("jpf"), QStringLiteral("jpx"),
        QStringLiteral("jxl"), QStringLiteral("exr"),
        QStringLiteral("kra"), QStringLiteral("ora")
    };
    return kExts.contains(extLower(path));
}

bool isAnimatedCandidatePath(const QString &path)
{
    static const QSet<QString> kAnim = {
        QStringLiteral("webp"), QStringLiteral("png"), QStringLiteral("apng"),
        QStringLiteral("ani"),
        QStringLiteral("avif"), QStringLiteral("avifs"), QStringLiteral("heics"),
        QStringLiteral("jxl")
    };
    return kAnim.contains(extLower(path));
}

bool isVectorExtraPath(const QString &path)
{
    const QString s = extLower(path);
    return s == QLatin1String("wmf") || s == QLatin1String("emf");
}

bool isArchiveExcludedExt(const QString &extLowerStr)
{
    static const QSet<QString> kFileOnly = {
        QStringLiteral("jxr"), QStringLiteral("wdp"), QStringLiteral("hdp"),
        QStringLiteral("wmf"), QStringLiteral("emf"),
        QStringLiteral("kra"), QStringLiteral("ora")
    };
    return isExtraPath(QStringLiteral("a.") + extLowerStr) || kFileOnly.contains(extLowerStr);
}

bool hasMultipleFrames(const QString &path)
{
    const QString s = extLower(path);
    if (s == QLatin1String("webp")) {
        return hasWebpAnimationFast(path);
    }
    if (s == QLatin1String("png") || s == QLatin1String("apng")) {
        return hasApngAnimationFast(path);
    }
    if (s == QLatin1String("ani")) {
        QByteArray data;
        if (!readFileBytes(path, kMaxFileBytes, data, nullptr, nullptr)) return false;
        QVector<QImage> f;
        QVector<int> d;
        return decodeAniFrames(data, f, d, nullptr, nullptr) && f.size() > 1;
    }
    if (s == QLatin1String("avif") || s == QLatin1String("avifs") || s == QLatin1String("heics")) {
        return hasHeifAnimation(path);
    }
#ifdef HAS_LIBJXL
    if (s == QLatin1String("jxl")) {
        return hasJxlAnimationFast(path);
    }
#endif
    return false;
}

QImage load(const QString &path, const QSize &viewportHint, QString *errorOut,
            LoadErrorCategory *categoryOut, bool toneMapSdr)
{
    Q_UNUSED(toneMapSdr);
    if (errorOut) errorOut->clear();
    if (categoryOut) *categoryOut = LoadErrorCategory::None;

    const QString s = extLower(path);

#ifdef Q_OS_WIN
    if (s == QLatin1String("jxr") || s == QLatin1String("wdp") || s == QLatin1String("hdp")) {
        return loadViaWic(path, errorOut, categoryOut);
    }
    if (s == QLatin1String("wmf") || s == QLatin1String("emf")) {
        return loadMetafileGdiplus(path, viewportHint, true, errorOut, categoryOut);
    }
#else
    Q_UNUSED(viewportHint);
#endif

    if (s == QLatin1String("kra") || s == QLatin1String("ora")) {
        return decodeKritaOrOra(path, errorOut, categoryOut);
    }

    QByteArray data;
    if (!readFileBytes(path, kMaxFileBytes, data, errorOut, categoryOut)) {
        return QImage();
    }

    if (s == QLatin1String("qoi")) return decodeQoi(data, errorOut, categoryOut);
    if (s == QLatin1String("hdr") || s == QLatin1String("pic")) return decodeRadianceHdr(data, errorOut, categoryOut);
    if (s == QLatin1String("dds")) return decodeDds(path, data, errorOut, categoryOut);
    if (s == QLatin1String("pcx")) return decodePcx(data, errorOut, categoryOut);
    if (s == QLatin1String("sgi") || s == QLatin1String("rgb") || s == QLatin1String("rgba")
        || s == QLatin1String("bw") || s == QLatin1String("int") || s == QLatin1String("inta")) {
        return decodeSgi(data, errorOut, categoryOut);
    }
    if (s == QLatin1String("pam")) return decodePam(data, errorOut, categoryOut);
    if (s == QLatin1String("pfm")) return decodePfm(data, errorOut, categoryOut);
    if (s == QLatin1String("ras") || s == QLatin1String("sun")) return decodeSunRaster(data, errorOut, categoryOut);
    if (s == QLatin1String("ff")) return decodeFarbfeld(data, errorOut, categoryOut);
    if (s == QLatin1String("xwd")) return decodeXwd(data, errorOut, categoryOut);
    if (s == QLatin1String("dpx")) return decodeDpx(data, errorOut, categoryOut);

    if (s == QLatin1String("ani")) {
        QVector<QImage> frames;
        QVector<int> delays;
        if (decodeAniFrames(data, frames, delays, errorOut, categoryOut) && !frames.isEmpty()) {
            return frames.first();
        }
        return QImage();
    }
    if (s == QLatin1String("apng")) {
        QVector<QImage> frames;
        QVector<int> delays;
        if (decodeApngFrames(data, frames, delays, errorOut, categoryOut) && !frames.isEmpty()) {
            return frames.first();
        }
        return QImage();
    }

#ifdef HAS_LIBOPENJP2
    if (s == QLatin1String("jp2") || s == QLatin1String("j2k") || s == QLatin1String("j2c")
        || s == QLatin1String("jpf") || s == QLatin1String("jpx")) {
        return decodeJp2(data, errorOut, categoryOut);
    }
#endif

#ifdef HAS_LIBJXL
    if (s == QLatin1String("jxl")) {
        QVector<QImage> frames;
        QVector<int> delays;
        if (decodeJxlFrames(data, true, frames, delays, errorOut, categoryOut) && !frames.isEmpty()) {
            return frames.first();
        }
        return QImage();
    }
#endif

#ifdef HAS_OPENEXR
    if (s == QLatin1String("exr")) {
        return decodeExr(data, errorOut, categoryOut);
    }
#endif

    setFail(QStringLiteral("unsupported extra format"), LoadErrorCategory::Unsupported, errorOut, categoryOut);
    return QImage();
}

bool loadAnimationFrames(const QString &path, QVector<QImage> &frames,
                         QVector<int> &delaysMs, QString *errorOut)
{
    frames.clear();
    delaysMs.clear();
    const QString s = extLower(path);
    LoadErrorCategory cat = LoadErrorCategory::None;

    if (s == QLatin1String("avif") || s == QLatin1String("avifs") || s == QLatin1String("heics")) {
        return loadHeifAnimationFrames(path, frames, delaysMs, errorOut);
    }

    QByteArray data;
    if (!readFileBytes(path, kMaxFileBytes, data, errorOut, &cat)) {
        return false;
    }

    if (s == QLatin1String("webp")) {
        return decodeWebpAnimFrames(data, frames, delaysMs, errorOut, &cat);
    }
    if (s == QLatin1String("png") || s == QLatin1String("apng")) {
        return decodeApngFrames(data, frames, delaysMs, errorOut, &cat);
    }
    if (s == QLatin1String("ani")) {
        return decodeAniFrames(data, frames, delaysMs, errorOut, &cat);
    }
#ifdef HAS_LIBJXL
    if (s == QLatin1String("jxl")) {
        return decodeJxlFrames(data, false, frames, delaysMs, errorOut, &cat);
    }
#endif
    return false;
}

} // namespace ExtraImageLoader
