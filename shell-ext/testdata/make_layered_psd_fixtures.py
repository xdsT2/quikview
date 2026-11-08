# -*- coding: utf-8 -*-
"""生成带图层的 PSD 测试夹具（shell-ext 缩略图验收用）。

为什么用脚本而不是塞二进制：
  派单要求「参照 make_psd_fixtures.py 的可复现原则，最好用脚本生成而非塞二进制」。
  这两个文件里含中文字形与渐变，二进制入库既占体积又无法审阅内容，
  而且任何人想改测试场景都得开 Photoshop。脚本可读、可改、可重跑。

生成两个文件，对应两种真实场景：
  test-layered-embedded.psd
      有图层的 PSD，且**带** 8BIM 0x040C 内嵌缩略图资源
      -> 资源管理器应能直接取到缩略图
  test-layered-no-embedded.psd
      有图层的 PSD，但**不带**内嵌缩略图资源
      -> 走回退路径（QuikView 自己渲染），用于验证回退分支

中文图层名注意：
  psd_tools 写图层名的 Pascal 串默认用 macroman 编码，中文会抛
  UnicodeEncodeError。必须先用 ASCII 名建层，再赋值 .name 为中文，
  这样走 UNICODE_LAYER_NAME (luni) 附加块，才能正常保存。

用法：
    pip install psd-tools pillow
    python make_layered_psd_fixtures.py [输出目录]

默认输出到脚本所在目录，与既有 test-*.psd 并列。
"""
import io
import os
import struct
import sys

from PIL import Image, ImageDraw
from psd_tools import PSDImage
from psd_tools.api.layers import PixelLayer

W, H = 640, 480

# 缩略图 ShellEx 资源 ID（8BIM 0x040C）
RES_THUMBNAIL = 0x040C


def _gradient(w, h, top, bottom):
    """竖直渐变底图。"""
    im = Image.new("RGB", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        d.line([(0, y), (w, y)], fill=c)
    return im


def _add_pixel(psd, image, ascii_name, cn_name, top=0, left=0):
    """先以 ASCII 名建层，再改成中文名（走 luni 块，避免 macroman 编码失败）。"""
    layer = PixelLayer.frompil(image, psd, ascii_name, top=top, left=left)
    layer.name = cn_name
    return layer


def _make_thumbnail_resource(size=(160, 120)):
    """构造 8BIM 0x040C 内嵌缩略图资源。

    结构（psd_tools ThumbnailResource，格式串 6I2H，共 28B）：
        fmt(u32) width(u32) height(u32) row(u32) total_size(u32)
        size(u32) bits(u16) planes(u16) + data
    注意 size 就是 data 长度，没有单独的 jfiflen 字段。
    """
    from psd_tools.psd.image_resources import ImageResource, ThumbnailResource

    tw, th = size
    im = Image.new("RGB", (tw, th), (40, 60, 90))
    d = ImageDraw.Draw(im)
    for i in range(0, th, 6):
        d.line([(0, i), (tw, i)], fill=(90 + i, 130 + i // 2, 180))
    d.ellipse([tw // 3, th // 3, tw * 2 // 3, th * 2 // 3], fill=(255, 214, 102))
    jb = io.BytesIO()
    im.save(jb, "JPEG", quality=90)
    jpg = jb.getvalue()

    thumb = ThumbnailResource(
        fmt=1,                       # 1 = kJpegRGB
        width=tw,
        height=th,
        row=tw * 3,
        total_size=tw * th * 3,
        bits=24,
        planes=1,
        data=jpg,
    )
    return ImageResource(key=RES_THUMBNAIL, name="", data=thumb)


def build_embedded(out_path):
    """3 层：背景渐变 / 分组（内含一个圆）/ 半透明光斑，并带内嵌缩略图。"""
    psd = PSDImage.new("RGB", (W, H), color=(16, 24, 32))

    bg = _gradient(W, H, (28, 40, 62), (120, 170, 210))
    psd.append(_add_pixel(psd, bg, "bg", "背景"))

    grp = psd.create_group(name="Scene")
    grp.name = "风景 Scene"
    disc = Image.new("RGBA", (180, 180), (0, 0, 0, 0))
    dd = ImageDraw.Draw(disc)
    dd.ellipse([10, 10, 170, 170], fill=(255, 214, 102, 235))
    grp.append(_add_pixel(psd, disc, "disc", "圆", top=77, left=280))

    # 半透明光斑（覆盖到画布外，验证 bbox 越界处理）
    glow = Image.new("RGBA", (245, 130), (0, 0, 0, 0))
    dg = ImageDraw.Draw(glow)
    for r in range(60, 0, -2):
        a = int(70 * (1 - r / 60.0))
        dg.ellipse([122 - r, 65 - r // 2, 122 + r, 65 + r // 2], fill=(255, 255, 255, a))
    psd.append(_add_pixel(psd, glow, "glow", "半透明边缘", top=395, left=485))

    psd.image_resources[RES_THUMBNAIL] = _make_thumbnail_resource()
    psd.save(out_path)
    return out_path


def build_no_embedded(out_path):
    """2 层，不带内嵌缩略图资源 -> 走 QuikView 回退渲染分支。"""
    psd = PSDImage.new("RGB", (W, H), color=(16, 24, 32))

    bg = _gradient(W, H, (18, 24, 34), (70, 90, 120))
    psd.append(_add_pixel(psd, bg, "bg", "背景"))

    patch = Image.new("RGBA", (210, 145), (0, 0, 0, 0))
    dp = ImageDraw.Draw(patch)
    dp.rounded_rectangle([0, 0, 209, 144], radius=18, fill=(240, 120, 90, 220))
    psd.append(_add_pixel(psd, patch, "patch", "偏红色块", top=116, left=94))

    buf = io.BytesIO()
    psd.save(buf)
    with open(out_path, "wb") as f:
        f.write(buf.getvalue())
    return out_path


def has_thumbnail_resource(path):
    """自检：文件里是否有 8BIM 0x040C 资源（用 psd_tools 回读）。"""
    psd = PSDImage.open(path)
    return RES_THUMBNAIL in psd.image_resources


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    os.makedirs(out_dir, exist_ok=True)

    a = build_embedded(os.path.join(out_dir, "test-layered-embedded.psd"))
    b = build_no_embedded(os.path.join(out_dir, "test-layered-no-embedded.psd"))
    for p in (a, b):
        print("%s  %d bytes" % (os.path.basename(p), os.path.getsize(p)))

    # 自检：两个文件必须一个有一个没有
    ea = has_thumbnail_resource(a)
    eb = has_thumbnail_resource(b)
    print("embedded 有 0x040C 资源 = %s" % ea)
    print("no-embedded 有 0x040C 资源 = %s" % eb)
    if not ea or eb:
        print("!! 自检失败：资源注入不符合预期")
        sys.exit(1)


if __name__ == "__main__":
    main()
