# Translating and verifying the SH-2 code

The game's SH-2 code (the cartridge's ROM at 0x00000000 and the program SIMMs at 0x06000000, deciphered) is
translated ahead of time into C++ (`tools/recomp.py`, `tools/sh2emit.py`) and run by the dispatcher in
`runtime/recomp.cpp`, satrecomp's, for one CPU and the CPS-3's memory. Code the build has no translation for, and
code in the CPU's cache RAM (0xC0000000, which the board deciphers as it is fetched), runs in the interpreter.

```sh
cps3recomp/tools/translate.sh ROMSET [FRAMES] [SEEDS]   # record, generate, build (work/<game>/: never committed)
cps3recomp/tools/check.sh ROMSET [FRAMES] [CHECK_FRAMES] # compare the translation with the interpreter
```

`translate.sh` records the blocks the game runs (`--cpu free --record-blocks`) in attract mode and in made-up play
(`tools/madeup_input.py SEED FRAMES`: coins, Start, a character, then random presses), adds the blocks found by
following their code through the ROMs, writes `work/<game>/gen/blocks_*.cpp` and builds. Running it again adds to
`work/<game>/blocks.txt`.

`tools/names.py fetch` downloads the table of a game's routines' names that its `game.cfg` names (`names_source`) to
`work/<game>/functions.tsv`. With it there, the translated blocks carry their routine's name in a comment, the checks
print each address with its routine (`check: block 0611962C (Player_move+0x4C) differs`), `names.py lookup` answers
for one address or name, and `names.py annotate` does the same for any log piped through it. Without it nothing
changes.

## The CPU modes (`--cpu`)

| Mode | What runs the SH-2 code |
| --- | --- |
| `native` | the translation (default); the interpreter for code the build lacks |
| `free` | the interpreter, block by block as the translation runs: the exact reference for `native` |
| `interp` | the interpreter alone, an instruction at a time (no dispatcher) |

Free running: a block runs to its end whatever the time slice (a function of several blocks on to its next backward
branch), and every instruction costs a fixed number of cycles by its kind (machine/sh2.cpp's table, which the
generator reads from `machine/sh2_ops.h`). `native` and `free` leave blocks at the same points and count the same
cycles, so they must produce the same machine; `interp` ends time slices at other instructions and so differs from
both in when the board's events (the DMAs' ends, the vertical blank) fall.

## The checks (`check.sh`)

1. Attract mode and made-up play, each with `--cpu free` and `--cpu native`: main RAM and the picture after every
   frame (`--hash`) must be the same.
2. The same play twice with the translation: the same again.
3. Block by block (`--check N`: every Nth dispatch): the interpreter runs the block from a saved state, then the
   translation from the same state, and the registers, the cycles, the interrupt and delay-slot flags, the call
   frames, main RAM and the cache RAM they leave are compared. A dispatch that reached the board's hardware (anything
   but main RAM, the ROMs and the cache RAM: `g_hardware`) cannot be run twice and is counted as not comparable.

## When they differ

- `--check 1` names the block and the first register or byte that differs (translation/interpreter).
- `CPS3RECOMP_NO_FUNCTIONS=1` (each block on its own), `CPS3RECOMP_NO_CALLS=1` (calls through the dispatcher) and
  `CPS3RECOMP_NO_BLOCKS=1` (no translation at all) narrow it down.
- `--dump DIR` writes the deciphered program (`bios.bin`, `program.bin`) for `tools/sh2dis.py FILE BASE START`; the
  generated function of a block is in `work/<game>/gen/` under its address.
