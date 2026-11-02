#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#
# Checks the translated code against the interpreter (docs/VERIFYING.md):
#  1. attract mode and made-up play (tools/madeup_input.py), each run with the interpreter (--cpu free: the
#     interpreter running the blocks as the translation does) and with the translation (--cpu native): the state
#     after every frame (main RAM and the picture) must be the same;
#  2. the same play run twice with the translation: the same again;
#  3. block by block: every dispatch of the translation run beside the interpreter from the same state, and the
#     registers, cycles, main RAM and cache RAM they leave compared (--check 1), over a shorter stretch of play.
#
#   cps3recomp/tools/check.sh ROMSET [FRAMES] [CHECK_FRAMES]      (from the top of the repository, after a build)
set -uo pipefail
ROM=${1:?usage: check.sh ROMSET [FRAMES] [CHECK_FRAMES]}
FRAMES=${2:-3000}
CHECK_FRAMES=${3:-1500}
GAME=${GAME:-sfiii3}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
BIN="$TOP/build/$GAME/$GAME-headless"
OUT="$TOP/work/$GAME/check"
mkdir -p "$OUT"
failed=0

compare() { # name a b
    if cmp -s "$2" "$3"; then
        echo "  $1: the same over $(wc -l < "$2" | tr -d ' ') frames"
    else
        echo "  $1: DIFFERENT, first at frame $(diff "$2" "$3" | grep -m1 '^<' | awk '{print $2}')"
        failed=1
    fi
}

PLAY=$(python3 "$TOP/cps3recomp/tools/madeup_input.py" 7 "$FRAMES")
echo "1. interpreter against translation ($FRAMES frames each)"
# shellcheck disable=SC2086
{
    "$BIN" "$ROM" --frames "$FRAMES" --cpu free --hash > "$OUT/attract-free" 2>/dev/null &
    "$BIN" "$ROM" --frames "$FRAMES" --cpu native --hash > "$OUT/attract-native" 2>/dev/null &
    "$BIN" "$ROM" --frames "$FRAMES" --cpu free --hash $PLAY > "$OUT/play-free" 2>/dev/null &
    "$BIN" "$ROM" --frames "$FRAMES" --cpu native --hash $PLAY > "$OUT/play-native" 2>/dev/null &
    "$BIN" "$ROM" --frames "$FRAMES" --cpu native --hash $PLAY > "$OUT/play-native2" 2>/dev/null &
    wait
}
compare "attract mode" "$OUT/attract-free" "$OUT/attract-native"
compare "made-up play" "$OUT/play-free" "$OUT/play-native"
echo "2. the translation twice"
compare "made-up play" "$OUT/play-native" "$OUT/play-native2"
echo "3. block by block ($CHECK_FRAMES frames of the play, every dispatch)"
# shellcheck disable=SC2086
"$BIN" "$ROM" --frames "$CHECK_FRAMES" --cpu native --check 1 $PLAY > /dev/null 2> "$OUT/blocks.log" || failed=1
grep -E "^check:|cps3recomp: check" "$OUT/blocks.log" | sed 's/^/  /'
if [ "$failed" = 0 ]; then
    echo "check: all the same"
else
    echo "check: FAILED"
fi
exit "$failed"
