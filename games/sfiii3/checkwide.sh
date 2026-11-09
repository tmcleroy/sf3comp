#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Widescreen switched in the in-game menu while the game runs (the play-test of 2026-11-07, the Steam Machine: "when
# switching to widescreen in the in game menu it shrinks the display and doesnt fill the full wide area, it's like a
# letterbox type thing. works fine when toggled via launcher"). The real frontend (frontend_sdl.cpp, not headless): its
# in-game menu worked as a player works it (CPS3RECOMP_MENU_SCRIPT: opened, Display, Widescreen changed, closed), and
# what its window was shown read back (CPS3RECOMP_WINDOW_SHOT), against a run started with that setting, as the
# launcher starts it (--features widescreen or -widescreen). Both ways: on from 4:3, and off from widescreen.
#
#   cpu-*   SDL's dummy video driver (no window on the screen): the picture through SDL's renderer, in a window of its
#           own shape (a window takes its picture's shape: 16:9 with widescreen, 4:3 without)
#   gpu-*   the GPU presenter as a window has it, drawing into a texture of a full screen's size (1280x720), SDL's
#           offscreen driver (no window), one run at a time under the GPU lock
# Pairs: the opening (each picture the same, pixel for pixel, from 60 frames after the switch), and the stage viewer (its
# pictures drawn on the CPU, its camera a stage's margin from the stage's ends: as wide in the window as started).
#
# Before (3bbe95e) the game gave the frontend no widest picture (Game::widestPicture, as the CPS-2 ports give it), so
# the window was made 4:3 at start-up and never took another shape: 16:9 shown in it letterboxed. checknative.sh's
# window twin and --present-test drew each picture afresh in a presenter shaped for it, so nothing sized once at start
# was ever looked at.
#
#   games/sfiii3/checkwide.sh ROM [BUILD]     (BUILD: build/sfiii3)
#
# CHECKWIDE_GPU=0: the CPU's runs only (no GPU device).
ROM=${1:?usage: checkwide.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
LIMITED=$(command -v gtimeout || command -v timeout)
LOCK=$(command -v lockf >/dev/null && echo "lockf -k /tmp/recomp-gpu.lock" || echo "flock /tmp/recomp-gpu.lock")
SWITCH=900  # (the frame the menu is opened at, in the opening; the stage viewer's: 1700)
run() { # KIND NAME FRAMES ARGS...
    local kind=$1 name=$2 frames=$3
    shift 3
    local env=(SDL_AUDIO_DRIVER=dummy CPS3RECOMP_AUDIO_SINK="$OUT/$name.raw:48000" CPS3RECOMP_WINDOW_SHOT="$OUT/$name-:50")
    if [ "$kind" = gpu ]; then
        env+=(SDL_VIDEO_DRIVER=offscreen CPS3RECOMP_WINDOW_OFFSCREEN=1 CPS3RECOMP_WINDOW_SIZE=1280x720)
        env "${env[@]}" $LOCK $LIMITED -k 10 180 "$BUILD/sfiii3" --rom "$ROM" --frames "$frames" --unthrottled --settings "$OUT/$name.ini" \
            --eeprom "$OUT/$name.bin" "$@" > /dev/null 2> "$OUT/$name.log"
    else
        env "${env[@]}" SDL_VIDEO_DRIVER=dummy nice -n 20 $LIMITED -k 10 300 "$BUILD/sfiii3" --rom "$ROM" --frames "$frames" --unthrottled \
            --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" "$@" > /dev/null 2> "$OUT/$name.log"
    fi
    echo $? > "$OUT/$name.status"
}
# (the menu's script, mdaam: opened, down to Display, into it, Widescreen changed, closed)
KINDS="cpu gpu"
[ "${CHECKWIDE_GPU:-1}" = 0 ] && KINDS=cpu
for kind in $KINDS; do
    for wide in widescreen -widescreen; do
        [ "$wide" = widescreen ] && n=wide || n=narrow
        run "$kind" "$kind-start-$n" 1500 --features "skip_intro,$wide" &
        [ "$kind" = gpu ] && wait
        CPS3RECOMP_MENU_SCRIPT=$SWITCH:mdaam run "$kind" "$kind-switch-$n" 1500 --features "skip_intro,$([ $n = wide ] && echo -widescreen || echo widescreen)" &
        [ "$kind" = gpu ] && wait
        run "$kind" "$kind-viewer-start-$n" 2600 --features "skip_intro,$wide,viewer_stage=18,viewer_scroll=1" --keys 30:cviewer_start &
        [ "$kind" = gpu ] && wait
        CPS3RECOMP_MENU_SCRIPT=1700:mdaam run "$kind" "$kind-viewer-switch-$n" 2600 \
            --features "skip_intro,$([ $n = wide ] && echo -widescreen || echo widescreen),viewer_stage=18,viewer_scroll=1" --keys 30:cviewer_start &
        wait
    done
done
status=0
for s in "$OUT"/*.status; do
    if [ "$(cat "$s")" != 0 ]; then
        echo "checkwide: $(basename "$s" .status) ended with $(cat "$s") (124: hung)"
        status=1
    fi
done
for kind in $KINDS; do
    for n in wide narrow; do
        shape=""
        [ "$kind" = cpu ] && { [ $n = wide ] && shape=16:9 || shape=4:3; }
        python3 "$HERE/widecheck.py" same "$OUT/$kind-start-$n-" "$OUT/$kind-switch-$n-" $((SWITCH + 60)) $shape || status=1
        python3 "$HERE/widecheck.py" edges "$OUT/$kind-viewer-start-$n-" "$OUT/$kind-viewer-switch-$n-" 1800 $shape || status=1
    done
done
echo "(pictures: $OUT)"
[ $status = 0 ] && echo "checkwide: all the same" || echo "checkwide: DIFFERENT"
exit $status
