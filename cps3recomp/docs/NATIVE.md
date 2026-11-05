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

**As built (2026-11-04)**: rather than instanced quads (a blend object ORs bits into what is under it, which a
fixed-function blend of integer colour numbers cannot do), the GPU draws with two compute passes (port/native_gpu.cpp,
port/shaders/native_*.comp): the frame's quads and tilemap lines are first made into tiles on the CPU (video/raster.cpp
BuildFrame: each a clipped 16x16 tile with its 16.16 steps, the work items both rasterisers take), binned by buffer row;
pass 1 gives each pixel of the colour-number buffer the tiles over its row in the board's order (later over earlier,
blends ORed), pass 2 resolves through the palette with the zoom and puts the text layer over it. The textures go up by
64 KB page, only those changed (Textures::PageVersion). The same integer steps as the CPU's, so the pixels are the same:
checknative.sh reads the GPU's picture back every frame (no window) and compares.

A hazard found doing it: with the compute passes and the copy pass that reads the picture back in one command buffer,
Metal's copy now and then read the picture before the compute pass had finished writing it (about one frame in 9000;
in one frame in two when the uploads went in a command buffer of their own). The frame then read back as the one
before. Submitting the drawing and waiting for it before the read-back cured it (0 different in 15 runs of 3000 frames
under load; CPS3RECOMP_NATIVE_GPU_ONE_BUFFER=1 puts it back for looking into it). That wait is the check's alone
(NativeGpu::DrawAndRead, now only with CHECKNATIVE_GPU_READ=1).

### The presenter's GPU path: the default (2026-11-05)

Where the window's picture goes through SDL's GPU API and no shader preset (the default on every platform with a GPU
device), the picture of a frame drawn the plain way is drawn by the GPU from the interface, and the CPU's renderer is not
run for it.

**Off for a while, and why it is back on (2026-11-05).** The Mac mini restarted three times that day (its userspace
watchdog: no check-ins from WindowServer, which was stuck in the GPU driver), and main's 2231b7f turned the GPU path off
by default (CPS3RECOMP_NATIVE_PRESENT=1 to opt in). The cause was the queue, not the passes: headless and offscreen runs
(checknative's GPU runs, the bench, `--present-test`) submitted a command buffer a frame and never waited on one, so with
no swapchain to hold them back they drew far faster than the GPU and piled up unfinished work, up to 33 command buffers
deep, until WindowServer got no GPU time. The fix (`Presenter::Submit`, as cps2recomp does): offscreen, every frame is
submitted with a fence and waited for at once, so exactly one frame is ever on the GPU; in a window the swapchain bounds
it (SDL's acquire hands out no texture while both frames in flight are unfinished, vsync or not, and such a frame draws
nothing: SDL 3.4.16's Metal and Vulkan acquire read). With it, every shader index is kept inside its buffer or texture
(the tile and row counts passed in the params, the textures' 8 MB and sizes, the overlay a column and a row inside the
composed texture), and the checks' and the bench's GPU devices are let go of before the program ends (`EndNativeGpu`),
not from static destructors at exit (the Deck's Vulkan driver crashed destroying one there). Checked on the Steam Deck
under Vulkan validation (no errors from our shaders) and on a MacBook Air (three GPU runs: the pixels the same, at most
one frame in flight, WindowServer responsive); the mini runs CPU-only checks. So the GPU path is on by default again:
in a window through SDL's GPU API, unless `--board`, `--renderer sdl` or CPS3RECOMP_NO_NATIVE_PRESENT=1 (the native
rasteriser on the CPU draws the picture otherwise, and headless).

Switches for looking into it: CPS3RECOMP_GPU_DEBUG=1 (SDL's GPU debug mode: Vulkan's validation layers, Metal's
checks), CPS3RECOMP_GPU_QUEUE_STATS=1 (the presenter's pictures submitted, how many by the GPU from the interface, the
most in flight, the waits, said at the end), CPS3RECOMP_NATIVE_GPU_TIMING=1 (each pass, uploads, tiles, resolve and
compose, submitted alone and waited for: its time on the GPU, the mean and the most), CPS3RECOMP_TWIN_REPEAT=N (the
window's twin shows each frame N times: a program drawing far faster than the GPU, for the queue's depth).

- **The interface is kept** (`native::Keep`, before the first machine: on unless the port's `--board`, since the CPU's
  rasteriser draws every picture from it too, below; CPS3RECOMP_NATIVE=1 or 0 says so whatever). `Machine::nativeCheck` stays the checks'
  (`native::Checking`, CPS3RECOMP_NATIVE_CHECK). What is handed over is taken only for a frame the GPU may draw
  (`machine::Machine::NativeTaking`: `nativeWanted`, set from the port's request below; always in the checks): not for
  run-ahead's frames, the menu's, a stage board's or a headless run's, which so cost nothing more. The object DMA
  flattens the handed-over lists into the frame's objects as the check does.
- **In step with the board** (`machine::Machine::NativeInSync`): a frame is in step when the game's code handed over its
  lists' heads and its registers (the rewrites ran, or under arcade timing their dry runs; not with CPS3RECOMP_NO_REWRITES). A
  frame that hands over after one that did not, or after a state was put back, first makes the interface again from the
  board's memories (`SyncNative`: colour RAM, the character RAM pages whose stamps changed since the two last agreed, the
  latched registers, the lists' heads, the flattened objects).
  So save states are as they were (the interface is in them only in the checks), old states load, online peers with
  and without the GPU path exchange the same states, and run-ahead's and rollback's states copy nothing more.
  CPS3RECOMP_NATIVE_SYNC_CHECK=1 compares the interface with the board every frame it is in step: 0 different over
  attract mode, a one-player game, a versus game, a rollback test, run-ahead 2 and a state saved and loaded (6000
  frames each; and 3000 frames of a versus game under arcade timing, by its dry runs), and the state hashes the same
  as with the interface not kept.
- **The frame** (`Machine::gpuPicture`, set by the port each frame: the GPU presenter and the menu closed): drawn the plain way (no stage board, nothing left out, the panel
  and the backdrop the board's) and in step, it is made ready (`native::BuildFrame`, `Machine::GpuFrame`) and the CPU's
  picture is owed: drawn by renderer.cpp only if something asks for it (`Machine::Picture`: a screenshot, the menu's
  picture, the readout, a hash), from the same memories, before the next frame changes them (a frame not drawn and a
  state put back draw it first). Asked for to change it (the game's sources drawing on it), GpuFrame is none. What the
  GPU draws it from (the colours, the texture pages changed, SS RAM) is kept as it was at that frame's end
  (`Machine::GpuVideo`, `GpuText`), apart from the machine's: run-ahead and rollback put the machine back after the frame
  shown was run, and the frame shown is still drawn by the GPU.
- **Shown** (`Presenter::ShowNative`) for every frame made for the GPU, with what the port puts over the picture put
  over it on the GPU: the readout's lines and boxes, the lines at the bottom (stopped, fast forward, speed, hosting, the
  zoom's label, a screenshot's note), online's names and chat, the camera's zoom and the in-game menu. The port draws
  them as it always has, once over an all-black picture and once over an all-white one (`Port::ShownOver`; the menu's
  at its own size, `MenuOverlay`), and the two give each pixel its colour over black and how much of what is under it it
  covers (`Probe`: the overlay, `NativeGpu::Overlay`, sent again only when it changes). A third compute pass
  (shaders/native_compose.comp) puts it over the frame's picture, scaled as the shown picture is (the menu's three times
  as tall, 4:3), a column and a row more for the presenter's filtering at the edges; zoomed, the picture as it is above
  and the one with the overlay under it, as Show's zoom takes them. Translucent drawing comes out within one in 255 of
  the CPU's; elsewhere the same (CPS3RECOMP_PRESENT_COMPARE=1 with `--present-every N`: the window's twin, with no
  window, the GPU's shown picture against the CPU's, every Nth frame). Show(pixels) remains for frames not made for the
  GPU (a stage board's, the CPU renderer's features). ShowNative records, in the one command buffer Show would use and
  where Show submits it: NativeGpu::Draw (the uploads: tiles, rows, the texture pages changed, the colours only when they
  changed, `Palette::Version`, SS RAM, the overlay; the compute passes), then the same render pass Show uses, sampling
  the picture (NativeGpu::LastShown: the frame's picture, 1024 x 256, or the composed one, uv scaled by the part used,
  `Presenter::Draw`'s texture and picture sizes). No CPU wait in a window; offscreen each frame waited for (above).

**The race, for a render pass**: checknative.sh now checks the GPU's pixels as the presenter shows them
(CPS3RECOMP_NATIVE_GPU_PRESENT=1, the default there): ShowNative into a texture of the presenter's own (OpenOffscreen),
1:1 with the nearest pixel sampled, the compute passes and the render pass in one command buffer, then the texture read
back in a command buffer of its own after the first's fence. 0 different in 15 runs of 3000 frames of the widescreen
versus game (--auto 31:300:2, the set-up that showed the copy pass's race), five at a time under a load average near 200.
So the render pass is ordered after the compute passes it samples (SDL's Metal textures are hazard-tracked, and a render
encoder after a compute encoder in a command buffer waits for its writes); no further ordering was needed.

**Measured** (an M-series Mac mini under a load average of 150-200 from other work; CPS3RECOMP_NATIVE_BENCH=WxH: each
frame shown both ways into a texture of that size, as the window would, the order alternating; the GPU's time is from
the submit to the fence, the wait's own latency in it):

| | CPU: renderer.cpp + Show | CPU: BuildFrame + ShowNative | GPU after submit: Show | GPU after submit: ShowNative |
| --- | --- | --- | --- | --- |
| versus, widescreen, 1920 x 1080 | 0.795 + 0.232 = 1.027 ms | 0.097 + 0.405 = 0.502 ms | 1.54 ms | 1.92 ms |
| attract, 1920 x 1080 | 0.641 + 0.277 = 0.918 ms | 0.108 + 0.434 = 0.541 ms | 1.74 ms | 2.05 ms |
| versus, widescreen, 3840 x 2160 | 1.050 + 0.443 = 1.493 ms | 0.114 + 0.629 = 0.743 ms | 3.07 ms | 3.30 ms |
| attract, 3840 x 2160 | 0.646 + 0.269 = 0.915 ms | 0.038 + 0.328 = 0.366 ms | 1.89 ms | 2.10 ms |

(99th percentiles, CPU: 2.3 against 1.1 ms, 2.1 / 1.7, 21.2 / 3.8, 3.6 / 0.6.) Keeping the interface costs the CPU
about 0.01-0.03 ms a frame when it takes what is handed over (headless runs taking it every frame against
none, 6000 frames, CPU time: attract +0.025 ms, versus +0.01, the rollback test +0.03); not taken (run-ahead, the menu,
headless runs), nothing measurable (within 1-2% either way over three runs of each). So the CPU's part of a frame is about half; the GPU's is 0.2-0.4 ms longer (the two compute passes); a
frame's picture is done (submit plus the GPU's time, from the frame's end) at 2.42 against 2.57 ms (versus, 1080),
2.59 / 2.65 (attract), 4.04 / 4.56 (versus, 4K): no later than the CPU path's.

**Input to photon**: the controls are read, the frame is run, and the picture's one command buffer is submitted at the
same point of the frontend's loop either way, with the same swapchain call (no wait without vsync, the screen's with
it) and no CPU wait added: no frame of queueing is added, and the picture is done no later (above). Not measured on a
screen: nobody has seen the GPU path in a window (the window's code, RunWindowed's choice between ShowNative and Show,
was only built; every run here was windowless).

The HUD, the status lines, the menu and the zoom as a GPU layer: done (the compose pass, above). Frames that still fall
back to the CPU's picture: a stage board's, and the CPU renderer's features (objects left out or put in front, the ring
out's way down).

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

**As built (2026-11-05), the producer's side**: the stage board is a second producer. Its game's hand-off routines are
3rd Strike's, as the same engine has them: the same rewrites, written against a game's places, are made for each game
and attached to the stage board, run there only dry (lift.h: `Lift::boards`; attached only while the interface is kept
and only where the code is), so they hand the stage board's own `native::Video` its palette, textures, registers and
lists and change nothing of that game but which calls come back to the dispatcher. The stage board takes them for the
frames the game board takes its own (`nativeWanted`): in step for a GPU (`NativeInSync`) or made again from its
memories after its unseen frames (`SyncNative`). Checked as the game board's are: its own level (a) against its chips
(the check's totals count both boards), and its in-step check. The composition the stage path does is reproduced from
the two interfaces: `native::Rasteriser` draws a chosen part of a frame's objects (`KeepObject`: the stage board's stage,
this board's all but its own stage), with or without the text layer, and over another picture as renderer.cpp's overlay
does (a blend over nothing of its own scales the colour under it, from its palette's banks): the picture is the same as
the two board models' ("native pixels with a stage"). The GPU draws each producer's part (a frame of one producer: its
own palette and texture pages, a NativeGpu of its own) the same as the CPU; composing the two on the GPU (an overlay pass)
is the GPU side's to do. Still as before: the board model draws the stage frames shown (the presenter's GPU path is
for plain frames), the second stage board's slide and blend, the stage alone.

**As built (2026-11-05, later), one frame and the stage alone**: the guest's stage layers go into the game's frame as a
second producer's: `native::AddGuest` builds the guest's draw list (its objects a `KeepObject` says are its stage, its
tilemaps' lines, with the game's margin) and adds its tiles to the game's frame marked `Tile::producer` 1, with its
zoom (`Frame::guestScreen`) and its palette and textures (`Frame::guestVideo`). The rasteriser draws them into a buffer of
their own and resolves the two in one pass: the guest's colour number through its palette, then the game's over it by
the overlay's rule (where the game drew nothing the guest's shows; a blend of the game's over nothing of its own scales
the guest's colour by its bank's scales, `native::BlendScales`), the text layer the game's (`Rasteriser::DrawWithGuest`,
`DrawFrame` for a frame made before). The GPU the same: the tiles pass bins the guest's tiles into rows 448-895 of the
colour-number texture (1024 x 896 now), reading its textures from the second 8 MB of the textures buffer; the resolve
pass reads both rows, the guest's palette after the game's and the 48 scales after both. So no second picture is drawn
or handed between the two: `Machine::DrawWithStage` on the CPU, and frames with a stage made for the GPU in a window
(`Machine::KeepGpuGuest`: the guest's interface as it was at the frame's end; an owed CPU picture drawn from the kept
frame). Checked: "native pixels with a stage" (the one frame against the two board models' composition) and "native GPU
pixels with a stage" (the GPU's one frame against the CPU's) all the same; the window's twin (CPS3RECOMP_PRESENT_COMPARE=1)
the same. The guest itself is no longer a game played behind its stage: its stage code alone runs on its board
(games/sfiii3/DEFINITIVE.md, "The way"), as cps2recomp's `guest_stages::Guest::StageAlone` (fight/guest_stages.h there)
does for Vampire Savior, with these differences: the CPS-3 games' main loop is a task scheduler, so the routine
replaced is the game task's modes (the title's and the game's, by its first step), not a loop's top; the screen's set-up
waits for things, so it is jumped to (`Guest::Jump`, its way back on the stack) rather than called; and the guest's
picture is not composed with the game's but drawn in the game's frame.

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

### Arcade timing: hooks at the hand-offs (decided 2026-11-04, built 2026-11-05)

With arcade timing the game's own code runs in place of the rewrites (its cycles are the board's; a rewrite's are
only said), so nothing would be handed over. Neither exact timing nor the board model is given up for it: hooks sit at
the hand-off points (charged nothing, as every hook is: recomp.cpp's RunLifts), and each gives the interface what the
routine is about to write, the same data the rewrite hands over; the chip writes the game's code then makes land in
plain memory or a small latch, not in a video model. A hook runs only when its rewrite does not (a Guest query of the
dispatcher's rule: no rewrites, or arcade timing without Game::rewritesNeeded).

The way to write them once: each hand-off rewrite written against a memory it is given, the guest's (the rewrite) or
an overlay of it that keeps writes to itself and drops the chip writes (the hook's dry run: what the routine will do,
worked out before it runs, nothing changed). What the rewrites `Call` must then have a native form for the dry run: the
divide, the tilemaps' tile banks (0x0612E14C: per tilemap, three bits of the settings word set or cleared by the
masks at 0x06605548 against 0x0206A1DE, then 0x0206A1DE from 0x0206A1DC), the CPU's character bank (0x0613AA5E,
0x0613AAB0: two words of RAM), and the list area's allocation (0x061379EC, 0x06137980; or ClearListArea's hand-over
made after the routine, from its result). checknative.sh then runs its four games again with arcade timing on, and the
rewrites' and the hooks' hand-overs must agree with the board's as they do now.

**As built** (runtime/lift.h, recomp.cpp's RunLifts): no second copy of each rewrite. A rewrite that hands over is
marked in its game's table (`Lift::handsOver`); where the dispatcher's rule leaves it out, RunLifts runs the same
function dry before the game's code, with the bus's timing off (charged nothing), on a `Guest` given a `DryRun`: a copy
of the registers, and an overlay that keeps the bytes it writes (main RAM, the stack) and drops its writes to the
chips (04000000-05FFFFFF) and the CPU's registers; reads see the overlay over the guest's memory. `Guest::Call` in a dry
run runs the routine's native form (`AddNativeForm`, the game's: what the routine does to main RAM and r0) instead of
the game's code; a call with no native form is said once on stderr. What the dry run hands the board (`Board().Native*`)
is handed over; nothing else stays. The live path is unchanged, so the rewrites are checked against the game's code as
before (checklifts). Only when the interface is kept (`native::On()`); `CPS3RECOMP_NO_DRY_RUNS=1` leaves them out. The
run's count is said at the end ("hand-overs made by dry runs of their rewrites").

Where the game's code is run, nothing moves: the dry runs add no entry of their own (they are the rewrites' entries,
there already), and an entry does not start a block (the translation is made ahead of time; Lookup passes over
lift entries). What an entry does change is that a call to it is not linked (it comes back to the dispatcher, which
takes a pending interrupt there): so CPS3RECOMP_NO_DRY_RUNS=1 is the dispatch as it was before them, and the state
hashes with the arcade timing are the same with the dry runs, without them, and with the interface not kept (every
frame of 12000 of attract mode, three one-player games and a versus game). (cps2recomp's hooks did start a block
at their address, which moved an interrupt and parted a movie; there they became inline watches.)

What it reads of the chips: only the DMA status word (the timer that stays). So with the board model gone, the game's
own code's writes to the registers need only land in a latch (or nowhere) and its writes to colour RAM, character RAM
and the list heads in plain memory: the interface has had all of it from the dry runs.

A dry run says before the routine runs what it will do, from the state at its start. Two things could part them: an
interrupt taken inside the routine that changes what it reads (only AskPalette runs outside an interrupt handler and
is not masked: the DMA-done or the vertical blank's interrupt between its read of the status word and of the queue's
state), and the frame's end falling inside the routine (the interface then a routine ahead of the board at that
frame's compare: only in a frame the game overruns). Neither was met in checknative's runs (games/sfiii3/NATIVE.md).

## Sound

The voices are already close to native: `pcm.cpp` mixes from the sixteen voices' registers, and the CPS-3 has no
sound CPU (the driver is the SH-2's own code, run at the end of the vertical blank, 0x0613A408 in 3rd Strike). The
native form: the driver's writes to a voice become **voice events with timestamps** (key on, key off, start, loop,
end, pitch step, volume left and right, each with the frame position of the write, as `Pcm::BeforeWrite` uses it
now), the mixer consumes the event list per frame; the voice registers and key-on word stay readable memory because
the driver reads them back (they are its own state, in effect). Level (c) compares the mixer's samples with pcm.cpp's
from the same frame.

Done: `sound/native_sound.h` (the file's name keeps it apart from video/native.h on the include path). A
`native::SoundEvent` is one of kKeyOn, kKeyOff, kKeys (the voices in a mask, or the whole word), kSample (a voice's
start, loop and end as places in the sample stream, and whether it loops on), kLooping, kPitch (the step, 4.12 fixed
point), kVolume (left and right), kVoice (all of a voice: the boot ROM's writes passed on); `Machine::NativeSound`
stamps it with `FramePosition()` as it is handed over. `native::Mixer` keeps the sixteen voices (settings, place,
fraction) and the keys, and at the frame's end plays the frame's events at their times by pcm.cpp's rules (the same
frame lengths, the same step, loop and end, the same scale), all and voice by voice. With `nativeCheck`, pcm.cpp keeps
its mix voice by voice too and hands both to `Machine::NativeCompareSound` at the end of the frame: the mixer's samples
against the board's, and each voice's settings and the keys against the chip's registers ("native sound" and "native
voices" in the report; the first frame, voice and sample that differ printed). The chip's registers that the board
model gives no effect (a voice's first low half and seventh register, the mode word at 040E0210) are not part of the
interface.

## The checks

Three levels, each run with the board model beside the native interface in the same run (`CPS3RECOMP_NATIVE_CHECK=1`:
the board keeps the interface beside its own memories, `Machine::nativeCheck`), the gates the existing checks are
(all the same over attract mode, a one-player game, a versus game, a training session):

- **(a) the interface**: what the board's memories and registers say, decoded into the interface's terms, equals
  what the rewrites handed over, every frame. For the palette (done: `games/sfiii3/checknative.sh`): the board's
  palette DMAs as its registers had them at each start and the colours the game's code wrote to colour RAM, in
  order, against the rewrites' transfers and colours; and the whole of colour RAM against the native palette. The
  frame's registers (done): the board's latched scrolls, tilemap words, screen timing and zoom and the text layer's
  registers against those handed over, every frame. The textures (done): all 8 MB of character RAM against the native
  store, every frame (the store made by the native loader from the lists the rewrites hand over). The object lists'
  heads (done): at every object DMA the game starts, the heads the DMA takes against those handed over; and the
  objects (done): the flattened list the board's DMA makes against the native interface's (FlattenObjects). Next: pixels.
- **(b) pixels** (done for the plain picture: video/raster.cpp, the draw list of quads and tilemap lines a GPU would
  take, rasterised, resolved through the native palette, the text layer over it; compared in system.cpp after every
  frame drawn without widescreen, a second board or objects left out): the native rasteriser's picture from the
  interface equals renderer.cpp's from the board, every
  frame (and the GPU's capture equals the rasteriser's, on the machines that have one).
- **(c) samples**: the native mixer's output from the voice events equals pcm.cpp's from the registers, every frame (done for 3rd Strike: `games/sfiii3/checksound.sh`, every sample of each side, voice by voice).

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
instead of the chips'. Until then the interface is kept in check runs and wherever the GPU draws the window's
picture (`native::On()`), is in save states only in check runs (`native::Checking()`), and is made again from the
board's memories after a state is put back elsewhere (`SyncNative`).

**As built for the video (2026-11-05, on the CPU; in a window the GPU draws the plain frames, above)**: the interface is kept by default
(`native::Keep` unless the port's `--board`), and every picture shown is drawn from it by `native::Rasteriser`
(runtime/system.cpp: `Compose`, every way the game's sources make the picture, each board's layers by `DrawLayers`
with the renderer's settings: margin, world shift, rows down, text layer, overlay, backdrop, the game's choice of
objects by index). A frame the game's code handed nothing over for (the boot ROM's), or the first after a state put back,
run-ahead's or rollback's frames, has the interface made again from the board's memories first
(`Machine::NativeForPicture`). And the board's video memories are made from the interface, not by its video model
(`Machine::NativeMakes`): the object DMA's list from the interface's objects, the palette and character DMAs' colours and
bytes taken from its palette and textures at the frame's end (`TakeFromNative`, with the compressed transfers' table and
what their runs left, as the board's state holds them), their times as before. So save states and the netplay format are
the same, byte for byte. The model (the three DMAs and `renderer.cpp`) runs only with `--board`, in the checks (where
the interface is compared with it), and for the frames the interface does not hold. Checked: default runs against
`--board`, the state hashes (the picture in them) the same over every frame, with rollback, run-ahead and a state saved
and loaded (the states the same byte for byte); the pixel check now composes every way the picture is drawn both ways.
The rasteriser is no slower: the picture's CPU time a frame (`CPS3RECOMP_PICTURE_AB=1`) a tenth less than renderer.cpp's.
Still the board's: the register latches (written by the game's code, read by the interface's SyncNative), the sound.
