#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The stages' check, two parts:
# 1. The stage rewrites (src/stage_*.cpp) over versus games of made-up play, two players: checklifts.sh's runs (the
#    computer playing) have no versus game, where the stage is chosen by the characters (stage_select.cpp); and over
#    training sessions started again on one stage after another, each stage's own routines met (stage_parts.cpp), the
#    few stages the other runs reach not enough; and the two bonus games (Alternative modes), for the bonus stages. In
#    layers, as checklifts.sh does it, with only these rewrites registered.
# 2. The guest stages (New Generation's and 2nd Impact's, src/stages.cpp): checkguests.sh, every stage of both games
#    against its own game's match in play, and in play with stages chosen mid-fight, widescreen off and on; the jitter,
#    the layers and the training session's Stage row.
# 3. 3rd Strike's own stages as the picture shows them: checkpictures.sh, the jitter (training sessions and the endless
#    battle's stage behind) and the layers (a stage piece in front of a fighter), every stage, widescreen off and on.
#
#   games/sfiii3/checkstages.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
#
# CHECKSTAGES_ONLY=lifts, guests, pictures, viewer, popin or wide: that part alone.
HERE=$(cd "$(dirname "$0")" && pwd)
status=0
if [ -z "$CHECKSTAGES_ONLY" ] || [ "$CHECKSTAGES_ONLY" = lifts ]; then
    names=$(grep -h '^ *{"[A-Za-z0-9_]*", 0x' "$HERE"/src/stage_*.cpp | sed 's/^ *{"\([A-Za-z0-9_]*\)".*/\1/' | sort -u | paste -sd, -)
    SFIII3_LIFTS_ONLY="$names" SFIII3_CHECK_VERSUS=1 "$HERE/checklifts.sh" "$@" || status=1
fi
if [ -z "$CHECKSTAGES_ONLY" ] || [ "$CHECKSTAGES_ONLY" = guests ]; then
    "$HERE/checkguests.sh" "$@" || status=1
fi
if [ -z "$CHECKSTAGES_ONLY" ] || [ "$CHECKSTAGES_ONLY" = pictures ]; then
    "$HERE/checkpictures.sh" "$1" "${2:-build/sfiii3}" || status=1
fi
# 4. The third play-test's (2026-11-07): the stage viewer's entries against each stage's own fight (checkviewer.sh), pop-in
#    at the widescreen picture's edges on every stage (checkpopin.sh), widescreen switched in the menu against started so,
#    in the real frontend (checkwide.sh; its GPU runs one at a time under the GPU lock, CHECKWIDE_GPU=0: none)
for part in viewer popin wide; do
    if [ -z "$CHECKSTAGES_ONLY" ] || [ "$CHECKSTAGES_ONLY" = $part ]; then
        "$HERE/check$part.sh" "$1" "${2:-build/sfiii3}" || status=1
    fi
done
[ $status = 0 ] && echo "checkstages: all the same" || echo "checkstages: DIFFERENT"
exit $status
