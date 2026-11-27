#include "PanoMetadata.h"
#include "HdrImageLoader.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QSize>
#include <QXmlStreamReader>

#include <cmath>

namespace {

// GPano 命名空间（Google 相片球面元数据）。
const char *kGpanoNs = "http://ns.google.com/photos/1.0/panorama/";
// XMP 标准包在 APP1 里的头（后面紧跟 XML 包）。
const char *kXmpSig = "http://ns.adobe.com/xap/1.0/";
const int kXmpSigLen = 29;   // 含结尾 0x00

// 2:1 判定容差：1%。4K(4096×2048)/8K(8192×4096) 都正好命中。
constexpr double kAspectTol = 0.01;
// 等比例缩图判定容差：宽、高缩放比差不超过 0.5%。
constexpr double kScaleTol = 0.005;

struct GpanoFields {
    QHash<QString, QString> map;
    bool isEmpty() const { return map.isEmpty(); }
    QString str(const char *key) const { return map.value(QLatin1String(key)); }
    double num(const char *key, bool *ok = nullptr) const
    {
        bool good = false;
        const double v = map.value(QLatin1String(key)).toDouble(&good);
        if (ok)
            *ok = good;
        return good ? v : 0.0;
    }
    bool has(const char *key) const { return map.contains(QLatin1String(key)); }
};

bool isFinite(double v)
{
    return std::isfinite(v);
}

bool approximately(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

// 从 XMP 包文本里取 GPano 字段。先用 QXmlStreamReader 按命名空间取（前缀可能
// 是 GPano:/GImage:/自定义），失败再退回「任意前缀 + 已知本地名」的正则式扫描。
GpanoFields parseGpano(const QByteArray &xml)
{
    GpanoFields out;
    const QString ns = QLatin1String(kGpanoNs);

    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement())
            continue;
        for (const QXmlStreamAttribute &a : r.attributes()) {
            const QString local = a.name().toString();
            const bool byNs = a.namespaceUri() == ns;
            const bool byPrefix = a.qualifiedName().startsWith(QLatin1String("GPano:"));
            if (byNs || byPrefix)
                out.map.insert(local, a.value().toString());
        }
    }
    if (!out.isEmpty())
        return out;

    // 兜底：XMP 里偶尔出现重复命名空间声明或未闭合实体，XML 解析器会报错；
    // 这里只认「已知字段名」，不把任意属性都收进来。
    static const char *kKnown[] = {
        "ProjectionType", "UsePanoramaViewer",
        "CroppedAreaImageWidthPixels", "CroppedAreaImageHeightPixels",
        "FullPanoWidthPixels", "FullPanoHeightPixels",
        "CroppedAreaLeftPixels", "CroppedAreaTopPixels",
        "InitialViewHeadingDegrees", "InitialViewPitchDegrees",
        "InitialViewRollDegrees", "InitialViewVerticalFOVDegrees",
        "InitialHorizontalFovDegrees",
    };
    for (const char *name : kKnown) {
        const QByteArray needle = QByteArray(name) + "=\"";
        const int at = xml.indexOf(needle);
        if (at < 0)
            continue;
        const int start = at + needle.size();
        const int end = xml.indexOf('"', start);
        if (end <= start)
            continue;
        out.map.insert(QString::fromLatin1(name),
                       QString::fromUtf8(xml.mid(start, end - start)));
    }
    return out;
}

// 顺序扫 JPEG 段，取第一份标准 XMP 包。不做整文件读：8K 原图动辄十几 MB，
// 识别走的是「打开图片」同一条热路径，只允许按段长 seek。
QByteArray readXmpPacket(QFile &f)
{
    // JPEG 以 SOI 开头：FF D8。
    if (f.size() < 4)
        return {};
    f.seek(0);
    const QByteArray head = f.read(2);
    if (head.size() != 2 || head.at(0) != '\xFF' || head.at(1) != '\xD8')
        return {};

    qint64 pos = 2;
    const qint64 size = f.size();
    while (pos + 4 < size) {
        f.seek(pos);
        const QByteArray mk = f.read(2);
        if (mk.size() != 2 || static_cast<quint8>(mk.at(0)) != 0xFF)
            return {};
        const quint8 marker = static_cast<quint8>(mk.at(1));
        // 填充字节 FF FF…：向后跳过。
        if (marker == 0xFF) {
            pos += 1;
            continue;
        }
        // 无载荷标记。
        if (marker == 0xD8 || marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            pos += 2;
            continue;
        }
        // 图像数据开始：XMP 只可能在它之前。
        if (marker == 0xDA || marker == 0xD9)
            return {};
        const QByteArray lenBytes = f.read(2);
        if (lenBytes.size() != 2)
            return {};
        // 两个字节都必须按无符号取：0x02C2 这种长度低字节 ≥0x80，按 char 取会变成负数，
        // 段长一错，后面所有段都跟着错位（实测就丢过 XMP）。
        const int segLen = (static_cast<int>(static_cast<quint8>(lenBytes.at(0))) << 8)
                           | static_cast<int>(static_cast<quint8>(lenBytes.at(1)));
        if (segLen < 2)
            return {};
        if (marker == 0xE1 && segLen >= 2 + kXmpSigLen + 4) {
            const QByteArray payload = f.read(segLen - 2);
            if (payload.size() == segLen - 2
                && payload.startsWith(QByteArray(kXmpSig, kXmpSigLen))) {
                return payload.mid(kXmpSigLen);
            }
        }
        pos += 2 + segLen;
    }
    return {};
}

double normalizeHeading(double deg)
{
    double h = std::fmod(deg, 360.0);
    if (h < 0.0)
        h += 360.0;
    return h;
}

Pano::Info makeNotFound(const QString &detail)
{
    Pano::Info i;
    i.kind = Pano::Kind::NotFound;
    i.detail = detail;
    return i;
}

} // namespace

QString Pano::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Complete:
        return QStringLiteral("complete");
    case Kind::Suppressed:
        return QStringLiteral("suppressed");
    case Kind::AspectOnly:
        return QStringLiteral("aspect-only");
    case Kind::Unsupported:
        return QStringLiteral("unsupported");
    case Kind::NotFound:
    default:
        return QStringLiteral("none");
    }
}

Pano::Info Pano::probeFile(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile())
        return makeNotFound(QStringLiteral("not a local file"));
    const QString suffix = fi.suffix().toLower();
    const bool hdrLike = suffix == QLatin1String("hdr") || suffix == QLatin1String("rgbe")
                         || suffix == QLatin1String("exr");
    const bool jpegLike = suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
                          || suffix == QLatin1String("jfif");
    if (!hdrLike && !jpegLike) {
        return makeNotFound(QStringLiteral("unsupported suffix: %1").arg(suffix));
    }

    int w = 0, h = 0;
    GpanoFields g;

    if (hdrLike) {
        // HDR/EXR 没有 XMP/GPano：只读文件头真实宽高（不解码像素），走 aspect-only
        // 手动入口；autoEntry 恒 false，因此不会主动弹胶囊。
        int hw = 0, hh = 0;
        QString label;
        bool isExr = false;
        if (!HdrImageLoader::probeHeader(path, hw, hh, label, isExr)) {
            return makeNotFound(QStringLiteral("HDR/EXR 头部解析失败"));
        }
        w = hw;
        h = hh;
    } else {
        QImageReader reader(path);
        const QSize px = reader.size();
        if (!px.isValid() || px.width() <= 0 || px.height() <= 0)
            return makeNotFound(QStringLiteral("cannot read JPEG size"));
        w = px.width();
        h = px.height();

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly))
            return makeNotFound(QStringLiteral("cannot open file"));
        g = parseGpano(readXmpPacket(f));
        f.close();
    }

    // 2:1 实际比例（「普通 2:1 宽图」与「无标签球形图」共用这一条）。
    const double aspect = h > 0 ? static_cast<double>(w) / static_cast<double>(h) : 0.0;
    const bool looksTwoToOne = h > 0 && std::fabs(aspect - 2.0) <= kAspectTol;

    // HDR/EXR：无 GPano 标记，只按实际 2:1 给手动入口；不主动弹胶囊。
    if (hdrLike) {
        Info out;
        if (looksTwoToOne) {
            out.kind = Kind::AspectOnly;
            out.manualEntry = true;
            out.detail = QStringLiteral("HDR/EXR 无 GPano 标记，按 2:1 实际比例识别");
            return out;
        }
        return makeNotFound(QStringLiteral("HDR/EXR 实际 %1×%2 不是 2:1").arg(w).arg(h));
    }

    Info out;

    // 初始视角：只在合法范围内采用 GPano 值，其余用中间视角（heading 0 / pitch 0）。
    auto initView = [&out, &g]() {
        bool ok = false;
        double heading = g.num("InitialViewHeadingDegrees", &ok);
        out.heading = ok && isFinite(heading) ? normalizeHeading(heading) : 0.0;
        ok = false;
        double pitch = g.num("InitialViewPitchDegrees", &ok);
        out.pitch = ok && isFinite(pitch) && pitch >= -90.0 && pitch <= 90.0 ? pitch : 0.0;
        ok = false;
        double roll = g.num("InitialViewRollDegrees", &ok);
        out.roll = ok && isFinite(roll) && roll >= -180.0 && roll <= 180.0 ? roll : 0.0;
        // 垂直 FOV 优先；老图只写 InitialHorizontalFovDegrees 时按 2:1 换算成垂直值。
        ok = false;
        double fov = g.num("InitialViewVerticalFOVDegrees", &ok);
        if (!ok) {
            ok = false;
            double hfov = g.num("InitialHorizontalFovDegrees", &ok);
            if (ok && hfov > 0.0 && hfov < 179.0) {
                const double v = 2.0 * std::atan(std::tan(hfov * M_PI / 360.0) / 2.0);
                fov = v * 180.0 / M_PI;
                ok = true;
            }
        }
        if (ok && isFinite(fov) && fov >= 10.0 && fov <= 120.0)
            out.fov = fov;
    };

    const QString projection = g.str("ProjectionType").trimmed();
    if (!projection.isEmpty() && projection.compare(QLatin1String("equirectangular"),
                                                   Qt::CaseInsensitive) != 0) {
        out.kind = Kind::Unsupported;
        out.detail = QStringLiteral("projection=%1（非等距柱状）").arg(projection);
        return out;
    }

    const bool hasCrop = g.has("CroppedAreaImageWidthPixels")
                         && g.has("CroppedAreaImageHeightPixels");
    const bool hasFull = g.has("FullPanoWidthPixels") && g.has("FullPanoHeightPixels");
    bool okCropW = false, okCropH = false, okFullW = false, okFullH = false;
    bool okLeft = false, okTop = false;
    const double cropW = g.num("CroppedAreaImageWidthPixels", &okCropW);
    const double cropH = g.num("CroppedAreaImageHeightPixels", &okCropH);
    const double fullW = g.num("FullPanoWidthPixels", &okFullW);
    const double fullH = g.num("FullPanoHeightPixels", &okFullH);
    const double left = g.num("CroppedAreaLeftPixels", &okLeft);
    const double top = g.num("CroppedAreaTopPixels", &okTop);
    const bool tagsUsable = hasCrop && hasFull && okCropW && okCropH && okFullW && okFullH
                            && cropW > 0.0 && cropH > 0.0 && fullW > 0.0 && fullH > 0.0
                            && okLeft && okTop;

    if (!g.isEmpty() && !tagsUsable) {
        // 有 GPano 但关键几何字段缺失 → 标记不可用，退到手动规则（派单：
        // 「缺失或冲突的标记退到手动规则」）。
        if (looksTwoToOne) {
            out.kind = Kind::AspectOnly;
            out.manualEntry = true;
            out.detail = QStringLiteral("GPano 几何字段缺失（crop/full 不全），按 2:1 退回手动");
            initView();
            return out;
        }
        return makeNotFound(QStringLiteral("GPano 几何字段缺失，且图片不是 2:1"));
    }

    if (tagsUsable) {
        // 完整球形：裁切区域覆盖全图、偏移为零、全图本身 2:1。
        const bool coversFullPano = approximately(cropW, fullW, 0.5)
                                    && approximately(cropH, fullH, 0.5);
        const bool atOrigin = approximately(left, 0.0, 0.5) && approximately(top, 0.0, 0.5);
        const bool panoTwoToOne = std::fabs(fullW / fullH - 2.0) <= kAspectTol;

        if (!coversFullPano || !atOrigin) {
            out.kind = Kind::Unsupported;
            out.detail = QStringLiteral("局部裁切 %1×%2 @(%3,%4) / 全图 %5×%6")
                             .arg(QString::number(cropW, 'g', 8),
                                  QString::number(cropH, 'g', 8),
                                  QString::number(left, 'g', 8),
                                  QString::number(top, 'g', 8),
                                  QString::number(fullW, 'g', 8),
                                  QString::number(fullH, 'g', 8));
            return out;
        }
        if (!panoTwoToOne) {
            out.kind = Kind::Unsupported;
            out.detail = QStringLiteral("全图 %1×%2 不是 2:1").arg(fullW).arg(fullH);
            return out;
        }

        // 标记尺寸与图片实际像素：完全一致，或等比例缩图（宽/高缩放比一致）。
        const bool exact = static_cast<int>(std::lround(cropW)) == w
                           && static_cast<int>(std::lround(cropH)) == h;
        const double sx = cropW > 0.0 ? w / cropW : 0.0;
        const double sy = cropH > 0.0 ? h / cropH : 0.0;
        const bool uniformScale = sx > 0.049 && sx < 20.0
                                  && std::fabs(sx - sy) <= kScaleTol;
        if (!exact && !uniformScale) {
            out.kind = looksTwoToOne ? Kind::AspectOnly : Kind::NotFound;
            out.manualEntry = looksTwoToOne;
            out.detail = QStringLiteral("标记 %1×%2 与实际 %3×%4 不成等比例")
                             .arg(QString::number(cropW, 'g', 8),
                                  QString::number(cropH, 'g', 8))
                             .arg(w)
                             .arg(h);
            if (out.manualEntry)
                initView();
            return out;
        }

        // 几何成立。UsePanoramaViewer=false 是「明确不要提示」，不是「不能看」：
        // 主窗不主动给入口，右键手动入口保留。
        const QString usePano = g.str("UsePanoramaViewer").trimmed().toLower();
        const bool wantPrompt = usePano.isEmpty() || usePano == QLatin1String("true")
                                || usePano == QLatin1String("1");
        out.kind = wantPrompt ? Kind::Complete : Kind::Suppressed;
        out.autoEntry = wantPrompt;
        out.manualEntry = true;
        out.detail = QStringLiteral("完整等距柱状 %1×%2%3")
                         .arg(cropW).arg(cropH)
                         .arg(exact ? QStringLiteral("")
                                    : QStringLiteral("（标记原尺寸，实际 %1×%2 等比例缩图）")
                                          .arg(w).arg(h));
        initView();
        return out;
    }

    // 完全没有任何 GPano：只有实际 2:1 才给手动入口。
    if (looksTwoToOne) {
        out.kind = Kind::AspectOnly;
        out.manualEntry = true;
        out.detail = QStringLiteral("无 GPano 标记，按 2:1 实际比例识别");
        return out;
    }
    return makeNotFound(QStringLiteral("无 GPano 标记，实际 %1×%2 不是 2:1").arg(w).arg(h));
}
