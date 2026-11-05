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
# heads, the registers and sprite RAM's object words (native::FlattenObjects). Over attract mode, a one-player game of made-up play (the computer's fights, the
# screens between), a two-player versus game and a training session. Then the same rewrites against the game's own
# code (CPS3RECOMP_LIFT_CHECK, with only them registered) over attract mode and the one-player game. Exit 0 when all
# the same.
#
#   games/sfiii3/checknative.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKNATIVE_NO_REWRITES=1: the native runs with the game's own code in place of the rewrites (nothing handed over),
# to see that the check finds that
ROM=${1:?usage: checknative.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
REWRITES="AskPalette,StartNextDma,TextColours,ObjectScrolls,TilemapRegisters,ScreenRegisters,PictureZoom,TextPalette,ListHeads,ClearListArea"
NAMES=(attract cpu7 versus training)
ARGS=("" "--auto 7:300:1" "--auto 31:300:2" "--keys 30:ctraining_start --auto 3:300:1")
status=0
pids=()
run() { # name kind args...
    local name=$1 kind=$2
    shift 2
    if [ "$kind" = native ]; then
        # shellcheck disable=SC2068
        (
            [ -n "$CHECKNATIVE_NO_REWRITES" ] && export CPS3RECOMP_NO_REWRITES=1
            CPS3RECOMP_NATIVE_CHECK=1 exec nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
                --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" $@ > /dev/null 2> "$OUT/$name.log"
        ) &
    else
        # shellcheck disable=SC2068
        SFIII3_LIFTS_ONLY="$REWRITES" CPS3RECOMP_LIFT_CHECK=1 nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" \
            --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" $@ > /dev/null 2> "$OUT/$name.log" &
    fi
    pids+=($!)
}
for i in "${!NAMES[@]}"; do
    run "${NAMES[$i]}" native ${ARGS[$i]}
done
run lifts-attract lifts
run lifts-cpu7 lifts --auto 7:300:1
for pid in "${pids[@]}"; do
    wait "$pid" || { echo "a run failed"; status=1; }
done
for name in "${NAMES[@]}"; do
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
    line=$(grep "cps3recomp: native object lists:" "$OUT/$name.log")
    echo "$name: object lists: ${line#cps3recomp: native object lists: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q " 0 different,"; then
        grep "^native lists, frame" "$OUT/$name.log" | head -3
        status=1
    fi
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
