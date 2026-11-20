# -*- coding: utf-8 -*-
"""
PowerWeb 图标渲染器 —— 纯 Python（无第三方库）

SDF（有符号距离场）抗锯齿 + 逐层合成，输出多尺寸 PNG。
设计：深蓝玻璃底 + 翠绿电源符号（圆环缺口 + 竖杆），环内叠极淡经纬线
      暗示 "Web / 网络"；小尺寸自动降级（去经纬线、加粗笔画）。
"""
import math
import os
import struct
import zlib

OUT = os.path.dirname(os.path.abspath(__file__))


def clamp(x, a=0.0, b=1.0):
    return a if x < a else (b if x > b else x)


def smoothstep(e0, e1, x):
    t = clamp((x - e0) / (e1 - e0))
    return t * t * (3 - 2 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def mix3(c0, c1, t):
    return (lerp(c0[0], c1[0], t), lerp(c0[1], c1[1], t), lerp(c0[2], c1[2], t))


def sd_round_rect(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    ax, ay = max(qx, 0.0), max(qy, 0.0)
    return math.hypot(ax, ay) + min(max(qx, qy), 0.0) - r


def sd_capsule(px, py, ax, ay, bx, by, r):
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    d = bax * bax + bay * bay
    h = 0.0 if d == 0 else clamp((pax * bax + pay * bay) / d)
    return math.hypot(pax - bax * h, pay - bay * h) - r


def sd_ellipse(px, py, cx, cy, rx, ry):
    dx = (px - cx) / rx
    dy = (py - cy) / ry
    return (math.hypot(dx, dy) - 1.0) * min(rx, ry)


class Canvas:
    def __init__(self, size):
        self.n = size
        self.buf = [0.0] * (size * size * 4)

    def blend(self, idx, r, g, b, cov):
        if cov <= 0.0:
            return
        if cov > 1.0:
            cov = 1.0
        buf = self.buf
        da = buf[idx + 3]
        oa = cov + da * (1 - cov)
        if oa <= 1e-6:
            return
        k = da * (1 - cov)
        buf[idx] = (r * cov + buf[idx] * k) / oa
        buf[idx + 1] = (g * cov + buf[idx + 1] * k) / oa
        buf[idx + 2] = (b * cov + buf[idx + 2] * k) / oa
        buf[idx + 3] = oa

    def add(self, idx, r, g, b, amt):
        if amt <= 0.0:
            return
        buf = self.buf
        buf[idx] = min(1.0, buf[idx] + r * amt)
        buf[idx + 1] = min(1.0, buf[idx + 1] + g * amt)
        buf[idx + 2] = min(1.0, buf[idx + 2] + b * amt)
        buf[idx + 3] = min(1.0, buf[idx + 3] + amt * 0.85)

    def png_bytes(self):
        n = self.n
        raw = bytearray()
        buf = self.buf
        prev = bytearray(n * 4)
        for y in range(n):
            cur = bytearray()
            row = y * n * 4
            for x in range(n * 4):
                cur.append(int(clamp(buf[row + x]) * 255 + 0.5))
            if y == 0:
                raw.append(0)
                raw += cur
            else:
                # Up 过滤：与上一行做差，渐变/柔光能压得很小
                raw.append(2)
                raw += bytes((cur[i] - prev[i]) & 0xFF for i in range(n * 4))
            prev = cur
        comp = zlib.compress(bytes(raw), 9)

        def chunk(tag, data):
            return (struct.pack(">I", len(data)) + tag + data
                    + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

        ihdr = struct.pack(">IIBBBBB", n, n, 8, 6, 0, 0, 0)
        return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr)
                + chunk(b"IDAT", comp) + chunk(b"IEND", b""))


C_TOP = (0.106, 0.157, 0.263)     # #1b2843
C_MID = (0.047, 0.075, 0.129)     # #0c1321
C_BOT = (0.020, 0.031, 0.055)     # #050810
EM_TOP = (0.560, 0.965, 0.800)    # #8ff6cc
EM_BOT = (0.043, 0.686, 0.514)    # #0baf83
GLOW = (0.063, 0.725, 0.506)      # #10b981


def render(size, variant="primary", globe=None, deco=None, dot_alpha=0.13):
    """variant: primary | flat | mono
    globe: None=默认，或 (alpha, line_ratio, meridian_rx, equator_ry, scale, dy)
    deco:  None | "dots"（底板点阵，暗示局域网/节点）
    """
    n = size
    cv = Canvas(n)
    S = float(n)

    margin = 0.030 if n <= 20 else (0.045 if n <= 32 else 0.055)
    radius = 0.250 if n <= 32 else 0.225
    bw = 0.5 - margin

    cx, cy = 0.5, 0.545
    if n <= 20:
        R = 0.245
    elif n <= 32:
        R = 0.228
    else:
        R = 0.212
    if n <= 20:
        wpx = 2.0
    elif n <= 24:
        wpx = 2.2
    elif n <= 32:
        wpx = 2.6
    else:
        wpx = max(2.4, 0.0735 * S)
    w = wpx / S
    stem_top = cy - 0.352
    gap_deg = 47.0 if n <= 24 else (44.0 if n <= 32 else 42.0)
    detail = n >= 40 and variant != "mono"

    # 经纬线参数：(alpha, 线宽比, 经线 rx 比, 纬线 ry 比, 球半径比, 竖直偏移)
    g_alpha, g_lw, g_rx, g_ry, g_sc, g_dy = (
        (0.0, 0.0090, 0.52, 0.32, 1.0, 0.0) if globe is None else globe)

    if g_sc < 0.999:
        # 小地球布局：竖杆末梢正好落在球的北极
        Ri_ = R - w * 0.5 - 0.010
        stem_bot = cy + g_dy - Ri_ * g_sc + w * 0.30
    else:
        stem_bot = cy - 0.032

    top_c, mid_c, bot_c = C_TOP, C_MID, C_BOT
    if variant == "flat":
        top_c = (0.086, 0.639, 0.478)
        mid_c = (0.035, 0.400, 0.310)
        bot_c = (0.016, 0.243, 0.200)

    gap_rad = math.radians(gap_deg)
    caps = []
    for sgn in (-1, 1):
        ea = math.radians(90.0 + sgn * gap_deg)
        caps.append((cx + math.cos(ea) * R, cy - math.sin(ea) * R))

    for py_i in range(n):
        py = (py_i + 0.5) / S
        for px_i in range(n):
            px = (px_i + 0.5) / S
            idx = (py_i * n + px_i) * 4

            # ===== 1. 底板 =====
            d_badge = sd_round_rect(px, py, cx, cy, bw, bw, radius)
            cov = clamp(0.5 - d_badge * S)
            if cov <= 0.0:
                continue

            t = clamp((py - margin) / (1 - 2 * margin))
            if t < 0.55:
                col = mix3(top_c, mid_c, t / 0.55)
            else:
                col = mix3(mid_c, bot_c, (t - 0.55) / 0.45)

            g1 = math.exp(-(((px - 0.26) ** 2 + (py - 0.08) ** 2) / 0.070))
            col = (col[0] + 0.050 * g1, col[1] + 0.080 * g1, col[2] + 0.145 * g1)
            g2 = math.exp(-(((px - 0.95) ** 2 + (py - 1.05) ** 2) / 0.100))
            col = (col[0] + 0.010 * g2, col[1] + 0.070 * g2, col[2] + 0.042 * g2)

            cv.blend(idx, col[0], col[1], col[2], cov)

            # 内描边：上亮下暗
            if n >= 40:
                d_in = sd_round_rect(px, py, cx, cy, bw - 0.005, bw - 0.005,
                                     radius - 0.003)
                edge = clamp(0.5 - abs(d_in) * S * 1.5)
                if edge > 0:
                    cv.blend(idx, 1.0, 1.0, 1.0, edge * 0.13 * smoothstep(1.0, 0.40, py))

            # 点阵：暗示局域网 / 节点，越靠边缘越淡
            if deco == "dots" and n >= 40:
                pitch = 1.0 / 16.0
                fx = px / pitch - 0.5
                fy = py / pitch - 0.5
                ddx = (fx - round(fx)) * pitch
                ddy = (fy - round(fy)) * pitch
                dd = math.hypot(ddx, ddy) - 0.0048
                dc = clamp(0.5 - dd * S)
                if dc > 0:
                    rad = math.hypot(px - cx, py - cy)
                    fade = clamp(1.0 - (rad / 0.62) ** 2)
                    a = dc * dot_alpha * fade
                    if a > 0:
                        cv.blend(idx, 0.55, 0.95, 0.85, a * cov)

            # ===== 2. 符号距离场 =====
            dx, dy = px - cx, py - cy
            dist = math.hypot(dx, dy)
            d_ring = abs(dist - R) - w * 0.5

            ang = math.degrees(math.atan2(-dy, dx))
            delta = abs(((ang - 90.0 + 180.0) % 360.0) - 180.0)

            if delta > gap_deg:
                d_arc = d_ring
            else:
                d_arc = max(d_ring, dist * math.sin(gap_rad - math.radians(delta)))
            for capx, capy in caps:
                dc = math.hypot(px - capx, py - capy) - w * 0.5
                if dc < d_arc:
                    d_arc = dc

            d_stem = sd_capsule(px, py, cx, stem_top, cx, stem_bot, w * 0.5)
            d_glyph = min(d_arc, d_stem)

            # ===== 3. 光晕 =====
            if n >= 24 and variant != "mono":
                gl = math.exp(-max(d_glyph, 0.0) / (0.050 if n > 32 else 0.070))
                cv.add(idx, GLOW[0], GLOW[1], GLOW[2], gl * 0.30 * cov)

            # ===== 4. 符号本体 =====
            gc = clamp(0.5 - d_glyph * S)
            if gc > 0:
                if variant == "mono":
                    col2 = (1.0, 1.0, 1.0)
                else:
                    tt = clamp((py - (stem_top - w)) / ((cy + R + w) - (stem_top - w)))
                    col2 = mix3(EM_TOP, EM_BOT, tt ** 0.9)
                    hi = math.exp(-((py - stem_top) ** 2) / 0.005)
                    col2 = (min(1.0, col2[0] + 0.09 * hi),
                            min(1.0, col2[1] + 0.07 * hi),
                            min(1.0, col2[2] + 0.05 * hi))
                cv.blend(idx, col2[0], col2[1], col2[2], gc)

            # ===== 5. 经线 / 纬线：环内补一颗小地球 =====
            if detail and g_alpha > 0:
                Ri = R - w * 0.5 - 0.010
                Rg = Ri * g_sc
                gy = cy + g_dy
                lw = max(1.0, g_lw * S) / S             # 归一化线宽
                d1 = abs(sd_ellipse(px, py, cx, gy, Rg * g_rx, Rg * 0.995))
                d2 = abs(sd_ellipse(px, py, cx, gy, Rg * 0.995, Rg * g_ry))
                dl = min(d1, d2) - lw * 0.5
                if dl < 0.02:
                    lc = clamp(0.5 - dl * S) * g_alpha
                    if lc > 0:
                        cv.blend(idx, 0.62, 0.99, 0.87, lc)

    return cv


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
    os.makedirs(os.path.join(OUT, "png"), exist_ok=True)
    for s in sizes:
        with open(os.path.join(OUT, "png", "powerweb-%d.png" % s), "wb") as f:
            f.write(render(s).png_bytes())
        print("  %d ok" % s)
    for v in ("flat", "mono"):
        with open(os.path.join(OUT, "png", "powerweb-256-%s.png" % v), "wb") as f:
            f.write(render(256, variant=v).png_bytes())
    print("rendered")


if __name__ == "__main__":
    main()
