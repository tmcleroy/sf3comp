#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); its shape is cps2recomp's tools/collection.sh.
#
# The collection: one launcher for every game in games/ (recomp-common's launcher with LAUNCHER_COLLECTION;
# launcher/src/description.cpp), in build/collection/ with each game's program beside it. Each game is built first,
# as `cmake -DGAME=<id>` builds it (in build/<id>: its translated code from work/<id>/gen, or none), then the
# launcher. Run from the repository's top; arguments are given to every CMake configure.
#   B=folder           where the launcher and the programs go (build/collection)
#   GB=folder          where each game is built, in a folder named by its id (build)
#   SKIP_GAMES=1       build no game: take the programs that are in GB as they are (one that is not there is left
#                      out; the launcher still lists it, and says its program is missing)
#   UPDATE_REPO=owner/name   the launcher updates itself from that repository's releases (none unless said)
#   VERSION=x.y.z      the launcher's version (the file VERSION's unless said)
set -eu
B=${B:-build/collection}
GB=${GB:-build}
ids=$(python3 cps3recomp/tools/gamecfg.py --games)
games=""
mkdir -p "$B"
for id in $ids; do
    games="$games${games:+;}$id"
    if [ -z "${SKIP_GAMES:-}" ]; then
        cmake -S . -B "$GB/$id" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAME="$id" "$@" | grep "sf3comp:" || true
        cmake --build "$GB/$id" --target "$id"
    fi
    for exe in "$GB/$id/$id" "$GB/$id/$id.exe"; do
        if [ -f "$exe" ]; then
            # (only when it changed: the app is made again when a program is newer than its copy there)
            cmp -s "$exe" "$B/$(basename "$exe")" || cp -p "$exe" "$B/"
            echo "$id: $exe"
        fi
    done
done
cmake -S cps3recomp/launcher -B "$B/launcher" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$(cd "$B" && pwd)" \
    -DCOLLECTION=ON -DGAMES="$games" -DGAME_NAME="SF3comp" -DGAME_EXE=sf3comp -DAPP_ID=io.github.tmcleroy.sf3comp \
    -DHOME_ENV=SF3COMP_HOME -DDISCLAIMER="Not affiliated with Capcom." -DGAME_VERSION="${VERSION:-}" \
    -DUPDATE_REPO="${UPDATE_REPO:-}" "$@" > /dev/null
cmake --build "$B/launcher" --target launcher
echo "built $B/sf3comp-launcher ($ids)"
