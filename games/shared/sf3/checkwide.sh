#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Widescreen in New Generation and 2nd Impact (games/shared/sf3/widescreen.cpp): on every stage of the game, a training
# session started over in the left corner and in the right, with widescreen and with its invisible walls too, and the
# picture then judged (widecheck.py): it is 512 columns, and no column of either margin is empty (one flat colour from
# top to bottom: the picture shown past the stage's end). And with widescreen off the game is the game: the same
# hashes as with the feature never asked for.
#
#   games/shared/sf3/checkwide.sh GAME ROM [BUILD]     (GAME: sfiiin or sfiii2n; BUILD: build/GAME)
GAME=${1:?usage: checkwide.sh GAME ROM [BUILD]}
ROM=${2:?usage: checkwide.sh GAME ROM [BUILD]}
BUILD=${3:-build/$GAME}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
case $GAME in
sfiiin) STAGES=13 ;;
*) STAGES=15 ;;
esac
throttle() { # (four runs at a time)
    while [ "$(jobs -rp | wc -l)" -ge 4 ]; do
        sleep 1
    done
}
run() { # NAME FEATURES: a session, started over in a corner once its round is on (the session's start with a stage
    # chosen takes until about frame 3100), its picture well after
    mkdir -p "$OUT/$1"
    nice -n 20 "$BUILD/$GAME" --rom "$ROM" --headless --frames 4400 --settings "$OUT/$1/settings.ini" --eeprom "$OUT/$1/eeprom.bin" \
        --keys 30:ctraining_start,3000:ctraining_restart --features "$2" --shot-every 4399:"$OUT/$1/p" > /dev/null 2> "$OUT/$1.log"
    echo $? > "$OUT/$1.status"
}
for stage in $(seq 1 $STAGES); do
    for corner in 2 5; do # (restart_at: the left corner, the right with the sides swapped)
        run "wide-$stage-$corner" "widescreen,stage=$stage,restart_at=$corner" & throttle
        run "walls-$stage-$corner" "widescreen,widescreen_walls,stage=$stage,restart_at=$corner" & throttle
    done
done
for way in none off; do
    features=-free_play
    [ $way = off ] && features=-free_play,-widescreen,-widescreen_walls
    nice -n 20 "$BUILD/$GAME" --rom "$ROM" --headless --frames 4000 --auto 7 --settings "$OUT/$way.ini" --eeprom "$OUT/$way.bin" \
        --features "$features" --hash-log "$OUT/$way.hash" > /dev/null 2>&1 & throttle
done
wait
status=0
for s in "$OUT"/*.status; do
    if [ "$(cat "$s")" != 0 ]; then
        echo "checkwide: $(basename "$s" .status) ended with $(cat "$s")"
        status=1
    fi
done
python3 "$HERE/widecheck.py" "$OUT" || status=1
if cmp -s "$OUT/none.hash" "$OUT/off.hash"; then
    echo "widescreen off: the same over $(wc -l < "$OUT/none.hash" | tr -d ' ') frames"
else
    echo "widescreen off: DIFFERENT"
    status=1
fi
[ -n "${CHECKWIDE_KEEP:-}" ] && echo "kept: $OUT"
[ $status = 0 ] && echo "checkwide: all the same" || echo "checkwide: DIFFERENT"
exit $status
