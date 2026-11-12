#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The native interface's check, level (a) (cps3recomp/docs/NATIVE.md): with CPS3RECOMP_NATIVE_CHECK=1 the board keeps
# what the rewritten drawing code hands the native interface beside its own memories, and after every frame compares
# them: the palette (src/video_palette.cpp: the palette DMAs as the board's registers had them and the colours the
# game's code wrote, in order, against the transfers and colours the rewrites handed over; and the whole of colour RAM
# against the native palette); the frame registers (src/video_registers.cpp: the board's latched ones against those
# handed over); the textures (all 8 MB of character RAM against the native store, made from the character DMA lists
# handed over: video_palette.cpp, video_textures.cpp); at every object DMA the game starts, the lists' heads it takes
# against those handed over (src/video_lists.cpp) and the objects it makes against those the interface makes from the
# heads, the registers and sprite RAM's object words (native::FlattenObjects). Level (b): every frame drawn the plain
# way, the native rasteriser's picture (video/raster.cpp, from the interface alone and the game's own memories) against
# renderer.cpp's from the board, pixel for pixel; and the GPU's picture of the same frame (port/native_gpu.cpp, SDL_GPU's
# compute) as the window's presenter shows it (Presenter::ShowNative: the compute passes and the render pass sampling the
# picture in one command buffer, into a texture of its own at 1:1, no window), read back after its fence against the
# rasteriser's (CHECKNATIVE_GPU_READ=1: the compute's picture read back itself instead; CHECKNATIVE_NO_GPU=1: neither). Over attract mode, a one-player game of made-up play (the computer's fights, the
# screens between), a two-player versus game (with widescreen and its invisible walls: the margin and the world's shift), a training
# session, and two games on a guest stage (New Generation's in the one-player game, 2nd Impact's in the versus game with widescreen):
# there the stage board's hand-offs (its game's rewrites run dry on it, src/video_games.cpp) are compared with its chips
# as the game board's are, and the one frame of its stage's layers under 3rd Strike's (the CPU's) with the two board
# models' composed picture, and the GPU's picture of the same frame with the CPU's (both stages made from their stage code
# alone, src/stages.cpp's StageAlone; 2nd Impact's market has pieces in front of the fighters, drawn over
# 3rd Strike's fighters: the one frame's four buffers, native::Frame::bands, and the board models' four pictures,
# Machine::Compose). Then the same rewrites against the game's own
# code (CPS3RECOMP_LIFT_CHECK, with only them registered) over attract mode and the one-player game. Exit 0 when all
# the same.
#
#   games/sfiii3/checknative.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKNATIVE_ARCADE=1: the native runs with the arcade timing on (the arcade_timing feature: the board's exact cycles).
# The game's own code then runs in place of the rewrites (but in the port's own modes, Game::rewritesNeeded: the
# training session), and the hand-off rewrites are run dry before it (runtime/lift.h, Lift::handsOver): what they hand
# over must be the board's as the rewrites' is. (The lift runs are the same either way: the rewrites against the game's
# code.)
# CHECKNATIVE_NO_REWRITES=1: the native runs with the game's own code in place of the rewrites and no dry runs
# (nothing handed over), to see that the check finds that
# With the GPU (not CHECKNATIVE_NO_GPU): two more runs, the window's own GPU path with a guest stage (the window's twin,
# port/main.cpp's CPS3RECOMP_PRESENT_COMPARE: each frame made for the GPU as a window makes it, the guest's interface
# kept as it was at the frame's end (Machine::KeepGpuGuest), shown, read back and compared with the CPU's picture shown
# the same way, every frame), over a one-player game whose stage is chosen again every 330 frames through all 28 of New
# Generation's and 2nd Impact's (each game's board made afresh, its stages built mid-fight), none, and one again; 4:3,
# and widescreen the other way round. (The stage runs above compare the GPU's picture drawn from the guest's interface
# itself; this, the window's copy of it.)
# CHECKNATIVE_JOBS: how many runs at once (6: each takes about 1 GB; all eight at once filled a 16 GB Mac's memory with
# the other checks)
ROM=${1:?usage: checknative.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
REWRITES="AskPalette,StartNextDma,TextColours,ObjectScrolls,TilemapRegisters,ScreenRegisters,PictureZoom,TextPalette,ListHeads,ClearListArea"
NAMES=(attract cpu7 versus training stage-ng stage-2i)
# (the guest stages: the stage games' sets beside ROM, sfiiin.zip and sfiii2n.zip; src/stages.cpp's choices 12, New
# Generation's Ken's, and 20, 2nd Impact's Hugo's: its market has pieces in front of the fighters)
ARGS=("" "--auto 7:300:1" "--auto 31:300:2 --features widescreen,widescreen_walls" "--keys 30:ctraining_start --auto 3:300:1"
    "--auto 7:300:1 --features stage=33" "--auto 31:300:2 --features stage=41,widescreen,widescreen_walls")
status=0
JOBS=${CHECKNATIVE_JOBS:-6}
run() { # name kind args...
    local name=$1 kind=$2
    shift 2
    # (JOBS at a time; each run's exit status kept in a file: macOS's bash 3.2 forgets a job that ended while `jobs` was
    # asked, and then cannot `wait` for it)
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
    if [ "$kind" = present ]; then
        # shellcheck disable=SC2068
        (
            timing=()
            [ -n "$CHECKNATIVE_ARCADE" ] && timing=(--features arcade_timing)
            CPS3RECOMP_PRESENT_COMPARE=1 CPS3RECOMP_PRESENT_COMPARE_SHOTS=different nice -n 20 lockf -k /tmp/recomp-gpu.lock gtimeout -k 30 1800 \
                "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" "${timing[@]}" \
                --present-every 1:"$OUT/$name-" --present-size 768x448 $@ > /dev/null 2> "$OUT/$name.log"
            echo $? > "$OUT/$name.status"
        ) &
    elif [ "$kind" = native ]; then
        # shellcheck disable=SC2068
        (
            [ -n "$CHECKNATIVE_NO_REWRITES" ] && export CPS3RECOMP_NO_REWRITES=1 CPS3RECOMP_NO_DRY_RUNS=1
            [ -z "$CHECKNATIVE_NO_GPU" ] && export CPS3RECOMP_NATIVE_GPU=1
            timing=()
            [ -n "$CHECKNATIVE_ARCADE" ] && timing=(--features arcade_timing)
            [ -z "$CHECKNATIVE_GPU_READ" ] && export CPS3RECOMP_NATIVE_GPU_PRESENT=1
            # One GPU run at a time on the whole machine (every worktree, every agent): several at once kept the GPU
            # from WindowServer until the watchdog restarted the Mac (twice, 2026-11-05). lockf lets go when the run ends. The lock is shared by every recomp project on this Mac, and a run is ended after 30 minutes.
            gpulock=()
            [ -z "$CHECKNATIVE_NO_GPU" ] && gpulock=(lockf -k /tmp/recomp-gpu.lock gtimeout -k 30 1800)
            CPS3RECOMP_STAGE_LOG=1 CPS3RECOMP_NATIVE_CHECK=1 nice -n 20 "${gpulock[@]}" "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
                --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" "${timing[@]}" $@ > /dev/null 2> "$OUT/$name.log"
            echo $? > "$OUT/$name.status"
        ) &
    else
        # shellcheck disable=SC2068
        (
            SFIII3_LIFTS_ONLY="$REWRITES" CPS3RECOMP_LIFT_CHECK=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
                --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" $@ > /dev/null 2> "$OUT/$name.log"
            echo $? > "$OUT/$name.status"
        ) &
    fi
}
for i in "${!NAMES[@]}"; do
    run "${NAMES[$i]}" native ${ARGS[$i]}
done
PRESENTS=()
if [ -z "$CHECKNATIVE_NO_GPU" ]; then
    # (the stages chosen again every 330 frames from 2000: src/stages.cpp's choices 1-13 New Generation's, 14-28 2nd
    # Impact's; then none, then one again)
    keys=""
    f=2000
    for c in $(seq 1 28) 0 3; do
        keys+="$f:fstage=$((c + 21)),"
        f=$((f + 330))
    done
    wkeys=""
    f=2000
    for c in $(seq 28 -1 1) 0 25; do
        wkeys+="$f:fstage=$((c + 21)),"
        f=$((f + 330))
    done
    PRESENTS=(present-stages present-stages-wide)
    run present-stages present --auto 7:300:1 --features stage=24 --keys "${keys%,}"
    run present-stages-wide present --auto 31:300:1 --features stage=46,widescreen,widescreen_walls --keys "${wkeys%,}"
fi
run lifts-attract lifts
run lifts-cpu7 lifts --auto 7:300:1
wait
for name in "${PRESENTS[@]}"; do
    line=$(grep "cps3recomp: present compare:" "$OUT/$name.log")
    echo "$name: ${line#cps3recomp: present compare: }"
    compared=$(echo "$line" | sed -n 's/.*, \([0-9]*\) compared with the CPU.*/\1/p')
    if [ "$(cat "$OUT/$name.status" 2>/dev/null)" != 0 ] || [ -z "$line" ] || [ "${compared:-0}" -lt 5000 ] || ! echo "$line" | grep -q ", 0 different ("; then
        grep "^present compare, frame" "$OUT/$name.log" | head -3
        tail -2 "$OUT/$name.log"
        status=1
    fi
done
for name in "${NAMES[@]}" lifts-attract lifts-cpu7; do
    if [ "$(cat "$OUT/$name.status" 2>/dev/null)" != 0 ]; then
        echo "$name: the run failed"
        tail -3 "$OUT/$name.log"
        status=1
    fi
done
for name in "${NAMES[@]}"; do
    if [ -n "$CHECKNATIVE_ARCADE" ]; then
        line=$(grep "cps3recomp: hand-overs made by dry runs" "$OUT/$name.log")
        echo "$name: arcade timing: ${line:-no dry runs}"
        grep "has no native form" "$OUT/$name.log" | head -3
        grep -q "has no native form" "$OUT/$name.log" && status=1
        # (the training session runs the rewrites from its start: no dry runs there)
        [ -z "$line" ] && [ -z "$CHECKNATIVE_NO_REWRITES" ] && [ "$name" != training ] && status=1
    fi
    line=$(grep "cps3recomp: native palette:" "$OUT/$name.log")
    echo "$name: palette: ${line#cps3recomp: native palette: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q " 0 different;.* 0 frames with the events different"; then
        grep "^native palette, frame" "$OUT/$name.log" | grep -v "frame registers" | head -3
        status=1
    fi
    line=$(grep "cps3recomp: native frame registers:" "$OUT/$name.log")
    echo "$name: frame registers: ${line#cps3recomp: native frame registers: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different"; then
        grep "frame registers different" "$OUT/$name.log" | head -3
        status=1
    fi
    line=$(grep "cps3recomp: native textures:" "$OUT/$name.log")
    echo "$name: textures: ${line#cps3recomp: native textures: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different"; then
        grep "textures different" "$OUT/$name.log" | head -3
        status=1
    fi
    line=$(grep "cps3recomp: native objects:" "$OUT/$name.log")
    echo "$name: objects: ${line#cps3recomp: native objects: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different"; then
        grep "^native objects, frame" "$OUT/$name.log" | head -3
        status=1
    fi
    line=$(grep "cps3recomp: native pixels:" "$OUT/$name.log")
    echo "$name: pixels: ${line#cps3recomp: native pixels: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different"; then
        grep "^native pixels, frame" "$OUT/$name.log" | head -3
        status=1
    fi
    if [ -z "$CHECKNATIVE_NO_GPU" ]; then
        line=$(grep "cps3recomp: native GPU pixels:" "$OUT/$name.log")
        echo "$name: GPU pixels: ${line#cps3recomp: native GPU pixels: }"
        if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
            grep "^native GPU" "$OUT/$name.log" | head -3
            status=1
        fi
    fi
    line=$(grep "cps3recomp: native object lists:" "$OUT/$name.log")
    echo "$name: object lists: ${line#cps3recomp: native object lists: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q " 0 different,"; then
        grep "^native lists, frame" "$OUT/$name.log" | head -3
        status=1
    fi
    case $name in stage-*)
        # The guest stage: the stage board's own hand-overs (its rewrites run dry on it, video_games.cpp) are in the
        # lines above beside the game board's; the one frame of the two producers' layers (its stage's under 3rd
        # Strike's), drawn by the native rasteriser, against the two board models' composed picture; and the GPU's
        # picture of the same one frame against the CPU's
        line=$(grep "cps3recomp: a second board runs" "$OUT/$name.log")
        echo "$name: ${line#cps3recomp: }"
        [ -z "$line" ] && status=1
        line=$(grep "cps3recomp: the stage boards' hand-overs made by dry runs" "$OUT/$name.log")
        echo "$name: ${line:-the stage board: no dry runs}"
        [ -z "$line" ] && [ -z "$CHECKNATIVE_NO_REWRITES" ] && status=1
        line=$(grep "cps3recomp: native pixels with a stage:" "$OUT/$name.log")
        echo "$name: pixels with a stage: ${line#cps3recomp: native pixels with a stage: }"
        if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different"; then
            grep "^native pixels with a stage, frame" "$OUT/$name.log" | head -3
            status=1
        fi
        if [ -z "$CHECKNATIVE_NO_GPU" ]; then
            line=$(grep "cps3recomp: native GPU pixels with a stage:" "$OUT/$name.log")
            echo "$name: GPU pixels with a stage: ${line#cps3recomp: native GPU pixels with a stage: }"
            if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
                grep "^native GPU pixels with a stage" "$OUT/$name.log" | head -3
                status=1
            fi
        fi
        ;;
    esac
done
for name in lifts-attract lifts-cpu7; do
    grep "cps3recomp: rewrite" "$OUT/$name.log" | sed "s/^cps3recomp: /$name: /"
    if grep -q "rewrite .* differs" "$OUT/$name.log" || grep "cps3recomp: rewrite" "$OUT/$name.log" | grep -qv " 0 different,"; then
        grep "rewrite .* differs" "$OUT/$name.log" | head -3
        status=1
    fi
done
[ $status = 0 ] && echo "checknative: all the same" || echo "checknative: DIFFERENT"
rm -rf "$OUT"
exit $status
