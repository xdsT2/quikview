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
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <vector>

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

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);

    QString samplesDir, outCsv, mode = QStringLiteral("legacy"), prefix;
    int rounds = 5;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLatin1(argv[i]);
        if (a == "--samples" && i + 1 < argc) samplesDir = QString::fromLatin1(argv[++i]);
        else if (a == "--out" && i + 1 < argc) outCsv = QString::fromLatin1(argv[++i]);
        else if (a == "--mode" && i + 1 < argc) mode = QString::fromLatin1(argv[++i]);
        else if (a == "--rounds" && i + 1 < argc) rounds = QString::fromLatin1(argv[++i]).toInt();
        else if (a == "--prefix" && i + 1 < argc) prefix = QString::fromLatin1(argv[++i]);
    }
    if (samplesDir.isEmpty() || outCsv.isEmpty()) {
        std::fprintf(stderr, "usage: display-bench --samples <dir> --out <csv> [--mode legacy|new] [--rounds N]\n");
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
