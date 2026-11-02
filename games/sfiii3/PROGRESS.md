# Street Fighter III 3rd Strike: progress

## Native / emulated

- The board (`cps3recomp/machine`): own code, no emulator. SH-2 from satrecomp.
- The game's SH-2 code: translated ahead of time (`cps3recomp/tools/translate.sh`, 23,063 blocks from attract mode
  and thirteen seeds of made-up play; checks in `tools/check.sh`: interpreter and translation the same over 6000 frames
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

- Widescreen (Game category: it changes play): 64 columns more each side (the renderer's margin: tilemaps drawn on,
  objects and the text layer moved over); in a fight the camera's ends (02026CF4/CF6 right, CF8/CFA left) are
  narrowed by 64 so the picture never shows past the stage. Seen on Ken's stage at both ends; other stages, the
  objects the game leaves out at the edges (it culls to its 384 columns) and non-fight screens not looked at.
- Straight to the opening (skip_intro, Enhanced): the first 760 frames (memory checks, region warning, Capcom's
  logo) fast-forwarded. Cheats: no damage (each player), infinite time.
- Credits: byte 02007CE0 (free_play keeps it at 2). Round clock: byte 02011377 (seconds; infinite_time).
- The DMA-done interrupt (level 10) drives the game's queue of character and palette DMAs (handler 060004D0 calls
  06138230: state word 0207603C, queues at 0207803C / 0207844C). Its timing matters: with the board's clock read
  stale inside a CPU slice the queue stuck and the fighters were never loaded.

- Training mode (games/sfiii3/src/training.cpp on cps3recomp/fight/session.cpp, cps2recomp's session without its
  modes and battles): the menu's Start (board reset, way to character select worked unseen, player 1 choosing for
  both), Start over (sides swapped or not), no clock, combo readout, input display, frame data and meter, health
  refills, the dummy (the toolkit's: stand, crouch, jump, guard, record, replay), hit boxes, full meter. Seen working
  in a made-up fight's pictures only. Frame data is read once a frame from the records (not the game's own steps)
  and checked on one move so far: Alex's standing LP reads startup 5, active 2, recovery 8, and the training script's
  recorded animation a444 has it hitting on frames 5-6 (a hit counts the frame after the box is out). Checked in bulk with
  `games/sfiii3/framecheck.py` (SFIII3_MOVES=1 logs each move; the reference is the training script's recorded
  animations, not in the repository): Alex's startups agree on every move recorded (123 of 123 in 12000 frames of
  made-up play, the recordings' first hit frame counted from 0 being startup less one; a hit registers on the step its
  box is out). Active differs when a move connects (the hit's freeze and the box taken away at the hit: the reference
  counts its own way); whiffs agree. Recovery and advantage are not checked against anything yet. "Nobody knocked
  out" puts health back to full below a quarter (no hook on the game's knock-out yet). The player records, boxes, camera and meter places are facts from Grouflon's 3rd_training_lua (no licence:
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

1. Frame data: recovery and advantage against published data (startups agree with the recorded animations; active
   on a hit differs by the hit's freeze). Then the dummy's guard (attackInRange from the boxes), wake-up and
   reversal options, stun readout, parry practice (3rd Strike's own).
2. Hooks in the translated code (satrecomp's lift/hook mechanism, not ported yet): the knock-out (so a session's
   "nobody knocked out" stops putting health back at a quarter), the round's steps, the music requests.
3. The definitive version (DEFINITIVE.md): music (which of the sound chip's voices are music: all sixteen play both
   music and effects, and their samples are mixed in the data; the sound driver's own record of what each voice
   plays is to find), New Generation's stage number and its stage ends, the stage rule checked on every stage, the
   stage boards translated, a proper menu of stages by name.
4. Custom soundtrack (needs the music requests, as 3).
5. Translation coverage: more recorded play (every character, supers, the endings).
6. Widescreen on every stage and screen; the objects the game culls at its 384 columns.
7. Linux / Windows builds (cps2recomp's and fighting_vipers' tools/release as the model), then the other devices.
8. The alternative modes (survival, endless battle, ring out), stage and model viewers, the colour editor, board
   settings (the EEPROM's operator settings), as the CPS-2 ports have them.

## Decisions for the user

- Which 3S revision: this port is on sfiii3nr1 (990512, chosen by the user: the Fightcade set; from the Internet Archive). Fightcade and the training scripts use
  sfiii3nr1 (990512).
- 3SX (crowded-street/3sx) is a native 3S port built on the PS2 decompilation; this project goes on separately from
  the arcade program (the NG / 2I content is arcade-only).
- Online play uses cps2recomp's online service (lobby keyed by game id `sfiii3`).
