<!-- What this changes, and why. -->

- [ ] Nothing from a game (files, save states, settings memory, translated code, block databases, listings, dumps, screenshots other than the README's), no cipher key or anything derived from one, nothing from Capcom's own materials or a leak, no emulator's or decompilation's code. `tools/audit.sh` passes.
- [ ] What is about one game is in its `games/<id>/` (its `game.cfg` or sources), not the toolkit (`cps3recomp/`).
- [ ] If the translator, the machine, the picture, the sound or the game's code changed: `cps3recomp/tools/check.sh` and the game's checks that cover it (`games/sfiii3/check*.sh`) pass; with every feature off the game runs as before (the same movie, the same `--hash-log`).
- [ ] Gameplay and control changes are an optional feature, off in Pure.
- [ ] No added input lag or lost speed (say what was measured, if timing, input, sound or presentation changed).
- [ ] Keyboard and gamepad both still work.
- [ ] `cps3recomp/recomp-common/` is unchanged (it is a copy: change recomp-common and run `cps3recomp/tools/sync-common.sh`).
- [ ] A line in `CHANGELOG.md` under "Unreleased"; the README says what players get, if it is a main feature.
