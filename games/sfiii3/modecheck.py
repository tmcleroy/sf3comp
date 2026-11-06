#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checkmodes.sh's judgements of one run each (the Alternative modes' play-test bugs of 2026-11-06), from what the run
# wrote: its log (stderr), its pictures (--shot-every), and bytes of RAM a frame (--trace). Prints what it found and
# "ok" or "FAILED: ..."; exit 0 when ok.
#
#   modecheck.py veiled STATE_TRACE SHOT_PREFIX FROM      (model viewer, colour editor: every picture from frame FROM
#                                                          until the round is fought (the match's state 2) black, and
#                                                          the character shown once it is)
#   modecheck.py viewer LOG                               (stage viewer: its camera moves from its first frame shown,
#                                                          by itself (viewer_scroll), with no shuffle)
#   modecheck.py teams LOG                                (tag team: two starts, the random teams not the same)
#   modecheck.py tagtimes LOG                             (tag team: no spare's frame run in a frame of the fight shown)
#   modecheck.py gauge TRACE LOG                          (survival: player 1's super art gauge the same after each
#                                                          next opponent comes on, and not all empty)
#   modecheck.py playtime TRACE LOG                       (ring out: the computer's play time put up by a win's as
#                                                          each next opponent comes on, as the arcade's by its wins)
#   modecheck.py stun TRACE LOG                           (parry the ball: the most stun set while the ball stage is
#                                                          played, and nobody stunned with none)
#   modecheck.py loud WAV [quiet]                         (the stage viewer's music: heard, or (quiet) silent)
import re
import struct
import sys
import wave
import zlib


def fail(why):
    print("FAILED: " + why)
    sys.exit(1)


def png_maximum(path):
    # (the largest colour value of a PNG the port writes: 8-bit RGB or RGBA, any filter)
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
    most, previous = 0, bytearray(stride)
    for y in range(height):
        row = raw[y * (stride + 1):(y + 1) * (stride + 1)]
        f, line = row[0], bytearray(row[1:])
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
        for x in range(stride):
            if step == 4 and x % 4 == 3:
                continue
            most = max(most, line[x])
        previous = line
    return most


def trace(path, width):
    d = open(path, 'rb').read()
    return [d[i * width:(i + 1) * width] for i in range(len(d) // width)]


def veiled(states_path, prefix, start):
    import glob
    states = trace(states_path, 1)
    fought = next((i + 1 for i, r in enumerate(states) if r[0] == 2), None)
    if fought is None:
        fail("the round was never fought")
    shots = sorted(int(re.search(r'(\d+)\.png$', p).group(1)) for p in glob.glob(prefix + '*.png'))
    before = [f for f in shots if start <= f < fought]
    after = [f for f in shots if fought + 2 <= f <= fought + 200]
    if not before or not after:
        fail("no pictures before or after the round (%d, %d)" % (len(before), len(after)))
    shown = [f for f in before if png_maximum('%s%06d.png' % (prefix, f)) > 0]
    print("round fought at frame %d; %d pictures before it, %d not black %s" % (fought, len(before), len(shown), shown[:6]))
    if shown:
        fail("the way to the round (the intro) is shown")
    if all(png_maximum('%s%06d.png' % (prefix, f)) == 0 for f in after):
        fail("nothing shown once the round is on")
    print("ok")


def viewer(log):
    rows = []
    for line in open(log):
        m = re.match(r'stage camera: 3rd Strike (-?\d+) \((-?\d+)-(-?\d+)\), given (-?\d+) \((-?\d+)-(-?\d+)\), it had (-?\d+)', line)
        if m:
            rows.append([int(x) for x in m.groups()])
    if len(rows) < 200:
        fail("the stage was shown for %d frames only" % len(rows))
    given = [r[3] for r in rows[:120]]
    jumps = [i for i in range(1, 300) if abs(rows[i][6] - rows[i - 1][3]) > 1]
    print("its first 120 frames shown: %d places of the camera (%d to %d); %d frames its camera was not where it was sent" %
          (len(set(given)), min(given), max(given), len(jumps)))
    if len(set(given)) < 30:
        fail("the camera did not move by itself at first")
    if jumps:
        fail("the camera shuffled (frames %s)" % jumps[:6])
    print("ok")


def teams(log):
    found = re.findall(r'battle: teams (.*)', open(log).read())
    print("teams: %s" % found)
    if len(found) < 2:
        fail("fewer than two team battles started (or none told)")
    if len(set(found)) < len(found):
        fail("Random chose the same teams again")
    print("ok")


def tagtimes(log):
    text = open(log).read()
    times = re.findall(r'battle times: (\d+) frames.*?(\d+) spare frames in them', text)
    tags = len(re.findall(r'tags in', text))
    if not times:
        fail("no times")
    frames, spares = times[-1]
    print("%s frames of the fight, %d tags, %s spare frames run in them; %s" % (frames, tags, spares, re.findall(r'battle times: .*', text)[-1]))
    if tags < 2:
        fail("fewer than two tags")
    if int(spares) != 0:
        fail("spares were run while the fight was shown (frames that take 6-20 ms more)")
    print("ok")


def gauge(path, log):
    # (the trace: 0x020694C4 (player 2's character, a word) to 0x020695BF: the gauge's fill at +0xF1, its bars at +0xFB)
    rows = trace(path, 0x020695BF - 0x020694C4 + 1)
    who = [struct.unpack('>H', r[0:2])[0] for r in rows]
    value = [(r[0xFB], r[0xF1]) for r in rows]
    matches = len(re.findall(r'the next match on', open(log).read()))
    changes = [i for i in range(1, len(rows)) if who[i] != who[i - 1] and i > 200]
    checked, lost = 0, []
    for i in changes:
        if i < 3 or i + 5 >= len(rows):
            continue
        was, now = value[i - 2], value[i + 5]
        if was == (0, 0):
            continue
        checked += 1
        if now != was:
            lost.append((i, was, now))
    print("%d next opponents (%d opponent changes seen), %d with a gauge to carry, lost %s" % (matches, len(changes), checked, lost[:4]))
    if checked == 0:
        fail("no gauge to carry over")
    if lost:
        fail("the gauge was not kept for the next opponent")
    print("ok")


def playtime(path, log):
    rows = trace(path, 2)
    time = [struct.unpack('>h', r)[0] for r in rows]
    outs = len(re.findall(r'side 2 rung out', open(log).read()))
    # (a win's 40 seconds put on at once, as each next opponent comes on: the play time goes up by a second a second
    # otherwise)
    rises = [(i, time[i - 1], time[i]) for i in range(1, len(time)) if time[i] - time[i - 1] >= 30 and time[i - 1] > 0]
    print("%d ring outs; the play time %d at the end; up by a win's at %s" % (outs, time[-1] if time else 0, rises[:4]))
    if outs < 1:
        fail("no ring out")
    if not rises:
        fail("the computer's play time (its level) not put up for the opponents rung out")
    print("ok")


def stun(path, log):
    rows = trace(path, 10)  # (0x020695F7: the most stun, +2 the stun's timer)
    text = open(log).read()
    if 'phase 3 -> 4' not in text or 'phase 4 -> 5' not in text:
        fail("the ball stage was not played to its end")
    unset = sum(1 for r in rows if r[0] == 0)
    stunned = [i + 1 for i, r in enumerate(rows) if r[2] and r[0] == 0]
    print("the most stun: %s; frames with none %d; stunned with none at %s" % (sorted(set(r[0] for r in rows)), unset, stunned[:3]))
    if stunned:
        fail("stunned at once: the most stun was 0")
    if not any(r[0] for r in rows):
        fail("the most stun never set")
    print("ok")


def loud(path, quiet):
    w = wave.open(path)
    rate = w.getframerate()
    w.setpos(min(w.getnframes(), 10 * rate))
    data = w.readframes(5 * rate)
    s = struct.unpack('<%dh' % (len(data) // 2), data)
    rms = (sum(x * x for x in s) / max(len(s), 1)) ** 0.5
    print("loudness %.0f" % rms)
    if quiet and rms > 0:
        fail("heard with no music chosen")
    if not quiet and rms < 500:
        fail("no music heard")
    print("ok")


what = sys.argv[1]
if what == 'veiled':
    veiled(sys.argv[2], sys.argv[3], int(sys.argv[4]))
elif what == 'viewer':
    viewer(sys.argv[2])
elif what == 'teams':
    teams(sys.argv[2])
elif what == 'tagtimes':
    tagtimes(sys.argv[2])
elif what == 'gauge':
    gauge(sys.argv[2], sys.argv[3])
elif what == 'playtime':
    playtime(sys.argv[2], sys.argv[3])
elif what == 'stun':
    stun(sys.argv[2], sys.argv[3])
elif what == 'loud':
    loud(sys.argv[2], len(sys.argv) > 3)
else:
    fail("what? " + what)
