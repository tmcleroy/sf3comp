#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The native sound's check, level (c) (cps3recomp/docs/NATIVE.md): with CPS3RECOMP_NATIVE_CHECK=1 the board keeps the
# native mixer (sound/native_sound.h) beside its own sound chip, fed with the events the rewritten sound driver hands
# over (src/sound_driver.cpp), and at the end of every frame compares the mixer's samples with pcm.cpp's, every sample
# of each side, voice by voice; and the voices as the events set them (what each plays, its loop, pitch and volume, the
# keys) against the chip's registers. Over the four runs checknative.sh makes: attract mode, a one-player game of
# made-up play, a two-player versus game and a training session. Then the same rewrites against the game's own code
# (CPS3RECOMP_LIFT_CHECK, with only them registered) over attract mode and the one-player game. Prints "all the same",
# or the first frame, voice and sample that differ. Exit 0 when all the same.
#
#   games/sfiii3/checksound.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKSOUND_NO_REWRITES=1: the native runs with the game's own code in place of the rewrites (nothing handed over),
# to see that the check finds that
ROM=${1:?usage: checksound.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
OUT=$(mktemp -d)
REWRITES="SoundSetUp,SoundStopAll,SoundChannel"
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
            [ -n "$CHECKSOUND_NO_REWRITES" ] && export CPS3RECOMP_NO_REWRITES=1
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
    line=$(grep "cps3recomp: native sound:" "$OUT/$name.log")
    echo "$name: samples: ${line#cps3recomp: native sound: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
        grep "^native sound, frame" "$OUT/$name.log" | head -3
        status=1
    fi
    line=$(grep "cps3recomp: native voices:" "$OUT/$name.log")
    echo "$name: voices: ${line#cps3recomp: native voices: }"
    if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
        grep "^native voices, frame" "$OUT/$name.log" | head -3
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
[ $status = 0 ] && echo "checksound: all the same" || echo "checksound: DIFFERENT"
rm -rf "$OUT"
exit $status
