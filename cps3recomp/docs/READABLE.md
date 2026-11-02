# Readable rewrites and hooks

A game's routines can be rewritten by hand as ordinary C++, and hooked (C++ run before the game's own code at an
address). The mechanism is satrecomp's (`runtime/lift.h`), on cps3recomp's dispatcher.

## The pieces

- `runtime/lift.h`: `Lift` (a routine's name, address, the size and hash of its code, the C++ function, whether it
  is a rewrite, the registers it may leave as it likes) and `AddHook` (a function run at an address, after which the
  game's code runs). A game's sources register them from their `RegisterGame` (`RegisterLifts`, `AddHook`).
- `Guest`: what the C++ sees of the CPU: its registers, T, reads and writes through the CPU's own bus (so writes to
  hardware are hardware writes), `Call` (a routine of the game's, run at once: for a rewrite of a routine that calls
  others not rewritten yet), `Return` (as rts, with the time the routine is said to take).
- The dispatcher asks the lifts and hooks at an address before any code there, in both `--cpu native` and
  `--cpu free` (so the two still run the same); translated code's calls to such an address come back to the
  dispatcher first. `--cpu interp` has no dispatcher and runs none.
- A lift runs only where the code is the code it was written from: the FNV-1a hash of `size` bytes from its address
  (`tools/lifthash.py DIR/program.bin 6000000 ADDRESS SIZE`, the program from `sfiii3-headless ROM --frames 1 --dump DIR`).

## Writing one

1. Read the routine (`tools/sh2dis.py DIR/program.bin 6000000 ADDRESS COUNT`), and its literal pool: read the pool's
   values at run time as the routine does (`Guest::Read32`), rather than writing them in.
2. Leave every register as the routine does, or name the ones no caller reads (`tools/liveout.py
   DIR/program.bin ADDRESS`: callers are found in the block database, `work/<game>/blocks.txt`) in `Lift::free`.
3. Register it with `rewrite = true` and check it: `CPS3RECOMP_LIFT_CHECK=1 <game> --rom ROM --headless --frames N
   --auto SEED:600` runs the rewrite and the game's own code for every call, from the same machine (the whole of it
   is put back between them), and compares registers (but the free ones), main RAM (but the stack below the
   routine's), the CPU's cache RAM and the writes to hardware in order. It reports at the end:
   `rewrite NAME: calls, compared, different, not comparable`. A rewrite is done at 0 different over several seeds,
   with its rare paths reached.

`CPS3RECOMP_NO_REWRITES=1` runs the game's own code where there are rewrites; `CPS3RECOMP_NO_LIFTS=1` leaves out
lifts and hooks altogether.

A lift or hook keeps nothing of its own between calls (or only for messages, as a debugging aid): what it carries
goes in the game's save state, so that rollback and run-ahead run it as the first time.

## In 3rd Strike

`games/sfiii3/src/lifts.cpp`: `NextRandom` (0611E0EE, the game's dice: the next of 64 numbers from a table),
`ClockTick` (060D7BAE, the round's clock, once a frame), `ClockFlash` (060D7CB8, its flashing under 30 seconds),
and the damage formula: `BaseDamage` (0609E36C, the attack's damage by the board's level and the handicaps),
`ScaleDamage` (0611E27C, guts and the combo's scaling) and `TakeDamage` (06124BC4, the health and the stun gauge,
knocked out or stunned), rewritten and checked; a hook on the vertical blank's interrupt (06000528) as an example. A rewrite's calls to the
game's routines (`Guest::Call`) reach the other rewrites, except while it is being checked (then the game's code runs
for all of them): `SFIII3_LIFTS_ONLY=NAME,...` registers only those named, to check one that another calls.
