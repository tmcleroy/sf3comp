# Contributing

Thanks for helping! This repository is sf3comp: the CPS-3 toolkit (`cps3recomp/`) and Street Fighter III: 3rd
Strike's port on it (`games/sfiii3/`), with the two games whose stages it can show (`games/sfiiin/`,
`games/sfiii2n/`).

## The rules that keep this project legal

These aren't negotiable; pull requests that break them are closed.

1. **Never add anything from a game.** No ROM or part of one, no extracted graphics, sounds or text, no settings
   memory (EEPROM), save states, RAM dumps or video, and nothing generated from a game: the translated C++
   (`work/<id>/gen/`), the block database (`work/<id>/blocks.txt`), disassembly listings. What a game's folder may
   hold is its own code, its `game.cfg` (the ROM set's file names and checksums), and notes found by playing and
   watching memory. The README's pictures are made by the program and kept few. The same goes for issues: attach
   logs, not game files.
2. **Never add a cipher key.** The CPS-3 program cipher's keys are worked out from the player's own ROM set when the
   game starts (`cps3recomp/machine/crypt.cpp`). No key, no number derived from one, and no table of them may be
   written into the code, the docs, a test or a commit message. Never point to where ROM sets can be found.
3. **Nothing from leaked material**: no leaked source, no Capcom documentation or tools, and no names taken from
   them.
4. **No emulator's code, and no decompilation's code.** Reading MAME or another emulator to learn how the board
   behaves is fine (`THIRD_PARTY_NOTICES.md` credits what was learned); copying its code in is not. The PS2
   decompilation of 3rd Strike (crowded-street/3s-decomp) is AGPL-3.0: its names for the game's routines and
   variables may be used, as facts, never its code.
5. **Only add code you have the right to contribute**, under the GPL-3.0. Code from other projects must have a
   compatible licence and be credited in `THIRD_PARTY_NOTICES.md`.

## Where things go

The toolkit (`cps3recomp/`) may not be about one game: no addresses, names or special cases of a game's. What a
game needs goes in its `game.cfg` or its own sources (`games/<id>/src`), through the toolkit's hooks
(`cps3recomp/runtime/lift.h`, `cps3recomp/runtime/game.h`).

`cps3recomp/recomp-common/` is a copy of another repository and is never changed here: changes go to
[recomp-common](https://github.com/tmcleroy/recomp-common), then `cps3recomp/tools/sync-common.sh`.

Every change to how the game plays, looks or is controlled is an optional feature, off in the Pure preset.

## Building and testing

`tools/audit.sh` and `tools/ci_smoke.sh` need no game: the audit checks the files, the smoke test builds the
program with no translated code and the launcher and starts both with no window. CI runs both, and builds and
starts the programs on macOS, Linux and Windows. [docs/BUILDING.md](docs/BUILDING.md) has the rest.

A change to the translator, the machine, the picture, the sound or the game's code needs the game as well, which
only someone with the ROM sets can run:

- `bash cps3recomp/tools/check.sh <sfiii3nr1.zip> 6000 1500`: the translated code against the interpreter, block by
  block;
- `games/sfiii3/checklifts.sh`, `checkfight.sh`, `checkstages.sh`: every hand-written routine against the game's own
  code on every call; `checknative.sh` and `checksound.sh`: the native picture and sound against the board's;
  `checkguests.sh`, `checkmodes.sh`, `checktraining.sh`: the guest stages, the modes and training mode;
- `ROM=<sfiii3nr1.zip> bash cps3recomp/tools/check_online.sh`: online play over a bad connection, with rollback;
- with every feature off the game must run exactly as before: the same movie must give the same hashes
  (`--movie`, `--hash-log`).

Say in the pull request which were run. Nothing may add input lag or slow the game down: measure when in doubt.
Both the keyboard and gamepads must keep working.

## Code style

- Match the surrounding code: naming, comment density and idiom. Comments explain why.
- C++20; 4-space indents, braces on the same line, lines up to about 120 columns.
- Every source file starts with the licence and copyright lines the others have; the audit fails on one without.
- Commit messages: a short summary line, then what changed and why.

## Code of conduct

Be kind and constructive. See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
