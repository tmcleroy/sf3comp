# Third-party notices

sf3comp is licensed under the GNU General Public License v3.0 (see `LICENSE`). Most of it was written for this
project. What follows is everything that was not; the licences of all of it are compatible with the GPL-3.0.

## In this repository

### MAME's CPS-3 driver (facts and drawing rules, BSD-3-Clause)

No MAME code is here, but some of the board's code follows MAME's CPS-3 driver (`src/mame/capcom/cps3.cpp`,
`cps3_a.cpp`, by David Haywood, Andreas Naive, Tomasz Slanina and ElSemi; BSD-3-Clause) closely enough to be
credited to it: the program cipher (`cps3recomp/machine/crypt.cpp`: Andreas Naive's algorithm), how the video chip
draws its objects, tilemaps and text layer (`cps3recomp/video/renderer.cpp`; its colour fade formula also in
`cps3recomp/video/native.h`), how the sound chip's voices step
through their samples (`cps3recomp/sound/pcm.cpp`), and the memory map and DMAs (`cps3recomp/machine/machine.cpp`).
The cipher's keys are worked out from the player's own ROM set when the game starts; none are in this repository.

```
Copyright (c) David Haywood, Andreas Naive, Tomasz Slanina, ElSemi
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
   following disclaimer in the documentation and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote
   products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### This author's other projects (GPL-3.0)

The SH-2 interpreter and recompiler (`cps3recomp/machine/sh2*`, `cps3recomp/tools/recomp.py`, `sh2emit.py`,
`cps3recomp/runtime/`) come from [satrecomp](https://github.com/tmcleroy/satrecomp); the port's frontend, menu
adapter, HUD, netplay, save states, custom soundtrack and fight aids (`cps3recomp/port/`, `cps3recomp/fight/`) from
[cps2recomp](https://github.com/tmcleroy/cps2recomp). `cps3recomp/recomp-common/` is a copy of
[recomp-common](https://github.com/tmcleroy/recomp-common) (the in-game menu, the settings rules and the launcher),
made by `cps3recomp/tools/sync-common.sh` and never changed here; its launcher descends from
[wiiparty-recomp](https://github.com/arelkair/wiiparty-recomp)'s by arelkair (GPL-3.0-or-later), and its own
`THIRD_PARTY_NOTICES.md` has the table, file by file.

### Sound file decoders (public domain, or MIT / MIT-0)

| File | Project | Licence |
| --- | --- | --- |
| `cps3recomp/third_party/stb_vorbis.c` | [stb](https://github.com/nothings/stb) `stb_vorbis` 1.22, by Sean Barrett | Public domain, or MIT |
| `cps3recomp/third_party/dr_flac.h` | [dr_libs](https://github.com/mackron/dr_libs) `dr_flac` 0.13.4, by David Reid | Public domain, or MIT No Attribution |
| `cps3recomp/third_party/dr_mp3.h` | dr_libs `dr_mp3` 0.7.4, by David Reid, after minimp3 by lieff | Public domain, or MIT No Attribution |
| `cps3recomp/third_party/dr_wav.h` | dr_libs `dr_wav` 0.14.6, by David Reid | Public domain, or MIT No Attribution |

`cps3recomp/third_party/librashader.h` is librashader's C header (MIT).

### jtcores' SH7604 model (GPL-3.0-or-later)

The arcade timing feature's rules (`cps3recomp/machine/bus.h`, `cps3recomp/machine/sh2_pipeline.h`) are the SH7604's
as the SH7604 model in [jtcores](https://github.com/jotego/jtcores) (jotego's CPS-3 core; its CPU from the MiSTer
Saturn core) applies them. The table in `sh2_pipeline.h` (which registers each kind of instruction reads and loads)
was made by running that model's decoder over every opcode (`cps3recomp/tools/timing/dump.sv` and `kinds.py`, which
run against a copy of jtcores the developer fetches); no code of jtcores is in this repository. GPL-3.0-or-later is
compatible with this project's GPL-3.0.

### Facts about 3rd Strike

Names for the game's routines and variables in comments (`Player_move`, `Com_Pl.c` and the like) are the PlayStation 2
version's, as the decompilation [crowded-street/3s-decomp](https://github.com/crowded-street/3s-decomp) (AGPL-3.0)
has them: names and understanding only, no code of it is here.

Some of the places in 3rd Strike's memory the training mode reads (the players' records, their boxes, the
parry windows, the stun gauge) were taken as facts from Grouflon's
[3rd_training_lua](https://github.com/Grouflon/3rd_training_lua) (which has no licence): no code of it is here.

## Downloaded at build time

| Component | Licence | Used for |
| --- | --- | --- |
| [SDL 3](https://libsdl.org) 3.4.16 | zlib | Window, input, audio, GPU (game and launcher) |
| [Dear ImGui](https://github.com/ocornut/imgui) 1.92.9b | MIT | The launcher's interface |
| [Inter](https://rsms.me/inter/) 4.1 | SIL Open Font License 1.1 | The launcher's font |
| [zlib](https://zlib.net) | zlib | Reading a ROM set's zip, screenshots |
| [librashader](https://github.com/SnowflakePowered/librashader) 0.12.1 | MPL-2.0 or GPL-3.0 (used under GPL-3.0) | The Display page's shaders |
| [slang-shaders](https://github.com/libretro/slang-shaders) | each file its own, stated at its top | The shader presets |

The release archives carry these licence texts in `licenses/`, and `SOURCE.txt` points to this version's source.

## What is not included

Nothing from any game or board is in this repository: no ROM or part of one, no key, no graphics, sound or text,
nothing made from a game's ROMs. Code translated from a game is made on the machine of someone who has its ROM set,
and is never part of this repository; a release's program does hold it, and whether a release is given to others is
the owner's decision.
