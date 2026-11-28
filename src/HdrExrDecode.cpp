#include "HdrExrDecode.h"

#include "HdrPreview.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef HAS_OPENEXR
#include <ImfArray.h>
#include <ImfChromaticities.h>
#include <ImfChromaticitiesAttribute.h>
#include <ImfIO.h>
#include <ImfRgbaFile.h>
#include <ImathBox.h>
#endif

namespace {

constexpr int kMaxDim = 30000;
constexpr quint64 kMaxPixels = 100000000ULL;
// 亮度统计的采样目标：约 20 万点。再多对白点几乎没有影响，只是白花 CPU。
constexpr double kMeterSamples = 200000.0;

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

int meterStep(int w, int h)
{
    const double s = std::sqrt(double(w) * double(h) / kMeterSamples);
    return std::max(1, int(s));
}

} // namespace

namespace HdrExrDecode {

bool isHdrSuffix(const QString &suffixLower)
{
    return suffixLower == QLatin1String("hdr") || suffixLower == QLatin1String("pic");
}

bool isExrSuffix(const QString &suffixLower)
{
    return suffixLower == QLatin1String("exr");
}

// ---------------------------------------------------------------------------
// Radiance HDR（.hdr / .pic，RGBE 编码）
//
// 两遍扫描：第一遍只累计亮度样本算白点，第二遍按白点曲线写像素。解码是按行
// 游标推进的，第一遍读完必须回到像素起点重来；这样峰值内存仍只有一条扫描线，
// 不会为 8K 图额外开一整幅浮点缓冲。
// ---------------------------------------------------------------------------
QImage decodeRadiance(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut,
                      float exposureEv)
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
        setFail(QStringLiteral("hdr invalid resolution line"), LoadErrorCategory::Corrupt,
                errOut, catOut);
        return QImage();
    }
    bool okH = false, okW = false;
    const int h = parts[1].toInt(&okH);
    const int w = parts[3].toInt(&okW);
    if (!okH || !okW || !validRasterSize(w, h)
        || (parts[0] != "-Y" && parts[0] != "+Y")
        || (parts[2] != "+X" && parts[2] != "-X")) {
        setFail(QStringLiteral("hdr unsupported orientation or size"), LoadErrorCategory::Corrupt,
                errOut, catOut);
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
    const int pixelStart = pos;

    // 一行 RGBE 解码（新式 RLE / 旧式 RLE / 平铺三种编码都写进 scan）。
    QString scanErr;
    auto readScanline = [&]() -> bool {
        if (pos + 4 > n) {
            scanErr = QStringLiteral("hdr truncated scanline");
            return false;
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
                        scanErr = QStringLiteral("hdr rle eof");
                        return false;
                    }
                    const uchar code = p[pos++];
                    if (code > 128) {
                        const int cnt = code & 127;
                        if (pos >= n || x + cnt > w) {
                            scanErr = QStringLiteral("hdr rle overflow");
                            return false;
                        }
                        const uchar val = p[pos++];
                        for (int k = 0; k < cnt; ++k) {
                            scan[size_t(ch) * w + x++] = val;
                        }
                    } else {
                        const int cnt = code;
                        if (cnt == 0 || pos + cnt > n || x + cnt > w) {
                            scanErr = QStringLiteral("hdr rle raw overflow");
                            return false;
                        }
                        for (int k = 0; k < cnt; ++k) {
                            scan[size_t(ch) * w + x++] = p[pos++];
                        }
                    }
                }
            }
            return true;
        }
        // Flat / Old RLE
        int x = 0;
        int rshift = 0;
        while (x < w) {
            if (pos + 4 > n) {
                scanErr = QStringLiteral("hdr flat eof");
                return false;
            }
            const uchar r = p[pos++];
            const uchar g = p[pos++];
            const uchar b = p[pos++];
            const uchar e = p[pos++];
            if (r == 1 && g == 1 && b == 1) {
                if (x == 0 || rshift >= 24) {
                    scanErr = QStringLiteral("hdr old rle invalid");
                    return false;
                }
                const int cnt = int(e) << rshift;
                rshift += 8;
                const uchar pr = scan[size_t(x - 1)];
                const uchar pg = scan[size_t(w) + x - 1];
                const uchar pb = scan[size_t(w) * 2 + x - 1];
                const uchar pe = scan[size_t(w) * 3 + x - 1];
                if (x + cnt > w) {
                    scanErr = QStringLiteral("hdr old rle overflow");
                    return false;
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
        return true;
    };

    // 单像素取线性值；e==0 表示精确黑。
    auto linearAt = [&](int sx, float rgb[3]) {
        const uchar e = scan[size_t(w) * 3 + sx];
        if (e == 0) {
            rgb[0] = rgb[1] = rgb[2] = 0.0f;
            return;
        }
        const float f = std::ldexp(1.0f, int(e) - 128 - 8) * invExp;
        rgb[0] = (scan[size_t(sx)] + 0.5f) * f;
        rgb[1] = (scan[size_t(w) + sx] + 0.5f) * f;
        rgb[2] = (scan[size_t(w) * 2 + sx] + 0.5f) * f;
    };

    // 第一遍：只统计亮度，定白点。天空穹顶下半球的精确黑不参与。
    const int step = meterStep(w, h);
    HdrPreview::Meter meter;
    for (int y = 0; y < h; ++y) {
        if (!readScanline()) {
            setFail(scanErr, LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        if (y % step != 0) {
            continue;
        }
        for (int x = 0; x < w; x += step) {
            float rgb[3];
            linearAt(x, rgb);
            meter.add(rgb[0], rgb[1], rgb[2]);
        }
    }

    const HdrPreview::Curve curve(meter.whitePoint());

    // 第二遍：回到像素起点重解，按曲线写 8bit sRGB。
    pos = pixelStart;
    for (int y = 0; y < h; ++y) {
        if (!readScanline()) {
            setFail(scanErr, LoadErrorCategory::Corrupt, errOut, catOut);
            return QImage();
        }
        const int dstY = flipY ? (h - 1 - y) : y;
        uchar *dst = img.scanLine(dstY);
        for (int x = 0; x < w; ++x) {
            const int sx = flipX ? (w - 1 - x) : x;
            float rgb[3];
            linearAt(sx, rgb);
            dst[x * 3 + 0] = curve(rgb[0]);
            dst[x * 3 + 1] = curve(rgb[1]);
            dst[x * 3 + 2] = curve(rgb[2]);
        }
    }
    return img;
}

// ---------------------------------------------------------------------------
// OpenEXR（.exr）
//
// 像素本来就整幅驻留（Imf::Rgba 数组），统计与映射都在这一份数据上完成，
// 不再额外开浮点副本。色彩元数据：有 chromaticities 且不等于 Rec.709/sRGB 时
// 先做 RGB→XYZ→sRGB 转换；缺失时按 OpenEXR 默认假设（Rec.709 基色）处理。
// ---------------------------------------------------------------------------
#ifdef HAS_OPENEXR
namespace {

class MemoryExrIStream : public Imf::IStream
{
public:
    explicit MemoryExrIStream(const QByteArray &data)
        : Imf::IStream("memory"), m_buf(data)
    {
    }
    bool read(char c[], int n) override
    {
        if (m_pos + uint64_t(n) > uint64_t(m_buf.size())) {
            throw std::runtime_error("exr: unexpected end of file");
        }
        std::memcpy(c, m_buf.constData() + m_pos, size_t(n));
        m_pos += uint64_t(n);
        return true;
    }
    uint64_t tellg() override { return m_pos; }
    void seekg(uint64_t pos) override { m_pos = pos; }
    void clear() override {}

private:
    const QByteArray &m_buf;
    uint64_t m_pos = 0;
};

bool chromaticitiesToSrgb(const Imf::Header &header, Imath::M44f *out)
{
    const auto *attr = header.findTypedAttribute<Imf::ChromaticitiesAttribute>("chromaticities");
    if (!attr) {
        return false;
    }
    const Imf::Chromaticities &src = attr->value();
    const Imf::Chromaticities dst;   // 默认即 Rec.709 / sRGB
    const bool same = src.red == dst.red && src.green == dst.green
                      && src.blue == dst.blue && src.white == dst.white;
    if (same) {
        return false;
    }
    // Imath 用行向量：p' = p · RGBtoXYZ(src) · XYZtoRGB(dst)
    const Imath::M44f m = Imf::RGBtoXYZ(src, 1.0f) * Imf::XYZtoRGB(dst, 1.0f);
    // 与单位阵足够接近就当没有（避免给绝大多数文件白算一遍矩阵）
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            const float want = (i == j) ? 1.0f : 0.0f;
            if (std::fabs(m[i][j] - want) > 1e-3f) {
                *out = m;
                return true;
            }
        }
    }
    return false;
}

} // namespace
#endif

QImage decodeExr(const QByteArray &data, QString *errOut, LoadErrorCategory *catOut,
                 float exposureEv)
{
#ifdef HAS_OPENEXR
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
            setFail(QStringLiteral("exr invalid dataWindow"), LoadErrorCategory::Corrupt,
                    errOut, catOut);
            return QImage();
        }
        Imf::Array2D<Imf::Rgba> pixels(h, w);
        file.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * ptrdiff_t(w), 1, w);
        file.readPixels(dw.min.y, dw.max.y);

        Imath::M44f toSrgb;
        const bool convert = chromaticitiesToSrgb(file.header(), &toSrgb);

        // 先统计：只收 alpha>0 且亮度>0 的像素；矩阵先做，曝光按 sRGB 基色统计。
        const int step = meterStep(w, h);
        HdrPreview::Meter meter;
        for (int y = 0; y < h; y += step) {
            for (int x = 0; x < w; x += step) {
                const Imf::Rgba &px = pixels[y][x];
                float r = float(px.r);
                float g = float(px.g);
                float b = float(px.b);
                const float a = float(px.a);
                if (!std::isfinite(a) || a <= 0.0f) {
                    continue;
                }
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
                    continue;
                }
                if (convert) {
                    const float nr = r * toSrgb[0][0] + g * toSrgb[1][0] + b * toSrgb[2][0];
                    const float ng = r * toSrgb[0][1] + g * toSrgb[1][1] + b * toSrgb[2][1];
                    const float nb = r * toSrgb[0][2] + g * toSrgb[1][2] + b * toSrgb[2][2];
                    r = nr;
                    g = ng;
                    b = nb;
                }
                meter.add(r, g, b);
            }
        }
        const HdrPreview::Curve curve(meter.whitePoint(), exposureEv);

        QImage img(w, h, QImage::Format_RGBA8888);
        if (img.isNull()) return QImage();
        for (int y = 0; y < h; ++y) {
            uchar *dst = img.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const Imf::Rgba &px = pixels[y][x];
                float r = float(px.r);
                float g = float(px.g);
                float b = float(px.b);
                if (convert) {
                    const float nr = r * toSrgb[0][0] + g * toSrgb[1][0] + b * toSrgb[2][0];
                    const float ng = r * toSrgb[0][1] + g * toSrgb[1][1] + b * toSrgb[2][1];
                    const float nb = r * toSrgb[0][2] + g * toSrgb[1][2] + b * toSrgb[2][2];
                    r = nr;
                    g = ng;
                    b = nb;
                }
                dst[x * 4 + 0] = curve(r);
                dst[x * 4 + 1] = curve(g);
                dst[x * 4 + 2] = curve(b);
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
#else
    Q_UNUSED(data);
    setFail(QStringLiteral("exr unsupported build"), LoadErrorCategory::Unsupported, errOut, catOut);
    return QImage();
#endif
}

} // namespace HdrExrDecode
