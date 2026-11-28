#include "HdrPreview.h"

#include <algorithm>
#include <cmath>

namespace HdrPreview {

namespace {

// ACES 近似（Narkowicz 2015）：定义域 [0,∞)，输出 [0,1)。
constexpr double kA = 2.51, kB = 0.03, kC = 2.43, kD = 0.59, kE = 0.14;

double aces(double x)
{
    const double y = (x * (kA * x + kB)) / (x * (kC * x + kD) + kE);
    return std::clamp(y, 0.0, 1.0);
}

// aces(1.0)：归一化用的白点落点。除以它，linear=white 才真正落在 255，
// 否则整幅图会比既有 16bit 通路灰一档。
double acesWhite()
{
    static const double v = aces(1.0);
    return v;
}

double srgbEncode(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return (v <= 0.0031308) ? (v * 12.92) : (1.055 * std::pow(v, 1.0 / 2.4) - 0.055);
}

// 亮度下限：低于它的视作纯黑。3D 渲染天空穹顶下半球常是精确 0，
// 不排除会把这些像素算进分位数，把白点整体拉低、暗部炸亮。
constexpr float kBlackLuma = 1e-6f;
// 白点夹取：全黑图/坏数据兜底，避免除小数。
constexpr float kMinWhite = 1e-3f;
constexpr float kMaxWhite = 1e9f;

} // namespace

void Meter::addLuma(float luma)
{
    if (!std::isfinite(luma) || luma <= kBlackLuma) {
        return;
    }
    if (int(m_luma.size()) >= kCapacity) {
        // 隔一丢一：保留一半样本继续累积，等价于均匀稀疏化，内存恒定。
        for (size_t i = 0, w = 0; i < m_luma.size(); i += 2, ++w) {
            m_luma[w] = m_luma[i];
        }
        m_luma.resize(size_t((m_luma.size() + 1) / 2));
    }
    m_luma.push_back(luma);
}

void Meter::add(float r, float g, float b)
{
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b)) {
        return;
    }
    // rec709：Y = 0.2126R + 0.7152G + 0.0722B
    addLuma(0.2126f * r + 0.7152f * g + 0.0722f * b);
}

float Meter::whitePoint() const
{
    if (m_luma.empty()) {
        return 1.0f;
    }
    std::vector<float> sorted = m_luma;
    std::sort(sorted.begin(), sorted.end());
    int idx = int(std::ceil(sorted.size() * 0.995)) - 1;
    idx = std::clamp(idx, 0, int(sorted.size()) - 1);
    const float white = sorted[size_t(idx)];
    if (!std::isfinite(white) || white <= kBlackLuma) {
        return 1.0f;
    }
    return std::clamp(white, kMinWhite, kMaxWhite);
}

Curve::Curve(float whitePoint)
{
    m_white = (std::isfinite(whitePoint) && whitePoint > kBlackLuma)
                  ? std::clamp(whitePoint, kMinWhite, kMaxWhite)
                  : 1.0f;
    m_invWhite = 1.0f / m_white;
    const double whiteOut = acesWhite();
    for (int i = 0; i <= kSize; ++i) {
        // sqrt 分布：t = i/N，x = t²·kMaxX
        const double t = double(i) / double(kSize);
        const double x = t * t * double(kMaxX);
        const double v = aces(x) / whiteOut;
        m_lut[i] = static_cast<unsigned char>(
            std::clamp(int(std::lround(srgbEncode(v) * 255.0)), 0, 255));
    }
}

unsigned char Curve::operator()(float linear) const
{
    if (!std::isfinite(linear) || linear <= 0.0f) {
        return 0;
    }
    const float x = linear * m_invWhite;
    if (x >= kMaxX) {
        return 255;
    }
    const float f = std::sqrt(x / kMaxX) * float(kSize);
    int i = int(f);
    i = std::clamp(i, 0, kSize - 1);
    const float frac = f - float(i);
    const float v = float(m_lut[i])
                    + (float(m_lut[i + 1]) - float(m_lut[i])) * frac;
    return static_cast<unsigned char>(std::clamp(int(std::lround(v)), 0, 255));
}

} // namespace HdrPreview
