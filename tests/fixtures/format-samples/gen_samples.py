#!/usr/bin/env python3
"""Generate test samples for QuikView T1/T2/T3 format coverage."""

import gzip
import io
import os
import struct
import subprocess
import sys
import zipfile
from pathlib import Path

from PIL import Image, ImageDraw

OUT_DIR = Path(__file__).resolve().parent


def make_test_image(w=64, h=48, color=(40, 140, 220, 255), accent=(240, 180, 40, 255)):
    img = Image.new("RGBA", (w, h), color)
    draw = ImageDraw.Draw(img)
    draw.rectangle([6, 6, w - 7, h - 7], outline=accent, width=3)
    draw.ellipse([w // 4, h // 4, 3 * w // 4, 3 * h // 4], fill=accent)
    return img


def run_ffmpeg(args):
    cmd = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error"] + args
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        raise RuntimeError(f"ffmpeg failed ({' '.join(cmd)}): {res.stderr}")


def gen_svgz():
    svg = (
        '<svg xmlns="http://www.w3.org/2000/svg" width="128" height="96" viewBox="0 0 128 96">'
        '<rect x="4" y="4" width="120" height="88" rx="12" fill="#2563eb"/>'
        '<circle cx="64" cy="48" r="24" fill="#facc15"/>'
        '</svg>'
    ).encode("utf-8")
    (OUT_DIR / "sample.svgz").write_bytes(gzip.compress(svg))
    zh_dir = OUT_DIR / "中文目录"
    zh_dir.mkdir(parents=True, exist_ok=True)
    (zh_dir / "测试矢量.svgz").write_bytes(gzip.compress(svg))
    # Truncated svgz
    gz = gzip.compress(svg)
    (OUT_DIR / "bad_trunc.svgz").write_bytes(gz[:-6])


def gen_dds():
    # 1) Uncompressed BGRA 16x16
    w, h = 16, 16
    hdr = bytearray(128)
    hdr[0:4] = b"DDS "
    struct.pack_into("<IIIIII", hdr, 4, 124, 0x1007, h, w, w * 4, 1)
    # DDS_PIXELFORMAT at offset 76 (32 bytes): dwSize=32, dwFlags=0x41 (ALPHAPIXELS|RGB), bitcount=32, BGRA masks
    struct.pack_into("<IIIIIIII", hdr, 76, 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<I", hdr, 108, 0x1000)
    pixels = bytearray()
    for y in range(h):
        for x in range(w):
            pixels += bytes([200, 100, 50, 255])  # B, G, R, A
    (OUT_DIR / "sample_bgra.dds").write_bytes(bytes(hdr) + bytes(pixels))

    # 2) BC1 (DXT1) 16x16 (4x4 blocks = 16 blocks * 8 bytes)
    hdr_bc1 = bytearray(128)
    hdr_bc1[0:4] = b"DDS "
    struct.pack_into("<IIIIII", hdr_bc1, 4, 124, 0x1007, h, w, 16 * 8, 1)
    struct.pack_into("<II4sI", hdr_bc1, 76, 32, 0x4, b"DXT1", 0)
    struct.pack_into("<I", hdr_bc1, 108, 0x1000)
    # Color0=0xF800 (red), Color1=0x07E0 (green), indices=0x00000000
    block_bc1 = struct.pack("<HHI", 0xF800, 0x07E0, 0x55555555)
    (OUT_DIR / "sample_bc1.dds").write_bytes(bytes(hdr_bc1) + block_bc1 * 16)

    # 3) BC3 (DXT5) 16x16 (16 blocks * 16 bytes)
    hdr_bc3 = bytearray(128)
    hdr_bc3[0:4] = b"DDS "
    struct.pack_into("<IIIIII", hdr_bc3, 4, 124, 0x1007, h, w, 16 * 16, 1)
    struct.pack_into("<II4sI", hdr_bc3, 76, 32, 0x4, b"DXT5", 0)
    struct.pack_into("<I", hdr_bc3, 108, 0x1000)
    block_bc3 = bytes([255, 128, 0, 0, 0, 0, 0, 0]) + block_bc1
    (OUT_DIR / "sample_bc3.dds").write_bytes(bytes(hdr_bc3) + block_bc3 * 16)

    # 4) Oversized DDS header (50000x50000) + truncated DDS
    hdr_huge = bytearray(hdr)
    struct.pack_into("<II", hdr_huge, 12, 50000, 50000)
    (OUT_DIR / "bad_huge.dds").write_bytes(bytes(hdr_huge))
    (OUT_DIR / "bad_trunc.dds").write_bytes(bytes(hdr)[:100])


def gen_farbfeld():
    w, h = 32, 24
    hdr = b"farbfeld" + struct.pack(">II", w, h)
    px = bytearray()
    for y in range(h):
        for x in range(w):
            px += struct.pack(">HHHH", 0x3000, 0xA000, 0xE000, 0xFFFF)
    (OUT_DIR / "sample.ff").write_bytes(hdr + bytes(px))
    (OUT_DIR / "bad_huge.ff").write_bytes(b"farbfeld" + struct.pack(">II", 50000, 50000) + b"\x00" * 16)


def gen_ani():
    frames_bytes = []
    for col in [(220, 60, 60, 255), (60, 200, 90, 255)]:
        im = make_test_image(32, 32, col)
        buf = io.BytesIO()
        im.save(buf, format="ICO", sizes=[(32, 32)])
        frames_bytes.append(buf.getvalue())

    def chunk(cid, payload):
        pad = b"\x00" if (len(payload) & 1) else b""
        return cid + struct.pack("<I", len(payload)) + payload + pad

    anih_data = struct.pack("<IIIIIIIII", 36, 2, 2, 32, 32, 32, 1, 8, 1)
    rate_data = struct.pack("<II", 6, 12)  # 100ms, 200ms
    seq_data = struct.pack("<II", 0, 1)
    fram_payload = b"fram" + chunk(b"icon", frames_bytes[0]) + chunk(b"icon", frames_bytes[1])
    list_chunk = chunk(b"LIST", fram_payload)
    body = b"ACON" + chunk(b"anih", anih_data) + chunk(b"rate", rate_data) + chunk(b"seq ", seq_data) + list_chunk
    riff = b"RIFF" + struct.pack("<I", len(body)) + body
    (OUT_DIR / "sample.ani").write_bytes(riff)


def gen_containers():
    im = make_test_image(64, 48, (90, 60, 180, 255))
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    png_bytes = buf.getvalue()
    for name in ["sample.kra", "sample.ora"]:
        with zipfile.ZipFile(OUT_DIR / name, "w", compression=zipfile.ZIP_DEFLATED) as zf:
            zf.writestr("mergedimage.png", png_bytes)


def gen_ffmpeg_and_pillow():
    base_png = OUT_DIR / "single.png"
    f1_png = OUT_DIR / "_f1.png"
    f2_png = OUT_DIR / "_f2.png"
    make_test_image(64, 48, (30, 120, 210, 255), (255, 220, 60, 255)).save(base_png)
    make_test_image(64, 48, (220, 50, 50, 255), (255, 255, 255, 255)).save(f1_png)
    make_test_image(64, 48, (40, 190, 90, 255), (20, 20, 20, 255)).save(f2_png)

    # Pillow outputs
    rgb = Image.open(base_png).convert("RGB")
    rgb.save(OUT_DIR / "sample.jpe", format="JPEG", quality=92)
    rgb.save(OUT_DIR / "sample.dib", format="DIB")
    rgb.save(OUT_DIR / "sample.pcx", format="PCX")
    rgb.save(OUT_DIR / "sample.pnm", format="PPM")
    Image.open(base_png).save(OUT_DIR / "single.webp", format="WEBP")

    # Static formats via ffmpeg
    for ext, extra in [
        ("qoi", []),
        ("hdr", []),
        ("sgi", []),
        ("pam", []),
        ("pfm", []),
        ("ras", []),
        ("xwd", []),
        ("dpx", []),
        ("jp2", ["-c:v", "libopenjpeg"]),
        ("j2k", ["-c:v", "libopenjpeg"]),
        ("jxl", ["-c:v", "libjxl"]),
        ("exr", ["-c:v", "exr"]),
    ]:
        run_ffmpeg(["-i", str(base_png)] + extra + [str(OUT_DIR / f"sample.{ext}")])

    # Animated formats via ffmpeg
    pattern = str(OUT_DIR / "_f%d.png")
    run_ffmpeg(["-framerate", "5", "-i", pattern, "-f", "apng", "-plays", "0", str(OUT_DIR / "anim.apng")])
    run_ffmpeg(["-framerate", "5", "-i", pattern, "-f", "apng", "-plays", "0", str(OUT_DIR / "anim_png.png")])
    run_ffmpeg(["-framerate", "5", "-i", pattern, "-c:v", "libwebp_anim", "-loop", "0", str(OUT_DIR / "anim.webp")])
    run_ffmpeg(["-framerate", "5", "-i", pattern, "-c:v", "libjxl_anim", "-f", "rawvideo", str(OUT_DIR / "anim.jxl")])

    # Clean temp frame PNGs
    f1_png.unlink(missing_ok=True)
    f2_png.unlink(missing_ok=True)


def gen_bad_samples():
    for src_name, bad_name in [
        ("sample.qoi", "bad_trunc.qoi"),
        ("sample.hdr", "bad_trunc.hdr"),
        ("sample.jxl", "bad_trunc.jxl"),
        ("sample.exr", "bad_trunc.exr"),
        ("sample.jp2", "bad_trunc.jp2"),
    ]:
        data = (OUT_DIR / src_name).read_bytes()
        (OUT_DIR / bad_name).write_bytes(data[: max(8, len(data) // 4)])

    (OUT_DIR / "bad_fake.exr").write_bytes(b"NOT_AN_EXR_FILE_TEXT_ONLY")
    # Oversized QOI header (50000x50000)
    (OUT_DIR / "bad_huge.qoi").write_bytes(b"qoif" + struct.pack(">IIBB", 50000, 50000, 4, 0) + b"\x00" * 16)


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    gen_svgz()
    gen_dds()
    gen_farbfeld()
    gen_ani()
    gen_containers()
    gen_ffmpeg_and_pillow()
    gen_bad_samples()
    print(f"Generated samples in {OUT_DIR}: {len(list(OUT_DIR.iterdir()))} items")


if __name__ == "__main__":
    main()
