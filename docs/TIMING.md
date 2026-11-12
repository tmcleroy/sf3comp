# The board's timing

The CPS-3's SH-2 does not run every instruction in one cycle: its memory takes time, its cache misses, and its
pipeline stalls. The port's **arcade timing** (the Game page's "Arcade timing", on by default)
charges the processor that time, so the game does as much work in a frame as the board's processor does. With it
off every access is instant, as the port has always run it, and the game gets through about twice the board's work
in a frame.

## What is charged

The rules are the SH7604's, as [jtcores](https://github.com/jotego/jtcores)' SH7604 model (jotego's CPS-3 core,
GPL-3.0-or-later, its CPU from the Saturn core) applies them. No code of it is in this repository; what it does is.

- **The bus** (`machine/bus.h`). An external access is bus cycles of 2 states and its area's waits, as the game sets
  up the bus state controller at start-up (3rd Strike: one wait for main RAM, the video and the SIMMs; area 0, the
  BIOS, a long wait); a 16-bit area takes two bus cycles for a long. The processor's time is worked out as the
  model's is: when the bus and the cache are next free, a read waiting for all of its bus cycles, a write holding
  the pipeline only until its bus cycle begins, fetches asked for ahead of their instructions.
- **The cache** (the same file). 64 entries of 4 ways of 16 bytes, its pseudo-LRU, a read miss filling the line a
  long at a time; writes go through to the bus. Its tags are part of the save state.
- **The pipeline** (`machine/sh2_pipeline.h`). One cycle when the instruction before accessed memory and this one's
  fetch falls due at the same time, or when the one before loaded a register this one reads. What each kind of
  instruction reads and loads is the model's own decoder's, dumped for every opcode (`tools/timing/`).

The interpreter charges these as it runs; the translated code charges the same in its memory and fetch macros
(`runtime/block_native.h`), so the two still run alike cycle for cycle (`tools/check.sh` with
`CPS3RECOMP_BUS_TIMING=1`). Idle and countdown loops are run pass by pass with it on, not counted at once.

The port's own code takes none of the board's time: a hook or a lift is charged nothing, and with arcade timing the
game's own code runs in place of the readable rewrites, except in a training session or a mode of the port's own
(`Game::rewritesNeeded`), where the rewrites are charged what they say.

## How it was checked

The reference is the CPS-3 core simulated with Verilator (jtframe's `jtsim`), with a few local changes kept out of
this repository: every write the SH-2 makes to main RAM logged, so that main RAM can be rebuilt at each vertical
blank; counts of its cycles held by the bus, split by the pipeline, and held by the FPGA's own memory; and, while
the FPGA's memory holds the CPU up (which a board's memory would not), the board's video time held too. The port
logs main RAM at each vertical blank the same way (`CPS3RECOMP_VBLANK_LOG`; `CPS3RECOMP_VBLANK_DUMP` writes it out
at a frame, `CPS3RECOMP_POKE` sets a word at a frame's start to keep the two in step).

From power-on, through the board's start-up and the attract mode:

| | the port reaches the core's frame 400 at its | the game's own spare-time count (attract mode) |
| --- | --- | --- |
| Arcade timing off | frame 386 (14 frames sooner) | 848 |
| The bus and the cache | frame 400 | 535 |
| ... and the pipeline (per access) | frame 400 | 503 (the core: 504) |
| ... when the bus and cache are free (now) | frame 401 | **505** (the core: 504) |

At its frame 400 the port's main RAM is the core's but for the stacks and a few values the start-up measures. The
per-access model's total came out right with its single accesses wrong; the present one also matches the core on a
bench of 30 small loops (each kind of access, and writes, reads and fetches against each other) to a tenth of a
cycle a pass, all but the BIOS's memory-test loop (a write and a read of the same address), which the port runs
about a sixth faster. The port's boot loses a frame against the core's in its loading screens.

A fight (the core run with a cabinet script, `fight.py` in the harness: a coin, Start, the first character, then
player 1 walking in and attacking on a fixed pattern against the computer; the port replaying the same as a movie).
The two stay in step only while the game's dice agree: on the frame the coin goes in the game rolls them until the
frame ends (37 times on the core, 39 on the port), and from then on the computer plays differently. With the dice set
to the core's after that frame, the fight ran in step for some 520 frames, until another roll that the timing
decides, and its spare-time count was within about one of the core's each frame:

| | spare-time count, a fight frame on average (the core: 315.3) |
| --- | --- |
| Arcade timing off | 716 (two different fights) |
| Arcade timing on | **316.2** |

The port's boot reaches the core's character select two frames later than the core.

## What it does not do

- **The reference is a model of the board, not a board.** Where the core differs from a CPS-3, so may the port. Its
  memory times are the SH-2's programmed waits; the board's own chips' answer times beyond them are not known.
- **The board's DMAs** (the character, palette and sprite lists) take no bus time from the processor here.
- **The on-chip peripherals** answer at once.
- The board's own timing-decided values (the dice rolled until a frame ends, the start-up's measurements) come out as
  the core's only while the two agree to the cycle; they do not stay so.
