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

## Read the decompilations first

Byte-matching decompilations of all three CPS-3 Street Fighter III programs exist, of the same sets this project
translates, by gibletto (AGPL-3.0): [3rd Strike](https://github.com/gibletto/sfIII3-cps3-decomp) (`sfiii3nr1`),
[2nd Impact](https://github.com/gibletto/sfIII2-cps3-decomp) (`sfiii2n`) and
[New Generation](https://github.com/gibletto/sfIII-cps3-decomp) (`sfiiina`, the same program as `sfiiin`). Nearly
every routine is C (in 3rd Strike all but 53 of 10,120), under Capcom's own file and routine names where the PS2
release's debug symbols gave them. They make a rewrite much cheaper: read the routine there before its SH-2 code.

Their licence is not this project's, and their source is written to compile back to the arcade's bytes, not to be
changed. So the rule is the one already kept for the PS2 decompilation (crowded-street/3s-decomp): **names and
understanding, no code**. Nothing of theirs is copied into this repository or committed (not their tables either:
`tools/names.py` fetches those to `work/`). A rewrite here is still written against the routine itself and is done
only when the check above says 0 different.

How they help:

- **Finding the routine.** `tools/names.py fetch` once, then `names.py lookup 061195E0` (`Player_move,
  src/PLMAIN.c`), `names.py lookup hit_stop` (every routine so named), `GAME=sfiiin names.py lookup bgst040` (a
  stage's routines in New Generation). Each `functions.tsv` gives address, size, name and file for every routine,
  so a routine's size for `Lift` and `lifthash.py` is there too, and the translated code and the checks' logs carry
  the names (docs/VERIFYING.md).
- **Reading it.** Open the named file in their tree at the commit `game.cfg` pins (`names_source`). Each file starts
  with what it does; `docs/HOW_THE_GAME_WORKS.md` in the 3rd Strike tree is a tour of the frame loop, tasks, input,
  drawing lists, moves, hit boxes and damage. Their `include/structs.h` and `work.h` name the records' fields:
  use them to name ours in `sfiii3.h` (a field's offset still comes from the routine's own code).
- **Where things are** (3rd Strike's files): the computer player in `Com_Pl.c`, `Com_Sub.c`, `Ck_Pass.c`,
  `PASS00`-`pass19`, `ACTIVE00`-`active20`; a fighter's frame in `PLMAIN.c`, `PLCNT*.c`, `PLS0*.c`; animations and
  boxes in `CHARMOVE.c`, `HITCHECK.c`; commands in `CMD_MAIN.c`; effects (projectiles, stage objects) in `EFFxx.c`;
  stages in `bg*.c`, `TATE00.c`; drawing lists and the video DMAs in `spr_list.c`, `dma_que.c`, `cram_bank.c`; the
  interrupts and task switch in `intr.src`, `tasksw.src`. The two older games' stage code, which the guest stages
  run, is their `bgstNNN.c`.
- **The other two games against 3rd Strike.** The 2nd Impact and New Generation trees were named from 3rd Strike's
  and say in comments where they differ: the quickest way to a guest stage's routine from its 3rd Strike
  counterpart.
- **What a rewrite must keep.** The same author's PC port
  ([sfiii3-cps3-port](https://github.com/gibletto/sfiii3-cps3-port)) compiles that C for a PC and had to mark every
  place where the C alone does not behave as the board does (its `HOST_PORT`, `BOARD_READ` and `NULL_READ` sites).
  The translated code keeps all of these by itself; a rewrite does not, so look the routine up there before
  rewriting it:
  - reads through a null pointer (address 0 is the BIOS, and the game uses what it reads: effects with no box
    tables in `CHARMOVE.c`, the bonus stage's car in `PLS02.c`, `spgauge.c`);
  - reads past a table's end into the next table (`PL_Blow_Off_Data` into `Hit_Range_Data`, `Power_Lv_Data`,
    `atsagct` into `exdm_ix_data`): read the ROM where the routine reads it, never a copy of the table;
  - a routine that returns nothing its caller then reads (whatever was left in r0), or reads arguments its caller
    never passed (stale r6, r7): in `Com_Sub.c`, `Ck_Pass.c`, `Com_Pl.c`, `entry_2.c`; `se_3.c` passes the stack
    pointer as an object;
  - a division by zero gives 0.
- **Sound.** [cps3-vst](https://github.com/gibletto/cps3-vst) documents the sound driver's data for `sfiii3nr1`:
  the bank, sample and sequence tables, the sequence's commands and the tempo and pitch arithmetic.
- **Stages, to compare with.** [3sx](https://github.com/crowded-street/3sx) (the PS2 program's port, AGPL-3.0) brings
  New Generation's stages in by converting them from the arcade set; its `docs/stages.md` lists each stage's
  scroll limits and widths.

What they do not give: the RAM address of a variable (their build's link map would; it needs their Windows
toolchain), and any check against the board's timing.

## In 3rd Strike

`games/sfiii3/src/lifts.cpp`: `NextRandom` (0611E0EE, the game's dice: the next of 64 numbers from a table),
`ClockTick` (060D7BAE, the round's clock, once a frame), `ClockFlash` (060D7CB8, its flashing under 30 seconds),
and the damage formula: `BaseDamage` (0609E36C, the attack's damage by the board's level and the handicaps),
`ScaleDamage` (0611E27C, guts and the combo's scaling) and `TakeDamage` (06124BC4, the health and the stun gauge,
knocked out or stunned), rewritten and checked; a hook on the vertical blank's interrupt (06000528) as an example. A rewrite's calls to the
game's routines (`Guest::Call`) reach the other rewrites, except while it is being checked (then the game's code runs
for all of them): `SFIII3_LIFTS_ONLY=NAME,...` registers only those named, to check one that another calls.
