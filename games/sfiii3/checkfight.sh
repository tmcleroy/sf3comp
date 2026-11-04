#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The fight's rewrites (src/fight_*.cpp) checked where checklifts.sh's runs seldom go: one-player games of made-up play
# with the round's clock held at its last second (--poke: every round a time over), with the players' health held low
# (every hit a knock-out, now and then both at once), with the health held low and every round's end counted as a double
# knock-out (Conclusion_Type held at 1: the judges' rounds, a match won on a double), and with both the clock and the
# health held (every round a time over with the health level: the judges); two-player games (Play_Type 1: the people's records), with the health held
# low; and a one-player game with the demonstration's recordings playing (fight_input.cpp: the flag at 0x02016B6C held
# at 0xFF, and the recordings' places set at the start, as the arcade's never-called 0x060922C8 would); and training
# sessions for the hit decisions (fight_judge.cpp), the dummy (player 2) set to parry (training.cpp's dummy_parry), to
# guard and red parry, to guard with both players' health held low (chip knock-outs, a guard failing), to guard at
# random and break throws (dummy_tech: throws broken, throws met), and Gill against Ryu watched (projectiles meeting,
# effects hitting fighters, counter hits and trades as they come); and a one-player game with only the computer's health
# held low (the made-up play's wins perfects: their message, fight_match.cpp's Game_Manage_7_4-7_6, the bonuses); one
# with the person's health held low (the matches lost: the continue, continued, then not and the game over); and one
# with the computer's held low and ten opponents beaten (0x02015507, VS_Index: the ending). As
# checklifts.sh: every rewrite run beside the game's own code on every call and compared, in layers
# (SFIII3_LIFTS_ONLY), until each was compared or a round reaches none of those left. Exit 0 when every one matched on
# every call.
#
#   games/sfiii3/checkfight.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
ROM=${1:?usage: checkfight.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
NAMES=(timeover lowhealth double draw two twolow recorded parry red chip tech projectiles perfect lose ending)
TRAINING="--keys 30:ctraining_start"
# (0x02011377: the clock's seconds, a byte; 0x02068D0B and 0x020691A3: the players' health, the low byte of the word
# at +0x9E of each record; 0x020113C1: the low byte of Conclusion_Type; 0x02017020 and 0x02017024: the recordings' places, 0x0617AF14 and 0x1C20 bytes on)
ARGS=("--auto 11:300:1 --poke 2011377=01" "--auto 23:300:1 --poke 2068D0B=02,20691A3=02"
    "--auto 29:300:1 --poke 2068D0B=02,20691A3=02,20113C1=01"
    "--auto 5:300:1 --poke 2011377=01,2068D0B=02,20691A3=02" "--auto 31:300:2" "--auto 37:300:2 --poke 2068D0B=02,20691A3=02"
    "--auto 13:300:1 --poke 2016B6C=FF"
    "$TRAINING --features dummy_parry,gauge_refill --auto 3:300:1" "$TRAINING --features dummy_guard=1,dummy_guard_action=1 --auto 5:300:1"
    "$TRAINING --features dummy_guard=1,gauge_refill --auto 9:300:1 --poke 2068D0B=02,20691A3=02"
    "$TRAINING --features dummy_tech=1,dummy_guard=3 --auto 17:300:1 --poke 2011387=06,2011388=02"
    "$TRAINING --features training_watch,gauge_refill --auto 50:300:1 --poke 2011387=00,2011388=02"
    "--auto 7:300:1 --poke 20691A3=01" "--auto 23:300:1 --poke 2068D0B=01" "--auto 7:300:1 --poke 20691A3=01,2015507=0A")
POKES=("" "" "" "" "" "" "1:2017020:0617,1:2017022:AF14,1:2017024:0617,1:2017026:CB34" "" "" "" "" "" "" "" "")

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
        CPS3RECOMP_POKE="${POKES[$i]}" SFIII3_LIFTS_ONLY="$only" CPS3RECOMP_LIFT_CHECK=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
            --settings "$OUT/$name-$round.ini" --eeprom "$OUT/$name-$round.bin" ${ARGS[$i]} > /dev/null 2> "$OUT/$name-$round.log" &
        pids+=($!)
        # (six at a time)
        while [ "$(jobs -rp | wc -l)" -ge 6 ]; do
            sleep 1
        done
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
