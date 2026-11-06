#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# 3rd Strike's own stages as the picture shows them (src/stages.cpp's checks; the guest stages' are checkguests.sh's),
# for the second play-test's bugs (2026-11-06, the Steam Machine). Headless, no GPU device (the GPU's picture against
# the CPU's is checknative.sh's).
#
# 1. Jitter (SFIII3_JITTER_CHECK=1): the fighters standing still, put at 7 places across the stage in turn (close
#    together, apart, far apart, from one end to the other); once the camera has had its time to get there, the stage
#    as shown (its layers drawn) must not move from one frame to the next. In training sessions on every stage (3rd
#    Strike's own board), and in the endless battle with its stage behind (the stage board, its camera following 3rd
#    Strike's: the modes' stages), every stage; 4:3 and widescreen.
# 2. Layers (SFIII3_LAYER_CHECK=1): 3rd Strike's own fight on each stage is the ground truth (its own board draws its
#    stage pieces in front of its fighters over them: Akuma's stage's roots, ...); the stage and the rest of that board
#    composed as the picture composes a stage board's stage with the fighters of the board it is shown with (one frame of
#    two producers, and two pictures: the modes' stages) must give the board's own picture at every pixel a fighter is
#    drawn at. Training sessions of made-up play on every stage, 4:3 and widescreen.
# Before (ea48e7b) no check compared a composed picture with a fighter behind a stage piece: the stage boards' checks
# compared the stage's layers alone, or the composed picture with another composed picture made the same way; and none
# looked at a still picture from one frame to the next.
#
#   games/sfiii3/checkpictures.sh ROM [BUILD]     (BUILD: build/sfiii3)
#
# CHECKPICTURES_JOBS: runs at once (6).
ROM=${1:?usage: checkpictures.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
JOBS=${CHECKPICTURES_JOBS:-6}
OUT=$(mktemp -d)
status=0
run() { # name env frames args...
    local name=$1 env=$2 frames=$3
    shift 3
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
    # shellcheck disable=SC2068
    (
        export $env
        nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$frames" --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" $@ \
            > /dev/null 2> "$OUT/$name.log"
        echo $? > "$OUT/$name.status"
    ) &
}
# (training sessions started again every EVERY frames, each on the next stage: training_stage, 3rd Strike's stage
# numbers plus one; its fight from about 600 frames after its start)
sessions() { # first last every
    local keys="" f=30 s
    for s in $(seq "$1" "$2"); do
        keys+="$f:ftraining_stage=$s,$f:ctraining_start,"
        f=$((f + $3))
    done
    echo "${keys%,}"
}
JITTERS=() LAYERS=()
for wide in "" wide; do
    features=""
    [ -n "$wide" ] && features="--features widescreen,widescreen_walls"
    for first in 1 8 15; do
        name="jitter-$first${wide:+-wide}"
        JITTERS+=("$name")
        # shellcheck disable=SC2086
        run "$name" SFIII3_JITTER_CHECK=1 11300 --keys "$(sessions $first $((first + 6)) 1600)" --auto 3:300:1 $features
        name="layers-$first${wide:+-wide}"
        LAYERS+=("$name")
        # shellcheck disable=SC2086
        run "$name" SFIII3_LAYER_CHECK=1 7800 --keys "$(sessions $first $((first + 6)) 1100)" --auto 5:300:1 $features
    done
    # (the endless battle's stage behind, shuffled, on each stage: SFIII3_BATTLE_STAGE, from 0)
    for stage in $(seq 0 20); do
        name="behind-$stage${wide:+-wide}"
        JITTERS+=("$name")
        # shellcheck disable=SC2086
        run "$name" "SFIII3_JITTER_CHECK=1 SFIII3_BATTLE_STAGE=$stage SFIII3_BATTLE_SEED=7" 3000 --keys 30:cendless_start \
            --features "endless_stage=1${wide:+,widescreen,widescreen_walls}"
    done
done
wait
for name in "${JITTERS[@]}" "${LAYERS[@]}"; do
    if [ "$(cat "$OUT/$name.status" 2>/dev/null)" != 0 ]; then
        echo "$name: the run failed"
        tail -3 "$OUT/$name.log"
        status=1
    fi
done
for name in "${JITTERS[@]}"; do
    line=$(grep "stages: the jitter check:" "$OUT/$name.log")
    echo "$name: ${line#stages: }"
    # (the endless battle's: its stage behind, the stage board's, measured)
    if [ -z "$line" ] || ! echo "$line" | grep -q "the stage's picture moved in 0 " || echo "$line" | grep -q "check: [0-9]* places held, 0 frames" ||
        { [ "${name#behind}" != "$name" ] && echo "$line" | grep -q "(0 of them"; }; then
        grep "^stages: the jitter check, frame" "$OUT/$name.log" | head -4
        status=1
    fi
done
for name in "${LAYERS[@]}"; do
    line=$(grep "stages: the layer check:" "$OUT/$name.log")
    echo "$name: ${line#stages: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q "different from the board's own picture in 0, the two pictures in 0$" ||
        echo "$line" | grep -q "check: 0 frames"; then
        grep "^stages: the layer check, frame" "$OUT/$name.log" | head -2
        status=1
    fi
done
[ $status = 0 ] && echo "checkpictures: all the same" || echo "checkpictures: DIFFERENT"
rm -rf "$OUT"
exit $status
