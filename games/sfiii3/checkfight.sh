#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The fight's rewrites (src/fight_*.cpp) checked where checklifts.sh's runs seldom go: one-player games of made-up play
# with the round's clock held at its last second (--poke: every round a time over), and with the players' health held
# low (every hit a knock-out, now and then both at once). As checklifts.sh: every rewrite run beside the game's own code
# on every call and compared, in layers (SFIII3_LIFTS_ONLY), until each was compared or a round reaches none of those
# left. Exit 0 when every one matched on every call.
#
#   games/sfiii3/checkfight.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
ROM=${1:?usage: checkfight.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
NAMES=(timeover lowhealth)
# (0x02011377: the clock's seconds, a byte; 0x02068D0B and 0x020691A3: the players' health, the low byte of the word
# at +0x9E of each record)
ARGS=("--auto 11:300:1 --poke 2011377=01" "--auto 23:300:1 --poke 2068D0B=02,20691A3=02")

left=$(grep -h '^ *{"[A-Za-z0-9_]*", 0x' "$HERE"/src/fight_*.cpp | sed 's/^ *{"\([A-Za-z0-9_]*\)".*/\1/' | sort -u)
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
[ $status = 0 ] && echo "checkfight: all the same"
rm -rf "$OUT"
exit $status
