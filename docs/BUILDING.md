# Building sf3comp

You need your own ROM sets: `sfiii3nr1.zip` (3rd Strike, Japan 990512), `sfiiin.zip` (New Generation) and
`sfiii2n.zip` (2nd Impact). Each game needs only its own; 3rd Strike takes the other two's as well, for the
definitive version's stages. None are in this repository.

There are three games (`games/<id>/game.cfg`: `sfiii3`, `sfiiin`, `sfiii2n`), each a program of its own, and two
ways to build them: the collection (one launcher for the three, what a release is) and one game with a launcher of
its own (what the checks use).

## A build for yourself (macOS, Linux)

Needs CMake, Ninja, a C++20 compiler, Python 3, SDL 3 (3.4.16 or later) and zlib.

```sh
# the game's code translated: the blocks games/sfiii3/block_addresses.txt lists, their code read from your ROM set
# (work/ is never committed). 0: no recording; leave it out to record more blocks first (a few minutes)
bash cps3recomp/tools/translate.sh path/to/sfiii3nr1.zip 0
# the other two games' code too (their own programs', and 3rd Strike's stages': without it their boards run in the
# interpreter, about a fifth the speed)
GAME=sfiiin bash cps3recomp/tools/translate.sh path/to/sfiiin.zip 0
GAME=sfiii2n bash cps3recomp/tools/translate.sh path/to/sfiii2n.zip 0

# the collection: every game's program (each built in build/<id>) and the one launcher, in build/collection
sh cps3recomp/tools/collection.sh
./build/collection/sf3comp-launcher
sh cps3recomp/tools/make_app.sh --collection   # macOS: build/collection/SF3comp.app

# or one game, with a launcher of its own
cmake -S . -B build/sfiii3 -G Ninja -DGAME=sfiii3 && ninja -C build/sfiii3
./build/sfiii3/sfiii3-launcher
sh cps3recomp/tools/make_app.sh sfiii3     # macOS: build/sfiii3/Street Fighter III 3rd Strike.app
```

`collection.sh` builds each game as the second form does (`cmake -DGAME=<id>` in `build/<id>`, the target `<id>`),
copies the programs to `build/collection/` and builds the launcher there from `cps3recomp/launcher` with
`COLLECTION=ON`. Arguments are given to every CMake configure; `SKIP_GAMES=1` takes the programs already built and
builds only the launcher; `B=` and `GB=` say where the collection and the games' builds go. 3rd Strike's program
holds the other two games' translated code as well as its own (the stages), so their code is compiled twice: once
for it and once for their own programs.

## The collection

The launcher is recomp-common's, built as a collection (`LAUNCHER_COLLECTION`): `cps3recomp/launcher/src/
description.cpp` tells it every game from the table `cps3recomp/tools/gamecfg.py --collection` writes from the
`game.cfg` files (each game's ROM set and its files' checksums, the switches of its first page, the games whose sets
it can use). A game is listed when its program is beside the launcher.

- **Names.** The collection is `SF3comp`: `sf3comp-launcher`, `SF3comp.app`, the app id
  `io.github.tmcleroy.sf3comp`, and the folder `SF3comp` in the player's user folder, which holds only the launcher's
  `launcher.ini` (each game's ROM set under `[game.<id>]`, its first-page switches under `[video.<id>]`).
- **Each game's folder** is named by the game (`Street Fighter III 3rd Strike`, ...), as the launcher of one game
  names it: `settings.ini`, `saves/` (the board's settings memory, save states), `logs/`. So what a player of 3rd
  Strike had before the collection is still used, and the ROM set that launcher had chosen is taken from its
  `launcher.ini` the first time the collection starts (with its Widescreen and Full screen switches).
- **Order.** `collection_order` in a `game.cfg` (lowest first), then the date in the set's name. 3rd Strike is first:
  the launcher's keys and buttons, shared by the three games, are read from the first game's `settings.ini`.
- **Stages.** 3rd Strike is started with `--guest sfiiin=PATH --guest sfiii2n=PATH` for the sets chosen on those
  games' pages (`guests` in its `game.cfg`); without them it looks beside its own set, as before.
- **Tests.** `SF3COMP_HOME=folder` keeps everything in that folder (the launcher's `launcher.ini`, and a folder for
  each game named by its id) and never touches the player's; `sf3comp-launcher --screenshot PREFIX` draws each
  page to a picture with no window. `sf3comp-launcher --play ID` starts a game straight away.
- **A game's program** knows it is in the collection when `sf3comp-launcher` is beside it
  (`cps3recomp/port/port.h`, `InCollection`): its "back to the launcher" opens that one.

The checks: `bash cps3recomp/tools/check.sh path/to/sfiii3nr1.zip 6000 1500` (the translated code against the
interpreter, block by block) and `ROM=path/to/sfiii3nr1.zip bash cps3recomp/tools/check_online.sh` (two copies
online over a bad connection, rollback and all).

## With no game (what CI builds)

`tools/ci_smoke.sh` builds the collection with no translated code (with a ROM set a program would run its game in the
interpreter, about a twentieth the speed), and starts everything with no window: each program lists its features, the
launcher draws its pages to pictures. `tools/audit.sh` checks the files for anything that must never be
committed. CI (`.github/workflows/ci.yml`) runs both, and the release scripts below with `NO_GAME_CODE=1`, on macOS,
Linux and Windows.

## Release builds

`tools/release/` builds one download for each system: the collection (the launcher and the three games' programs),
with SDL built in and the licences beside the programs:

| Script | Where | Makes |
| --- | --- | --- |
| `macos.sh` | macOS | `build/release/SF3comp-macOS.zip` holding `SF3comp.app` (Apple silicon and Intel) |
| `linux.sh` | Linux, x86_64 or ARM64 (Docker: cps2recomp's `cps2recomp-build` image) | `SF3comp-linux-x86_64.tar.gz` |
| `windows.sh` | Linux with MinGW-w64 (the same image) | `SF3comp-windows-x64.zip` |

Each builds in `build/release-<system>/` (the games in `games/<id>`, the launcher in `launcher`) and needs every
game's translated code (above) and Rust for librashader (`cps3recomp/tools/librashader.sh`, the Display
page's shaders). `check_archive.sh` fails an archive that holds game files or a home path, or lacks a licence, a game's program
or the launcher. Each script also writes `build/release/update-<system>.txt`, the list of files the launcher's
self-update reads (`docs/RELEASING.md`). A
release's program holds code translated from the game: it is made on the machine of someone who has the ROM sets,
never by CI, and whether it is given to others is the owner's decision.
