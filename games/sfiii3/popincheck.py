#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# checkpopin.sh's judge: each run's "popin: stage ..." lines (SFIII3_POPIN_CHECK, src/stages.cpp). A stage passes with
# no stage piece appearing or going in the widescreen margins, no tilemap cell changing there, and no end column with
# nothing of the stage, over at least LEAST_FRAMES frames compared; beyond what the stage does between them, in the board's
# own 384 (its people coming and going, things it brings on, animation), as many for the margins' width, half again, and 2. Allowances (ALLOW below) name the stage, the count and
# the reason, and are said each run. Prints a line a run and stage, and "ok" or "FAILED: ...".
import glob
import os
import re
import sys

LEAST_FRAMES = 300
BY_WIDTH = 128 / 384 * 1.5  # (the margins' columns against the 384 between them, and half again)
# (run kind, stage) -> (objects, cells, black frames) allowed, and why
ALLOW = {
    # (Elena's stage in a fight, at the top of a high jump: its sky's last 6 columns at the right end of the wide picture have
    # nothing drawn, for a few frames (8 of 1688 in the check's fight, 2026-11-08: a tilemap the stage does not write that far
    # past the board's 384); not put right yet)
    # (New Generation's Gill stage in a fight: its lava throws up sparks every 72 frames at the same place, which the
    # fight's camera keeps in the left margin; the stage's own effect, as many before the widened range as after (13))
    ('fight', 'sfiiin:0'): ((16, 0, 0), "New Generation's Gill stage: the lava's sparks, its own, in the left margin"),
    ('fight', 'sfiii3:8'): ((0, 0, 12), "Elena's sky, its right end's last columns at the top of a high jump"),
}

LINE = re.compile(r"popin: stage (\S+): (\d+) frames compared; objects appearing or going in the margins (\d+) \(between them (\d+)\); "
                  r"tilemap cells changing in the margins (\d+) \(between them (\d+)\); frames with an end column with nothing of the stage (\d+)")


def main():
    out = sys.argv[1]
    failures, seen = [], 0
    for log in sorted(glob.glob(os.path.join(out, '*.log'))):
        run = os.path.basename(log)[:-4]
        kind = run.split('-')[0]
        lines = [LINE.search(l) for l in open(log, errors='replace')]
        lines = [m for m in lines if m]
        if not lines:
            failures.append("%s: no stage shown in widescreen" % run)
            print("%-12s no stage shown" % run)
            continue
        for m in lines:
            stage, frames, om, oi, cm, ci, black = m.group(1), *map(int, m.groups()[1:])
            if frames < 30:
                continue  # (a stage passed through on the way: the unseen fight's before the viewer's is shown)
            seen += 1
            allow = ALLOW.get((kind, stage))
            line = "%-12s %-14s %5d frames: objects in the margins %3d (between %3d), cells %3d (between %3d), black ends %3d" % (
                run, stage, frames, om, oi, cm, ci, black)
            why = []
            if frames < LEAST_FRAMES and stage == lines[-1].group(1):
                why.append("only %d frames compared" % frames)
            a = allow[0] if allow else (0, 0, 0)
            # (what the 4:3 game shows too, its things coming and going, its animation: as many in the margins, for their
            # width, as between them, and half again; and two)
            if om > a[0] + oi * BY_WIDTH + 2:
                why.append("%d pop-ins of objects" % om)
            if cm > a[1] + ci * BY_WIDTH + 2:
                why.append("%d tilemap cells popping" % cm)
            if black > a[2]:
                why.append("%d frames with an empty end" % black)
            if allow:
                line += "  (allowed %s: %s)" % (allow[0], allow[1])
            print(line + ("" if not why else ": " + ", ".join(why)))
            if why:
                failures.append("%s %s: %s" % (run, stage, ", ".join(why)))
    if not seen:
        failures.append("nothing compared")
    if failures:
        print("FAILED: %d: %s" % (len(failures), "; ".join(failures[:8])))
        sys.exit(1)
    print("ok")


if __name__ == "__main__":
    main()
