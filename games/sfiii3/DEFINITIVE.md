# The definitive version: 3rd Strike with New Generation's and 2nd Impact's stages and music

## The way: two boards, one picture

3rd Strike plays the fight on its own board, as it is. Beside it a second board runs New Generation or 2nd Impact in a
fight on the stage wanted, unseen and unheard but for its stage: its camera follows 3rd Strike's, and the picture is
its stage with 3rd Strike's fighters, effects and HUD drawn over it. The same way cps2recomp's battles put fighters over
a stage another machine shows (fight/battle.h there).

Proved with `sfiii3-headless --stage-cfg games/sfiii2n/game.cfg --stage-rom sfiii2n.zip` (an experiment in
port/headless.cpp): Ken and Oro fought 3rd Strike's fight on 2nd Impact's harbour stage, its camera following.

## Facts it stands on

- The three games share their engine's layout. The camera: 3rd Strike 0x02026CB0 (copies 0x02026CA8, 0x02026CC0),
  2nd Impact 0x020221D4 (copies 0x020221CC, 0x020221E4); the ends it keeps it between at +0x44 (right, twice) and
  +0x48 (left, twice): 3rd Strike 0x02026CF4/CF8, 2nd Impact 0x02022218/221C. Other layers' cameras follow (blocks
  nearby).
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

- New Generation's: the camera 0x0201BE84 (copies 0x0201BE7C, 0x0201BE94), its left edge 0x0201BE6E (copy
  0x0201BEF0); its stage's ends are not kept beside it as in the later two (the words there are ±240 and 208): taken
  as 3rd Strike's (272-748) for now. The clock 0x0200EB33 (copy 0x0201DB15, 54 frames a second), the players' health
  0x0200D22B and 0x0200D603 (copies at +2, and the HUD's 0x0201DAF1 and 0x0201DB09; the records 0x0200D18C and
  0x0200D564, 0x3D8 apart). Its stage number: 0x02012CA3 (as 2nd Impact's:
  copies at +0x0F and next to the clock at +0x11): 4 London, 8 the cliffs, 12 a snowy street seen.
- Both seen working: 3rd Strike's fight on New Generation's London stage, and on 2nd Impact's street stage chosen by
  its number (`--stage-number 4`).

- In the game: the Display page's "Stages (experiment)" (games/sfiii3/src/stages.cpp, runtime/system.h's stage
  board): every stage of New Generation (numbers 0-12) and 2nd Impact (0-14; 15 repeats 14), named for what they show
  (whose each is: to name properly), from the
  sets beside the player's 3rd Strike set. Seen in pictures only.

- 3rd Strike's sound, so far (for its music to give way to the stage game's): the driver is at 0x0613B000-0x0613BD00
  (the writes to the sound chip: 0613BC66, 0613BBA8/BBBA, 0613B44A and others). The game's code calls into
  0x06138000-0x0613FFFF at 06138BA0 (r4 0x0E, 0x1B, 0x0C, 0x14...), 0613A182 (r4 0x40C-0x419: effects or voices?),
  061387DE (r4 0-3, r5 a pointer into 02070B70.. or 0: four at once) and 0613A9B0 (EEPROM addresses: not sound).
  Skipping any one of the four changes nothing in attract mode's music (67 key-ons either way): the music is started
  another way. The driver's writer at 0613BC66 works on a voice's record (r14: pitch at +24/+28, fine tuning at
  +95/+96) and the chip's voice registers (r13): who owns each record is the next thing to find. A debugging aid
  probes them (games/sfiii3/src/sound_probe.cpp: SFIII3_SOUND_PROBE, SFIII3_SOUND_SKIP).

- The stage board's camera: its game makes its camera from its fighters every frame (writing its camera words does
  nothing), so its fighters, unseen, are put where 3rd Strike's are (their place across the stage, mapped from one
  stage's ends to the other's), and its camera follows them with its own smoothing. In widescreen 2nd Impact's ends
  (which its game keeps) are narrowed by the margin, so its picture fills the wide screen; New Generation's stage
  fills only a little more than its 384 columns (its tilemaps are filled ahead of its camera, it seems): a black strip
  at the right in widescreen. New Generation's ends are beside its camera as 2nd Impact's are (camera +0x44 and +0x48: 0x0201BEC8, 0x0201BECC); on the rainy London stage they read 240 and -240 and its camera stays at 752, so that stage takes 320-680 and keeps a black strip at the right in widescreen.

- The music: 3rd Strike's sound driver keeps a record for each of its music's 16 voices (0x02078D0C on, 0x74 each)
  and for each of 8 sound effects' (0x020797EC on), which take voices 8-15 from the music while they play (found by
  hooking its note-pitch routine 0x0613B1D8, called as a voice is set: the record in r14, the voice's registers in r13).
  A hook there keeps which voices are music; in a fight on another game's stage the mixer leaves those out
  (Machine::silenced: they play on unheard, so nothing of the game changes) and the stage board's sound is mixed in (its
  fighters idle and its round held, it is nearly all music). Not listened to yet: 11-15 voices left out as music in a
  fight, the stage board's sound at healthy levels. With stages on, the rollback check still passes (354 rollbacks).

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
5. The menu: stages by game and by name (today one long choice on the Display page).
6. Check the stage rule on every stage of NG and 2I (stage objects with a fighter's palette, effects). 3rd Strike's
   shadows over another stage darken it now (they were black). New Generation's rainy London stage: its camera
   does not follow (a black strip at the right in widescreen).
