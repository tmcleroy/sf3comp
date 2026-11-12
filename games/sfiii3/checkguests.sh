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
# 3. The second play-test's (2026-11-06, the Steam Machine), each of which the two parts above could not see:
#    - jitter (SFIII3_JITTER_CHECK=1): training sessions with the fighters standing still, put at 7 places across the stage
#      in turn (close together, apart, far apart), every guest stage chosen in turn, 4:3 and widescreen; once the camera
#      has had its time to get there, the stage as shown (its layers drawn) must not move from one frame to the next. Part 2
#      allowed its camera a column or two off where it was sent, on average and in a frame, and judged camera words: New
#      Generation's camera going a column past and back every frame (2 pixels each way) was within both;
#    - layers (SFIII3_LAYER_CHECK=1, in part 1's runs): the stage game's own match on its stage is the ground truth; its
#      stage and its fighters composed as the picture composes a stage with another board's fighters (one frame of two
#      producers, and two pictures) must give its own picture at every pixel a fighter is drawn at: a stage piece in front
#      of a fighter covers it. Part 1 compared the stage's layers alone, and the composed picture with 3rd Strike's
#      fighters over each of two stages composed the same way: nothing there compared a fighter with a stage piece in
#      front of it;
#    - the training session's Stage row (SFIII3_SESSION_STAGES=1): every guest stage chosen there, a session started on
#      it, the stage shown is that one. No check started a session on a guest stage from that row (it had none).
# On main before 2026-11-06 (638cb12) every run of parts 1 and 2 failed; on ea48e7b every run of part 3 but some of the
# jitter's (2nd Impact's: see DEFINITIVE.md).
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
        run "$name" "SFIII3_STAGE_COMPARE=play SFIII3_LAYER_CHECK=1" "$PLAY_FRAMES" --features "stage=$((choice + 21))$features"
    done
done
# (each stage chosen again every 550 frames from 2000: the game's every stage, then the other game's first, none, and
# its first again)
switches() { # first last other
    local keys="" f=2000 c
    for c in $(seq "$1" "$2") "$3" 0 "$1"; do
        keys+="$f:fstage=$((c + 21)),"
        f=$((f + 550))
    done
    echo "${keys%,}"
}
REALS=(real-ng real-2i real-ng-wide real-2i-wide real-opp-ng real-opp-2i)
run real-ng SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 7:300:1 --features stage=22 --keys "$(switches 1 13 14)"
run real-2i SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 11:300:1 --features stage=35 --keys "$(switches 14 28 1)"
run real-ng-wide SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 23:300:1 --features stage=22,widescreen,widescreen_walls --keys "$(switches 1 13 14)"
run real-2i-wide SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 31:300:1 --features stage=35,widescreen,widescreen_walls --keys "$(switches 14 28 1)"
run real-opp-ng SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 7:300:1 --features stage=50
run real-opp-2i SFIII3_STAGE_CHECK=1 "$FRAMES" --auto 13:300:1 --features stage=51,widescreen,widescreen_walls
# (3: the jitter: a training session from frame 30, its fight from about 300; each game's stages chosen in turn, a stage
# every 1000 frames, its 7 places held 130 frames each)
inturn() { # first last
    local keys="" f=300 c
    for c in $(seq "$1" "$2"); do
        keys+="$f:fstage=$((c + 21)),"
        f=$((f + 1000))
    done
    echo "${keys%,}"
}
JITTERS=(jitter-ng jitter-2i jitter-ng-wide jitter-2i-wide)
run jitter-ng SFIII3_JITTER_CHECK=1 14000 --keys "30:ctraining_start,$(inturn 1 13)" --auto 3:300:1
run jitter-2i SFIII3_JITTER_CHECK=1 16000 --keys "30:ctraining_start,$(inturn 14 28)" --auto 3:300:1
run jitter-ng-wide SFIII3_JITTER_CHECK=1 14000 --keys "30:ctraining_start,$(inturn 1 13)" --auto 3:300:1 --features widescreen,widescreen_walls
run jitter-2i-wide SFIII3_JITTER_CHECK=1 16000 --keys "30:ctraining_start,$(inturn 14 28)" --auto 3:300:1 --features widescreen,widescreen_walls
# (3: the training session's Stage row: stage 22-34 New Generation's stages 0-12, 35-49 2nd Impact's 0-14; a
# session started on each in turn, every 1000 frames of the keys' count, seven a run: each fought for some 450 frames
# before the next; a run's frames count the session's way to its fight, run fast, too: twice as many)
sessions() { # first last
    local keys="" f=30 c
    for c in $(seq "$1" "$2"); do
        keys+="$f:fstage=$c,$f:ctraining_start,"
        f=$((f + 1000))
    done
    echo "${keys%,}"
}
SESSIONS=(sessions-22 sessions-29 sessions-35 sessions-42)
for first in 22 29 35 42; do
    last=$((first + 6))
    [ $first = 29 ] && last=34
    [ $first = 42 ] && last=49
    run "sessions-$first" SFIII3_SESSION_STAGES=1 $(((last - first + 1) * 2000 + 500)) --keys "$(sessions $first $last)" --auto 3:300:1
done
wait
for name in "${PLAYS[@]}" "${REALS[@]}" "${JITTERS[@]}" "${SESSIONS[@]}"; do
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
    # (the layers: at the fighters' pixels, the stage and its fighters composed both ways as the board drew them. One known
    # gap: 2nd Impact's Yang stage (24), its match's own entrance, a fighter drawn behind the stage's far layers (priority
    # 110), in about 90 frames: one rule of what is in front, the fighters' frontmost, cannot put a fighter behind the
    # stage's layers too; 3rd Strike's fighters on a guest stage are never there)
    line=$(grep "stages: the layer check:" "$OUT/$name.log")
    echo "$name: ${line#stages: }"
    one=$(echo "$line" | sed -n "s/.*different from the board's own picture in \([0-9]*\), the two pictures in \([0-9]*\)$/\1/p")
    two=$(echo "$line" | sed -n "s/.*different from the board's own picture in \([0-9]*\), the two pictures in \([0-9]*\)$/\2/p")
    most=0
    [ "$choice" = 24 ] && most=100
    if [ -z "$line" ] || [ "${one:-99999}" -gt $most ] || [ "${two:-99999}" -gt $most ] || echo "$line" | grep -q "check: 0 frames"; then
        grep "^stages: the layer check, frame" "$OUT/$name.log" | head -2
        status=1
    fi
done
for name in "${JITTERS[@]}"; do
    line=$(grep "stages: the jitter check:" "$OUT/$name.log")
    echo "$name: ${line#stages: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q "the stage's picture moved in 0 " || echo "$line" | grep -q "check: [0-9]* places held, 0 frames"; then
        grep "^stages: the jitter check, frame" "$OUT/$name.log" | head -4
        status=1
    fi
done
# (each session on the stage chosen for it, from its fight's first frame: that game's stage of that number, 150 frames or
# more, in the order chosen, and nothing else shown)
for name in "${SESSIONS[@]}"; do
    line=$(grep "stages: training sessions' stages shown:" "$OUT/$name.log")
    echo "$name: ${line#stages: }"
    first=${name#sessions-}
    last=$((first + 6))
    [ $first = 29 ] && last=34
    [ $first = 42 ] && last=49
    want=""
    for c in $(seq "$first" "$last"); do
        if [ "$c" -le 34 ]; then want+="sfiiin:$((c - 22)) "; else want+="sfiii2n:$((c - 35)) "; fi
    done
    got=$(echo "${line#*shown:}" | tr ';' '\n' | sed -n 's/^ *\([a-z0-9]*:[0-9]*\) \([0-9]*\) frames$/\1 \2/p' | awk '$2 >= 150 || /^own/ {print $1}' | tr '\n' ' ')
    if [ "$got" != "$want" ]; then
        echo "  wanted: $want"
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
