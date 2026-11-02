# Street Fighter III 3rd Strike: progress

## Native / emulated

- The board (`cps3recomp/machine`): own code, no emulator. SH-2 from satrecomp.
- The game's SH-2 code: translated ahead of time (`cps3recomp/tools/translate.sh`, 18,789 blocks from attract mode
  and five seeds of made-up play; checks in `tools/check.sh`: interpreter and translation the same over 6000 frames
  of each, 3.56 M dispatches compared block by block, 0 different). 287 fps headless on a loaded machine (the
  interpreter: 15). Code not reached by the recording runs in the interpreter.
- Picture: own software renderer (`cps3recomp/video/renderer.cpp`), MAME's drawing rules.
- Sound: own 16-voice mixer (`cps3recomp/sound/pcm.cpp`), not listened to yet.
- Frontend: cps2recomp's port/ (window, SDL_GPU presenter, shaders, menu, HUD, zoom, bindings, netplay, run-ahead,
  save states) on the board through `runtime/system.h`; recomp-common's launcher (`cps3recomp/launcher`).
- Not yet: translation of the SH-2 code (a branch is in progress), training mode (fight/), widescreen, custom
  soundtrack, Linux/Windows builds.

## Game facts

- Set `sfiii3nr1` (Japan 990512, NO CD: the Fightcade revision, the user's choice on 2026-11-02; downloaded from the
  Internet Archive's MAME 0.264 non-merged item to ~/code/recomp/cps3-roms). The 990608 `sfiii3n` also runs (only SIMM 1 differs). Program: cartridge ROM (BIOS, 512 KB) + SIMM 1-2 (16 MB, the game's code
  runs from 0x06000000). Graphics and samples: SIMM 3-6 (64 MB).
- Cipher keys: found from the set at load (`machine/crypt.cpp`), never stored.
- The BIOS checks each program SIMM's flash ID (04 / AD) with the SH-2's DMA from the cache-through mirror
  0x26000000; the DMA reads past the cipher.

- Credits: byte 02007CE0 (free_play keeps it at 2). Round clock: byte 02011377 (seconds; infinite_time).
- The DMA-done interrupt (level 10) drives the game's queue of character and palette DMAs (handler 060004D0 calls
  06138230: state word 0207603C, queues at 0207803C / 0207844C). Its timing matters: with the board's clock read
  stale inside a CPU slice the queue stuck and the fighters were never loaded.

- Training aids (games/sfiii3/src/training.cpp, on cps3recomp/fight/): refill health, full meter, hit boxes, input
  display. The player records, boxes, camera and meter places are facts from Grouflon's 3rd_training_lua (no licence:
  facts only, no code), made for the 990512 set; life and positions checked on this 990608 set, the rest seen to
  work in a fight's pictures only.

## Gotchas

- Addresses 0x2xxxxxxx are the cache-through mirror of 0x0xxxxxxx: main RAM is reached at 0x22000000 too.
- Interrupts are levels 12 (vertical blank) and 10 (palette/character DMA done), auto-vectored, held until
  acknowledged at 05100000 / 05110000.

## Not checked by a person

- No one has seen it in a window, played it with a pad or keyboard, or heard it. Pictures and WAVs from headless runs
  only. The launcher's pictures were taken with no window.
- Online play compiles but has never been tried (its service is cps2recomp's; the lobby is keyed by `sfiii3`).
- The VS screen shows horizontal stripes: probably the game's wipe, not checked against the board.

## Next

1. Translation coverage: more recorded play (every character, supers, the endings).
2. The full training session: port cps2recomp's fight/session.{h,cpp} with the modes and battle layers it sits on
   (alternative.cpp, battle.cpp: these use cps2recomp's spare machines) and a 3S description. Health here is a byte
   (session.cpp reads words). The steps and contacts are read once a frame from RAM until the translation gives hooks.
   Frame data needs 3S's own hit-stun and guard-stun state (not in the training script's facts): compare with
   published frame data.
3. Widescreen: the board has a 496-column mode; the stage and camera limits need finding.
4. The definitive version: New Generation (sfiiin) and 2nd Impact (sfiii2n) sets are in ~/code/recomp/cps3-roms
   (Internet Archive, the user's choice); both run their attract modes on the board (games/sfiiin, games/sfiii2n:
   game.cfg only). 2nd Impact's instructions are deciphered but its data is not (RomSet::opcodes); its cartridge's
   keys are those of a dead CPU, tried when they cannot be solved. Next: find where NG and 2I keep their stages and
   music, and how 3S loads its own, to graft them. 2nd Impact's SIMM program is not enciphered (game.cfg program_enciphered = 0).
5. Online play's cost: a save state is 10 MB (8 MB of it character RAM) and took 14.7 ms to keep on a loaded
   machine. Keep character RAM by changed pages (it changes in bulk, by the character DMA) before trying online play.
   The rollback check itself passes: `--rollback-test 3 --hash-log` equals a plain run's over 2600 frames of made-up
   play (227 rollbacks, 1255 frames run again).
6. Linux / Windows builds (cps2recomp's and fighting_vipers' tools/release as the model).

## Decisions for the user

- Which 3S revision: this port is on sfiii3nr1 (990512, chosen by the user: the Fightcade set; from the Internet Archive). Fightcade and the training scripts use
  sfiii3nr1 (990512).
- 3SX (crowded-street/3sx) is a native 3S port built on the PS2 decompilation; this project goes on separately from
  the arcade program (the NG / 2I content is arcade-only).
- Online play uses cps2recomp's online service (lobby keyed by game id `sfiii3`).
