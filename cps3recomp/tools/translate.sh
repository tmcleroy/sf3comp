#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp).
#
# Translates the game's SH-2 code: records the blocks it runs (attract mode, then made-up play with several seeds,
# all with the interpreter: --cpu free), adds the blocks found by following their code, writes C++ for them all
# (tools/recomp.py) and builds the game with it.
#
#   cps3recomp/tools/translate.sh ROMSET [FRAMES] [SEEDS]      (from the top of the repository)
#
# The block list, games/<game>/block_addresses.txt, is committed: each block's address and length in instructions,
# no code. Recording adds what it finds to it (sorted, each block once). The code itself is read from the ROM set
# given, deciphered as the game loads it (<game>-headless --block-code), into work/<game>/blocks.txt (the block
# database recomp.py reads) and work/<game>/gen/ (the C++): derived from the game, never committed.
# FRAMES 0: no recording, the committed list translated as it is.
set -euo pipefail
ROM=${1:?usage: translate.sh ROMSET [FRAMES] [SEEDS]}
FRAMES=${2:-6000}
SEEDS=${3:-"1 2 3"}
GAME=${GAME:-sfiii3}
TOP=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$TOP/build/$GAME"
WORK="$TOP/work/$GAME"
mkdir -p "$WORK"
cmake -S "$TOP" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAME="$GAME" >/dev/null
ninja -C "$BUILD" "$GAME-headless" >/dev/null
GAME_BIN="$BUILD/$GAME-headless"

LIST="$TOP/games/$GAME/block_addresses.txt"
[ -f "$LIST" ] || echo "# cps3recomp block list: address, length in instructions. No code: cps3recomp/tools/translate.sh reads it from your ROM set" > "$LIST"

record() {
    nice -n 20 "$GAME_BIN" "$ROM" --cpu free --record-blocks "$WORK/blocks-$1.txt" "${@:2}" 2>&1 | grep "block database" || true
}
if [ "$FRAMES" != 0 ]; then
    # (each run into its own file, so that they can run side by side; then merged)
    rm -f "$WORK"/blocks-*.txt
    pids=()
    record attract --frames "$FRAMES" &
    pids+=($!)
    for seed in $SEEDS; do
        # shellcheck disable=SC2046
        record "play$seed" --frames "$FRAMES" $(python3 "$TOP/cps3recomp/tools/madeup_input.py" "$seed" "$FRAMES") &
        pids+=($!)
    done
    wait "${pids[@]}"
fi
# The blocks recorded, and those of a database made before the list was kept (work/<game>/blocks.txt), into the
# list by address and length: only the ROMs' (what runs in main RAM is whatever the program put there)
shopt -s nullglob
recorded=("$WORK"/blocks-*.txt)
[ ! -f "$WORK/blocks.txt" ] || recorded+=("$WORK/blocks.txt")
if [ ${#recorded[@]} -gt 0 ]; then
    { head -1 "$LIST"
      { grep -v '^#' "$LIST" || true
        cat "${recorded[@]}" | awk '!/^#/ {a = substr($1, 1, 2); if (a == "00" || a == "06") printf "%s %d\n", $1, length($2) / 4}'
      } | LC_ALL=C sort -u; } > "$WORK/list.merged"
    mv "$WORK/list.merged" "$LIST"
    rm -f "$WORK"/blocks-*.txt
fi
"$GAME_BIN" "$ROM" --block-code "$LIST" "$WORK/blocks.txt"
echo "block list: $(grep -vc '^#' "$LIST") blocks"

# (a stage game's code is linked into 3rd Strike's program beside 3rd Strike's own: its names prefixed, its tables
# registered by name; its own headless build finds them the same way)
case "$GAME" in
    sfiii3) PREFIX_ARGS="" ;;
    sfiiin) PREFIX_ARGS="--prefix sfiiin --index-base 10000" ;;
    sfiii2n) PREFIX_ARGS="--prefix sfiii2n --index-base 20000" ;;
    *) PREFIX_ARGS="--prefix $GAME --index-base 30000" ;;
esac
# shellcheck disable=SC2086
python3 "$TOP/cps3recomp/tools/recomp.py" --blocks "$WORK/blocks.txt" --ops "$TOP/cps3recomp/machine/sh2_ops.h" --out "$WORK/gen" $PREFIX_ARGS
cmake -S "$TOP" -B "$BUILD" >/dev/null
ninja -C "$BUILD" "$GAME-headless" >/dev/null
echo "built $GAME_BIN with the translated code"
