// 显示就绪帧逐位等价校验：旧路径复刻（DisplayFrame::legacyPrepare）与新路径
// （DisplayFrame::prepare）对同一输入必须逐字节一致。覆盖：内嵌非 sRGB ICC /
// 高位深(16bit) / 带透明 / 无 profile 四类，两种选项组合（ICC 开关 × tone map 开关）。
#include "DisplayFrame.h"
#include "ExtraImageLoader.h"
#include "ToneMap.h"

#include <QApplication>
#include <QByteArray>
#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QStringList>

#include <cstdio>
#include <cstring>

static int g_failures = 0;
static int g_pairs = 0;
static int g_skipped = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            ++g_failures; \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

namespace {

QImage decodeAny(const QString &path)
{
    if (ExtraImageLoader::isExtraPath(path)) {
        QString err;
        LoadErrorCategory cat = LoadErrorCategory::None;
        const QImage img = ExtraImageLoader::load(path, QSize(), &err, &cat, false);
        if (!img.isNull())
            return img;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    return reader.read();
}

// 逐字节 + 关键属性比对；detail 回填首个不一致原因。
bool imagesEqual(const QImage &a, const QImage &b, QString &detail)
{
    if (a.isNull() || b.isNull()) {
        if (a.isNull() != b.isNull()) {
            detail = QStringLiteral("isNull 不一致: %1 vs %2").arg(a.isNull(), b.isNull());
            return false;
        }
        return true;
    }
    if (a.size() != b.size()) {
        detail = QStringLiteral("尺寸不一致: %1x%2 vs %3x%4")
                     .arg(a.width()).arg(a.height()).arg(b.width()).arg(b.height());
        return false;
    }
    if (a.format() != b.format()) {
        detail = QStringLiteral("格式不一致: %1 vs %2").arg(a.format()).arg(b.format());
        return false;
    }
    const bool aCs = a.colorSpace().isValid();
    const bool bCs = b.colorSpace().isValid();
    if (aCs != bCs) {
        detail = QStringLiteral("colorSpace 有效性不一致: %1 vs %2").arg(aCs).arg(bCs);
        return false;
    }
    const bool aSrgb = aCs && a.colorSpace() == QColorSpace::SRgb;
    const bool bSrgb = bCs && b.colorSpace() == QColorSpace::SRgb;
    if (aSrgb != bSrgb) {
        detail = QStringLiteral("sRGB 标记不一致: %1 vs %2").arg(aSrgb).arg(bSrgb);
        return false;
    }
    if (a.sizeInBytes() != b.sizeInBytes()) {
        detail = QStringLiteral("字节数不一致: %1 vs %2").arg(a.sizeInBytes()).arg(b.sizeInBytes());
        return false;
    }
    if (std::memcmp(a.constBits(), b.constBits(), a.sizeInBytes()) != 0) {
        detail = QStringLiteral("像素字节不一致（%1 字节）").arg(a.sizeInBytes());
        return false;
    }
    return true;
}

QImage makeRgb(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uchar *line = img.scanLine(y);
            const int off = x * 3;
            line[off + 0] = uchar(255 * x / w);
            line[off + 1] = uchar(120);
            line[off + 2] = uchar(255 * y / h);
        }
    return img;
}

QImage makeRgba(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uchar *line = img.scanLine(y);
            const int off = x * 4;
            line[off + 0] = uchar(40);
            line[off + 1] = uchar(120);
            line[off + 2] = uchar(200);
            line[off + 3] = uchar(255 * x / w);   // 半透明渐变
        }
    return img;
}

QImage makeGray16(int w, int h)
{
    QImage img(w, h, QImage::Format_Grayscale16);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            quint16 v = quint16(65535 * x / w);
            *reinterpret_cast<quint16 *>(img.scanLine(y) + x * 2) = v;
        }
    return img;
}

QColorSpace loadAdobeRgb()
{
    const QString path = QStringLiteral(R"(C:\Windows\System32\spool\drivers\color\AdobeRGB1998.icc)");
    QFile f(path);
    if (f.open(QIODevice::ReadOnly))
        return QColorSpace::fromIccProfile(f.readAll());
    return QColorSpace();
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    const QString samplesDir = QStringLiteral(SOLEVIEW_FORMAT_SAMPLES_DIR);
    const QColorSpace adobe = loadAdobeRgb();

    struct Sample { QString label; QImage image; };
    QVector<Sample> samples;

    // 合成：无 profile RGB / 带透明 RGBA / 16bit 高位深
    samples.push_back({QStringLiteral("synth-rgb-untagged"), makeRgb(64, 48)});
    samples.push_back({QStringLiteral("synth-rgba-alpha"), makeRgba(64, 48)});
    samples.push_back({QStringLiteral("synth-gray16"), makeGray16(64, 48)});
    if (adobe.isValid()) {
        QImage ad = makeRgb(64, 48);
        ad.setColorSpace(adobe);
        samples.push_back({QStringLiteral("synth-adobe-icc"), ad});
    } else {
        std::fprintf(stderr, "NOTE: AdobeRGB profile 不可用，跳过非 sRGB ICC 合成样本\n");
    }

    // fixtures：JPEG/WebP/PNG/APNG/DDS/PAM/EXR/HDR/JXL/JP2
    const QStringList fixtureNames = {
        "sample.jpe", "single.webp", "single.png", "anim_png.png", "sample_bgra.dds",
        "sample.pam", "sample.exr", "sample.hdr", "sample.jxl", "sample.jp2",
    };
    for (const QString &name : fixtureNames) {
        const QImage img = decodeAny(QDir(samplesDir).absoluteFilePath(name));
        if (img.isNull()) {
            ++g_skipped;
            std::fprintf(stderr, "NOTE: 解码跳过 %s\n", qPrintable(name));
            continue;
        }
        samples.push_back({name, img});
    }

    // 大图（可选目录，由 SOLEVIEW_EQ_BIG_DIR 指定）：4000×3000 内嵌 AdobeRGB ICC、
    // 4000×3000 RGBA 透明、两张 Z049 实拍 3648×5472 / 5472×3648。覆盖整图与大 stride
    // 路径；目录或缺任一图时跳过并计入 skipped，不判失败。
    const QString bigDir = QString::fromLocal8Bit(qgetenv("SOLEVIEW_EQ_BIG_DIR"));
    if (!bigDir.isEmpty()) {
        const QStringList bigNames = {
            "perf_adobe.jpg", "perf_transparent.png",
            "perf_z049_0001.jpg", "perf_z049_0003.jpg",
        };
        for (const QString &name : bigNames) {
            const QImage img = decodeAny(QDir(bigDir).absoluteFilePath(name));
            if (img.isNull()) {
                ++g_skipped;
                std::fprintf(stderr, "NOTE: 大图解码跳过 %s\n", qPrintable(name));
                continue;
            }
            samples.push_back({QStringLiteral("big-") + name, img});
        }
    }

    const DisplayFrame::PrepareOptions opts[2] = {
        {true, false},   // ICC 开 / tone map 关（默认 SDR）
        {false, true},   // ICC 关 / tone map 开（高位深路径）
    };

    int propFail = 0;
    for (const Sample &s : samples) {
        for (const DisplayFrame::PrepareOptions &opt : opts) {
            const QImage legacy = DisplayFrame::legacyPrepare(s.image, opt);
            const QImage fresh = DisplayFrame::prepare(s.image, opt);
            ++g_pairs;
            QString detail;
            if (!imagesEqual(legacy, fresh, detail)) {
                ++g_failures;
                std::fprintf(stderr, "MISMATCH [%s] icc=%d tonemap=%d: %s\n",
                             qPrintable(s.label), opt.imageIcc, opt.hdrTonemap, qPrintable(detail));
            } else {
                std::printf("OK [%s] icc=%d tonemap=%d  size=%dx%d fmt=%d bytes=%lld\n",
                             qPrintable(s.label), opt.imageIcc, opt.hdrTonemap,
                             fresh.width(), fresh.height(), fresh.format(),
                             static_cast<long long>(fresh.sizeInBytes()));
            }
        }
    }

    std::printf("\n== 等价结论：比对 %d 组（%d 张样本 × 2 选项），跳过 %d，不一致 %d ==\n",
                g_pairs, samples.size(), g_skipped, g_failures);
    if (g_failures != 0)
        return 1;
    return 0;
}

