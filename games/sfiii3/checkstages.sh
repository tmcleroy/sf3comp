#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy
#
# The stage rewrites' check (src/stage_*.cpp) over versus games of made-up play, two players: checklifts.sh's runs (the
# computer playing) have no versus game, where the stage is chosen by the characters (stage_select.cpp); and over
# training sessions started again on one stage after another, each stage's own routines met (stage_parts.cpp), the few
# stages the other runs reach not enough; and the two bonus games (Alternative modes), for the bonus stages. In layers, as checklifts.sh does it, with only these rewrites registered.
#
#   games/sfiii3/checkstages.sh ROM [BUILD] [FRAMES]     (BUILD: build/sfiii3; FRAMES: 12000 a run)
HERE=$(cd "$(dirname "$0")" && pwd)
names=$(grep -h '^ *{"[A-Za-z0-9_]*", 0x' "$HERE"/src/stage_*.cpp | sed 's/^ *{"\([A-Za-z0-9_]*\)".*/\1/' | sort -u | paste -sd, -)
SFIII3_LIFTS_ONLY="$names" SFIII3_CHECK_VERSUS=1 exec "$HERE/checklifts.sh" "$@"
