# 3rd Strike going native: what the game does to the video and sound chips

The toolkit's half (the interface, the checks, the GPU, guest stages, timing) is `cps3recomp/docs/NATIVE.md`. This is
the game's: every place its code touches the chips, what it writes and reads back, when in the frame, and the plan.

How it was found: the census (`CPS3RECOMP_HW_CENSUS`, with `CPS3RECOMP_NO_BLOCKS=1`) over 6000 frames of attract mode
and 6000 of made-up play (a one-player game: character select, fights, the screens between), every access by
instruction with the lines of the frame it fell on (the frame starts at the vertical blank: lines 0-39 are the blank,
40-263 the picture), then the routines read. Counts below are from those two runs. Names in quotes are the PlayStation 2
decompilation's (crowded-street/3s-decomp, AGPL: names and understanding only, no code) where its routine is the
arcade's at a higher level; the PS2 build replaced the CPS-3's video and sound with its own, so the arcade's
lowest-level routines (the DMA queue, the list heads) have no name there.

## The frame

The vertical blank's interrupt (level 12, 0x06000528) does the chips' work for the frame, in this order:

1. 0x0613809A: the DMA queue kicked (the next character or palette DMA of the draining set started, if none is under way)
2. 0x0612DDCC: the object lists' eight global scrolls written (040C0000-1F, from main RAM)
3. 0x06005C00: the screen's timing registers (040C0060-66, 70-76) and the text layer's registers (05050000-0A, 28)
   written when the game has changed the mode (four times in the runs: start-up); the coin counters (05000008)
4. 0x0613696C: the object list's DMA started (8, 9 written four times to 040C0082): the board takes the list as it is now
5. 0x0612E610: the four tilemaps' registers (040C0020-5F) and the text layer's scroll (0505000E, 05050020-22)
6. 0x06005FB8: the whole picture's zoom (040C0068-6E, 78-7E) when the game asks (0x0200DCD0)
7. inputs, then 0x06136996 (waits for the object DMA's status bit 0 and writes 0 to 040C0082), 0x060BA986 (a tilemap's
   rows copied in from main RAM), 0x06136F90 (the next frame's list heads written: sprite RAM's first 8 KB),
   0x0613A408 (the sound driver: every voice's registers and the key-on word), the acknowledge (05100000).

All of the chips' registers but the palette DMA's are written in lines 0-45: inside the blank or just after it, never
while the picture is out. **No raster effects**: nothing is changed per line by timing. What looks per line is data:
the tilemaps' line scroll tables and the text layer's row scroll, both in memory, and the object list drawing a
tilemap a line at a time (an object with no width). The only thing that changes while the picture is out is colour:
a palette DMA started at once (0x06137DF0's path below) can land on any line (6-240 in the runs); the board model
applies it at once and draws at the frame's end, as the native palette does, so the two agree (a real board might show
a few lines of the old colours: not modelled, and not wanted).

## Video: the routines

### Palette (done: video_palette.cpp, checknative.sh)

- 0x06137DF0, palette transfer asked for (r4 source, r5 destination, r6 length, r7 green fade, stack: red, blue): reads
  the DMA status (040C000C bits 1-2) and the queue's state (0x0207603C); started at once (state 3, 040C00A0-AE) or
  queued. Callers 0x060D4E16, 0x060D4E48, 0x060D4E92, 0x060D51FE, 0x060D6CF8, 0x060D6D24 ("load_any_color", the fades:
  "FadeIn", "FadeOut", "ToneDown"). About 150 calls in 12000 frames of attract mode, a third started at once.
- 0x06137FAA: the same queued always (RAM only; not rewritten: nothing of the chip's).
- 0x06138168, the next DMA of the draining set started (r4 1: up to 0x155 character transfers made into the chip's list
  in character RAM and the character DMA started, 040C0096/98, with the CPU's character RAM bank set and put back,
  0x0613AA5E/0x0613AAB0, 040C0086; r4 2: the next palette transfer, 040C00A0-AE). From the DMA-done interrupt
  (0x060004D0 -> 0x061380E4) and the vertical blank's kick (0x0613809A). About 3000 calls in 12000 frames of attract mode.
- 0x061388F0, the text layer's colours (r6 words from r5 to colour r4/2 of the set at 0x02078CE4, 0xFFFF left as it is):
  the only colours the game's code writes itself (256, once, at start-up); 0x061388DA sets that set's base and the
  text layer's palette base register (05050024).
- The boot ROM writes a few colours of its own at start-up (its memory test and warning: 0x00000E88 clears colour RAM,
  0x0000F3D2-F432, 0x0001030C, 0x0000287C, 0x00013546). **Nothing reads colour RAM back.**

### Character DMA (the textures)

- The requests: 0x06137CE4 (the character request beside the palette one, 13 callers: entries of 12 bytes in 0x02076044, the
  chip's list entry as is) and 0x06138168 r4 1 above; the list built in character RAM at 0x02007EE0 (bank 0x02007EE4),
  the DMA's end interrupts chaining the rest. 0x061367D0 clears character RAM's first 4 KB (start-up, the list's area).
- The game never reads character RAM. **Native**: the store fills by the list's transfers (machine.cpp's CharacterDma
  decompression, rewritten as the store's loader), the list itself no longer needs to be in character RAM.
- 0x06137B08 resets the queue (start-up, a new screen): spins on the status bits until the DMAs end, acknowledges level
  10 itself (05110000 at 0x06137BC0), sets the list's place and bank.

### Object lists

- 0x06136F90 ("the list heads written"; the PS2's "appSetupBasePriority"/priority lists are the idea): walks 128
  priorities (0x0206FC94 or 0x0206FE94 by the frame's buffer, 0x020700A8; nodes of six words linked at +12), writes each
  node's three longs as a list head (object block, count, global scroll; position; settings) into sprite RAM
  04000000, pads to 350 heads with empty ones (0x02007ED0/D8/DA), ends with 0x80000000. In the vertical blank. 0x06136F28
  links a node into a priority (the push, 8 callers), 0x06137214 flips the buffers. 0x061367FA-0x061368AA writes heads too
  (1800 writes, start-up and screen changes).
- The object words (sprite RAM 04002000 on), by many drawing routines through the frame (lines 5-264): 0x06087638-
  0x06087810 (the bulk: about 700000 writes, "mlt_obj_disp"/"mlt_obj_trans"'s counterpart, called from 0x06087580),
  0x060B9B60-0x060B9E48 (tilemaps at 04030000-04043FFF: the stages' layers), 0x06088E86-0x06088EB0 (objects changed in
  place), 0x060E3EC2, 0x060BA9C4 (in the vertical blank), 0x06088118-0x06088178, 0x06136924-0x06136942 (the first
  block, 04002000-040020FF), 0x06138660-0x061386A2, 0x06087952.
- **Read back**: 0x06088E9E (36000 reads: the objects edited in place), 0x060BA9B8 (47000: a tilemap's rows read
  back in the vertical blank), 0x060E3EBC (74000), 0x060B9C7E-0x060B9E42 (tilemap cells read to change), 0x06087950-70.
  So sprite RAM's object words and tilemaps stay plain memory; the list heads (04000000) are never read back.
- The object DMA: 0x0613696C starts it (vertical blank), 0x06136996 waits on status bit 0 (12000 reads: once a frame)
  and writes 0.

### Frame registers

- Global scrolls 0x0612DDCC (writes at 0x0612DDFA/0x0612DE26), tilemap registers 0x0612E610 (0x0612E670-0x0612E8D8),
  screen timing 0x06005C00 (0x06005C2E-5A), zoom 0x06005FB8 (0x06005FDA-0x0600600A, 204 writes: the stages' zooms),
  text layer scroll and settings 0x0612E9C2-0x0612E9F6 (0505000E, 05050020-22), text palette base 0x061388DA. All in
  the vertical blank, from main RAM the game's code set: each becomes one call with the frame's values.

### Text layer (SS RAM)

- The map (05040000-05043FFF): 0x061389C8 (clears), 0x060D6004, 0x06091420-0x06091450, 0x060D5E2C-42 (digits:
  "score8x16_put"), 0x06138B10-BA, 0x06139278-86, 0x060D5CDC-FC, 0x060D636C-74, 0x060D72AC-0x060D7308 ("scfont_put",
  "SSPutStr", "vital_put", "sc_clear"): about 250000 writes. Read back: 0x060D5FFC (53000: the map's cells read to
  change), 0x060D72A8/B0.
- The characters (05048000-0504FFFF): 0x060D533A-0x060D54C6 (440000 writes), 0x0608F4E4-0x0608F572, 0x0608F758-D6,
  0x060D5A6C-88, 0x0608F9EA-0x0608FA86, 0x061388C0-C8, 0x060D5876-B4, 0x060D5BE4-F8. Read back: 0x060D5360 (98000),
  0x0608F4DC (119000), 0x0608F754/6C, 0x0608FA7C: characters are made by reading and changing what is there.
- So SS RAM stays plain memory, the native text layer reads it; only its registers become calls.

## Sound

- The driver is the SH-2's own code, at the end of the vertical blank (0x0613A408, lines 2-47): the voices' registers
  (040E0000-01FF, 16 voices of 8 longs) by 0x0613BBA8-0x0613BC9A (400000 writes, from 0x0613B61E), the key-on word
  (040E0200) by read, mask and write at 0x0613B446/4A, 0x0613B554/58, 0x0613B6AE/B2, 0x0613B8BE/C2, 0x0613BC7E/82;
  0x061393F8 and 0x06139422 (start-up, silence everything, from 0x06133E50); 0x0613A384 (a key-on from a request,
  0x06094E18, 0x06095060); 05000C00 written by 0x06139396 (63 times: not in machine.cpp's map; ignored by the board).
- **Read back**: the key-on word (85000 reads: every change is read, masked, written) and the voices' registers
  (0x0613BC9C, 58000 reads of 040E0002-01FA: the driver steps its voices on from what the chip holds). So the voice
  registers and the key-on word stay memory the driver owns; the native mixer takes the frame's writes to them as
  events with their times (`Machine::FramePosition`, as pcm.cpp's BeforeWrite does) and plays from them.
- The game's request side (0x061394D0, "SE"/"Sound3rd"'s requests, 16 callers) never touches the chip.

## Timing the game waits on

- The DMA status word (040C000C) read by 0x06137DF0 (queue or start), 0x0613809A and 0x061380E4 (the kick and the
  chain: the latter spins while bits 1-2 are set), 0x06136996 (spins on bit 0), 0x06137B08 (spins on bits 1, 2 at a
  reset). On the board model a palette or character DMA is busy for 2500 cycles and its end raises level 10; the
  object DMA is busy 100 cycles. These are the timing the queue runs on (PROGRESS.md's gotcha: the queue stuck when the
  board's clock was read stale). The native interface keeps them as a timer: no chip, the same busy times and the same
  interrupt.
- The DMA-done interrupt (level 10, 0x060004D0) runs the chain: up to two transfers a frame in the blank, more through
  the frame when the queue is long (lines 0-60 for the character DMAs, 0-56 for the queued palettes). How much of a
  screen's loading lands in a frame depends on this: the fighters' graphics come over several frames.
- The slowdown: the main loop's work against the vertical blank (0x02007F00 counts them). With arcade timing the CPU's
  cycles and the bus decide it (docs/TIMING.md); the video area's accesses are charged one wait. The rewrites of the
  hand-offs must keep charging the bus as the writes did, or run the game's own code with arcade timing (as all
  rewrites do now).

## Arcade timing: the hand-overs by dry runs

With the arcade timing on (the arcade_timing feature) the game's own code runs in place of the rewrites, outside the
port's own modes (Game::rewritesNeeded: a training session, the battles), so that its cycles are the board's. The ten
hand-off rewrites (AskPalette, StartNextDma, TextColours, ObjectScrolls, TilemapRegisters, ScreenRegisters,
PictureZoom, TextPalette, ListHeads, ClearListArea: marked `kHandsOver` in their tables) are then run dry at the
routine's start, charged nothing (the toolkit's NATIVE.md, "Arcade timing"): the same functions, on an overlay, so
they hand over what the game's code is about to write. EmptyFrame (0x061367EE) is a hook already and runs either way.

What they call has a native form (src/video_forms.cpp): the divide 0x0612D428 (ListHeads' pads), the tilemaps' tile
banks 0x0612E14C (TilemapRegisters, when 0x0206A1E0 asks), the screen's mode 0x06005AFC (ScreenRegisters, when the
screen's turn changed; nothing of it is handed over), the CPU's character bank 0x0613AA5E and 0x0613AAB0 (StartNextDma's
list: its place in character RAM's 8 MB), the list area's page 0x061379EC and its place 0x06137980 (ClearListArea).
Each does what the routine does to main RAM and r0; they are checked by what the dry runs hand over.

## What stays plain memory

Sprite RAM's object words and tilemaps (04002000 on), SS RAM (the text map and characters), the voices' registers
and the key-on word, main RAM's queues and lists (the game's own). Becoming calls: the list heads, the object DMA, the
character DMA (and its list in character RAM), the palette DMA, colour RAM (never read back), the frame registers, the
text layer's registers, the status word and the two interrupts (as a timer).

## The plan

| Piece | Routines | Size | Check |
| --- | --- | --- | --- |
| Palette | 0x06137DF0, 0x06138168 (palette), 0x061388F0 | done | (a) all the same, 4 x 12000 frames |
| Frame registers | 0x0612DDCC, 0x0612E610 (with the text layer's scrolls), 0x06005C00, 0x06005FB8, 0x061388DA (video_registers.cpp) | done | (a) all the same: the latched registers every frame |
| Textures | 0x06138168 (characters: the list's writes and its transfers handed over), 0x06136782 (video_textures.cpp); the store's loader (native.cpp, CharacterDma's rules) | done | (a) all the same: all 8 MB of character RAM against the store every frame |
| Object lists' heads | 0x06136F90 (video_lists.cpp), 0x061367EE hooked (it waits on the DMA it starts) | done | (a) all the same: at every object DMA, the heads it takes against those handed over |
| Objects | native::FlattenObjects: the heads, the registers and sprite RAM's object words flattened (SpriteDma's rules, in the interface's terms) | done | (a) all the same: the board's flattened list against the native objects, at every object DMA |
| Arcade timing's hand-overs | the ten hand-off rewrites run dry (Lift::handsOver), src/video_forms.cpp's native forms | done | (a) and (b) all the same with CHECKNATIVE_ARCADE=1; state hashes the same with CPS3RECOMP_NO_DRY_RUNS=1 |
| Native rasteriser | the toolkit (video/raster.cpp): the draw list of quads and layer lines, text, resolve, zoom | done for the plain picture and widescreen; the features' object choices and a second producer to come | (b) all the same: pixels against renderer.cpp, every frame |
| SDL_GPU renderer | the toolkit (port/native_gpu.cpp): two compute passes from BuildFrame's tiles, the palette, the textures by page, the text layer; shown by the presenter (Presenter::ShowNative), the window's default picture where nothing is put over it (2026-11-05) | done for the plain picture; the HUD, status lines, menu and zoom still the CPU's | (b) all the same: the GPU's picture as the presenter shows it (render pass, 1:1) against the rasteriser's, every frame; 15 runs of 3000 frames with 0 different |
| DMA timer | the toolkit: status bits and level 10 without the chips | small | gates: state hashes the same with and without the chip functions |
| Sound events | 0x0613A408's writes (0x0613BBA8-0x0613BC9A, the key-on sites), 0x061393F8, 0x0613A384 | medium | (c): samples against pcm.cpp |
| Guest stages | New Generation's and 2nd Impact's same hand-offs (the shared engine: found by the census on their boards) | medium a game | (a) per producer, then the composed frame |
| Board model out of the default build | machine.cpp's chip functions behind `--board` | small, last | every gate |

Order: (done: the palette, the frame registers, the textures, the lists' heads, the objects: the whole frame's
interface exists, level (a); the rasteriser, level (b), for the plain picture and widescreen; the GPU renderer, the
window's default picture for frames drawn the plain way, 2026-11-05: cps3recomp/docs/NATIVE.md, "The presenter's GPU
path") the HUD, status lines, menu and zoom as a GPU layer next, so that no frame falls back to the CPU's picture; the
rasteriser's features' object choices (the stage boards, objects left out); sound events beside them (independent);
guest stages once 3rd Strike's frame is native.
