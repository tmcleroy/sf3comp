# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; the shape is cps2recomp's tools/release/lib.sh.
#
# Shared by the release scripts (sourced, not run). A release is 3rd Strike's program and its launcher, as one
# download for each system. The program holds the game's code translated to C++, as a build from source does: the
# generated code in work/sfiii3/gen (and the stage games' in work/sfiiin/gen and work/sfiii2n/gen, for the
# definitive version's stages), made by cps3recomp/tools/translate.sh from the builder's own ROM sets. So releases are
# built on the machine of someone who has the games, never by CI.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
GAME=sfiii3
VERSION="${VERSION:-$(cat "$ROOT/VERSION")}"
OUT="$ROOT/build/release"
DEPS="${DEPS:-$ROOT/build/deps}"
NAME="Street Fighter III 3rd Strike" # the launcher's window, app and folder
EXE=$GAME                            # the launcher: sfiii3-launcher
ARCHIVE=SF3comp                      # the downloads: SF3comp-macOS.zip and the rest
APP_ID=io.github.tmcleroy.sf3comp
SDL_VERSION=3.4.16
SDL_SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
SDL_MINGW_SHA256=c7ef65bd72eabac6e5b535411dbd8d5824d0aab24fd62ff8812666b336f18a9c
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$@"; else shasum -a 256 "$@"; fi; }
mkdir -p "$OUT" "$DEPS"

sdl_source() {
    if [ ! -d "$DEPS/SDL3-$SDL_VERSION/src" ]; then
        curl -fsSL -o "$DEPS/SDL3-$SDL_VERSION.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-$SDL_VERSION.tar.gz"
        echo "$SDL_SHA256  $DEPS/SDL3-$SDL_VERSION.tar.gz" | sha256 -c - >/dev/null
        tar -xzf "$DEPS/SDL3-$SDL_VERSION.tar.gz" -C "$DEPS"
    fi
}
# SDL 3 as a static library in $DEPS/sdl-$1 (the rest: more arguments for CMake)
sdl_static() {
    name=$1; shift
    if [ ! -f "$DEPS/sdl-$name/lib/cmake/SDL3/SDL3Config.cmake" ]; then
        sdl_source
        cmake -S "$DEPS/SDL3-$SDL_VERSION" -B "$DEPS/sdl-$name-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=OFF \
            -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DCMAKE_INSTALL_PREFIX="$DEPS/sdl-$name" "$@" >/dev/null
        cmake --build "$DEPS/sdl-$name-build" --target install >/dev/null
    fi
}
# The game's program and its launcher, in $1 (the rest: more arguments for CMake)
build_programs() {
    build=$1; shift
    for game in sfiii3 sfiiin sfiii2n; do
        if ! ls "$ROOT/work/$game/gen"/blocks_*.cpp >/dev/null 2>&1; then
            echo "error: no translated code for $game in work/$game/gen: run cps3recomp/tools/translate.sh with its ROM set first (GAME=$game)" >&2
            exit 1
        fi
    done
    cmake -S "$ROOT" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DGAME=$GAME "$@" | grep "sf3comp:"
    cmake --build "$build" --target $GAME launcher
}
copy_programs() { # from $1 into $2 (the end of a program's name: $3)
    cp "$1/$GAME$3" "$1/$GAME-launcher$3" "$2/"
}
check_archive() {
    "$ROOT/tools/release/check_archive.sh" "$1"
}
# What every archive carries beside the programs: $1 the folder, $2 the build folder (the launcher's downloads)
add_notices() {
    mkdir -p "$1/licenses"
    cp "$ROOT/LICENSE" "$1/LICENSE.txt"
    cp "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/README.md" "$ROOT/CHANGELOG.md" "$1/"
    sdl_source
    cp "$DEPS/SDL3-$SDL_VERSION/LICENSE.txt" "$1/licenses/SDL3.txt"
    # (the launcher's downloads: in this build's _deps, or where the build was told they are)
    imgui="$(grep -E '^FETCHCONTENT_SOURCE_DIR_IMGUI' "$2/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    inter="$(grep -E '^FETCHCONTENT_SOURCE_DIR_INTER' "$2/CMakeCache.txt" 2>/dev/null | cut -d= -f2)"
    cp "${imgui:-$2/_deps/imgui-src}/LICENSE.txt" "$1/licenses/DearImGui.txt"
    cp "$(find "${inter:-$2/_deps/inter-src}" -iname 'LICENSE*' | head -1)" "$1/licenses/Inter-OFL.txt"
    { echo "stb_vorbis (cps3recomp/third_party/stb_vorbis.c):"; sed -n '/^ALTERNATIVE A - MIT License/,$p' "$ROOT/cps3recomp/third_party/stb_vorbis.c"
      for f in dr_flac dr_mp3 dr_wav; do echo; echo "$f (cps3recomp/third_party/$f.h):"; sed -n '/^ALTERNATIVE 1 - Public Domain/,$p' "$ROOT/cps3recomp/third_party/$f.h"; done
    } > "$1/licenses/sound-decoders.txt"
    printf 'Source for this version (GPL-3.0): https://github.com/tmcleroy/sf3comp/tree/v%s\n' "$VERSION" > "$1/SOURCE.txt"
}
# The launcher's self-update manifest (recomp-common/launcher/src/update.h): $OUT/update-$1.txt for the archive $3
# and every file in $2
update_files() {
    platform=$1 top=$2 archive=$3
    manifest="$OUT/update-$platform.txt"
    printf 'recomp-update 2\nversion %s\n' "$VERSION" > "$manifest"
    printf 'archive %s %s %s\n' "$(sha256 "$archive" | cut -d' ' -f1)" "$(wc -c < "$archive" | tr -d ' ')" "$(basename "$archive")" >> "$manifest"
    printf 'top %s\n' "$(basename "$top")" >> "$manifest"
    (cd "$top" && find . -type f | sed 's|^\./||' | LC_ALL=C sort) | while IFS= read -r path; do
        mode=-
        [ -x "$top/$path" ] && mode=x
        printf 'file %s %s %s %s\n' "$(sha256 "$top/$path" | cut -d' ' -f1)" "$(wc -c < "$top/$path" | tr -d ' ')" "$mode" "$path" >> "$manifest"
    done
    echo "wrote $manifest ($(grep -c '^file ' "$manifest") files)"
}
