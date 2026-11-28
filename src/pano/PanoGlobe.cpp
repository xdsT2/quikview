#include "PanoGlobe.h"

#include "HdrExrDecode.h"
#include "MappedFile.h"

#include <QFileInfo>
#include <QImageReader>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>

#include <cmath>

namespace {

// 半分辨率交互帧的触发阈值：拖动中每帧都按 0.5 渲染，松手后 120ms 补全分辨率。
constexpr int kSettleMs = 120;
constexpr double kInteractiveScale = 0.5;
constexpr double kMinFov = 20.0;
constexpr double kMaxFov = 110.0;

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
        if (!m_src.isNull())
            renderFrame(1.0);
    });
}

bool PanoGlobe::load(const QString &path, QString *error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    QImage img;
    if (HdrExrDecode::isHdrSuffix(suffix) || HdrExrDecode::isExrSuffix(suffix)) {
        // HDR/EXR 没有对应的 Qt 图像插件：走主程序同一套解码与 SDR 预览曲线
        // （I3-2），保证球面与平面看到的是同一次色调映射结果。大文件走内存
        // 映射，避免再复制一整份（8K HDR 约 100 MB）。
        MappedFile file;
        QString err;
        LoadErrorCategory cat = LoadErrorCategory::None;
        if (!file.open(path, 256LL * 1024 * 1024, &err, &cat)) {
            if (error)
                *error = err;
            return false;
        }
        img = HdrExrDecode::isExrSuffix(suffix)
                  ? HdrExrDecode::decodeExr(file.bytes(), &err, &cat)
                  : HdrExrDecode::decodeRadiance(file.bytes(), &err, &cat);
        if (img.isNull()) {
            if (error)
                *error = err.isEmpty() ? QStringLiteral("无法解码该 HDR/EXR 文件") : err;
            return false;
        }
    } else {
        QImageReader reader(path);
        reader.setDecideFormatFromContent(false);
        img = reader.read();
        if (img.isNull()) {
            if (error)
                *error = reader.errorString();
            return false;
        }
    }
    // 统一成 RGB32：渲染循环只处理 QRgb，避免每像素判格式。
    if (img.format() != QImage::Format_RGB32)
        img = img.convertToFormat(QImage::Format_RGB32);
    // 源图按 2:1 视作 360×180；不是 2:1 的图按「尝试以 360° 查看」也照常绕满一圈，
    // 由用户自己判断值不值得看（派单只对自动入口设门槛）。
    m_src = img;
    m_frame = QImage();
    return true;
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
    if (!m_src.isNull())
        renderFrame(1.0);
    emit viewChanged();
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
    if (!m_src.isNull())
        renderFrame(1.0);
}

// 等距柱状 → 透视：屏幕上每个像素反投一条视线，落到球面求经纬度后双线性取样。
// 纬度取视线与水平面的夹角；未归一化视线的纵分量不能直接求反正弦。
void PanoGlobe::renderFrame(double scale)
{
    if (m_src.isNull() || width() <= 0 || height() <= 0) {
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

    const int sw = m_src.width();
    const int sh = m_src.height();
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

    for (int y = 0; y < H; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(m_frame.scanLine(y));
        const double ny = (1.0 - 2.0 * (y + 0.5) / H) * tanY;
        for (int x = 0; x < W; ++x) {
            const double nx = (2.0 * (x + 0.5) / W - 1.0) * tanX;
            // 视线（相机空间，+z 为屏幕正中）
            const double cx = nx;
            const double cy0 = ny;
            const double cz0 = 1.0;
            // pitch：绕 X 轴
            const double y1 = cy0 * cosP + cz0 * sinP;
            const double z1 = -cy0 * sinP + cz0 * cosP;
            // yaw：绕 Y 轴
            const double x2 = cx * cosY + z1 * sinY;
            const double z2 = -cx * sinY + z1 * cosY;
            const double y2 = y1;
            const double lon = std::atan2(x2, z2);
            const double lat = std::atan2(y2, std::sqrt(cx * cx + z1 * z1));
            const double u = 0.5 + lon / (2.0 * M_PI);
            const double v = 0.5 - lat / M_PI;

            // 双线性（横向环绕接缝、纵向夹到极线）
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
            const int r = static_cast<int>(qRed(a) * w00 + qRed(b) * w10
                                           + qRed(c) * w01 + qRed(d) * w11 + 0.5);
            const int g = static_cast<int>(qGreen(a) * w00 + qGreen(b) * w10
                                           + qGreen(c) * w01 + qGreen(d) * w11 + 0.5);
            const int bl = static_cast<int>(qBlue(a) * w00 + qBlue(b) * w10
                                            + qBlue(c) * w01 + qBlue(d) * w11 + 0.5);
            line[x] = qRgb(r, g, bl);
        }
    }
    update();
}

void PanoGlobe::scheduleFullQuality()
{
    m_settleTimer->start();
}

double PanoGlobe::benchFrameMs(int frames, double scale)
{
    if (m_src.isNull() || frames <= 0)
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
    if (event->button() != Qt::LeftButton || m_src.isNull()) {
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
    if (!m_dragging || m_src.isNull()) {
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
    if (m_src.isNull()) {
        QWidget::wheelEvent(event);
        return;
    }
    const int steps = event->angleDelta().y();
    if (steps == 0) {
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
