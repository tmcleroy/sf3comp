#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The native sound's check, level (c) (cps3recomp/docs/NATIVE.md): with CPS3RECOMP_NATIVE_CHECK=1 the board keeps its
# own mixer (pcm.cpp, from the sound chip's registers) beside the native mixer (sound/native_sound.h, from the events
# the game's sound driver hands over: src/sound_driver.cpp), and at the end of every frame compares the two, every
# sample of each side, voice by voice; and the voices as the events set them (what each plays, its loop, pitch and
# volume, the keys) against the chip's registers. Over the four runs checknative.sh makes (attract mode, a one-player
# game of made-up play, a two-player versus game, a training session), each twice: as the port runs by default (the
# rewrites hand the events over) and with arcade timing (CPS3RECOMP_BUS_TIMING=1: the game's own code runs, and the
# hooks work out its events and let them go at its writes to the chip). Then the default build's sound end to end:
# what it plays (the native mixer, --audio-dump) against what --board plays (pcm.cpp), the same bytes, over attract
# mode and the one-player game, by default and with arcade timing. Arcade timing's state hashes with the hooks and
# without them (SFIII3_SOUND_NO_HOOKS), over the four runs: the same every frame. And the rewrites against the game's own code
# (CPS3RECOMP_LIFT_CHECK, with only them registered) over attract mode and the one-player game. Prints "all the same",
# or the first frame, voice and sample that differ. Exit 0 when all the same.
#
#   games/sfiii3/checksound.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKSOUND_NO_LIFTS=1: the runs with neither the rewrites nor the hooks (nothing handed over), to see that the check
# finds that. CHECKSOUND_JOBS: how many runs at once (4: each takes about 1 GB, and more at once filled a 16 GB Mac's
# memory until its watchdog restarted it)
ROM=${1:?usage: checksound.sh ROM [BUILD] [FRAMES]}
BUILD=${2:-build/sfiii3}
FRAMES=${3:-12000}
OUT=$(mktemp -d)
REWRITES="SoundSetUp,SoundStopAll,SoundChannel"
NAMES=(attract cpu7 versus training)
ARGS=("" "--auto 7:300:1" "--auto 31:300:2" "--keys 30:ctraining_start --auto 3:300:1")
JOBS=${CHECKSOUND_JOBS:-4}
status=0
runs=()
slot() { # (waits for a free slot: JOBS runs at once)
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
}
game() { # name arcade(0/1) args...: a run of the game, its log in OUT/name.log, its exit status in OUT/name.status
    local name=$1 arcade=$2
    shift 2
    slot
    # (the exit status kept in a file: macOS's bash 3.2 forgets a job that ended while `jobs` was asked, and then cannot
    # `wait` for it)
    # shellcheck disable=SC2068
    (
        [ -n "$CHECKSOUND_NO_LIFTS" ] && export CPS3RECOMP_NO_LIFTS=1
        [ "$arcade" = 1 ] && export CPS3RECOMP_BUS_TIMING=1
        nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$FRAMES" --settings "$OUT/$name.ini" \
            --eeprom "$OUT/$name.bin" $@ > /dev/null 2> "$OUT/$name.log"
        echo $? > "$OUT/$name.status"
    ) &
    runs+=("$name")
}
checked() { # name arcade args...
    local name=$1 arcade=$2
    shift 2
    # shellcheck disable=SC2068
    CPS3RECOMP_NATIVE_CHECK=1 game "$name" "$arcade" $@
}
for i in "${!NAMES[@]}"; do
    # shellcheck disable=SC2086
    checked "${NAMES[$i]}" 0 ${ARGS[$i]}
    # shellcheck disable=SC2086
    checked "arcade-${NAMES[$i]}" 1 ${ARGS[$i]}
done
DUMPS=(attract cpu7)
for i in 0 1; do
    for arcade in 0 1; do
        # shellcheck disable=SC2086
        game "heard-$arcade-${DUMPS[$i]}" $arcade --audio-dump "$OUT/native-$arcade-${DUMPS[$i]}.wav" ${ARGS[$i]}
        # shellcheck disable=SC2086
        game "board-$arcade-${DUMPS[$i]}" $arcade --board --audio-dump "$OUT/board-$arcade-${DUMPS[$i]}.wav" ${ARGS[$i]}
    done
done
# arcade timing's state, with the hooks and without them (SFIII3_SOUND_NO_HOOKS): the same, every frame (they take
# none of the board's time and change nothing)
for i in "${!NAMES[@]}"; do
    # shellcheck disable=SC2086
    game "hooks-${NAMES[$i]}" 1 --hash-log "$OUT/hooks-${NAMES[$i]}.hash" ${ARGS[$i]}
    # shellcheck disable=SC2086
    SFIII3_SOUND_NO_HOOKS=1 game "nohooks-${NAMES[$i]}" 1 --hash-log "$OUT/nohooks-${NAMES[$i]}.hash" ${ARGS[$i]}
done
lifts() { # name args...
    local name=$1
    shift
    # shellcheck disable=SC2068
    SFIII3_LIFTS_ONLY="$REWRITES" CPS3RECOMP_LIFT_CHECK=1 game "$name" 0 $@
}
lifts lifts-attract
lifts lifts-cpu7 --auto 7:300:1
wait
for name in "${runs[@]}"; do
    if [ "$(cat "$OUT/$name.status" 2>/dev/null)" != 0 ]; then
        echo "$name: the run failed"
        tail -3 "$OUT/$name.log"
        status=1
    fi
done
for name in "${NAMES[@]}"; do
    for run in "$name" "arcade-$name"; do
        line=$(grep "cps3recomp: native sound:" "$OUT/$run.log")
        echo "$run: samples: ${line#cps3recomp: native sound: }"
        if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
            grep "^native sound, frame" "$OUT/$run.log" | head -3
            status=1
        fi
        line=$(grep "cps3recomp: native voices:" "$OUT/$run.log")
        echo "$run: voices: ${line#cps3recomp: native voices: }"
        if [ -z "$line" ] || ! echo "$line" | grep -q ", 0 different$"; then
            grep "^native voices, frame" "$OUT/$run.log" | head -3
            status=1
        fi
    done
done
for name in "${DUMPS[@]}"; do
    for arcade in 0 1; do
        a="$OUT/native-$arcade-$name.wav" b="$OUT/board-$arcade-$name.wav"
        what=$([ $arcade = 1 ] && echo "arcade-$name" || echo "$name")
        if [ -s "$a" ] && cmp -s "$a" "$b"; then
            echo "$what: heard: the native mixer's sound and --board's the same ($(($(wc -c < "$a") / 4)) samples)"
        else
            echo "$what: heard: the native mixer's sound and --board's DIFFERENT ($(cmp "$a" "$b" 2>&1 | head -1))"
            status=1
        fi
    done
done
for name in "${NAMES[@]}"; do
    a="$OUT/hooks-$name.hash" b="$OUT/nohooks-$name.hash"
    if [ -s "$a" ] && cmp -s "$a" "$b"; then
        echo "arcade-$name: state hashes with the hooks and without them the same ($(wc -l < "$a" | tr -d ' ') frames)"
    else
        echo "arcade-$name: state hashes with the hooks and without them DIFFERENT ($(cmp "$a" "$b" 2>&1 | head -1))"
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
