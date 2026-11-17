"""Minimal GDI screen grab to BMP (no third-party modules).

Used to check what the borderless window actually shows on screen when the
in-process frame capture is not enabled.
"""
import ctypes
import struct
import sys
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wintypes.DWORD),
        ("biWidth", wintypes.LONG),
        ("biHeight", wintypes.LONG),
        ("biPlanes", wintypes.WORD),
        ("biBitCount", wintypes.WORD),
        ("biCompression", wintypes.DWORD),
        ("biSizeImage", wintypes.DWORD),
        ("biXPelsPerMeter", wintypes.LONG),
        ("biYPelsPerMeter", wintypes.LONG),
        ("biClrUsed", wintypes.DWORD),
        ("biClrImportant", wintypes.DWORD),
    ]


def grab(path, scale=1):
    width = user32.GetSystemMetrics(0)
    height = user32.GetSystemMetrics(1)
    screen = user32.GetDC(0)
    mem = gdi32.CreateCompatibleDC(screen)
    bitmap = gdi32.CreateCompatibleBitmap(screen, width, height)
    gdi32.SelectObject(mem, bitmap)
    gdi32.BitBlt(mem, 0, 0, width, height, screen, 0, 0, 0x00CC0020)

    info = BITMAPINFOHEADER()
    info.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    info.biWidth = width
    info.biHeight = -height
    info.biPlanes = 1
    info.biBitCount = 32
    info.biCompression = 0
    buffer = ctypes.create_string_buffer(width * height * 4)
    gdi32.GetDIBits(mem, bitmap, 0, height, buffer, ctypes.byref(info), 0)

    gdi32.DeleteObject(bitmap)
    gdi32.DeleteDC(mem)
    user32.ReleaseDC(0, screen)

    # BMP stores rows bottom-up unless the height is negative; keep it simple and
    # write a 32-bit top-down bitmap.
    header = struct.pack(
        "<2sIHHI", b"BM", 14 + 40 + len(buffer.raw), 0, 0, 14 + 40
    )
    dib = struct.pack(
        "<IiiHHIIiiII",
        40, width, -height, 1, 32, 0, len(buffer.raw), 2835, 2835, 0, 0
    )
    with open(path, "wb") as handle:
        handle.write(header)
        handle.write(dib)
        handle.write(buffer.raw)
    return width, height


if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "screen.bmp"
    print(grab(target))
