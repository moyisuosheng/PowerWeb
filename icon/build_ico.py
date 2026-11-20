# -*- coding: utf-8 -*-
"""
把渲染结果打包成多尺寸 .ico，并把图标资源写进 PowerWeb.exe。

ICO 结构：16~64 用 BMP(DIB) 条目（兼容性最好），128/256 用 PNG 条目（体积小）。
资源写入用 Win32 UpdateResource，等价于 rc 编译进 .rsrc 段，但不需要 windres。
"""
import ctypes
import os
import struct
import sys
from ctypes import wintypes

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_icon as ri

OUT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(OUT)

SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
PNG_FROM = 128          # >= 该尺寸用 PNG 压缩条目


def bmp_entry(cv):
    n = cv.n
    hdr = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, 32, 0,
                      n * n * 4, 0, 0, 0, 0)
    xor = bytearray()
    for y in range(n - 1, -1, -1):
        base = y * n * 4
        for x in range(n):
            i = base + x * 4
            r, g, b, a = cv.buf[i], cv.buf[i + 1], cv.buf[i + 2], cv.buf[i + 3]
            xor += bytes((int(b * 255 + 0.5), int(g * 255 + 0.5),
                          int(r * 255 + 0.5), int(a * 255 + 0.5)))
    mask_row = ((n + 31) // 32) * 4
    mask = b"\x00" * (mask_row * n)
    return hdr + bytes(xor) + mask


def build_ico(path, cv_by_size):
    entries = []
    blobs = []
    offset = 6 + 16 * len(SIZES)
    for n in SIZES:
        cv = cv_by_size[n]
        if n >= PNG_FROM:
            data = cv.png_bytes()
        else:
            data = bmp_entry(cv)
        entries.append(struct.pack("<BBBBHHII",
                                   0 if n == 256 else n,
                                   0 if n == 256 else n,
                                   0, 0, 1, 32, len(data), offset))
        blobs.append(data)
        offset += len(data)
    with open(path, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(SIZES)))
        for e in entries:
            f.write(e)
        for b in blobs:
            f.write(b)
    return path


# ---------------- PE 资源写入 ----------------
RT_ICON, RT_GROUP_ICON = 3, 14
LANG = 0


def ico_to_resources(ico_bytes):
    """解析 .ico，返回 (icons[(id, bytes)], group_bytes)"""
    reserved, typ, count = struct.unpack_from("<HHH", ico_bytes, 0)
    assert typ == 1, "not an ico"
    icons = []
    grp = [struct.pack("<HHH", 0, 1, count)]
    for k in range(count):
        w, h, cc, rsv, planes, bits, size, off = struct.unpack_from(
            "<BBBBHHII", ico_bytes, 6 + 16 * k)
        data = ico_bytes[off:off + size]
        rid = k + 1
        icons.append((rid, data))
        grp.append(struct.pack("<BBBBHHIH", w, h, cc, rsv, planes, bits, size, rid))
    return icons, b"".join(grp)


def embed_icon(exe_path, ico_bytes):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.BeginUpdateResourceW.restype = wintypes.HANDLE
    k32.BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    k32.UpdateResourceW.restype = wintypes.BOOL
    k32.UpdateResourceW.argtypes = [wintypes.HANDLE, ctypes.c_void_p,
                                    ctypes.c_void_p, wintypes.WORD,
                                    ctypes.c_void_p, wintypes.DWORD]
    k32.EndUpdateResourceW.restype = wintypes.BOOL
    k32.EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]

    icons, grp = ico_to_resources(ico_bytes)
    h = k32.BeginUpdateResourceW(exe_path, False)
    if not h:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        for rid, data in icons:
            buf = ctypes.create_string_buffer(data, len(data))
            if not k32.UpdateResourceW(h, ctypes.c_void_p(RT_ICON),
                                       ctypes.c_void_p(rid), LANG,
                                       ctypes.cast(buf, ctypes.c_void_p), len(data)):
                raise ctypes.WinError(ctypes.get_last_error())
        buf = ctypes.create_string_buffer(grp, len(grp))
        if not k32.UpdateResourceW(h, ctypes.c_void_p(RT_GROUP_ICON),
                                   ctypes.c_void_p(1), LANG,
                                   ctypes.cast(buf, ctypes.c_void_p), len(grp)):
            raise ctypes.WinError(ctypes.get_last_error())
    except Exception:
        k32.EndUpdateResourceW(h, True)   # 丢弃改动
        raise
    if not k32.EndUpdateResourceW(h, False):
        raise ctypes.WinError(ctypes.get_last_error())
    return len(icons)


def main():
    exe = os.path.join(ROOT, "PowerWeb.exe")
    bak = exe + ".bak"
    ico = os.path.join(OUT, "PowerWeb.ico")

    # 每次从原始备份还原，保证可重复、不叠加资源
    if os.path.exists(bak):
        import shutil
        shutil.copy2(bak, exe)

    # 主图标（纯电源符号）
    cvs = {n: ri.render(n) for n in SIZES}
    build_ico(ico, cvs)
    print("ico  ->", ico, os.path.getsize(ico), "bytes")

    with open(ico, "rb") as f:
        data = f.read()
    n = embed_icon(exe, data)
    print("exe  ->", exe, os.path.getsize(exe), "bytes, icons:", n)


if __name__ == "__main__":
    main()
