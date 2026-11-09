#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp.
#
# Release build for Windows x64, cross-compiled with MinGW-w64 on Linux:
# build/release/SF3comp-windows-x64.zip (the launcher, the three games, SDL3.dll, zlib1.dll, librashader.dll and the shaders, licences).
# Needs the packages of tools/release/Dockerfile, and the translated code of a build of each game with its ROM
# sets (lib.sh), and Rust with its x86_64-pc-windows-gnu target for librashader (tools/librashader.sh: the Display
# page's shaders).
set -eu
. "$(dirname "$0")/lib.sh"
BUILD="$ROOT/build/release-windows"
SDL="$DEPS/SDL3-mingw-$SDL_VERSION"
if [ ! -d "$SDL/x86_64-w64-mingw32" ]; then
    curl -fsSL -o "$DEPS/sdl3-mingw.tar.gz" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL3-devel-$SDL_VERSION-mingw.tar.gz"
    echo "$SDL_MINGW_SHA256  $DEPS/sdl3-mingw.tar.gz" | sha256 -c - >/dev/null
    mkdir -p "$SDL" && tar -xzf "$DEPS/sdl3-mingw.tar.gz" -C "$SDL" --strip-components=1
fi
sdl_source # (for its licence text)
build_programs "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/release/mingw-w64.cmake" \
    -DCMAKE_PREFIX_PATH="$SDL/x86_64-w64-mingw32" -DCMAKE_EXE_LINKER_FLAGS="-static"

STAGE="$BUILD/stage/$ARCHIVE"
rm -rf "$BUILD/stage" && mkdir -p "$STAGE"
copy_programs "$BUILD" "$STAGE" .exe
cp "$SDL/x86_64-w64-mingw32/bin/SDL3.dll" "$STAGE/"
cp "$(find /usr/x86_64-w64-mingw32 /usr/lib/gcc/x86_64-w64-mingw32 -name zlib1.dll | head -1)" "$STAGE/"
x86_64-w64-mingw32-strip "$STAGE"/*.exe
# (linked statically but for SDL3.dll, zlib1.dll and Windows' own; anything else here is a mistake)
if x86_64-w64-mingw32-objdump -p "$STAGE"/*.exe | grep -i 'DLL Name' | grep -viE 'SDL3|zlib1|KERNEL32|msvcrt|USER32|SHELL32|ADVAPI32|ole32|api-ms-|GDI32|IMM32|WINMM|VERSION|SETUPAPI|OLEAUT32|COMDLG32|WS2_32|IPHLPAPI'; then
    echo "error: a program needs a DLL that is not in the archive" >&2
    exit 1
fi
add_notices "$STAGE" "$BUILD/launcher"
# (the Display page's shaders, port/shading.h: librashader beside the games and the presets, tools/librashader.sh)
LIBRA=$("$ROOT/cps3recomp/tools/librashader.sh" --windows)
if x86_64-w64-mingw32-objdump -p "$LIBRA/librashader.dll" | grep -i 'DLL Name' | grep -viE 'KERNEL32|ntdll|msvcrt|USER32|ADVAPI32|api-ms-|bcrypt|WS2_32|userenv|OPENGL32|GDI32|ole32|OLEAUT32|SHELL32'; then
    echo "error: librashader.dll needs a DLL that is not in the archive" >&2
    exit 1
fi
cp "$LIBRA/librashader.dll" "$STAGE/"
cp -R "$LIBRA/shaders" "$STAGE/"
cp "$LIBRA/librashader-LICENSE.md" "$STAGE/licenses/librashader-MPL-2.0.txt"
cp "$LIBRA/librashader-LICENSE-GPL.md" "$STAGE/licenses/librashader-GPL-3.0.txt"
# (zlib1.dll is in the archive and the MinGW-w64 and GCC runtimes are linked into the programs)
cp "$(ls /usr/share/doc/libz-mingw-w64*/copyright | head -1)" "$STAGE/licenses/zlib.txt"
cp /usr/share/doc/mingw-w64-common/copyright "$STAGE/licenses/MinGW-w64-runtime.txt"
cp /usr/share/doc/gcc-mingw-w64-base/copyright "$STAGE/licenses/GCC-runtime.txt"
rm -f "$OUT/$ARCHIVE-windows-x64.zip"
(cd "$BUILD/stage" && zip -qr "$OUT/$ARCHIVE-windows-x64.zip" "$ARCHIVE")
check_archive "$OUT/$ARCHIVE-windows-x64.zip"
update_files windows-x64 "$STAGE" "$OUT/$ARCHIVE-windows-x64.zip"
echo "wrote $OUT/$ARCHIVE-windows-x64.zip"
