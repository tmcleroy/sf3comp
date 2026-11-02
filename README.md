# sf3comp

A native port of Capcom's **Street Fighter III** arcade games (CPS-3), made by static recompilation: the game's SH-2
program becomes C++ that runs on a machine written for this project, not an emulator.

The aim is one definitive Street Fighter III: **3rd Strike**'s gameplay, with the stages, music and more of
**New Generation** and **2nd Impact**. The first milestone is 3rd Strike on its own, with the features of the CPS-2
ports ([CPS-2 Recomp](https://github.com/tmcleroy/cps2recomp)): training mode, online play, widescreen and the rest.

**Status:** early. The board is built and 3rd Strike starts and runs its attract mode (interpreter only, no window yet).

**No game files are included.** You need your own ROM set (`sfiii3n`). Its cipher's keys are worked out from the set
itself when the game starts; none are in this repository.

## Layout

```
cps3recomp/   the CPS-3 toolkit: machine/ (SH-2, board, cipher, ROM set, flash), video/, sound/, port/, tools/
games/sfiii3/ 3rd Strike: game.cfg, src/, PROGRESS.md
work/, build/ generated from your ROM set, never committed
```

## Building

```sh
cmake -S . -B build/sfiii3 -G Ninja && ninja -C build/sfiii3
./build/sfiii3/sfiii3-headless path/to/sfiii3n.zip --frames 1800 --shots 150:work/f
```

GPL-3.0. Not affiliated with Capcom.
