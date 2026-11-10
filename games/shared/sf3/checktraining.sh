#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Training mode in New Generation and 2nd Impact (games/shared/sf3/training.cpp), checked as 3rd Strike's is
# (games/sfiii3/checktraining.sh): each run headless, no GPU device, a training session started by the menu's command,
# player 1's play a scripted movie made by trainingcheck.py (Ryu against Ken: the characters the cursors start on),
# judged by trainingcheck.py from the run's log: the training's frames (SF3_TRAINING_LOG), its parries (SF3_PARRIES),
# its moves (SF3_MOVES), the HUD's lines (CPS3RECOMP_HUD_LOG), and the frame data log's file.
#
#   session      heavy hits on the dummy for a minute: the clock stays at 99, nobody is knocked out, the round goes on
#   refill       health refills: full again after each hit left alone
#   gauge        gauge refills: a super art again and again, the gauge full between; none without
#   restart      "Start over" in the left corner, and in the right with the sides swapped
#   stage        the Stage row's ninth choice: the round is fought on that stage (the game's stage byte)
#   stance       the dummy crouches, jumps, stands
#   guard        the dummy guards player 1's normals when told to, and is hit when not
#   record       player 1's controls recorded as player 2's, then replayed
#   getup        the dummy's quick stand: up sooner after a sweep
#   stunmash     the dummy stunned again and again, mashing: out sooner, nothing held once the stun is over
#   computer     the dummy played by the game: it attacks
#   combo        the combo readout: single hits, and pairs that combo, counted as such
#   inputs       the input display: what is pressed, nothing for neutral
#   stun         the stun readout follows the hits
#   framedata    a standing light punch out of reach: the same numbers every time, adding up to the move's length
#   framelog     the frame data log: a line for every move, the same numbers for the same move
#   parrykind    grounded normals parried by the dummy: read as forward or down
#   parry_NN     the dummy's parry at 100, 50 and 0%: of the attacks that meet it, that share parried (in range)
#   tech_NN      (2nd Impact) the dummy's throw escape at 100, 50 and 0%
#   features     the feature list has what the game has, and not what it lacks
#
#   games/shared/sf3/checktraining.sh GAME ROM [BUILD]     (GAME: sfiiin, sfiii2n or sfiii3n; BUILD: build/GAME)
GAME=${1:?usage: checktraining.sh GAME ROM [BUILD]}
ROM=${2:?usage: checktraining.sh GAME ROM [BUILD]}
BUILD=${3:-build/$GAME}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
CHECK="python3 $HERE/trainingcheck.py"
START=1400 # (the frame from which player 1's script plays: the round is on by then)

run() { # NAME FRAMES SCRIPT ARGS...: a training session of player 1 playing SCRIPT, its stderr in OUT/NAME.log
    local name=$1 frames=$2 script=$3
    shift 3
    mkdir -p "$OUT/$name"
    $CHECK movie "$script" $START "$frames" "$OUT/$name/movie.txt"
    SF3_TRAINING_LOG=1 SF3_PARRIES=1 SF3_MOVES=1 CPS3RECOMP_HUD_LOG=1 nice -n 20 "$BUILD/$GAME" --rom "$ROM" --headless --frames "$frames" \
        --settings "$OUT/$name/settings.ini" --eeprom "$OUT/$name/eeprom.bin" --keys "${KEYS:-30:ctraining_start}" \
        --movie "$OUT/$name/movie.txt" "$@" > /dev/null 2> "$OUT/$name.log"
    echo $? > "$OUT/$name.status"
}
throttle() { # (four runs at a time: the other gates and agents run theirs too)
    while [ "$(jobs -rp | wc -l)" -ge 4 ]; do
        sleep 1
    done
}
# (player 2's stun record: the most it takes, the word's low byte, poked low so that every hit stuns; and the stage
# number of the Stage row's ninth choice)
case $GAME in
sfiiin) STUN=200D993 STAGE=9 ;; # (the ninth choice: Oro's, number 9)
sfiii3n) STUN=206960B STAGE=8 ;; # (3rd Strike's 990608 revision: player 2's stun record as 3rd Strike's; Elena's, number 8)
*) STUN=200ED8B STAGE=8 ;;      # (Elena's, number 8)
esac
run session 5000 heavies --features damage_readout & throttle
run refill 5000 pokes --features health_refill & throttle
run gauge 4000 supers --features gauge_refill & throttle
run nogauge 3000 supers & throttle
# (the command's key by the screen's ticks: the session's start runs some 500 frames unseen in its first)
KEYS=30:ctraining_start,1700:ctraining_restart run restart_left 2900 idle --features restart_at=2 & throttle
KEYS=30:ctraining_start,1700:ctraining_restart run restart_right 2900 idle --features restart_at=5 & throttle
run stage 1800 idle --features stage=9 & throttle
run crouch 2000 idle --features dummy_crouch & throttle
run jump 2000 idle --features dummy_jump & throttle
run stand 2000 idle --features dummy_stand & throttle
run guard 5000 normals --features dummy_guard=1 & throttle
run noguard 5000 normals & throttle
KEYS=30:ctraining_start,900:fdummy_record=1,1500:fdummy_record=0,1501:fdummy_replay=1 run record 3000 normals & throttle
run getup 3000 sweeps --features dummy_getup=1 & throttle
run nogetup 3000 sweeps & throttle
run stunmash 6000 pokes --poke $STUN=03 --features dummy_stun_mash & throttle
run nostunmash 6000 pokes --poke $STUN=03 & throttle
run computer 4000 idle --features dummy_computer & throttle
run combo 5000 combos --features damage_readout & throttle
run inputs 3000 inputs --features input_display & throttle
run stun 5000 heavies --features stun_readout & throttle
run framedata 3000 light --features frame_data & throttle
run framelog 6000 normals --features frame_log,dummy_guard=3 & throttle
run parrykind 6000 normals --features dummy_parry=1,parry_readout & throttle
for rate in 100 50 0; do
    # (dummy_parry's and dummy_tech's choices: Off, 100%, 90% ... 10%)
    value=$(( rate == 0 ? 0 : 11 - rate / 10 ))
    run parry_$rate 9000 normals --features dummy_parry=$value & throttle
    if [ "$GAME" = sfiii2n ]; then
        run tech_$rate 9000 throws --features dummy_tech=$value & throttle
    fi
done
wait
"$BUILD/$GAME" --list-features > "$OUT/features.txt" 2> /dev/null
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
judge session session "$OUT/session.log"
judge refill refill "$OUT/refill.log"
judge gauge gauge "$OUT/gauge.log" "$OUT/nogauge.log"
judge restart_left restart "$OUT/restart_left.log" left
judge restart_right restart "$OUT/restart_right.log" right-swapped
judge stage stage "$OUT/stage.log" $STAGE
judge crouch stance "$OUT/crouch.log" "$OUT/jump.log" "$OUT/stand.log"
judge guard guard "$OUT/guard.log" "$OUT/noguard.log"
judge record record "$OUT/record.log"
judge getup getup "$OUT/getup.log" "$OUT/nogetup.log"
judge stunmash stunmash "$OUT/stunmash.log" "$OUT/nostunmash.log"
judge computer computer "$OUT/computer.log"
judge combo combo "$OUT/combo.log"
judge inputs inputs "$OUT/inputs.log"
judge stun stun "$OUT/stun.log"
judge framedata framedata "$OUT/framedata.log"
judge framelog framelog "$OUT/framelog.log" "$OUT/framelog/frame_data.csv"
judge parrykind parrykind "$OUT/parrykind.log"
judge parry_100 parryrate 70 130 "$OUT/parry_100.log" "$OUT/parry_50.log" "$OUT/parry_0.log"
if [ "$GAME" = sfiii2n ]; then
    judge tech_100 techrate 70 130 "$OUT/tech_100.log" "$OUT/tech_50.log" "$OUT/tech_0.log"
    judge features features "$OUT/features.txt" stage hit_boxes gauge_refill stun_readout parry_readout dummy_parry dummy_tech dummy_getup \
        dummy_stun_mash dummy_computer frame_log -dummy_guard_action -dummy_level
else
    judge features features "$OUT/features.txt" stage hit_boxes gauge_refill stun_readout parry_readout dummy_parry dummy_getup \
        dummy_stun_mash dummy_computer frame_log -dummy_tech -dummy_guard_action -dummy_level
fi
[ $status = 0 ] && echo "checktraining: all the same"
[ -n "$KEEP" ] && echo "kept: $OUT" || rm -rf "$OUT"
exit $status
