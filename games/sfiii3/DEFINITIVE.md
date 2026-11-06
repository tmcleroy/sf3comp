# The definitive version: 3rd Strike with New Generation's and 2nd Impact's stages and music

## The way: the guest's stage code alone, one frame (2026-11-05)

3rd Strike plays the fight on its own board, as it is. Beside it the stage board holds New Generation's or 2nd Impact's
program and RAM, but no game of theirs is played on it: no mode, character select, fighters or match. It is started
once, with no controls, and kept where its game task runs the game's modes each frame (the title's and attract mode's
routine and the game's, by the task's first step: New Generation's 0x0602B454 and 0x0602B9BE, 2nd Impact's 0x06084C5C
and 0x06085298), which a lift replaces (src/stages.cpp, `StageAlone`, `AloneFrame`). The first time it gets there (its
start-up done, about 400 of its frames, run 16 a shown frame) it is kept, and every stage of that game starts from there:

- **Built**, as the match's set-up builds it: the screen set up afresh (New Generation's 0x0602A69E, 2nd Impact's
  0x06083B8E: the DMA queue, character RAM's areas, the object lists and layers; it waits for the object DMA and a frame,
  so it is jumped to, with its way back to the lift kept, rather than called), then the stage's number at its head's
  +4 and +5 (where the stage's choice puts it) and the game's stage byte, the match's set-up's clearing (four words and a
  byte), the effects' lists emptied, the stage's state cleared and its first step (New Generation: 0x0609104C,
  0x0607C054, 0x06079CB8, 0x06079FF0; 2nd Impact: 0x060C072C, 0x060A2438, 0x060A0158, 0x060A0480). New Generation's
  match's set-up also asks for colours (0x0608BC5E, r4 1), and so does this.
- **Each frame** only the stage's routines, those of the match's frame that are the stage's: the stage's frame (its
  head's step: 0 its set-up, the layers, the textures and colours asked for; then its frame, the stage's own routine by
  number), the effects' two queues and seven lists (the stage's people, waterfalls and things are effects: New
  Generation 0x0608EF88 and 0x06090F34, 2nd Impact 0x060BE46C and 0x060C0614). Then the game task goes on as after its
  modes: the objects' lists flipped, its wait for the frame.
- **The frame's hand-off** is the board's vertical blank, as the game's own: its video hand-offs run dry into the stage
  board's own native interface (video_games.cpp), its DMA-done interrupt chains the textures and colours the stage asked
  for, its sound driver plays. The interrupts are kept: on the CPS-3 the frame's hand-offs and the sound driver are the
  vertical blank's own code (there is no sound CPU), and they are the game's.
- **The camera in** (stages.cpp's `Give`, 2026-11-06): the words the stage's camera code reads, from 3rd Strike's camera
  as its stage is shown (its scripted place in a super art's or a round's scripted move) mapped to the stage: its
  fighters' places for the camera code (+0x60 across and +0x62 up, scr_mv_x and scr_mv_y, which a fighter's own frame
  makes from its place, +0x64 and +0x68: with no fighter running they were left at 0, and the camera made for the
  stage's left end every frame, put back each frame), its camera held when 3rd Strike's is (bg_stop, the stage's state
  at +0x18 of what the match's set-up clears), and the stage's shakes (its head's +0x2A, +0x2C). Its camera is left
  where its last frame left it and its own camera code takes it to where it is sent: its fighters are put one in each
  outer quarter of its picture with their middle where it is to go (scr_11_22), so that the step its code takes (cut to
  80 in 100) is the step wanted; only when it is further than 24 columns from there (a round's start, a stage built) is
  it put there at once, and then its far layers with it. So where it was (old_pos_x), where it is and how far it moved
  are its own each frame, as in a match: its stage's code brings in the stage's columns as its camera passes them by
  those (put where it was sent each frame, it brought in the wrong ones: black parts of the stage), and its far layers
  go by how far it moved (they scrolled by themselves). Up: its fighters given the height that makes its camera code's
  place up 3rd Strike's (it eases there a few lines behind in a fast jump). In its own entrance (its frame not whole,
  +0x2E/+0x30 not 64, or its camera held for its entrance: New Generation's Gill stage, which zooms out from a close-up
  and pans down from the sky, and some of 2nd Impact's) nothing is given: given 3rd Strike's camera through it (a stage
  chosen mid-fight), its entrance put its zoom whole as it began and then drew it on out past whole, the stage small and
  black at the right and bottom for the rest of the fight.
- **The music**: the game's own routine for the stage's tune, called once its stage's frame runs (New Generation's
  0x0602940C, by the head's stage number from a table; 2nd Impact's 0x06082848, r4 the stage's number): the same tune
  the round's management asks for in a match (New Generation's Ken stage: the same song data, 0x0638DC9A, both ways).
  The board's sound is made as it was: its driver writes its sound chip, the stage board's pcm.cpp mixes it, and that is
  added to 3rd Strike's in a fight on the stage with 3rd Strike's music's voices left out (music.cpp's MusicVoice: a
  voice is the music's unless the driver's effect channel of that number is playing; until 2026-11-06 it was told by a
  hook on the note's pitch routine from registers the rewritten channel step no longer sets, so no voice was ever left
  out, and 3rd Strike's music played on under every guest stage's: two soundtracks at once) (Machine::stageSound,
  board.silenced). What is different is only what the driver is asked for: the stage's tune and nothing else (no
  announcer, no fighters' voices or effects of a hidden match). Not changed: the guest's sound on the native mixer
  (native_sound.h) would need New Generation's and 2nd Impact's drivers' rewrites or hooks of their own (their request
  side is not 3rd Strike's: 0x063CD87C, 0x060F9D18), so the stage board's sound stays pcm.cpp's.

**One frame.** The stage board's stage layers (its objects that `StageWords` says are its stage, its tilemaps' lines,
with its own palette and textures) are a second producer's tiles in 3rd Strike's frame (video/raster.h, `Frame::guest`,
`AddGuest`), drawn by one rasteriser (`Rasteriser::DrawWithGuest`) and on the GPU by the same passes (port/native_gpu.cpp:
the guest's buffer in rows 448 on, its textures the second 8 MB, its palette and 3rd Strike's blends' scales after 3rd
Strike's), under 3rd Strike's layers by the rule the two pictures were composed by: where 3rd Strike drew nothing the
stage shows, and 3rd Strike's shadows (a blend over nothing of its own) darken the stage's colour as they do 3rd Strike's.
No picture is handed between the two boards; in a window the GPU draws frames with a stage too (Machine::DrawWithStage,
KeepGpuGuest). The modes' own 3rd Strike stages behind their fighters (battle.cpp: a 3rd Strike stage board, a second
one sliding or fading in, darkened) are still composed as two pictures.

**Before** (until 2026-11-05): the stage board played a hidden match on the chosen stage, coins, Start and a character on
its way there (8 of its frames a frame), its clock and health held, its fighters made players' so that they stood. That
way is kept behind SFIII3_STAGE_MATCH=1, for the check below.

**The checks against ground truth** (2026-11-06, checkguests.sh, part of checkstages.sh): the check below compared the
stage alone with the match's stage given everything the match hands it, the fighters' records whole among it, producer
against producer: the stage alone never had to make its camera from what a fight gives it, so the play-test's bugs (black
parts, far layers scrolling by themselves, no camera up, New Generation's Gill stage drawn small, 3rd Strike's music
under the stage's) were all out of its reach. Now SFIII3_STAGE_COMPARE=play: the stage game's own match on the stage,
played (made-up play on its board: 600 frames idle, then 600 walking, jumping and attacking, by turns; its own camera), is
the ground truth, and the stage alone is given only what `Give` gives it in a fight, from the match's own camera and
fighters, and the noise (dice, frame count, hit stop, the fighters' records but their places). All 28 stages, 4:3 and
widescreen, 3466-3644 frames each: 15 stages the same in every frame but one (the fight's second or third); New
Generation's Gill, Alex, Ryu, Elena and Sean and 2nd Impact's Gill, Hugo, Elena and Akuma differ in 66-134 frames of
the round's start (the match's camera held and moved by its own intro); both Necro stages' far layer behind the window
some columns off (65 frames; 2nd Impact's every frame); 2nd Impact's Yang stage's far tilemap now and then 2 columns
behind. On main before (638cb12) every stage differed in 2000-3600 frames. And SFIII3_STAGE_CHECK=1 in play: one-player
games with the stage chosen mid-fight through every stage of each game, the other game's, none and one again, and the
opponent's stages; each frame its camera where it was sent (0.6 of a column off on average or less, and as far up as sent), its zoom whole when its
frame is, no music of 3rd Strike's heard with the stage's, no stage board sound left playing at a stage's build. With
the GPU, checknative.sh's window twin over all 28 stages chosen in turn: the GPU's picture as a window shows it the CPU's,
every frame (also on main: none of the play-test's bugs was the GPU's).

**The check** (SFIII3_STAGE_COMPARE=1): the stage board plays the match as before (the reference, shown), and before the
frame its match is set up a second board is made as it is then, and makes the stage alone from there; each frame it is
given what the match hands the match's stage (taken at the stage's frame routine: the fighters' records, the camera held
and its reasons, the scripted moves' speeds and the zoom's asks, the head's stops, scripted moves and shakes, the dice's
counters and the hit stop's and frame count's words: `StageAlone::matchHands`), and the two stages are compared each
frame: their stage layers drawn alone (pixels), their stage objects (the three words drawn, not the tilemaps' line
objects nor the blank filler objects whose places the fighters take) and tilemap registers, and 3rd Strike's frame
composed over each. Both boards run one frame a frame. Results (2026-11-05, 5000 frames each, made-up play seed 7 and a
widescreen versus game seed 31, the camera given across most of each stage):

| | stages | stage layers different | composed picture different |
| --- | --- | --- | --- |
| New Generation, seed 7 | all 13 | 0 of 3466-3546 frames each | 0 |
| New Generation, seed 31, widescreen | all 13 | 0 | 0 |
| 2nd Impact, seed 7 | 12 of 15 | 0 of 3644 each | 0 |
| | Ryu's (2), Dudley's (4) | 1 frame each (the fight's second) | 1 |
| | Yang's (10) | 127 frames | 126 |
| 2nd Impact, seed 31, widescreen | 12 of 15 | 0 | 0 |
| | Ryu's, Dudley's | 1 frame each | 1 |
| | Yang's | 480 frames | 480 |

The objects of both Necro stages differ in their tile numbers only (the match's set-up takes character RAM's areas in
another order: the textures 0x60 tiles further on), the pixels the same. 2nd Impact's Yang stage: its far tilemap is
2 columns behind the match's now and then (its scroll register 0x140 against 0x142), the rest of the stage the same.
Not found yet; ruled out: the fighters' records, the dice, the hit stop, the camera held and zoom's asks, the frame's
shake and offsets, all of RAM below the stage's head and the low RAM (handed whole: no change), a match frame that runs
long (none) or late (its stage's frame runs at 0.10-0.12 of the frame).

Proved with `sfiii3-headless --stage-cfg games/sfiii2n/game.cfg --stage-rom sfiii2n.zip` (an experiment in
port/headless.cpp): Ken and Oro fought 3rd Strike's fight on 2nd Impact's harbour stage, its camera following.

## Facts it stands on

- The three games share their engine's layout: a stage's head (0x54 bytes) and seven layers of 0x90 after it
  (stage_camera.cpp), the camera the fighters' layer's place (layer 1, speed 1.0): 3rd Strike 0x02026CB0 (copies
  0x02026CA8, 0x02026CC0; head 0x02026BAC), 2nd Impact 0x02022264 (copies 0x0202225C, 0x02022274; head 0x02022160),
  New Generation 0x0201BF14 (copies 0x0201BF0C, 0x0201BF24; head 0x0201BE10); the ends it keeps it between at +0x44
  (right, twice) and +0x48 (left, twice): 3rd Strike 0x02026CF4/CF8, 2nd Impact 0x020222A8/22AC, New Generation
  0x0201BF58/BF5C; its place up at +0x04 (shown: -0x04), its top at +0x4E. Some stages' fighters' layers have their middle at 0
  (New Generation's rainy London and Ryu stages, 2nd Impact's Ryu stage: ends -240 and 240, -202 and 203). (Until
  2026-11-04 the stage board's camera was taken for layer 0's place, a far layer that moves by a sixteenth to three
  quarters of it: its camera then followed 3rd Strike's only roughly, not at all on 2nd Impact's Elena stage, and its
  fighters seemed to stand 512 off on the stages above.)
- Which objects are which (games/sfiii3/src/stages.cpp): on 3rd Strike's board its stage is its tilemaps and its
  objects with palettes 0x80 and up; on the stage board its fighters are the objects of 6 bits a pixel with a palette
  below 0x40 (New Generation 0x08 and 0x10, 2nd Impact 0x00 and 0x18) and their shadows (palette 0, blending), and
  everything else is its stage (its townspeople have palettes from 0x40). Its HUD is the text layer, not drawn. Seen on
  New Generation's London and snowy street and 2nd Impact's street and market.
- (Earlier rule, kept for the record:) The object list says which objects are the stage: its main list's groups. Tilemap lines (an object with no width)
  and objects with palettes 0x80 and up are the stage; the fighters' palettes are below (0x08, 0x10 in 3rd Strike,
  0x00, 0x18 in 2nd Impact), their shadows palette 0 with blending. Seen on one stage of each game: to check on all.
- The board keeps each object's group (Machine::SpriteGroups); the renderer draws a chosen part (drawGroup) and can
  draw over another picture (overlay).

- 2nd Impact's own places: the clock 0x02010167 (copy 0x02023EF5; a second is 54 frames), the players' records
  0x0200E504 and 0x0200E910 (health at +0x9F as in 3rd Strike, with a copy at +0xA1, and the HUD's at 0x02023ED1 and
  0x02023EE9). Holding the clock at 99 and both healths at 160, with no input once it fights, keeps 2nd Impact's
  board on its stage while 3rd Strike plays round after round (seen over 9000 frames).
- 2nd Impact's stage: the byte 0x020142E7, held before the fight starts, chooses it (6 Hugo's market, 5 and 4 two
  other stages seen; held at 6 through character select, a fight that would be on the street was on the market).
  Copies of the number the game makes from it: 0x02010178, 0x0200EC69, 0x020142F6, 0x020155D1, 0x02022164,
  0x020221AB (setting those alone does not choose it).

- New Generation's: the camera above; its stage's ends beside it as in the later two. The clock 0x0200EB33 (copy 0x0201DB15, 54 frames a second), the players' health
  0x0200D22B and 0x0200D603 (copies at +2, and the HUD's 0x0201DAF1 and 0x0201DB09; the records 0x0200D18C and
  0x0200D564, 0x3D8 apart). Its stage number: 0x02012CA3 (as 2nd Impact's:
  copies at +0x0F and next to the clock at +0x11): numbered as 3rd Strike numbers its characters (below).
- Both seen working: 3rd Strike's fight on New Generation's London stage, and on 2nd Impact's street stage chosen by
  its number (`--stage-number 4`).

- In the game: the Display page's "Stages (experiment)" (games/sfiii3/src/stages.cpp, runtime/system.h's stage
  board): every stage of New Generation (numbers 0-12) and 2nd Impact (0-14; 15 repeats 14), named by whose stage each is
  (below), from the
  sets beside the player's 3rd Strike set. Seen in pictures only.

- 3rd Strike's sound, so far (for its music to give way to the stage game's): the driver is at 0x0613B000-0x0613BD00
  (the writes to the sound chip: 0613BC66, 0613BBA8/BBBA, 0613B44A and others). The game's code calls into
  0x06138000-0x0613FFFF at 06138BA0 (r4 0x0E, 0x1B, 0x0C, 0x14...), 0613A182 (r4 0x40C-0x419: effects or voices?),
  061387DE (r4 0-3, r5 a pointer into 02070B70.. or 0: four at once) and 0613A9B0 (EEPROM addresses: not sound).
  Skipping any one of the four changes nothing in attract mode's music (67 key-ons either way): the music is started
  another way. The driver's writer at 0613BC66 works on a voice's record (r14: pitch at +24/+28, fine tuning at
  +95/+96) and the chip's voice registers (r13): who owns each record is the next thing to find. A debugging aid
  probes them (games/sfiii3/src/sound_probe.cpp: SFIII3_SOUND_PROBE, SFIII3_SOUND_SKIP).

- The stage board's camera: its camera words are given 3rd Strike's place mapped from one stage's ends to the
  other's, and its fighters, unseen, are put where 3rd Strike's are (mapped the same way), so its game's own camera
  step leaves it about there (over all 28 stages in a one-player game in widescreen: within 0-0.5 columns on average,
  1.3-1.7 on 2nd Impact's four widest, Alex's, Dudley's, Necro's and Ken's). Both Necro stages were 6 off, their
  cameras held off the stage as the fight began and taken for an offset of their fighters' coordinates (as New
  Generation's rainy London stage's 512): one is taken now only with the fighters' middle outside the stage's ends. On a
  stage wider than 3rd Strike's the fighters keep 3rd Strike's distances from the camera (mapped from end to end, they
  stood past the quarters its camera keeps them in, and it went after them). In widescreen the ends its game keeps are
  narrowed by the margin, so its picture fills the wide screen (both games). Up: its fighters raised so that its
  camera goes up as 3rd Strike's (the higher one's height past 88 times 1.75, as in 3rd Strike), and its layers'
  places up given too, for the frames its game holds its camera (its own round's start).

- The music: 3rd Strike's sound driver keeps a record for each of its music's 16 voices (0x02078D0C on, 0x74 each)
  and for each of 8 sound effects' (0x020797EC on), which take voices 8-15 from the music while they play (found by
  hooking its note-pitch routine 0x0613B1D8, called as a voice is set: the record in r14, the voice's registers in r13).
  A hook there keeps which voices are music; in a fight on another game's stage the mixer leaves those out
  (Machine::silenced: they play on unheard, so nothing of the game changes) and the stage board's sound is mixed in (its
  fighters idle and its round held, it is nearly all music). Not listened to yet: 11-15 voices left out as music in a
  fight, the stage board's sound at healthy levels. With stages on, the rollback check still passes (354 rollbacks).

- Whose stage is whose: New Generation and 2nd Impact number their stages as 3rd Strike numbers its characters (Gill
  0, Alex 1, Ryu 2, Yun 3, Dudley 4, Necro 5, Hugo 6, Ibuki 7, Elena 8, Oro 9, Yang 10, Ken 11, Sean 12, Urien 13,
  Akuma 14), found in their own one-player games (made-up play: the stage byte against the opponent's name over each
  fight). New Generation has 0-12 (its 6, Hugo's number though it has no Hugo, is a beer festival, taken as Hugo's), 2nd Impact 0-14. The choices are named so,
  and "the opponent's" puts each 3rd Strike fight on that game's stage of the character whose stage 3rd Strike chose
  (0x020154F5): between fights the stage board is reset and taken to a fight on that stage (four of its frames a
  frame, there in time for the round's start in made-up play); for the characters that game has not, 3rd Strike's own.
  The picture and the stage's music are composed through the whole match (the round's start, fight, knock-out and
  end: the match's state 1-8), not only while it is fought.

## To do

1. The stage board on the stage chosen, its way to a fight worked unseen and held there: done (every stage of both
   games in the choice; four of its frames a frame on its way, so it is there when 3rd Strike's fight starts).
   2026-11-05: replaced by the stage alone ("The way"): a stage is built from the kept start-up in two of its frames,
   so the opponent's comes at once and "load ahead" (stages_preload) is passed over for these games.
2. Its music in place of 3rd Strike's: done; to listen to, and the stage board's own effects (its round's announcer
   before 3rd Strike's fight starts is not heard: its sound is only mixed in during a fight). A custom soundtrack's
   file, when one plays, is heard in place of both.
3. The second board translated: done (tools/translate.sh with GAME=sfiiin and sfiii2n; linked into 3rd Strike's
   program when generated; the interpreter otherwise, or with CPS3RECOMP_NO_STAGE_BLOCKS=1).
4. The second board in the save state (it is scenery today: a rollback redraws with it as it is then; the game's
   state hash is the same over rollbacks, the picture's not always).
5. The menu: stages by game (today one long choice on the Display page, named by whose stage it is).
6. Check the stage rule on every stage of NG and 2I (stage objects with a fighter's palette, effects): all 28 seen
   composed in one picture each (made-up play, frame 4500): the stage whole, its own fighters gone, 3rd Strike's
   fighters, shadows and HUD over it, in widescreen too. Effects and moving stage pieces over time not looked at.  Some stages'
   cameras stop short of their ends words (2nd Impact's Ken, Sean and Urien stages): those ends are learned as the
   camera stays put short of where it was sent, and the fighters' middle is kept inside them, so no strip shows past a
   stage's end. The stage games' fighters are made players' (record byte 3) so that a computer opponent does not fight
   on behind 3rd Strike's. 3rd Strike's
   shadows over another stage darken it now (they were black).
7. Every stage of both games looked at through a one-player game's first fight (made-up play, frames from its round's
   start to 4800, the stage chosen and the opponent's; 2026-11-03). Found and put right: "ROUND 1" and "FIGHT!"
   (palettes 0x1ED and 0x1E0) were taken for 3rd Strike's stage and left out (its stages' objects use 0x80-0xFC and
   0x1FF, every one of the 21 in an endless battle); the opponent's stage without "load ahead" came some 200 frames
   into the round's start, 3rd Strike's own shown first (the board's trip from its reset, ~1600 of its frames: now from
   its start-up passed, kept once a game); before a round 3rd Strike's camera ends are not its stage's (1 and 95), and
   the stage board's camera went to its right end, panned back over the round's start and learned that place as its
   end (now held at its middle between rounds, ends learned only in a round).
8. 2026-11-04: the camera's layer put right (above: it was a far layer's), so the stage board's camera follows 3rd
   Strike's across on every stage; up too (Oro's entrance from the sky on his stage, jumps); an end learned where its
   camera was held (it stayed put when sent either way: 2nd Impact's Elena stage cut to 510-513 for the match) is
   forgotten. 2nd Impact's Elena stage's sea, bright with the moon's glints as a round starts and darker later, is 2nd
   Impact's own (its own game, made-up play, the same: frames 2300 and 3200); half of it had lost its glints for good,
   its camera held (that was ours). Looked at too (made-up play, pictures and the sound log): a versus game on a
   chosen stage, training sessions (the computer the dummy, and watched), the training's stage viewer (in widescreen
   too), widescreen on all 28 (no strip at either side), a one-player game's second round, knock-outs, the round's and
   the match's ends (the score's tally over the stage, as 3rd Strike's own has it) and the next fight's stage, the
   stage's music in fights and training. Not looked at: the continue screen (not a stage: 3rd Strike's own screen,
   nothing composed), a third round.
9. 2026-11-04, later: the stage board's camera against where it was sent, every frame of a one-player game's first
   fight on each of the 28 (made-up play, seed 7, widescreen, frames 2000-4800 of the board), mean columns off, before
   and after: New Generation's Gill 1.64 to 0.44, Necro 5.77 to 0.49, Ken 2.73 to 0.49; 2nd Impact's Alex 2.35 to
   1.38, Necro 5.77 to 1.68, Ken 2.96 to 1.46; the other 22 the same as before (0-0.5; 2nd Impact's Dudley 1.32). The
   pictures (frame 3600, both Necro stages) show the stage some 5 columns further left of the fighters before.
10. Wanted (the user, 2026-11-04: "this is huge"): the guest stage through the native video interface
   (docs/NATIVE.md). Today the stage board's picture is drawn by a second board model and laid into 3rd Strike's.
   Once both games hand their frames to the native interface, the guest stage's layers, scrolls and palettes go
   into 3rd Strike's frame as layers of their own: no second picture to blend, no colour ranges to tell apart, the
   camera and widescreen taken from the layers themselves, and the second board kept only as the guest game's code
   and RAM (no video chip model for it). The interface is to be designed with this in mind from the start.
   2026-11-05, the producer's side done: both stage games hand their frames to the stage board's own interface
   (their hand-off routines are 3rd Strike's, run dry on it: games/sfiii3/NATIVE.md, "Guest stages"), and the
   picture made from the two interfaces' layers is the board models' (checknative.sh's stage runs).
   2026-11-05, later: done. The stage from the stage games' stage code alone (no match), and drawn with 3rd Strike's
   layers as one frame by one renderer, the CPU's and the GPU's ("The way", above). Still the board's on the stage
   board: its sound (pcm.cpp), its frame's interrupts and DMA timing (the game's own code, as on 3rd Strike's board).
