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
- The object list says which objects are the stage: its main list's groups. Tilemap lines (an object with no width)
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
  as 3rd Strike's (272-748) for now. The clock 0x0200EB33 (copy 0x0201DB15, 54 frames a second), player 1's health
  0x0200D603 (copies 0x0200D605, 0x0201DB09; the record 0x0200D564). Player 2's and the stage number: to find.
- Both seen working: 3rd Strike's fight on New Generation's London stage, and on 2nd Impact's street stage chosen by
  its number (`--stage-number 4`).

## To do

1. The stage board on the stage chosen (2nd Impact: its stage byte, above; New Generation's to find), its way to a
   fight worked unseen (coins, Start, a character) and held there (done, above). Its fighters stand still, out of the picture's way (they still set its camera's walls).
2. Its music in place of 3rd Strike's, 3rd Strike's sound effects kept: which of the sound chip's voices play music
   (or the game's music request, to stop 3rd Strike's).
3. The second board translated too (its own generated code: today it runs in the interpreter, about half the speed).
4. The second board in the save state, for rollback and online play; widescreen on both.
5. The menu: a stage choice per game (3rd Strike's own, New Generation's, 2nd Impact's).
6. Check the stage rule on every stage of NG and 2I (stage objects with a fighter's palette, effects).
