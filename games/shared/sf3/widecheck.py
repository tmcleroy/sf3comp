#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
"""Judges checkwide.sh's pictures: widecheck.py DIR [WIDTH [EXCUSED [FEW]]]. Each DIR/<run>/p*.png must be WIDTH columns wide (512, the
sources' own widescreen, unless said: 495 is the board's own wide picture), and no column of its margins (the columns
past the board's 384, half at each side) may have 40 lines of black together below the HUD: a column so is a layer of
the stage that ends short of the picture's edge. EXCUSED: runs (comma-separated) whose black columns are the stage's own
art, said but not counted. FEW: so many empty columns or fewer are said but not counted (the board's own wide picture:
a far layer whose art ends a column or few short of the game's own camera limits, as the board shows it). Prints a
line a fault and a count; exits 1 on a fault."""

import glob
import os
import struct
import sys
import zlib


def read_png(path):
    data = open(path, "rb").read()
    at, chunks, width, height, depth, kind = 8, [], 0, 0, 0, 0
    while at < len(data):
        size, tag = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + size]
        if tag == b"IHDR":
            width, height, depth, kind = struct.unpack(">IIBB", body[:10])
        elif tag == b"IDAT":
            chunks.append(body)
        at += 12 + size
    bpp = {2: 3, 6: 4}[kind]
    raw = zlib.decompress(b"".join(chunks))
    rows, prev, stride = [], bytearray(width * bpp), width * bpp
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, bpp, rows


def main():
    out = sys.argv[1]
    want = int(sys.argv[2]) if len(sys.argv) > 2 else 512
    margin = (want - 384) // 2
    excused = set(sys.argv[3].split(",")) if len(sys.argv) > 3 and sys.argv[3] else set()
    few = int(sys.argv[4]) if len(sys.argv) > 4 else 0
    faults = seen = 0
    for path in sorted(glob.glob(os.path.join(out, "*", "p*.png"))):
        name = os.path.basename(os.path.dirname(path))
        width, height, bpp, rows = read_png(path)
        seen += 1
        if width != want:
            print("%s: the picture is %d columns, not %d" % (name, width, want))
            faults += 1
            continue
        # (a column of a margin with 40 lines or more of black together, below the HUD: a layer that ends there)
        empty = []
        for x in list(range(margin)) + list(range(want - margin, want)):
            run = most = 0
            for y in range(40, 208):
                run = run + 1 if bytes(rows[y][x * bpp:x * bpp + 3]) == b"\0\0\0" else 0
                most = max(most, run)
            if most >= 40:
                empty.append(x)
        if empty:
            left = sum(1 for x in empty if x < margin)
            why = " (the stage's own black art)" if name in excused else " (the game's own, as the board shows it)" if len(empty) <= few else ""
            print("%s: %d columns of the margins with nothing drawn (%d at the left, %d at the right)%s" %
                  (name, len(empty), left, len(empty) - left, why))
            faults += not why
    print("widescreen: %d pictures, %d with a fault" % (seen, faults))
    return 1 if faults or not seen else 0


if __name__ == "__main__":
    sys.exit(main())
