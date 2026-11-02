# sf3comp

An unofficial native port of Capcom's **Street Fighter III: 3rd Strike** (arcade, CPS-3 board) for macOS, Linux
(Steam Deck included) and Windows, made by static recompilation: the game's SH-2 program becomes C++ that runs on a
machine written for this project, not an emulator. It adds a full training mode, online play with rollback,
widescreen, and modes the arcade game never had.

The aim is one definitive Street Fighter III: 3rd Strike's gameplay, with the stages and music of **New Generation**
and **2nd Impact** as well ([games/sfiii3/DEFINITIVE.md](games/sfiii3/DEFINITIVE.md)).

> **Early.** The whole game plays, and its translated code is checked against an interpreter instruction by
> instruction, but it has been played on few machines yet. Expect rough edges.

**No game files are included.** You need your own ROM set: `sfiii3nr1` (Japan 990512). For the other games'
stages, `sfiiin` (New Generation) and `sfiii2n` (2nd Impact) as well. The sets' cipher keys are worked out from the
sets themselves when the game starts; none are in this repository. This project is not affiliated with, endorsed by,
or sponsored by Capcom. *Street Fighter* is a trademark of Capcom Co., Ltd.

| Widescreen | Training mode | A 2nd Impact stage |
| --- | --- | --- |
| ![](docs/screenshots/widescreen.png) | ![](docs/screenshots/training.png) | ![](docs/screenshots/stage-2nd-impact.png) |

## What it has

- **A launcher and an in-game menu** (Esc or F1; on a gamepad, the left stick's click, or Start and Select
  together): every feature, save states, speed, the cheats, the board's own settings (difficulty, damage, timer,
  rounds, bonus games) and its test menu. Keyboard, gamepad or arcade stick, every button changeable per player.
- **Pure or Enhanced**: Pure plays exactly as the board does; Enhanced (the default) adds free play and your own
  character colours. Every other feature is a switch of its own.
- **Widescreen**: the stage drawn out to 16:9, or 16:9 played exactly as the board plays (invisible walls at the
  board's own ends). Shaders, scaling and picture shapes as the other recomp ports have them.
- **Online play** with rollback, by a code or the lobby, from the launcher's Online page.
- **Zoom**: the left stick's click held and a shoulder pressed zooms in on a fighter, or on both.
- **A custom soundtrack**: your own music in place of the game's, by song or as a playlist; or any stage's music
  for every fight.
- **Gill playable**, for either player.

## Training mode

A fight with no clock that nobody loses: hit boxes (projectiles' too), an input display, a frame meter and frame
data (with a log), combo, stun and parry readouts, health and super art gauges that refill, and a stage of your
choice. The dummy guards (always, after a hit, or at random), parries, red parries while guarding, escapes throws,
quick-stands, mashes out of stun, records and replays you, answers with the recording, or is played by the game.

## Alternative modes

The in-game menu's first row has ways to play and watch the game that the board does not have.

| Mode | What it does |
| --- | --- |
| Training mode | As above |
| Attract mode | The game's own show on a loop: everything, the intro only, or one demonstration fight after another |
| Stage viewer | Any stage with nobody on it and no panel, scrolled with the stick or by itself, with any stage's tune. "Infinite" goes through every stage in turn |
| Model viewer | One character alone, on a stage or plain white or black, from half its size to eight times, moved with your controls or going through any of its animations, at full speed or slower |
| Colour editor | Change a character's colours while you watch, then fight in them |
| Survival | One computer opponent after another on the health you have left |
| Endless battle | Two fighters the game plays for as long as you like, with no bars and no clock, on one stage, on stages that fade into one another, or on stages that scroll by. Touch the controls to take a fighter over |
| Ring out | No health: knock your opponent over the end of the stage, then jump down after it onto the next |
| Team battle | Two teams of two or three. **Knock-out**: one fighter of each team at a time, the next coming on when one is knocked out. **Tag**: tag your next fighter in with the left trigger (or Start); yours flies off the screen and the next jumps in from that side with an attack. Each keeps its own health and a resting one gets some back; one whose bar empties lies there a while and the next jumps in. The computer's team tags too, at good moments. **Tag team**: co-op (players 1 and 2 against the computer's team) or 2 vs 2 for four players |

## The definitive version's stages

With New Generation's and 2nd Impact's sets beside 3rd Strike's, a fight can be on one of their stages, or on each
opponent's own, with that game's music. The other game runs beside this one to draw its stage. Load ahead keeps
the stages ready between fights.

## Playing

Download the release for your system from the Releases page (macOS: an app for Apple silicon and Intel, signed ad hoc,
so Gatekeeper asks the first time; Linux x86_64, the Steam Deck too; Windows x64), unpack it and start the launcher.
In the launcher, choose `sfiii3nr1.zip` (or drop it on the launcher); the other games' sets go beside it. Settings, save states and the board's settings memory go in
your user folder; the ROM sets are only ever read.

## Building

See [docs/BUILDING.md](docs/BUILDING.md): the game's code is translated from your own ROM set, then built with CMake;
`tools/release/` makes the downloads for macOS, Linux and Windows.

```
cps3recomp/   the CPS-3 toolkit: machine/ (SH-2, board, cipher, ROM set, flash), video/, sound/, port/, tools/
games/sfiii3/ 3rd Strike: game.cfg, src/, PROGRESS.md
work/, build/ generated from your ROM set, never committed
```

GPL-3.0. Not affiliated with Capcom.
