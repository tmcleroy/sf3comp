# Changelog

## 0.1.0-beta.2

- Guest stages (New Generation's and 2nd Impact's): ROUND 1 and FIGHT! shown over them (they were hidden), the
  opponent's stage there from the round's first frame with "load ahead" off (3rd Strike's own stage showed first), no
  camera pan at a round's start. Tag team battle: the incoming fighter's score is shown at once (it read 00 until the
  next points).
- Readable code: the computer player (how it chooses a plan, answers, guards, parries, gets up, and plays its plans),
  the stage's camera and the versus stage choice, and the fight's score, clock, round end and hit test are written
  again as C++, each checked against the game's own code on every call (checklifts.sh).
- Bonus games (Alternative modes): Crush the Car or Parry the Ball, straight from the menu with the character chosen at
  character select; the menu again when it is over.
- The alternative modes made as the CPS-2 ports have them. Ring out: a fighter knocked about at an open end goes over
  it and falls, and the winner jumps down onto the next stage, the camera following it. Endless battle: the stages
  scroll by with no break (or fade, as a choice). Tag team battle (so named; tag the default; rested fighters keep
  their health). Attract mode with free play on. The stage viewer with only music. The model viewer straight to its
  character, with Reset options and the animation's name at the top. The colour editor's panel: any character, set,
  line and colour, random colours, a library of saved sets.
- The zoom on a fighter works (it had nothing to follow). Custom colours are always on. The endless battle's scroll
  never stops at a stage's end; the model viewer keeps its character in place with nothing else drawn; a mode that is
  over opens the menu on its page instead of starting the board again.
- The menu in the CPS-2 ports' order and words (Alpha 2's, row by row). Endless battle's shuffle fades one stage into
  the next. In a ring out the computer plays the mode: out of its own corner, pushing you to yours. Training's hit
  boxes in widescreen were drawn 64 columns left of the fighters.
- Arcade timing (the Game page, off unless chosen): the processor takes the board's time, its memory's waits, its
  cache's misses and its pipeline's stalls, so the game slows down where the board does. Checked against jotego's
  CPS-3 core simulated: the start-up, the attract mode and a fight, frame by frame.

## 0.1.0-beta.1

The first version: 3rd Strike (sfiii3nr1, Japan 990512) as translated code on the port's own CPS-3 machine.

- The launcher (game page, Online page, features, controllers) and the in-game menu, save states, free play, the
  CPS-2 ports' display options and shaders, widescreen (or 16:9 played as on the board, with invisible walls).
- Training mode: hit boxes (projectiles too), input display, frame meter, frame data and its log, damage, stun and
  parry readouts; a dummy that guards, parries, escapes throws, quick-stands, mashes out of stun, records and replays,
  or is played by the game; a stage choice; start over in a corner.
  The dummy's While guarding: a red parry.
- Alternative modes: attract mode (everything, the intro only, or the fights only), a stage viewer (any stage, scrolled
  with the stick or by itself, any tune; Infinite: every stage in turn), survival, endless battle (shuffled or
  scrolling stages), ring out, team battle (knock-out or tag: tag in with the left trigger, a jump-in attack, the
  knocked-out lying a while, the team's gauge carried over, a time over to the team with more health; co-op and 2 vs
  2; CPU difficulty and stage), a model viewer (every animation, the stage's front pieces in front or behind), the
  colour editor and custom colours; play as Gill; full gauge cheats.
- Readable rewrites of the round's clock and the damage formula, checked call by call against the game's code.
- Online play with rollback (a code, the lobby, the waiting room).
- A custom soundtrack (by song or as a playlist), and any stage's music for a fight.
- The definitive version's stages: New Generation's and 2nd Impact's, one chosen or each opponent's own, with their
  music (their sets beside 3rd Strike's).
