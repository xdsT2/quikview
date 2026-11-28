// 显示管线分段基准（离线，offscreen）。复刻 ImageCache worker 解码链与基线
// MainWindow::showImage / ImageCanvas::toMonitorSpace 的旧路径操作，逐段计时。
// 用法：display-bench --samples <dir> --mode legacy|new --out <csv> [--rounds N]
// 段：decode / srgb / tonemap / monitor / fromImage；paint 离线不可测，记 未测。
#include "ArchiveStore.h"
#include "DisplayFrame.h"
#include "ExtraImageLoader.h"
#include "PsdLoader.h"
#include "RawImageLoader.h"
#include "HeifLoader.h"
#include "SvgLoader.h"
#include "ToneMap.h"

#include <QApplication>
#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifdef HAS_OPENEXR
#include <ImfIO.h>
#include <ImfInputFile.h>
#include <ImfFrameBuffer.h>
#include <ImfChannelList.h>
#include <ImfPixelType.h>
#include <ImfRgbaFile.h>
#include <ImathBox.h>
#include <ImfArray.h>
#endif

namespace {

// 与 ImageCache::startFullDecode 同构的解码分支（样本集均为 reader/extra 可解）。
QImage decodeLikeCache(const QString &path, bool hdrTonemap)
{
    if (ArchiveStore::isEntryPath(path)) {
        QString ignored;
        return ArchiveStore::decode(path, QSize(), &ignored);
    }
    if (PsdLoader::isPsdPath(path))
        return PsdLoader::load(path, false).composite;
    if (SvgLoader::isSvgPath(path)) {
        QString err; LoadErrorCategory cat = LoadErrorCategory::None;
        return SvgLoader::render(path, QSize(), false, &err, &cat);
    }
    if (ExtraImageLoader::isExtraPath(path)) {
        QString err; LoadErrorCategory cat = LoadErrorCategory::None;
        return ExtraImageLoader::load(path, QSize(), &err, &cat, hdrTonemap);
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull() && isRawFilePath(path)) {
        QString rawErr;
        img = loadRawImage(path, &rawErr, hdrTonemap);
    }
    if (img.isNull()) {
        QString heifErr;
        img = loadHeifImage(path, &heifErr, hdrTonemap);
    }
    return img;
}

// 基线旧路径：先 toneMap，再 ICC→sRGB（MainWindow 4897 语义复刻）。

double median(std::vector<qint64> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2] / 1000.0; // us -> ms
}

double p90(std::vector<qint64> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const size_t idx = std::min(v.size() - 1, size_t(qsizetype(v.size() * 0.9)));
    return v[idx] / 1000.0;
}

double minMs(std::vector<qint64> v)
{
    if (v.empty()) return 0;
    return *std::min_element(v.begin(), v.end()) / 1000.0;
}

double maxMs(std::vector<qint64> v)
{
    if (v.empty()) return 0;
    return *std::max_element(v.begin(), v.end()) / 1000.0;
}

QString formatName(QImage::Format f)
{
    switch (f) {
    case QImage::Format_RGBA32FPx4: return QStringLiteral("RGBA32FPx4");
    case QImage::Format_RGBX32FPx4: return QStringLiteral("RGBX32FPx4");
    case QImage::Format_RGBA16FPx4: return QStringLiteral("RGBA16FPx4");
    case QImage::Format_RGBA64: return QStringLiteral("RGBA64");
    case QImage::Format_RGB32: return QStringLiteral("RGB32");
    case QImage::Format_ARGB32: return QStringLiteral("ARGB32");
    case QImage::Format_Grayscale8: return QStringLiteral("Grayscale8");
    default: return QStringLiteral("other(%1)").arg(int(f));
    }
}

double availPhysMB()
{
#ifdef Q_OS_WIN
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st))
        return double(st.ullAvailPhys) / 1048576.0;
#endif
    return -1.0;
}

// ---------------- 真 HDR 浮点直通（做法 B；仅 bench 层绕开解码层 8bit 折叠） ----------------

bool imageBytesEqual(const QImage &a, const QImage &b, QString &detail)
{
    if (a.isNull() || b.isNull()) {
        if (a.isNull() != b.isNull()) { detail = QStringLiteral("isNull 不一致"); return false; }
        return true;
    }
    if (a.size() != b.size()) { detail = QStringLiteral("尺寸不一致"); return false; }
    if (a.format() != b.format()) { detail = QStringLiteral("格式不一致"); return false; }
    if (a.sizeInBytes() != b.sizeInBytes()) { detail = QStringLiteral("字节数不一致"); return false; }
    if (std::memcmp(a.constBits(), b.constBits(), a.sizeInBytes()) != 0) {
        detail = QStringLiteral("像素字节不一致"); return false;
    }
    return true;
}

QString tierOf(const QString &name)
{
    if (name.contains(QLatin1String("8k"), Qt::CaseInsensitive)) return QStringLiteral("8k");
    if (name.contains(QLatin1String("4k"), Qt::CaseInsensitive)) return QStringLiteral("4k");
    if (name.contains(QLatin1String("1k"), Qt::CaseInsensitive)) return QStringLiteral("1k");
    return QStringLiteral("?");
}

#ifdef HAS_OPENEXR
class BenchExrIStream : public Imf::IStream
{
public:
    explicit BenchExrIStream(const QByteArray &buf) : Imf::IStream("bench.exr"), m_buf(buf), m_pos(0) {}
    bool isMemoryMapped() const override { return true; }
    char *readMemoryMapped(int n) override
    {
        if (m_pos + quint64(n) > quint64(m_buf.size()))
            throw IEX_NAMESPACE::InputExc("Unexpected EOF in EXR memory stream");
        char *ptr = const_cast<char *>(m_buf.constData() + m_pos);
        m_pos += quint64(n);
        return ptr;
    }
    bool read(char c[], int n) override
    {
        if (m_pos + quint64(n) > quint64(m_buf.size()))
            throw IEX_NAMESPACE::InputExc("Unexpected EOF in EXR memory stream");
        std::memcpy(c, m_buf.constData() + m_pos, size_t(n));
        m_pos += quint64(n);
        return m_pos < quint64(m_buf.size());
    }
    uint64_t tellg() override { return m_pos; }
    void seekg(uint64_t pos) override { m_pos = pos; }

private:
    const QByteArray &m_buf;
    uint64_t m_pos = 0;
};
#endif

// EXR → 线性浮点 RGBA32FPx4。优先 FLOAT 通道直读（保留 32-bit 浮点，不打折）；
// 通道为 half 时回落到 Imf::Rgba（half）。回落路径会在 CSV depth 列标注。
QImage parseExrFloat(const QByteArray &data, QString *errOut, QString *routeOut)
{
#ifdef HAS_OPENEXR
    try {
        BenchExrIStream stream(data);
        Imf::InputFile file(stream);
        if (!file.isComplete()) {
            if (errOut) *errOut = QStringLiteral("exr incomplete");
            return QImage();
        }
        const Imf::Header &hdr = file.header();
        const Imath::Box2i dw = hdr.dataWindow();
        const int w = dw.max.x - dw.min.x + 1;
        const int h = dw.max.y - dw.min.y + 1;
        if (w <= 0 || h <= 0 || quint64(w) * quint64(h) > 100000000ULL) {
            if (errOut) *errOut = QStringLiteral("exr bad size");
            return QImage();
        }

        const Imf::ChannelList &chans = hdr.channels();
        auto chanType = [&chans](const char *name) -> Imf::PixelType {
            auto it = chans.find(name);
            return it == chans.end() ? Imf::NUM_PIXELTYPES : it.channel().type;
        };
        const bool floatR = chanType("R") == Imf::FLOAT;
        const bool floatG = chanType("G") == Imf::FLOAT;
        const bool floatB = chanType("B") == Imf::FLOAT;

        if (floatR && floatG && floatB) {
            std::vector<float> r(quint64(w) * h), g(quint64(w) * h), b(quint64(w) * h);
            const bool hasA = chanType("A") == Imf::FLOAT;
            std::vector<float> a;
            if (hasA) a.assign(quint64(w) * h, 1.0f);

            Imf::FrameBuffer fb;
            const ptrdiff_t baseOffX = dw.min.x;
            const ptrdiff_t baseOffY = dw.min.y;
            auto addChan = [&](const char *name, std::vector<float> &buf) {
                fb.insert(name, Imf::Slice(Imf::FLOAT,
                    reinterpret_cast<char *>(buf.data())
                        - baseOffX * static_cast<ptrdiff_t>(sizeof(float))
                        - baseOffY * w * static_cast<ptrdiff_t>(sizeof(float)),
                    static_cast<ptrdiff_t>(sizeof(float)),
                    static_cast<ptrdiff_t>(sizeof(float)) * w));
            };
            addChan("R", r); addChan("G", g); addChan("B", b);
            if (hasA) addChan("A", a);
            file.setFrameBuffer(fb);
            file.readPixels(dw.min.y, dw.max.y);

            QImage img(w, h, QImage::Format_RGBA32FPx4);
            if (img.isNull()) {
                if (errOut) *errOut = QStringLiteral("exr alloc failed (OOM)");
                return QImage();
            }
            for (int y = 0; y < h; ++y) {
                float *line = reinterpret_cast<float *>(img.scanLine(y));
                for (int x = 0; x < w; ++x) {
                    const size_t idx = size_t(y) * w + x;
                    line[x * 4 + 0] = r[idx];
                    line[x * 4 + 1] = g[idx];
                    line[x * 4 + 2] = b[idx];
                    line[x * 4 + 3] = hasA ? a[idx] : 1.0f;
                }
            }
            if (routeOut) *routeOut = QStringLiteral("EXR-float32");
            return img;
        }

        // 回落：Rgba（half）通道
        stream.seekg(0);
        Imf::RgbaInputFile rgba(stream);
        if (!rgba.isComplete()) {
            if (errOut) *errOut = QStringLiteral("exr incomplete (rgba)");
            return QImage();
        }
        Imf::Array2D<Imf::Rgba> pixels(h, w);
        rgba.setFrameBuffer(&pixels[0][0] - dw.min.x - dw.min.y * ptrdiff_t(w), 1, w);
        rgba.readPixels(dw.min.y, dw.max.y);
        QImage img(w, h, QImage::Format_RGBA32FPx4);
        if (img.isNull()) {
            if (errOut) *errOut = QStringLiteral("exr alloc failed (OOM, rgba)");
            return QImage();
        }
        for (int y = 0; y < h; ++y) {
            float *line = reinterpret_cast<float *>(img.scanLine(y));
            for (int x = 0; x < w; ++x) {
                const Imf::Rgba &px = pixels[y][x];
                const float av = std::isfinite(float(px.a)) ? float(px.a) : 1.0f;
                line[x * 4 + 0] = float(px.r);
                line[x * 4 + 1] = float(px.g);
                line[x * 4 + 2] = float(px.b);
                line[x * 4 + 3] = av;
            }
        }
        if (routeOut) *routeOut = QStringLiteral("EXR-half(rgba)");
        return img;
    } catch (const std::exception &ex) {
        if (errOut) *errOut = QString::fromUtf8(ex.what());
        return QImage();
    } catch (...) {
        if (errOut) *errOut = QStringLiteral("exr unknown decode error");
        return QImage();
    }
#else
    if (errOut) *errOut = QStringLiteral("本构建未启用 HAS_OPENEXR");
    return QImage();
#endif
}

// RGBE Radiance HDR → 线性浮点 RGBA32FPx4（自解，不经过解码层 8bit 折叠）。
QImage parseHdrFloat(const QByteArray &data, QString *errOut)
{
    if (data.size() < 16 || (!data.startsWith("#?RADIANCE") && !data.startsWith("#?RGBE"))) {
        if (errOut) *errOut = QStringLiteral("hdr invalid magic");
        return QImage();
    }
    int pos = 0;
    const int n = data.size();
    auto readLine = [&data, &pos, n]() -> QByteArray {
        const int start = pos;
        while (pos < n && data.at(pos) != '\n') ++pos;
        QByteArray line = data.mid(start, pos - start);
        if (pos < n) ++pos;
        if (line.endsWith('\r')) line.chop(1);
        return line;
    };

    float exposure = 1.0f;
    bool foundBlank = false;
    while (pos < n) {
        const QByteArray line = readLine();
        if (line.isEmpty()) { foundBlank = true; break; }
        if (line.startsWith("EXPOSURE=")) {
            bool ok = false;
            const float ex = line.mid(9).trimmed().toFloat(&ok);
            if (ok && ex > 0.0f && std::isfinite(ex)) exposure *= ex;
        }
    }
    if (!foundBlank || pos >= n) {
        if (errOut) *errOut = QStringLiteral("hdr header truncated");
        return QImage();
    }

    const QByteArray dimLine = readLine();
    const QList<QByteArray> parts = dimLine.simplified().split(' ');
    if (parts.size() != 4) {
        if (errOut) *errOut = QStringLiteral("hdr invalid resolution line");
        return QImage();
    }
    bool okH = false, okW = false;
    const int h = parts[1].toInt(&okH);
    const int w = parts[3].toInt(&okW);
    if (!okH || !okW || w <= 0 || h <= 0 || quint64(w) * quint64(h) > 100000000ULL
        || (parts[0] != "-Y" && parts[0] != "+Y")
        || (parts[2] != "+X" && parts[2] != "-X")) {
        if (errOut) *errOut = QStringLiteral("hdr unsupported orientation or size");
        return QImage();
    }
    const bool flipY = (parts[0] == "+Y");
    const bool flipX = (parts[2] == "-X");

    QImage img(w, h, QImage::Format_RGBA32FPx4);
    if (img.isNull()) {
        if (errOut) *errOut = QStringLiteral("hdr alloc failed (OOM)");
        return QImage();
    }

    const uchar *p = reinterpret_cast<const uchar *>(data.constData());
    std::vector<uchar> scan(size_t(w) * 4);
    const float invExp = 1.0f / exposure;

    for (int y = 0; y < h; ++y) {
        if (pos + 4 > n) { if (errOut) *errOut = QStringLiteral("hdr truncated scanline"); return QImage(); }
        const bool newRle = (w >= 8 && w <= 0x7FFF && p[pos] == 2 && p[pos + 1] == 2
                             && (p[pos + 2] & 0x80) == 0
                             && ((int(p[pos + 2]) << 8) | p[pos + 3]) == w);
        if (newRle) {
            pos += 4;
            for (int ch = 0; ch < 4; ++ch) {
                int x = 0;
                while (x < w) {
                    if (pos >= n) { if (errOut) *errOut = QStringLiteral("hdr rle eof"); return QImage(); }
                    const uchar code = p[pos++];
                    if (code > 128) {
                        const int cnt = code & 127;
                        if (pos >= n || x + cnt > w) { if (errOut) *errOut = QStringLiteral("hdr rle overflow"); return QImage(); }
                        const uchar val = p[pos++];
                        for (int k = 0; k < cnt; ++k) scan[size_t(ch) * w + x++] = val;
                    } else {
                        const int cnt = code;
                        if (cnt == 0 || pos + cnt > n || x + cnt > w) { if (errOut) *errOut = QStringLiteral("hdr rle raw overflow"); return QImage(); }
                        for (int k = 0; k < cnt; ++k) scan[size_t(ch) * w + x++] = p[pos++];
                    }
                }
            }
        } else {
            int x = 0;
            int rshift = 0;
            while (x < w) {
                if (pos + 4 > n) { if (errOut) *errOut = QStringLiteral("hdr flat eof"); return QImage(); }
                const uchar r = p[pos++], g = p[pos++], b = p[pos++], e = p[pos++];
                if (r == 1 && g == 1 && b == 1) {
                    if (x == 0 || rshift >= 24) { if (errOut) *errOut = QStringLiteral("hdr old rle invalid"); return QImage(); }
                    const int cnt = int(e) << rshift;
                    rshift += 8;
                    const uchar pr = scan[size_t(x - 1)];
                    const uchar pg = scan[size_t(w) + x - 1];
                    const uchar pb = scan[size_t(w) * 2 + x - 1];
                    const uchar pe = scan[size_t(w) * 3 + x - 1];
                    if (x + cnt > w) { if (errOut) *errOut = QStringLiteral("hdr old rle overflow"); return QImage(); }
                    for (int k = 0; k < cnt; ++k) {
                        scan[size_t(x)] = pr; scan[size_t(w) + x] = pg;
                        scan[size_t(w) * 2 + x] = pb; scan[size_t(w) * 3 + x] = pe;
                        ++x;
                    }
                } else {
                    scan[size_t(x)] = r; scan[size_t(w) + x] = g;
                    scan[size_t(w) * 2 + x] = b; scan[size_t(w) * 3 + x] = e;
                    ++x;
                    rshift = 0;
                }
            }
        }

        const int dstY = flipY ? (h - 1 - y) : y;
        float *line = reinterpret_cast<float *>(img.scanLine(dstY));
        for (int x = 0; x < w; ++x) {
            const int sx = flipX ? (w - 1 - x) : x;
            const uchar e = scan[size_t(w) * 3 + sx];
            float r = 0, g = 0, b = 0;
            if (e != 0) {
                const float f = std::ldexp(1.0f, int(e) - 128 - 8) * invExp;
                r = (scan[size_t(sx)] + 0.5f) * f;
                g = (scan[size_t(w) + sx] + 0.5f) * f;
                b = (scan[size_t(w) * 2 + sx] + 0.5f) * f;
            }
            line[x * 4 + 0] = r;
            line[x * 4 + 1] = g;
            line[x * 4 + 2] = b;
            line[x * 4 + 3] = 1.0f;
        }
    }
    return img;
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    QString samplesDir, floatDir, isoDir, outCsv, mode = QStringLiteral("legacy"), prefix;
    int rounds = 5;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLatin1(argv[i]);
        if (a == "--samples" && i + 1 < argc) samplesDir = QString::fromLatin1(argv[++i]);
        else if (a == "--float-dir" && i + 1 < argc) floatDir = QString::fromLatin1(argv[++i]);
        else if (a == "--iso" && i + 1 < argc) isoDir = QString::fromLatin1(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outCsv = QString::fromLatin1(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) mode = QString::fromLatin1(argv[++i]);
        else if (a == "--rounds" && i + 1 < argc) rounds = QString::fromLatin1(argv[++i]).toInt();
        else if (a == "--prefix" && i + 1 < argc) prefix = QString::fromLatin1(argv[++i]);
    }
    if (outCsv.isEmpty() || (samplesDir.isEmpty() && floatDir.isEmpty() && isoDir.isEmpty())) {
        std::fprintf(stderr, "usage: display-bench --samples <dir> --out <csv> [--mode legacy|new] [--rounds N]\n"
                             "       display-bench --float-dir <dir> --out <csv> [--rounds N]\n");
        return 2;
    }

    // 模拟显示器目标 profile（本机 monitorIcc 默认关，离线用非 sRGB profile 逼出转换负载）。
    const QString profPath = QStringLiteral(R"(C:\Windows\System32\spool\drivers\color\AdobeRGB1998.icc)");
    QColorSpace target;
    {
        QFile f(profPath);
        if (f.open(QIODevice::ReadOnly))
            target = QColorSpace::fromIccProfile(f.readAll());
    }

    // ---- 做法 B：真浮点直通 bench（绕开解码层 8bit 折叠；仅离线基准，不改产品行为）----
    if (!floatDir.isEmpty()) {
        QDir fdir(floatDir);
        QStringList ffiles = fdir.entryList(
            {QStringLiteral("*.exr"), QStringLiteral("*.hdr")}, QDir::Files, QDir::Name);
        std::sort(ffiles.begin(), ffiles.end(), [](const QString &a, const QString &b) {
            const QString ta = tierOf(a), tb = tierOf(b);
            return ta == tb ? a < b : ta < tb;
        });
        if (!prefix.isEmpty()) {
            QStringList filtered;
            for (const QString &f : ffiles)
                if (f.startsWith(prefix))
                    filtered.append(f);
            ffiles = filtered;
        }

        QFile out(outCsv);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::fprintf(stderr, "无法写出 %s\n", qPrintable(outCsv));
            return 3;
        }
        QTextStream ts(&out);
        ts << "file,route,tier,w,h,depth,rounds,parse_ms,tonemap_med_ms,tonemap_p90_ms,"
              "legacy_total_med_ms,legacy_total_p90_ms,new_total_med_ms,new_total_p90_ms,"
              "eq_pass,eq_bytes,note\n";

        // 新旧路径同一输入、同一选项；toneMap 段调用的是同一个 ToneMap::toneMapToSdr，
        // 该段耗时相同是必然的——收益是这段从 UI 线程挪到 worker，不是段内提速。
        DisplayFrame::PrepareOptions optT;
        optT.imageIcc = false;
        optT.hdrTonemap = true;

        for (const QString &fn : ffiles) {
            const QString path = fdir.absoluteFilePath(fn);
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                ts << fn << ",,,,,,,,,,,,,,0,cannot open\n";
                continue;
            }
            const QByteArray data = f.readAll();
            const bool isExr = fn.endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive);

            QString err, route;
            QElapsedTimer pt;
            pt.start();
            QImage raw = isExr ? parseExrFloat(data, &err, &route)
                               : parseHdrFloat(data, &err);
            if (!isExr) route = QStringLiteral("RGBE-float");
            const double parseMs = pt.nsecsElapsed() / 1000.0 / 1000.0;

            if (raw.isNull()) {
                ts << fn << ',' << route << ',' << tierOf(fn)
                   << ",,," << (isExr ? "EXR" : "RGBE 8+8exp") << ',' << rounds
                   << ',' << parseMs << ",,,,,,,,,0,," << err << '\n';
                continue;
            }

            const QImage leg = DisplayFrame::legacyPrepare(raw, optT);
            const QImage fresh = DisplayFrame::prepare(raw, optT);
            QString detail;
            const bool eq = imageBytesEqual(leg, fresh, detail);

            std::vector<qint64> tm, legTot, newTot;
            for (int r = 0; r < rounds; ++r) {
                QElapsedTimer t;
                t.start();
                QImage sdr = ToneMap::toneMapToSdr(raw);
                tm.push_back(t.nsecsElapsed() / 1000);
                t.restart();
                QImage l = DisplayFrame::legacyPrepare(raw, optT);
                legTot.push_back(t.nsecsElapsed() / 1000);
                t.restart();
                QImage nw = DisplayFrame::prepare(raw, optT);
                newTot.push_back(t.nsecsElapsed() / 1000);
                (void)sdr; (void)l; (void)nw;
            }

            ts << fn << ',' << route << ',' << tierOf(fn)
               << ',' << raw.width() << ',' << raw.height() << ','
               << (isExr ? "EXR 32bit-float" : "RGBE 8+8exp")
               << ',' << rounds << ',' << parseMs << ','
               << median(tm) << ',' << p90(tm) << ','
               << median(legTot) << ',' << p90(legTot) << ','
               << median(newTot) << ',' << p90(newTot) << ','
               << (eq ? 1 : 0) << ',' << fresh.sizeInBytes() << ','
               << (eq ? QString() : detail) << '\n';
        }
        ts.flush();
        std::printf("bench float mode: dir=%s files=%d -> %s\n",
                    qPrintable(floatDir), ffiles.size(), qPrintable(outCsv));
        return 0;
    }

    // ---- 隔离验证：同一浮点 buffer 上冷/热重复 toneMap，拆分转换/抽样/曲线/映射子段，
    //      并 dump 输入 QImage 形态（格式/ stride / alpha / colorSpace）。仅 bench 取证，不改产品。 ----
    if (!isoDir.isEmpty()) {
        QDir idir(isoDir);
        QStringList ifiles = idir.entryList(
            {QStringLiteral("*.exr"), QStringLiteral("*.hdr")}, QDir::Files, QDir::Name);
        std::sort(ifiles.begin(), ifiles.end(), [](const QString &a, const QString &b) {
            const QString ta = tierOf(a), tb = tierOf(b);
            return ta == tb ? a < b : ta < tb;
        });
        if (!prefix.isEmpty()) {
            QStringList filtered;
            for (const QString &f : ifiles)
                if (f.startsWith(prefix))
                    filtered.append(f);
            ifiles = filtered;
        }

        QFile out(outCsv);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::fprintf(stderr, "无法写出 %s\n", qPrintable(outCsv));
            return 3;
        }
        QTextStream ts(&out);
        ts << "file,route,w,h,format,format_name,size_in_bytes,bytes_per_line,has_alpha,"
              "colorspace_valid,avail_phys_mb,cold1_ms,cold2_ms,cold3_ms,rounds,"
              "tot_min_ms,tot_med_ms,tot_p90_ms,tot_max_ms,conv_med_ms,luma_med_ms,"
              "curve_med_ms,map_est_med_ms,note\n";

        for (const QString &fn : ifiles) {
            const QString path = idir.absoluteFilePath(fn);
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                ts << fn << ",,,,,,,,,,,,,,,,,,,,,,cannot open\n";
                continue;
            }
            const QByteArray data = f.readAll();
            const bool isExr = fn.endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive);

            QString err, route;
            QImage raw = isExr ? parseExrFloat(data, &err, &route)
                               : parseHdrFloat(data, &err);
            if (!isExr) route = QStringLiteral("RGBE-float");

            if (raw.isNull()) {
                ts << fn << ',' << route << ",,,,,,,,,,,,,,,,,,,,," << err << '\n';
                continue;
            }

            const QImage::Format fmt = raw.format();
            const bool hasA = raw.hasAlphaChannel();
            const QColorSpace cs = raw.colorSpace();
            const bool csValid = cs.isValid();
            const double memBefore = availPhysMB();

            // raw 浮点位分类（排查解析器是否写出反规格化/特殊位模式）。
            {
                const size_t n = size_t(raw.width()) * raw.height() * 4;
                const float *fp = reinterpret_cast<const float *>(raw.constBits());
                size_t zero = 0, sub = 0, normal = 0, special = 0;
                float mn = std::numeric_limits<float>::infinity();
                float mx = -std::numeric_limits<float>::infinity();
                for (size_t i = 0; i < n; ++i) {
                    quint32 u;
                    std::memcpy(&u, &fp[i], 4);
                    const quint32 e = (u >> 23) & 0xFF;
                    const quint32 m = u & 0x7FFFFF;
                    if (u == 0) ++zero;
                    else if (e == 0 && m != 0) ++sub;
                    else if (e == 255) ++special;
                    else ++normal;
                    if (std::isfinite(fp[i])) {
                        mn = std::min(mn, fp[i]);
                        mx = std::max(mx, fp[i]);
                    }
                }
                std::printf("[iso] %s raw bits: zero=%.3f%% subnormal=%.3f%% normal=%.3f%% "
                            "special=%.3f%% min=%.6g max=%.6g\n",
                            qPrintable(fn), zero * 100.0 / n, sub * 100.0 / n,
                            normal * 100.0 / n, special * 100.0 / n, mn, mx);
            }

            // 同一 buffer 连续三次：第 1 次为冷（含首次触页/TLB），第 2、3 次为同 buffer 热态。
            qint64 cold1 = 0, cold2 = 0, cold3 = 0;
            {
                QElapsedTimer t;
                t.start();
                QImage s1 = ToneMap::toneMapToSdr(raw);
                cold1 = t.nsecsElapsed() / 1000;
                t.restart();
                QImage s2 = ToneMap::toneMapToSdr(raw);
                cold2 = t.nsecsElapsed() / 1000;
                t.restart();
                QImage s3 = ToneMap::toneMapToSdr(raw);
                cold3 = t.nsecsElapsed() / 1000;
                (void)s1; (void)s2; (void)s3;
            }

            std::vector<qint64> tot, conv, luma, curve;
            for (int r = 0; r < rounds; ++r) {
                QElapsedTimer t;
                t.start();
                QImage c = raw.convertToFormat(QImage::Format_RGBA64);
                conv.push_back(t.nsecsElapsed() / 1000);
                t.restart();
                QVector<quint32> samples = ToneMap::sampleLuma16(c);
                luma.push_back(t.nsecsElapsed() / 1000);
                t.restart();
                ToneMap::Curve16 cv = ToneMap::buildCurve16(samples);
                curve.push_back(t.nsecsElapsed() / 1000);
                t.restart();
                QImage s = ToneMap::toneMapToSdr(raw);
                tot.push_back(t.nsecsElapsed() / 1000);
                (void)c; (void)samples; (void)cv; (void)s;
            }

            const double mapEst = std::max(0.0, median(tot) - median(conv) - median(luma) - median(curve));
            std::printf("[iso] %s route=%s fmt=%s(%d) %dx%d bytes=%lld stride=%d alpha=%d "
                        "csValid=%d availPhys=%.0fMB cold=%.2f/%.2f/%.2f ms tot med=%.2f max=%.2f\n",
                        qPrintable(fn), qPrintable(route), qPrintable(formatName(fmt)), int(fmt),
                        raw.width(), raw.height(), qlonglong(raw.sizeInBytes()), raw.bytesPerLine(),
                        int(hasA), int(csValid), memBefore,
                        cold1 / 1000.0, cold2 / 1000.0, cold3 / 1000.0,
                        median(tot), maxMs(tot));

            ts << fn << ',' << route << ',' << raw.width() << ',' << raw.height()
               << ',' << int(fmt) << ',' << formatName(fmt) << ',' << raw.sizeInBytes()
               << ',' << raw.bytesPerLine() << ',' << int(hasA) << ',' << int(csValid)
               << ',' << memBefore << ',' << cold1 / 1000.0 << ',' << cold2 / 1000.0
               << ',' << cold3 / 1000.0 << ',' << rounds << ','
               << minMs(tot) << ',' << median(tot) << ',' << p90(tot) << ',' << maxMs(tot)
               << ',' << median(conv) << ',' << median(luma) << ',' << median(curve)
               << ',' << mapEst << ",\n";
        }
        ts.flush();
        std::printf("bench iso mode: dir=%s files=%d -> %s\n",
                    qPrintable(isoDir), ifiles.size(), qPrintable(outCsv));
        return 0;
    }

    const QDir dir(samplesDir);
    QStringList files = dir.entryList({QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                                              QStringLiteral("*.png"), QStringLiteral("*.webp"),
                                              QStringLiteral("*.jxl"), QStringLiteral("*.exr"),
                                              QStringLiteral("*.hdr"), QStringLiteral("*.pfm"),
                                              QStringLiteral("*.jp2")},
                                             QDir::Files, QDir::Name);
    if (!prefix.isEmpty()) {
        QStringList filtered;
        for (const QString &f : files)
            if (f.startsWith(prefix))
                filtered.append(f);
        files = filtered;
    }

    DisplayFrame::PrepareOptions opt;
    opt.imageIcc = true;
    opt.hdrTonemap = false; // 本版恒 false；高位深样本另在等价测试覆盖 toneMap

    // new 模式模拟 P0-b 的显示器 profile 位图缓存：首算落缓存，其后命中近零成本。
    QHash<QString, QPixmap> monitorCache;

    struct Row {
        QString file;
        std::vector<qint64> decode, srgb, tonemap, monitor, fromImage, total;
    };
    std::vector<Row> rows;

    for (const QString &fn : files) {
        Row row;
        row.file = fn;
        const QString path = dir.absoluteFilePath(fn);
        for (int r = 0; r < rounds; ++r) {
            QElapsedTimer total;
            total.start();

            // decode
            QElapsedTimer t;
            t.start();
            QImage raw = decodeLikeCache(path, opt.hdrTonemap);
            row.decode.push_back(t.nsecsElapsed() / 1000);
            if (raw.isNull()) {
                row.decode.push_back(0); row.srgb.push_back(0); row.tonemap.push_back(0);
                row.monitor.push_back(0); row.fromImage.push_back(0); row.total.push_back(0);
                break;
            }

            // 后处理：legacy 先 toneMap 再 srgb；new 走 DisplayFrame::prepare（同一顺序，后台合并）
            QImage sdr;
            t.restart();
            if (opt.hdrTonemap) {
                sdr = ToneMap::toneMapToSdr(raw);
                row.tonemap.push_back(t.nsecsElapsed() / 1000);
            } else {
                sdr = raw;
                row.tonemap.push_back(0);
            }
            t.restart();
            QImage ready;
            if (mode == QLatin1String("new")) {
                ready = DisplayFrame::prepare(sdr, opt);
            } else {
                ready = sdr;
                if (opt.imageIcc && !ready.isNull()) {
                    const QColorSpace cs = ready.colorSpace();
                    if (cs.isValid() && cs != QColorSpace::SRgb)
                        ready.convertToColorSpace(QColorSpace::SRgb);
                }
            }
            row.srgb.push_back(t.nsecsElapsed() / 1000);

            // monitor profile 转换（toMonitorSpace 语义；目标非 sRGB 才转）。
            // legacy：每次重算；new：位图缓存命中即取，不再整图转换。
            QPixmap pm;
            const bool cachedHit = (mode == QLatin1String("new") && monitorCache.contains(fn));
            t.restart();
            if (cachedHit) {
                row.monitor.push_back(0);
            } else {
                QImage mon = ready;
                if (target.isValid() && !mon.isNull()) {
                    if (!mon.colorSpace().isValid())
                        mon.setColorSpace(QColorSpace::SRgb);
                    mon.convertToColorSpace(target);
                }
                row.monitor.push_back(t.nsecsElapsed() / 1000);
                // fromImage（未命中：转换后现转 pixmap）
                t.restart();
                pm = QPixmap::fromImage(mon);
                row.fromImage.push_back(t.nsecsElapsed() / 1000);
                if (mode == QLatin1String("new"))
                    monitorCache.insert(fn, pm);
            }
            if (cachedHit) {
                pm = monitorCache.value(fn);
                row.fromImage.push_back(0);   // 命中：pixmap 现成，不再走 fromImage
            }

            row.total.push_back(total.nsecsElapsed() / 1000);
            (void)pm;
        }
        rows.push_back(std::move(row));
    }

    QFile out(outCsv);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        std::fprintf(stderr, "cannot open %s\n", qPrintable(outCsv));
        return 3;
    }
    QTextStream ts(&out);
    ts << "file,rounds,decode_med_ms,decode_p90_ms,srgb_med_ms,srgb_p90_ms,"
          "tonemap_med_ms,tonemap_p90_ms,monitor_med_ms,monitor_p90_ms,"
          "fromimage_med_ms,fromimage_p90_ms,total_med_ms,total_p90_ms\n";
    for (const auto &row : rows) {
        ts << row.file << ',' << row.decode.size() << ','
           << median(row.decode) << ',' << p90(row.decode) << ','
           << median(row.srgb) << ',' << p90(row.srgb) << ','
           << median(row.tonemap) << ',' << p90(row.tonemap) << ','
           << median(row.monitor) << ',' << p90(row.monitor) << ','
           << median(row.fromImage) << ',' << p90(row.fromImage) << ','
           << median(row.total) << ',' << p90(row.total) << '\n';
    }
    ts.flush();
    std::printf("bench mode=%s samples=%d rows=%d -> %s\n", qPrintable(mode), files.size(),
                int(rows.size()), qPrintable(outCsv));
    return 0;
}
