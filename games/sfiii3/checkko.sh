#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The knock-out's check: its sound and its slow motion (checkko.py says what is compared). The runs: a one-player game of
# made-up play to past its first knock-out (frame 5087), headless by default and with --board, its sound dumped and the
# slow motion's bytes traced, a state saved 300 frames before the knock-out; the same game from that state, headless; and
# from that state through the window's frontend (SDL's dummy video: no window, no GPU) with its sound taken by the
# checks' sink at a sound device's pace (CPS3RECOMP_AUDIO_SINK), at the Steam Machine's 48 kHz (PipeWire's quantum of 512)
# and the Mac's 44.1 kHz. Prints what it measured and "checkko: all the same", or what is wrong. Exit 0 when all is well.
# About a minute (two of its runs play in real time, one after the other).
#
#   games/sfiii3/checkko.sh ROM [BUILD]     (BUILD: build/sfiii3)
#
# (checksound.sh runs it at its end)
ROM=${1:?usage: checkko.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${CHECKKO_KEEP:-$(mktemp -d)} # (CHECKKO_KEEP=DIR: the runs kept there)
mkdir -p "$OUT"
GAME=(--auto 7:300:1)
# (CHECKKO_FEATURES=A,B: the runs with those features too, to see that the check finds a fault: board_sound=1, the board's
# JAMMA sound, say)
[ -n "$CHECKKO_FEATURES" ] && GAME+=(--features "$CHECKKO_FEATURES")
SLOW=0200EEC0-0200EEDF # (EXE_flag, SLOW_flag, SLOW_timer: checkko.py)
STATE_AT=4787
AFTER=700 # frames run from the state (the knock-out, its hold and its slow motion, and a little more)
status=0
headless() { # name args...
    local name=$1
    shift
    nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" "$@" > /dev/null 2> "$OUT/$name.log" ||
        { echo "$name: the run failed"; tail -3 "$OUT/$name.log"; status=1; }
}
headless native --frames 5400 "${GAME[@]}" --audio-dump "$OUT/native.wav" --trace "$SLOW:$OUT/native.slow" --state-save "$STATE_AT:$OUT/ko.state" &
headless board --frames 5400 "${GAME[@]}" --board --audio-dump "$OUT/board.wav" --trace "$SLOW:$OUT/board.slow" &
wait
headless from --frames "$AFTER" "${GAME[@]}" --state-load "1:$OUT/ko.state" --audio-dump "$OUT/from.wav" --trace "$SLOW:$OUT/from.slow"
heard() { # the frontend's runs from the state
    for sink in 48000:512 44100:441; do
        rate=${sink%%:*}
        # (in real time: not niced, one at a time, and the frontend's own pacing; no window: SDL's dummy video driver; and
        # no sound device: SDL's dummy sound driver, for a build without the sink)
        SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy CPS3RECOMP_AUDIO_SINK="$OUT/heard-$rate.raw:$sink" "$BUILD/sfiii3" --rom "$ROM" \
            --frames "$AFTER" --settings "$OUT/heard-$rate.ini" --eeprom "$OUT/heard-$rate.bin" "${GAME[@]}" --state-load "1:$OUT/ko.state" \
            --trace "$SLOW:$OUT/heard-$rate.slow" > /dev/null 2> "$OUT/heard-$rate.log" ||
            { echo "heard-$rate: the run failed"; tail -3 "$OUT/heard-$rate.log"; status=1; }
        grep "audio sink" "$OUT/heard-$rate.log" | sed "s/^cps3recomp: /heard-$rate: /"
        if ! cmp -s "$OUT/heard-$rate.slow" "$OUT/from.slow"; then
            echo "heard-$rate: the frontend's game not the headless one's from the same state"
            status=1
        fi
    done
}
heard
# (the frontend's runs are in real time, so a busy machine can hold one up and put a gap in its sound: they are made once
# more before that is called wrong; a fault in the game's own sound fails both times)
if ! python3 -I "$HERE/checkko.py" "$OUT" > "$OUT/result.txt"; then
    echo "(the frontend's runs once more)"
    heard
    python3 -I "$HERE/checkko.py" "$OUT" > "$OUT/result.txt" || status=1
fi
cat "$OUT/result.txt"
if [ $status != 0 ] && ! grep -q "^checkko: DIFFERENT" "$OUT/result.txt"; then
    echo "checkko: DIFFERENT"
fi
[ -z "$CHECKKO_KEEP" ] && rm -rf "$OUT"
exit $status
