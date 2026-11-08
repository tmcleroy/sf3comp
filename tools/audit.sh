#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; its shape is cps2recomp's tools/audit.sh.
# Checks the repository's tracked files for things that must never be committed (CONTRIBUTING.md): game files,
# cipher keys, anything made from a game, an emulator's sources, personal paths and addresses, large files; that
# every file marked as another project's is listed in the notices; and that every source file has its licence line.
# Run it before pushing; CI runs it on every push and pull request. Exits non-zero on a finding.
set -eu
cd "$(git rev-parse --show-toplevel)"
LIST="$(mktemp)"
trap 'rm -f "$LIST"' EXIT
git ls-files > "$LIST"

fail=0
check() { if [ -n "$2" ]; then echo "AUDIT FAIL: $1"; echo "$2" | head -10; fail=1; else echo "ok: $1"; fi; }
# (this script names the things it looks for, so it is not searched itself; nor are the third-party decoders and
# the GPL licence text, other projects' files as they publish them: the notices list them, checked below)
grep_files() { grep -v -e '^tools/audit\.sh$' -e '^cps3recomp/third_party/' -e '^LICENSE$' "$LIST" | tr '\n' '\0' | xargs -0 grep -nIE "$@" 2>/dev/null || true; }

# A CPS-3 set's files: the cartridge's BIOS (<set>_<region>.29f400.u2) and the SIMMs' flash (<set>-simm<n>.<m>)
check "no game, key, save or archive file types" "$(grep -iE '\.(zip|7z|rar|key|bin|rom|chd|state|sav|eep|nv|u2|29f400|ic[0-9]+)$|simm[0-9]\.[0-9]$|(^|/)(cps3boot|cps3-roms?)/' "$LIST" || true)"
check "nothing made from a game (generated code, block databases, listings, memory dumps)" "$(grep -E '(^|/)(work|build)/|(^|/)gen/|(^|/)blocks(_[0-9a-z]+)?\.(cpp|txt)$|(^|/)(game_cfg|game_info)\.h$|\.(dump|ram|lst)$' "$LIST" || true)"
check "every game has its game.cfg" "$(grep -E '^games/[^/]+/' "$LIST" | cut -d/ -f2 | sort -u | while read -r g; do [ -f "games/$g/game.cfg" ] || echo "games/$g"; done)"
check "no pictures but the README's, no sound or video" "$(grep -iE '\.(png|jpe?g|gif|bmp|ppm|webp|mp4|mov|webm|wav|ogg|mp3|flac)$' "$LIST" | grep -vE '^docs/screenshots/[a-z0-9-]+\.(png|jpg)$' || true)"
# (the published checksums of the libraries the builds download, and the commits they are pinned to, are not keys;
# a ROM file's CRC in game.cfg is 8 digits, a fact about the file)
check "no key-like hex strings (32 digits or more)" "$(grep_files '(^|[^0-9a-fA-F.])[0-9a-fA-F]{32,}([^0-9a-fA-F]|$)' | grep -vE 'URL_HASH SHA256=[0-9a-f]{64}|SHA256=[0-9a-f]{64}|_COMMIT=[0-9a-f]{40}$|^cps3recomp/recomp-common/COMMIT:|^\.github/workflows/' || true)"
# (a CPS-3 set's cipher key is two 32-bit numbers; the port works them out from the player's set at load
# (machine/crypt.cpp) and none may be written into the tree: not as numbers given to a key, not as a key table)
check "no cipher key written out" "$(grep_files -i -e '\b(key[12]?|cipher[a-z_]*key[a-z_]*)[ ]*[=:{(,][ ]*\{?[ ]*0x[0-9a-f]{6,8}' -e 'init_crypt[ ]*\(' -e '\.key[12][ ]*=[ ]*0x' || true)"
check "no home folder paths or scratch folders" "$(grep_files -e '/Users/[a-z]' -e '/home/[a-z]+/' -e 'C:\\\\Users\\\\[a-z]' -e '~/code/' -e '/private/tmp/' -e '/tmp/claude' || true)"
check "no private session links, machine names or private network addresses" "$(grep_files -i -e 'claude\.ai/code/session' -e '\b192\.168\.[0-9]+\.[0-9]+' -e '\b10\.[0-9]+\.[0-9]+\.[0-9]+\b' -e 'steammachine\.lan' -e 'steamdeck\.local' -e 'deck@' || true)"
# (the README's pictures may be larger; the sound file decoders are kept as their projects publish them)
check "no file over 512 KB (the README's pictures: 2 MB; the third-party decoders: 1 MB)" "$( (grep -v -e '^docs/screenshots/' -e '^cps3recomp/third_party/' "$LIST" | tr '\n' '\0' | xargs -0 find 2>/dev/null -prune -type f -size +512k; \
    grep '^cps3recomp/third_party/' "$LIST" | tr '\n' '\0' | xargs -0 find 2>/dev/null -prune -type f -size +1024k; \
    grep '^docs/screenshots/' "$LIST" | tr '\n' '\0' | xargs -0 find 2>/dev/null -prune -type f -size +2048k) || true)"
# (the names of ports that are not public yet, and of the maintainer's machines, are not written here, since this file
# is public: one pattern per line in .git/private-names, or in the file AUDIT_PRIVATE_NAMES names, on the
# maintainer's machine; without either, only the generic forms are looked for)
names="${AUDIT_PRIVATE_NAMES:-$(git rev-parse --git-common-dir)/private-names}"
check "no names of ports that are still private, or of private machines" "$(grep_files -i -e '[a-z0-9]-archive\b' -e '[a-z0-9]-private\b' || true
    if [ -f "$names" ]; then grep -v -e '^#' -e '^$' "$names" | while read -r n; do grep_files -i -e "$n"; done; fi)"
check "no words that point to where game files or keys are found" "$(grep_files -i -e 'archive\.org' -e 'internet archive' -e 'rom ?sites?\b' -e 'emuparadise' -e 'vimm' -e 'romsfun' -e 'torrent' -e 'download (the|a|an) (rom|rom set|key|key file)' || true)"
check "no submodules" "$(git ls-files -s | awk '$1 == "160000" {print $4}'; [ ! -f .gitmodules ] || echo .gitmodules)"
check "no patch files" "$(grep -iE '\.(patch|diff)$|(^|/)patches/' "$LIST" || true)"
check "no emulator's sources in the tree" "$(grep -iE '(^|/)(vendor|external|deps)/|(^|/)third_party/.+/.+|(^|/)(mame|fbneo|fbalpha|libretro|3sx|3s-decomp)[^/]*/|(^|/)cps3(run|snd|_a)?\.(cpp|c|h)$|(^|/)d_cps3\.cpp$' "$LIST" || true)"
# (the PS2 decompilation crowded-street/3s-decomp is AGPL-3.0: its names are used, never its code; a file of it
# would carry its own path names)
check "nothing from the PS2 decompilation's tree" "$(grep -iE '(^|/)sf33rd/|(^|/)Source/Game/' "$LIST" || true)"
# Files of another project's say so in their first lines; each must be one THIRD_PARTY_NOTICES.md lists
check "files of another project's are only the ones the notices list" "$(grep_files -l -e '^(//|#) *license: *BSD' -e 'SPDX-License-Identifier: (GPL-3\.0-or-later|AGPL|BSD|MIT|Zlib)' \
    | grep -vE '^cps3recomp/recomp-common/launcher/src/(ui\.cpp|ui\.h|embedded\.h)$|^(THIRD_PARTY_NOTICES|CONTRIBUTING)\.md$' || true)"
check "every source file has a license line" "$(grep -E '\.(h|c|cpp|inc|frag|vert|comp|mm|py|sh|mk|cmake|cfg|sv|yml|plist)$|CMakeLists\.txt$|(^|/)Makefile$' "$LIST" | grep -v '^cps3recomp/third_party/' | tr '\n' '\0' | xargs -0 grep -LE 'SPDX-License-Identifier:' 2>/dev/null || true)"
check "every source file has a copyright line" "$(grep -E '\.(h|c|cpp|inc|frag|vert|comp|mm|py|sh|mk|cmake|cfg|sv|yml|plist)$|CMakeLists\.txt$|(^|/)Makefile$' "$LIST" | grep -v -e '^cps3recomp/third_party/' -e '^cps3recomp/recomp-common/launcher/src/\(ui\.cpp\|ui\.h\|embedded\.h\)$' | tr '\n' '\0' | xargs -0 grep -LE 'Copyright \(C\)|Copyright \(c\)|Copyright [0-9]' 2>/dev/null || true)"
check "third_party/ holds only what the notices list" "$(grep '^cps3recomp/third_party/' "$LIST" | grep -v '/README\.md$' | while read -r f; do grep -qF "\`$f\`" THIRD_PARTY_NOTICES.md || echo "$f"; done)"
check "the README and the notices are there" "$(for f in README.md LICENSE THIRD_PARTY_NOTICES.md CONTRIBUTING.md CODE_OF_CONDUCT.md SECURITY.md CHANGELOG.md VERSION; do [ -f "$f" ] || echo "missing: $f"; done)"
[ "$fail" = 0 ] || { echo "audit failed"; exit 1; }
echo "audit passed ($(wc -l < "$LIST" | tr -d ' ') files)"
