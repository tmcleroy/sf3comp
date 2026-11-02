#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
"""Made-up play for the headless game: prints --hold arguments.

  madeup_input.py SEED FRAMES

A coin and Start (and again every so often, to continue after a loss), a character and a super art chosen, then
player 1 pressing directions and buttons at random. The same seed always gives the same play, so that two runs
can be compared frame by frame (tools/check.sh) and code is reached for recording (tools/translate.sh).
"""

import random
import sys

DIRS = ["", "p1up", "p1down", "p1left", "p1right", "p1down+p1left", "p1down+p1right", "p1up+p1left", "p1up+p1right"]
BUTTONS = ["p1lp", "p1mp", "p1hp", "p1lk", "p1mk", "p1hk"]


def main():
    seed, frames = int(sys.argv[1]), int(sys.argv[2])
    rng = random.Random(seed)
    holds = []
    # (the game takes a minute to start: coin and Start once its attract mode runs)
    for at in range(900, frames, 1800):
        holds.append((at, at + 5, "coin1"))
        holds.append((at + 100, at + 105, "start1"))
        # (a character, then a super art: player 1's jab picks; a direction first moves the cursor)
        for k in range(6):
            t = at + 300 + k * 120
            holds.append((t, t + 8, rng.choice(DIRS[1:5])))
            holds.append((t + 40, t + 45, "p1lp"))
    t = 1700
    while t < frames:
        length = rng.randint(3, 12)
        parts = [d for d in [rng.choice(DIRS)] if d]
        if rng.random() < 0.6:
            parts.append(rng.choice(BUTTONS))
        if rng.random() < 0.15:
            parts.append(rng.choice(BUTTONS))
        if parts:
            holds.append((t, t + length, "+".join(parts)))
        t += length + rng.randint(1, 10)
    print(" ".join("--hold %d-%d:%s" % h for h in holds))


if __name__ == "__main__":
    main()
