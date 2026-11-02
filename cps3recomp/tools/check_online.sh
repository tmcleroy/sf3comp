#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from cps2recomp.
#
# Online play (issue #70, port/netplay.h) between two copies of the game on this machine: one hosts, the other
# joins it over localhost, each with made-up play for its own player, every packet sent late (50 ms, give or take 25)
# and one in twenty lost (--net-fake), so that the guesses go wrong and frames are rolled back. The host has every
# feature that changes play on and the joining side none, so the joining side must take the host's (and both turn
# off the cheats). Both write a
# frame's state hash once it has both players' real controls (--hash-log); the two logs must be the same, and
# neither side may stop on a desync. And the host's record of both players' controls (--record), replayed without the
# network, must give the same hashes: each player's controls drove their own player. At the board's pace: FRAMES
# (3000 unless said) take FRAMES / 60 seconds. Each side gives a name and says a line in the chat (--net-name,
# --net-say): both sides must have both names, and each the other's line.
#
# JOIN_ROM: the joining side's set, another region's (game.cfg's other_set): it plays the host's program, whose online_words
# the host sends, so the two logs and the replay (of the host's set) must still be the same.
#
# ARCADE=1: the host plays alone while it waits (--net-arcade) and the other side joins a few seconds later, so that
# the match begins from the game as the host's play left it, sent whole; the replay from the board's reset is not tried.
#
#   [ARCADE=1] [JOIN_ROM=<set>] GAME=<id> check_online.sh [FRAMES]   (make GAME=<id> check-online: from the repository's top, after `make`; ROM=your set)
set -eu
GAME=${GAME:-sfiii3}
ROM=${ROM:?the ROM set: ROM=path}
JOIN_ROM=${JOIN_ROM:-$ROM}
frames=${1:-3000}
arcade=${ARCADE:-}
work=work/$GAME/check
mkdir -p "$work"
: > "$work/online-host.ini"
: > "$work/online-join.ini"
rm -f "$work/online-host.txt" "$work/online-join.txt" "$work/online-host.eeprom" "$work/online-join.eeprom"
# (the features that change play, as tools/check_rollback.sh has them, but for the custom colours: each player's own)
play=$("build/$GAME/$GAME" --list-features 2>&1 | awk -F'\t' '($2=="Game"||$2=="Cheats"||$2=="Display") && NF==5 &&
    $1 !~ /^(display_|window_size|run_ahead|custom_soundtrack|widescreen_walls|custom_colours)/ {print $1}' | paste -sd, -)
# (the match has them but for the cheats, which are off on both sides: the replay is given the same)
cheats=$("build/$GAME/$GAME" --list-features 2>&1 | awk -F'\t' '$2=="Cheats" {print $1}')
match=$(printf '%s\n' "$play" | tr ',' '\n' | grep -vxF "$cheats" | paste -sd, -)
port=$((7900 + RANDOM % 1000))
side() { # host | join, then the program's arguments
    name=$1; shift
    rom=$ROM; [ "$name" = join ] && rom=$JOIN_ROM
    "build/$GAME/$GAME" --rom "$rom" --headless --frames "$frames" --settings "$work/online-$name.ini" \
        --eeprom "$work/online-$name.eeprom" --hash-log "$work/online-$name.txt" --net-fake 50:25:5 --net-service off "$@" 2> "$work/online-$name.err"
}
side host --auto 11:0:1 --features "$play" --host "$port" --record "$work/online-host.mov" ${arcade:+--net-arcade} \
    --net-name "Host P1" --net-say "hello from the host" &
host=$!
if [ -n "$arcade" ]; then sleep 4; else sleep 0.5; fi
status=0
side join --auto 12:0:1 --join "127.0.0.1:$port" --net-name "Joiner" --net-say "good luck!" || { echo "FAILED: the joining side stopped"; tail -3 "$work/online-join.err"; status=1; }
wait "$host" || { echo "FAILED: the host stopped"; tail -3 "$work/online-host.err"; status=1; }
# (each side's log ends where it last had the other's controls: the shorter is what both have)
lines=$(wc -l < "$work/online-host.txt" | tr -d ' ')
joined=$(wc -l < "$work/online-join.txt" | tr -d ' ')
[ "$joined" -lt "$lines" ] && lines=$joined
if [ "$lines" -lt $((frames - 30)) ]; then
    echo "FAILED: only $lines frames had both players' controls"
    status=1
elif cmp -s <(head -n "$lines" "$work/online-host.txt") <(head -n "$lines" "$work/online-join.txt"); then
    echo "ok: the two sides the same over $lines frames, with $play"
else
    echo "FAILED: the two sides differ from frame $(diff <(head -n "$lines" "$work/online-host.txt") <(head -n "$lines" "$work/online-join.txt") | grep '^<' | head -1 | cut -d' ' -f2)"
    status=1
fi
# (each side knows both names, and has the other's line of the chat: sent straight to it, through the lost packets)
for name in host join; do
    if ! grep -q '^online: player 1 Host P1, player 2 Joiner$' "$work/online-$name.err"; then
        echo "FAILED: the $name doesn't have both players' names"
        status=1
    fi
done
if grep -q '^online: chat: Joiner: good luck!$' "$work/online-host.err" && grep -q '^online: chat: Host P1: hello from the host$' "$work/online-join.err"; then
    echo "ok: the names and the chat came across"
else
    echo "FAILED: the chat didn't come across"
    status=1
fi
if [ -n "$arcade" ]; then
    # (the host played before the other side came: the match began from there)
    played=$(sed -n 's/^online: the match begins after \([0-9]*\) frames of the host.s play.*/\1/p' "$work/online-host.err")
    if [ "${played:-0}" -lt 120 ]; then
        echo "FAILED: the host had not played while it waited (${played:-no} frames)"
        status=1
    else
        echo "ok: the match began after $played frames of the host's own play"
    fi
    for name in host join; do
        echo "$name: $(grep '^online: frame' "$work/online-$name.err" | tail -1 | sed 's/^online: //')"
    done
    exit $status
fi
# And the match is what the game does with those controls: both players' (--record), replayed without the network
: > "$work/online-replay.ini"
"build/$GAME/$GAME" --rom "$ROM" --headless --frames "$lines" --settings "$work/online-replay.ini" --features "$match" \
    --movie "$work/online-host.mov" --hash-log "$work/online-replay.txt" --hash-no-picture --hash-no-sound 2> /dev/null
if cmp -s <(head -n "$lines" "$work/online-host.txt") <(head -n "$lines" "$work/online-replay.txt"); then
    echo "ok: the match the same as both players' controls replayed without the network"
else
    echo "FAILED: the match and its replay differ from frame $(diff <(head -n "$lines" "$work/online-host.txt") <(head -n "$lines" "$work/online-replay.txt") | grep '^<' | head -1 | cut -d' ' -f2)"
    status=1
fi
for name in host join; do
    echo "$name: $(grep '^online: frame' "$work/online-$name.err" | tail -1 | sed 's/^online: //')"
done
exit $status
