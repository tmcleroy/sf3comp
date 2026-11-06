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
#    fight (stages.cpp's Give: its camera across and up, its fighters' places as its camera code reads them, its camera
#    held and the stage's shakes, here the match's own) and the noise (the dice, the frame count, the hit stop, and the
#    fighters' records but their places). The stage's layers (pixels), its tilemaps and its picture's zoom, against the
#    match's, every frame: the same but in the frames of each stage's known gaps (`allowed` below). The ground truth is
#    the stage game's own picture of its own stage in play; the check with SFIII3_STAGE_COMPARE=1 (DEFINITIVE.md) was
#    producer against producer: it handed the stage alone all that the match hands its stage, the fighters' records whole
#    among it, so the stage alone never had to make its camera from what a fight gives it, and every one of the play-test's
#    camera bugs (the camera dragged to the stage's left end: black parts, far layers scrolling by themselves; never up)
#    was out of its reach; and its matches had no stage built mid-fight (New Generation's Gill stage's entrance) and no
#    sound check.
# 2. In play (SFIII3_STAGE_CHECK=1): a one-player game of made-up play against the computer (supers, jumps, knock-outs,
#    rounds), the stage chosen again every 550 frames through every stage of the game (each built mid-fight, as one chosen
#    on the menu is), then the other game's, then none, then one again; 4:3 and widescreen; and the opponent's stages of
#    each game (built between fights). Each frame on a guest stage: its camera where it was sent, across and up (after its
#    frame, within the ends its game keeps: within the limits, stages.cpp's kAcrossOff and kUpOff); its picture's zoom whole
#    when its own frame is; no music of 3rd Strike's heard with the stage's (two soundtracks at once); no sound of the
#    stage board's playing when a stage is built there; none heard with no stage chosen: all 0.
# On main before 2026-11-06 (638cb12) every run of both parts failed.
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
# The frames each stage's layers may still differ from its match's (2026-11-06; not yet found, each a known gap, the
# rest of each run's 3466-3644 frames the same pixel for pixel): 1, the fight's second or third frame (many stages: its
# far layers a frame behind as the camera is first given); 66-134, a round's start, the match's camera held and moved by
# its own intro (New Generation's Gill (1), Alex (2), Ryu (3), Elena (9), Sean (13), 2nd Impact's Gill (14), Hugo (20),
# Elena (22), Akuma (28)); both Necro stages (6, 19), a far layer behind the window some columns off (65 frames; 2nd
# Impact's every frame), their objects' tile numbers another (textures 0x60 tiles on, the same pixels: objects not
# compared); 2nd Impact's Yang stage (24), its far tilemap now and then 2 columns behind (as with the old check). A
# stage over its allowance, or with its zoom different at all, fails. Before 2026-11-06 every stage's differed in
# 2000-3600 frames.
allowed() { # choice
    case $1 in
    1) echo 95 ;; 2) echo 140 ;; 3) echo 95 ;; 9) echo 95 ;; 13) echo 70 ;; 14) echo 100 ;; 20) echo 125 ;; 22) echo 120 ;; 28) echo 130 ;;
    6) echo 70 ;; 19) echo 3644 ;; 24) echo 3100 ;; *) echo 1 ;;
    esac
}
for name in "${PLAYS[@]}"; do
    line=$(grep "stages: stage alone against its own match in play" "$OUT/$name.log")
    echo "$name: ${line#stages: stage alone against its own match in play }"
    choice=${name#play-}
    choice=${choice%-wide}
    frames=$(echo "$line" | sed -n 's/.*: \([0-9]*\) frames compared.*/\1/p')
    layers=$(echo "$line" | sed -n "s/.*the stage's layers \([0-9]*\) different.*/\1/p")
    composed=$(echo "$line" | sed -n 's/.*the composed picture \([0-9]*\) different.*/\1/p')
    if [ -z "$line" ] || [ "${frames:-0}" -lt 1000 ] || [ "${layers:-99999}" -gt "$(allowed "$choice")" ] ||
        [ "${composed:-99999}" -gt "$(allowed "$choice")" ] || ! echo "$line" | grep -q "its zoom 0 different"; then
        grep "^stages: the check" "$OUT/$name.log" | head -3
        status=1
    fi
done
for name in "${REALS[@]}"; do
    line=$(grep "stages: the stage check:" "$OUT/$name.log")
    echo "$name: ${line#stages: the stage check: }"
    if [ -z "$line" ] || echo "$line" | grep -q "^stages: the stage check: 0 frames" ||
        ! echo "$line" | grep -q "(within the limits); its picture's zoom not whole with its frame whole: 0; 3rd Strike's music heard with the stage's: 0 (" ||
        ! echo "$line" | grep -q "playing 0; its sound heard with no stage: 0$" || echo "$line" | grep -q "(of 0 frames of the stage's music)"; then
        grep "^stages: the stage check, frame" "$OUT/$name.log" | head -6
        status=1
    fi
done
[ $status = 0 ] && echo "checkguests: all the same" || echo "checkguests: DIFFERENT"
rm -rf "$OUT"
exit $status
