#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
"""The names of a game's routines, where a table of them is published: for the translator's comments and for
reading the checks' logs. A table is lines of "address<TAB>size<TAB>name<TAB>file" (hex address, # comments).

  names.py fetch [GAME...]      download the table game.cfg names (names_source) to work/<game>/functions.tsv
  names.py annotate [GAME]      copy stdin to stdout, each code address followed by the routine it is in
  names.py lookup WHAT [GAME]   an address (the routine it is in) or part of a name or file (the routines so named)

The tables are other projects' work and are never committed: work/ holds them, as it holds everything else made from
a game. Without a table, annotate copies its input unchanged and the translator writes no names.
"""

import bisect
import os
import re
import sys
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gamecfg  # noqa: E402

# A code address as the logs and sources write it: 8 hex digits in the program's ROM (06xxxxxx), with or without 0x
ADDRESS = re.compile(r"(?<![0-9A-Za-z_])(?:0x)?(06[0-9A-Fa-f]{6})(?![0-9A-Za-z_])")


def table_path(game):
    return os.path.join(gamecfg.ROOT, "work", game, "functions.tsv")


class Names:
    """A game's routines by address."""

    def __init__(self, path=None):
        self.starts = []
        self.routines = {}  # start -> (size, name, file)
        if path and os.path.exists(path):
            with open(path) as f:
                for line in f:
                    parts = line.rstrip("\n").split("\t")
                    if line.startswith("#") or len(parts) < 3:
                        continue
                    try:
                        self.routines[int(parts[0], 16)] = (int(parts[1]), parts[2], parts[3] if len(parts) > 3 else "")
                    except ValueError:
                        continue
            self.starts = sorted(self.routines)

    def __bool__(self):
        return bool(self.starts)

    def find(self, address):
        """(start, name, file) of the routine the address is in, or None."""
        i = bisect.bisect_right(self.starts, address) - 1
        if i < 0:
            return None
        start = self.starts[i]
        size, name, source = self.routines[start]
        if address >= start + size:
            return None
        return start, name, source

    def label(self, address, with_file=False):
        """"name" or "name+0x1C" (and its file), or "" for an address in no routine."""
        found = self.find(address)
        if not found:
            return ""
        start, name, source = found
        text = name if address == start else "%s+0x%X" % (name, address - start)
        if with_file and source:
            text += ", " + source
        return text

    def annotate(self, line):
        def named(m):
            label = self.label(int(m.group(1), 16))
            return "%s (%s)" % (m.group(0), label) if label else m.group(0)
        return ADDRESS.sub(named, line)


def for_game(game=None):
    return Names(table_path(game or os.environ.get("GAME", "sfiii3")))


def fetch(games):
    failed = 0
    for game in games:
        source = gamecfg.load(game).get("names_source", "")
        if not source:
            print("%s: game.cfg names no table (names_source)" % game)
            continue
        try:
            with urllib.request.urlopen(urllib.request.Request(source, headers={"User-Agent": "cps3recomp"}), timeout=60) as r:
                data = r.read()
        except OSError as e:
            print("%s: %s: %s" % (game, source, e))
            failed = 1
            continue
        path = table_path(game)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
        print("%s: %d routines named (%s)" % (game, len(Names(path).starts), os.path.relpath(path, gamecfg.ROOT)))
    return failed


def main():
    args = sys.argv[1:]
    if not args or args[0] not in ("fetch", "annotate", "lookup"):
        sys.exit(__doc__)
    if args[0] == "fetch":
        games = args[1:] or sorted(g for g in os.listdir(os.path.join(gamecfg.ROOT, "games"))
                                   if os.path.exists(os.path.join(gamecfg.ROOT, "games", g, "game.cfg")))
        sys.exit(fetch(games))
    if args[0] == "annotate":
        names = for_game(args[1] if len(args) > 1 else None)
        for line in sys.stdin:
            sys.stdout.write(names.annotate(line) if names else line)
        return
    if len(args) < 2:
        sys.exit(__doc__)
    names = for_game(args[2] if len(args) > 2 else None)
    if not names:
        sys.exit("no table: names.py fetch")
    what = args[1]
    if re.fullmatch(r"(0x)?[0-9A-Fa-f]{6,8}", what):
        address = int(what, 16)
        print("%08X %s" % (address, names.label(address, True) or "in no routine"))
        return
    for start in names.starts:
        size, name, source = names.routines[start]
        if what.lower() in name.lower() or what.lower() in source.lower():
            print("%08X %5d %s, %s" % (start, size, name, source))


if __name__ == "__main__":
    main()
