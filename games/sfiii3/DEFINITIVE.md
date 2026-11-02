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

## To do

1. The stage board held in a fight on the stage chosen: its stage select (or the stage variable at a match's start),
   no clock, nobody knocked out, its fighters out of the way (they still move its camera's walls).
2. Its music in place of 3rd Strike's, 3rd Strike's sound effects kept: which of the sound chip's voices play music
   (or the game's music request, to stop 3rd Strike's).
3. The second board translated too (its own generated code: today it runs in the interpreter, about half the speed).
4. The second board in the save state, for rollback and online play; widescreen on both.
5. The menu: a stage choice per game (3rd Strike's own, New Generation's, 2nd Impact's).
6. Check the stage rule on every stage of NG and 2I (stage objects with a fighter's palette, effects).
