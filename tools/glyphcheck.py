#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""检查 src 里的字符串字面量有没有 Nexus 字体画不出来的字符。

背景：Nexus 用 addons\\Nexus\\Fonts\\<UserFont> 当 ImGui 字体（默认 arcdps_font.ttf），
Nexus 只给常用范围建字形。字面量里出现范围外的字符（比如箭头 →）时，面板上会显示成 “?”。

用法（在项目根目录）：
    python tools/glyphcheck.py [字体路径] [源文件...]

退出码 0 = 没问题；1 = 有会被画成 “?” 的字符。
"""

import collections
import os
import struct
import sys

DEFAULT_FONT = os.path.join(
    "D:", "Games", "Guild_Wars_2", "addons", "Nexus", "Fonts", "arcdps_font.ttf")
DEFAULT_SOURCES = [os.path.join("src", "MarkerCycle.cpp")]

# Nexus 给 ImGui 建字形的范围：拉丁 + 常用标点 + 中文 + 全角符号。
# 箭头 (U+2190-21FF)、数学符号、emoji 都不在里面，一律会被画成 “?”。
ALLOWED_RANGES = [
    (0x0000, 0x007F),  # ASCII
    (0x2000, 0x206F),  # 常用标点：— “ ” … ·
    (0x3000, 0x303F),  # 中日韩标点：、。「」《》
    (0x4E00, 0x9FFF),  # 汉字
    (0xFE30, 0xFE4F),  # 兼容形式
    (0xFF00, 0xFFEF),  # 全角：，：；（）！？
]


# ------------------------------------------------------------------ 字体 cmap

def font_offsets(d):
    if d[:4] == b"ttcf":
        n = struct.unpack(">I", d[8:12])[0]
        return [struct.unpack(">I", d[12 + 4 * i:16 + 4 * i])[0] for i in range(n)]
    return [0]


def table_dir(d, foff):
    num_tables = struct.unpack(">H", d[foff + 4:foff + 6])[0]
    tables = {}
    for i in range(num_tables):
        off = foff + 12 + i * 16
        tag = d[off:off + 4].decode("latin1")
        toff, tlen = struct.unpack(">II", d[off + 8:off + 16])
        tables[tag] = (toff, tlen)
    return tables


def parse_cmap_subtable(d, p, fmt):
    codes = set()
    if fmt == 4:
        seg_x2 = struct.unpack(">H", d[p + 6:p + 8])[0]
        seg = seg_x2 // 2
        ends = struct.unpack(">%dH" % seg, d[p + 14:p + 14 + seg_x2])
        starts = struct.unpack(">%dH" % seg, d[p + 16 + seg_x2:p + 16 + 2 * seg_x2])
        deltas = struct.unpack(">%dh" % seg, d[p + 16 + 2 * seg_x2:p + 16 + 3 * seg_x2])
        ro_pos = p + 16 + 3 * seg_x2
        roffs = struct.unpack(">%dH" % seg, d[ro_pos:ro_pos + seg_x2])
        for s in range(seg):
            for c in range(starts[s], min(ends[s], 0xFFFF) + 1):
                if roffs[s] == 0:
                    g = (c + deltas[s]) & 0xFFFF
                else:
                    gp = ro_pos + 2 * s + roffs[s] + 2 * (c - starts[s])
                    if gp + 2 > len(d):
                        continue
                    g = struct.unpack(">H", d[gp:gp + 2])[0]
                    if g:
                        g = (g + deltas[s]) & 0xFFFF
                if g:
                    codes.add(c)
    elif fmt == 12:
        ng = struct.unpack(">I", d[p + 12:p + 16])[0]
        for i in range(ng):
            q = p + 16 + i * 12
            st, en, _ = struct.unpack(">III", d[q:q + 12])
            codes.update(range(st, en + 1))
    return codes


def cmap_codes(path):
    d = open(path, "rb").read()
    fonts = []
    for foff in font_offsets(d):
        tables = table_dir(d, foff)
        if "cmap" not in tables:
            fonts.append(set())
            continue
        base = tables["cmap"][0]
        n = struct.unpack(">H", d[base + 2:base + 4])[0]
        best = None
        for i in range(n):
            rec = base + 4 + i * 8
            off = struct.unpack(">I", d[rec + 4:rec + 8])[0]
            p = base + off
            fmt = struct.unpack(">H", d[p:p + 2])[0]
            if fmt in (4, 12):
                best = (fmt, p)
        fonts.append(parse_cmap_subtable(d, best[1], best[0]) if best else set())
    return fonts


# ------------------------------------------------------------------ 源码扫描

def extract_literals(src):
    """返回 [(行号, 字面量内容), ...]。手写扫描，避免正则转义踩坑。"""
    out = []
    i, n, line = 0, len(src), 1
    while i < n:
        ch = src[i]
        if ch == "\n":
            line += 1
            i += 1
        elif src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += src.count("\n", i, j)
            i = j
        elif ch == '"':
            buf = []
            j = i + 1
            while j < n and src[j] != '"':
                if src[j] == "\\" and j + 1 < n:
                    buf.append(src[j:j + 2])
                    j += 2
                else:
                    buf.append(src[j])
                    j += 1
            out.append((line, "".join(buf)))
            i = j + 1
        elif ch == "'":
            j = i + 1
            while j < n and src[j] != "'":
                j += 2 if src[j] == "\\" else 1
            i = j + 1
        else:
            i += 1
    return out


def policy_ok(code):
    for lo, hi in ALLOWED_RANGES:
        if lo <= code <= hi:
            return True
    return False


def scan(paths, codes):
    bad = 0
    for path in paths:
        src = open(path, encoding="utf-8").read()
        missing = collections.Counter()
        out_of_policy = collections.Counter()
        first_line = {}
        literals = extract_literals(src)
        for line, lit in literals:
            for ch in lit:
                if ord(ch) < 128:
                    continue
                if ord(ch) not in codes:
                    missing[ch] += 1
                    first_line.setdefault(ch, line)
                if not policy_ok(ord(ch)):
                    out_of_policy[ch] += 1
                    first_line.setdefault(ch, line)
        print("%s：字面量 %d 个" % (path, len(literals)))
        for ch, cnt in missing.most_common():
            print("  字体里没有      U+%04X %r x%d（首次出现在第 %d 行）"
                  % (ord(ch), ch, cnt, first_line[ch]))
        for ch, cnt in out_of_policy.most_common():
            print("  会被画成 “?” 的  U+%04X %r x%d（首次出现在第 %d 行）"
                  % (ord(ch), ch, cnt, first_line[ch]))
        if not missing and not out_of_policy:
            print("  全部字符都有字形，而且都在允许范围内")
        bad += len(missing) + len(out_of_policy)
    return bad


def main():
    argv = sys.argv[1:]
    font = DEFAULT_FONT
    if argv and os.path.exists(argv[0]) and argv[0].lower().endswith((".ttf", ".ttc", ".otf")):
        font = argv.pop(0)
    sources = argv or DEFAULT_SOURCES
    if not os.path.exists(font):
        print("找不到字体：%s" % font)
        return 2
    fonts = cmap_codes(font)
    codes = set().union(*fonts)
    print("字体：%s" % font)
    print("  TTC 内字体数 %d，各字形数 %s，合并 %d"
          % (len(fonts), [len(f) for f in fonts], len(codes)))
    bad = scan(sources, codes)
    if bad:
        print("\n结论：有 %d 种字符会被画成 “?”，请换掉。" % bad)
        return 1
    print("\n结论：没有会被画成 “?” 的字符。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
