#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checkviewer.sh's judge: which stage each of the stage viewer's entries shows, against ground truth: each stage as its
# own fight shows it (training sessions on that stage: 3rd Strike's own board drawing its own stage, or a guest stage
# made alone, which checkguests.sh checks against that game's own match), its layers drawn alone
# (SFIII3_STAGE_PICTURES). A stage is told by its colours: a histogram of its pictures' colours (16 levels a channel),
# compared by how much two histograms share. An entry passes when the reference nearest its pictures is the stage it
# names, and that stage is told apart from every other stage it might be taken for (a name whose own fight shows
# another name's stage is no stage of its own: 3rd Strike's number 18, Q's, builds Dudley's). And every stage of the
# references is in the viewer: 3rd Strike's and New Generation's and 2nd Impact's. Prints a line per entry and "ok" or
# "FAILED: ..."; writes a strip (reference | the viewer's) for each entry that fails.
#
#   stagecheck.py REFS VIEWS OUT
#     REFS:  a folder of folders, one per stage, named by the training row's name, of its stage pictures
#     VIEWS: a folder of folders, one per viewer entry, named "<index> <name>", of its pictures (the shown picture: the
#            viewer draws its stage alone), and <index>.wav, its sound (heard), or <index>-quiet.wav (none chosen)
import os
import struct
import subprocess
import sys
import wave
import zlib

LEVELS = 16
SAME = 0.90      # (two stages sharing this much of their colours are one stage)
CLEAR = 0.20     # (an entry's own stage nearer it than the next by this much: 0.33 the least seen, New Generation: Ken's, 2026-11-08)


def read_png(path):
    data = open(path, 'rb').read()
    at, idat, width, height, kind = 8, b'', 0, 0, 2
    while at < len(data):
        length, name = struct.unpack('>I4s', data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        if name == b'IHDR':
            width, height, _, kind = struct.unpack('>IIBB', body[:10])
        elif name == b'IDAT':
            idat += body
        at += 12 + length
    raw = zlib.decompress(idat)
    step = 4 if kind == 6 else 3
    stride = width * step
    rows, previous = [], bytearray(stride)
    for y in range(height):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        f, line = row[0], bytearray(row[1:])
        if f:
            for x in range(stride):
                a = line[x - step] if x >= step else 0
                b = previous[x]
                c = previous[x - step] if x >= step else 0
                if f == 1:
                    line[x] = (line[x] + a) & 255
                elif f == 2:
                    line[x] = (line[x] + b) & 255
                elif f == 3:
                    line[x] = (line[x] + (a + b) // 2) & 255
                elif f == 4:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        previous = line
    return width, height, step, rows


def histogram(paths, into=None):
    h = into if into is not None else {}
    shift = 8 - LEVELS.bit_length() + 1
    for path in paths:
        width, height, step, rows = read_png(path)
        for y in range(0, height, 3):
            row = rows[y]
            for x in range(0, width * step, 3 * step):
                r, g, b = row[x] >> shift, row[x + 1] >> shift, row[x + 2] >> shift
                if r == 0 and g == 0 and b == 0:
                    continue  # (nothing drawn there)
                k = (r << 8) | (g << 4) | b
                h[k] = h.get(k, 0) + 1
    return h


def share(a, b):
    ta, tb = sum(a.values()) or 1, sum(b.values()) or 1
    return sum(min(v / ta, b.get(k, 0) / tb) for k, v in a.items())


def pictures(folder, least=0):
    out = []
    for name in sorted(os.listdir(folder)):
        if name.endswith('.png'):
            frame = int(''.join(c for c in name if c.isdigit())[-6:] or 0)
            if frame >= least:
                out.append(os.path.join(folder, name))
    return out


def loudness(path):
    w = wave.open(path)
    rate = w.getframerate()
    w.setpos(min(w.getnframes(), int(25 * rate)))
    data = w.readframes(8 * rate)
    s = struct.unpack('<%dh' % (len(data) // 2), data)
    return (sum(x * x for x in s) / max(len(s), 1)) ** 0.5


def alias(name):
    return name.replace("Akuma (red)'s", "Akuma's (red)")


def main():
    refs_dir, views_dir, out = sys.argv[1], sys.argv[2], sys.argv[3]
    os.makedirs(out, exist_ok=True)
    refs = {}
    for name in sorted(os.listdir(refs_dir)):
        paths = pictures(os.path.join(refs_dir, name))
        if paths:
            refs[name] = (histogram(paths), paths)
    failures = []
    # (stages of the references that are one stage: the second name's own fight shows the first's)
    names = list(refs)
    same_as = {}
    for i, a in enumerate(names):
        for b in names[:i]:
            s = share(refs[a][0], refs[b][0])
            if s >= SAME and b not in same_as:
                same_as[a] = b
                print("reference: the stage of %s shows %s (%.2f of their colours shared): one stage" % (a, b, s))
    covered = set()
    for entry in sorted(os.listdir(views_dir), key=lambda e: int(e.split(' ')[0]) if e.split(' ')[0].isdigit() else 0):
        folder = os.path.join(views_dir, entry)
        if not os.path.isdir(folder):
            continue
        index, name = entry.split(' ', 1)
        name = alias(name)
        paths = pictures(folder, 1200)
        if not paths:
            failures.append("%s: no picture" % entry)
            print("entry %s: no picture" % entry)
            continue
        h = histogram(paths)
        ranked = sorted(((share(h, refs[r][0]), r) for r in refs), reverse=True)
        best, nearest = ranked[0]
        own = next((s for s, r in ranked if r == name), None)
        other = next(((s, r) for s, r in ranked if r != name and same_as.get(r) != name and same_as.get(name) != r), (0, "none"))
        line = "entry %-3s %-34s nearest %-34s %.2f (its own %s; the next other %s %.2f)" % (
            index, name, nearest, best, "%.2f" % own if own is not None else "none", other[1], other[0])
        why = None
        if name not in refs:
            why = "no stage of that name"
        elif name in same_as:
            why = "no stage of its own: its own fight shows %s" % same_as[name]
        elif nearest != name and same_as.get(nearest) != name:
            why = "shows %s" % nearest
        elif best - other[0] < CLEAR:
            why = "not told apart from %s" % other[1]
        # (its sound: its own music heard, or none with none chosen)
        wav = os.path.join(views_dir, index + '.wav')
        quiet = os.path.join(views_dir, index + '-quiet.wav')
        if os.path.exists(wav):
            level = loudness(wav)
            line += ", its music %.0f" % level
            if level < 300 and not why:
                why = "no music heard"
        if os.path.exists(quiet):
            level = loudness(quiet)
            line += ", with none chosen %.0f" % level
            if level > 0 and not why:
                why = "heard with no music chosen"
        print(line + ("" if not why else ": " + why))
        if why:
            failures.append("%s: %s" % (entry, why))
            # (the strip: the reference of the stage it names (or the nearest's) | the viewer's)
            ref = refs.get(same_as.get(name, name)) or refs.get(nearest)
            strip = os.path.join(out, "entry-%s.png" % index)
            subprocess.run(["magick", ref[1][len(ref[1]) // 2], paths[len(paths) // 2], "+append", strip], check=False)
            print("  strip: %s" % strip)
        else:
            covered.add(name)
            covered.add(same_as.get(name, name))
    for name in names:
        if name not in covered and same_as.get(name) not in covered:
            failures.append("%s not in the viewer" % name)
    missing = [n for n in names if n not in covered and same_as.get(n) not in covered]
    if missing:
        print("stages not in the viewer: %s" % ", ".join(missing))
    if failures:
        print("FAILED: %d: %s" % (len(failures), "; ".join(failures[:6])))
        sys.exit(1)
    print("ok")


if __name__ == "__main__":
    main()
