"""Inspect a frame dumped by the patch (MUSOUTweaksPC_frame.bmp).

The patch writes an uncompressed 24-bit bottom-up BMP, so this needs no
third-party modules. It reports the image size, finds large uniform-colour
regions (the kind of flat rectangle a broken sprite shows up as) and can crop a
region out as its own BMP for a closer look.

Usage:
    python tests/inspect_frame.py FRAME.bmp                 # summary + flat areas
    python tests/inspect_frame.py FRAME.bmp --crop X Y W H --out part.bmp
    python tests/inspect_frame.py FRAME.bmp --probe X Y     # pixel value
"""
import argparse
import struct
import sys


def read_bmp(path):
    data = open(path, 'rb').read()
    if data[:2] != b'BM':
        raise SystemExit('not a BMP')
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    header_size = struct.unpack_from('<I', data, 14)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bit_count = struct.unpack_from('<H', data, 28)[0]
    if bit_count not in (24, 32):
        raise SystemExit(f'unsupported bit count {bit_count}')
    top_down = height < 0
    height = abs(height)
    stride = (width * bit_count // 8 + 3) & ~3
    return data, pixel_offset, width, height, bit_count, stride, top_down


def pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x, y):
    row = y if top_down else height - 1 - y
    at = pixel_offset + row * stride + x * (bit_count // 8)
    return data[at + 2], data[at + 1], data[at]  # r, g, b


def find_flat_regions(data, pixel_offset, width, height, bit_count, stride, top_down,
                      step=8, tolerance=6, min_pixels=400):
    """Coarse scan: group neighbouring samples that share one colour."""
    regions = []
    visited = set()
    for y in range(0, height, step):
        for x in range(0, width, step):
            if (x, y) in visited:
                continue
            base = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x, y)
            if not (min(base) > 40):  # skip near-black background
                continue
            left = x
            while left - step >= 0:
                px = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, left - step, y)
                if max(abs(a - b) for a, b in zip(px, base)) > tolerance:
                    break
                left -= step
            right = x
            while right + step < width:
                px = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, right + step, y)
                if max(abs(a - b) for a, b in zip(px, base)) > tolerance:
                    break
                right += step
            top = y
            while top - step >= 0:
                px = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x, top - step)
                if max(abs(a - b) for a, b in zip(px, base)) > tolerance:
                    break
                top -= step
            bottom = y
            while bottom + step < height:
                px = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x, bottom + step)
                if max(abs(a - b) for a, b in zip(px, base)) > tolerance:
                    break
                bottom += step
            for yy in range(top, bottom + 1, step):
                for xx in range(left, right + 1, step):
                    visited.add((xx, yy))
            area = (right - left) // step * (bottom - top) // step
            if area >= min_pixels:
                regions.append((area, left, top, right - left, bottom - top, base))
    regions.sort(reverse=True)
    return regions


def write_crop(src, out, x, y, w, h):
    """Crop and write a top-down BMP.

    The output declares a negative height on purpose: a bottom-up file makes it
    impossible to tell at a glance whether a viewer is showing the crop the right
    way round, and an upside-down crop reads as a mirrored UI when the point of
    the crop is to judge a UI sprite.
    """
    data, pixel_offset, width, height, bit_count, stride, top_down = read_bmp(src)
    row_bytes = (w * 3 + 3) & ~3
    rows = []
    for j in range(h):  # j = 0 is the top row of the crop
        sy = y + j
        line = bytearray()
        for i in range(w):
            r, g, b = pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x + i, sy)
            line += bytes((b, g, r))
        line += b'\0' * (row_bytes - len(line))
        rows.append(bytes(line))
    body = b''.join(rows)
    header = struct.pack('<2sIHHI', b'BM', 14 + 40 + len(body), 0, 0, 14 + 40)
    dib = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(out, 'wb').write(header + dib + body)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('frame')
    parser.add_argument('--crop', nargs=4, type=int, metavar=('X', 'Y', 'W', 'H'))
    parser.add_argument('--out')
    parser.add_argument('--probe', nargs=2, type=int, metavar=('X', 'Y'))
    parser.add_argument('--step', type=int, default=8)
    args = parser.parse_args()

    data, pixel_offset, width, height, bit_count, stride, top_down = read_bmp(args.frame)
    print(f'{args.frame}: {width}x{height} {bit_count}bpp top_down={top_down}')

    if args.probe:
        x, y = args.probe
        print(f'pixel({x},{y}) = {pixel(data, pixel_offset, width, stride, bit_count, top_down, height, x, y)}')
        return

    if args.crop:
        x, y, w, h = args.crop
        out = args.out or 'crop.bmp'
        print('wrote', write_crop(args.frame, out, x, y, w, h))
        return

    regions = find_flat_regions(data, pixel_offset, width, height, bit_count, stride, top_down,
                                step=args.step)
    print(f'flat regions (top {min(12, len(regions))}):')
    for area, x, y, w, h, colour in regions[:12]:
        print(f'  x={x:5d} y={y:5d} w={w:5d} h={h:5d} rgb={colour}')


if __name__ == '__main__':
    sys.exit(main())
