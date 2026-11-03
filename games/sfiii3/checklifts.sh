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
# The runs go at once, in parallel. A rewrite that no run called is listed ("not reached"): its path was not met.
# (The state hashes of a run with the rewrites and one without are not compared: a rewrite says roughly how long it
# took, not the original's exact cycles, so the game's spare time, and the dice it rolls in it, can part. With the
# arcade timing on the rewrites do not run outside the port's own modes: the board's timing is the game's own code's.)
ROM=${1:?usage: checklifts.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
OUT=$(mktemp -d)
NAMES=(attract cpu7 cpu11 cpu23)
ARGS=("--keys 30:cattract_start" "--auto 7:300:1" "--auto 11:300:1" "--auto 23:300:1")
for i in "${!NAMES[@]}"; do
    name=${NAMES[$i]}
    # shellcheck disable=SC2086
    CPS3RECOMP_LIFT_CHECK=1 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" --settings "$OUT/$name.ini" \
        --eeprom "$OUT/$name.bin" ${ARGS[$i]} > /dev/null 2> "$OUT/$name.log" &
done
wait
status=0
for name in "${NAMES[@]}"; do
    if ! grep -q "cps3recomp: rewrite" "$OUT/$name.log"; then
        echo "$name: no report (the run failed?)"; tail -3 "$OUT/$name.log"; status=1
    fi
    if grep -q "rewrite .* differs" "$OUT/$name.log"; then
        echo "$name: a rewrite differs:"; grep "rewrite .* differs" "$OUT/$name.log" | head -5; status=1
    fi
    if grep "cps3recomp: rewrite" "$OUT/$name.log" | grep -v " 0 different," | sed "s/^cps3recomp: /$name: /" | grep .; then
        status=1
    fi
done
cat "$OUT"/*.log | grep "cps3recomp: rewrite" | awk '{n[$3]+=$4; c++} END {for (k in n) if (n[k] == 0) print "not reached:", k; print length(n), "rewrites"}' | sort
[ $status = 0 ] && echo "checklifts: all the same"
rm -rf "$OUT"
exit $status
