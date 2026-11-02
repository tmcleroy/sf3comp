#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp.
#
# Release build for macOS (Apple silicon and Intel in one program, macOS 13 and later):
# build/release/SF3comp-macOS.zip holding "Street Fighter III 3rd Strike.app" (the launcher, with the game beside
# it). Needs the translated code (lib.sh), and Rust for librashader
# (tools/librashader.sh: the Display page's shaders). SDL 3 is built in. The app is
# signed ad hoc and not notarized, so Gatekeeper warns the first time it is opened.
set -eu
. "$(dirname "$0")/lib.sh"
BUILD="$ROOT/build/release-macos"
# (MAC_ARCHS=arm64: Apple silicon alone, for CI's pull requests, which are not releases)
ARCH="-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_OSX_ARCHITECTURES=${MAC_ARCHS:-arm64;x86_64}"
sdl_static macos $ARCH
build_programs "$BUILD" $ARCH -DCMAKE_PREFIX_PATH="$DEPS/sdl-macos"

APP="$OUT/$NAME.app"
rm -rf "$APP" && mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
copy_programs "$BUILD" "$APP/Contents/MacOS" ""
strip -x "$APP/Contents/MacOS/"*
sed -e "s/@VERSION@/$VERSION/g" -e "s/@NAME@/$NAME/g" -e "s/@APP_ID@/$APP_ID/g" -e "s/@EXE@/$EXE-launcher/g" \
    "$ROOT/tools/release/Info.plist" > "$APP/Contents/Info.plist"
add_notices "$APP/Contents/Resources" "$BUILD"
# (the Display page's shaders, port/shading.h: librashader and the presets, tools/librashader.sh)
LIBRA=$("$ROOT/cps3recomp/tools/librashader.sh")
mkdir -p "$APP/Contents/Frameworks"
cp "$LIBRA/librashader.dylib" "$APP/Contents/Frameworks/"
cp -R "$LIBRA/shaders" "$APP/Contents/Resources/"
cp "$LIBRA/librashader-LICENSE.md" "$APP/Contents/Resources/licenses/librashader-MPL-2.0.txt"
cp "$LIBRA/librashader-LICENSE-GPL.md" "$APP/Contents/Resources/licenses/librashader-GPL-3.0.txt"
codesign --force --deep -s - "$APP"

for binary in "$APP/Contents/MacOS/"* "$APP/Contents/Frameworks/"*; do
    if otool -L "$binary" | grep -q '/opt/homebrew\|/usr/local'; then
        echo "error: $binary links a library outside macOS" >&2
        exit 1
    fi
done
rm -f "$OUT/$ARCHIVE-macOS.zip"
(cd "$OUT" && ditto -c -k --norsrc --noextattr --keepParent "$NAME.app" "$ARCHIVE-macOS.zip")
check_archive "$OUT/$ARCHIVE-macOS.zip"
update_files macos "$APP" "$OUT/$ARCHIVE-macOS.zip"
echo "wrote $OUT/$ARCHIVE-macOS.zip"
