#!/usr/bin/env python3
"""Pictures of what the firmware draws on the OLED. Runs build/screens (test/screens.c: logic.c ui_lines() ->
oled.c -> a model of the panel RAM) and writes docs/images/screen-<name>.png, drawn at the panel's real pixel pitch
(0.18 mm columns x 0.20 mm rows, 0.16 x 0.18 mm lit area, N069 datasheet s1.2) with white pixels on dark glass.

Usage: tools/screens.py [OUT_DIR]   (make screens)"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
FW = os.path.dirname(HERE)
SCALE = 50                     # px per mm
COL_P, ROW_P = 0.18, 0.20      # pixel pitch
COL_W, ROW_H = 0.16, 0.18      # lit size
MARGIN = 1.0                   # mm of glass around the active area
GLASS, LIT = (12, 13, 16), (235, 240, 255)


def frames():
    out = subprocess.run([os.path.join(FW, 'build', 'screens')], check=True, capture_output=True, text=True).stdout
    lines = out.splitlines()
    for i in range(0, len(lines), 3):
        name, r0, r1 = lines[i].split('|')
        pages = [bytes.fromhex(lines[i + 1]), bytes.fromhex(lines[i + 2])]
        yield name, (r0, r1), pages


def lit(pages, col, row):
    return (pages[row >> 3][col] >> (row & 7)) & 1


def png(path, w, h, rows):
    raw = b''.join(b'\x00' + bytes(r) for r in rows)
    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def render(pages, path):
    w = int(round((96 * COL_P + 2 * MARGIN) * SCALE))
    h = int(round((16 * ROW_P + 2 * MARGIN) * SCALE))
    m = MARGIN * SCALE
    img = [bytearray(GLASS * w) for _ in range(h)]
    for row in range(16):
        y0 = int(round(m + row * ROW_P * SCALE)); y1 = int(round(m + (row * ROW_P + ROW_H) * SCALE))
        for col in range(96):
            if not lit(pages, col, row):
                continue
            x0 = int(round(m + col * COL_P * SCALE)); x1 = int(round(m + (col * COL_P + COL_W) * SCALE))
            for y in range(y0, y1):
                img[y][3 * x0:3 * x1] = bytes(LIT * (x1 - x0))
    png(path, w, h, img)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(FW, '..', 'docs', 'images')
    os.makedirs(out, exist_ok=True)
    for name, text, pages in frames():
        p = os.path.join(out, 'screen-%s.png' % name)
        render(pages, p)
        print('wrote %s  [%s] [%s]' % (os.path.relpath(p), text[0], text[1]))


if __name__ == '__main__':
    main()
