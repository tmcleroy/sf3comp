# Street Fighter III 3rd Strike, the 990608 revision (sfiii3n)

The board's later release, as a game of its own in the collection. The definitive version (games/sfiii3) is made from
990512 (sfiii3nr1) and stays so: its sources name that program's routines address by address.

## What the two programs are to each other

Of the set's files only the program's first SIMM differs. The program was built again: 2.5 million of its words
differ from 990512's, nearly all of them addresses (every routine is somewhere else), in 684 stretches from the
first kilobyte on. The memory is laid out the same: of the 1,533 places in memory that a routine of 990512 and the
same routine here both name, 1,527 are the same address (the six others are tables that moved with the code).

So what reads and writes the game's memory carries over unchanged, and what names code does not. This game has the
shared sources (games/shared/sf3: free play, the straight start, the board's settings, cheats, training mode,
widescreen), with 3rd Strike's own facts (`src/facts.cpp`), and the port's own features (online play, replays, save
states, run-ahead). It has none of the definitive version's sources: no alternative modes, no stages of the other
games, no model viewer, no native sound (the board's mixer is heard), no arcade-timing rewrites.

## To bring it the definitive version's sources (not done)

Every routine of 990512 would need its place here: the two programs compared instruction by instruction with
addresses left out gives it for the routines that are the same, and names the ones the revision changed (whose
rewrites would then be 990512's behaviour, and must stay off). A plain comparison of bytes does not: the routines'
literal pools and their branches to each other move with them (tried: 434 of 9,892 routines matched that way).

## Checks

- Translated code: 16,600 blocks; a run of made-up play (`--auto 7`, 4000 frames) ran 99.67% of its block runs
  translated.
- Rollback (`--rollback-test 3`) and run-ahead 2: the same hashes as a plain run over 4000 frames.
- The shared training check (`games/shared/sf3/checktraining.sh sfiii3n`, the cursors' own characters: Alex against
  Alex): the session's clock, health refill, start over in both corners, the stage chosen, the dummy's stances, guard
  (21 of 22 guarded), record and replay, quick stand (60 frames down for 70), stun mashing, the computer's play, the
  input display, the stun readout, frame data and its log, and the dummy's parries (85 of 86 at 100%, 37 of 78 at 50%,
  none at 0%) all pass: the facts carried over hold. Four of its judgements fail on the script, which was written for
  the older games' Ryu: Alex's crouching light kicks do not combo, its motion makes no super art of his (the gauge
  itself read full on every frame), too little damage was done to try a knock-out, and the feature list has the
  throw escape, which the check expects only of 2nd Impact. Not checked: the throw escape itself, a super art made.
