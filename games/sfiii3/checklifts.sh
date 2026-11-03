#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The rewrites' check (runtime/lift.h): every rewrite run beside the game's own code on every call and compared
# (CPS3RECOMP_LIFT_CHECK=1: registers, main RAM, the cache RAM and the writes to hardware, in order), over runs that
# have the computer playing: attract mode's demonstration fights (the computer against the computer) and one-player
# games of made-up play. Exit 0 when every rewrite matched on every call.
#
#   games/sfiii3/checklifts.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# In layers: while a rewrite is compared, what it calls runs as the game's own code, so a rewrite called only from
# inside another rewrite is not met while that one is registered. So the runs are made again with only the rewrites
# not compared yet registered (SFIII3_LIFTS_ONLY), the game's own code then calling them, until each has been
# compared or a round reaches none of those left ("not reached": its path was not met in these runs).
#
# (The state hashes of a run with the rewrites and one without are not compared: a rewrite says roughly how long it
# took, not the original's exact cycles, so the game's spare time, and the dice it rolls in it, can part. With the
# arcade timing on the rewrites do not run outside the port's own modes: the board's timing is the game's own code's.)
ROM=${1:?usage: checklifts.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
NAMES=(attract cpu7 cpu11 cpu23)
ARGS=("--keys 30:cattract_start" "--auto 7:300:1" "--auto 11:300:1" "--auto 23:300:1")

# Every rewrite registered: the names in the sources' lift tables ({"Name", 0x...}, not in a remark), unless
# SFIII3_LIFTS_ONLY names some
if [ -n "$SFIII3_LIFTS_ONLY" ]; then
    left=$(echo "$SFIII3_LIFTS_ONLY" | tr ',' '\n' | sort -u)
else
    left=$(grep -h '^ *{"[A-Za-z0-9_]*", 0x' "$HERE"/src/*.cpp | sed 's/^ *{"\([A-Za-z0-9_]*\)".*/\1/' | sort -u)
fi
status=0
checked=0
round=0
while [ -n "$left" ]; do
    round=$((round + 1))
    only=$(echo "$left" | paste -sd, -)
    pids=()
    for i in "${!NAMES[@]}"; do
        name=${NAMES[$i]}
        # shellcheck disable=SC2086
        SFIII3_LIFTS_ONLY="$only" CPS3RECOMP_LIFT_CHECK=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
            --settings "$OUT/$name-$round.ini" --eeprom "$OUT/$name-$round.bin" ${ARGS[$i]} > /dev/null 2> "$OUT/$name-$round.log" &
        pids+=($!)
    done
    for i in "${!NAMES[@]}"; do
        if ! wait "${pids[$i]}"; then
            echo "round $round, ${NAMES[$i]}: the run failed"; tail -3 "$OUT/${NAMES[$i]}-$round.log"; status=1
        fi
    done
    for name in "${NAMES[@]}"; do
        log="$OUT/$name-$round.log"
        if grep -q "rewrite .* differs" "$log"; then
            echo "round $round, $name: a rewrite differs:"; grep "rewrite .* differs" "$log" | head -5; status=1
        fi
        if grep "cps3recomp: rewrite" "$log" | grep -v " 0 different," | sed "s/^cps3recomp: /round $round, $name: /" | grep .; then
            status=1
        fi
    done
    # (those compared this round: called at least once)
    done_now=$(cat "$OUT"/*-"$round".log | grep "cps3recomp: rewrite" | awk '$4 > 0 {sub(":", "", $3); print $3}' | sort -u)
    reached=$(comm -12 <(echo "$left") <(echo "$done_now"))
    count=$(echo "$reached" | grep -c .)
    echo "round $round: $count compared"
    [ "$count" = 0 ] && break
    checked=$((checked + count))
    left=$(comm -23 <(echo "$left") <(echo "$done_now"))
done
for name in $left; do
    echo "not reached: $name"
done
echo "$checked rewrites compared"
[ $status = 0 ] && echo "checklifts: all the same"
rm -rf "$OUT"
exit $status
