#!/usr/bin/env python3
"""Gera resources/icons/loudmixer.ico e os PNGs do ícone sem dependências
externas (rasterização simples com supersampling). Reproduz o desenho de
src/ui/AppIcons.cpp / resources/icons/loudmixer.svg.

Uso: python3 tools/make_icons.py
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "resources", "icons")
BLUE = (0x1F, 0x6F, 0xEB)
WHITE = (0xFF, 0xFF, 0xFF)
AMBER = (0xFF, 0xB0, 0x20)


def inside_round_rect(x, y):
    x0, y0, x1, y1, r = 1, 1, 31, 31, 7
    if not (x0 <= x <= x1 and y0 <= y <= y1):
        return False
    cx = min(max(x, x0 + r), x1 - r)
    cy = min(max(y, y0 + r), y1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def inside_speaker(x, y):
    if 6 <= x <= 10.5 and 12.5 <= y <= 19.5:
        return True
    if 10.5 <= x <= 16:
        t = (x - 10.5) / 5.5  # de 12.5..19.5 até 7.5..24.5
        return 12.5 - 5 * t <= y <= 19.5 + 5 * t
    return False


def on_arc(x, y, cx, cy, r, width=2.2, span=50):
    d = math.hypot(x - cx, y - cy)
    ang = math.degrees(math.atan2(y - cy, x - cx))
    return abs(d - r) <= width / 2 and -span <= ang <= span


def color_at(x, y):
    if not inside_round_rect(x, y):
        return None
    if inside_speaker(x, y) or on_arc(x, y, 14.2, 16, 5.0):
        return WHITE
    if on_arc(x, y, 14.6, 16, 9.3):
        return AMBER
    return BLUE


def render(size, ss=4):
    rows = []
    for py in range(size):
        row = bytearray()
        for px in range(size):
            acc = [0, 0, 0, 0]
            for sy in range(ss):
                for sx in range(ss):
                    x = (px + (sx + 0.5) / ss) * 32 / size
                    y = (py + (sy + 0.5) / ss) * 32 / size
                    c = color_at(x, y)
                    if c:
                        acc[0] += c[0]; acc[1] += c[1]; acc[2] += c[2]; acc[3] += 255
            n = ss * ss
            a = acc[3] // n
            if a:
                row += bytes([acc[0] * 255 // acc[3], acc[1] * 255 // acc[3], acc[2] * 255 // acc[3], a])
            else:
                row += b"\0\0\0\0"
        rows.append(bytes(row))
    return rows


def png(size):
    raw = b"".join(b"\0" + r for r in render(size))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    sizes = [16, 24, 32, 48, 64, 128, 256]
    images = {s: png(s) for s in sizes}
    for s in (48, 128, 256):
        with open(os.path.join(OUT, f"loudmixer-{s}.png"), "wb") as f:
            f.write(images[s])
    # ICO com entradas PNG (suportado desde o Windows Vista).
    header = struct.pack("<HHH", 0, 1, len(sizes))
    offset = 6 + 16 * len(sizes)
    entries, blobs = b"", b""
    for s in sizes:
        data = images[s]
        entries += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
        blobs += data
    with open(os.path.join(OUT, "loudmixer.ico"), "wb") as f:
        f.write(header + entries + blobs)
    print("ícones gerados em", OUT)


if __name__ == "__main__":
    main()
