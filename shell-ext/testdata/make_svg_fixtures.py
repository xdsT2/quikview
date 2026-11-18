#!/usr/bin/env python3
"""生成 shell-ext 的 SVG 测试夹具（纯文本，不需要任何第三方库）。

用法：python shell-ext/testdata/make_svg_fixtures.py
默认输出到脚本所在目录，与既有 test-*.psd 并列。

五个夹具各自对应一条能力边界（缩略图自测逐条断言，见 src/selftest.cpp）：
  test-svg-basic.svg     纯形状 + 白色画布：基本渲染与尺寸/颜色
  test-svg-alpha.svg     无背景：透明通道（WTSAT_ARGB 预乘 alpha）
  test-svg-gradient.svg  线性 + 径向渐变：渐变是否真的生效
  test-svg-text.svg      <text>：nanosvg 没有字体栈，文字不渲染（如实记录局限）
  test-svg-huge.svg      viewBox 50000px：超大坐标系不按原始尺寸分配位图

注意：夹具内容只描述「画什么」，具体断言在 selftest 里，双方靠坐标约定对齐。
坐标改动必须同步改 selftest 的采样点。
"""
import os

OUT = os.path.dirname(os.path.abspath(__file__))

BASIC = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <rect x="0" y="0" width="512" height="512" fill="#ffffff"/>
  <circle cx="256" cy="256" r="160" fill="#1f6feb"/>
</svg>
"""

ALPHA = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <!-- 不画背景：四角必须是全透明，中间圆必须不透明 -->
  <circle cx="256" cy="256" r="180" fill="#e05252"/>
</svg>
"""

GRADIENT = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <defs>
    <!-- 坐标一律写百分比：nanosvg 对 objectBoundingBox 的单位小数坐标
         （x2="1"）按用户单位解释，渐变会塌缩成硬边（实测，见回执的局限表）。 -->
    <linearGradient id="lin" x1="0%" y1="0%" x2="100%" y2="0%">
      <stop offset="0" stop-color="#ff0000"/>
      <stop offset="1" stop-color="#0000ff"/>
    </linearGradient>
    <radialGradient id="rad" cx="50%" cy="50%" r="50%">
      <stop offset="0" stop-color="#ffffff"/>
      <stop offset="1" stop-color="#000000"/>
    </radialGradient>
  </defs>
  <rect x="0" y="0" width="512" height="512" fill="url(#lin)"/>
  <circle cx="256" cy="256" r="90" fill="url(#rad)"/>
</svg>
"""

TEXT = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">
  <!-- 顶部色带（形状，必须渲染） + 下方一行文字（nanosvg 不渲染） -->
  <rect x="0" y="0" width="512" height="160" fill="#333333"/>
  <text x="40" y="340" font-family="sans-serif" font-size="120" fill="#ff0000">SVG TEXT</text>
</svg>
"""

HUGE = """<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" width="50000" height="50000" viewBox="0 0 50000 50000">
  <rect x="0" y="0" width="50000" height="50000" fill="#00aa77"/>
  <circle cx="25000" cy="25000" r="18000" fill="#112233"/>
</svg>
"""

FILES = {
    "test-svg-basic.svg": BASIC,
    "test-svg-alpha.svg": ALPHA,
    "test-svg-gradient.svg": GRADIENT,
    "test-svg-text.svg": TEXT,
    "test-svg-huge.svg": HUGE,
}

for name, body in FILES.items():
    path = os.path.join(OUT, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(body)
    print(name, len(body), "bytes")
