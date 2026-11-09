#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
"""Carries an address known in one game over to another game of the same engine, by the routines' names.

Two programs built from much the same source (3rd Strike, 2nd Impact, New Generation) have routines of the same
name (tools/names.py: work/<game>/functions.tsv) whose code loads the same things in the same order, each from its
own literal pool. So where both routines load as many RAM addresses, the first's n-th is the second's n-th; a pair
seen so in several routines is taken, one seen once is a guess.

  crossmap.py FROM_GAME FROM_PROGRAM TO_GAME TO_PROGRAM [ADDRESS...]

PROGRAM: the game's deciphered program (<game>-headless ROMSET --frames 1 --dump DIR writes DIR/program.bin), at
06000000; where the dump has an opcodes.bin beside it (2nd Impact: its instructions and its data are not read the
same way) the instructions are read from that. With no ADDRESS: every pair found, as "from to votes of seen". With addresses (hex, of FROM_GAME): each
with what it is in TO_GAME, the votes, and the routines that say so; a routine's address (06xxxxxx) is answered by
name alone. An address inside a record (a player's, say) is not a literal of its own: give the record's start, and
check the offset in the routine itself (the records are not laid out the same in each game).

A result is a lead, not a fact: confirm each one in the game before it is used (docs/READABLE.md).
"""

import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from names import for_game  # noqa: E402

BASE = 0x06000000
RAM = (0x02000000, 0x02080000)


def literals(program, start, size, opcodes=None):
    """The RAM addresses a routine loads from its literal pool (mov.l @(disp,PC),Rn), in the order of its code."""
    out = []
    for pc in range(start, start + size - 1, 2):
        op = struct.unpack_from(">H", opcodes or program, pc - BASE)[0]
        if op >> 12 == 0xD:
            at = (pc & ~3) + 4 + (op & 0xFF) * 4
            if start <= at < start + size + 64 and at - BASE + 4 <= len(program):
                value = struct.unpack_from(">I", program, at - BASE)[0]
                if RAM[0] <= value < RAM[1]:
                    out.append(value)
    return out


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        sys.exit(__doc__)
    a_names, b_names = for_game(args[0]), for_game(args[2])
    if not a_names or not b_names:
        sys.exit("no table for one of the games: names.py fetch")
    a_prog, b_prog = open(args[1], "rb").read(), open(args[3], "rb").read()

    def opcodes(path):
        beside = os.path.join(os.path.dirname(path), "opcodes.bin")
        return open(beside, "rb").read() if os.path.exists(beside) else None
    a_ops, b_ops = opcodes(args[1]), opcodes(args[3])
    b_by_name = {}
    for start in b_names.starts:
        size, name, _ = b_names.routines[start]
        b_by_name.setdefault(name, []).append((start, size))
    votes = defaultdict(Counter)  # from -> to -> count
    said = defaultdict(list)      # (from, to) -> routines
    seen = Counter()              # from -> routines of both games that load it
    for start in a_names.starts:
        size, name, _ = a_names.routines[start]
        twins = b_by_name.get(name, [])
        if len(twins) != 1:
            continue
        la = literals(a_prog, start, size, a_ops)
        lb = literals(b_prog, twins[0][0], twins[0][1], b_ops)
        for value in set(la):
            seen[value] += 1
        if not la or len(la) != len(lb):
            continue
        for x, y in set(zip(la, lb)):
            votes[x][y] += 1
            said[(x, y)].append(name)
    if len(args) == 4:
        for x in sorted(votes):
            y, n = votes[x].most_common(1)[0]
            print("%08X %08X %d of %d" % (x, y, n, seen[x]))
        return
    for text in args[4:]:
        x = int(text, 16)
        if BASE <= x < BASE + 0x1000000:
            found = a_names.find(x)
            if not found:
                print("%08X in no routine" % x)
                continue
            twins = b_by_name.get(found[1], [])
            print("%08X %s: %s" % (x, a_names.label(x), ", ".join("%08X" % (t[0] + (x - found[0] if len(twins) == 1 and x == found[0] else 0)) for t in twins) or "no routine of that name"))
            continue
        if x not in votes:
            print("%08X: no pair found (loaded by %d routines both games have)" % (x, seen[x]))
            continue
        for y, n in votes[x].most_common(3):
            print("%08X %08X %d of %d (%s)" % (x, y, n, seen[x], ", ".join(said[(x, y)][:6])))


if __name__ == "__main__":
    main()
