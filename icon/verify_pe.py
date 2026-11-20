# -*- coding: utf-8 -*-
"""解析 PE 资源表，校验 PowerWeb.exe 里的 RT_GROUP_ICON / RT_ICON，
并把资源里的图标重新组装成 .ico，供肉眼确认（不依赖 .NET）。"""
import os
import struct
import sys

OUT = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(OUT)


def read_pe(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:2] != b"MZ":
        raise SystemExit("not a PE")
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew:e_lfanew + 4] == b"PE\0\0"
    coff = e_lfanew + 4
    num_sec = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96)
    res_rva, res_size = struct.unpack_from("<II", data, dd + 2 * 8)
    sec = opt + opt_size
    sections = []
    for i in range(num_sec):
        o = sec + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("latin1")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        sections.append((name, vaddr, max(vsize, rawsize), rawptr, rawsize))
    return data, res_rva, res_size, sections


def rva2off(rva, sections):
    for name, va, size, ptr, rawsize in sections:
        if va <= rva < va + size:
            return ptr + (rva - va)
    raise SystemExit("rva 0x%x 不在任何段内" % rva)


def walk(data, res_off, sections, res_size):
    """返回 {(type_id, name_id, lang_id): (offset, size)}"""
    out = {}
    base = res_off

    def entries(dir_rel):
        p = base + dir_rel
        named, ids = struct.unpack_from("<HH", data, p + 12)
        n = named + ids
        res = []
        for i in range(n):
            name, off = struct.unpack_from("<II", data, p + 16 + i * 8)
            res.append((name, off))
        return res

    for tname, toff in entries(0):
        for nname, noff in entries(toff & 0x7FFFFFFF):
            for lname, loff in entries(noff & 0x7FFFFFFF):
                d = base + (loff & 0x7FFFFFFF)
                rva, size, cp = struct.unpack_from("<III", data, d)
                out[(tname, nname, lname)] = (rva2off(rva, sections), size)
    return out


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "PowerWeb.exe")
    data, res_rva, res_size, sections = read_pe(exe)
    res_off = rva2off(res_rva, sections)
    res = walk(data, res_off, sections, res_size)

    types = {}
    for (t, n, l) in res:
        types.setdefault(t, []).append((n, l))
    print("资源类型统计：", {k: len(v) for k, v in sorted(types.items())})
    print("RT_ICON(3)  :", sorted(n for n, _ in types.get(3, [])))
    print("RT_GROUP(14):", sorted(n for n, _ in types.get(14, [])))

    # 语言 ID 不确定（windres 用 1033，UpdateResource 用 0），按类型+ID 找
    cands = [k for k in res if k[0] == 14 and k[1] == 1]
    if not cands:
        raise SystemExit("没有找到 RT_GROUP_ICON id=1")
    goff, gsize = res[cands[0]]
    grp = data[goff:goff + gsize]
    rsv, typ, cnt = struct.unpack_from("<HHH", grp, 0)
    print("组图标条目数：", cnt)

    blobs = []
    for i in range(cnt):
        w, h, cc, rr, planes, bits, size, rid = struct.unpack_from(
            "<BBBBHHIH", grp, 6 + i * 14)
        ikeys = [k for k in res if k[0] == 3 and k[1] == rid]
        if not ikeys:
            raise SystemExit("缺少 RT_ICON id=%d" % rid)
        ioff, isize = res[ikeys[0]]
        if isize != size:
            raise SystemExit("RT_ICON %d 大小不一致 %d != %d" % (rid, isize, size))
        blobs.append((w or 256, h or 256, data[ioff:ioff + isize]))

    # 重新组装 ico
    head = struct.pack("<HHH", 0, 1, len(blobs))
    off = 6 + 16 * len(blobs)
    body = b""
    for w, h, blob in blobs:
        head += struct.pack("<BBBBHHII", 0 if w == 256 else w, 0 if h == 256 else h,
                            0, 0, 1, 32, len(blob), off)
        off += len(blob)
        body += blob
    rebuilt = os.path.join(OUT, "rebuilt-from-exe.ico")
    with open(rebuilt, "wb") as f:
        f.write(head + body)
    print("重建 ico ->", rebuilt, len(head + body), "bytes")
    print("尺寸：", [w for w, _, _ in blobs])

    # 把 256 那张 PNG 条目直接落盘，便于目视
    for w, h, blob in blobs:
        if blob[:8] == b"\x89PNG\r\n\x1a\n":
            p = os.path.join(OUT, "from-exe-%d.png" % w)
            with open(p, "wb") as f:
                f.write(blob)
            print("导出 ->", p)


if __name__ == "__main__":
    main()
