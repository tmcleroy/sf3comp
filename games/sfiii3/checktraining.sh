#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Training mode, checked for the play-test bugs of 2026-11-06 and its dummy's options (each run headless, no GPU device,
# player 1's play a scripted movie made by trainingcheck.py, judged by trainingcheck.py from the run's log: the
# training's frames (SFIII3_TRAINING_LOG), its parries (SFIII3_PARRIES), the HUD's lines (CPS3RECOMP_HUD_LOG), and the
# frame data log's file):
#
#   combo        light kicks landing one at a time on a standing dummy, and pairs of light punches that combo: the combo
#                readout counts the pairs as combos of two and nothing more
#                (it counted every hit of the session as one)
#   parrykind    grounded normals parried by the dummy: read as forward or down, never anti-air (a tap forward opens the
#                anti-air window too, and that was named)
#   parry_NN     the dummy's parry at 100, 50 and 0%: of the attacks that meet it, that share parried (in range)
#   tech_NN      the dummy's throw escape at 100, 50 and 0%: of player 1's throws, that share escaped (in range)
#   stunmash     the dummy stunned again and again, mashing: nothing held once the stun is over, and no move of its own
#                (it mashed on, and the mashing's moves came out)
#   level_N      the computer as player 2 at its lowest and its highest level: its play time held there, and it takes
#                more of player 1's health at the highest
#   inputs       the input display: nothing shown for neutral (it showed 5)
#   framelog     the frame data log: a line for every move, the same numbers for the same move, a note on screen as it
#                writes (it stopped for good at the first move hit out of: three moves in a session)
#   features     the training page has no Stage viewer (the Alternative modes' stage viewer is the mode)
#
#   games/sfiii3/checktraining.sh ROM [BUILD]     (BUILD: build/sfiii3)
ROM=${1:?usage: checktraining.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
CHECK="python3 $HERE/trainingcheck.py"
START=1300 # (the frame from which player 1's script plays: the round is on by then)
# (Ryu against Ryu: the characters chosen, bytes 0x02011387 and 0x02011388, set while no match is on)
RYUS="--poke 2011387=02,2011388=02"

run() { # NAME FRAMES SCRIPT ARGS...: a training session of player 1 playing SCRIPT, its stderr in OUT/NAME.log
    local name=$1 frames=$2 script=$3
    shift 3
    mkdir -p "$OUT/$name"
    $CHECK movie "$script" $START "$frames" "$OUT/$name/movie.txt"
    SFIII3_TRAINING_LOG=1 SFIII3_PARRIES=1 CPS3RECOMP_HUD_LOG=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$frames" \
        --settings "$OUT/$name/settings.ini" --eeprom "$OUT/$name/eeprom.bin" --keys 30:ctraining_start \
        --movie "$OUT/$name/movie.txt" "$@" > /dev/null 2> "$OUT/$name.log"
    echo $? > "$OUT/$name.status"
}
throttle() { # (four runs at a time: the other gates and agents run theirs too)
    while [ "$(jobs -rp | wc -l)" -ge 4 ]; do
        sleep 1
    done
}
run combo 5000 combos $RYUS --features damage_readout & throttle
run parrykind 6000 normals $RYUS --features dummy_parry=1,parry_readout & throttle
for rate in 100 50 0; do
    # (dummy_parry's choices: Off, 100%, 90% ... 10%)
    value=$(( rate == 0 ? 0 : 11 - rate / 10 ))
    run parry_$rate 9000 normals $RYUS --features dummy_parry=$value & throttle
    run tech_$rate 9000 throws $RYUS --features dummy_tech=$value & throttle
done
run stunmash 7000 pokes $RYUS --poke 206960B=03 --features dummy_stun_mash & throttle
run level_1 7000 idle $RYUS --features dummy_computer,dummy_level=1 & throttle
run level_8 7000 idle $RYUS --features dummy_computer,dummy_level=8 & throttle
run inputs 3000 inputs $RYUS --features input_display & throttle
run framelog 6000 normals $RYUS --features frame_log,dummy_guard=3 & throttle
wait
"$BUILD/sfiii3" --list-features > "$OUT/features.txt" 2> /dev/null
echo $? > "$OUT/features.status"

status=0
judge() { # NAME CHECK...
    local name=$1
    shift
    if [ "$(cat "$OUT/$name.status")" != 0 ]; then
        echo "$name: the run failed"; tail -3 "$OUT/$name.log"; status=1; return
    fi
    if ! result=$($CHECK "$@" 2>&1); then
        status=1
    fi
    echo "$name: $(echo "$result" | tr '\n' ' ')"
}
judge combo combo "$OUT/combo.log"
judge parrykind parrykind "$OUT/parrykind.log"
judge parry_100 parryrate 70 130 "$OUT/parry_100.log" "$OUT/parry_50.log" "$OUT/parry_0.log"
judge tech_100 techrate 70 130 "$OUT/tech_100.log" "$OUT/tech_50.log" "$OUT/tech_0.log"
judge stunmash stunmash "$OUT/stunmash.log"
judge level_1 level "$OUT/level_1.log" "$OUT/level_8.log"
judge inputs inputs "$OUT/inputs.log"
judge framelog framelog "$OUT/framelog.log" "$OUT/framelog/frame_data.csv"
judge features features "$OUT/features.txt"
[ $status = 0 ] && echo "checktraining: all the same"
[ -n "$KEEP" ] && echo "kept: $OUT" || rm -rf "$OUT"
exit $status
