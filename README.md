# SF3comp

**[Join me on Discord](https://discord.gg/FSC9GZ6nm)**

[![CI](https://github.com/tmcleroy/sf3comp/actions/workflows/ci.yml/badge.svg)](https://github.com/tmcleroy/sf3comp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/tmcleroy/sf3comp?include_prereleases)](https://github.com/tmcleroy/sf3comp/releases)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)

<img width="1561" height="1220" alt="sf3-logo-menu" src="https://github.com/user-attachments/assets/ec532429-2d1e-48b9-90e0-632ec7c45785" />





**SF3comp is an unofficial native port of Capcom's three Street Fighter III arcade games (CPS-3), New Generation, 2nd
Impact and 3rd Strike, in one app for macOS, Linux (Steam Deck and Steam Machine included) and Windows.** The games'
SH-2 programs are translated ahead of time into C++ and run natively on your machine.
All three get a full training mode, online play with rollback and widescreen. 3rd Strike also gets new modes and the
stages and music from New Generation and Second Impact.

**[Get started →](#getting-started)**

**No game files are included.** You need your own arcade ROM sets: `sfiii3nr1` (3rd Strike), `sfiiin` (New
Generation) and `sfiii2n` (2nd Impact), any of them; `sfiii3n` too for 3rd Strike's later revision ([what you need](#what-you-need)). This project cannot help you get them.

## What sf3comp does that I haven't found in any other 3rd Strike port
### I'm aware there are other 3rd strike decomp/recomp projects, this is what I believe makes mine unique
- **New Generation's and 2nd Impact's stages and music, playable in 3rd Strike.** With those ROM sets beside 3rd Strike's, you can fight on any of their stages. They are drawn by those games' own stage code running beside 3rd Strike's, not converted into another engine.
  <img width="3584" height="1764" alt="sf3comp-all-stages" src="https://github.com/user-attachments/assets/16e71b6d-c4bf-48a9-a300-9d661f491c4b" />
- **It's based on the arcade version (CPS-3):**  The other native ports are either matching decomps meant to compile to the CPS-3 ROM (which is very cool) or built from the console versions that aren't played competitively.
- **Arcade code with a full console-like feature set:** Rollback online play, a full training mode (hit boxes, frame data and frame meter, a dummy that parries, record and replay) and save states, all on the arcade program itself, so there is no console behaviour to correct.

  https://github.com/user-attachments/assets/89200dbd-9bf8-4c74-ab92-0e046ec7acca

- **Checked against the original:** The translated code is compared with an interpreter block by block (registers, cycles and RAM), and each hand-written routine is compared call by call with the game's own code (registers, RAM and hardware writes).
- **Fully native picture and sound, from the game's own drawing and sound code:** No emulator runs and no video or sound chip is emulated: the game hands its frames and voices straight to the port's own renderer and mixer. The board's chips are modelled only as the reference the checks compare against.
- **An optional arcade timing mode:** that slows the CPU where the real board's bus, cache and pipeline do, measured against jotego's CPS-3 core for the MiSTer run in simulation: within a frame of it over start-up and attract mode.
- **Modes the board never had:** tag team battle (including two against two for four players), ring out, and endless battle on stages that scroll one into the next.
- **The whole trilogy:** New Generation and 2nd Impact are fully playable in the same app, each with training mode, widescreen and rollback online. I recomped them to add their stages to 3rd strike and ended up making both games playable as well.
- **Match replays:** every match is kept, and you can take control from any moment of a replay.




## Training mode

In all three games, from the in-game menu, a fight with no clock that nobody loses:

- **Hit boxes** (projectiles' too), **frame data** (startup, active, recovery, advantage) with a **frame meter** and a
  log to a file, an **input display**, and combo, damage, stun and parry readouts.
- **A dummy** that guards (always, after the first hit, or at random), **parries** and red-parries a share of your
  attacks, escapes throws, gets up quickly, mashes out of stun, **records and replays** what you show it, answers
  with the recording, or is played by the game's own computer at the level you choose.
- A **hit confirm drill**: the dummy guards at random and scores whether you went on only when the attack hit.
- In 3rd Strike, a **red parry drill**, and the **Evo Moment 37** scenario: Ken on a sliver of health against
  Chun-Li's super art, try after try, with your parries counted.
- Health and super art gauges refilled, any stage (the other games' too), and save states.

## Online play

Two players on two machines, with **rollback**. Host from the launcher's Online page and give the other player the
match's code, or find each other in the lobby or the waiting room; nothing needs setting up on either router. The
match has the host's board settings. Any mix of macOS, Linux, the Steam Deck and Windows can play each other.

## Replays

Every match you play, offline or online, is kept as a small file and listed on the in-game menu's Replays page. A
replay is the game run again from the match's start with what both players held, so it can be stopped, stepped a
frame at a time, slowed, and watched with the hit boxes, the input display and the frame data on. Go back and forward
in it, and press Start at any moment to **take over a side and play on from there**; the restart key puts you back
at that moment to try again.

## 3rd Strike's other revision

The board had two releases a month apart. The definitive version is made from the first, 990512 (`sfiii3nr1`), the one
tournaments and Fightcade play. The later one, 990608 (`sfiii3n`), is in the launcher as a game of its own, with
training mode, widescreen, online play and replays, as the two older games have.

## The definitive version's stages (3rd Strike)

With New Generation's and 2nd Impact's ROM sets beside 3rd Strike's, a fight can be on any of their stages, or on
each opponent's own, with that game's music: in arcade fights, training, the stage viewer and the modes. In the arcade
game each opponent's stage can also come from any of the three games at random. Their stages are drawn by their own
games' stage code, run beside 3rd Strike's.

## Alternative modes (3rd Strike)

The in-game menu's first row has ways to play the board never had: **survival**, **endless battle** (the game plays
both fighters on stages that scroll or fade one into the next; touch the controls to take one over), **ring out**,
a **tag team battle** (tag your partner in, or two against two for four players), the **bonus games** straight
away, a **stage viewer**, a **model viewer** and a **colour editor** whose colours you then fight in. And **Gill**,
playable by either player.

## Everything else

- **Widescreen**: the stage drawn out to 16:9, or 16:9 played exactly as the board plays (its own walls).
- **Native picture and sound**: the game's drawing and sound code hands its frames and voices to the port's own
  renderer and mixer; the board's video and sound chips are modelled only as the reference the checks compare
  against. Sharp or smooth scaling, 4:3 or square pixels, RetroArch shaders, and a zoom that follows the fighters.
- **An in-game menu** (Esc or F1; on a gamepad the left stick's click, or Start and Select together): save states,
  speed, the cheats, the board's own settings and test menu, every feature. Pure plays exactly as the board does;
  Enhanced (the default) adds free play, arcade timing, integer scaling and, in widescreen, invisible walls.
- **Controls**: keyboard, gamepad or arcade stick, every button changeable per player.
- **A custom soundtrack**: your own music in place of the board's, by song or as a playlist.
- **Arcade timing** (on by default): the processor slows down where the real board does, checked frame by frame
  against a cycle-accurate model of it ([docs/TIMING.md](docs/TIMING.md)).
- **A launcher** that checks your ROM set, sets every feature and both players' controls, hosts and joins online
  matches, and adds the game to Steam.

<img width="1092" height="804" alt="image" src="https://github.com/user-attachments/assets/ca7ca2fe-ff78-4358-8af7-4ff47f7c5e09" />

## Getting started

1. Download the build for your system from the [releases page](https://github.com/tmcleroy/sf3comp/releases) and
   unpack it.
2. Open the launcher (`SF3comp.app`, `sf3comp-launcher`, `sf3comp-launcher.exe`), pick a game and choose its ROM set
   (or drop it on the launcher). With `sfiiin.zip` and `sfiii2n.zip` beside `sfiii3nr1.zip`, 3rd Strike has the
   other games' stages too. Your ROM sets are only ever read.
3. Press **Play**.

- **macOS:** the app isn't notarized: the first time, right-click it and choose Open.
- **Windows:** the programs aren't signed: if Windows says it protected your PC, press More info, then Run anyway.
- **Steam Deck and Steam Machine:** in Desktop Mode, unpack the Linux download somewhere it can stay, open
  `sf3comp-launcher`, choose your ROM sets, and press **Add to Steam**; the game is then in Game Mode under
  **Non-Steam**.

Settings, save states and the board's settings memory are kept in your user folder.

## What you need

- **`sfiii3nr1`**: Street Fighter III 3rd Strike: Fight for the Future (Japan 990512, NO CD), as a zip or a folder of
  its files. The launcher checks every file and says which is missing or different. Other revisions don't work: the
  port is made from this one's program.
- **`sfiiin`**: New Generation (Asia 970204, NO CD), and **`sfiii2n`**: 2nd Impact (Asia 970930, NO CD). Each plays
  as a game of its own, and beside `sfiii3nr1` gives 3rd Strike its stages.
- macOS 13 or later (Apple silicon or Intel), Linux x86_64 (SteamOS included) or Windows 10 or 11, 64-bit.
- A gamepad or stick is recommended (six buttons).

The CPS-3's program is enciphered; the port works out each set's cipher key from the set itself when the game starts.
No key is in this repository or in a release.

**Status:** early (beta). The whole game plays, and its translated code is checked against an interpreter
instruction by instruction. Played on macOS, a Steam Machine and a Steam Deck (online between a Mac and a Deck).
The Windows build runs the game exactly as the Mac's (checked frame by frame) but has not yet been played on a
Windows PC.

## How it works

The game's SH-2 code is translated ahead of time into C++ from your own ROM set and compiled into the program; what
the translation did not reach runs in an interpreter. The parts the features touch (the fight, the computer player,
the stages, the drawing and sound code) are rewritten by hand as readable C++, each checked call by call against the
game's own code. The board (memory map, cipher, DMA, settings memory) is code written for this project. The CPS-3
toolkit is in [`cps3recomp/`](cps3recomp/), the game's code in [`games/sfiii3/`](games/sfiii3/).

## Building from source

With CMake, Ninja, a C++20 compiler, Python 3, SDL 3 and zlib, and your own ROM sets:

```sh
bash cps3recomp/tools/translate.sh path/to/sfiii3nr1.zip 0    # once for each set: its code translated (work/, never committed)
sh cps3recomp/tools/collection.sh                             # the three games and the one launcher, in build/collection
./build/collection/sf3comp-launcher
```

[docs/BUILDING.md](docs/BUILDING.md) has the other games' stages, the release builds for each system and the checks;
[docs/RELEASING.md](docs/RELEASING.md) how a release is made.

## Feedback and contributing

Bug reports and pull requests are welcome: see [CONTRIBUTING.md](CONTRIBUTING.md) for the rules (above all: nothing
from the game, ever, and never a key). Security reports go by [SECURITY.md](SECURITY.md).

## Related projects

| Project | What |
| --- | --- |
| [cps2recomp](https://github.com/tmcleroy/cps2recomp) | The same kind of port for Capcom's CPS-2 games, whose frontend, training and online play this one shares |
| [satrecomp](https://github.com/tmcleroy/satrecomp) | The Sega Saturn toolkit, whose SH-2 translator and interpreter this one started from |
| [recomp-common](https://github.com/tmcleroy/recomp-common) | The in-game menu, settings and launcher these projects share |

## License and credits

[GPL-3.0-only](LICENSE). [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) has the notices in full.

- The program cipher and the board's drawing rules follow MAME's CPS-3 driver (BSD-3-Clause; the cipher is Andreas
  Naive's work); no MAME code is here.
- The arcade timing follows [jtcores](https://github.com/jotego/jtcores)' SH7604 model in jotego's CPS-3 core
  (GPL-3.0-or-later).
- Names for the game's routines come from the PlayStation 2 decompilation (crowded-street/3s-decomp); some of the
  training mode's memory addresses from Grouflon's [3rd_training_lua](https://github.com/Grouflon/3rd_training_lua).
  No code of either is here.
- The launcher's look comes from the launcher of [wiiparty-recomp](https://github.com/arelkair/wiiparty-recomp) by
  arelkair (GPL-3.0-or-later), in `cps3recomp/recomp-common/launcher/`.
- The programs use SDL 3 (zlib), Dear ImGui (MIT), the Inter font (SIL OFL 1.1) and librashader (MPL-2.0 or
  GPL-3.0); the release archives carry their licenses.

The repository holds no game data and nothing derived from it.

This project is not affiliated with, endorsed by, or sponsored by Capcom. *Street Fighter* is a trademark of Capcom
Co., Ltd.; "CPS-3" is a name of Capcom's, used here only to say what the program works with.
