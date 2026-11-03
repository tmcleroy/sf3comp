# Changelog

## Unreleased

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
