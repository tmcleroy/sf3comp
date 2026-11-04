# The definitive version: 3rd Strike with New Generation's and 2nd Impact's stages and music

## The way: two boards, one picture

3rd Strike plays the fight on its own board, as it is. Beside it a second board runs New Generation or 2nd Impact in a
fight on the stage wanted, unseen and unheard but for its stage: its camera follows 3rd Strike's, and the picture is
its stage with 3rd Strike's fighters, effects and HUD drawn over it. The same way cps2recomp's battles put fighters over
a stage another machine shows (fight/battle.h there).

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
