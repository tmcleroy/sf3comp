# Going native: the video and sound interface

The default build models the CPS-3 board: the game's code writes the video chip's registers and memories and the
sound chip's voices, and `machine/` works out what the chips would do (the object-list DMA that flattens the lists,
the character DMA that decompresses graphics, the palette DMA that fades colours, the interrupts) for `renderer.cpp`
and `pcm.cpp` to draw and mix. The aim: the game's code hands a native interface what it means (objects, tilemap
lines, scrolls, palettes, the text layer, voices) and the port draws and mixes that directly, with no chip model in
the default build. The board model stays as the reference, and checks run the two side by side until every frame and
every sample agree.

This file is the toolkit's half: the interface, how it is checked, how the renderer and the mixer consume it, what
cannot become events. 3rd Strike's half (its routines, addresses, read-backs, timing, the plan and its sizes) is
`games/sfiii3/NATIVE.md`. The model for the checks is cps2recomp's `sound/driver.cpp`, the QSound driver as native
code checked write for write against the Z80 (`--sound check`).

## Two kinds of thing the game writes

The census (`CPS3RECOMP_HW_CENSUS`, below) splits the chips' surface in two, and the plan follows the split.

**Commands**: things the chip acts on: the object-list DMA (a write sequence to 040C0082), the character DMA (its
list's address and a start bit), the palette DMA (eight words and a start bit), the registers latched once a frame at
the vertical blank (the scrolls, the four tilemaps' words, the screen's timing and zoom, the text layer's scroll and
palette base), the key-on word. These become calls on the native interface with what they mean: a palette transfer,
a list of objects, a frame's layer settings. The game writes none of them back as data, but it **reads the DMA status
word** (040C000C) to choose between starting a transfer and queuing it, and spins on it in a few places; and its
**DMA-done interrupt** (level 10) starts the next queued transfer. Both stay, as a timer of the native interface's own
(below), not a chip.

**Memories the game reads back**: these cannot become events; they stay plain memory the game owns, and the native
renderer reads them as data:

- the object words of sprite RAM (04002000-0407FFFF): objects are moved and edited in place (read, changed, written);
- the tilemaps (in sprite RAM too) and their line-scroll tables;
- SS RAM, the text layer's map and its 8x8 characters: text is drawn by reading characters and the map and writing
  them back (nibble by nibble for the characters);
- character RAM is written by the CPU only to build the character DMA's list (and to clear it); what the DMA brings
  in is never read by the CPU, so it can become a native texture store;
- the sound chip's voice registers and its key-on word: the driver reads them back (the key-on word is changed by
  read, mask, write; the voices' registers are read to step them on).

So the native interface is "events for commands, shared memory for the rest": a frame is the memories as they stand
at the vertical blank plus the frame's commands, exactly what the board's chips see, but described in the game's
terms and consumed by a renderer that never emulates a DMA.

## The interface (video/native.h)

Per frame, as the vertical blank takes it (the board takes the object list at the vertical blank and draws the whole
frame from what is there then: the census found no register written outside lines 0-45 of the frame, so there is no
raster effect to carry; only colours can change while the picture is out, see the game's file):

```
struct Producer {            // one game's board: 3rd Strike, or a guest stage's (New Generation, 2nd Impact)
    Palette palette;         // 0x20000 colours of 15 bits + the blend bit, kept from frame to frame
    Textures textures;       // what the character DMA brought in: 8 MB of 8-bit pixels, 16x16 tiles (shared, by page)
    const SpriteRam *sprites;// the object words and tilemaps, as the game left them (shared memory, read only)
    const SsRam *text;       // the text layer's map and characters (shared memory)
};
struct Quad {                // an object's tile, ready for a GPU: one per 16x16 tile after the list's zoom
    int16 x, y, w, h;        // where, in the buffer's pixels (zoomed: w, h the drawn size)
    uint16 tile;             // 16x16 tile in Textures
    uint32 paletteBase;      // colour * granularity (64 or 256): the palette table's row
    uint8 flags;             // flip x, flip y, blend (shadow/highlight bits), 6/8 bits a pixel
    uint16 group, order;     // the main list's entry it came from (features pick by it), its place in the list
    uint8 producer;          // whose palette and textures
};
struct LayerLine {           // a line of a tilemap the object list places (an object with no width)
    uint8 tilemap; int16 bufferLine; int16 scrollX, scrollY; bool lineScroll, flipX, flipY;
    uint32 mapBase, lineBase; uint16 group, order; uint8 producer;
};
struct Frame {
    std::vector<Quad> quads; std::vector<LayerLine> lines;  // in the board's order (later over earlier)
    int width, height;       // the screen's timing registers' picture (384 or 496 x 224)
    uint8 zoomX, zoomY;      // the whole picture's zoom (0x40 none, up to 0x80)
    TextLayer text;          // scroll, palette base, which producer
    std::vector<Producer*> producers;
};
```

What the game hands over goes into this as it happens: `Palette::Transfer` (a palette DMA: source in the graphics
stream, destination, length, fade), `Palette::Set` (a colour the game writes), `Textures::Load` (a character DMA's
list: each transfer decompressed into the store, the 6-bit and 8-bit runs as the board has them), `ObjectLists`
(the list heads the game writes into sprite RAM's first 8 KB, taken at the vertical blank and flattened into quads
and layer lines with the lists' settings and the global scrolls), and the frame's latched registers
(`FrameRegisters`: the eight global scrolls, the four tilemaps' four words, the screen's timing and zoom, the text
layer's scroll and palette base). Each is the native counterpart of one of `machine.cpp`'s chip functions, written in
the interface's terms rather than the chip's registers.

### GPU rendering (the user's requirement)

SDL_GPU draws the frame from these structs directly; a software renderer that makes a picture to hand to the
presenter does not count. So the structs are what a GPU consumes:

- **Quads**: one instanced draw of the frame's quads, in list order, into an offscreen target of colour numbers
  (R32_UINT, up to 1024 x 448 for the picture's zoom, as renderer.cpp's buffer): the fragment shader reads the
  tile's texel (an R8 texture array of 16x16 tiles, 0 discarded) and writes `texel | paletteBase`; a blend quad ORs
  its bits in instead (an atomic or a second target: the shadow/highlight bits 13-16). Zoomed tiles are quads of
  their drawn size sampling with nearest texels, the board's rounding reproduced in the quad's size (renderer.cpp's
  "rounded up, so that zoomed tiles leave no gaps").
- **Layer lines**: one quad per line, its fragment shader reading the tilemap (a buffer of the 64 x 64 tile words)
  and the tile texture; the line scroll table read in the vertex stage. These interleave with the quads by `order`.
- **Palette tables**: each producer's 0x20000 colours as a texture (R16_UINT), updated by the frame's transfers (a
  frame changes a few hundred colours at most: uploaded as ranges).
- **Resolve**: a full-screen pass from colour numbers to colours through the palette (and the picture's zoom, and
  the text layer over it, sampling SS RAM's map and characters as two small buffers), into the presenter's texture.
  The presenter (port/present.cpp: shaders, scaling) takes it from there as it does now.

The CPU keeps a rasteriser of the same structs (`native::Rasterise`, the reference for the GPU and the one the checks
use: level (b) compares it with renderer.cpp, pixel for pixel; a GPU capture can be compared with it the same way).

### Guest stages through the same interface (the user's requirement)

The definitive version's stages run New Generation or 2nd Impact on a second board beside 3rd Strike today, and the
port draws two pictures and lays one into the other (DEFINITIVE.md). With the interface, the second game is a second
**producer**: its rewritten (or hooked) drawing code hands its own palette transfers, textures and object lists to its
own `Producer`, and its quads and layer lines go into 3rd Strike's frame with `producer` set:

- each producer has its own palette table and texture store, so the two games' colours never share colour RAM (no
  ranges to tell apart, no second picture, no blend);
- the frame's order puts the guest's stage layers under 3rd Strike's fighters by the rule stages.cpp now applies to
  objects (the guest's stage lines and stage objects first, then 3rd Strike's non-stage quads), as data rather than
  as two renders composed;
- the guest board keeps only the guest game's CPU, RAM and the plain memories it reads back (its sprite RAM's object
  words, its SS RAM, which it does not draw anyway); no video chip model, no renderer of its own;
- the camera and widescreen read the layers themselves (a layer line's scroll) instead of two cameras kept in step.

This needs the guest game's drawing hand-offs (its palette DMA request, its list-head writer, its character DMA,
its frame registers) found and rewritten for each guest game: the three games share the engine, so 3rd Strike's
routines are the map (games/sfiii3/NATIVE.md).

### Timing: what stays

The game's slowdown is its CPU's: the main loop does a frame's work and waits for the vertical blank's count to move
(arcade timing: the SH-2's cycles, its bus and its cache, docs/TIMING.md). Native video keeps that if:

- **the DMA durations stay**: the palette and character DMAs' status bits and their done interrupt 2500 cycles after
  the start, the object list's status bit for 100 cycles (machine.cpp's numbers): the game queues, waits and chains
  on them. In the native build this is a small timer in the interface (start, busy until, raise level 10), not a
  chip; the transfer's effect is applied at once, as machine.cpp does;
- **the bus is still charged** for what were hardware writes: with arcade timing each write to the video area costs
  its bus cycles (the video area has one wait). A rewrite that hands over instead of writing is charged what the
  writes would have cost, or arcade timing keeps the game's own code (as for every rewrite now: Game::rewritesNeeded);
- **the vertical blank and the DMA-done interrupt stay where they are**: they are the board's clock, kept by the
  frame loop (`Machine::RunFrame`) with or without a chip model.

## Sound

The voices are already close to native: `pcm.cpp` mixes from the sixteen voices' registers, and the CPS-3 has no
sound CPU (the driver is the SH-2's own code, run at the end of the vertical blank, 0x0613A408 in 3rd Strike). The
native form: the driver's writes to a voice become **voice events with timestamps** (key on, key off, start, loop,
end, pitch step, volume left and right, each with the frame position of the write, as `Pcm::BeforeWrite` uses it
now), the mixer consumes the event list per frame; the voice registers and key-on word stay readable memory because
the driver reads them back (they are its own state, in effect). Level (c) compares the mixer's samples with pcm.cpp's
from the same frame.

## The checks

Three levels, each run with the board model beside the native interface in the same run (`CPS3RECOMP_NATIVE_CHECK=1`:
the board keeps the interface beside its own memories, `Machine::nativeCheck`), the gates the existing checks are
(all the same over attract mode, a one-player game, a versus game, a training session):

- **(a) the interface**: what the board's memories and registers say, decoded into the interface's terms, equals
  what the rewrites handed over, every frame. For the palette (done: `games/sfiii3/checknative.sh`): the board's
  palette DMAs as its registers had them at each start and the colours the game's code wrote to colour RAM, in
  order, against the rewrites' transfers and colours; and the whole of colour RAM against the native palette. Next:
  the object list (the flattened list the board's DMA makes against the quads the native lists make), the frame's
  registers, the textures (character RAM against the store), the text layer's settings.
- **(b) pixels**: the native rasteriser's picture from the interface equals renderer.cpp's from the board, every
  frame (and the GPU's capture equals the rasteriser's, on the machines that have one).
- **(c) samples**: the native mixer's output from the voice events equals pcm.cpp's from the registers, every frame.

Each level also runs with the game's own code in place of the rewrites (CHECKNATIVE_NO_REWRITES=1), to see that the
check finds the difference; and the rewrites are checked against the game's code as all rewrites are
(`CPS3RECOMP_LIFT_CHECK`: registers, RAM, the hardware writes in order). The two checks are separate runs: the lift
check puts the machine back between the rewrite and the game's code, which takes back what the rewrite handed over.

The boot ROM (the cartridge's BIOS: memory test, the region warning) writes a few colours itself before the game
starts; the check passes those on to the native palette as they are written, since they are not the game's code. A
native boot would set them itself.

## The census

`CPS3RECOMP_HW_CENSUS=FILE` (with `CPS3RECOMP_NO_BLOCKS=1`, so that PC is the instruction's) counts every access to
the chips, the controls and the interrupts by instruction: read or write, PC, PR, the register or memory, the
addresses met, the access sizes, the count, and the lines of the frame it happened on. What the game reads back, when
in the frame it writes, and which routines are the hand-offs all come from it (games/sfiii3/NATIVE.md). The census and
its listings are made from the game and are never committed.

## Removing the board model

When levels (a)-(c) hold for every piece: the default build runs the interface alone (the chips' functions in
machine.cpp out of the path, the plain memories kept), `renderer.cpp` and `pcm.cpp` kept for `--board` runs and the
checks. Save states then hold the interface's state (palettes, textures by page as character RAM is now, the timers)
instead of the chips'. Until then the interface is kept only in check runs (`native::On()`), and is in save states
only then.
