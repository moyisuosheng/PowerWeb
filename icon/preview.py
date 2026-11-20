# -*- coding: utf-8 -*-
"""对照预览图：原生尺寸 + 小尺寸放大检视（浅底/深底）。"""
import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_icon as ri

OUT = os.path.dirname(os.path.abspath(__file__))


class Sheet:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.buf = [0.0] * (w * h * 4)

    def fill(self, r, g, b):
        for i in range(self.w * self.h):
            self.buf[i * 4] = r
            self.buf[i * 4 + 1] = g
            self.buf[i * 4 + 2] = b
            self.buf[i * 4 + 3] = 1.0

    def rect(self, x0, y0, x1, y1, r, g, b):
        for y in range(max(0, y0), min(self.h, y1)):
            for x in range(max(0, x0), min(self.w, x1)):
                i = (y * self.w + x) * 4
                self.buf[i] = r
                self.buf[i + 1] = g
                self.buf[i + 2] = b

    def blit(self, cv, x0, y0, scale=1):
        n = cv.n
        for y in range(n * scale):
            gy = y0 + y
            if not (0 <= gy < self.h):
                continue
            for x in range(n * scale):
                gx = x0 + x
                if not (0 <= gx < self.w):
                    continue
                si = ((y // scale) * n + (x // scale)) * 4
                a = cv.buf[si + 3]
                if a <= 0:
                    continue
                di = (gy * self.w + gx) * 4
                self.buf[di] = cv.buf[si] * a + self.buf[di] * (1 - a)
                self.buf[di + 1] = cv.buf[si + 1] * a + self.buf[di + 1] * (1 - a)
                self.buf[di + 2] = cv.buf[si + 2] * a + self.buf[di + 2] * (1 - a)

    def png(self, path):
        raw = bytearray()
        for y in range(self.h):
            raw.append(0)
            row = y * self.w * 4
            for x in range(self.w):
                i = row + x * 4
                raw.append(int(ri.clamp(self.buf[i]) * 255 + 0.5))
                raw.append(int(ri.clamp(self.buf[i + 1]) * 255 + 0.5))
                raw.append(int(ri.clamp(self.buf[i + 2]) * 255 + 0.5))
                raw.append(255)
        comp = zlib.compress(bytes(raw), 9)

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        ihdr = struct.pack(">IIBBBBB", self.w, self.h, 8, 6, 0, 0, 0)
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                    + chunk(b"IDAT", comp) + chunk(b"IEND", b""))


def build(path=None, **kw):
    cvs = {s: ri.render(s, **kw) for s in (16, 24, 32, 48, 64, 128, 256)}
    W, H = 780, 400
    sh = Sheet(W, H)

    # 浅底区
    sh.rect(0, 0, W, 130, 0.95, 0.955, 0.965)
    # 深底区
    sh.rect(0, 130, W, H, 0.075, 0.085, 0.105)

    # 第一排：原生尺寸（浅底）
    x = 22
    y = 40
    for s in (16, 24, 32, 48, 64):
        sh.blit(cvs[s], x, y, 1)
        x += s + 24
    sh.blit(cvs[128], 470, 2, 1)
    sh.blit(cvs[256], W - 262, 0, 1)

    # 第二排：小尺寸放大（深底）
    x = 24
    y = 170
    for s, sc in ((16, 6), (24, 5), (32, 4), (48, 3)):
        sh.blit(cvs[s], x, y, sc)
        x += s * sc + 22

    sh.png(path or os.path.join(OUT, "preview.png"))
    return path or os.path.join(OUT, "preview.png")


if __name__ == "__main__":
    print(build())
