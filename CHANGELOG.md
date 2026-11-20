# Changelog

## Unreleased

- **Online Tag team battle: both players choose while the game is paused**: the host's Esc menu now pauses the game
  for both players and opens the other player's, so each chooses their fighters and super arts there. The other
  player then says **Ready**, and the host's **Start Tag team battle** waits until they have (changing a fighter
  takes Ready back). The other player no longer has to choose their team before the host starts.

## 0.2.0-beta.2

**4rd Strike joins the collection, 3rd Strike's Tag team battle can be played online, the game can record video
and save the last 30 seconds as a replay, scenes export as layers for the diorama, and the in-game menu has the
other recomp ports' layout and the mouse.** Pre-release, as 0.2.0-beta.1 was.

- **Scene export with run-ahead**: with Run-ahead on, a saved scene and a clip were kept whole (one layer, the
  picture) in a fight: the layers were drawn from the frame the game goes back to, not the frame shown. They are now
  taken of the frame shown, and come apart as without it. A fight's parts also put in front the player the picture
  has in front where the two are over each other (a throw's, say), not the one whose sprites the board drew last.
- **Tag team battle online** (3rd Strike): in a match, Esc opens the in-game menu over the running game (it goes on for
  both players; your fighter stands still while it is open). Its **Online match** page has the host's **Start Tag team
  battle** and **Back to Versus**, and each player's own team: the fighters and their **super arts**. Both teams are
  people, the stage one of 3rd Strike's; the rest of the Tag team battle page (style, fighters a team, stage) is the
  host's. The page's first fighter, second and third now have a **super art** row each offline too (I, II or III).
- **Scene export: parts, objects, overlays** (cps2recomp's): in a round, a scene and a clip's frames are also split by
  what is in them: the stage behind, each player, the stage in front, the panel and the rest; a saved scene has each
  object by itself (`entries.png`, `entry-map.png`); in training, the hit boxes and the readout are overlays of their
  own, the boxes listed with their player. The diorama shows them as cps2recomp's do.
- **The in-game menu laid out as in the other recomp ports** (recomp-common's standard layout): Online, Display,
  Game, Replays and Stage first; then Alternative modes. In New Generation, 2nd Impact, 3rd Strike 990608 and 4rd
  Strike, Training mode (and Dummy under it) is now under Alternative modes, as in 3rd Strike. Some words as the
  other ports have them: Scaling's **Whole pixels** (was Integer), the Board page's **As the board is set** (was As
  the board has it), the endless battle's **Stages**. Settings already chosen stay as they are.
- **The mouse in the in-game menu**: pointing at a row chooses it, a click does what Enter does, the right button goes
  back a page and the wheel goes up and down. The keypad's Enter confirms too.
- **Scene export** (cps2recomp's, every game): the Game page's **Scene export**. **Save scene** (Shift+F12) saves the
  picture and each of its layers by itself as a see-through PNG (the board's tilemaps, the objects between them, the
  text layer; New Generation's or 2nd Impact's stage's layers too, in a fight on one), with a `scene.json` of their
  order, in `exports/scenes/` beside your saves; **Record clip** (Ctrl+F12) the same for each frame of 3, 5 or 10
  seconds. Shadows are in the layer they fall on, so the layers put back together make the picture exactly. With SVG
  pictures too, a scene's `scene.svg` has each layer as a layer of its own. The **scene diorama**
  (`tools/diorama/diorama.html`, cps2recomp's) shows them as 3D cut-outs: open it from the launcher's Games page
  (Tools) or as a file.
- **SVG pictures** (cps2recomp's): the Game page's **SVG pictures too** writes an SVG beside each screenshot's PNG
  (F12): each pixel a square, so it scales to any size exactly, in the Display page's shape. Drawn, it is its PNG
  pixel for pixel.
- **Instant replay** (cps2recomp's, every game): the Video page's **Save replay** (Alt+Shift+F12) saves the last 30
  seconds just played as a video, with no recording started (**Replay length**: 30, 60 or 10 seconds, or off). The
  game keeps a state every second and every frame's controls, and plays them again into the video beside the game,
  in the time each frame has to spare, so the game goes on at its speed.
- **Video recording** (cps2recomp's, every game): the in-game menu's **Game > Video** page. **Record video** (Alt+F12)
  records what you play and hear as an MP4 (H.264 and AAC, which plays anywhere) in `videos/` beside your saves, until
  you stop it: the picture as screenshots take it, larger (3 times the board's lines unless **Video size** says 2, 4
  or 1080 lines) with each pixel a sharp block, in the Display page's shape; the board's sound and the custom
  soundtrack's. Stopped or in the menu nothing is recorded; slowed or sped up it plays at the board's speed.
  `--record-video FILE.mp4` records a whole run, and with `--movie` and `--headless` makes a movie into a video.
- **Each game's files on its page**: every game's page in the launcher has a **This game's files** card with that
  game's folder (its settings, saves, save states and logs) and a **Show** button that opens it.
- **Dreamcast arranged can only be chosen with the disc**: the launcher's Custom soundtrack dropdown and the in-game
  menu offer it only once a Dreamcast disc image is beside the ROM set (or in the game's folder), or its music has
  been copied already.
- **4rd Strike** (4rd Arrange Edition 2013, `sfiii4n`; issues #9, #23) as a game of its own in the launcher, with
  training mode, widescreen, online play in its own waiting room, and replays. The hack patches 990608's program in
  place (two routines and the characters' data), so it has that revision's facts and translated code.
- **Widescreen in New Generation and 2nd Impact is the board's own**: the 16:9 mode both games have (2nd Impact's
  test menu calls it Screen mode; New Generation's leaves the row out). The picture is 495 columns, the life bars,
  faces and gauges are laid out at the corners, and the stages' ends are the game's own, so the corners are no longer
  reached sooner and there are no invisible walls to choose. Switched in the in-game menu, the game starts again, as
  the board does when the setting changes. 3rd Strike keeps its widescreen: its own wide mode leaves the panel laid
  out for 4:3. (Issue #18)
- **Dreamcast arranged music, fixed** (player reports): each round now has its own Dreamcast version (rounds 1, 2 and
  3, as the Dreamcast plays them); only the round-3 version played before, for every round. And it plays at the
  arcade music's loudness (it was about three times louder than the sound effects and the board's music). The first
  start after updating copies the other rounds' files from the disc image, which has to be beside the ROM set still.
- The custom soundtrack: a song in soundtrack.ini may name other files for other moments (`file2`, `file3`...), as
  the game's sources choose them (3rd Strike: `file3` for a stage's song in round 3 and later).
- **Bezels** (Display page; issue #8): something beside a 4:3 picture on a wider window or screen, off unless chosen.
  With bezels the window opens 16:9 and the picture keeps its shape in the middle; full screen they fill whatever is
  left beside it, ultrawide screens too. Black, White, four patterns drawn by the game itself (Dusk, Carbon, Neon
  grid, Brushed metal), or **Your own**: the first .png or .bmp in a `bezels` folder beside the game's settings,
  scaled to cover the screen with its middle in the middle. Under shaders too.
- **Borderless full screen** (Display → **Full screen as**; issue #20): **Borderless window** makes full screen a
  window with no edges as large as the screen instead, for what needs the game in a window (NVIDIA RTX HDR, overlays,
  recorders, quick switching to other windows). The launcher's Full screen, F11 and Alt+Enter all follow it. Not with
  the Arcade monitor's 384 x 224, which always takes the screen over.
- Dreamcast arranged music: when it can't be copied, the in-game menu says why the next time it opens (which disc
  image, and what was wrong: most often its track files missing beside the .cue or .gdi), and says so when it was.
  docs/DREAMCAST_MUSIC.md no longer says guest stages play their own music with it: they play the arrangement too.

## 0.2.0-beta.1

- **Dreamcast arranged music** (3rd Strike; issue #28): Custom soundtrack has a fourth choice, **Dreamcast arranged**.
  With your own image of the Dreamcast 3rd Strike disc beside the ROM set (or in the soundtrack folder): a Redump .cue
  with its tracks, a .gdi, or the .zip of either, as downloaded. The first time, its arranged stage themes are copied
  out (about 60 MB, a few seconds) into a dreamcast folder beside the settings, and from then on they play in place of
  the board's stage music, each looping as it does on the Dreamcast. The other music stays the board's.
- The custom soundtrack plays **ADX** files too (CRI's ADPCM, with their own loop points).
- **Held arrows in the in-game menu**: holding Up, Down, Left or Right goes on through the rows and choices, faster
  after a second (a long choice, the stages say, no longer needs a press for each).
- **Country flags in the waiting room**: choose **Your country** on the Online page (none unless you do) and the
  others in the waiting room see your flag and country beside your name, as you see theirs. Needs the online service
  deployed with it (`online/`); an older service just doesn't pass countries on.
- **Input delay for hosting**: the launcher's Online page has an **Input delay** choice (0 to 8 frames, 2 unless chosen)
  under Host, for the matches you host and the invitations you send; the other player gets the same.
- **Arcade monitor** (its own section on the launcher's Features page; in the menu, under Display): **Full screen
  resolution → 384 x 224** switches the screen to the board's own resolution (an arcade monitor's modeline from CRT
  Emudriver, or a custom resolution) and fills it pixel for pixel. Only that exact mode is used, at the refresh nearest
  59.6 Hz; if the screen doesn't list it, full screen stays at the desktop's resolution. **Screen** chooses which
  monitor the game opens and goes full screen on. Both are set on the launcher: the in-game menu can only turn them back
  off, so a wrong mode is never sent to the monitor while playing. **Compact menu**: the in-game menu in small letters
  with its rows close together, about ten rows at 224 lines with none running into the next (on by itself at
  384 x 224). The menu is also drawn on the screen's own pixels wherever the picture is under 672 lines, so its
  letters are no longer shrunk. (Issue #15)
- **Android**: SF3comp-android.apk, the launcher and every game in one app for 64-bit ARM phones and tablets
  (Android 9 and later), as CPS-2 Recomp's. Add the ROM sets from the phone with the system's picker (they are copied
  into the app's folder); the screen has touch controls until a gamepad is used, Back or the screen's Menu button
  opens the in-game menu, the Display page's shaders work, and online play reaches the same service. (Issue #14)
- **Opponent's stage, any game** (3rd Strike, Stage page, on in the Enhanced preset): in the arcade game, with Stage
  at the game's choice, each fight is on the opponent's own stage from 3rd Strike, New Generation or 2nd Impact, one
  of them at random (fighting Akuma: his stage from 3rd Strike or 2nd Impact). Only the games whose sets are there
  are drawn from; with 3rd Strike's set alone nothing changes. (Issue #7)

## 0.1.0-beta.3

The three Street Fighter III games in one app, and online play through its own service.

- **Online play has its own service** (`online/`, sf3comp-online), no longer cps2recomp's: codes, the lobby and the
  waiting room are this collection's alone, with a stats page for whoever runs it. Both players need this build or
  later; an older one still finds only the old service's lobby.
- **Stage on the menu's first page**, in every game: one choice of where the next fight is, in the arcade game,
  training and the modes that have no stage of their own to say (3rd Strike: its own stages and the other games').
  It replaces the Training page's Stage row and the Display page's "Stages (experiment)". Online, the host's choice.
- **Guest stages in the modes** (3rd Strike): the tag team battle's and the endless battle's Stage rows have New
  Generation's and 2nd Impact's stages too, as training's Stage row has them (they had 3rd Strike's only).
- **New defaults** (the Enhanced preset, and Reset all options): arcade timing, invisible walls in widescreen, and
  Scaling's whole-pixel choice, now called Integer. A player whose settings already say otherwise keeps them.
- Widescreen: the black screen before a round (and the striped wipes either side of it) left the added columns at the
  left uncovered. The text layer, which the game fills for them, now wraps into those columns as it does on the board.
- **3rd Strike's later revision (990608, `sfiii3n`)** as a game of its own in the launcher, with training mode,
  widescreen, online play and replays. The definitive version stays the 990512 one.
- **Input lag readout** (Display page): the frames the game itself takes to show a press (measured in a fight),
  what run-ahead takes off, the port's own time from the press to handing the frame to the screen, and the sum.
- **Training scenario: Evo Moment 37** (3rd Strike): Ken with a sliver of health against Chun-Li's super art, every
  try by itself; the readout counts the parries and says where a try ended.
- **Red parry drill** (3rd Strike): a score of the strings you guarded and then parried out of.
- **Hit confirm drill** (training, all three games): the dummy guards at random and each attack that touches it is
  judged by what you did next (gone on after a hit, held back after a guarded one), with a running score.
- **Match replays.** Every match, offline or online, is kept (the latest 40) and listed on the in-game menu's Replays
  page: watched again with the readouts and hit boxes, stopped, stepped, slowed, gone back and forward in, and played
  on from any moment with Start (the restart key goes back to that moment). In all three games.
- **SF3comp: the three Street Fighter III games in one app.** New Generation and 2nd Impact play as games of their own
  beside 3rd Strike, picked in one launcher (`SF3comp.app`, `sf3comp-launcher`), each with training mode, widescreen
  and online play with rollback. 3rd Strike's settings, saves and ROM set carry over from the single-game launcher.
  Where a far background layer's art ends short of the wider picture at a stage's end, its own end is shown again,
  mirrored. Hit boxes sit on the fighters' pictures (they were a frame early).
- The launcher finds ROM sets in a `rom` folder beside it, and shows 3rd Strike as "Street Fighter III 3rd Strike
  Definitive" (its folder of settings and saves is the one it had).
- Board page: each setting now sets its own (Difficulty set the damage level and Damage level the difficulty, Timer
  speed did nothing, and the rounds, Event and Bonus games were crossed).
- Stage viewer: New Generation's and 2nd Impact's stages, each with its own music; "Q's" gone (Q has no stage of his
  own: it showed Dudley's). On one stage it stays on it.
- Widescreen: switched in the in-game menu, the window takes the 16:9 shape as it does when started so (it stayed 4:3,
  the picture letterboxed in it); the stages' things no longer pop in 64 columns inside the picture's edges.

- The open source project: a new README, CONTRIBUTING, CODE_OF_CONDUCT and SECURITY, issue and pull request
  templates, the files' audit (tools/audit.sh), CI on macOS, Linux and Windows (the programs built with no
  translated code and started with no window: tools/ci_smoke.sh), and a release workflow that drafts a release from
  a tag (docs/RELEASING.md).
- Sound in stereo: the board set to its EXTERNAL (stereo) output, not JAMMA (mono), its default. In mono the knock-out's
  boom, made of one voice for each side, played twice on both, twice as loud, flanged and clipping. The Board page's
  Sound row picks mono again.
- Readable code: the fighters' moves (the step in a fight, the hit stop, where a move may be cancelled, the dispatch
  into normal moves, specials, super arts and throws) and the animations' frames and boxes are written again as C++,
  each checked against the game's own code on every call.
- Bonus games: straight from character select to the bonus stage, as the game itself goes there after a won match
  ("BONUS STAGE", the versus screen, the stage), with no match fought unseen first.
- Guest stages: the camera follows 3rd Strike's across and up on every stage (it followed on some only as the fighters
  neared the picture's edges, on 2nd Impact's Elena stage not at all), Oro's entrance from the sky included, and on
  both Necro stages and the widest ones it is no longer a few columns to one side. Widescreen fills on New
  Generation's stages (a strip showed past their ends).

## 0.1.0-beta.2

- Guest stages (New Generation's and 2nd Impact's): ROUND 1 and FIGHT! shown over them (they were hidden), the
  opponent's stage there from the round's first frame with "load ahead" off (3rd Strike's own stage showed first), no
  camera pan at a round's start. Tag team battle: the incoming fighter's score is shown at once (it read 00 until the
  next points).
- Readable code: the computer player (how it chooses a plan, answers, guards, parries, gets up, and plays its plans),
  the stage's camera and the versus stage choice, and the fight's score, clock, round end and hit test are written
  again as C++, each checked against the game's own code on every call (checklifts.sh).
- Bonus games (Alternative modes): Crush the Car or Parry the Ball, straight from the menu with the character chosen at
  character select; the menu again when it is over.
- The alternative modes made as the CPS-2 ports have them. Ring out: a fighter knocked about at an open end goes over
  it and falls, and the winner jumps down onto the next stage, the camera following it. Endless battle: the stages
  scroll by with no break (or fade, as a choice). Tag team battle (so named; tag the default; rested fighters keep
  their health). Attract mode with free play on. The stage viewer with only music. The model viewer straight to its
  character, with Reset options and the animation's name at the top. The colour editor's panel: any character, set,
  line and colour, random colours, a library of saved sets.
- The zoom on a fighter works (it had nothing to follow). Custom colours are always on. The endless battle's scroll
  never stops at a stage's end; the model viewer keeps its character in place with nothing else drawn; a mode that is
  over opens the menu on its page instead of starting the board again.
- The menu in the CPS-2 ports' order and words (Alpha 2's, row by row). Endless battle's shuffle fades one stage into
  the next. In a ring out the computer plays the mode: out of its own corner, pushing you to yours. Training's hit
  boxes in widescreen were drawn 64 columns left of the fighters.
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
