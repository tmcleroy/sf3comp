# Street Fighter III New Generation: progress

The set is sfiiin (Asia 970204, NO CD). It is a game of its own in the collection's launcher (2026-11-09), built from the sources the
three Street Fighter III games share (`games/shared/sf3`) and its own facts (`src/facts.cpp`). Its translated code
is also linked into 3rd Strike's program, for the definitive version's stages.

## What it has

- The game as the board plays it, with what the port gives every game: the in-game menu, save states, speed, run-ahead,
  arcade timing, the display options, controls, online play with rollback.
- Free play, straight to the opening, the cheats (no damage for each player, one-hit knock-outs, infinite time) and the
  Board page (difficulty, damage level, timer speed, rounds, event, continue, demo sound): no bonus games, one Rounds setting.
- Training mode (`games/shared/sf3/training.cpp`, the toolkit's session told this game's facts): a session started from
  the in-game menu (player 1 chooses both characters), no clock, nobody knocked out, health and gauge refills, "Start
  over" (where the round began or a corner, sides swapped or not), the stage chosen (its own 12 and a spare one); the dummy stands,
  crouches, jumps, guards (always, after a hit, at random), records and replays, answers with the recording, gets up
  quickly, mashes out of stun, parries a share of the attacks, or is played by the game; the input display, the
  combo and stun readouts, hit boxes (projectiles' too), frame data with its meter and log, and the parry readout
  (forward, down and in the air: this game has no window of its own against jump-ins, no throw escape, and no red parry).

Not planned (the user's decision, 2026-11-09): the alternative modes (survival, tag team, ring out, the viewers and the
rest). This game does not need 3rd Strike's whole feature set: training mode is what matters most for it.

Not yet: custom soundtrack, native sound (the board's mixer is what is heard: `Game::boardSound`; the native mixer has
no sound driver of this game's feeding it, and played nothing).

Widescreen (`games/shared/sf3/widescreen.cpp`, `checkwide.sh`): where a far layer's art ends short of the wider picture at a
stage's end, the renderer shows the layer's own end again, mirrored, under everything else (`native::SetFillEnds`).
checkwide.sh still names a few pictures: 2 to 5 columns on New Generation's stage 13 and 2nd Impact's 3 and 4 with the
invisible walls, 24 on 2nd Impact's stage 4 (its pieces there are objects, not a tilemap), and 2nd Impact's stage 9, whose
cliff is black art (not a fault).

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

### Training mode's

Found from the game's own routines (their literal pools, by the routines' names) and the records' layouts as the
routines use them, then checked in scripted training sessions (`games/shared/sf3/checktraining.sh`: RAM traced a frame
at a time, dumps, pictures with the boxes drawn). `src/facts.cpp` has each with its evidence.

| What | Where | How checked |
| --- | --- | --- |
| The clock's frames left of its second (word) | 0200EB34 | counts down a frame; kept above 0 the picture's clock stays at 99 |
| Character select, each player (byte: 2 choosing, 5 chosen) | 02012CEB, 02012CF3 | 1 to 5 traced through a made-up game's select; a session gets to the choice by it |
| The camera's middle, and its ends (words) | 0201BF14, 0201BF18; 0201BF58, 0201BF5C | the middle of the players' places in dumps; the boxes drawn through it sit on the fighters |
| The stage of the next fight (byte) | 02012CA3 | written before a fight: all 13 numbers pictured (6 is a stage no character of this game has) |
| In a record: who works it, facing, the record that hit it | +0x03, +0x0A, +0x14 | the dummy fights by itself at 0; the boxes turn by it; player 1's address from the first hit |
| ... its routine and reaction (words), hit freeze (byte) | +0x26, +0x28, +0x45 | 4/0 through a punch, 1/25 stunned; 7 down to 0 after a hit |
| ... place (words), animation and its frame | +0x64, +0x68, +0x1C0, +0x1C4 | traced walking, jumping and through moves |
| ... the boxes: body (5), attack (3), thrown-by (1), push (1) | +0x258, +0x260, +0x268, +0x270 | drawn in pictures: on the fighters, the attack's on the limb out, a fireball's at its front |
| ... met flag, hurt stop, hits taken | +0x299, +0x2C6, +0x2D6 | 1 from the frame a punch hits; -12 as a parry lands; one more a hit |
| ... character, thrown, cannot guard, guarding, attacking, input taken | +0x334, +0x343, +0x346, +0x347, +0x39E, +0x39F | Ryu 2, Ken 11; 22 throws; 33 guards seen in 33 moves guarded |
| ... its commands', super art's and stun's records (pointers) | +0x32C, +0x364, +0x36C | 0201B6C4, 0200D93C, 0200D97C for player 1 in dumps |
| The super art's record: fill, most stocks, stocks, kind | +0x0C, +0x14, +0x16, +0x04 | set full the gauge says MAX and the art comes out (10 times); the kind from the code alone |
| The stun's record: most, timer, taken | +2, +4, +8 | the readout follows the hits: 0 of 64, up with each, draining after |
| The parry windows (forward, down, air) | the commands' record +0x1D, +0x1F, +0x21 | the dummy's taps parried 52 of 52 normals (forward 31, down 21), 8 jump-ins (forward) and, jumping itself, 7 attacks (air) |
| The projectiles' list and records | 0205DF0A; 0201DE04, 0x800 apart | a fireball in flight is the third record of list 3; its box is drawn |

## Checks (2026-11-09)

- `GAME=sfiiin cps3recomp/tools/check.sh` over 3000 frames: interpreter and translation the same in attract mode and in
  made-up play, the translation the same twice, 3,505,666 dispatches compared block by block with 0 different.
- Rollback (`--rollback-test 3`) and run-ahead 1 and 2: the same hashes as a plain run over 4000 frames.
- `check_online.sh`: two copies over a lossy link the same over 3000 frames, and the same as the controls replayed.
- Straight to the opening on and off: the same hashes over 3000 frames.
- Each cheat and free play in a run (the health, clock and credits read from RAM).
- `games/shared/sf3/checktraining.sh sfiiin ROM`: 20 judgements of scripted training sessions, all passing (the session,
  health and gauge refills, start over in both corners, the stage, the dummy's stances, guard, record and replay, quick
  stand, stun mashing and computer, the combo, input and stun readouts, frame data and its log, the parries' kinds and
  rates). A standing light punch out of reach measured 4/3/5 (11 frames) every time, the frames player 1 was not free for.
- With every training feature off the game runs as before the training sources were added: `--auto 7` over 4000 frames
  gives the same hashes (with the default features, and without free play and the straight start).

Not checked by a person: nobody has played it in a window yet. Training mode's pictures were read from headless runs only; the hit boxes are
those of the frame before (the picture a frame shows is of the fighters as the frame before left them: drawn from the frame's own
records an attack's box showed a frame before the limb was out; seen frame by frame, a standing heavy punch's box is now on the arm for
its three frames). Not tried: a super art whose gauge is a timer, other characters than Ryu and Ken.
