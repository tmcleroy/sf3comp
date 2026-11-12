#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The guest stages in the modes (src/battle.cpp's Stage rows, src/stages.cpp's StageForMode): New Generation's and 2nd
# Impact's stages chosen on the tag team battle's and the endless battle's Stage rows, each fought on. Until 2026-11-11
# those rows had 3rd Strike's stages only (the play-test's "can't select guest stages in tag team battle").
#
#   1. The rows: the launcher's feature list (--list-features) has every guest stage, by the menu Stage's names and in its
#      order, on team_stage and endless_stage.
#   2. In play, headless (SFIII3_GUEST_LOG=1: every 300 frames, the stage wanted and the one whose board is there): a
#      tag team battle (the tag style) and a knock-out team battle, of made-up play against the computer, on a 2nd Impact
#      stage and a New Generation stage, and an endless battle on another: from the first logged frame of a fight on,
#      every logged frame in a fight is on the chosen guest stage, its board there and come to its fight; and there is
#      such a frame.
#
#   games/sfiii3/checkguestmodes.sh ROM_DIR [BUILD]     (ROM_DIR: sfiii3nr1.zip, sfiiin.zip and sfiii2n.zip; BUILD:
#                                                        build/sfiii3; SHOTS=dir: the fights' pictures kept there)
ROMS=${1:?usage: checkguestmodes.sh ROM_DIR [BUILD]}
BUILD=${2:-build/sfiii3}
OUT=$(mktemp -d)
LIMITED=$(command -v gtimeout || command -v timeout)
fail=0

# 1. the rows
"$BUILD/sfiii3" --list-features > "$OUT/features.txt" 2>/dev/null
python3 - "$OUT/features.txt" <<'EOF' || fail=1
import sys
rows = {}
for line in open(sys.argv[1]):
    fields = line.rstrip("\n").split("\t")
    if len(fields) >= 6:
        rows[fields[0]] = fields[-1].split("|")
guest = lambda c: c.startswith(("New Generation:", "2nd Impact:"))
# (the menu Stage's own guest stages: not its last two, each game's "the opponent's", which the modes' rows have not)
guests = [c for c in rows.get("stage", []) if guest(c) and not c.endswith("the opponent's")]
bad = len(guests) < 28
if bad:
    print(f"rows: stage has {len(guests)} guest stages")
for row in ("team_stage", "endless_stage"):
    tail = [c for c in rows.get(row, []) if guest(c)]
    if tail != guests:
        print(f"rows: {row} has {len(tail)} guest stages, not the menu Stage's {len(guests)} in its order")
        bad = True
print("rows: " + ("FAILED" if bad else f"team_stage and endless_stage have the menu Stage's {len(guests)} guest stages, in its order"))
sys.exit(1 if bad else 0)
EOF

# 2. in play. The choices: team_stage 0 Random, 1-21 3rd Strike's, then the guest stages from 22 (GuestStageNames'
# order: New Generation's 13, then 2nd Impact's 15); endless_stage 0 Scroll, 1 Shuffle, 2-22 3rd Strike's, then the
# guests from 23
run() { # NAME GUEST FRAMES ARGS...
    local name=$1 guest=$2 frames=$3
    shift 3
    mkdir -p "$OUT/$name"
    local shots=()
    [ -n "$SHOTS" ] && mkdir -p "$SHOTS/$name" && shots=(--shot-every 600:"$SHOTS/$name/f_")
    SFIII3_GUEST_LOG=1 SFIII3_BATTLE_SEED=7 nice -n 10 $LIMITED -k 10 "${LIMIT:-900}" "$BUILD/sfiii3" --rom "$ROMS/sfiii3nr1.zip" \
        --headless --frames "$frames" --settings "$OUT/$name/settings.ini" --eeprom "$OUT/$name/eeprom.bin" "${shots[@]}" "$@" \
        > /dev/null 2> "$OUT/$name.log"
    local status=$?
    python3 - "$OUT/$name.log" "$guest" "$name" "$status" <<'EOF' || return 1
import re, sys
log, guest, name, status = sys.argv[1], int(sys.argv[2]), sys.argv[3], int(sys.argv[4])
rows = [tuple(map(int, m.groups())) for m in re.finditer(
    r"guest stage: frame (\d+), wanted (-?\d+) \(mode (-?\d+), session (-?\d+)\), loaded (-?\d+), fought (\d), on stage (\d)", open(log).read())]
fights = [r for r in rows if r[6] == 1]
if status != 0:
    print(f"{name}: the run stopped ({status})"); sys.exit(1)
if not fights:
    print(f"{name}: no fight logged ({len(rows)} lines)"); sys.exit(1)
# (the first fight's first logged frames may be before its board is there: it takes a few seconds to build)
first = next((i for i, r in enumerate(fights) if r[4] == guest and r[5] == 1), None)
if first is None:
    print(f"{name}: never on guest stage {guest} (wanted {sorted(set(r[1] for r in fights))}, loaded {sorted(set(r[4] for r in fights))})")
    sys.exit(1)
off = [r for r in fights[first:] if not (r[1] == guest and r[4] == guest and r[5] == 1)]
if off:
    print(f"{name}: {len(off)} of {len(fights) - first} logged fight frames off guest stage {guest}: frame {off[0][0]} {off[0]}")
    sys.exit(1)
print(f"{name}: on guest stage {guest} in all {len(fights) - first} logged fight frames from frame {fights[first][0]}")
EOF
}
# (2nd Impact: Ryu's, guest 16; New Generation: Ken's, guest 12; 2nd Impact: Urien's, guest 27)
run tag 16 6000 --features team_stage=$((21 + 16)) --keys 30:cteam_start --auto 7:300:1 &
run knockout 12 6000 --features team_style=1,team_stage=$((21 + 12)) --keys 30:cteam_start --auto 7:300:1 &
run endless 27 4000 --features endless_stage=$((22 + 27)) --keys 30:cendless_start &
for job in $(jobs -p); do wait "$job" || fail=1; done
rm -rf "$OUT"
[ $fail = 0 ] && echo "checkguestmodes: all passed" || echo "checkguestmodes: FAILED"
exit $fail
