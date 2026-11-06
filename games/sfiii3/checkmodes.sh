#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The Alternative modes, checked for the play-test bugs of 2026-11-06 (each run headless, no GPU device, judged by
# modecheck.py from its log, pictures and RAM):
#
#   model       the model viewer: black from its start until the round is fought, the character shown from then
#               (the battle's intro was shown, the character not framed in it)
#   colours     the colour editor: the same
#   viewer      the stage viewer, scrolling by itself: its camera moves from the first frame its stage is shown, and is
#               where it was sent (for some 300 frames it stayed at the stage's middle, a few columns this way and that)
#   viewer_tune ... its own music heard; viewer_quiet: none heard with "None"
#   teams       two tag team battles of random teams in one run: not the same teams (the dice's seed was the frame
#               count of a machine reset a moment before: 0, every time)
#   tag         a tag team battle of made-up play: no spare machine's frame run in a frame of the fight shown, tags and
#               the round's start included (each frame took 6-20 ms more while the next tags' fights were made)
#   survival    survival, the opponent's health held low: player 1's super art gauge the same as each next opponent
#               comes on (it was empty)
#   ringout     ring out: the computer's play time (its level, as the arcade's by its wins) put up by a win's as each
#               next opponent comes on (it was not: the same opponent each time)
#   ball        Parry the ball straight from the menu: the most stun set, nobody stunned at the first hit
#
#   games/sfiii3/checkmodes.sh ROM [BUILD]     (BUILD: build/sfiii3)
ROM=${1:?usage: checkmodes.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
CHECK="python3 $HERE/modecheck.py"

run() { # NAME FRAMES ARGS...: a run, its stderr in OUT/NAME.log
    local name=$1 frames=$2
    shift 2
    mkdir -p "$OUT/$name"
    nice -n 20 "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$frames" --settings "$OUT/$name/settings.ini" \
        --eeprom "$OUT/$name/eeprom.bin" "$@" > /dev/null 2> "$OUT/$name.log"
    echo $? > "$OUT/$name.status"
}
D="$OUT"
run model 2200 --keys 30:cmodel_start --shot-every 25:"$D/model/m_" --trace 20154A7-20154A7:"$D/model/state.bin" &
run colours 1400 --features model_character=2 --keys 30:ccolours_start --shot-every 25:"$D/colours/m_" \
    --trace 20154A7-20154A7:"$D/colours/state.bin" &
SFIII3_STAGE_CAMERA=1 SFIII3_STAGE_CAMERA_ALL=1 run viewer 2500 --features viewer_stage=12,viewer_scroll=2 --keys 30:cviewer_start \
    --audio-dump "$D/viewer/sound.wav" &
run viewer_quiet 2000 --features viewer_stage=12,viewer_music=1 --keys 30:cviewer_start --audio-dump "$D/viewer_quiet/sound.wav" &
SFIII3_BATTLE_LOG=1 run teams 3000 --keys 30:cteam_start,300:cteam_start &
SFIII3_BATTLE_SEED=7 SFIII3_BATTLE_TIMES=1 SFIII3_BATTLE_LOG=1 run tag 6000 --keys 30:cteam_start --auto 7:300:1 &
wait
SFIII3_BATTLE_SEED=7 SFIII3_BATTLE_LOG=1 run survival 5500 --keys 30:csurvival_start --auto 7:300:1 --poke 20691A3=08 \
    --trace 20694C4-20695BF:"$D/survival/trace.bin" &
SFIII3_BATTLE_SEED=7 SFIII3_BATTLE_LOG=1 run ringout 6500 --keys 30:cringout_start --auto 11:300:1 --trace 2011372-2011373:"$D/ringout/trace.bin" &
SFIII3_BONUS_LOG=1 run ball 2600 --keys 30:cbonus_ball --auto 7:300:1 --trace 20695F7-2069600:"$D/ball/trace.bin" &
wait

status=0
judge() { # NAME CHECK...
    local name=$1
    shift
    if [ "$(cat "$OUT/$name.status")" != 0 ]; then
        echo "$name: the run failed"; tail -3 "$OUT/$name.log"; status=1; return
    fi
    if ! result=$($CHECK "$@" 2>&1); then
        status=1
    fi
    echo "$name: $(echo "$result" | tr '\n' ' ')"
}
judge model veiled "$D/model/state.bin" "$D/model/m_" 50
judge colours veiled "$D/colours/state.bin" "$D/colours/m_" 50
judge viewer viewer "$D/viewer.log"
judge viewer loud "$D/viewer/sound.wav"
judge viewer_quiet loud "$D/viewer_quiet/sound.wav" quiet
judge teams teams "$D/teams.log"
judge tag tagtimes "$D/tag.log"
judge survival gauge "$D/survival/trace.bin" "$D/survival.log"
judge ringout playtime "$D/ringout/trace.bin" "$D/ringout.log"
judge ball stun "$D/ball/trace.bin" "$D/ball.log"
[ $status = 0 ] && echo "checkmodes: all the same"
rm -rf "$OUT"
exit $status
