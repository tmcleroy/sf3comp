# Changelog

## Unreleased

- **Timed super arts with the gauge kept full**: Yun's and Yang's 3rd (Genei-jin, Sei'ei-enbu), Oro's 1st and 3rd,
  Makoto's, Q's and Twelve's 3rd now last their whole time with training's **Gauge refills** or the **Full gauge**
  cheat on. Refilling the bar the moment the art began had ended it at once; the gauge is left to run down now and
  filled again once it is empty (as 3rd_training_lua does).
- **Rewind and a practice point** (cps2recomp's, #443 there): two switches on the Game page (and the launcher's
  Features page), off unless chosen, never online, in every game of the collection. Hold R (a gamepad's left trigger)
  and the game runs backwards through its last half a minute, unheard; `]` (the left shoulder held half a second) keeps
  the game as it is, Backspace (the shoulder tapped) puts it back at once and Backslash (the shoulder with the left
  trigger held) replays the last attempt from it until a button is pressed. Until a point is set, Backspace is
  training's Start over as before. The states share their unchanged blocks, as the instant replay's do, so half a
  minute is about 140 MB (about 230 MB on a guest stage). `cps3recomp/tools/check_rewind.sh` checks that the game comes out frame for frame as a run
  that simply played on.
- **Online sets in 3rd Strike, and the score on the screen** (from cps2recomp #438): the host chooses **Set format**
  under Host on the launcher's Online page (Casual, FT3, FT5, FT10; `online_first_to`, the host's for both). The
  strip at the top shows the names and the games each has won, in Casual too; the first to the target wins the set,
  and once both machines agree on the final score it is shown for two seconds and both go back to the launcher. The
  **win streaks** now count too (they had no match results until now), from the same games. Counted on the frames
  both machines have the real controls for, from the match's records (two people, and a player with the board's
  number of rounds); not in training or the tag team battle.
- **Online → Network stats** (on in Enhanced): at the right under the life bar, in small letters, the ping and its jitter, the input
  delay, and the last second's rollbacks (how many, the most frames one went back).
- **Online, Swap sides** (from cps2recomp #472): the host was always player 1; its match menu now has **Swap sides**.
  Both machines change on the same frame without leaving, each keeping its own controller; the set's score and the
  win streaks start again, and the tag team battle's fighter picks go with the side. Only the host can: the other
  player asks in the chat.
- **Online, Pick characters again** (from cps2recomp #456): after a match only the loser's continue could change
  characters. Either player chooses **Pick characters again** on the match menu; once the other chooses it too, the
  board starts again on the same frame for both and both are put in at character select (every game). The set's
  score stays. The asks and the way back are kept with the game in a rollback.
- Online protocol 8 (the set's final score agreed, Swap sides and Pick characters again): both players need this
  version. The service (`online/`) renews a match's lobby entry every 3 seconds (was 10) and forgets a waiting room
  player 15 seconds after their last check-in (was 30), as the launcher now checks in every 3 to 10 seconds.
- The launcher's Online page: **Set format** under Host, and its introduction says no code is needed for a listed match.
- **Fix: the games crashed at start on Windows PCs with Bandicam installed**, even with it closed: its Vulkan
  capture layer, which Windows loads into every Vulkan program, crashed as the game made its GPU device. The games
  now leave Bandicam's layer out (OBS capture and the other overlays still work; cps2recomp #446).
- **A crash leaves its log.** A game's output reached `logs/game.log` only in blocks, so a crash lost it and the
  launcher's report pointed at an empty log. It is now written as it goes, and on Windows a crash adds where it
  happened and the calls that led there (the build's unstripped programs, kept in `build/release-windows/symbols/`,
  and `tools/release/crash_where.sh` name them). `CPS3RECOMP_TEST_CRASH_AT_FRAME=N` crashes a game on purpose to try it.
- **Windows: fewer antivirus false alarms** (cps2recomp #433). Every program now says what it is (name, version,
  publisher and a manifest, in its Properties), the games' online play makes its requests itself (WinHTTP, as the
  launcher already did) instead of starting a hidden `curl.exe`, and the release build signs the programs when it is
  given a certificate (docs/RELEASING.md); without one, nothing is signed, as before.
- **Touch controls: turn them off, the stick where the thumb goes down, the menu answers a finger** (#53, from
  spelunkomp's #40 and #41). **Touch controls** (Game page; Android, Windows, Linux) **Off**: never shown, so a thumb
  on a handheld PC's or a Steam Deck's screen no longer brings them up mid-match. **Touch movement**: the stick's
  middle is where the finger goes down, anywhere in the left half of the screen away from the buttons (the new
  default), or **Fixed** where it is drawn. The open **in-game menu takes a finger** whether the touch controls are
  on or off: tap a row to choose it and do what a click does, slide up or down to move a row at a time; a finger on
  the controls' own buttons is still theirs. A laptop's or a Mac's touchpad no longer counts as a touch screen.
- **Widescreen set in the in-game menu stays set** in the collection: the menu wrote it to the game's own folder's
  launcher.ini, and the collection's launcher, which keeps it in its own, turned it back at the next start.
- **Android: Widescreen is on the Features page** with the other features, kept as the in-game menu sets it; the
  launcher's Display card (Full screen, which a phone always is) is gone there.
- **Scanlines: OSSC is gone from the Shader choices**: a RetroArch preset made for that box's "Full" aspect, it drew
  the picture 4:3 with the sides smeared (widescreen most of all). The Shader is now kept by name in settings.ini, so
  the other choices stay as chosen; a saved OSSC is Off.
- **Which controller is which player** (the launcher's Controllers page, recomp-common's `[controllers]`): each
  connected gamepad is Automatic, Player 1-4 or Not used, and the game hands out its gamepads that way (again when
  one comes or goes), in place of the first one connected always being player 1's. A Steam Deck's own controls can
  be Not used, so two pads plugged into it play each other. Nothing chosen: as before. The Start + Back menu chord
  comes only from a player's gamepad.
- **Arcade monitor: super resolution, synced to the screen, modes at the board's exact rate** (as CPS-2 Recomp's; the
  page now comes from recomp-common's `arcade_sdl.h` and `arcade_screen.h` in place of the port's own code). **Full
  screen resolution** gains **Super resolution**: a mode of the board's 224 lines and 1920 columns or more (CRT
  Emudriver's 2560 x 224) with the picture stretched across it, or, where the screen has none, one of 240 lines with
  the 224 one for one in its middle; 384 x 224 chosen before stays chosen. **Keep the board's pace** (off by default):
  off, in full screen at an arcade resolution the game runs one frame per screen refresh, each shown as it refreshes
  (VSync on), so a mode that isn't exactly the board's 59.60 Hz never shows a frame twice or tears; on, the board's own
  clock as before; online, always the board's pace. Synced, on Windows, a **frame delay** reads each frame's controls
  and makes its picture as late before the refresh as it can. On Windows, **Make the mode at the board's rate** has
  [Switchres](https://github.com/antonioginer/switchres) (GroovyMAME's, downloaded at build time) make the chosen mode
  at exactly 59.60 Hz within the ranges of the **Monitor** chosen (Arcade 15 kHz, Generic 15 kHz, NTSC TV, PAL TV,
  Arcade 15/25 kHz, Arcade 15/25/31 kHz, Arcade 31 kHz) and switch the desktop to it until the game ends; if it
  can't, the mode the system lists. Both set on the launcher. The game's log says the mode chosen or made, and every
  ten seconds the rate measured. Online, the Arcade monitor page is now each player's own, as the Display page is (a
  match turned it off, which took full screen out of the arcade resolution).
- **The Discord link at the top of the launcher's Online page** ("Join us on Discord"), as cps2recomp has it.
- recomp-common synced to eac7765 (recomp-common#88 merged: the arcade monitor's Switchres modes, screen sync and
  frame delay, `arcade_screen.h`; and the launcher's Discord link for a game of its own, #87).
- **The input lag readout in a smaller font** (recomp-common's `kSmallFont`), so it covers less of the picture.
- recomp-common synced to 641c65d (the smaller font, Windows requests with WinHTTP, the arcade monitor's modes header,
  the menu's compact layout and Reset all options' question).
- **Online Tag team battle: both players choose while the game is paused**: the host's Esc menu now pauses the game
  for both players and opens the other player's, so each chooses their fighters and super arts there. The other
  player then says **Ready**, and the host's **Start Tag team battle** waits until they have (changing a fighter
  takes Ready back). The other player no longer has to choose their team before the host starts.
- **Online Tag team battle: 2 vs 2 or 3 vs 3 from the match's menu**: the host's Online match page has **Tag team:
  fighters**, which both players see change (and which takes the other player's Ready back). When the battle is
  won, the board now starts again for both, as Back to Versus does: it no longer went on into a round 2 with the
  fighters last on, and its winner into arcade mode past the first opponent select (#56).
- **FPS counter** (Display page, off by default): the frames shown each second, in the top right corner. 59 or 60 at
  the board's full speed (its screen's 59.6); fewer means this machine isn't keeping up (run-ahead, a heavy shader, a slow processor). Each
  player's own online.
- **99 save-state slots** (were 9): the menu's Slot row and F6/F7 go from 1 to 99, around both ways. States already
  saved in slots 1-9 load as before.
- **Your own shaders: one folder for every game, the preset chosen by name.** The launcher makes a `shaders` folder in
  its own folder (beside launcher.ini; updates leave it alone), and its Features page has **Your own preset** and the
  **Shaders folder** (with Open shaders folder) under Shader; the in-game menu's Display page has Your own preset too,
  listed again each time it opens. Every `.slangp` there or in a folder there is listed, so a RetroArch shader pack
  goes in whole. The choice is kept by name (`[shader] own` in settings.ini); choosing one chooses Your own. A game's
  own `shaders` folder beside its settings still works and comes first. A preset that won't load says why in the menu
  and in the log.
- **The left stick's click no longer opens the menu** unless you ask for it: a gamepad's Guide, or Start and Select
  together, open it now. The launcher's Controllers page has a **Menu button** setting: Guide, left-stick click or
  Start+Select; Guide or Start+Select (the default); or Start+Select only. Start and Select together always open it
  (Steam often takes Guide on a Steam Deck). The left stick's click held with a shoulder still zooms. Esc and F1 are
  unchanged.

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
