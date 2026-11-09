#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Pop-in at the edges of the widescreen picture (the play-test of 2026-11-07, the Steam Machine: "in widescreen (at least
# stage viewer mode) stage elements that would be offscreen on 4:3 pop in at the edges of 16:9"). Headless, no GPU device.
#
# SFIII3_POPIN_CHECK (src/stages.cpp): each frame a stage is shown in widescreen, its objects and each of its tilemaps
# drawn alone against the frame before: a stage piece that appears or goes whole inside the picture, in the columns
# widescreen adds, is pop-in (the same between them, in the board's own 384, is what the 4:3 game shows too, and is only
# counted); a tilemap cell that changes once its layer is lined up likewise; and an end column with nothing of the stage
# in it. Over every stage: the stage viewer scrolling across each of its entries (3rd Strike's stages on the stage
# board, New Generation's and 2nd Impact's made alone), and fights in training sessions of made-up play on each of the
# session's Stage row (3rd Strike's own board drawing its own stage, the guest stages composed with it).
# Strips of the first pop-ins (the frame before | the frame | red where it appeared) in the run's folder.
#
#   games/sfiii3/checkpopin.sh ROM [BUILD]     (BUILD: build/sfiii3)
#
# CHECKPOPIN_JOBS: runs at once (6). CHECKPOPIN_ONLY=viewer or fights: that half alone.
ROM=${1:?usage: checkpopin.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
JOBS=${CHECKPOPIN_JOBS:-6}
OUT=$(mktemp -d)
LIMITED=$(command -v gtimeout || command -v timeout)
choices() { # FEATURE: its choices, one a line
    "$BUILD/sfiii3" --list-features 2>/dev/null | awk -F '\t' -v f="$1" '$1 == f { print $NF }' | tr '|' '\n'
}
run() { # NAME FRAMES ARGS...
    local name=$1 frames=$2
    shift 2
    while [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; do
        sleep 1
    done
    mkdir -p "$OUT/$name"
    (
        SFIII3_POPIN_CHECK=1 SFIII3_POPIN_STRIPS="$OUT/$name" SFIII3_BATTLE_SEED=7 nice -n 20 $LIMITED -k 10 600 "$BUILD/sfiii3" --rom "$ROM" --headless \
            --frames "$frames" --settings "$OUT/$name.ini" --eeprom "$OUT/$name.bin" "$@" > /dev/null 2> "$OUT/$name.log"
        echo $? > "$OUT/$name.status"
    ) &
}
if [ "${CHECKPOPIN_ONLY:-viewer}" = viewer ]; then
    n=0
    while IFS= read -r name; do
        if [ $n -gt 0 ]; then
            run "viewer-$n" 3000 --features "widescreen,viewer_stage=$n,viewer_scroll=2,viewer_music=1" --keys 30:cviewer_start
        fi
        n=$((n + 1))
    done < <(choices viewer_stage)
fi
if [ "${CHECKPOPIN_ONLY:-fights}" = fights ]; then
    n=0
    while IFS= read -r name; do
        if [ $n -gt 0 ]; then
            run "fight-$n" 3000 --features "widescreen,training_stage=$n" --keys 30:ctraining_start --auto $((n + 3)):300:1
        fi
        n=$((n + 1))
    done < <(choices training_stage)
fi
wait
status=0
for s in "$OUT"/*.status; do
    if [ "$(cat "$s")" != 0 ]; then
        echo "checkpopin: $(basename "$s" .status) ended with $(cat "$s") (124: hung)"
        status=1
    fi
done
python3 "$(dirname "$0")/popincheck.py" "$OUT" || status=1
echo "(logs and strips: $OUT)"
[ $status = 0 ] && echo "checkpopin: all the same" || echo "checkpopin: DIFFERENT"
exit $status
