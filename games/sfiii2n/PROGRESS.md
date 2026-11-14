# Street Fighter III 2nd Impact: progress

The set is sfiii2n (Asia 970930, NO CD). It is a game of its own in the collection's launcher (2026-11-09), built from the sources the
three Street Fighter III games share (`games/shared/sf3`) and its own facts (`src/facts.cpp`). Its translated code
is also linked into 3rd Strike's program, for the definitive version's stages.

## What it has

- The game as the board plays it, with what the port gives every game: the in-game menu, save states, speed, run-ahead,
  arcade timing, the display options, controls, online play with rollback.
- Free play, straight to the opening, the cheats (no damage for each player, one-hit knock-outs, infinite time) and the
  Board page (difficulty, damage level, timer speed, rounds, event, continue, demo sound): a Bonus games setting, one Rounds setting, and a Screen mode row in its test menu (the settings' byte 0x20) the port does not offer yet.
- Training mode (`games/shared/sf3/training.cpp`, the toolkit's session told this game's facts): a session started from
  the in-game menu (player 1 chooses both characters), no clock, nobody knocked out, health and gauge refills, "Start
  over" (where the round began or a corner, sides swapped or not), the stage chosen (its own 15); the dummy stands,
  crouches, jumps, guards (always, after a hit, at random), records and replays, answers with the recording, gets up
  quickly, mashes out of stun, parries a share of the attacks, escapes a share of the throws, or is played by the game; the input display, the
  combo and stun readouts, hit boxes (projectiles' too), frame data with its meter and log, and the parry readout
  (forward, down, in the air and against jump-ins; no red parry in this game).

Not planned (the user's decision, 2026-11-09): the alternative modes (survival, tag team, ring out, the viewers and the
rest). This game does not need 3rd Strike's whole feature set: training mode is what matters most for it.

Not yet: custom soundtrack, native sound (the board's mixer is what is heard: `Game::boardSound`; the native mixer has
no sound driver of this game's feeding it, and played nothing).

Widescreen (`games/shared/sf3/widescreen.cpp`, `checkwide.sh`) is the board's own wide picture (2026-11-13, issue #18):
the settings' byte 0x20 written before the board's first frame, a setting the test menu has as Screen mode
(the game page's first row, Screen mode). The game then sets the screen's timing
for 495 columns (`set_screen_mode` with 7 instead of 3), lays its panel out to the corners and keeps the camera's ends
for the wider picture. It reads the setting as it starts, into a byte of RAM (`Facts::boardWide`) it reads from then
on; `mode_init_task` lays the panel out by it, so that byte set while the game runs gives a wide picture with the
panel still laid out for 384. So when that byte is not what the switch says (switched in the in-game menu, or a first
start whose settings the game wrote its defaults over), the setting is written and the board started again, as a
training session's start does. checkwide.sh: every stage in both corners with nothing empty at
the edges (2nd Impact's stage 9 aside, whose cliff is black art), switched in a session and back, and with widescreen
off the same hashes as never asked for. The sources' 64 columns a side and their invisible walls (still 3rd Strike
990608's) are no longer used here.

## Facts

Each was carried over from 3rd Strike's (`games/sfiii3/src/sfiii3.h`) with `cps3recomp/tools/crossmap.py` and then
checked in RAM dumps of made-up play (`--auto 7`: the title at frame 700, character select at 1500, a fight at 2250,
3000 and 3750) against the pictures of the same frames.

| What | Where | How checked |
| --- | --- | --- |
| Credits (byte) | 02008528 | counts the run's coins; kept at 2, Start begins a game with no coin |
| The round's clock (byte) | 02010167 | the clock the pictures show at the three fight frames |
| A character locked in (bytes, FF) | 020142B8, 020142BA | 00 at the title, FF once chosen and through the fight |
| The match's state (byte, 2: fought) | 02014299 | 0 at the title, 1 at select, 2 in the fight, 6 once a round is over |
| The players' records | 0200E504, 0200E910 | health at +0x9F (160 full) goes down with the bars in the pictures |
| The opening | frame 780 | pictures every 100 frames: black at 800, the opening by 900 |
| The game settings in the EEPROM | bytes 0x21 difficulty, 0x22 damage, 0x23 timer, 0x24 rounds, 0x25 event, 0x27 bonus games (2nd Impact); system 0x02 continue, 0x05 demo sound | every one set at once from the Board page, then both pages of the test menu read: each row showed its choice. The game's RAM copy of the block and the EEPROM's bytes compared gave the order: the game's bytes are high first in each word |

### Training mode's

Found from the game's own routines (their literal pools, by the routines' names) and the records' layouts as the
routines use them, then checked in scripted training sessions (`games/shared/sf3/checktraining.sh`: RAM traced a frame
at a time, dumps, pictures with the boxes drawn). `src/facts.cpp` has each with its evidence.

| What | Where | How checked |
| --- | --- | --- |
| The clock's frames left of its second (word) | 02010168 | kept above 0 the picture's clock stays at 99 |
| Character select, each player (byte: 2 choosing, 5 chosen) | 0201432F, 02014337 | 5 and 5 once chosen, in dumps; a session gets to the choice by it |
| The camera's middle, and its ends (words) | 02022264, 02022268; 020222A8, 020222AC | 699 with the players at 846 and 547; the boxes drawn through it sit on the fighters |
| The stage of the next fight (byte) | 020142E7 | written before a fight: all 15 numbers pictured |
| In a record: who works it, facing, the record that hit it | +0x03, +0x0A, +0x14 | the dummy fights by itself at 0; the boxes turn by it; player 1's address from the first hit |
| ... its routine and reaction (words), hit freeze (byte) | +0x26, +0x28, +0x45 | 4/0 through a punch, 1/25 stunned |
| ... place (words), animation and its frame | +0x64, +0x68, +0x1C0, +0x1C4 | traced walking, jumping and through moves |
| ... the boxes: body (4), hands (2), throw (1), thrown-by (1), attack (2), push (1) | +0x260, +0x268, +0x278, +0x280, +0x288, +0x294 | drawn in pictures: on the fighters, the attack's on the limb out, a fireball's at its front |
| ... met flag, hurt stop, hits taken | +0x2BF, +0x2EC, +0x2FC | 1 from the frame a move hits; -15 as a parry lands; one more a hit |
| ... character, thrown, cannot guard, guarding, attacking, input taken | +0x358, +0x367, +0x36A, +0x36B, +0x3C0, +0x3C1 | Ryu 2, Ken 11; 47 throws; 33 guards seen in 33 moves guarded |
| ... its commands', super art's and stun's records (pointers) | +0x350, +0x388, +0x390 | 020218CC, 0200ED1C, 0200ED74 for player 1 in dumps |
| The super art's record: fill, most stocks, stocks, kind | +0x18, +0x20, +0x22, +0x06 | set full, with the redraw byte, the gauge says MAX and the art comes out (10 times); the kind from the code alone |
| The gauge's redraw bytes | 020145E0, 020145E1 | without them player 2's gauge stayed drawn empty |
| The stun's record: most, timer, taken | +2, +4, +8 | the readout follows the hits; 36 of 72 in a dump |
| The parry windows (forward, down, air, anti-air) | the commands' record +0x1D, +0x1F, +0x21, +0x2F | the dummy's taps parried 52 of 52 normals (forward 31, down 21) and 13 jump-ins (anti-air); in the air none (the window opened, twice too early) |
| The projectiles' list and records | 02064316; 02024210, 0x800 apart | a fireball in flight is the third record of list 3; its box is drawn |

## Checks (2026-11-09)

- `GAME=sfiii2n cps3recomp/tools/check.sh` over 3000 frames: interpreter and translation the same in attract mode and in
  made-up play, the translation the same twice, 3,640,553 dispatches compared block by block with 0 different.
- Rollback (`--rollback-test 3`) and run-ahead 1 and 2: the same hashes as a plain run over 4000 frames.
- `check_online.sh`: two copies over a lossy link the same over 3000 frames, and the same as the controls replayed.
- Straight to the opening on and off: the same hashes over 3000 frames.
- Each cheat and free play in a run (the health, clock and credits read from RAM).
- `games/shared/sf3/checktraining.sh sfiii2n ROM`: 21 judgements of scripted training sessions, all passing (the session,
  health and gauge refills, start over in both corners, the stage, the dummy's stances, guard, record and replay, quick
  stand, stun mashing, throw escape and computer, the combo, input and stun readouts, frame data and its log, the parries' kinds and
  rates). A standing light punch out of reach measured 5/2/5 (11 frames) every time, the frames player 1 was not free for.
- With every training feature off the game runs as before the training sources were added: `--auto 7` over 4000 frames
  gives the same hashes (with the default features, and without free play and the straight start).

Not checked by a person: nobody has played it in a window yet. Training mode's pictures were read from headless runs only; the hit boxes are
those of the frame before (the picture a frame shows is of the fighters as the frame before left them: drawn from the frame's own
records an attack's box showed a frame before the limb was out; seen frame by frame, a standing heavy punch's box is now on the arm for
its three frames). Not tried: a super art whose gauge is a timer, other characters than Ryu and Ken. The dummy jumping
parried nothing in the air (24 hits taken, its taps read as too early twice): the air window is read, but its parry was not seen.
