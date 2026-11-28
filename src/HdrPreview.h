#pragma once

#include <vector>

// HDR（Radiance .hdr / OpenEXR .exr）线性场景光的 SDR 预览曲线。
//
// 背景（BUG-20261128-03 / 需求池 I3-2）：旧路径把每个通道直接送进一条白点锚在
// 1.0 的 ACES 曲线再截断。天空这类绝对亮度 3~30 的 HDR，整片会压成 250+ 的死白，
// 云层细节全丢；而曝光归一化若用整幅平均值，又会因为天空穹顶下半球近全黑被拉偏。
//
// 这里的口径与项目既有的 16bit 通路（ToneMap::buildCurve16）保持一致：
//   · 白点 = 有效非黑像素亮度的 99.5% 分位（纯黑像素不参与统计）；
//   · 逐通道 ACES 近似肩部压缩，并把 aces(1.0) 归一化回 255，避免整体发灰；
//   · 输出 sRGB 编码的 8bit。
namespace HdrPreview {

// 线性亮度计量。只收「有限且亮度 > 0」的样本，样本数超上限后隔一丢一，
// 因此内存占用与图片尺寸无关（上限约 13 万个 float）。
class Meter
{
public:
    static constexpr int kCapacity = 262144;

    // r/g/b 为线性场景光；非有限值与非正亮度一律忽略。
    void add(float r, float g, float b);
    void addLuma(float luma);

    int count() const { return int(m_luma.size()); }

    // 99.5% 分位亮度；样本不足或全黑时回退 1.0（曲线退化为「不改曝光」）。
    float whitePoint() const;

private:
    std::vector<float> m_luma;
};

// 白点 → 逐通道查表。x = linear / white ∈ [0, 4]，表按 sqrt 分布取样，
// 暗部更密（人眼与 sRGB 编码在暗部的斜率大），4× 白点以上一律 255。
class Curve
{
public:
    static constexpr int kSize = 4096;
    static constexpr float kMaxX = 4.0f;

    explicit Curve(float whitePoint);

    // 线性值 → 8bit sRGB；NaN/负数 → 0。
    unsigned char operator()(float linear) const;

    float whitePoint() const { return m_white; }

private:
    float m_white = 1.0f;
    float m_invWhite = 1.0f;
    unsigned char m_lut[kSize + 1] = {};
};

// 便捷封装：先喂样本得到白点，再逐像素用曲线。
struct AutoExposure
{
    Meter meter;
    Curve curve() const { return Curve(meter.whitePoint()); }
};

} // namespace HdrPreview
