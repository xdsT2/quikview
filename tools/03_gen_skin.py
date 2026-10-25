#!/usr/bin/env python3
"""QuikView 界面素材生成器。

生成 `resources/skin/` 下由参数化几何描述的界面素材（RGBA PNG）。
各文件的尺寸、帧序与状态语义是代码侧的接口约束，设计参数见 `docs/ATTRIBUTION.md`。

生成物与 `resources/skin/PROVENANCE.json` 中记录的 SHA-256 逐字节一致，
因此本脚本也是「素材非第三方拷贝」的可复现证据。

用法::

    python tools/03_gen_skin.py            # 重新生成并写盘
    python tools/03_gen_skin.py --check    # 只校验现有文件是否与生成结果一致

依赖：Pillow（绘制全程使用自带图元，不读取任何外部图像）。
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SKIN_DIR = ROOT / "resources" / "skin"
PROVENANCE = SKIN_DIR / "PROVENANCE.json"

# 窗口四边内阴影：由外向内 6 级衰减，最外一级最重
SHADOW_ALPHAS = (88, 58, 32, 16, 6, 0)
SHADOW_RGB = (0, 0, 0)

# 皮肤素材的绘制不依赖任何随机数或系统状态，同样输入必得同样输出
PNG_SAVE_KWARGS = {"optimize": True}


# --------------------------------------------------------------------------
# 生成器
# --------------------------------------------------------------------------

def gen_border_top() -> Image.Image:
    """顶部内阴影：60×6，自上而下 88→0 衰减。"""
    w, h = 60, 6
    im = Image.new("RGBA", (w, h), (*SHADOW_RGB, 0))
    px = im.load()
    for y, alpha in enumerate(SHADOW_ALPHAS):
        for x in range(w):
            px[x, y] = (*SHADOW_RGB, alpha)
    return im


def gen_border_bottom() -> Image.Image:
    """底部内阴影：60×6，自下而上衰减（与顶部镜像）。"""
    w, h = 60, 6
    im = Image.new("RGBA", (w, h), (*SHADOW_RGB, 0))
    px = im.load()
    for y, alpha in enumerate(reversed(SHADOW_ALPHAS)):
        for x in range(w):
            px[x, y] = (*SHADOW_RGB, alpha)
    return im


def gen_border_left() -> Image.Image:
    """左侧内阴影：6×60，自左向右 88→0 衰减。"""
    w, h = 6, 60
    im = Image.new("RGBA", (w, h), (*SHADOW_RGB, 0))
    px = im.load()
    for x, alpha in enumerate(SHADOW_ALPHAS):
        for y in range(h):
            px[x, y] = (*SHADOW_RGB, alpha)
    return im


def gen_border_right() -> Image.Image:
    """右侧内阴影：6×60，自右向左衰减。"""
    w, h = 6, 60
    im = Image.new("RGBA", (w, h), (*SHADOW_RGB, 0))
    px = im.load()
    for x, alpha in enumerate(reversed(SHADOW_ALPHAS)):
        for y in range(h):
            px[x, y] = (*SHADOW_RGB, alpha)
    return im


# name -> 生成函数；顺序即写盘顺序
GENERATORS = {
    "border_top.png": gen_border_top,
    "border_bottom.png": gen_border_bottom,
    "border_left.png": gen_border_left,
    "border_right.png": gen_border_right,
}


# --------------------------------------------------------------------------
# 驱动
# --------------------------------------------------------------------------

def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_expected() -> dict[str, str]:
    if not PROVENANCE.is_file():
        return {}
    data = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    return {name: meta["sha256"] for name, meta in data.get("files", {}).items()}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="生成 QuikView 界面素材")
    ap.add_argument("--check", action="store_true",
                    help="校验全部素材，不写盘；发现不一致时返回非零")
    args = ap.parse_args(argv)

    expected = load_expected()
    if not expected:
        print("PROVENANCE.json 缺失或为空", file=sys.stderr)
        return 2

    if not args.check:
        SKIN_DIR.mkdir(parents=True, exist_ok=True)
        for name, fn in GENERATORS.items():
            image = fn()
            image.save(SKIN_DIR / name, "PNG", **PNG_SAVE_KWARGS)
            print(f"WROTE    {name}  {image.width}×{image.height}")
        return 0

    failed: list[str] = []

    # 第一层：全部素材的落盘文件必须与登记的 SHA-256 一致（防篡改）
    for name, want in expected.items():
        target = SKIN_DIR / name
        if not target.is_file():
            print(f"MISSING   {name}")
            failed.append(name)
        elif _sha256(target) == want:
            print(f"OK        {name}")
        else:
            print(f"MISMATCH  {name}")
            failed.append(name)

    # 第二层：标为 generated 的素材必须能被本脚本逐字节重建
    print("\n-- 生成器复现校验 --")
    for name, fn in GENERATORS.items():
        buf = io.BytesIO()
        fn().save(buf, "PNG", **PNG_SAVE_KWARGS)
        got = hashlib.sha256(buf.getvalue()).hexdigest()
        want = expected.get(name)
        if got == want:
            print(f"REBUILT   {name}")
        else:
            print(f"REBUILD-FAIL {name}\n         期望 {want}\n         实际 {got}")
            failed.append(name)

    if failed:
        print(f"\n{len(failed)} 项未通过：{', '.join(failed)}", file=sys.stderr)
        return 1
    total = len(expected)
    print(f"\n全部 {total} 个文件校验通过，其中 {len(GENERATORS)} 个可由生成器逐字节重建。")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
