# -*- coding: utf-8 -*-
"""构造标准 PSD 测试文件（28B 预览头）"""
from PIL import Image
import struct, io

im = Image.new('RGB', (400, 300))
for x in range(400):
    for y in range(0, 300, 5):
        im.putpixel((x, y), (x * 255 // 400, y * 255 // 300, 128))
jb = io.BytesIO()
im.save(jb, 'JPEG', quality=90)
jpg = jb.getvalue()
TW, TH = 400, 300

def be32(v):
    return struct.pack('>I', v)

def be16(v):
    return struct.pack('>H', v)

hdr = b'8BPS' + be16(1) + b'\x00' * 6 + be16(3) + be32(TH) + be32(TW) + be16(8) + be16(3)
cmdata = be32(0)
# 标准 28B 预览头: format(1) w h wbytes totalsize + bpp(24) planes(1) + jfiflen
prev_head = (be32(1) + be32(TW) + be32(TH) + be32(TW * 3) + be32(TW * TH * 3)
             + be16(24) + be16(1) + be32(len(jpg)))
assert len(prev_head) == 28, len(prev_head)
res_block = b'8BIM' + be16(0x040C) + b'\x00\x00' + be32(28 + len(jpg)) + prev_head + jpg
ires = be32(len(res_block)) + res_block
open('Temp/test-thumb.psd', 'wb').write(hdr + cmdata + ires + be32(0) + be32(0))
print('test-thumb.psd', len(hdr + cmdata + ires) + 8, 'jpg', len(jpg))

res2 = b'8BIM' + be16(0x03EB) + b'\x00\x00' + be32(4) + be32(0)
open('Temp/test-nothumb.psd', 'wb').write(hdr + cmdata + be32(len(res2)) + res2 + be32(0) + be32(0))
print('test-nothumb.psd ok')
