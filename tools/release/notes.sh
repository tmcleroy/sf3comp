#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of sf3comp; from cps2recomp.
#
# A release's text: the paragraph a version's section of CHANGELOG.md may begin with (before its list), what to
# download, how to install it on each system, and the rest of that section. Written by
# .github/workflows/release.yml for the draft (docs/RELEASING.md).
#   tools/release/notes.sh 0.1.0 > notes.md
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION=${1:?usage: notes.sh VERSION}
changes=$(awk -v v="## $VERSION" '$0 ~ "^## " { on = ($0 == v); next } on' "$ROOT/CHANGELOG.md")
if [ -z "$changes" ]; then
    echo "error: CHANGELOG.md has no section for $VERSION" >&2
    exit 1
fi
summary=$(printf '%s\n' "$changes" | awk '/^- / { exit } { line[n++] = $0 }
    END { s = 0; while (s < n && line[s] == "") s++; e = n - 1; while (e >= s && line[e] == "") e--
          for (i = s; i <= e; i++) print line[i] }')
list=$(printf '%s\n' "$changes" | awk '/^- / { on = 1 } on')
[ -z "$summary" ] || printf '%s\n\n' "$summary"
cat <<EOF
An unofficial native port of Capcom's three Street Fighter III arcade games (CPS-3): 3rd Strike, New Generation and
2nd Impact, in one launcher. **No game is included: you need your own ROM sets**
([which ones](https://github.com/tmcleroy/sf3comp#what-you-need)).

## Download

| Your system | Download |
| --- | --- |
| macOS 13 or later, Apple silicon or Intel | **SF3comp-macOS.zip** |
| Windows 10 or 11, 64-bit | **SF3comp-windows-x64.zip** |
| Linux x86_64, Steam Deck, Steam Machine | **SF3comp-linux-x86_64.tar.gz** |

## Install

### macOS
1. Open the zip: it gives **SF3comp.app**. Move it to your **Applications** folder. (The app of an earlier version,
   **Street Fighter III 3rd Strike.app**, can go to the Trash: its ROM set, settings and saves are kept.)
2. The app is not notarized by Apple, so the first time: right-click it and choose **Open**, then **Open**. If
   macOS only offers **Done** or **Move to Trash**, choose **Done**, then open **System Settings → Privacy &
   Security**, scroll down and press **Open Anyway**.
3. In the launcher, **Choose folder…** and show it the folder your ROM sets are in (\`sfiii3nr1.zip\`, \`sfiiin.zip\`,
   \`sfiii2n.zip\`: as many of the three as you have), or drop a set on the launcher. Press a game's **Play**.

### Windows
1. Right-click the zip, choose **Extract All…**, and extract it to a folder of your own (for example
   \`Documents\\Games\`).
2. In the \`SF3comp\` folder, open **sf3comp-launcher.exe**. The programs are not signed, so the first time Windows
   may say it protected your PC: press **More info**, then **Run anyway**.
3. Choose your ROM sets as above, and press a game's **Play**.

### Linux, Steam Deck and Steam Machine
1. Unpack it where it can stay: \`tar xzf SF3comp-linux-x86_64.tar.gz -C ~/Applications\` (on a Steam Deck, in
   Desktop Mode: right-click the file in Dolphin, **Extract → Extract archive here**).
2. Run \`SF3comp/sf3comp-launcher\`. It needs only an X11 or Wayland desktop and the usual sound libraries (SDL 3
   is built in).
3. Choose your ROM sets as above. **Add to Steam** on a game's page puts it in your Steam library (under
   **Non-Steam**) for Game Mode.

In the game, Esc or F1 (on a gamepad, a click of the left stick, or Start and Select together) opens the in-game
menu. Your settings, save states and each board's settings memory are in your own user folder, a folder for each game;
the ROM sets are only read.

Coming from a version before the three games were one download (0.1.0-beta.2 and earlier): 3rd Strike keeps its
folder, so its settings, saves and the ROM set you chose are as you left them. The launcher is now
\`sf3comp-launcher\` (the old \`sfiii3-launcher\` can be deleted); a Steam shortcut made by the old launcher starts
the old launcher, so make it again from the new one.

## What's new in $VERSION

$list
EOF
