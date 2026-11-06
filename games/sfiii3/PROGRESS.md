# Street Fighter III 3rd Strike: progress

## Native / emulated

- The board (`cps3recomp/machine`): own code, no emulator. SH-2 from satrecomp.
- The game's SH-2 code: translated ahead of time (`cps3recomp/tools/translate.sh`, 23,970 blocks from attract mode
  and nineteen seeds of made-up play; checks in `tools/check.sh`: interpreter and translation the same over 6000 frames
  of each, 3.56 M dispatches compared block by block, 0 different). 287 fps headless on a loaded machine (the
  interpreter: 15). Code not reached by the recording runs in the interpreter.
- Picture: the native video interface drawn by its CPU rasteriser (`cps3recomp/video/raster.cpp`), the board's video memories
  made from the interface (2026-11-05); the board model's renderer (`cps3recomp/video/renderer.cpp`, MAME's drawing rules) with
  `--board` and in the checks, the reference.
- Sound: own 16-voice mixer (`cps3recomp/sound/pcm.cpp`), not listened to yet.
- Frontend: cps2recomp's port/ (window, SDL_GPU presenter, shaders, menu, HUD, zoom, bindings, netplay, run-ahead,
  save states) on the board through `runtime/system.h`; recomp-common's launcher (`cps3recomp/launcher`).
- Not yet: Linux/Windows release builds (Linux on ARM and x86_64 compiles and matches the Mac's hashes), ring out and tag team,
  the model viewer and music player, the colour editor.

## Game facts

- Set `sfiii3nr1` (Japan 990512, NO CD: the Fightcade revision, the user's choice on 2026-11-02; downloaded from the
  Internet Archive's MAME 0.264 non-merged item to ~/code/recomp/cps3-roms). The 990608 `sfiii3n` also runs (only SIMM 1 differs). Program: cartridge ROM (BIOS, 512 KB) + SIMM 1-2 (16 MB, the game's code
  runs from 0x06000000). Graphics and samples: SIMM 3-6 (64 MB).
- Cipher keys: found from the set at load (`machine/crypt.cpp`), never stored.
- The BIOS checks each program SIMM's flash ID (04 / AD) with the SH-2's DMA from the cache-through mirror
  0x26000000; the DMA reads past the cipher.

- Widescreen (Game category: it changes play): 64 columns more each side (the renderer's margin: tilemaps drawn on,
  objects and the text layer moved over); in a fight the camera's ends (02026CF4/CF6 right, CF8/CFA left) are
  narrowed by 64 so the picture never shows past the stage. Seen on Ken's stage at both ends; other stages, the
  objects the game leaves out at the edges (it culls to its 384 columns) and non-fight screens not looked at.
  Invisible walls (widescreen_walls): the limits left as the game sets them, the world drawn off centre near a stage's
  end (Renderer::shift) and the text layer (the panel) centred: the state hash is the same as without widescreen.
- The board's game settings (Board page: difficulty, damage, timer speed, rounds against the computer and between
  two players, event, bonus games, continue, demo sound): the settings EEPROM's bytes 0x21-0x24, 0x25, 0x27, 0x02,
  0x05 (and their copy 0x30 on), found by changing them in the test menu and comparing the EEPROM (each word holds
  the game's bytes low first: until 2026-11-02 they were written one byte off, so difficulty set the damage); written before the board starts, so a reset takes a change; on a blank EEPROM (the first
  start) the game's defaults go over them, so they take from the next start.
- Straight to the opening (skip_intro, Enhanced): the first 760 frames (memory checks, region warning, Capcom's
  logo) fast-forwarded. Cheats: no damage, no stun (each player), one hit knocks out, infinite time.
- Play as Gill (Game page, host's online): the chosen character's byte (0x02011387/88) set to Gill's 0 while no
  match is on; Gill vs Gill in a training session and a one-player game as Gill seen in pictures.
- Fight music (Game page, host's online: it changes the driver's RAM): the game asks for a song with 0x061394D0
  (r4: song or effect, 16 callers); a stage song asked for (10-41, two a stage) is changed for the chosen stage's.
- Animations (model.cpp): a character's record holds ten lists of animations (+0x1AC on, each a list of longs, each an
  animation's address: Alex's 0x061DE53C on); an animation is cells of 16 bytes (the first word's high byte the
  frames shown, below 0x100 a command, 0xFAFF holds; the fourth the picture); the record's +0x200 is the animation
  playing, +0x204 its cell (in fours of bytes), +0x214 the frames left of it, +0x20C the step; 0x06089848 advances it.
  The model viewer plays one chosen (A0..J159, the ones a character lacks passed over), or all in turn.
- Stage viewer (training page): Machine::onlyStage draws the board's own stage objects and no text layer (or the
  stage board's picture alone); the stick moves both fighters together so the camera scrolls.
- Survival, endless battle and team battle (games/sfiii3/src/battle.cpp, as cps2recomp's fight/battle.h): a board
  reset to character select for two players, that machine kept, and each next match made on a spare machine
  (runtime/spare.h, cps2recomp's) from it while the knock-out plays (six of its frames a frame, about 70 frames of
  the port's), then put in the port's place: no screens between fights. Characters by kChosen, the stage by kStage,
  the computer's sides by record byte 3, a match decided by kRoundsWon. Endless: no panel (the text layer not drawn),
  no clock, the loser replaced, a side a person's while they touch it. Survival: the player's character kept with
  its health (and a tenth back), a new opponent each time. Team: the loser's next on fresh, the winner with its
  health. Seen in pictures and logs; not hand-played.
- Play-test fixes (2026-11-06; `games/sfiii3/checkmodes.sh ROM BUILD`, headless, judged by modecheck.py, fails on the
  code before and passes after): the stage viewer's camera taken from the first frame its stage is shown (it followed
  the unseen fight's, held at the stage's middle and shuffling, until that fight's entrances were over: ~280 frames),
  its music the stage's own or none (another stage's was silent), Infinite its first choice; survival keeps player 1's
  super art gauge from one opponent to the next; ring out puts a win's 40 seconds on the computer's play time
  (Control_Time, 0x02011372: its level, as the arcade's Update_Level_Control does for a person's win) as each next
  opponent comes on; the modes' dice seeded from the time (the seed was the frame count of a machine just reset: 0,
  the same teams every time); the tag style's fights made before the first fight is seen, one for each pair of
  characters the teams can have on (both sides' next fights were made while the fight went on, three spare frames a
  frame each, four 8 MB states kept and put back for each: 6-20 ms more a frame at the round's start and after every
  tag; now 0 spare frames in a frame shown), and the spares' frames run several to a load (runtime/spare.h,
  RunSpareFrames); Parry the ball from the menu sets up the stun gauges as a match's set-up would (0x061186F2's work:
  the most stun was 0, so the first ball stunned); the model viewer and colour editor black until the round is on,
  the versus fight's intro not shown.
- Credits: byte 02007CE0 (free_play keeps it at 2). Round clock: byte 02011377 (seconds; infinite_time).
- The DMA-done interrupt (level 10) drives the game's queue of character and palette DMAs (handler 060004D0 calls
  06138230: state word 0207603C, queues at 0207803C / 0207844C). Its timing matters: with the board's clock read
  stale inside a CPU slice the queue stuck and the fighters were never loaded.

- Training mode (games/sfiii3/src/training.cpp on cps3recomp/fight/session.cpp, cps2recomp's session without its
  modes and battles): the menu's Start (board reset, way to character select worked unseen, player 1 choosing for
  both), Start over (sides swapped or not), no clock, combo readout, input display, frame data and meter, health
  refills, the dummy (the toolkit's: stand, crouch, jump, guard, record, replay), hit boxes, full meter, a stun readout (taken / the most), a parry readout (parried on which frame of the window, the wrong
  window, too early or too late by so many: windows at 02026335.. per the training script; all four outcomes seen), a dummy that parries
  (it learns each of player 1's animations' frames until its attack comes out and taps 2 frames before, again no
  sooner than 18 frames on: 168 parries to 63 hits over three seeds of made-up play; misses are mostly throws and
  first sightings; projectiles parried as they come within three frames of their closing speed: 7 of 8 of Gill's
  fireballs), guards (standing against jump-ins and the overheads it has been hit by, moves within 280 and projectiles within 160), escapes throws, quick-stands, mashes out of stun (timer two bytes on from kStunMost), a stage
  choice (0x020154F5), the frame data log. Seen working
  in a made-up fight's pictures only. Frame data is read once a frame from the records (not the game's own steps)
  and checked on one move so far: Alex's standing LP reads startup 5, active 2, recovery 8, and the training script's
  recorded animation a444 has it hitting on frames 5-6 (a hit counts the frame after the box is out). Checked in bulk with
  `games/sfiii3/framecheck.py` (SFIII3_MOVES=1 logs each move; the reference is the training script's recorded
  animations, not in the repository): Alex's startups agree on every move recorded (123 of 123 in 12000 frames of
  made-up play, the recordings' first hit frame counted from 0 being startup less one; a hit registers on the step its
  box is out). Active differs when a move connects (the hit's freeze and the box taken away at the hit: the reference
  counts its own way); whiffs agree. Recovery and advantage are not checked against anything yet. "Nobody knocked
  out" puts health back to full below a quarter (no hook on the game's knock-out yet). The player records, boxes, camera and meter places are facts from Grouflon's 3rd_training_lua (no licence:
  facts only, no code), made for the 990512 set; life and positions checked on this 990608 set, the rest seen to
  work in a fight's pictures only.

## Gotchas

- Addresses 0x2xxxxxxx are the cache-through mirror of 0x0xxxxxxx: main RAM is reached at 0x22000000 too.
- Interrupts are levels 12 (vertical blank) and 10 (palette/character DMA done), auto-vectored, held until
  acknowledged at 05100000 / 05110000.

## Not checked by a person

- No one has seen it in a window, played it with a pad or keyboard, or heard it. Pictures and WAVs from headless runs
  only. The launcher's pictures were taken with no window.
- Online play: played between the Mac and the Steam Deck by the user (2026-11-02), it works. The launcher's Online
  page (recomp-common's single-game Online page, PR #48 there, merged with main for this copy).
- (old note) Online play compiles but has never been tried (its service is cps2recomp's; the lobby is keyed by `sfiii3`).
- The VS screen shows horizontal stripes: probably the game's wipe, not checked against the board.

## Custom soundtrack

The CPS-2 ports' custom soundtrack (cps2recomp's port/soundtrack.* and decoders, unchanged but for names), fed by
3rd Strike's music driver as music.cpp reads it: the song table at the address kept at 0x02079C6C (49 songs, from
1), track 0's record (0x02078D0C: +0 its place, +4 its start, +0x5E bit 0x80 stopped) saying which song plays and
when it starts again. A file plays while its song does (the board's music voices left out, unheard: the game's state
is the same, checked by hash with and without, and over rollbacks); with no file named for a song, the folder plays
as a playlist through everything. Songs named from pictures at their starts and from a training session on each stage
(music.cpp; every stage's music is known, the endings' and a few others' not); songs.txt lists them. The stage byte
(0x020154F5, a character's number) gives training mode its stage choice.
Not listened to by a person: checked by the sound's loudness only (the music's voices silent under a file, the effects
heard, the file's tone in --soundtrack-dump).

## Priority: native video and sound (2026-11-04)

The user's decision: going native at the edges comes before more readable CPU code. The port should stop
modelling the CPS-3 board in its default build. The game's drawing and sound code is rewritten to hand a native
interface its frames (objects, tilemap lines, scrolls, palettes, the text layer) and its voices, instead of
writing the video and sound chips' registers and memories. The board model (machine/, video/renderer.cpp,
sound/pcm.cpp) stays only as the reference. Checks run the two side by side: the same interface decoded from the
board's memories against the one the rewrites hand over, every frame, then pixels and samples. The interface and
the checks: cps3recomp/docs/NATIVE.md; this game's routines, read-backs, timing and plan: games/sfiii3/NATIVE.md.
Done, level (a), all the same in checknative.sh over attract mode, a one-player game, a versus game and a training
session (12000 frames each): the palette (src/video_palette.cpp), the frame registers (src/video_registers.cpp), the
textures (the character DMA's lists and transfers, src/video_palette.cpp and video_textures.cpp, made by the native
loader), the object lists' heads (src/video_lists.cpp) and the objects made from them (native::FlattenObjects). With the
arcade timing too (2026-11-05): the hand-off rewrites are run dry before the game's own code, charged nothing, and hand
over the same (runtime/lift.h's DryRun, src/video_forms.cpp's native forms of what they call; NATIVE.md, "Arcade
timing"): `CHECKNATIVE_ARCADE=1 checknative.sh` all the same, the state hashes the same with the dry runs and without.
Guest stages (2026-11-05): New Generation's and 2nd Impact's hand-offs are 3rd Strike's routines on their own places
(src/video.h, video_games.cpp), run dry on the stage board into its own interface; the picture made from the two
interfaces' layers is the board models' (checknative.sh's stage runs, CPU; each producer's part on the GPU).
Later that day: no match is played behind a borrowed stage any more. The stage board runs only the guest's stage code
(its game task's modes replaced: the stage built, then each frame its stage's frame and effects; its tune by the game's
own routine), and its stage layers are drawn with 3rd Strike's as one frame by one renderer, CPU or GPU (DEFINITIVE.md,
"The way"). Checked against the match's stage given the same inputs, frame for frame: New Generation's 13 stages all the
same, 2nd Impact's 12 of 15 (Ryu's and Dudley's one frame each, Yang's far layer 2 columns off on 127 of 3644 frames;
DEFINITIVE.md).
2026-11-06, after the first play-test: that check handed the stage alone the match's own camera words, so it missed what
the play-test found (black parts, far layers scrolling by themselves, no camera up, New Generation's Gill stage drawn
small after a stage chosen mid-fight, 3rd Strike's music under the stage's). The stage alone's camera now goes as a
match's does (its fighters' places for its camera code given, its camera held and shaken as 3rd Strike's, nothing given
in its own entrance), 3rd Strike's music voices are told from its driver's channels, and checkguests.sh (in
checkstages.sh) checks every stage against its own game's match in play and in play with stages chosen mid-fight.
Sound, level (c), done: the driver's voice step, set-up, all-off and resume rewritten (src/sound_driver.cpp) to hand
voice events (key on and off, the sample, looping, pitch, volume) to a native mixer (cps3recomp/sound/native_sound.h);
checksound.sh compares the mixer's samples with pcm.cpp's, voice by voice, every frame of the same four runs (12000 frames each:
7.5 million samples a side a run, 0 different; the voices against the chip's registers, 0 different; the rewrites against the game's code, 381056 calls a run, 0 different). A deliberate mistake (the pitch handed over one off) was found at its first frame, voice and sample. Nobody has listened to the native mixer (it is only compared with pcm.cpp, which nobody has listened to
closely either); it is kept only in check runs, the default build still mixes with pcm.cpp. Next:
the rasteriser's pixel check (b) passes for the plain picture (video/raster.cpp: draw list of quads and tilemap lines,
the same pixels as renderer.cpp's every frame, widescreen too). SDL_GPU (2026-11-05): the window's picture is now drawn
by the GPU from the interface (port/native_gpu.cpp's compute passes, then the presenter's render pass, one command
buffer, no CPU wait) for every frame drawn the plain way with nothing put over it; the CPU's renderer runs then only if
its picture is asked for. About half the CPU time for the picture, the picture done no later, no frame of queueing
added (the numbers: cps3recomp/docs/NATIVE.md); checknative.sh checks the GPU's pixels as the presenter shows them.
The HUD, status lines, online names, zoom and menu are put over it on the GPU too (a third compute pass, from what the
port draws over a black and a white picture). It was off by default for a few hours on 2026-11-05 (the Mac mini
restarted three times: headless and offscreen GPU runs never waited on their frames, up to 33 command buffers queued,
and WindowServer got no GPU time); each offscreen frame is now waited for as it is submitted (one on the GPU, as
cps2recomp does), a window's bounded by its swapchain, and it is on by default again in a window
(CPS3RECOMP_NO_NATIVE_PRESENT=1 or `--renderer sdl` leave the picture to the CPU's rasteriser, `--board` to the board
model). Checked on the Steam Deck under Vulkan validation and on a MacBook Air; not yet played in a window.
Next: the features' object choices. Wanted features this makes possible, both asked for by the user: GPU rendering (SDL_GPU drawing
the interface's objects and layers, not a software renderer's picture handed over), and the guest stages drawn as
layers through the same interface (DEFINITIVE.md item 10). The readable work below stops at the move code and boxes that are done; the rest waits.

## Readable code: what gets rewritten

The user's decision (2026-11-04): rewrite what features need, not the whole program for its own sake. About 645
routines are readable (about a quarter of the code seen running; measured from the block database with routine
starts taken from direct calls).

Done: the computer player, the round, the match and its end, damage, the hit tests and decisions, input and the
command reader, the score and the clock, the stage camera, zoom, per-stage code and stage choice, the bonus games.

Done 2026-11-04 (items 1 and 2 of the order below): the characters' move code down to the shared layer
(fight_moves.cpp: player_mv_4000 0x06119A6C, check_hit_stop, select_hit_stop, look_after_timers, check_lever_data,
process_attack and check_cg_cancel_data (the cancels: where a move's recovery may end), process_catch, Player_normal
and its flags, Player_attack (the dispatch: below 16 the shared normal moves, plpat_lv_00; from 16 the character's own
specials, super arts and command throws, plxx_extra_attack_table), Attack_00000 and Attack_01000), and the animations
and boxes (fight_boxes.cpp: char_move 0x06089848, check_cgd_patdat and check_cgd_patdat2, set_new_attnum (the frame's
attack), set_jugde_area 0x0608C4A4 (each box pointer: the character's table of that kind plus the frame's box set's
index), set_char_move_init, set_char_move_init2, exset_char_move_init, char_move_z, _wca, _index, _cmhs,
setup_comm_back, setup_comm_abbak, the sound's filter). sfiii3.h names the record's fields they set (routines, hit
stop, animation, frame's cancels, box set, box pointers, attack). Not written again: the characters' own steps
(Normal_*, Player_damage's reactions, the plxx tables' specials), the animations' 125 commands (decode_chcmd, run as
the game's code), Attack_02000 on. Not met in the checks' runs: exset_char_move_init, check_cgd_patdat2,
char_move_cmhs (their callers: a hurt step 0x0612388A, jumping_cg_type_check 0x0611BFA2, a hit stop's end with
hsjp_ok).

Still to rewrite, in this order (each because a feature reads or changes it):

1. (done, above) The characters' move code.
2. (done, above) Hit boxes and hurt boxes.
3. Projectiles and the effects that hit (they share the hit tests; frame data for fireballs).
4. The parts of drawing and the screens that the modes change: the HUD, the select screen, the versus and win screens.

Left as translated code on purpose, unless a feature comes to need it: the sound driver's side in the game,
boot, the board test and service screens, decompression and loading, the attract demo's own steps, the
endings and the staff roll, and the drawing routines no mode touches.

## Next

1. Frame data: recovery and advantage against published data (startups agree with the recorded animations; active
   on a hit differs by the hit's freeze). The dummy: guard from the boxes (attackInRange), the parry's remaining misses (SFIII3_PARRY_MISSES).
2. Hooks in the translated code: the knock-out (so a session's "nobody knocked out" stops putting health back at a
   quarter), the round's steps.
3. The definitive version (DEFINITIVE.md): the stage
   rule checked on every stage, a proper menu of stages by name, the stage board's own effects and announcer.
4. Custom soundtrack: the songs not placed yet (songs.txt's unnamed ones: the endings, say).
5. Translation coverage: more recorded play (every character, supers, the endings).
6. Widescreen on every stage and screen; the objects the game culls at its 384 columns; invisible walls (play as on
   the board in 16:9, the CPS-2 ports' widescreen_walls: needs the HUD's objects told from the world's).
7. Linux / Windows builds (cps2recomp's and fighting_vipers' tools/release as the model), then the other devices.
   Linux on 64-bit ARM (gold) and on x86_64 compile and link in Docker (cps2recomp's build images), and their
   1500-frame --auto 7 hash is the Mac's (c476e974); Windows not tried, no release scripts yet.
8. The colour editor: done (colours.cpp, model.cpp): each fighter's palettes come by palette DMA from the
   graphics SIMMs as its match begins (player 1's to colours 0x000-0x1FF and a copy at 0x200, player 2's to 0x400 and
   0x600); a custom colour is given for the graphics word it is read from (Machine::paletteColour), kept per
   character in palettes/<Name>.txt. Not yet: a library of saved sets, colours per colour button named.
9. The alternative modes10. Team battle's tag style: done in a first form (battle.cpp: each side's next fighter's fight made on a spare, the
   other side's record, the camera, clock and rounds carried over on a tag; the newcomer where its partner was). Not
   yet as cps2recomp's: the jump off and the jump in, the call; a person's partner in co-op/2v2.
11. The alternative modes (: cps2recomp's fight/alternative and battle,
   3000 lines tied to the CPS-2 games' memory), a music player and model viewer, the colour editor, the board's other settings.

## Decisions for the user

- Which 3S revision: this port is on sfiii3nr1 (990512, chosen by the user: the Fightcade set; from the Internet Archive). Fightcade and the training scripts use
  sfiii3nr1 (990512).
- 3SX (crowded-street/3sx) is a native 3S port built on the PS2 decompilation; this project goes on separately from
  the arcade program (the NG / 2I content is arcade-only).
- Online play uses cps2recomp's online service (lobby keyed by game id `sfiii3`).
