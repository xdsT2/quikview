#include "PanoGlobe.h"

#include "BoxDownsampler.h"
#include "ExrStream.h"
#include "HdrError.h"
#include "HdrRadiance.h"
#include "RenderSdr.h"

#include <QImageReader>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>
#include <vector>

namespace {

// 半分辨率交互帧的触发阈值：拖动中每帧都按 0.5 渲染，松手后 120ms 补全分辨率。
constexpr int kSettleMs = 120;
constexpr double kInteractiveScale = 0.5;
constexpr double kMinFov = 20.0;
constexpr double kMaxFov = 110.0;

// 浮点纹理上限：≤4K 原生保留；8K/16K 盒式降到 4096×2048（RGB float ≈ 96 MiB）。
// QUIKVIEW_PANO_TEXW/TEXH 仅作内存受限场景复测开关，不改变产品默认。
constexpr int kDefaultTexW = 4096;
constexpr int kDefaultTexH = 2048;

constexpr double kEvMin = -5.0;
constexpr double kEvMax = 5.0;
constexpr double kEvStep = 0.5;

double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

double wrapHeading(double deg)
{
    double h = std::fmod(deg, 360.0);
    if (h < 0.0)
        h += 360.0;
    return h;
}

int texCapW()
{
    bool ok = false;
    const int v = qEnvironmentVariable("QUIKVIEW_PANO_TEXW").toInt(&ok);
    return ok && v > 0 ? v : kDefaultTexW;
}

int texCapH()
{
    bool ok = false;
    const int v = qEnvironmentVariable("QUIKVIEW_PANO_TEXH").toInt(&ok);
    return ok && v > 0 ? v : kDefaultTexH;
}

// 目标纹理尺寸：源图在上限内原生保留；超出则按 2:1 等比压到上限内，只缩不放。
QSize targetSize(int srcW, int srcH)
{
    const int capW = texCapW();
    const int capH = texCapH();
    if (srcW <= capW && srcH <= capH)
        return QSize(srcW, srcH);
    const double scale = std::min(static_cast<double>(capW) / srcW,
                                  static_cast<double>(capH) / srcH);
    return QSize(qMax(1, static_cast<int>(std::lround(srcW * scale))),
                 qMax(1, static_cast<int>(std::lround(srcH * scale))));
}

// sRGB 编码 8bit LUT：线性 [0,1] → 8bit，1024 级。表项由内核同一
// hdrprobe::srgbEncode 公式预计算，与平面链路 HdrImageLoader 同一口径。
class SrgbLut {
public:
    SrgbLut()
    {
        for (int i = 0; i < 1024; ++i) {
            float v = static_cast<float>(i) / 1023.0f;
            v = hdrprobe::srgbEncode(v);
            v = std::min(1.0f, std::max(0.0f, v));
            m_lut[i] = static_cast<uint8_t>(std::lround(v * 255.0f));
        }
    }
    uint8_t operator()(float v) const
    {
        if (v <= 0.0f) return 0;
        if (v >= 1.0f) return 255;
        const int idx = std::min(1023, static_cast<int>(v * 1023.0f + 0.5f));
        return m_lut[idx];
    }
private:
    uint8_t m_lut[1024]{};
};

const SrgbLut &srgbLut()
{
    static const SrgbLut lut;
    return lut;
}

// 内核错误 → 上层文案与大类键。类别前缀（「文件损坏：…」/「格式不支持：…」）
// 由内核 ZhText 给出；上层不做字符串匹配，只按 Corrupt/Unsupported/Io 分类。
void fillFailure(QString *error, QString *category, const hdrprobe::HdrError &err)
{
    const QString base = QString::fromUtf8(hdrprobe::HdrError::ZhText(err.code));
    const QString detail = QString::fromUtf8(err.message);
    if (error) {
        if (detail.isEmpty())
            *error = base;
        else if (detail.startsWith(base))
            *error = detail;
        else
            *error = base + QStringLiteral("；") + detail;
    }
    if (category) {
        switch (err.category) {
        case hdrprobe::HdrCategory::Corrupt:    *category = QStringLiteral("Corrupt"); break;
        case hdrprobe::HdrCategory::Unsupported: *category = QStringLiteral("Unsupported"); break;
        default:                                 *category = QStringLiteral("Io"); break;
        }
    }
}

bool isHdrSuffix(const QString &s) { return s == QLatin1String("hdr") || s == QLatin1String("rgbe"); }
bool isExrSuffix(const QString &s) { return s == QLatin1String("exr"); }

} // namespace

PanoGlobe::PanoGlobe(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    m_settleTimer = new QTimer(this);
    m_settleTimer->setSingleShot(true);
    m_settleTimer->setInterval(kSettleMs);
    connect(m_settleTimer, &QTimer::timeout, this, [this]() {
        if (hasImage())
            renderFrame(1.0);
    });
}

bool PanoGlobe::load(const QString &path, QString *error, QString *errorCategory)
{
    if (error) error->clear();
    if (errorCategory) errorCategory->clear();

    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool wantExr = isExrSuffix(suffix);
    const bool wantHdr = isHdrSuffix(suffix);

    // ---- 既有 JPEG/PNG 路径：QImageReader → RGB32，行为与基线一致，不经曝光/映射 ----
    if (!wantExr && !wantHdr) {
        QImageReader reader(path);
        reader.setDecideFormatFromContent(false);
        QImage img = reader.read();
        if (img.isNull()) {
            if (error)
                *error = reader.errorString();
            if (errorCategory)
                *errorCategory = QStringLiteral("Io");
            return false;
        }
        if (img.format() != QImage::Format_RGB32)
            img = img.convertToFormat(QImage::Format_RGB32);
        m_src = img;
        m_tex.clear();
        m_texW = m_texH = 0;
        m_nativeW = img.width();
        m_nativeH = img.height();
        m_isFloat = false;
        m_frame = QImage();
        return true;
    }

    // ---- HDR / EXR：流式逐行解码 + 盒式降采样，落成驻留线性浮点纹理 ----
    const QByteArray utf8 = path.toUtf8();
    hdrprobe::HdrError err;
    int srcW = 0, srcH = 0;
    QSize dst;
    std::vector<float> tex;

    if (wantExr) {
        hdrprobe::ExrStream exr;
        err = exr.open(utf8.constData());
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(error, errorCategory, err);
            return false;
        }
        srcW = exr.width();
        srcH = exr.height();
        if (srcW <= 0 || srcH <= 0) {
            err = hdrprobe::HdrError{hdrprobe::HdrStatus::CorruptHeader,
                                      hdrprobe::HdrCategory::Corrupt, "非法图像尺寸"};
            fillFailure(error, errorCategory, err);
            return false;
        }
        dst = targetSize(srcW, srcH);
        hdrprobe::BoxDownsampler ds(srcW, srcH, dst.width(), dst.height());
        std::vector<float> row(static_cast<size_t>(srcW) * 3);
        int srcY = 0;
        while (exr.nextRow(row.data(), srcY, err))
            ds.pushRow(srcY, row.data());
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(error, errorCategory, err);
            return false;
        }
        ds.finish();
        tex = ds.releaseFrame();
    } else {
        hdrprobe::HdrRadiance hdr;
        err = hdr.open(utf8.constData());
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(error, errorCategory, err);
            return false;
        }
        srcW = hdr.width();
        srcH = hdr.height();
        if (srcW <= 0 || srcH <= 0) {
            err = hdrprobe::HdrError{hdrprobe::HdrStatus::CorruptHeader,
                                      hdrprobe::HdrCategory::Corrupt, "非法图像尺寸"};
            fillFailure(error, errorCategory, err);
            return false;
        }
        dst = targetSize(srcW, srcH);
        hdrprobe::BoxDownsampler ds(srcW, srcH, dst.width(), dst.height());
        std::vector<float> row(static_cast<size_t>(srcW) * 3);
        int srcY = 0;
        while (hdr.nextRow(row.data(), srcY, err))
            ds.pushRow(srcY, row.data());
        if (err.code != hdrprobe::HdrStatus::Ok) {
            fillFailure(error, errorCategory, err);
            return false;
        }
        ds.finish();
        tex = ds.releaseFrame();
    }

    if (tex.empty()) {
        if (error) *error = QStringLiteral("解码结果为空");
        if (errorCategory) *errorCategory = QStringLiteral("Corrupt");
        return false;
    }

    m_tex = std::move(tex);
    m_texW = dst.width();
    m_texH = dst.height();
    m_nativeW = srcW;
    m_nativeH = srcH;
    m_isFloat = true;
    m_src = QImage();
    m_frame = QImage();
    return true;
}

QSize PanoGlobe::sourceSize() const
{
    if (m_isFloat)
        return QSize(m_texW, m_texH);
    return m_src.size();
}

void PanoGlobe::setInitialView(double heading, double pitch, double fov)
{
    m_homeHeading = wrapHeading(heading);
    m_homePitch = clampd(pitch, -89.0, 89.0);
    m_homeFov = clampd(fov > 0.0 ? fov : 75.0, kMinFov, kMaxFov);
    resetView();
}

void PanoGlobe::resetView()
{
    m_heading = m_homeHeading;
    m_pitch = m_homePitch;
    m_fov = m_homeFov;
    m_settleTimer->stop();
    if (hasImage())
        renderFrame(1.0);
    emit viewChanged();
}

void PanoGlobe::setEv(double ev)
{
    const double next = clampd(ev, kEvMin, kEvMax);
    if (m_isFloat) {
        m_ev = next;
        if (hasImage())
            renderFrame(1.0);
    } else {
        // SDR 图不经过曝光链路：保持 0，避免对已编码 sRGB 二次映射。
        m_ev = 0.0;
    }
    emit evChanged();
}

void PanoGlobe::stepEv(double delta)
{
    setEv(m_ev + delta);
}

void PanoGlobe::resetEv()
{
    setEv(0.0);
}

void PanoGlobe::projectRay(double nx, double ny,
                           double cosP, double sinP, double cosY, double sinY,
                           double &u, double &v) const
{
    // 视线（相机空间，+z 为屏幕正中）
    const double y1 = ny * cosP + 1.0 * sinP;
    const double z1 = -ny * sinP + 1.0 * cosP;
    const double x2 = nx * cosY + z1 * sinY;
    const double z2 = -nx * sinY + z1 * cosY;
    const double y2 = y1;
    const double lon = std::atan2(x2, z2);
    const double lat = std::atan2(y2, std::sqrt(nx * nx + z1 * z1));
    u = 0.5 + lon / (2.0 * M_PI);
    v = 0.5 - lat / M_PI;
}

void PanoGlobe::sampleTex(double u, double v, float rgb[3]) const
{
    const int sw = m_texW;
    const int sh = m_texH;
    const double fx = u * sw - 0.5;
    const double fy = clampd(v * sh - 0.5, 0.0, static_cast<double>(sh - 1));
    int x0 = static_cast<int>(std::floor(fx));
    const double tx = fx - x0;
    x0 %= sw;
    if (x0 < 0)
        x0 += sw;
    const int x1 = (x0 + 1) % sw;
    int y0 = static_cast<int>(std::floor(fy));
    if (y0 < 0)
        y0 = 0;
    if (y0 > sh - 1)
        y0 = sh - 1;
    const int y1s = qMin(y0 + 1, sh - 1);
    const double ty = fy - y0;

    const float *a = m_tex.data() + (static_cast<size_t>(y0) * sw + x0) * 3;
    const float *b = m_tex.data() + (static_cast<size_t>(y0) * sw + x1) * 3;
    const float *c = m_tex.data() + (static_cast<size_t>(y1s) * sw + x0) * 3;
    const float *d = m_tex.data() + (static_cast<size_t>(y1s) * sw + x1) * 3;
    const double w00 = (1.0 - tx) * (1.0 - ty);
    const double w10 = tx * (1.0 - ty);
    const double w01 = (1.0 - tx) * ty;
    const double w11 = tx * ty;
    for (int k = 0; k < 3; ++k)
        rgb[k] = static_cast<float>(a[k] * w00 + b[k] * w10 + c[k] * w01 + d[k] * w11);
}

void PanoGlobe::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(QStringLiteral("#0c0d0f")));
    if (m_frame.isNull()) {
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(rect(), m_frame);
}

void PanoGlobe::resizeEvent(QResizeEvent *)
{
    if (hasImage())
        renderFrame(1.0);
}

// 等距柱状 → 透视：屏幕上每个像素反投一条视线，落到球面求经纬度后双线性取样。
// 纬度取视线与水平面的夹角；未归一化视线的纵分量不能直接求反正弦。
//
// 色彩管线顺序（硬约束）：
//   球面投影求经纬度 → 双线性采样线性浮点 → 曝光 ×2^EV → Reinhard v/(1+v)
//   → sRGB 编码 → 8bit。曝光只施加在线性数据上，不在 sRGB 编码之后；
//   线性纹理不在解码阶段预烘焙曝光（否则调 EV 要重新解码）。
void PanoGlobe::renderFrame(double scale)
{
    if (!hasImage() || width() <= 0 || height() <= 0) {
        m_frame = QImage();
        update();
        return;
    }
    const double dpr = devicePixelRatioF() > 0.0 ? devicePixelRatioF() : 1.0;
    const int W = qMax(1, static_cast<int>(std::lround(width() * dpr * scale)));
    const int H = qMax(1, static_cast<int>(std::lround(height() * dpr * scale)));
    if (m_frame.size() != QSize(W, H) || m_frame.format() != QImage::Format_RGB32)
        m_frame = QImage(W, H, QImage::Format_RGB32);
    m_frameScale = scale;

    const int sw = m_isFloat ? m_texW : m_src.width();
    const int sh = m_isFloat ? m_texH : m_src.height();
    const int srcStride = static_cast<int>(m_src.bytesPerLine() / 4);
    const QRgb *srcBits = reinterpret_cast<const QRgb *>(m_src.constBits());

    const double tanY = std::tan(clampd(m_fov, kMinFov, kMaxFov) * M_PI / 360.0);
    const double tanX = tanY * static_cast<double>(W) / static_cast<double>(H);
    const double pitchRad = m_pitch * M_PI / 180.0;
    const double yawRad = m_heading * M_PI / 180.0;
    const double cosP = std::cos(pitchRad);
    const double sinP = std::sin(pitchRad);
    const double cosY = std::cos(yawRad);
    const double sinY = std::sin(yawRad);

    const float gain = m_isFloat ? std::pow(2.0f, static_cast<float>(m_ev)) : 1.0f;
    const SrgbLut &srgb = srgbLut();

    for (int y = 0; y < H; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(m_frame.scanLine(y));
        const double ny = (1.0 - 2.0 * (y + 0.5) / H) * tanY;
        for (int x = 0; x < W; ++x) {
            const double nx = (2.0 * (x + 0.5) / W - 1.0) * tanX;
            double u = 0.0, v = 0.0;
            projectRay(nx, ny, cosP, sinP, cosY, sinY, u, v);

            int r = 0, g = 0, bl = 0;
            if (m_isFloat) {
                float lin[3] = {0.0f, 0.0f, 0.0f};
                sampleTex(u, v, lin);
                for (int k = 0; k < 3; ++k) {
                    float val = lin[k] * gain;          // 曝光 ×2^EV（线性域）
                    if (val < 0.0f) val = 0.0f;
                    val = val / (1.0f + val);           // Reinhard（与基线口径一致）
                    const uint8_t c = srgb(val);         // sRGB 编码（映射之后）
                    if (k == 0) r = c; else if (k == 1) g = c; else bl = c;
                }
            } else {
                // 既有 8bit SDR 路径：双线性直接在 QRgb 上加权，不做曝光/色调映射。
                const double fx = u * sw - 0.5;
                const double fy = clampd(v * sh - 0.5, 0.0, static_cast<double>(sh - 1));
                int x0 = static_cast<int>(std::floor(fx));
                const double tx = fx - x0;
                x0 %= sw;
                if (x0 < 0)
                    x0 += sw;
                const int x1 = (x0 + 1) % sw;
                int y0 = static_cast<int>(std::floor(fy));
                if (y0 < 0)
                    y0 = 0;
                if (y0 > sh - 1)
                    y0 = sh - 1;
                const int y1s = qMin(y0 + 1, sh - 1);
                const double ty = fy - y0;

                const QRgb a = srcBits[y0 * srcStride + x0];
                const QRgb b = srcBits[y0 * srcStride + x1];
                const QRgb c = srcBits[y1s * srcStride + x0];
                const QRgb d = srcBits[y1s * srcStride + x1];
                const double w00 = (1.0 - tx) * (1.0 - ty);
                const double w10 = tx * (1.0 - ty);
                const double w01 = (1.0 - tx) * ty;
                const double w11 = tx * ty;
                r = static_cast<int>(qRed(a) * w00 + qRed(b) * w10 + qRed(c) * w01 + qRed(d) * w11 + 0.5);
                g = static_cast<int>(qGreen(a) * w00 + qGreen(b) * w10 + qGreen(c) * w01 + qGreen(d) * w11 + 0.5);
                bl = static_cast<int>(qBlue(a) * w00 + qBlue(b) * w10 + qBlue(c) * w01 + qBlue(d) * w11 + 0.5);
            }
            line[x] = qRgb(r, g, bl);
        }
    }
    update();
}

bool PanoGlobe::probeCenterPixel(float lin[3], int srgb8[3]) const
{
    if (!m_isFloat || m_tex.empty() || width() <= 0 || height() <= 0)
        return false;
    const double pitchRad = m_pitch * M_PI / 180.0;
    const double yawRad = m_heading * M_PI / 180.0;
    double u = 0.0, v = 0.0;
    projectRay(0.0, 0.0, std::cos(pitchRad), std::sin(pitchRad),
               std::cos(yawRad), std::sin(yawRad), u, v);
    sampleTex(u, v, lin);
    const float gain = std::pow(2.0f, static_cast<float>(m_ev));
    const SrgbLut &srgb = srgbLut();
    for (int k = 0; k < 3; ++k) {
        float val = lin[k] * gain;
        if (val < 0.0f) val = 0.0f;
        val = val / (1.0f + val);
        srgb8[k] = srgb(val);
    }
    return true;
}

void PanoGlobe::scheduleFullQuality()
{
    m_settleTimer->start();
}

double PanoGlobe::benchFrameMs(int frames, double scale)
{
    if (!hasImage() || frames <= 0)
        return 0.0;
    QElapsedTimer clock;
    clock.start();
    for (int i = 0; i < frames; ++i) {
        // 每帧挪一点视角，避免编译器把重复渲染优化成同一张缓存（同时更接近真实拖动）。
        m_heading = wrapHeading(m_heading + 1.0);
        renderFrame(scale);
        QCoreApplication::processEvents();
    }
    return static_cast<double>(clock.elapsed()) / frames;
}

void PanoGlobe::clampPitch()
{
    m_pitch = clampd(m_pitch, -89.0, 89.0);
}

void PanoGlobe::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !hasImage()) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_dragging = true;
    m_dragFrom = event->position().toPoint();
    m_dragHeading = m_heading;
    m_dragPitch = m_pitch;
    setCursor(Qt::ClosedHandCursor);
    m_settleTimer->stop();
}

void PanoGlobe::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !hasImage()) {
        QWidget::mouseMoveEvent(event);
        return;
    }
    const QPoint d = event->position().toPoint() - m_dragFrom;
    const double aspect = height() > 0 ? static_cast<double>(width()) / height() : 1.0;
    // 每像素对应多少度：让「画面跟手」而不是按固定灵敏度猜。
    const double fovX = 2.0 * std::atan(std::tan(m_fov * M_PI / 360.0) * aspect) * 180.0 / M_PI;
    const double degPerPxX = fovX / qMax(1, width());
    const double degPerPxY = m_fov / qMax(1, height());
    m_heading = wrapHeading(m_dragHeading - d.x() * degPerPxX);
    m_pitch = clampd(m_dragPitch + d.y() * degPerPxY, -89.0, 89.0);
    renderFrame(kInteractiveScale);
}

void PanoGlobe::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }
    m_dragging = false;
    setCursor(Qt::OpenHandCursor);
    scheduleFullQuality();
    emit viewChanged();
}

void PanoGlobe::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        resetView();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PanoGlobe::wheelEvent(QWheelEvent *event)
{
    if (!hasImage()) {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = event->angleDelta().y();
    if (steps == 0) {
        return;
    }
    // Shift/Ctrl + 滚轮 = 调曝光（±0.5 EV）；普通滚轮 = 调整垂直视野。
    const bool evModifier = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
    if (evModifier) {
        stepEv(steps > 0 ? kEvStep : -kEvStep);
        event->accept();
        return;
    }
    // 上滚拉近（视野变小），每档 3°，夹在 20°~110°。
    const double nextFov = clampd(m_fov - (steps / 120.0) * 3.0, kMinFov, kMaxFov);
    event->accept();
    if (nextFov == m_fov)
        return;
    m_fov = nextFov;
    renderFrame(kInteractiveScale);
    scheduleFullQuality();
    emit viewChanged();
}
