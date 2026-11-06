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
#   ringout_gauge  ring out: the same, the gauge kept from one opponent rung out to the next (it was emptied)
#   ringout     ring out: the computer's play time (its level, as the arcade's by its wins) put up by a win's as each
#               next opponent comes on (it was not: the same opponent each time)
#   ball        Parry the ball straight from the menu: the most stun set, nobody stunned at the first hit
#
# ... and of the next play-test (each run with a time limit: a run that stops going on is "hung"):
#
#   view_q, view_alex, view_again, edit_ryu, edit_again, view_after_tag
#               the model viewer and the colour editor with an animation chosen and "Play all" (the menu keeps them), on
#               several characters, started again, another character chosen in them, and after a tag team battle: the
#               run goes on to its end (an animation put in the fighter's record in the entrances before the round,
#               or begun without its frames' size, sent the game's animation code into what is not code, and the
#               program hung inside a frame, its picture black), and while the round is on the picture is never black
#               for more than 60 frames (a tag as the battle's round was decided left the picture held for good: the
#               viewer started after it stayed black)
#   tag_3, tag_5, tag_11, tag_13
#               tag team battles of made-up play against the computer, three a side (tag_13: two), two battles a run:
#               the run goes on to its end, the computer's side tags, and the picture never stands still for more than
#               300 frames (seed 5: the computer's partner brought in as the clock ran out, the round decided within
#               the tag's held frames, and the picture held for good)
#
#   games/sfiii3/checkmodes.sh ROM [BUILD]     (BUILD: build/sfiii3)
ROM=${1:?usage: checkmodes.sh ROM [BUILD]}
BUILD=${2:-build/sfiii3}
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$(mktemp -d)
CHECK="python3 $HERE/modecheck.py"

# (each run with a time limit, LIMIT seconds: one that hangs is stopped, its status 124)
LIMITED=$(command -v gtimeout || command -v timeout)
run() { # NAME FRAMES ARGS...: a run, its stderr in OUT/NAME.log
    local name=$1 frames=$2
    shift 2
    mkdir -p "$OUT/$name"
    nice -n 20 $LIMITED -k 10 "${LIMIT:-300}" "$BUILD/sfiii3" --rom "$ROM" --headless --frames "$frames" --settings "$OUT/$name/settings.ini" \
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
SFIII3_BATTLE_SEED=7 SFIII3_BATTLE_LOG=1 run ringout_gauge 6500 --keys 30:cringout_start --auto 11:300:1 --poke 20691A3=08 \
    --trace 20694C4-20695BF:"$D/ringout_gauge/trace.bin" &
SFIII3_BONUS_LOG=1 run ball 2600 --keys 30:cbonus_ball --auto 7:300:1 --trace 20695F7-2069600:"$D/ball/trace.bin" &
wait
# (the viewer and the editor: pictures every 10 frames, the match's state and the sides' locks traced)
look() { # NAME: the arguments for its pictures and trace
    echo --shot-every 10:"$D/$1/m_" --trace 20154A7-20154C8:"$D/$1/state.bin"
}
PLAY_ALL=skip_intro=1,free_play=1,model_animation=1,model_all=1
run view_q 5000 --features $PLAY_ALL,model_character=18,model_background=22 --keys 30:cmodel_start $(look view_q) &
run view_alex 7000 --features $PLAY_ALL,model_character=1,model_background=1 --keys 30:cmodel_start $(look view_alex) &
run view_again 9000 --features $PLAY_ALL,model_character=16,model_background=1 --keys 30:cmodel_start,1000:cmodel_start,1600:fmodel_character=11 \
    $(look view_again) &
run edit_ryu 6000 --features skip_intro=1,free_play=1,model_character=2,model_background=1,colours_animation=1531 \
    --keys 30:ccolours_start,1100:fmodel_character=4 $(look edit_ryu) &
run edit_again 6000 --features skip_intro=1,free_play=1,model_character=11,model_background=4,colours_animation=481 \
    --keys 30:ccolours_start,1000:ccolours_start $(look edit_again) &
SFIII3_BATTLE_SEED=5 run view_after_tag 10500 --features team_size=1,model_character=7 --keys 30:cteam_start,7400:cmodel_start \
    --auto 5:300:1 $(look view_after_tag) &
wait
# (tag team battles, two a run: pictures every 10 frames)
tags() { # NAME SEED SIZE
    SFIII3_BATTLE_SEED=$2 SFIII3_BATTLE_TIMES=1 SFIII3_BATTLE_LOG=1 run "$1" 14000 --features team_size=$3 --keys 30:cteam_start,9000:cteam_start \
        --auto "$2":300:1 $(look "$1")
}
tags tag_3 3 1 &
tags tag_5 5 1 &
tags tag_11 11 1 &
tags tag_13 13 0 &
wait

status=0
judge() { # NAME CHECK...
    local name=$1
    shift
    if [ "$(cat "$OUT/$name.status")" = 124 ]; then
        echo "$name: FAILED: hung (the program stopped going on: stopped after ${LIMIT:-300} s)"; tail -2 "$OUT/$name.log"; status=1; return
    fi
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
judge ringout_gauge gauge "$D/ringout_gauge/trace.bin" "$D/ringout_gauge.log"
judge ball stun "$D/ball/trace.bin" "$D/ball.log"
judge view_q shown "$D/view_q/state.bin" "$D/view_q/m_" 5000 "$D/view_q.log" 60
judge view_alex shown "$D/view_alex/state.bin" "$D/view_alex/m_" 7000 "$D/view_alex.log" 60
judge view_again shown "$D/view_again/state.bin" "$D/view_again/m_" 9000 "$D/view_again.log" 60
judge edit_ryu shown "$D/edit_ryu/state.bin" "$D/edit_ryu/m_" 6000 "$D/edit_ryu.log" 60
judge edit_again shown "$D/edit_again/state.bin" "$D/edit_again/m_" 6000 "$D/edit_again.log" 60
judge view_after_tag shown "$D/view_after_tag/state.bin" "$D/view_after_tag/m_" 10500 "$D/view_after_tag.log" 60
for t in tag_3 tag_5 tag_11 tag_13; do
    judge $t frozen "$D/$t/state.bin" "$D/$t/m_" 14000 "$D/$t.log" 300 1
done
[ $status = 0 ] && echo "checkmodes: all the same"
rm -rf "$OUT"
exit $status
