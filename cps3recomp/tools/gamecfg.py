#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
"""Reads games/<id>/game.cfg (key = value lines, # comments).

  gamecfg.py KEY [GAME]            print one value
  gamecfg.py --header OUT [GAME]   write a C header with every value as a GAME_<KEY> string macro
  gamecfg.py --collection OUT GAME...   write the collection launcher's table of games (games.h, for launcher/src/description.cpp)
  gamecfg.py --games               print every game's id, in the collection's order
"""

import os
import re
import sys

# Where games/ is: where this is run from (make runs it from the top, the example from example/), or else this repository
ROOT = os.getcwd() if os.path.isdir("games") else os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load(game=None):
    game = game or os.environ.get("GAME", "sfiii3")
    cfg = {}
    with open(os.path.join(ROOT, "games", game, "game.cfg")) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            key, value = key.strip(), value.strip()
            # (a key given again adds to it: other_set, a line a set)
            cfg[key] = cfg[key] + " || " + value if key in cfg else value
    return cfg


# What the collection's launcher is told of each game (launcher/src/description.cpp, GameInfo), in its order
COLLECTION_KEYS = ["id", "name", "rom_set", "rom_parent", "rom_name", "bios", "simm1", "simm2", "simm3", "simm4", "simm5", "simm6",
                   "program_enciphered", "launcher_extra_sets", "guests", "display", "launcher_name"]


def collection_order(game):
    """Where a game stands in the collection: by its `collection_order` (a number, the lowest first; the game the
    launcher takes the shared keys and buttons from is the first), then as the games came out (the date in the set's
    name, "990512"), then by name."""
    cfg = load(game)
    date = re.search(r"\b(\d{6})\b", cfg.get("rom_name", ""))
    return (int(cfg.get("collection_order") or 1000), date.group(1) if date else "999999", cfg.get("name", game))


def quoted(value):
    return '"%s"' % value.replace("\\", "\\\\").replace('"', '\\"')


def write_if_changed(path, content):
    if not os.path.exists(path) or open(path).read() != content:
        os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
        with open(path, "w") as f:
            f.write(content)


def main():
    args = sys.argv[1:]
    if len(args) >= 2 and args[0] == "--collection":
        lines = ["// Generated from games/*/game.cfg by cps3recomp/tools/gamecfg.py --collection", "#pragma once", "",
                 "static const GameInfo kGames[] = {"]
        for game in sorted(args[2:], key=collection_order):
            cfg = load(game)
            cfg.setdefault("id", game)
            lines.append("    {" + ", ".join(quoted(cfg.get(key, "")) for key in COLLECTION_KEYS) + "},")
        lines.append("};")
        write_if_changed(args[1], "\n".join(lines) + "\n")
        return
    if args and args[0] == "--games":
        print(" ".join(sorted((g for g in sorted(os.listdir(os.path.join(ROOT, "games")))
                               if os.path.isfile(os.path.join(ROOT, "games", g, "game.cfg"))), key=collection_order)))
        return
    if len(args) >= 2 and args[0] == "--header":
        cfg = load(args[2] if len(args) > 2 else None)
        lines = ["// Generated from game.cfg by cps2recomp/tools/gamecfg.py", "#pragma once", ""]
        for key, value in cfg.items():
            lines.append('#define GAME_%s "%s"' % (key.upper(), value.replace("\\", "\\\\").replace('"', '\\"')))
        content = "\n".join(lines) + "\n"
        if not os.path.exists(args[1]) or open(args[1]).read() != content:
            os.makedirs(os.path.dirname(args[1]) or ".", exist_ok=True)
            with open(args[1], "w") as f:
                f.write(content)
        return
    if not args:
        sys.exit(__doc__)
    print(load(args[1] if len(args) > 1 else None).get(args[0], ""))


if __name__ == "__main__":
    main()
