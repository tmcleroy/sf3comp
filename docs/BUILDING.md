# Building sf3comp

You need your own ROM set: `sfiii3nr1.zip` (3rd Strike, Japan 990512). For the definitive version's stages, also
`sfiiin.zip` (New Generation) and `sfiii2n.zip` (2nd Impact), beside it. None are in this repository.

## A build for yourself (macOS, Linux)

Needs CMake, Ninja, a C++20 compiler, Python 3, SDL 3 (3.4.16 or later) and zlib.

```sh
# the game's code translated (from your ROM set; a few minutes; work/ is never committed)
bash cps3recomp/tools/translate.sh path/to/sfiii3nr1.zip
# the stage games' code too (optional: without it their boards run in the interpreter, about a fifth the speed)
GAME=sfiiin bash cps3recomp/tools/translate.sh path/to/sfiiin.zip 8000 "1 2 3 7 8 9"
GAME=sfiii2n bash cps3recomp/tools/translate.sh path/to/sfiii2n.zip 8000 "1 2 3 7 8 9"
cmake -S . -B build/sfiii3 -G Ninja && ninja -C build/sfiii3
./build/sfiii3/sfiii3-launcher
sh cps3recomp/tools/make_app.sh sfiii3     # macOS: build/sfiii3/Street Fighter III 3rd Strike.app
```

The checks: `bash cps3recomp/tools/check.sh path/to/sfiii3nr1.zip 6000 1500` (the translated code against the
interpreter, block by block) and `ROM=path/to/sfiii3nr1.zip bash cps3recomp/tools/check_online.sh` (two copies
online over a bad connection, rollback and all).

## Release builds

`tools/release/` builds one download for each system, with SDL built in and the licences beside the programs:

| Script | Where | Makes |
| --- | --- | --- |
| `macos.sh` | macOS | `build/release/SF3comp-macOS.zip` (Apple silicon and Intel) |
| `linux.sh` | Linux, x86_64 or ARM64 (Docker: cps2recomp's `cps2recomp-build` image) | `SF3comp-linux-x86_64.tar.gz` |
| `windows.sh` | Linux with MinGW-w64 (the same image) | `SF3comp-windows-x64.zip` |

Each needs the translated code (above) and Rust for librashader (`cps3recomp/tools/librashader.sh`, the Display
page's shaders). `check_archive.sh` fails an archive that holds game files or a home path, or lacks a licence. A
release's program holds code translated from the game: it is made on the machine of someone who has the ROM sets,
never by CI, and whether it is given to others is the owner's decision.
