#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; its shape is cps2recomp's tools/ci_smoke.sh.
# Builds and starts what needs no game, with no window: every game's program with no translated code (it would run a
# ROM set in the interpreter; here it only lists its features) and the collection's launcher, which draws its pages
# to pictures (cps3recomp/tools/collection.sh), and 3rd Strike's own launcher (the build of one game: cmake
# -DGAME=<id>). Used by CI and before a pull request. BUILD= the build folder (build/ci unless said); with
# SKIP_BUILD=1 nothing is built and the programs in BIN= (an unpacked release's folder, say; BUILD unless said) are
# only started.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="${BUILD:-$ROOT/build/ci}"
GAMES="$(cd "$ROOT" && python3 cps3recomp/tools/gamecfg.py --games)"
if [ "${SKIP_BUILD:-}" != 1 ]; then
    if ls "$ROOT"/work/*/gen/blocks_*.cpp >/dev/null 2>&1; then
        echo "note: work/ has translated code, which this build takes in (CI's tree has none)"
    fi
    # shellcheck disable=SC2086
    (cd "$ROOT" && B="$B" GB="$B/games" sh cps3recomp/tools/collection.sh ${CMAKE_ARGS:-})
    cmake --build "$B/games/sfiii3" --target launcher
fi
BIN="${BIN:-$B}"
EXE=""
[ -f "$BIN/sfiii3.exe" ] && EXE=.exe
for game in $GAMES; do
    features="$("$BIN/$game$EXE" --list-features | tr -d '\r')"
    need="run_ahead display_scaling online_delay"
    [ "$game" != sfiii3 ] || need="free_play widescreen_walls stage stages_preload dummy_parry"
    for f in $need; do
        echo "$features" | grep -q "^$f	" || { echo "$game: the program's feature list has no $f"; exit 1; }
    done
    echo "ok: $game starts and lists its features ($(echo "$features" | wc -l | tr -d ' '))"
done
OUT="$B/smoke"
rm -rf "$OUT" && mkdir -p "$OUT/home" "$OUT/home-sfiii3"
# (the collection's: its Games, Online, Features and Controllers pages; a test's folder, never the player's)
env SDL_VIDEO_DRIVER=dummy SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy "SF3COMP_HOME=$OUT/home" \
    "$BIN/sf3comp-launcher$EXE" --screenshot "$OUT/page"
pages="$(ls "$OUT"/page*.png | wc -l | tr -d ' ')"
[ "$pages" -ge 4 ] || { echo "the launcher drew $pages pages, not 4"; exit 1; }
for game in $GAMES; do
    [ -f "$OUT/home/$game/settings.ini" ] || { echo "the launcher has no game $game"; exit 1; }
done
echo "ok: the launcher starts, has every game and draws its pages ($pages)"
# (a game's own launcher, where it was built)
ONE="$B/games/sfiii3/sfiii3-launcher$EXE"
if [ -f "$ONE" ]; then
    env SDL_VIDEO_DRIVER=dummy SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy "SFIII3_HOME=$OUT/home-sfiii3" \
        "$ONE" --screenshot "$OUT/one"
    pages="$(ls "$OUT"/one*.png | wc -l | tr -d ' ')"
    [ "$pages" -ge 3 ] || { echo "3rd Strike's own launcher drew $pages pages, not 3"; exit 1; }
    echo "ok: 3rd Strike's own launcher starts and draws its pages ($pages)"
fi
