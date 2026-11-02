#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
"""Hash of a routine's code for a lift (runtime/lift.h): FNV-1a 64 over its bytes.

  lifthash.py DUMP BASE ADDRESS SIZE     DUMP: the deciphered program (sfiii3-headless ROM --frames 1 --dump DIR: DIR/program.bin, base 6000000; DIR/bios.bin, base 0) or a main RAM dump (base 2000000)
"""
import sys


def fnv(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit(__doc__)
    data = open(sys.argv[1], "rb").read()
    at = int(sys.argv[3], 16) - int(sys.argv[2], 16)
    size = int(sys.argv[4], 0)
    print("0x%016Xull" % fnv(data[at:at + size]))
