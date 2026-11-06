#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The guest stages' check (src/stages.cpp: New Generation's and 2nd Impact's stages made from their stage code alone,
# games/sfiii3/DEFINITIVE.md), against ground truth and in play, on every stage of both games, with widescreen off and
# on. Headless, no GPU (the GPU's picture with a guest stage against the CPU's is checknative.sh's, under the GPU lock).
#
# 1. Against the stage game's own match in play (SFIII3_STAGE_COMPARE=play), each of the 28 stages, 4:3 and widescreen:
#    the stage board plays its own match on the stage (made-up play there: its fighters idle for 600 frames, then 600 of
#    walking, jumping and attacking, by turns; its own camera following them, up as they jump), and a second board
#    makes the stage alone from the frame the match is set up, given each frame only what the stage alone is given in a
#    fight (stages.cpp's Give: its camera across and up and its fighters' places, here the match's own) and the dice and
#    frame count. The stage's layers (pixels), its objects and tilemaps, and its picture's zoom, against the match's,
#    every frame: all the same. (The check with SFIII3_STAGE_COMPARE=1, checknative's and DEFINITIVE.md's, handed the
#    stage alone all that the match hands its stage, the fighters' records among it, so the stage alone never made its
#    camera from what it is given: it found nothing of the camera's, and its matches had no stage chosen mid-fight.)
# 2. In play (SFIII3_STAGE_CHECK=1): a one-player game of made-up play against the computer (supers, jumps, knock-outs,
#    rounds), the stage chosen again every 550 frames through every stage of the game (each built mid-fight, as one chosen
#    on the menu is), then the other game's, then none, then one again; 4:3 and widescreen; and the opponent's stages of
#    each game (built between fights). Each frame on a guest stage: its camera where it was sent, across and up (after its
#    frame, within the ends its game keeps); its picture's zoom whole when its own frame is; no music of 3rd Strike's
#    heard with the stage's; no sound of the stage board's playing when a stage is built there; none heard with no
#    stage chosen. All 0.
#
#   games/sfiii3/checkguests.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run in play)
#
# CHECKGUESTS_PLAY_FRAMES: the frames of each run against the match (5000: its fight from about 1350, its first 600
# frames idle); CHECKGUESTS_JOBS: runs at once (6). The stage games' sets beside ROM (sfiiin.zip, sfiii2n.zip).
ROM=${1:?usage: checkguests.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
PLAY_FRAMES=${CHECKGUESTS_PLAY_FRAMES:-5000}
JOBS=${CHECKGUESTS_JOBS:-6}
OUT=$(mktemp -d)
status=0
run() { # name env args...
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
# (src/stages.cpp's choices: 1-13 New Generation's stages 0-12, 14-28 2nd Impact's 0-14, 29 and 30 the opponent's)
PLAYS=()
for wide in "" wide; do
    features=""
    [ -n "$wide" ] && features=",widescreen,widescreen_walls"
    for choice in $(seq 1 28); do
        name="play-$choice${wide:+-wide}"
        PLAYS+=("$name")
        run "$name" SFIII3_STAGE_COMPARE=play "$PLAY_FRAMES" --features "stages=$choice$features"
    done
done
# (each stage chosen again every 550 frames from 2000: the game's every stage, then the other game's first, none, and
# its first again)
switches() { # first last other
    local keys="" f=2000 c
    for c in $(seq "$1" "$2") "$3" 0 "$1"; do
        keys+="$f:fstages=$c,"
        f=$((f + 550))
    done
    echo "${keys%,}"
}
REALS=(real-ng real-2i real-ng-wide real-2i-wide real-opp-ng real-opp-2i)
run real-ng SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 7:300:1 --features stages=1 --keys "$(switches 1 13 14)"
run real-2i SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 11:300:1 --features stages=14 --keys "$(switches 14 28 1)"
run real-ng-wide SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 23:300:1 --features stages=1,widescreen,widescreen_walls --keys "$(switches 1 13 14)"
run real-2i-wide SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 31:300:1 --features stages=14,widescreen,widescreen_walls --keys "$(switches 14 28 1)"
run real-opp-ng SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 7:300:1 --features stages=29
run real-opp-2i SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 13:300:1 --features stages=30,widescreen,widescreen_walls
wait
for name in "${PLAYS[@]}" "${REALS[@]}"; do
    if [ "$(cat "$OUT/$name.status" 2>/dev/null)" != 0 ]; then
        echo "$name: the run failed"
        tail -3 "$OUT/$name.log"
        status=1
    fi
done
for name in "${PLAYS[@]}"; do
    line=$(grep "stages: stage alone against its own match in play" "$OUT/$name.log")
    echo "$name: ${line#stages: stage alone against its own match in play }"
    frames=$(echo "$line" | sed -n 's/.*: \([0-9]*\) frames compared.*/\1/p')
    if [ -z "$line" ] || [ "${frames:-0}" -lt 1000 ] ||
        ! echo "$line" | grep -q "layers 0 different, its objects 0 different, its zoom 0 different, the composed picture 0 different"; then
        grep "^stages: the check" "$OUT/$name.log" | head -3
        status=1
    fi
done
for name in "${REALS[@]}"; do
    line=$(grep "stages: the stage check:" "$OUT/$name.log")
    echo "$name: ${line#stages: the stage check: }"
    if [ -z "$line" ] || echo "$line" | grep -q "^stages: the stage check: 0 frames" ||
        ! echo "$line" | grep -q "across 0, up 0; its picture's zoom not whole with its frame whole: 0; 3rd Strike's music heard with the stage's: 0 (" ||
        ! echo "$line" | grep -q "playing 0; its sound heard with no stage: 0$" || echo "$line" | grep -q "(of 0 frames of the stage's music)"; then
        grep "^stages: the stage check, frame" "$OUT/$name.log" | head -6
        status=1
    fi
done
[ $status = 0 ] && echo "checkguests: all the same" || echo "checkguests: DIFFERENT"
rm -rf "$OUT"
exit $status
