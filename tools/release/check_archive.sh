#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp.
#
# Fails if a release archive holds anything it must not (game files, keys, a builder's home path) or lacks what it
# must carry (the programs, the licences).
#   check_archive.sh ARCHIVE...
set -eu
status=0
bad() { echo "ARCHIVE FAIL ($archive): $1"; status=1; }
for archive in "$@"; do
    dir="$(mktemp -d)"
    case "$archive" in
        *.zip) unzip -q "$archive" -d "$dir" ;;
        *) tar -xzf "$archive" -C "$dir" ;;
    esac
    names="$(cd "$dir" && find . -type f | sort)"
    found="$(echo "$names" | grep -iE '\.(zip|7z|key|bin|rom|state|sav|eep|lst|u2)$|simm[0-9]' || true)"
    [ -z "$found" ] || bad "game or data files: $found"
    for need in LICENSE.txt THIRD_PARTY_NOTICES.md licenses/SDL3.txt licenses/DearImGui.txt licenses/Inter-OFL.txt licenses/sound-decoders.txt SOURCE.txt; do
        echo "$names" | grep -q "/$need\$" || bad "missing $need"
    done
    find "$dir" -type f \( -name sfiii3 -o -name sfiii3.exe \) | grep -q . || bad "no game program"
    find "$dir" -type f \( -name sfiii3-launcher -o -name sfiii3-launcher.exe \) | grep -q . || bad "no launcher"
    paths="$(cd "$dir" && grep -rlaE '/Users/[a-z]|/home/[a-z]+/|C:\\Users\\' . || true)"
    [ -z "$paths" ] || bad "a builder's home path inside: $paths"
    rm -rf "$dir"
    [ $status -ne 0 ] || echo "ok: $archive"
done
exit $status
