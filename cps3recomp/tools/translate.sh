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
# work/<game>/blocks.txt (the block database) and work/<game>/gen/ (the C++) are derived from the game: never
# committed. Running it again adds to the database.
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

record() {
    nice -n 20 "$GAME_BIN" "$ROM" --cpu free --record-blocks "$WORK/blocks-$1.txt" "${@:2}" 2>&1 | grep "block database" || true
}
# (each run into its own file, so that they can run side by side; then merged)
pids=()
record attract --frames "$FRAMES" &
pids+=($!)
for seed in $SEEDS; do
    # shellcheck disable=SC2046
    record "play$seed" --frames "$FRAMES" $(python3 "$TOP/cps3recomp/tools/madeup_input.py" "$seed" "$FRAMES") &
    pids+=($!)
done
wait "${pids[@]}"
touch "$WORK/blocks.txt"
cat "$WORK"/blocks-*.txt "$WORK/blocks.txt" | grep -v '^#' | sort -u > "$WORK/blocks.merged"
{ echo "# cps3recomp block database: address, code bytes (derived from the game: never commit)"; cat "$WORK/blocks.merged"; } > "$WORK/blocks.txt"
rm -f "$WORK"/blocks-*.txt "$WORK/blocks.merged"
echo "block database: $(grep -vc '^#' "$WORK/blocks.txt") blocks"

# (a stage game's code is linked into 3rd Strike's program beside 3rd Strike's own: its names prefixed, its tables
# registered by name; its own headless build finds them the same way)
case "$GAME" in
    sfiii3) PREFIX_ARGS="" ;;
    sfiiin) PREFIX_ARGS="--prefix sfiiin --index-base 10000" ;;
    sfiii2n) PREFIX_ARGS="--prefix sfiii2n --index-base 20000" ;;
    *) PREFIX_ARGS="--prefix $GAME --index-base 30000" ;;
esac
# (the routines' names in comments, where their table has been fetched: tools/names.py fetch)
NAMES_ARGS=""
[ -f "$WORK/functions.tsv" ] && NAMES_ARGS="--names $WORK/functions.tsv"
# shellcheck disable=SC2086
python3 "$TOP/cps3recomp/tools/recomp.py" --blocks "$WORK/blocks.txt" --ops "$TOP/cps3recomp/machine/sh2_ops.h" --out "$WORK/gen" $PREFIX_ARGS $NAMES_ARGS
cmake -S "$TOP" -B "$BUILD" >/dev/null
ninja -C "$BUILD" "$GAME-headless" >/dev/null
echo "built $GAME_BIN with the translated code"
