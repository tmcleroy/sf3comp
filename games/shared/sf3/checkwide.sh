#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# Widescreen in the games built from the shared sources (games/shared/sf3/widescreen.cpp): on every stage of the game,
# a training session started over in the left corner and in the right, with widescreen, and the picture then judged
# (widecheck.py): it is as wide as it should be, and no column of either margin is empty (one flat colour from top to
# bottom: the picture shown past the stage's end). New Generation and 2nd Impact show the board's own wide picture
# (495 columns); 3rd Strike 990608 the sources' own (512), with its invisible walls too. Then widescreen switched on and
# off again in a session: the board starts again, its picture wide and then not. And with widescreen off the game is
# the game: the same hashes as with the feature never asked for.
#
#   games/shared/sf3/checkwide.sh GAME ROM [BUILD]     (GAME: sfiiin, sfiii2n or sfiii3n; BUILD: build/GAME)
GAME=${1:?usage: checkwide.sh GAME ROM [BUILD]}
ROM=${2:?usage: checkwide.sh GAME ROM [BUILD]}
BUILD=${3:-build/$GAME}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
case $GAME in
sfiiin) STAGES=13 ;;
*) STAGES=15 ;;
esac
# (the board's own wide picture: where a far layer's art ends a few columns short of the game's own camera limits, as on
# New Generation's stages 11 and 13 and 2nd Impact's 3 and 4, that is what the board shows: up to 4 columns allowed)
WIDTH=495 WALLS= FEW=4
[ "$GAME" = sfiii3n ] && WIDTH=512 WALLS=1 FEW=0
# (2nd Impact's stage 9 is narrower than the wide picture's camera ever needs to move: its cliff at the left is black
# art, the same in both corners)
EXCUSED=
[ "$GAME" = sfiii2n ] && EXCUSED=wide-9-2,wide-9-5
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
        [ -n "$WALLS" ] && { run "walls-$stage-$corner" "widescreen,widescreen_walls,stage=$stage,restart_at=$corner" & throttle; }
    done
done
for way in none off; do
    features=-free_play
    [ $way = off ] && features=-free_play,-widescreen,-widescreen_walls
    nice -n 20 "$BUILD/$GAME" --rom "$ROM" --headless --frames 4000 --auto 7 --settings "$OUT/$way.ini" --eeprom "$OUT/$way.bin" \
        --features "$features" --hash-log "$OUT/$way.hash" > /dev/null 2>&1 & throttle
done
# (switched on in a session, and off again: the picture as switched each time; with the board's own wide picture the
# board starts again for it)
mkdir -p "$OUT/switch"
nice -n 20 "$BUILD/$GAME" --rom "$ROM" --headless --frames 7200 --settings "$OUT/switch/settings.ini" --eeprom "$OUT/switch/eeprom.bin" \
    --keys 30:ctraining_start,3000:fwidescreen=1,3100:ctraining_start,6000:fwidescreen=0 --features -widescreen,stage=5 \
    --shot-every 600:"$OUT/switch/s" > /dev/null 2> "$OUT/switch.log" & throttle
wait
status=0
for s in "$OUT"/*.status; do
    if [ "$(cat "$s")" != 0 ]; then
        echo "checkwide: $(basename "$s" .status) ended with $(cat "$s")"
        status=1
    fi
done
python3 "$HERE/widecheck.py" "$OUT" "$WIDTH" "$EXCUSED" "$FEW" || status=1
widths=$(python3 -c "
import struct, sys
print(' '.join(str(struct.unpack('>I', open('$OUT/switch/s%06d.png' % f, 'rb').read()[16:20])[0]) for f in (2400, 5400, 7200)))")
if [ "$widths" = "384 $WIDTH 384" ]; then
    echo "switched in a session: the picture as switched each time ($widths columns)"
else
    echo "switched in a session: the pictures are $widths columns, not 384 $WIDTH 384"
    status=1
fi
if cmp -s "$OUT/none.hash" "$OUT/off.hash"; then
    echo "widescreen off: the same over $(wc -l < "$OUT/none.hash" | tr -d ' ') frames"
else
    echo "widescreen off: DIFFERENT"
    status=1
fi
[ -n "${CHECKWIDE_KEEP:-}" ] && echo "kept: $OUT"
[ $status = 0 ] && echo "checkwide: all the same" || echo "checkwide: DIFFERENT"
exit $status
