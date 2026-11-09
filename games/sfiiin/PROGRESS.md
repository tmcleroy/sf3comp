# Street Fighter III New Generation: progress

The set is sfiiin (Asia 970204, NO CD). It is a game of its own in the collection's launcher (2026-11-09), built from the sources the
three Street Fighter III games share (`games/shared/sf3`) and its own facts (`src/facts.cpp`). Its translated code
is also linked into 3rd Strike's program, for the definitive version's stages.

## What it has

- The game as the board plays it, with what the port gives every game: the in-game menu, save states, speed, run-ahead,
  arcade timing, the display options, controls, online play with rollback.
- Free play, straight to the opening, the cheats (no damage for each player, one-hit knock-outs, infinite time) and the
  Board page (difficulty, damage level, timer speed, rounds, event, continue, demo sound): no bonus games, one Rounds setting.

Not yet: training mode, the alternative modes, widescreen, custom soundtrack, native picture and sound (it is drawn
and mixed by the board model, which 3rd Strike keeps only as its reference).

## Facts

Each was carried over from 3rd Strike's (`games/sfiii3/src/sfiii3.h`) with `cps3recomp/tools/crossmap.py` and then
checked in RAM dumps of made-up play (`--auto 7`: the title at frame 700, character select at 1500, a fight at 2250,
3000 and 3750) against the pictures of the same frames.

| What | Where | How checked |
| --- | --- | --- |
| Credits (byte) | 02007188 | counts the run's coins; kept at 2, Start begins a game with no coin |
| The round's clock (byte) | 0200EB33 | the clock the pictures show at the three fight frames |
| A character locked in (bytes, FF) | 02012C74, 02012C76 | 00 at the title, FF once chosen and through the fight |
| The match's state (byte, 2: fought) | 02012C55 | 0 at the title, 1 at select, 2 in the fight, 6 once a round is over |
| The players' records | 0200D18C, 0200D564 | health at +0x9F (160 full) goes down with the bars in the pictures |
| The opening | frame 780 | pictures every 100 frames: black at 800, the opening by 900 |
| The game settings in the EEPROM | bytes 0x21 difficulty, 0x22 damage, 0x23 timer, 0x24 rounds, 0x25 event, 0x27 bonus games (2nd Impact); system 0x02 continue, 0x05 demo sound | every one set at once from the Board page, then both pages of the test menu read: each row showed its choice. The game's RAM copy of the block and the EEPROM's bytes compared gave the order: the game's bytes are high first in each word |

## Checks (2026-11-09)

- `GAME=sfiiin cps3recomp/tools/check.sh` over 3000 frames: interpreter and translation the same in attract mode and in
  made-up play, the translation the same twice, 3,505,666 dispatches compared block by block with 0 different.
- Rollback (`--rollback-test 3`) and run-ahead 1 and 2: the same hashes as a plain run over 4000 frames.
- `check_online.sh`: two copies over a lossy link the same over 3000 frames, and the same as the controls replayed.
- Straight to the opening on and off: the same hashes over 3000 frames.
- Each cheat and free play in a run (the health, clock and credits read from RAM).

Not checked by a person: nobody has played it in a window yet.
