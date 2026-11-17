"""Crop / zoom a frame dumped by the patch, for eyeballing a suspected sprite bug.

The patch writes a normal bottom-up 24-bit BMP. This tool always writes its
output as a top-down BMP (negative height) so there is never any doubt about
which way round the result is, and can scale the crop up with nearest-neighbour
so small artefacts survive the downscale a viewer might apply.

Usage:
    python tests/crop.py SRC.bmp X Y W H OUT.bmp [--zoom N]
"""
import argparse
import struct


def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM':
        raise SystemExit('not a BMP')
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    if bpp != 24:
        raise SystemExit(f'only 24bpp supported, got {bpp}')
    top_down = h < 0
    h = abs(h)
    stride = (w * 3 + 3) & ~3
    return data, off, w, h, stride, top_down


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('x', type=int)
    ap.add_argument('y', type=int)
    ap.add_argument('w', type=int)
    ap.add_argument('h', type=int)
    ap.add_argument('out')
    ap.add_argument('--zoom', type=int, default=1)
    a = ap.parse_args()

    data, off, w, h, stride, top_down = read_bmp(a.src)
    x0, y0 = max(0, a.x), max(0, a.y)
    cw, ch = min(a.w, w - x0), min(a.h, h - y0)
    z = max(1, a.zoom)

    rows = []
    for j in range(ch):                       # j = 0 is the TOP row of the crop
        sy = y0 + j
        row = (sy if top_down else h - 1 - sy) * stride
        line = bytearray()
        for i in range(cw):
            p = off + row + (x0 + i) * 3
            b, g, r = data[p], data[p + 1], data[p + 2]
            line += bytes((b, g, r)) * z
        rows.append(bytes(line) * z)
    out_stride = (cw * z * 3 + 3) & ~3
    pad = b'\0' * (out_stride - cw * z * 3)
    body = b''.join(r + pad for r in rows)     # top-down order

    ow, oh = cw * z, ch * z
    header = struct.pack('<2sIHHI', b'BM', 14 + 40 + len(body), 0, 0, 14 + 40)
    dib = struct.pack('<IiiHHIIiiII', 40, ow, -oh, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(a.out, 'wb').write(header + dib + body)
    print(f'{a.out}: {ow}x{oh} from ({x0},{y0}) {cw}x{ch} zoom={z}')


if __name__ == '__main__':
    main()
