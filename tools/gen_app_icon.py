#!/usr/bin/env python3
"""SoleView 应用图标生成器（母版 → 多尺寸 ICO）。

唯一母版：`website/assets/img/favicon.svg`（官网真源，本脚本只读，绝不回写）。
产物：`resources/soleview.ico`，9 帧 PNG 压缩帧（Vista+ 现代 ICO 格式）：

    16 / 20 / 24 / 32 / 40 / 48 / 64 / 128 / 256

设计口径（与 `docs/ATTRIBUTION.md` 4b 节一致）：
- 所有帧在 256px 逻辑画布上一次绘制，再 LANCZOS 下采样到目标尺寸，保证小尺寸
  抗锯齿充分；16/20 两档做光学微调（去掉不可辨识的内层细描边），配色与母版同源。
- ICO 容器手工封装：每帧一条 PNG 记录，不依赖外部图标工具。
- 路径全部相对脚本自身位置解析，禁止硬编码绝对路径；同一份母版重跑必得同一份 ico。

依赖：Pillow（`pip install pillow`）。不读取任何外部位图素材。

用法::

    python tools/gen_app_icon.py                 # 重新生成 resources/soleview.ico
    python tools/gen_app_icon.py --preview-dir DIR   # 额外导出逐尺寸 PNG 供目检
    python tools/gen_app_icon.py --check         # 只校验现有 ico 是否与重跑结果逐字节一致
"""

from __future__ import annotations

import argparse
import io
import re
import struct
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
SVG_MASTER = ROOT / "website" / "assets" / "img" / "favicon.svg"
ICO_OUT = ROOT / "resources" / "soleview.ico"

SIZES: Tuple[int, ...] = (16, 20, 24, 32, 40, 48, 64, 128, 256)
CANVAS = 256  # 逻辑画布边长（与母版 viewBox 一致）
SUPERSAMPLE = 4  # 离屏倍率：高倍画布绘制后 LANCZOS 回落，边缘矢量级抗锯齿

SVG_NS = "{http://www.w3.org/2000/svg}"

# 16/20 等极小尺寸下不可辨识、去掉后不影响品牌识别的细节：内层白色高光描边。
OPTIC_SKIP_SIZES = frozenset((16, 20))

PIPE_WORK = 24  # 贝塞尔/圆弧每段采样点数（256 画布上足够平滑）


# --------------------------------------------------------------------------- #
# 母版解析
# --------------------------------------------------------------------------- #

def _num(el: ET.Element, name: str, default: float = 0.0) -> float:
    v = el.attrib.get(name)
    return float(v) if v is not None else default


def _color(value: Optional[str]) -> Optional[Tuple[int, int, int]]:
    """#RRGGBB → (r, g, b)；url(#...) / none / 空 返回 None（由调用方按渐变处理）。"""
    if not value or value == "none":
        return None
    if value.startswith("#"):
        h = value[1:]
        if len(h) == 3:
            h = "".join(c * 2 for c in h)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))  # type: ignore[return-value]
    return None


def _parse_gradients(root: ET.Element) -> Dict[str, List[Tuple[float, Tuple[int, int, int]]]]:
    """读 defs 里的 linearGradient：{id: [(offset, rgb), ...]}。母版只用到对角两停渐变。"""
    grads: Dict[str, List[Tuple[float, Tuple[int, int, int]]]] = {}
    for grad in root.iter(SVG_NS + "linearGradient"):
        stops: List[Tuple[float, Tuple[int, int, int]]] = []
        for stop in grad.findall(SVG_NS + "stop"):
            offset = float(stop.attrib.get("offset", "0%").rstrip("%")) / 100.0
            rgb = _color(stop.attrib.get("stop-color", "#000000"))
            stops.append((offset, rgb if rgb else (0, 0, 0)))
        if stops:
            grads[grad.attrib.get("id", "")] = stops
    return grads


def _grad_rgb(stops: List[Tuple[float, Tuple[int, int, int]]], t: float) -> Tuple[int, int, int]:
    t = max(0.0, min(1.0, t))
    if len(stops) == 1:
        return stops[0][1]
    for i in range(len(stops) - 1):
        o0, c0 = stops[i]
        o1, c1 = stops[i + 1]
        if o0 <= t <= o1:
            f = 0.0 if o1 == o0 else (t - o0) / (o1 - o0)
            return tuple(round(c0[k] + (c1[k] - c0[k]) * f) for k in range(3))  # type: ignore[return-value]
    return stops[-1][1]


# --------------------------------------------------------------------------- #
# 路径（d）解析：只支持母版用到的 M / L / C / Q / Z（绝对坐标）
# --------------------------------------------------------------------------- #

_TOKEN_RE = re.compile(r"[MmLlCcQqZz]|-?\d*\.?\d+(?:[eE][-+]?\d+)?")


def _parse_path(d: str) -> List[List[Tuple[float, float]]]:
    """把 d 拆成若干子路径，每条返回折线点列（曲线已采样为多边形）。"""
    tokens = _TOKEN_RE.findall(d)
    pos = 0
    subpaths: List[List[Tuple[float, float]]] = []
    cur: List[Tuple[float, float]] = []
    cx = cy = 0.0
    start_x = start_y = 0.0
    cmd = ""

    def pair() -> Tuple[float, float]:
        nonlocal pos
        x = float(tokens[pos]); y = float(tokens[pos + 1]); pos += 2
        return x, y

    while pos < len(tokens):
        tok = tokens[pos]
        if tok.isalpha():
            cmd = tok; pos += 1
        if cmd in ("M", "m"):
            x, y = pair()
            if cur:
                subpaths.append(cur)
            cur = [(x, y)]
            cx, cy, start_x, start_y = x, y, x, y
            cmd = "L"  # 后续隐式直线
        elif cmd in ("L", "l"):
            x, y = pair()
            cur.append((x, y)); cx, cy = x, y
        elif cmd in ("C", "c"):
            x1, y1 = pair(); x2, y2 = pair(); x, y = pair()
            for k in range(1, PIPE_WORK + 1):
                f = k / PIPE_WORK
                a = (1 - f) ** 3
                b = 3 * (1 - f) ** 2 * f
                c = 3 * (1 - f) * f ** 2
                dd = f ** 3
                cur.append((a * cx + b * x1 + c * x2 + dd * x,
                             a * cy + b * y1 + c * y2 + dd * y))
            cx, cy = x, y
        elif cmd in ("Q", "q"):
            x1, y1 = pair(); x, y = pair()
            for k in range(1, PIPE_WORK + 1):
                f = k / PIPE_WORK
                a = (1 - f) ** 2
                b = 2 * (1 - f) * f
                dd = f ** 2
                cur.append((a * cx + b * x1 + dd * x,
                             a * cy + b * y1 + dd * y))
            cx, cy = x, y
        elif cmd in ("Z", "z"):
            cur.append((start_x, start_y))
            subpaths.append(cur); cur = []
            cmd = ""
        else:
            break
    if cur:
        subpaths.append(cur)
    return subpaths


# --------------------------------------------------------------------------- #
# 扁平指令（g 属性向下继承）
# --------------------------------------------------------------------------- #

def _walk(el: ET.Element, inherited: Dict[str, str], out: List[Dict[str, object]]) -> None:
    attrs = dict(inherited)
    attrs.update(el.attrib)
    tag = el.tag

    if tag == SVG_NS + "defs":
        return
    if tag == SVG_NS + "g":
        for child in el:
            _walk(child, attrs, out)
        return

    inst: Dict[str, object] = {"tag": tag, "attrs": attrs}

    if tag == SVG_NS + "rect":
        inst["box"] = (_num(el, "x"), _num(el, "y"),
                       _num(el, "x") + _num(el, "width"),
                       _num(el, "y") + _num(el, "height"))
        inst["rx"] = _num(el, "rx")
    elif tag == SVG_NS + "path":
        inst["subpaths"] = _parse_path(el.attrib["d"])
    elif tag == SVG_NS + "circle":
        m = re.match(r"translate\(\s*([-\d.]+)[\s,]+([-\d.]+)\s*\)",
                     attrs.get("transform", ""))
        ox, oy = (float(m.group(1)), float(m.group(2))) if m else (0.0, 0.0)
        inst["center"] = (ox + _num(el, "cx"), oy + _num(el, "cy"))
        inst["r"] = _num(el, "r")
    out.append(inst)


def flatten(root: ET.Element) -> List[Dict[str, object]]:
    out: List[Dict[str, object]] = []
    for el in root:
        _walk(el, {}, out)
    return out


# --------------------------------------------------------------------------- #
# 绘制
# --------------------------------------------------------------------------- #

def _diagonal_gradient(stops: List[Tuple[float, Tuple[int, int, int]]], size: int) -> Image.Image:
    """母版渐变均为 x1=0% y1=0% → x2=100% y2=100% 的对角渐变。"""
    layer = Image.new("RGB", (size, size))
    px = layer.load()
    denom = max(1, size - 1)
    for y in range(size):
        for x in range(size):
            t = ((x / denom) + (y / denom)) / 2.0
            px[x, y] = _grad_rgb(stops, t)
    return layer


def _rounded_mask(box: Tuple[float, float, float, float], radius: float,
                  size: int) -> Image.Image:
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle(box, radius=radius, fill=255)
    return mask


def _attr(inst: Dict[str, object], name: str, default: str = "") -> str:
    return str(inst["attrs"].get(name, default))  # type: ignore[union-attr]


def _draw(instructions: List[Dict[str, object]],
          grads: Dict[str, List[Tuple[float, Tuple[int, int, int]]]],
          skip_inner_stroke: bool) -> Image.Image:
    ss = SUPERSAMPLE
    side = CANVAS * ss

    def sc(v: float) -> float:
        return v * ss

    img = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    for inst in instructions:
        tag = inst["tag"]
        attrs = inst["attrs"]  # type: ignore[assignment]
        fill = _attr(inst, "fill", "none")
        stroke = _color(_attr(inst, "stroke", ""))
        sw = float(attrs.get("stroke-width", "0"))
        fop = float(attrs.get("fill-opacity", attrs.get("opacity", "1")))
        sop = float(attrs.get("stroke-opacity", "1"))

        # 极小尺寸：跳过内层白色高光描边 rect（fill=none + 白色细描边）。
        if skip_inner_stroke and tag == SVG_NS + "rect" \
                and fill == "none" and stroke == (255, 255, 255):
            continue

        if tag == SVG_NS + "rect":
            x0, y0, x1, y1 = inst["box"]  # type: ignore[assignment]
            box = (sc(x0), sc(y0), sc(x1), sc(y1))
            rx = sc(float(inst["rx"]))
            if fill.startswith("url(#"):
                gid = fill[5:-1]
                mask = _rounded_mask(box, rx, side)
                img.paste(_diagonal_gradient(grads[gid], side), (0, 0), mask)
                draw = ImageDraw.Draw(img)
            elif fill != "none":
                rgb = _color(fill)
                if rgb:
                    if fop < 1.0:
                        # ImageDraw 的半透明 fill 是覆盖而非 alpha 混合，
                        # 必须先落独立图层再 atop 合成到底图（否则背板区 alpha 塌成 115）。
                        layer = Image.new("RGBA", (side, side), (0, 0, 0, 0))
                        ImageDraw.Draw(layer).rounded_rectangle(
                            box, radius=rx, fill=(*rgb, round(255 * fop)))
                        img.alpha_composite(layer)
                        draw = ImageDraw.Draw(img)
                    else:
                        draw.rounded_rectangle(box, radius=rx, fill=(*rgb, 255))
            if stroke:
                draw.rounded_rectangle(box, radius=rx, outline=(*stroke, round(255 * sop)),
                                        width=max(1, round(sw * ss)))

        elif tag == SVG_NS + "path":
            subpaths: List[List[Tuple[float, float]]] = inst["subpaths"]  # type: ignore[assignment]
            linecap = _attr(inst, "stroke-linecap", "butt")
            if fill.startswith("url(#"):
                gid = fill[5:-1]
                mask = Image.new("L", (side, side), 0)
                mdraw = ImageDraw.Draw(mask)
                for poly in subpaths:
                    mdraw.polygon([(sc(x), sc(y)) for x, y in poly], fill=255)
                img.paste(_diagonal_gradient(grads[gid], side), (0, 0), mask)
                draw = ImageDraw.Draw(img)
            elif fill != "none":
                rgb = _color(fill)
                if rgb:
                    if fop < 1.0:
                        layer = Image.new("RGBA", (side, side), (0, 0, 0, 0))
                        ldraw = ImageDraw.Draw(layer)
                        for poly in subpaths:
                            ldraw.polygon([(sc(x), sc(y)) for x, y in poly],
                                          fill=(*rgb, round(255 * fop)))
                        img.alpha_composite(layer)
                        draw = ImageDraw.Draw(img)
                    else:
                        for poly in subpaths:
                            draw.polygon([(sc(x), sc(y)) for x, y in poly],
                                         fill=(*rgb, 255))

            if stroke:
                width = max(1, round(sw * ss))
                sa = round(255 * sop)
                for poly in subpaths:
                    pts = [(sc(x), sc(y)) for x, y in poly]
                    draw.line(pts, fill=(*stroke, sa), width=width, joint="curve")
                    if linecap == "round":
                        r = width / 2.0
                        for px_, py_ in (pts[0], pts[-1]):
                            draw.ellipse((px_ - r, py_ - r, px_ + r, py_ + r),
                                         fill=(*stroke, sa))

        elif tag == SVG_NS + "circle":
            cx, cy = inst["center"]  # type: ignore[assignment]
            r = float(inst["r"])
            rgb = _color(fill)
            if rgb:
                draw.ellipse((sc(cx - r), sc(cy - r), sc(cx + r), sc(cy + r)), fill=rgb)

    return img.resize((CANVAS, CANVAS), Image.LANCZOS)


def _render_frame(size: int, instructions: List[Dict[str, object]],
                  grads: Dict[str, List[Tuple[float, Tuple[int, int, int]]]]) -> Image.Image:
    base = _draw(instructions, grads, skip_inner_stroke=size in OPTIC_SKIP_SIZES)
    if size == CANVAS:
        return base
    return base.resize((size, size), Image.LANCZOS)


# --------------------------------------------------------------------------- #
# ICO 封装（每帧一条 PNG 记录）
# --------------------------------------------------------------------------- #

def _encode_png(img: Image.Image) -> bytes:
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    return buf.getvalue()


def pack_ico(frames: List[Tuple[int, bytes]]) -> bytes:
    """frames: [(size, png_bytes), ...]，尺寸升序。"""
    header = struct.pack("<HHH", 0, 1, len(frames))
    entries: List[bytes] = []
    offset = 6 + 16 * len(frames)
    body = b""
    for size, png in frames:
        dim = 0 if size == 256 else size  # 256 在目录项里记 0
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                                    len(png), offset))
        body += png
        offset += len(png)
    return header + b"".join(entries) + body


def build_frames(preview_dir: Optional[Path] = None) -> List[Tuple[int, bytes, Image.Image]]:
    tree = ET.parse(SVG_MASTER)
    root = tree.getroot()
    instructions = flatten(root)
    grads = _parse_gradients(root)
    out: List[Tuple[int, bytes, Image.Image]] = []
    for size in SIZES:
        img = _render_frame(size, instructions, grads)
        if preview_dir:
            preview_dir.mkdir(parents=True, exist_ok=True)
            img.save(preview_dir / f"soleview-{size}.png")
        out.append((size, _encode_png(img), img))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Regenerate resources/soleview.ico from the SVG master.")
    ap.add_argument("--preview-dir", type=Path, help="export per-size PNG previews here")
    ap.add_argument("--check", action="store_true",
                    help="verify the existing ICO matches a fresh rebuild byte-for-byte")
    args = ap.parse_args()

    if not SVG_MASTER.is_file():
        print(f"[gen_app_icon] master SVG missing: {SVG_MASTER}", file=sys.stderr)
        return 2

    frames = build_frames(args.preview_dir)
    ico_bytes = pack_ico([(s, png) for s, png, _ in frames])

    if args.check:
        if not ICO_OUT.is_file():
            print("[check] existing ICO missing"); return 1
        if ICO_OUT.read_bytes() == ico_bytes:
            print(f"[check] OK: {ICO_OUT} matches rebuild ({len(ico_bytes)} bytes, "
                  f"{len(frames)} frames)")
            return 0
        print("[check] MISMATCH: existing ICO differs from rebuild", file=sys.stderr)
        return 1

    ICO_OUT.write_bytes(ico_bytes)
    print(f"[gen_app_icon] wrote {ICO_OUT}")
    for size, png, _ in frames:
        print(f"  {size:3d} px  {len(png):6d} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
