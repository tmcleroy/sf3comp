#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checkwide.sh's judge: widescreen switched in the in-game menu while the game runs against a run started with it, as
# the window shows them (CPS3RECOMP_WINDOW_SHOT: the frontend's own picture, read back). Prints a line per pair and "ok"
# or "FAILED: ..."; for a pair that differs, a strip (started | switched | red where they differ) beside its pictures.
#
#   widecheck.py same STARTED_PREFIX SWITCHED_PREFIX FROM [SHAPE]
#       every picture of both from frame FROM: the same size and the same pixels; with SHAPE (16:9 or 4:3) the window
#       that shape too (a window takes its picture's shape)
#   widecheck.py edges STARTED_PREFIX SWITCHED_PREFIX FROM
#       (pictures whose game differs, a stage viewer's camera kept from the stage's ends by the margin): the same size,
#       and the picture as wide in the window: its first and last columns not black the same, within 2
import glob
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from stagecheck import read_png  # noqa: E402


def fail(why):
    print("FAILED: " + why)
    sys.exit(1)


def shots(prefix, start):
    out = {}
    for p in glob.glob(prefix + '*.png'):
        m = re.search(r'(\d+)\.png$', p)
        if m and int(m.group(1)) >= start:
            out[int(m.group(1))] = p
    return out


def strip(a, b, where):
    subprocess.run(["magick", a, b, "(", a, b, "-compose", "difference", "-composite", "-threshold", "0", "-fill", "red", "-opaque", "white", ")",
                    "-resize", "50%", "+append", where], check=False)
    print("  strip: " + where)


def extent(path):
    width, height, step, rows = read_png(path)
    left, right = width, -1
    for y in range(0, height, 4):
        row = rows[y]
        for x in range(width):
            if row[x * step] or row[x * step + 1] or row[x * step + 2]:
                left = min(left, x)
                break
        for x in range(width - 1, -1, -1):
            if row[x * step] or row[x * step + 1] or row[x * step + 2]:
                right = max(right, x)
                break
    return width, height, left, right


def main():
    kind, started, switched, start = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
    shape = sys.argv[5] if len(sys.argv) > 5 else None
    a, b = shots(started, start), shots(switched, start)
    frames = sorted(set(a) & set(b))
    if len(frames) < 3:
        fail("%d pictures to compare (started %d, switched %d)" % (len(frames), len(a), len(b)))
    bad = []
    for f in frames:
        wa, ha, la, ra = extent(a[f])
        wb, hb, lb, rb = extent(b[f])
        why = None
        if (wa, ha) != (wb, hb):
            why = "the window %dx%d, started %dx%d" % (wb, hb, wa, ha)
        elif shape and abs(wa / ha - (16 / 9 if shape == '16:9' else 4 / 3)) > 0.02:
            why = "the window %dx%d is not %s" % (wa, ha, shape)
        elif kind == 'same' and open(a[f], 'rb').read() != open(b[f], 'rb').read():
            r = subprocess.run(["magick", "compare", "-metric", "AE", a[f], b[f], "null:"], capture_output=True, text=True)
            pixels = r.stderr.split(' ')[0]
            if pixels not in ('0', '0.0'):
                why = "%s pixels different (the picture in the window started %d-%d, switched %d-%d)" % (pixels, la, ra, lb, rb)
        elif kind == 'edges' and (abs(la - lb) > 2 or abs(ra - rb) > 2):
            why = "the picture in the window columns %d-%d, started %d-%d" % (lb, rb, la, ra)
        if why:
            bad.append((f, why))
    print("%s: %d pictures from frame %d compared, %d different" % (os.path.basename(switched), len(frames), start, len(bad)))
    for f, why in bad[:4]:
        print("  frame %d: %s" % (f, why))
    if bad:
        f = bad[0][0]
        strip(a[f], b[f], switched + "strip-%06d.png" % f)
        fail("%d of %d pictures not as started" % (len(bad), len(frames)))
    print("ok")


main()
