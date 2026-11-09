# Releasing

Versions follow `MAJOR.MINOR.PATCH`, with `-beta.N` or `-rc.N` while a version is not final. A tag is `v` and the
version (`v0.1.0-beta.3`). A release with a `-` in its version is marked as a pre-release on GitHub. Versions stay
beta until every platform's download has been played by hand.

The downloads contain code translated from the game, so CI cannot build them: a maintainer with the ROM sets builds
them and attaches them to the draft that the tag opens.

## Steps

1. **In a pull request:** set `VERSION`, and move what is under "Unreleased" in `CHANGELOG.md` to a new
   `## <version>` section. `tools/audit.sh` and `tools/ci_smoke.sh` pass, and the games' checks
   (`cps3recomp/tools/check.sh` for each game, `games/sfiii3/check*.sh`) for what changed. Merge.
2. **Tag** the merged commit on `main` and push the tag:

   ```sh
   git switch main && git pull
   git tag -a "v$(cat VERSION)" -m "sf3comp $(cat VERSION)"
   git push origin "v$(cat VERSION)"
   ```

   The `Release` workflow checks that the tag is `VERSION`, runs the audit and CI's builds, and opens a **draft**
   release whose notes are `tools/release/notes.sh`'s (the downloads, how to install, the changelog's section). It
   fails if the changelog has no such section.
3. **Build the downloads** from the tagged commit, on a Mac with Docker and your own ROM sets:

   ```sh
   git status --short            # must print nothing: build exactly what was tagged
   bash cps3recomp/tools/translate.sh path/to/sfiii3nr1.zip      # and the other two games' (docs/BUILDING.md)
   tools/release/all.sh          # audit, then macOS, Linux x86_64, Windows, then SHA256SUMS.txt
   ```

   Each download is the collection: `sf3comp-launcher` and the programs `sfiii3`, `sfiiin` and `sfiii2n` (on macOS
   inside `SF3comp.app`). The scripts refuse to start if a game's translated code is missing.

   Each script refuses to pack an archive that holds game data or a personal path, or lacks a licence
   (`tools/release/check_archive.sh`).
4. **Try each download** before anyone else does: unpack it somewhere else, start the launcher, choose the ROM sets,
   play a round of each game with a gamepad and with the keyboard. macOS on a Mac, the Linux one on a Steam Deck or Steam
   Machine, the Windows one on a Windows PC.
5. **Upload and publish:**

   ```sh
   cd build/release
   gh release upload "v$(cat ../../VERSION)" SF3comp-macOS.zip SF3comp-linux-x86_64.tar.gz \
       SF3comp-windows-x64.zip SHA256SUMS.txt
   gh release edit "v$(cat ../../VERSION)" --draft=false
   ```

6. Add an empty "Unreleased" section to `CHANGELOG.md` for what comes next.

## The launcher's own update

Each script also writes `build/release/update-<system>.txt`: the download and every file in it, with checksums, as
the launcher's self-update reads them (`cps3recomp/recomp-common/launcher/src/update.h`). The launcher only looks for
updates when it is built with the repository to ask: `UPDATE_REPO=tmcleroy/sf3comp tools/release/all.sh`. That is
left off while the repository is private (the check would fail for every player), and no version released so far
has it. Once it is on, upload the `update-*.txt` files with every release, beside the downloads. A launcher updates
only a copy whose launcher is where the manifest has it (`sf3comp-launcher`).

## Coming from the versions before the collection

Up to 0.1.0-beta.2 a download was 3rd Strike alone: `Street Fighter III 3rd Strike.app` (elsewhere
`sfiii3-launcher` in the folder `SF3comp`), its things in the player's folder `Street Fighter III 3rd Strike`. The
collection is `SF3comp.app` and `sf3comp-launcher`, with the same app id. What carries over, and what to try before
publishing:

- 3rd Strike's folder is the same one, so `settings.ini` (features, keys and buttons), `saves/` and the custom
  soundtrack's folder are used as they are.
- The ROM set chosen in the old launcher, and its Widescreen and Full screen switches, are read from that folder's
  `launcher.ini` the first time the new launcher starts. New Generation's and 2nd Impact's sets are then found by
  "Look in a folder", or 3rd Strike finds them beside its own as before.
- Not carried over: the online name, a custom soundtrack folder chosen somewhere else, and the Muted switch (they
  were the old launcher's own, in its `launcher.ini`); a Steam shortcut the old launcher made (it starts the old
  launcher); and the old app or `sfiii3-launcher` itself, which the player deletes.

## If something is wrong after publishing

Don't replace the files of a published release. Fix it, raise the version (`-beta.N+1`, or the patch number) and
release again; say in the changelog what was wrong.

## Source for the binaries

The GPL-3.0 asks that the source of a binary be offered with it. The tag is that source: GitHub attaches it to every
release, and each archive's `SOURCE.txt` points to it. Build only from a tagged, clean commit so that this stays
true.
