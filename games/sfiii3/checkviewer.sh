#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The stage viewer's stages (the play-test of 2026-11-07, the Steam Machine: "guest stages should be available in stage
# viewer mode. also in stage viewer Qs stage is dudleys"). Headless, no GPU device.
#
# Ground truth: each stage as its own fight shows it, a training session on it from the session's Stage row (3rd
# Strike's own board drawing its own stage; New Generation's and 2nd Impact's made alone, which checkguests.sh checks
# against each game's own match), its layers drawn alone (SFIII3_STAGE_PICTURES). Then the viewer on each of its
# entries, its shown picture (the viewer draws the stage alone) every 100 frames and its sound. stagecheck.py names the
# stage each entry shows by its colours: it must be the one the entry names, told apart from the rest; every stage of
# the references must be in the viewer; its music heard, and nothing with "None" chosen.
#
# Before (3bbe95e) the viewer's list was the characters' (21: Q's builds Dudley's stage, Q having none of his own) and
# had no guest stage. No check looked at which stage an entry showed: checkmodes.sh's viewer runs judged the camera and
# the music on one stage.
#
#   games/sfiii3/checkviewer.sh ROM [BUILD] [REFBUILD]   (BUILD: build/sfiii3; REFBUILD: the references' build, BUILD's
#                                                         unless said: one with SFIII3_STAGE_PICTURES)
#
# CHECKVIEWER_JOBS: runs at once (6).
ROM=${1:?usage: checkviewer.sh ROM [BUILD] [REFBUILD]}
BUILD=${2:-build/sfiii3}
REFBUILD=${3:-$BUILD}
JOBS=${CHECKVIEWER_JOBS:-6}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
LIMITED=$(command -v gtimeout || command -v timeout)
wait_slot() {
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
}
choices() { # BINARY FEATURE: its choices, one a line
    "$1" --list-features 2>/dev/null | awk -F '\t' -v f="$2" '$1 == f { print $NF }' | tr '|' '\n'
}
# The references: a training session on each stage of the session's Stage row (but the game's choice), its fight's stage
mkdir -p "$OUT/refs" "$OUT/views"
n=0
while IFS= read -r name; do
    if [ $n -gt 0 ]; then
        wait_slot
        dir="$OUT/refs/$name"
        mkdir -p "$dir"
        (
            SFIII3_STAGE_PICTURES="$dir/p:50" nice -n 20 $LIMITED -k 10 300 "$REFBUILD/sfiii3" --rom "$ROM" --headless --frames 1700 \
                --settings "$OUT/r$n.ini" --eeprom "$OUT/r$n.bin" --features "training_stage=$n" --keys 30:ctraining_start \
                --auto 3:300:1 > /dev/null 2> "$OUT/r$n.log"
            echo $? > "$OUT/r$n.status"
        ) &
    fi
    n=$((n + 1))
done < <(choices "$REFBUILD/sfiii3" training_stage)
# The viewer on each entry (but Infinite): its pictures and its sound; with "None" on each game's first, silence
n=0
quiet=""
while IFS= read -r name; do
    if [ $n -gt 0 ]; then
        wait_slot
        dir="$OUT/views/$n $name"
        mkdir -p "$dir"
        (
            nice -n 20 $LIMITED -k 10 300 "$BUILD/sfiii3" --rom "$ROM" --headless --frames 2600 --settings "$OUT/v$n.ini" \
                --eeprom "$OUT/v$n.bin" --features "viewer_stage=$n,viewer_scroll=2" --keys 30:cviewer_start \
                --shot-every 100:"$dir/v" --audio-dump "$OUT/views/$n.wav" > /dev/null 2> "$OUT/v$n.log"
            echo $? > "$OUT/v$n.status"
        ) &
        case "$name" in
        "Gill's" | "New Generation: Gill's" | "2nd Impact: Gill's") quiet="$quiet $n" ;;
        esac
    fi
    n=$((n + 1))
done < <(choices "$BUILD/sfiii3" viewer_stage)
for q in $quiet; do
    wait_slot
    (
        nice -n 20 $LIMITED -k 10 300 "$BUILD/sfiii3" --rom "$ROM" --headless --frames 2600 --settings "$OUT/q$q.ini" \
            --eeprom "$OUT/q$q.bin" --features "viewer_stage=$q,viewer_music=1" --keys 30:cviewer_start \
            --audio-dump "$OUT/views/$q-quiet.wav" > /dev/null 2> "$OUT/q$q.log"
        echo $? > "$OUT/q$q.status"
    ) &
done
wait
status=0
for s in "$OUT"/*.status; do
    if [ "$(cat "$s")" != 0 ]; then
        echo "checkviewer: $(basename "$s" .status) ended with $(cat "$s") (124: hung)"
        status=1
    fi
done
python3 "$HERE/stagecheck.py" "$OUT/refs" "$OUT/views" "$OUT/strips" || status=1
echo "(pictures and strips: $OUT)"
[ $status = 0 ] && echo "checkviewer: all the same" || echo "checkviewer: DIFFERENT"
exit $status
