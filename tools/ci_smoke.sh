#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; its shape is cps2recomp's tools/ci_smoke.sh.
# Builds and starts what needs no game, with no window: the program with no translated code (it would run a ROM set
# in the interpreter; here it only lists its features) and the launcher, which draws its pages to pictures. Used by
# CI and before a pull request. BUILD= the build folder (build/ci unless said); with SKIP_BUILD=1 nothing is built and
# the programs in BIN= (an unpacked release's folder, say; BUILD unless said) are only started.
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="${BUILD:-$ROOT/build/ci}"
if [ "${SKIP_BUILD:-}" != 1 ]; then
    if ls "$ROOT"/work/*/gen/blocks_*.cpp >/dev/null 2>&1; then
        echo "note: work/ has translated code, which this build takes in (CI's tree has none)"
    fi
    cmake -S "$ROOT" -B "$B" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAME=sfiii3 ${CMAKE_ARGS:-} | grep "sf3comp:"
    cmake --build "$B" --target sfiii3 launcher
fi
BIN="${BIN:-$B}"
EXE=""
[ -f "$BIN/sfiii3.exe" ] && EXE=.exe
features="$("$BIN/sfiii3$EXE" --list-features | tr -d '\r')"
for f in free_play widescreen_walls stages training_stage dummy_parry; do
    echo "$features" | grep -q "^$f	" || { echo "the program's feature list has no $f"; exit 1; }
done
echo "ok: the program starts and lists its features ($(echo "$features" | wc -l | tr -d ' '))"
OUT="$B/smoke"
rm -rf "$OUT" && mkdir -p "$OUT/home"
env SDL_VIDEO_DRIVER=dummy SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy "SFIII3_HOME=$OUT/home" \
    "$BIN/sfiii3-launcher$EXE" --screenshot "$OUT/page"
pages="$(ls "$OUT"/page*.png | wc -l | tr -d ' ')"
[ "$pages" -ge 3 ] || { echo "the launcher drew $pages pages, not 3"; exit 1; }
echo "ok: the launcher starts and draws its pages ($pages)"
