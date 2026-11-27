// T1 / T2 / T3 全格式解码、动图帧导航、RAW 注册表与坏文件安全单测。
#include "DirectoryLoader.h"
#include "ExtraImageLoader.h"
#include "FrameNavigator.h"
#include "RawImageLoader.h"
#include "RawJpgGroup.h"
#include "RotateAutoSave.h"
#include "SvgLoader.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <wincodec.h>
#include <gdiplus.h>
#endif

#include <cstdio>
#include <cstring>

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

#ifdef Q_OS_WIN
static bool createTestJxr(const QString &outPath)
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = (hr == S_OK || hr == S_FALSE);

    IWICImagingFactory *factory = nullptr;
    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        if (needUninit) CoUninitialize();
        return false;
    }

    IWICStream *stream = nullptr;
    IWICBitmapEncoder *encoder = nullptr;
    IWICBitmapFrameEncode *frame = nullptr;
    bool ok = false;

    const std::wstring wpath = outPath.toStdWString();
    if (SUCCEEDED(factory->CreateStream(&stream)) && stream
        && SUCCEEDED(stream->InitializeFromFilename(wpath.c_str(), GENERIC_WRITE))
        && SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, &encoder)) && encoder
        && SUCCEEDED(encoder->Initialize(stream, WICBitmapEncoderNoCache))
        && SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && frame) {
        const UINT w = 32, h = 24;
        frame->Initialize(nullptr);
        frame->SetSize(w, h);
        WICPixelFormatGUID pf = GUID_WICPixelFormat24bppBGR;
        frame->SetPixelFormat(&pf);
        QByteArray pixels(int(w * h * 3), '\0');
        for (UINT y = 0; y < h; ++y) {
            for (UINT x = 0; x < w; ++x) {
                const int off = int((y * w + x) * 3);
                pixels[off + 0] = char(200);
                pixels[off + 1] = char(120);
                pixels[off + 2] = char(40);
            }
        }
        if (SUCCEEDED(frame->WritePixels(h, w * 3, UINT(pixels.size()),
                                         reinterpret_cast<BYTE *>(pixels.data())))
            && SUCCEEDED(frame->Commit())
            && SUCCEEDED(encoder->Commit())) {
            ok = true;
        }
    }

    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    factory->Release();
    if (needUninit) CoUninitialize();
    return ok;
}

static bool createTestEmf(const QString &outPath)
{
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) {
        return false;
    }
    bool ok = false;
    {
        HDC hdc = GetDC(nullptr);
        const std::wstring wpath = outPath.toStdWString();
        Gdiplus::RectF bounds(0.0f, 0.0f, 120.0f, 90.0f);
        Gdiplus::Metafile meta(wpath.c_str(), hdc, bounds, Gdiplus::MetafileFrameUnitPixel,
                               Gdiplus::EmfTypeEmfPlusDual);
        if (meta.GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(&meta);
            Gdiplus::SolidBrush blue(Gdiplus::Color(255, 30, 110, 220));
            Gdiplus::SolidBrush gold(Gdiplus::Color(255, 240, 190, 40));
            g.FillRectangle(&blue, 4, 4, 112, 82);
            g.FillEllipse(&gold, 30, 20, 60, 50);
            ok = (g.GetLastStatus() == Gdiplus::Ok);
        }
        ReleaseDC(nullptr, hdc);
    }
    return ok && QFileInfo::exists(outPath);
}
#endif

static bool hasNonZeroPixel(const QImage &img)
{
    if (img.isNull() || img.width() <= 0 || img.height() <= 0) return false;
    const QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    const uchar *bits = rgba.constBits();
    const qsizetype total = rgba.sizeInBytes();
    for (qsizetype i = 0; i < total; i += 17) {
        if (bits[i] != 0) return true;
    }
    return false;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    const QString samplesDir = QStringLiteral(SOLEVIEW_FORMAT_SAMPLES_DIR);
    CHECK(QDir(samplesDir).exists());

    // 1. T1: RAW 39 种扩展名统一事实来源 & 白名单一致性
    {
        const QStringList &raws = rawFileExtensions();
        CHECK(raws.size() == 39);
        for (const char *ext : { "dng", "nef", "cr2", "cr3", "crw", "arw", "nrw",
                                 "3fr", "fff", "iiq", "rwl", "x3f", "ari" }) {
            CHECK(raws.contains(QLatin1String(ext)));
            CHECK(isRawFilePath(QStringLiteral("test.") + QLatin1String(ext)));
            CHECK(RawJpgGroup::isRawSideExt(QLatin1String(ext)));
        }
        CHECK(RawJpgGroup::isRawSideExt(QStringLiteral("psb")));
        CHECK(RawJpgGroup::isJpgSideExt(QStringLiteral("jpe")));

        const QStringList allExts = DirectoryLoader::imageExtensions();
        for (const char *ext : { "*.psb", "*.svgz", "*.cr3", "*.jpe", "*.dib", "*.pnm",
                                 "*.qoi", "*.hdr", "*.dds", "*.jxr", "*.wmf", "*.emf",
                                 "*.ani", "*.pcx", "*.sgi", "*.pam", "*.pfm", "*.ras",
                                 "*.ff", "*.xwd", "*.dpx", "*.apng", "*.jp2", "*.j2k",
                                 "*.jxl", "*.exr", "*.kra", "*.ora", "*.avifs", "*.heics" }) {
            CHECK(allExts.contains(QLatin1String(ext)));
        }
    }

    // 2. T1: SVGZ（含中文路径）与截断拦截
    {
        QString err;
        LoadErrorCategory cat = LoadErrorCategory::None;
        const QImage s1 = SvgLoader::render(samplesDir + QStringLiteral("/sample.svgz"),
                                            QSize(256, 256), true, &err, &cat);
        CHECK(!s1.isNull());
        CHECK(s1.width() > 0 && s1.height() > 0);
        CHECK(hasNonZeroPixel(s1));

        const QImage sZh = SvgLoader::render(samplesDir + QStringLiteral("/中文目录/测试矢量.svgz"),
                                             QSize(256, 256), true, &err, &cat);
        CHECK(!sZh.isNull());

        const QImage bad = SvgLoader::render(samplesDir + QStringLiteral("/bad_trunc.svgz"),
                                             QSize(256, 256), true, &err, &cat);
        CHECK(bad.isNull());
        CHECK(cat == LoadErrorCategory::Corrupt);
    }

    // 3. T1: QImageReader 别名（jpe / dib / pnm）
    {
        for (const char *name : { "sample.jpe", "sample.dib", "sample.pnm" }) {
            QImageReader r(samplesDir + QLatin1Char('/') + QLatin1String(name));
            const QImage img = r.read();
            CHECK(!img.isNull());
            CHECK(img.width() == 64 && img.height() == 48);
        }
    }

    // 4. T2 + T3: ExtraImageLoader 全部静态格式
    {
        struct Case { const char *file; int w; int h; };
        const Case cases[] = {
            { "sample.qoi",      64, 48 },
            { "sample.hdr",      64, 48 },
            { "sample_bgra.dds", 16, 16 },
            { "sample_bc1.dds",  16, 16 },
            { "sample_bc3.dds",  16, 16 },
            { "sample.ani",      32, 32 },
            { "sample.pcx",      64, 48 },
            { "sample.sgi",      64, 48 },
            { "sample.pam",      64, 48 },
            { "sample.pfm",      64, 48 },
            { "sample.ras",      64, 48 },
            { "sample.ff",       32, 24 },
            { "sample.xwd",      64, 48 },
            { "sample.dpx",      64, 48 },
            { "anim.apng",       64, 48 },
            { "sample.jp2",      64, 48 },
            { "sample.j2k",      64, 48 },
            { "sample.jxl",      64, 48 },
            { "sample.exr",      64, 48 },
            { "sample.kra",      64, 48 },
            { "sample.ora",      64, 48 },
        };
        for (const Case &c : cases) {
            const QString p = samplesDir + QLatin1Char('/') + QLatin1String(c.file);
            QString err;
            LoadErrorCategory cat = LoadErrorCategory::None;
            const QImage img = ExtraImageLoader::load(p, QSize(), &err, &cat);
            if (img.isNull()) {
                std::fprintf(stderr, "Failed to load %s: %s\n", c.file, err.toUtf8().constData());
            }
            CHECK(!img.isNull());
            CHECK(img.width() == c.w && img.height() == c.h);
            CHECK(hasNonZeroPixel(img));
        }
    }

#ifdef Q_OS_WIN
    // 5. T2: Windows WIC (.jxr) 与 GDI+ (.emf) 原生生成并解码
    {
        const QString jxrPath = samplesDir + QStringLiteral("/sample_gen.jxr");
        CHECK(createTestJxr(jxrPath));
        const QImage jxr = ExtraImageLoader::load(jxrPath);
        CHECK(!jxr.isNull());
        CHECK(jxr.width() == 32 && jxr.height() == 24);
        CHECK(hasNonZeroPixel(jxr));

        const QString emfPath = samplesDir + QStringLiteral("/sample_gen.emf");
        CHECK(createTestEmf(emfPath));
        const QImage emf = ExtraImageLoader::load(emfPath, QSize(240, 180));
        CHECK(!emf.isNull());
        CHECK(emf.width() > 0 && emf.height() > 0);
        CHECK(hasNonZeroPixel(emf));
    }
#endif

    // 6. T2 + T3: FrameNavigator 多帧动图导航 + 单帧负例
    {
        for (const char *animName : { "anim.webp", "anim.apng", "anim_png.png", "sample.ani", "anim.jxl" }) {
            FrameNavigator nav;
            nav.open(samplesDir + QLatin1Char('/') + QLatin1String(animName));
            CHECK(nav.hasFrames());
            CHECK(nav.animatable());
            CHECK(nav.frameCount() >= 2);
            CHECK(nav.lastDelay() > 0);
            const QImage f1 = nav.nextFrame();
            CHECK(!f1.isNull());
            CHECK(nav.frameIndex() == 1);
            const QImage f0 = nav.prevFrame();
            CHECK(!f0.isNull());
            CHECK(nav.frameIndex() == 0);
        }
        for (const char *staticName : { "single.png", "single.webp" }) {
            FrameNavigator nav;
            nav.open(samplesDir + QLatin1Char('/') + QLatin1String(staticName));
            CHECK(!nav.hasFrames());
        }
    }

    // 7. RotateAutoSave 拦截新格式
    {
        CHECK(RotateAutoSave::unsupportedReason(QStringLiteral("a.psb")) == QStringLiteral("PSD"));
        CHECK(RotateAutoSave::unsupportedReason(QStringLiteral("a.cr3")) == QStringLiteral("RAW"));
        CHECK(RotateAutoSave::unsupportedReason(samplesDir + QStringLiteral("/anim.webp")) == QStringLiteral("ANIM"));
        CHECK(RotateAutoSave::unsupportedReason(samplesDir + QStringLiteral("/anim.apng")) == QStringLiteral("ANIM"));
        CHECK(RotateAutoSave::unsupportedReason(samplesDir + QStringLiteral("/anim_png.png")) == QStringLiteral("ANIM"));
    }

    // 8. 异常/截断/假后缀/超大头安全失败
    {
        for (const char *badName : { "bad_trunc.qoi", "bad_trunc.hdr", "bad_trunc.dds",
                                     "bad_trunc.jxl", "bad_trunc.exr", "bad_trunc.jp2",
                                     "bad_fake.exr", "bad_huge.qoi", "bad_huge.ff", "bad_huge.dds" }) {
            QString err;
            LoadErrorCategory cat = LoadErrorCategory::None;
            const QImage img = ExtraImageLoader::load(samplesDir + QLatin1Char('/') + QLatin1String(badName),
                                                      QSize(), &err, &cat);
            if (!img.isNull()) {
                std::fprintf(stderr, "Expected null for %s, got %dx%d\n", badName, img.width(), img.height());
            }
            CHECK(img.isNull());
        }
    }

    if (g_failures == 0) {
        std::printf("formats.pure: PASS\n");
        return 0;
    }
    std::printf("formats.pure: FAIL (%d)\n", g_failures);
    return 1;
}
