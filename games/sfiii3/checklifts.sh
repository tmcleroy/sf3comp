#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The rewrites' check (runtime/lift.h): every rewrite run beside the game's own code on every call and compared
# (CPS3RECOMP_LIFT_CHECK=1: registers, main RAM, the cache RAM and the writes to hardware, in order), over runs that
# have the computer playing: attract mode's demonstration fights (the computer against the computer) and one-player
# games of made-up play; and training sessions (fight/session.h) with the computer as the dummy or as both players,
# the characters and super arts the others never lead to set (--poke, every frame: kChosen, the art), and in one the
# computer's mode now and then set by hand (CPS3RECOMP_POKE, machine.cpp) to the modes and steps the fights reach too
# seldom (in the air after a hit, getting up with a move, the rarer steps of a parry, a few answers): a rewrite is
# compared with the game's code from whatever state it is called in, so a state set by hand checks it as well as one
# met. Exit 0 when every rewrite matched on every call.
#
#   games/sfiii3/checklifts.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKLIFTS_RUNS=first: only the first four runs (attract mode and the one-player games), as before the training ones
# CHECKLIFTS_RUNS=all: nine more, every character as the computer and against it (pairs watched): the per-character
#   routines and lists the others' characters miss (17 runs a round: about twice as long)
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
POKES=("" "" "" "")
if [ "$CHECKLIFTS_RUNS" != first ]; then
    # Training sessions (player 1's made-up play chooses; the fight is on from about frame 1300), the super art
    # gauges kept full: with the computer as the dummy (player 2) or watched (both players the computer). The
    # characters (sfiii3.h's kChosen: 0x02011387 player 1, 0x02011388 player 2, Gill 0 ... Remy 20, as kCharacter) and
    # the arts (each player's art word, 0x0206959C and 0x020695C8: Ryu's third, Oro's second) set every frame (--poke).
    DUMMY="--features dummy_computer,gauge_refill --keys 30:ctraining_start"
    WATCH="--features training_watch,gauge_refill --keys 30:ctraining_start"
    # A side's computer set by hand (CPS3RECOMP_POKE: frame:address:word) every EVERY frames from frame 2000, one
    # state after another: "mode step word word [kDoing kDoingHow [plan]]": its mode words (0x02015572, 8 a side),
    # its record's kDoing and kDoingHow (+0x26, +0x28; - to leave them), the plan or answer (kPattern 0x020155A2, its
    # script from the start: kPlanPlace 0x02015582, 16 a side). (Player 2's, set by hand, are met only with it the
    # dummy: watched, they were not.)
    cycle() {
        local side=$1 every=$2 k=0 f n w out=""
        shift 2
        local states=("$@")
        for ((f = 2000; f < FRAMES; f += every)); do
            read -r -a w <<< "${states[$((k % ${#states[@]}))]}"
            for n in 0 1 2 3; do
                out+="$f:$(printf %X $((0x02015572 + side * 8 + n * 2))):$(printf %X "${w[$n]}"),"
            done
            if [ -n "${w[4]}" ] && [ "${w[4]}" != - ]; then
                out+="$f:$(printf %X $((0x02068C92 + side * 0x498))):${w[4]},"
                out+="$f:$(printf %X $((0x02068C94 + side * 0x498))):$(printf %X "${w[5]}"),"
            fi
            if [ -n "${w[6]}" ]; then
                out+="$f:$(printf %X $((0x020155A2 + side * 2))):$(printf %X "${w[6]}"),"
                out+="$f:$(printf %X $((0x02015582 + side * 16))):0,$f:$(printf %X $((0x02015584 + side * 16))):0,"
            fi
            k=$((k + 1))
        done
        echo "$out"
    }
    NAMES+=(forced shin urien ken)
    ARGS+=("$DUMMY --auto 3:300:1 --poke 2011387=00,2011388=02,20695C9=02"
           "$DUMMY --auto 5:300:1 --poke 2011387=12,2011388=0F"
           "$WATCH --auto 13:300:1 --poke 2011387=0D,2011388=0E"
           "$DUMMY --auto 17:300:1 --poke 2011387=06,2011388=0B")
    # Ryu (player 2) against Gill: in the air (mode 11, each step); getting up with move 46 (mode 10, step 4, each
    # phase); a parry's steps 1-4 (12; 3 in a parry's action, 31); guarding as it is hit in the air (7, hit 8: so
    # Next_Be_Float); the Denjin's plans 55-60 (mode 2); answers 63, 67, 68 and 71 (mode 6)
    forced=$(cycle 1 150 "11 0 0 0" "11 1 0 0" "11 2 0 0" "11 3 0 0" "10 4 0 0" "10 4 1 0" "10 4 2 0" "12 1 0 0" \
        "12 3 0 0 0 31" "12 4 0 0" "12 2 0 0" "7 0 0 0 1 8" "2 0 0 0 - - 55" "2 0 0 0 - - 57" "2 0 0 0 - - 58" \
        "2 0 0 0 - - 59" "2 0 0 0 - - 60" "6 0 0 0 - - 63" "6 0 0 0 - - 67" "6 0 0 0 - - 68" "6 0 0 0 - - 71")
    # Shin Akuma (player 2): a parry done (Flip_2nd), his own way of parrying again
    shin=$(cycle 1 50 "12 2 0 0")
    # Urien (player 1, watched, against Akuma): answers 10 and 104 (Short_Range_Attack, Hi_Jump)
    urien=$(cycle 0 300 "6 0 0 0 - - 10" "6 0 0 0 - - 104")
    # Ken (player 2): answers 165, 245, 248 and 249 (Normal_Attack_SP, Lever_Attack_SP)
    ken=$(cycle 1 300 "6 0 0 0 - - 165" "6 0 0 0 - - 245" "6 0 0 0 - - 248" "6 0 0 0 - - 249")
    POKES+=("$forced" "$shin" "$urien" "$ken")
fi
if [ "$CHECKLIFTS_RUNS" = all ]; then
    # Every character as the computer and against the computer: pairs watched (Oro with his second art, Ryu with his
    # third)
    for pair in "00 01" "03 04" "05 06" "07 08" "0A 09 01" "0C 10" "11 12" "13 14" "0B 02 02"; do
        read -r -a c <<< "$pair"
        NAMES+=("pair${c[0]}${c[1]}")
        ARGS+=("$WATCH --auto $((0x${c[0]} + 31)):300:1 --poke 2011387=${c[0]},2011388=${c[1]}${c[2]:+,20695C9=${c[2]}}")
        POKES+=("")
    done
fi

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
        CPS3RECOMP_POKE="${POKES[$i]}" SFIII3_LIFTS_ONLY="$only" CPS3RECOMP_LIFT_CHECK=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
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
