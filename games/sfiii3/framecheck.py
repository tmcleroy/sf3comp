#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
"""Compares the training mode's frame data with recorded animations.

  framecheck.py MOVES FRAMEDATA_DIR

MOVES: the lines the game prints with SFIII3_MOVES=1 ("move PLAYER CHARACTER ANIMATION STARTUP ACTIVE RECOVERY").
FRAMEDATA_DIR: a folder of <character>_framedata.json files of recorded animations, keyed by animation, with the
frames a move hits on (Grouflon's 3rd_training_lua has them for this set, data/sfiii3nr1/framedata; not part of this
repository). A move's first hit frame (counted from 0) should be its startup less one, and its first range of hit frames as long as its active.
"""
import json
import os
import sys
from collections import Counter

# The game's character numbers (the players' records, 0x3C0)
CHARACTERS = ["gill", "alex", "ryu", "yun", "dudley", "necro", "hugo", "ibuki", "elena", "oro", "yang", "ken", "sean", "urien",
              "gouki", "gill", "chunli", "makoto", "q", "twelve", "remy"]


def main():
    moves, folder = sys.argv[1], sys.argv[2]
    reference = {}
    results = Counter()
    lines = []
    for line in open(moves):
        parts = line.split()
        if len(parts) != 7 or parts[0] != "move":
            continue
        character = CHARACTERS[int(parts[2])] if int(parts[2]) < len(CHARACTERS) else parts[2]
        animation, startup, active = parts[3], int(parts[4]), int(parts[5])
        if character not in reference:
            path = os.path.join(folder, character + "_framedata.json")
            reference[character] = json.load(open(path)) if os.path.exists(path) else {}
        known = reference[character].get(animation)
        if not known or not known.get("hit_frames"):
            results["not in the reference"] += 1
            continue
        first = known["hit_frames"][0]
        # (the recordings count an animation's frames from 0, and a hit registers on the frame its box is out: the
        # first hit frame k is startup k + 1)
        want_startup = (first["min"] if isinstance(first, dict) else first) + 1
        want_active = first["max"] - first["min"] + 1 if isinstance(first, dict) else None
        ok = startup == want_startup and (want_active is None or active == want_active)
        results["the same" if ok else "different"] += 1
        if not ok:
            lines.append("%s %s: startup %d active %d, reference %d %s" % (character, animation, startup, active, want_startup, want_active))
    for line in sorted(set(lines)):
        print(line)
    print(dict(results))


if __name__ == "__main__":
    main()
